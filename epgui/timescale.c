/*
 *  Nextview EPG GUI: PI timescale window
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation. You find a copy of this
 *  license in the file COPYRIGHT in the root directory of this release.
 *
 *  THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL,
 *  BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF
 *  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *
 *  Description:
 *
 *    Implements the so-called "timescale" popups, which have one scale
 *    for each network covered by the provider. Each one which represent
 *    the next five days; the ranges which are actually covered by PI
 *    blocks in the database are marked red or blue in the scales.
 *    The timescales can be opened separately for the browser and
 *    acquisition database.  While acquisition is running, incoming PI
 *    blocks are added to the scales.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: timescale.c,v 1.19 2020/06/17 19:32:20 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <string.h>
#include <time.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgtscqueue.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgctxmerge.h"
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgui/timescale.h"


// state of TimeScale windows
static struct
{
   bool   open;              // TRUE while window is open
   bool   locked;            // locked after provider or version change notification until rebuild
   bool   filled;            // set TRUE once the initial content is drawn; enables acq tails
   bool   isForAcq;          // ui db identical to acq db (and NOT merged)
   int    highlighted;       // index of last network marked by acq
   time_t startTime;         // for DisplayPi: start time of time scales, equal start time of oldest PI in db
   sint   widthPixels;       // width of the PI scales in pixels
   sint   widthSecs;         // width in seconds (i.e. time span covered by the entire scale)
   sint   secsPerPixel;      // scale factor for zooming
} tscaleState;

static Tcl_TimerToken scaleUpdateHandler = NULL;  // for "now" arrow in date scales

const char * const tsc_wid = ".tsc_ui";

typedef enum
{
   STREAM_COLOR_CURRENT_V1,
   //STREAM_COLOR_CURRENT_V2,
   STREAM_COLOR_OLD_V1,
   //STREAM_COLOR_OLD_V2,
   STREAM_COLOR_EXPIRED_V1,
   //STREAM_COLOR_EXPIRED_V2,
   STREAM_COLOR_DEFECTIVE,
   STREAM_COLOR_MISSING,
   STREAM_COLOR_COUNT
} STREAM_COLOR;

const char * const streamColors[STREAM_COLOR_COUNT] =
{
   "red",           // stream 1, current version -> red
   //"#4040ff",       // stream 1, current version -> blue
   "#A52A2A",       // stream 2, obsolete version -> brown
   //"#483D8B",       // stream 2, obsolete version -> DarkSlate
   "#FFC000",       // stream 1, expired -> orange
   //"#4CFFFF",       // stream 2, expired -> cyan
   "yellow",        // defect block -> yellow
   "#888888"        // missing block -> gray
};

// timescale popup configuration (must match Tcl source)
#define NOW_NEXT_BLOCK_COUNT    5
// minimum width of the scale canvas in days
#define TSC_SCALE_MIN_DAY_COUNT       2
// conversion factor between canvas and time offsets (minimum, default, maximum)
// note: the actual factor can be changed by the zoom function
#define TSC_MIN_SECS_PER_PIXEL       (5 * 60)  // must be > 0
#define TSC_DEF_SECS_PER_PIXEL      (30 * 60)
#define TSC_MAX_SECS_PER_PIXEL      (60 * 60)


// ----------------------------------------------------------------------------
// Update status line at the bottom of timescale windows
// - only called when acquisition is running for the respective database
// - status line contains the provider's network name and
//   PI fill percentages
//
static void TimeScale_UpdateStatusLine( ClientData dummy )
{
   const AI_BLOCK * pAi;
   Tcl_DString msg_dstr;
   Tcl_DString cmd_dstr;

   if (tscaleState.open)
   {
      if (EpgDbContextGetCni(pUiDbContext) == 0)
      {  // context no longer exists (e.g. acq stopped) -> close the popup
         // note: the destroy callback will be invoked and update the status variables
         sprintf(comm, "destroy %s", tsc_wid);
         eval_check(interp, comm);
      }
      else if (EpgDbContextIsMerged(pUiDbContext) == FALSE)
      {
         EpgDbLockDatabase(pUiDbContext, TRUE);
         pAi = EpgDbGetAi(pUiDbContext);
         if (pAi != NULL)
         {
            sprintf(comm, "%s", AI_GET_SERVICENAME(pAi));

            if (Tcl_ExternalToUtfDString(NULL, comm, -1, &msg_dstr) != NULL)
            {
               Tcl_DStringInit(&cmd_dstr);
               Tcl_DStringAppend(&cmd_dstr, tsc_wid, -1);
               Tcl_DStringAppend(&cmd_dstr, ".top.title configure -text", -1);
               // append message as list element, so that '{' etc. is escaped properly
               Tcl_DStringAppendElement(&cmd_dstr, Tcl_DStringValue(&msg_dstr));

               eval_check(interp, Tcl_DStringValue(&cmd_dstr));

               Tcl_DStringFree(&cmd_dstr);
               Tcl_DStringFree(&msg_dstr);
            }
         }
         EpgDbLockDatabase(pUiDbContext, FALSE);
      }
      else  // merged database
      {
         sprintf(comm, "%s.top.title configure -text {Merged database.}\n", tsc_wid);
         eval_check(interp, comm);
      }
   }
}

// ----------------------------------------------------------------------------
// Update the date scale and NOW marker in a timescale window
// - the date scale at the top of the window contains marker at the positions of
//   daybreaks (i.e. 0:00 am)
// - the "NOW" marker sets the position of the current time; unless the database
//   contains expired PI, the NOW position will be at the very left
//
static void TimeScale_UpdateDateScale( ClientData dummy )
{
   time_t now = time(NULL);
   Tcl_Obj *objv[5];
   char   str_buf[50];
   time_t toff;
   struct tm * pTm;
   uint   idx;

   if (scaleUpdateHandler != NULL)
      Tcl_DeleteTimerHandler(scaleUpdateHandler);
   scaleUpdateHandler = NULL;

   if ( tscaleState.open )
   {
      if (now >= tscaleState.startTime)
      {
         // build Tcl script to draw the data scale (use objects because the date list may get long)
         objv[0] = Tcl_NewStringObj("TimeScale_DrawDateScale", -1);
         objv[1] = Tcl_NewStringObj(tsc_wid, -1);
         objv[2] = Tcl_NewIntObj(tscaleState.widthPixels);

         toff = now - tscaleState.startTime;
         if (toff < tscaleState.widthSecs - 7)
         {
            objv[3] = Tcl_NewIntObj(toff / tscaleState.secsPerPixel);
         }
         else
         {  // "now" is off the scale -> draw a vertical arrow pointing out of the window
            objv[3] = Tcl_NewStringObj("off", -1);
         }
         objv[4] = Tcl_NewListObj(0, NULL);

         // generate list of pixel positions of daybreaks (note: cannot use constant
         // intervals because of possible intermediate daylight saving time change)
         idx = 0;
         while (1)
         {
            idx += 1;

            pTm = localtime(&tscaleState.startTime);
            // set time to midnight 0:00
            pTm->tm_sec   = 0;
            pTm->tm_min   = 0;
            pTm->tm_hour  = 0;
            // advance by +x days (note: day per month overflow allowed by mktime)
            pTm->tm_mday += idx;
            // set daylight saving time indicator to "unknown"
            pTm->tm_isdst = -1;

            toff = mktime(pTm) - tscaleState.startTime;

            if (toff < tscaleState.widthSecs)
            {
               Tcl_ListObjAppendElement(interp, objv[4],
                                        Tcl_NewIntObj(toff / tscaleState.secsPerPixel));

               sprintf(str_buf, "%d.%d.", pTm->tm_mday, pTm->tm_mon + 1);
               Tcl_ListObjAppendElement(interp, objv[4], Tcl_NewStringObj(str_buf, -1));
            }
            else
               break;
         }

         // execute the script (including inc & dec of ref' counts)
         for (idx = 0; idx < 5; idx++)
            Tcl_IncrRefCount(objv[idx]);

         if (Tcl_EvalObjv(interp, 5, objv, 0) != TCL_OK)
            debugTclErr(interp, "PiOutput-InsertText");

         for (idx = 0; idx < 5; idx++)
            Tcl_DecrRefCount(objv[idx]);
      }

      // install timer to update the position of the NOW array; the interval is
      // calculated as half of the number of seconds that are represented by one pixel
      if (scaleUpdateHandler == NULL)  // condition required because of loop
         scaleUpdateHandler =
            Tcl_CreateTimerHandler(1000 * tscaleState.secsPerPixel / 2,
                                   TimeScale_UpdateDateScale, NULL);
   }
}

// ----------------------------------------------------------------------------
// Translate timescale pixel offset to absolute start time
// - used by button-press callback in timescales -> set cursor on the PI with
//   the given network and start time
// - return 0 upon error or if the given scale does not refer to the UI db
//
static int TimeScale_GetTime( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_TimeScale_GetTime <window> <offset>";
   time_t reqtime;
   int poff;
   int result;

   if (objc != 3)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetIntFromObj(interp, objv[2], &poff) != TCL_OK)
   {  // string parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else
   {
      if ( tscaleState.open )
      {
        reqtime = tscaleState.startTime + (poff * tscaleState.secsPerPixel);
      }
      else
      {
        debug1("TimeScale-GetTime: scale %s not open", Tcl_GetString(objv[1]));
        reqtime = 0;
      }

      Tcl_SetObjResult(interp, Tcl_NewIntObj(reqtime));

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Update connection between timescale windows and acq
// - updates are requested, if the ACQ timscale window is open or
//   if acquisition happens to run for the current UI provider
//
static void TimeScale_RequestAcq( void )
{
#if 0  // TODO TTX
   EPGACQ_DESCR acqState;
   bool isForAcq;

   if (tscaleState.open)
   {
      EpgAcqCtl_DescribeAcqState(&acqState);

      tscaleState.isForAcq = (acqState.ttxGrabState != ACQDESCR_DISABLED) &&
                             EpgSetup_IsAcqWorkingForUiDb();
   }
#endif
}

#if 0 //TODO TTX
// ----------------------------------------------------------------------------
// mark the last netwop that a PI was received from
// - can be called with invalid netwop index to unmark last netwop only
//
static void TimeScale_HighlightNetwop( uchar netwop )
{
   if (netwop != tscaleState.highlighted)
   {
      // remove the highlighting from the previous netwop
      if (tscaleState.highlighted != 0xff)
      {
         sprintf(comm, "TimeScale_MarkNet %s %d $::text_fg",
                       tsc_wid, tscaleState.highlighted);
         eval_global(interp, comm);

         tscaleState.highlighted = 0xff;
      }

      // highlight the new netwop
      if (netwop != 0xff)
      {
         sprintf(comm, "TimeScale_MarkNet %s %d %s",
                       tsc_wid, netwop,
                       streamColors[STREAM_COLOR_CURRENT_V1]);
         eval_check(interp, comm);

         tscaleState.highlighted = netwop;
      }
   }
}
#endif

// ----------------------------------------------------------------------------
// Display PI time range
// - converts start time into scale coordinates
//
static void TimeScale_DisplayPi( STREAM_COLOR streamCol, uchar netwop,
                                 time_t start, time_t stop, uchar hasDesc, bool isLast )
{
   time_t now;
   sint corrOff;
   sint startOff, stopOff;

   if (streamCol < STREAM_COLOR_COUNT)
   {
      if (tscaleState.startTime == 0)
      {  // base time not yet set (empty db or network mode) -> set now
         // note: this guess can be wrong because the first PI is not neccessarily
         // the oldest if the db is incomplete; but we can correct the estimation later
         now = time(NULL);
         if (start < now)
            tscaleState.startTime = start;
         else
            tscaleState.startTime = now;

         TimeScale_UpdateDateScale(NULL);
      }
      else if (start < tscaleState.startTime)
      {  // this PI starts before the current beginning of the scales -> shift to the right
         // convert start time difference to pixel correction offset, rounding up
         corrOff = ((tscaleState.startTime - start) + tscaleState.secsPerPixel-1) / tscaleState.secsPerPixel;
         debug2("TimeScale-DisplayPi: shift timescale by %ld minutes = %d pixels", (tscaleState.startTime - start) / 60, corrOff);

         sprintf(comm, "TimeScale_ShiftRight %s %d\n", tsc_wid, corrOff);
         eval_check(interp, comm);

         // set start time to the exact equivalence of the pixel offset
         tscaleState.startTime -= corrOff * tscaleState.secsPerPixel;
         TimeScale_UpdateDateScale(NULL);
      }

      start -= tscaleState.startTime;

      // check if the PI lies inside the visible range
      if ( (start < tscaleState.widthSecs) &&
           (stop > tscaleState.startTime) )
      {
         stop -= tscaleState.startTime;
         if (stop > tscaleState.widthSecs)
            stop = tscaleState.widthSecs;

         startOff = (start + tscaleState.secsPerPixel/2) / tscaleState.secsPerPixel;
         stopOff  = (stop + tscaleState.secsPerPixel/2)  / tscaleState.secsPerPixel;

         sprintf(comm, "TimeScale_AddRange %s %d %d %d %s %d %d\n",
                 tsc_wid, netwop, startOff, stopOff,
                 streamColors[streamCol], hasDesc, isLast);
         eval_check(interp, comm);

         if (tscaleState.filled && tscaleState.isForAcq)
         {
            sprintf(comm, "TimeScale_AddTail %s %d %d %d\n", tsc_wid, netwop, startOff, stopOff);
            eval_check(interp, comm);
         }
      }
      else
         dprintf1("TimeScale-DisplayPi: PI is beyond the scale by %ld minutes\n", (start - tscaleState.widthSecs) / 60);
   }
   else
      fatal1("TimeScale-DisplayPi: illegal color idx=%d", streamCol);
}

// ----------------------------------------------------------------------------
// Fill timescale window with range info from a PI timescale queue
//
static void TimeScale_AddPi( EPGDB_PI_TSC * ptsc )
{
   const EPGDB_PI_TSC_ELEM * pPt;
   STREAM_COLOR col;
   time_t startTime, stopTime, baseTime;

   tscaleState.filled = FALSE;

   while ((pPt = EpgTscQueue_PopElem(ptsc, &baseTime)) != NULL)
   {
      startTime = baseTime + pPt->startOffMins * 60;
      stopTime  = startTime + pPt->durationMins * 60;

      if (pPt->flags & PI_TSC_MASK_IS_MISSING)
         col = STREAM_COLOR_MISSING;
      else if (pPt->flags & PI_TSC_MASK_IS_DEFECTIVE)
         col = STREAM_COLOR_DEFECTIVE;
      else if (pPt->flags & PI_TSC_MASK_IS_EXPIRED)
      {
         col = STREAM_COLOR_EXPIRED_V1;
      }
      else if (pPt->flags & PI_TSC_MASK_IS_CUR_VERSION)
      {
         col = STREAM_COLOR_CURRENT_V1;
      }
      else
      {
         col = STREAM_COLOR_OLD_V1;
      }

      TimeScale_DisplayPi(col, pPt->netwop, startTime, stopTime,
                          pPt->flags & PI_TSC_MASK_HAS_DESC_TEXT,
                          pPt->flags & PI_TSC_MASK_IS_LAST);
   }
   sprintf(comm, "TimeScale_RaiseTail %s\n", tsc_wid);
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Helper function: determine width of the timescale window
//
static uint TimeScale_GetScaleWidth( EPGDB_CONTEXT * dbc )
{
   const PI_BLOCK * pFirstPi;
   const PI_BLOCK * pLastPi;
   uint  result;

   EpgDbLockDatabase(dbc, TRUE);

   // Determine width by covered time
   pFirstPi = EpgDbSearchFirstPi(dbc, NULL);
   pLastPi = EpgDbSearchLastPi(dbc, NULL);
   // note we get either first and last or none, so we need to handle just these 2 cases
   if ((pFirstPi != NULL) && (pLastPi != NULL))
   {
      result = pLastPi->stop_time - pFirstPi->start_time + 24*60*60;
   }
   else
   {
      result = 24*60*60;
   }
   EpgDbLockDatabase(dbc, FALSE);

   dprintf1("TimeScale-GetScaleWidth: %d days\n", result / (24*60*60));
   return result;
}

// ----------------------------------------------------------------------------
// Update time scale popup window for new provider or AI version
// - the network table is updated; rows added or deleted if required
// - all scales are cleared and refilled (this is required even when the
//   provider has not changed because the time base might have changed,
//   i.e. expired PI might have been dropped)
//
static void TimeScale_CreateOrRebuild( ClientData dummy )
{
   const PI_BLOCK  * pPi;
   EPGDB_PI_TSC      tsc;
   time_t now = time(NULL);

   if ( tscaleState.open && tscaleState.locked )
   {
      // clear the lock
      tscaleState.locked = FALSE;

      if ((pUiDbContext != NULL) && (EpgDbContextGetCni(pUiDbContext) != 0))
      {
         // initialize/reset state struct
         // (note: conversion factor secsPerPixel is reset only upon initial display or prov change)
         tscaleState.highlighted = 0xff;
         tscaleState.filled      = FALSE;
         tscaleState.isForAcq    = FALSE;
         tscaleState.widthSecs   = TimeScale_GetScaleWidth(pUiDbContext);
         tscaleState.widthPixels = tscaleState.widthSecs / tscaleState.secsPerPixel;

         // create (or update) network table: each row has a label and scale
         sprintf(comm, "TimeScale_Open %s 0x%04X %d\n",
                       tsc_wid, EpgDbContextGetCni(pUiDbContext), tscaleState.widthPixels);
         eval_check(interp, comm);

         EpgDbLockDatabase(pUiDbContext, TRUE);
         pPi = EpgDbSearchFirstPi(pUiDbContext, NULL);
         if (pPi != NULL)
         {
            dprintf0("TimeScale-InitialFill: fill scale\n");
            // calculate new scale start time: now or oldest PI start time
            // note: if db is not complete the first PI might not be the oldest one; this is corrected later
            if (pPi->start_time < now)
               tscaleState.startTime = pPi->start_time;
            else
               tscaleState.startTime = now;

            // initialize the scales with the PI already in the db
            EpgTscQueue_Init(&tsc);
            EpgTscQueue_AddAll(&tsc, pUiDbContext);

            EpgTscQueue_SetProvCni(&tsc, EpgDbContextGetCni(pUiDbContext));
            TimeScale_AddPi(&tsc);

            // just to catch error cases: clear remaining data (normally all data should have been processed and freed)
            EpgTscQueue_Clear(&tsc);

            // set NOW marker
            TimeScale_UpdateDateScale(NULL);
         }
         else
         {
            dprintf0("TimeScale-InitialFill: no PI - wait for acq\n");
            tscaleState.startTime = 0;

            sprintf(comm, "TimeScale_ClearDateScale %s %d\n", tsc_wid, tscaleState.widthPixels);
            eval_check(interp, comm);
         }

         EpgDbLockDatabase(pUiDbContext, FALSE);

         // display summary at the bottom of the window
         TimeScale_UpdateStatusLine(NULL);

         // update connection between timescale windows and acq
         // note: must be alled after acq queue is processed (b/c is-For-Acq flags)
         TimeScale_RequestAcq();
      }
      else
      {  // context no longer exists (e.g. acq stopped) -> close the popup
         // note: the destroy callback will be invoked and update the status variables
         sprintf(comm, "destroy %s", tsc_wid);
         eval_check(interp, comm);
      }
   }
}

// ----------------------------------------------------------------------------
// Open or close the timescales popup window
// - called by the "View timescales" entry in the Control menu
// - also called when the window is destroyed (e.g. via the wm "Close" cmd)
// - can not be opened until an AI block has been received
//   because we need to know the number and names of netwops in the EPG
//
static int TimeScale_Toggle( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_TimeScale_Toggle [0|1]";
   uint provCni;
   int newState;
   int result;

   provCni = 0;  // avoid compiler warnings
   result = TCL_OK;

   if ((objc == 1) || (objc == 2))
   {
      provCni = EpgDbContextGetCni(pUiDbContext);

      // determine new state from optional second parameter: 0, 1 or toggle
      if (objc == 1)
      {  // no parameter -> toggle current state
         newState = ! tscaleState.open;
      }
      else
      {
         result = Tcl_GetBooleanFromObj(interp, objv[1], &newState);
      }
   }
   else
      result = TCL_ERROR;

   if (result == TCL_OK)
   {
      // check if the window already is in the user-requested state
      if (newState != tscaleState.open)
      {
         if (tscaleState.open == FALSE)
         {  // user requests to open a new window
            // refuse request if no AI is available (required for netwop list)
            if (provCni != 0)
            {
               tscaleState.highlighted = 0xff;
               tscaleState.open   = TRUE;
               tscaleState.locked = TRUE;
               tscaleState.secsPerPixel = TSC_DEF_SECS_PER_PIXEL;

               // create the window and its content
               TimeScale_CreateOrRebuild(NULL);
            }
         }
         else
         {  // user requests to destroy the window

            // note: set to FALSE before window destruction to avoid recursion
            tscaleState.open = FALSE;

            sprintf(comm, "destroy %s; update", tsc_wid);
            eval_check(interp, comm);

            // update connection between timescale windows and acq
            TimeScale_RequestAcq();
         }
         sprintf(comm, "set menuStatusTscaleOpen %d\n", tscaleState.open);
         eval_check(interp, comm);
      }
   }
   else
   {  // error of any kind -> display usage
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Zoom a timescale window horizontally
//
static int TimeScale_Zoom( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_TimeScale_Zoom <step>";  // TODO param obsolete
   sint  newFac;
   int   step;
   int   result = TCL_ERROR;

   if (objc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetIntFromObj(interp, objv[1], &step) != TCL_OK)
   {  // string parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else
   {
      newFac = tscaleState.secsPerPixel;
      if (step > 0)
         newFac = (newFac * 2) / 3;
      else if (step < 0)
         newFac = (newFac * 3 + 1) / 2;
      // round up/down to the closest minute
      newFac = newFac - ((newFac + 30) % 60);

      if (newFac < TSC_MIN_SECS_PER_PIXEL)
         newFac = TSC_MIN_SECS_PER_PIXEL;
      else if (newFac > TSC_MAX_SECS_PER_PIXEL)
          newFac = TSC_MAX_SECS_PER_PIXEL;

      if (newFac != tscaleState.secsPerPixel)
      {
         tscaleState.secsPerPixel = newFac;

         tscaleState.locked = TRUE;
         AddMainIdleEvent(TimeScale_CreateOrRebuild, NULL, TRUE);
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Notify the windows of this module about a database provider change
// - when provider changes, the respective window is rebuilt
// - when acq shifts from ui db to a separate acq db, the highlighted
//   netwop in the ui window must be unmarked
//
void TimeScale_ProvChange( void )
{
   dprintf0("TimeScale-ProvChange\n");

   // update & reload the respective network timescale popup
   // when the context is closed the popup window is destroyed
   if (tscaleState.open)
   {
      tscaleState.locked = TRUE;
      tscaleState.secsPerPixel = TSC_DEF_SECS_PER_PIXEL;
      AddMainIdleEvent(TimeScale_CreateOrRebuild, NULL, TRUE);
   }
}

// ----------------------------------------------------------------------------
// Notify timescale windows about an AI version change in the acq database
// - number and names of netwops might have changed,
//   range of valid PI might have changed, and PI hence gotten removed
// - scale contents have to be recolored (shaded) to reflect obsolete version
// - the timescale queue (filled by background acquisition) processing is
//   locked until the window is updated
//
void TimeScale_VersionChange( uint acqCni )
{
   dprintf1("TimeScale-VersionChange: acqCni:0x%X\n", acqCni);

   if ( tscaleState.open )
   {
      if ( EpgDbContextIsMerged(pUiDbContext)
              ? EpgContextMergeCheckForCni(pUiDbContext, acqCni)
              : (acqCni == EpgDbContextGetCni(pUiDbContext)) )
      {
         tscaleState.locked = TRUE;
         AddMainIdleEvent(TimeScale_CreateOrRebuild, NULL, TRUE);
      }
   }
}

// ----------------------------------------------------------------------------
// Acq Event Handler: Schedule an update of db stats
// - only relevant for timescale status line
// - called regularily when acquisition received a new AI (of the same provider)
//   or after acquisition parameters are changed by the user
// - execution is delayed until acq control has finished processing new blocks
//
void TimeScale_AcqStatsUpdate( void )
{
   if (tscaleState.open)
   {
      dprintf0("TimeScale-AcqStatsUpdate: scheduling update\n");

      AddMainIdleEvent(TimeScale_UpdateStatusLine, NULL, TRUE);
   }
}

// ----------------------------------------------------------------------------
// Initialize module state variables
// - this should be called only once during start-up
//
void TimeScale_Create( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_TimeScale_Toggle", &cmdInfo) == 0)
   {
      Tcl_CreateObjCommand(interp, "C_TimeScale_Toggle", TimeScale_Toggle, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_TimeScale_GetTime", TimeScale_GetTime, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_TimeScale_Zoom", TimeScale_Zoom, (ClientData) NULL, NULL);
   }
   else
      fatal0("TimeScale-Create: commands are already created");

   memset(&tscaleState, 0, sizeof(tscaleState));
}

