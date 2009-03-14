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
 *  $Id: timescale.c,v 1.17 2008/10/19 13:27:32 tom Exp tom $
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
   bool   isForAcq;          // ui target only: ui db identical to acq db (and NOT merged)
   uchar  lastStream;        // index of last received stream
   int    highlighted;       // index of last network marked by acq
   time_t startTime;         // for DisplayPi: start time of time scales, equal start time of oldest PI in db
   sint   widthPixels;       // width of the PI scales in pixels
   sint   widthSecs;         // width in seconds (i.e. time span covered by the entire scale)
   sint   secsPerPixel;      // scale factor for zooming
} tscaleState[2];

static Tcl_TimerToken scaleUpdateHandler = NULL;  // for "now" arrow in date scales

const char * const tscn[2] =
{
   ".tsc_ui",
   ".tsc_acq"
};

typedef enum
{
   STREAM_COLOR_CURRENT_V1,
   STREAM_COLOR_CURRENT_V2,
   STREAM_COLOR_OLD_V1,
   STREAM_COLOR_OLD_V2,
   STREAM_COLOR_EXPIRED_V1,
   STREAM_COLOR_EXPIRED_V2,
   STREAM_COLOR_DEFECTIVE,
   STREAM_COLOR_MISSING,
   STREAM_COLOR_COUNT
} STREAM_COLOR;

const char * const streamColors[STREAM_COLOR_COUNT] =
{
   "red",           // stream 1, current version -> red
   "#4040ff",       // stream 1, current version -> blue
   "#A52A2A",       // stream 2, obsolete version -> brown
   "#483D8B",       // stream 2, obsolete version -> DarkSlate
   "#FFC000",       // stream 1, expired -> orange
   "#4CFFFF",       // stream 2, expired -> cyan
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
//   PI fill percentages for stream 1 and stream 1+2
//
static void TimeScale_UpdateStatusLine( ClientData dummy )
{
   EPGDB_CONTEXT * pAcqDbContext;
   EPGDB_CONTEXT * dbc;
   const AI_BLOCK * pAi;
   EPGDB_BLOCK_COUNT count[2];
   ulong total, curVersionCount;
   uint nearPerc;
   int target;
   Tcl_DString msg_dstr;
   Tcl_DString cmd_dstr;

   pAcqDbContext = EpgAcqCtl_GetDbContext(TRUE);

   for (target=0; target < 2; target++)
   {
      if (tscaleState[target].open)
      {
         dbc = ((target == DB_TARGET_UI) ? pUiDbContext : pAcqDbContext);

         if (EpgDbContextGetCni(dbc) == 0)
         {  // context no longer exists (e.g. acq stopped) -> close the popup
            // note: the destroy callback will be invoked and update the status variables
            sprintf(comm, "destroy %s", tscn[target]);
            eval_check(interp, comm);
         }
         else if ( (EpgDbContextIsMerged(dbc) == FALSE) &&
                   (EpgDbContextIsXmltv(dbc) == FALSE) )
         {
            EpgDbLockDatabase(dbc, TRUE);
            pAi = EpgDbGetAi(dbc);
            if (pAi != NULL)
            {
               if ( (target == DB_TARGET_ACQ) || (pAcqDbContext == pUiDbContext) )
               {
                  EpgAcqCtl_GetDbStats(count, NULL);
               }
               else
               {  // acq not running for this database
                  time_t acqMinTime[2];
                  memset(acqMinTime, 0, sizeof(acqMinTime));
                  EpgDbGetStat(dbc, count, acqMinTime, 0);
               }

               total            = count[0].ai + count[1].ai;
               curVersionCount  = count[0].curVersion + count[0].expired + count[0].defective +
                                  count[1].curVersion + count[1].expired + count[1].defective;

               if (curVersionCount < total)
               {  // db not complete -> print percentage for far & near

                  if (count[0].ai > 0)
                     nearPerc = (int)((double)(count[0].expired + count[0].defective +
                                               count[0].curVersion) * 100.0 / count[0].ai);
                  else
                     nearPerc = 100;

                  sprintf(comm, "%s database %d%% complete, near data %d%%.",
                                AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop),
                                (int)((double)curVersionCount * 100.0 / total), nearPerc);
               }
               else
               {
                  sprintf(comm, "%s database 100%% complete.",
                                AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
               }

               if (Tcl_ExternalToUtfDString(NULL, comm, -1, &msg_dstr) != NULL)
               {
                  Tcl_DStringInit(&cmd_dstr);
                  Tcl_DStringAppend(&cmd_dstr, tscn[target], -1);
                  Tcl_DStringAppend(&cmd_dstr, ".top.title configure -text", -1);
                  // append message as list element, so that '{' etc. is escaped properly
                  Tcl_DStringAppendElement(&cmd_dstr, Tcl_DStringValue(&msg_dstr));

                  eval_check(interp, Tcl_DStringValue(&cmd_dstr));

                  Tcl_DStringFree(&cmd_dstr);
                  Tcl_DStringFree(&msg_dstr);
               }
            }
            EpgDbLockDatabase(dbc, FALSE);
         }
         else if (EpgDbContextIsMerged(dbc))
         {
            sprintf(comm, "%s.top.title configure -text {Merged database.}\n", tscn[target]);
            eval_check(interp, comm);
         }
         else
         {
            sprintf(comm, "%s.top.title configure -text {Imported database.}\n", tscn[target]);
            eval_check(interp, comm);
         }
      }
   }

   EpgAcqCtl_GetDbContext(FALSE);
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
   uchar   str_buf[50];
   time_t toff;
   struct tm * pTm;
   uint   target, idx;
   uint   strOff;

   if (scaleUpdateHandler != NULL)
      Tcl_DeleteTimerHandler(scaleUpdateHandler);
   scaleUpdateHandler = NULL;

   for (target=0; target < 2; target++)
   {
      if ( tscaleState[target].open )
      {
         if (now >= tscaleState[target].startTime)
         {
            // build Tcl script to draw the data scale (use objects because the date list may get long)
            objv[0] = Tcl_NewStringObj("TimeScale_DrawDateScale", -1);
            objv[1] = Tcl_NewStringObj(tscn[target], -1);
            objv[2] = Tcl_NewIntObj(tscaleState[target].widthPixels);

            toff = now - tscaleState[target].startTime;
            if (toff < tscaleState[target].widthSecs - 7)
            {
               objv[3] = Tcl_NewIntObj(toff / tscaleState[target].secsPerPixel);
            }
            else
            {  // "now" is off the scale -> draw a vertical arrow pointing out of the window
               objv[3] = Tcl_NewStringObj("off", -1);
            }
            objv[4] = Tcl_NewListObj(0, NULL);

            // generate list of pixel positions of daybreaks (note: cannot use constant
            // intervals because of possible intermediate daylight saving time change)
            strOff = strlen(comm);
            idx = 0;
            while (1)
            {
               idx += 1;

               pTm = localtime(&tscaleState[target].startTime);
               // set time to midnight 0:00
               pTm->tm_sec   = 0;
               pTm->tm_min   = 0;
               pTm->tm_hour  = 0;
               // advance by +x days (note: day per month overflow allowed by mktime)
               pTm->tm_mday += idx;
               // set daylight saving time indicator to "unknown"
               pTm->tm_isdst = -1;

               toff = mktime(pTm) - tscaleState[target].startTime;

               if (toff < tscaleState[target].widthSecs)
               {
                  Tcl_ListObjAppendElement(interp, objv[4],
                                           Tcl_NewIntObj(toff / tscaleState[target].secsPerPixel));

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
               Tcl_CreateTimerHandler(1000 * tscaleState[target].secsPerPixel / 2,
                                      TimeScale_UpdateDateScale, NULL);
      }
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
   uint target;
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
      if (strncmp(tscn[DB_TARGET_UI], Tcl_GetString(objv[1]), strlen(tscn[DB_TARGET_UI])) == 0)
        target = DB_TARGET_UI;
      else
        target = DB_TARGET_ACQ;

      if ( tscaleState[target].open )
      {
        if ( (target == DB_TARGET_UI) ||
             (EpgAcqCtl_GetProvCni() == EpgDbContextGetCni(pUiDbContext)) )
        {
           reqtime  = tscaleState[target].startTime + (poff * tscaleState[target].secsPerPixel);
        }
        else
        {  // not the UI database -> return error code
           reqtime = 0;
        }
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
   bool isForAcq;
   bool enable;
   bool allProviders;

   enable = allProviders = FALSE;

   if (tscaleState[DB_TARGET_ACQ].open)
   {  // ACQ timescales are open -> forward any incoming data
      allProviders = TRUE;
      enable       = TRUE;
   }

   if (tscaleState[DB_TARGET_UI].open)
   {  // UI timescales are open
      if ( (EpgDbContextIsMerged(pUiDbContext) == FALSE) &&
           (EpgDbContextIsXmltv(pUiDbContext) == FALSE) )
      {  // normal db -> forward incoming data if it's for the same db
         isForAcq = (EpgAcqCtl_GetProvCni() == EpgDbContextGetCni(pUiDbContext));
         enable |= isForAcq;
         // remember request state in the timscale state struct
         tscaleState[DB_TARGET_UI].isForAcq = isForAcq;
      }
      else if ( EpgDbContextIsMerged(pUiDbContext) )
      {  // merged db -> forward timescale info for any newly merged PI
         EpgContextMergeEnableTimescale(pUiDbContext, TRUE);

         // no immediate connection between acq and merged db
         tscaleState[DB_TARGET_UI].isForAcq = FALSE;
      }
      else
      {
         tscaleState[DB_TARGET_UI].isForAcq = FALSE;
      }
   }
   else if (EpgDbContextIsMerged(pUiDbContext))
   {  // UI timscales are no open -> disable timescale generation for merged db
      EpgContextMergeEnableTimescale(pUiDbContext, FALSE);
   }

   EpgAcqCtl_EnableTimescales(enable, allProviders);
}

// ----------------------------------------------------------------------------
// mark the last netwop that a PI was received from
// - can be called with invalid stream index to unmark last netwop only
//
static void TimeScale_HighlightNetwop( int target, uchar netwop, uchar stream )
{
   if ( (netwop != tscaleState[target].highlighted) ||
        (stream != tscaleState[target].lastStream) )
   {
      // remove the highlighting from the previous netwop
      if (tscaleState[target].highlighted != 0xff)
      {
         sprintf(comm, "TimeScale_MarkNet %s %d $::text_fg",
                       tscn[target], tscaleState[target].highlighted);
         eval_global(interp, comm);

         tscaleState[target].highlighted = 0xff;
         tscaleState[target].lastStream  = 0xff;
      }

      // highlight the new netwop
      if (stream < 2)
      {
         sprintf(comm, "TimeScale_MarkNet %s %d %s",
                       tscn[target], netwop,
                       (stream == 0) ? streamColors[STREAM_COLOR_CURRENT_V1] : streamColors[STREAM_COLOR_CURRENT_V2]);
         eval_check(interp, comm);

         tscaleState[target].highlighted = netwop;
         tscaleState[target].lastStream  = stream;
      }
   }
}

// ----------------------------------------------------------------------------
// Display PI time range
// - converts start time into scale coordinates
//
static void TimeScale_DisplayPi( int target, STREAM_COLOR streamCol, uchar netwop,
                                 time_t start, time_t stop, uchar hasShortInfo, uchar hasLongInfo, bool isLast )
{
   time_t now;
   sint corrOff;
   sint startOff, stopOff;

   if ((target < 2) && (streamCol < STREAM_COLOR_COUNT))
   {
      if (tscaleState[target].startTime == 0)
      {  // base time not yet set (empty db or network mode) -> set now
         // note: this guess can be wrong because the first PI is not neccessarily
         // the oldest if the db is incomplete; but we can correct the estimation later
         now = time(NULL);
         if (start < now)
            tscaleState[target].startTime = start;
         else
            tscaleState[target].startTime = now;

         TimeScale_UpdateDateScale(NULL);
      }
      else if (start < tscaleState[target].startTime)
      {  // this PI starts before the current beginning of the scales -> shift to the right
         // convert start time difference to pixel correction offset, rounding up
         corrOff = ((tscaleState[target].startTime - start) + tscaleState[target].secsPerPixel-1) / tscaleState[target].secsPerPixel;
         debug2("TimeScale-DisplayPi: shift timescale by %ld minutes = %d pixels", (tscaleState[target].startTime - start) / 60, corrOff);

         sprintf(comm, "TimeScale_ShiftRight %s %d\n", tscn[target], corrOff);
         eval_check(interp, comm);

         // set start time to the exact equivalence of the pixel offset
         tscaleState[target].startTime -= corrOff * tscaleState[target].secsPerPixel;
         TimeScale_UpdateDateScale(NULL);
      }

      start -= tscaleState[target].startTime;

      // check if the PI lies inside the visible range
      if ( (start < tscaleState[target].widthSecs) &&
           (stop > tscaleState[target].startTime) )
      {
         stop -= tscaleState[target].startTime;
         if (stop > tscaleState[target].widthSecs)
            stop = tscaleState[target].widthSecs;

         startOff = (start + tscaleState[target].secsPerPixel/2) / tscaleState[target].secsPerPixel;
         stopOff  = (stop + tscaleState[target].secsPerPixel/2)  / tscaleState[target].secsPerPixel;

         sprintf(comm, "TimeScale_AddRange %s %d %d %d %s %d %d %d\n",
                 tscn[target], netwop, startOff, stopOff,
                 streamColors[streamCol],
                 hasShortInfo, hasLongInfo, isLast);
         eval_check(interp, comm);

         if (tscaleState[target].filled && tscaleState[target].isForAcq)
         {
            if (streamCol == STREAM_COLOR_CURRENT_V1)
            {
               sprintf(comm, "TimeScale_AddTail %s %d %d %d 0\n", tscn[target], netwop, startOff, stopOff);
               eval_check(interp, comm);
            }
            else if (streamCol == STREAM_COLOR_CURRENT_V2)
            {
               sprintf(comm, "TimeScale_AddTail %s %d %d %d 1\n", tscn[target], netwop, startOff, stopOff);
               eval_check(interp, comm);
            }
         }
      }
      else
         dprintf1("TimeScale-DisplayPi: PI is beyond the scale by %ld minutes\n", (start - tscaleState[target].widthSecs) / 60);
   }
   else
      fatal2("TimeScale-DisplayPi: illegal target=%d or color idx=%d", target, streamCol);
}

// ----------------------------------------------------------------------------
// Fill timescale window with range info from a PI timescale queue
//
static void TimeScale_AddPi( int target, EPGDB_PI_TSC * ptsc )
{
   const EPGDB_PI_TSC_ELEM * pPt;
   STREAM_COLOR col;
   time_t startTime, stopTime, baseTime;
   uint   blockIdx;
   sint   idx;

   tscaleState[target].filled = EpgTscQueue_IsIncremental(ptsc);

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
         // stream distinction is meaningless for old database version
         if ( (pPt->flags & PI_TSC_MASK_IS_STREAM_1) ||
              !(pPt->flags & PI_TSC_MASK_IS_CUR_VERSION) )
            col = STREAM_COLOR_EXPIRED_V1;
         else
            col = STREAM_COLOR_EXPIRED_V2;
      }
      else if (pPt->flags & PI_TSC_MASK_IS_CUR_VERSION)
      {
         if (pPt->flags & PI_TSC_MASK_IS_STREAM_1)
            col = STREAM_COLOR_CURRENT_V1;
         else
            col = STREAM_COLOR_CURRENT_V2;
      }
      else
      {
         if (pPt->flags & PI_TSC_MASK_IS_STREAM_1)
            col = STREAM_COLOR_OLD_V1;
         else
            col = STREAM_COLOR_OLD_V2;
      }

      for (idx=0; idx < 2; idx++)
      {
         if ((idx == target) || (target == DB_TARGET_BOTH))
         {
            TimeScale_DisplayPi(idx, col, pPt->netwop, startTime, stopTime,
                                pPt->flags & PI_TSC_MASK_HAS_SHORT_I,
                                pPt->flags & PI_TSC_MASK_HAS_LONG_I,
                                pPt->flags & PI_TSC_MASK_IS_LAST);

            // The first 5 blocks (according to the numbers in the AI per netwop) are
            // additionally inserted to the 5 separate buttons in front of the scales
            // to reflect NOW coverage.
            for ( blockIdx = pPt->blockIdx;
                     (blockIdx < NOW_NEXT_BLOCK_COUNT) &&
                     (blockIdx < (uint)pPt->blockIdx + pPt->concatCount);
                        blockIdx++ )
            {
               sprintf(comm, "TimeScale_MarkNow %s %d %d %s\n",
                             tscn[idx], pPt->netwop, blockIdx, streamColors[col]);
               eval_check(interp, comm);
            }
         }
      }
   }
   sprintf(comm, "TimeScale_RaiseTail %s\n", tscn[target]);
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Fetch timescale queue from merge context and update the UI timescale window
// - invoked as main idle event after notification by merge context
// - similar to timescale handling for normal db, but with a different queue
// - NOTE: for the merged db no netwop highlighting is added, because the
//   scales are not notified about every incoming PI, but only about actual PI
//   additions to the db
//
static void TimeScale_ProcessMergeQueue( ClientData dummy )
{
   EPGDB_PI_TSC * ptsc;

   if ( EpgDbContextIsMerged(pUiDbContext) )
   {
      ptsc = EpgContextMergeGetTimescaleQueue(pUiDbContext);
      if (ptsc != NULL)
      {
         if ( tscaleState[DB_TARGET_UI].open )
         {
            if (tscaleState[DB_TARGET_UI].locked == FALSE)
            {
               EpgTscQueue_SetProvCni(ptsc, EpgDbContextGetCni(pUiDbContext));
               TimeScale_AddPi(DB_TARGET_UI, ptsc);

               ifdebug0(EpgTscQueue_HasElems(ptsc), "TimeScale-ProcessMergeQueue: after prcessing, discard remaining data");
               EpgTscQueue_ClearUnprocessed(ptsc);
            }
         }
         else
         {  // timescales are no longer open -> discard remaining data
            ifdebug0(EpgTscQueue_HasElems(ptsc), "TimeScale-ProcessMergeQueue: not open: discard remaining data");
            EpgTscQueue_Clear(ptsc);
         }
      }
   }
   // note: when the merge context is closed the tsc queue is cleared, so the error case need not be handled here
}

// ----------------------------------------------------------------------------
// Fetch timescale queue from acq and update the windows
// - invoked as main idle event after notification by acq module
//   also invoked when a scale is unlocked
// - updates are locked after a provider or AI version change in any timescale
//   window, i.e. no updates occur until both windows are unlocked (or closed)
// - this function also handles UI timescale cleanup after acq has moved to
//   a different db
//
static void TimeScale_ProcessAcqQueue( ClientData dummy )
{
   EPGDB_PI_TSC * ptsc;
   const EPGDB_PI_TSC_ELEM *pPt;
   uchar stream, netwop;
   uint acqCni;
   bool isUiDb;

   // get queue handle from the acq module
   ptsc = EpgAcqCtl_GetTimescaleQueue();
   if ((ptsc != NULL) && (EpgAcqCtl_GetProvCni() != 0))
   {
      acqCni = EpgAcqCtl_GetProvCni();
      isUiDb = ((EpgDbContextGetCni(pUiDbContext) == acqCni) && (acqCni != 0));

      // save netwop and stream of the last received PI
      pPt = EpgTscQueue_PeekTail(ptsc, acqCni);
      if (pPt != NULL)
      {
         stream = ((pPt->flags & PI_TSC_MASK_IS_STREAM_1) ? 0 : 1);
         netwop = pPt->netwop;
      }
      else
         stream = netwop = 0xff;

      // determine which timescale windows receive the data
      // - if any open timescale is locked, nothing is done
      if (tscaleState[DB_TARGET_UI].open)
      {
         if (tscaleState[DB_TARGET_UI].locked == FALSE)
         {
            if (tscaleState[DB_TARGET_ACQ].open)
            {
               if (tscaleState[DB_TARGET_ACQ].locked == FALSE)
               {
                  if (isUiDb)
                  {  // both UI and ACQ timescales are open and refer to the same database
                     dprintf0("TimeScale-ProcessAcqQueue: UI == ACQ\n");
                     EpgTscQueue_SetProvCni(ptsc, acqCni);
                     // must process them in parallel ("both"), because popping from a queue frees the data
                     TimeScale_AddPi(DB_TARGET_BOTH, ptsc);

                     TimeScale_HighlightNetwop(DB_TARGET_UI, netwop, stream);
                     TimeScale_HighlightNetwop(DB_TARGET_ACQ, netwop, stream);
                  }
                  else
                  {  // both UI and ACQ are open, but refer to different databases
                     dprintf0("TimeScale-ProcessAcqQueue: UI != ACQ\n");
                     EpgTscQueue_SetProvCni(ptsc, acqCni);
                     TimeScale_AddPi(DB_TARGET_ACQ, ptsc);

                     // after an acq provider change, there might still be data in the queue fro the ui db
                     EpgTscQueue_SetProvCni(ptsc, EpgDbContextGetCni(pUiDbContext));
                     TimeScale_AddPi(DB_TARGET_UI, ptsc);

                     TimeScale_HighlightNetwop(DB_TARGET_ACQ, netwop, stream);
                  }
                  // discard unused data in the queue; can only refer to a previously used db
                  EpgTscQueue_ClearUnprocessed(ptsc);
               }
            }
            else
            {  // only UI timescale is open
               // - if UI db == ACQ db, it's handled just as if this was the acq timescales
               // - else, the queue will normally be empty; however after acq moved away from
               //   the ui db, some data for the ui db might still be queued
               dprintf0("TimeScale-ProcessAcqQueue: UI only\n");
               EpgTscQueue_SetProvCni(ptsc, EpgDbContextGetCni(pUiDbContext));
               TimeScale_AddPi(DB_TARGET_UI, ptsc);
               EpgTscQueue_ClearUnprocessed(ptsc);

               // note: no highlighting for the merged db
               if (isUiDb)
                  TimeScale_HighlightNetwop(DB_TARGET_UI, netwop, stream);
            }

            if ( tscaleState[DB_TARGET_UI].isForAcq && (isUiDb == FALSE) )
            {  // UI db is no longer identical with acq db

               // remove netwop highlighting
               TimeScale_HighlightNetwop(DB_TARGET_UI, 0xff, 0xff);
               // remove acq tail markers
               sprintf(comm, "TimeScale_ClearTail %s\n", tscn[DB_TARGET_UI]);
               eval_check(interp, comm);

               // update connection between timescale windows and acq
               TimeScale_RequestAcq();
            }
            else if ( (tscaleState[DB_TARGET_UI].isForAcq == FALSE) && isUiDb )
            {  // UI db is now identical with acq db
               TimeScale_RequestAcq();
            }
         }
      }
      else if (tscaleState[DB_TARGET_ACQ].open)
      {
         if (tscaleState[DB_TARGET_ACQ].locked == FALSE)
         {  // only acq timescale is open
            dprintf0("TimeScale-ProcessAcqQueue: ACQ only\n");
            EpgTscQueue_SetProvCni(ptsc, acqCni);
            TimeScale_AddPi(DB_TARGET_ACQ, ptsc);
            EpgTscQueue_ClearUnprocessed(ptsc);

            TimeScale_HighlightNetwop(DB_TARGET_ACQ, netwop, stream);
         }
      }

      if ( (!tscaleState[DB_TARGET_UI].open || !isUiDb) &&
           !tscaleState[DB_TARGET_ACQ].open )
      {
         ifdebug0(EpgTscQueue_HasElems(ptsc), "TimeScale-ProcessAcqQueue: discard remaining data");
         // discard all remaining data
         EpgTscQueue_Clear(ptsc);
      }
   }
   else if ( (tscaleState[DB_TARGET_UI].open) &&
             (tscaleState[DB_TARGET_UI].locked == FALSE) &&
             (tscaleState[DB_TARGET_UI].isForAcq) )
   {  // acq has been switched OFF but UI scale is still connected with acq

      // remove netwop highlighting
      TimeScale_HighlightNetwop(DB_TARGET_UI, 0xff, 0xff);
      // remove acq tail markers
      sprintf(comm, "TimeScale_ClearTail %s\n", tscn[DB_TARGET_UI]);
      eval_check(interp, comm);

      // update connection between timescale windows and acq
      TimeScale_RequestAcq();
   }
}

// ----------------------------------------------------------------------------
// Helper function: determine width of the timescale window
//
static uint TimeScale_GetScaleWidth( EPGDB_CONTEXT * dbc )
{
   const AI_BLOCK * pAi;
   const PI_BLOCK * pFirstPi;
   const PI_BLOCK * pLastPi;
   uint  days, maxDays;
   uint  idx;
   uint  result;

   maxDays = TSC_SCALE_MIN_DAY_COUNT;

   EpgDbLockDatabase(dbc, TRUE);
   if ( !EpgDbContextIsMerged(dbc) && !EpgDbContextIsXmltv(dbc) )
   {
      // regular database: use "day count" from AI so missing blocks can be indicated
      pAi = EpgDbGetAi(dbc);
      if (pAi != NULL)
      {
         for (idx = 0; idx < pAi->netwopCount; idx++)
         {
            days = AI_GET_NETWOP_N(pAi, idx)->dayCount;
            if (days > maxDays)
               maxDays = days;
         }
      }
      result = (maxDays + 1) * 24*60*60 + dbc->expireDelayPi;
   }
   else
   {  // merged or imported db: determine width by covered time
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
   EPGDB_CONTEXT   * dbc;
   const PI_BLOCK  * pPi;
   EPGDB_PI_TSC      tsc;
   time_t now = time(NULL);
   uint   target;

   for (target=0; target < 2; target++)
   {
      if ( tscaleState[target].open && tscaleState[target].locked )
      {
         // clear the lock
         tscaleState[target].locked = FALSE;

         dbc = ((target == DB_TARGET_ACQ) ? EpgAcqCtl_GetDbContext(TRUE) : pUiDbContext);

         if ((dbc != NULL) && (EpgDbContextGetCni(dbc) != 0))
         {
            // initialize/reset state struct
            // (note: conversion factor secsPerPixel is reset only upon initial display or prov change)
            tscaleState[target].highlighted = 0xff;
            tscaleState[target].lastStream  = 0xff;
            tscaleState[target].filled      = FALSE;
            tscaleState[target].isForAcq    = (target == DB_TARGET_ACQ);
            tscaleState[target].widthSecs   = TimeScale_GetScaleWidth(dbc);
            tscaleState[target].widthPixels = tscaleState[target].widthSecs / tscaleState[target].secsPerPixel;

            // create (or update) network table: each row has a label and scale
            sprintf(comm, "TimeScale_Open %s 0x%04X %s %d %d\n",
                          tscn[target], EpgDbContextGetCni(dbc),
                          ((target == DB_TARGET_UI) ? "ui" : "acq"),
                          (EpgDbContextIsMerged(dbc) || EpgDbContextIsXmltv(dbc)),
                          tscaleState[target].widthPixels);
            eval_check(interp, comm);

            EpgDbLockDatabase(dbc, TRUE);
            pPi = EpgDbSearchFirstPi(dbc, NULL);
            if (pPi != NULL)
            {
               dprintf1("TimeScale-InitialFill: fill %s scale\n", ((target == DB_TARGET_UI) ? "ui" : "acq"));
               // calculate new scale start time: now or oldest PI start time
               // note: if db is not complete the first PI might not be the oldest one; this is corrected later
               if (pPi->start_time < now)
                  tscaleState[target].startTime = pPi->start_time;
               else
                  tscaleState[target].startTime = now;

               // initialize the scales with the PI already in the db
               EpgTscQueue_Init(&tsc);
               EpgTscQueue_AddAll(&tsc, dbc);

               EpgTscQueue_SetProvCni(&tsc, EpgDbContextGetCni(dbc));
               TimeScale_AddPi(target, &tsc);

               // just to catch error cases: clear remaining data (normally all data should have been processed and freed)
               assert(EpgTscQueue_HasElems(&tsc) == FALSE);
               EpgTscQueue_Clear(&tsc);

               // set NOW marker
               TimeScale_UpdateDateScale(NULL);
            }
            else
            {
               dprintf1("TimeScale-InitialFill: no PI for %s scale - wait for acq\n", ((target == DB_TARGET_UI) ? "ui" : "acq"));
               tscaleState[target].startTime = 0;

               sprintf(comm, "TimeScale_ClearDateScale %s %d\n", tscn[target], tscaleState[target].widthPixels);
               eval_check(interp, comm);
            }

            EpgDbLockDatabase(dbc, FALSE);

            // display summary at the bottom of the window
            TimeScale_UpdateStatusLine(NULL);

            // process data from acq timescale queue, which may have been held due to the lock
            TimeScale_ProcessAcqQueue(NULL);

            // update connection between timescale windows and acq
            // note: must be alled after acq queue is processed (b/c is-For-Acq flags)
            TimeScale_RequestAcq();
         }
         else
         {  // context no longer exists (e.g. acq stopped) -> close the popup
            // note: the destroy callback will be invoked and update the status variables
            sprintf(comm, "destroy %s", tscn[target]);
            eval_check(interp, comm);
         }

         if (target == DB_TARGET_ACQ)
            EpgAcqCtl_GetDbContext(FALSE);
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
   const char * const pUsage = "Usage: C_TimeScale_Toggle ui|acq [0|1]";
   const char * pKey;
   uint provCni;
   int target, newState;
   int result;

   provCni = 0;  // avoid compiler warnings
   target = 0;
   result = TCL_OK;

   pKey = Tcl_GetString(objv[1]);
   if (((objc == 2) || (objc == 3)) && (pKey != NULL))
   {
      // determine target from first parameter: ui or acq db context
      if (strcmp(pKey, "ui") == 0)
      {
         provCni = EpgDbContextGetCni(pUiDbContext);
         target = DB_TARGET_UI;
      }
      else if (strcmp(pKey, "acq") == 0)
      {
         provCni = EpgAcqCtl_GetProvCni();
         target = DB_TARGET_ACQ;
      }
      else
         result = TCL_ERROR;
   }
   else
      result = TCL_ERROR;

   if (result == TCL_OK)
   {
      // determine new state from optional second parameter: 0, 1 or toggle
      if (objc == 2)
      {  // no parameter -> toggle current state
         newState = ! tscaleState[target].open;
      }
      else
      {
         result = Tcl_GetBooleanFromObj(interp, objv[2], &newState);
      }
   }

   if (result == TCL_OK)
   {
      // check if the window already is in the user-requested state
      if (newState != tscaleState[target].open)
      {
         if (tscaleState[target].open == FALSE)
         {  // user requests to open a new window
            // refuse request if no AI is available (required for netwop list)
            if (provCni != 0)
            {
               tscaleState[target].highlighted = 0xff;
               tscaleState[target].open   = TRUE;
               tscaleState[target].locked = TRUE;
               tscaleState[target].secsPerPixel = TSC_DEF_SECS_PER_PIXEL;

               // create the window and its content
               TimeScale_CreateOrRebuild(NULL);
            }
         }
         else
         {  // user requests to destroy the window

            // note: set to FALSE before window destruction to avoid recursion
            tscaleState[target].open = FALSE;

            sprintf(comm, "destroy %s; update", tscn[target]);
            eval_check(interp, comm);

            // update connection between timescale windows and acq
            TimeScale_RequestAcq();
         }
         sprintf(comm, "set menuStatusTscaleOpen(%s) %d\n", Tcl_GetString(objv[1]), tscaleState[target].open);
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
   const char * const pUsage = "Usage: C_TimeScale_Zoom ui|acq <step>";
   const char * pKey;
   uint target;
   sint  newFac;
   int   step;
   int   result = TCL_ERROR;

   if (objc != 3)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetIntFromObj(interp, objv[2], &step) != TCL_OK)
   {  // string parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else if ( (pKey = Tcl_GetString(objv[1])) != NULL)
   {
      result = TCL_OK;
      // determine target from first parameter: ui or acq db window
      if (strcmp(pKey, "ui") == 0)
         target = DB_TARGET_UI;
      else if (strcmp(pKey, "acq") == 0)
         target = DB_TARGET_ACQ;
      else
         target = TCL_ERROR;

      if (result == TCL_OK)
      {
         newFac = tscaleState[target].secsPerPixel;
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

         if (newFac != tscaleState[target].secsPerPixel)
         {
            tscaleState[target].secsPerPixel = newFac;

            tscaleState[target].locked = TRUE;
            AddMainIdleEvent(TimeScale_CreateOrRebuild, NULL, TRUE);
         }
      }
      else
         debug1("TimeScale-Zoom: illegal target window keyword '%s'", pKey);
   }
   else
      debug0("TimeScale-Zoom: failed to get string from Tcl objv[1]");

   return result;
}

// ----------------------------------------------------------------------------
// Notify the windows of this module about a database provider change
// - when provider changes, the according window is rebuilt
// - when acq shifts from ui db to a separate acq db, the highlighted
//   netwop in the ui window must be unmarked
//
void TimeScale_ProvChange( int target )
{
   dprintf2("TimeScale-ProvChange: called for %d: %s\n", target, ((target == DB_TARGET_UI) ? "ui" : ((target == DB_TARGET_ACQ) ? "acq" : "unknown")));

   if (target < 2)
   {
      // update & reload the respective network timescale popup
      // when the context is closed the popup window is destroyed
      if (tscaleState[target].open)
      {
         tscaleState[target].locked = TRUE;
         tscaleState[target].secsPerPixel = TSC_DEF_SECS_PER_PIXEL;
         AddMainIdleEvent(TimeScale_CreateOrRebuild, NULL, TRUE);
      }

      // if a network in the inactive UI window is still marked, unmark it
      if ((target != DB_TARGET_UI) && tscaleState[DB_TARGET_UI].open)
      {
         // trigger acq queue processing to have netwop markers enabled or removed
         // the acq request state is update then only; NOT done here to avoid recursive call to acq ctl
         AddMainIdleEvent(TimeScale_ProcessAcqQueue, NULL, TRUE);
      }
   }
   else
      fatal1("TimeScale_ProvChange: illegal target: %d", target);
}

// ----------------------------------------------------------------------------
// Notify timescale windows about an AI version change in the acq database
// - number and names of netwops might have changed,
//   range of valid PI might have changed, and PI hence gotten removed
// - scale contents have to be recolored (shaded) to reflect obsolete version
// - the timescale queue (filled by background acquisition) processing is
//   locked until the window is updated
//
void TimeScale_VersionChange( void )
{
   bool update = FALSE;

   dprintf0("TimeScale-VersionChange: called\n");

   if ( tscaleState[DB_TARGET_ACQ].open )
   {
      tscaleState[DB_TARGET_ACQ].locked = TRUE;
      update = TRUE;
   }

   if ( tscaleState[DB_TARGET_UI].open )
   {
      if ( EpgDbContextIsMerged(pUiDbContext) == FALSE )
      {
         if (EpgAcqCtl_GetProvCni() == EpgDbContextGetCni(pUiDbContext))
         {
            tscaleState[DB_TARGET_UI].locked = TRUE;
            update = TRUE;
         }
      }
      else
      {  // merged database: search for the acq CNI in the list of merged providers
         if ((EpgContextMergeCheckForCni(pUiDbContext, EpgAcqCtl_GetProvCni())))
         {
            tscaleState[DB_TARGET_UI].locked = TRUE;
            update = TRUE;
         }
      }
   }

   // if ACQ or UI or both need to be redrawn, install an event in the main loop
   // - note: which scales need to be updated is determined by the locked bits,
   //   so we do not need to pass this information here; this makes handling
   //   redundant calls easier too.
   if (update)
   {
      AddMainIdleEvent(TimeScale_CreateOrRebuild, NULL, TRUE);
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
   if (tscaleState[DB_TARGET_UI].open || tscaleState[DB_TARGET_ACQ].open)
   {
      dprintf0("TimeScale-AcqStatsUpdate: scheduling update\n");

      AddMainIdleEvent(TimeScale_UpdateStatusLine, NULL, TRUE);
   }
}

// ----------------------------------------------------------------------------
// Notification from acq that PI have been captured
// - only used while timescale windows are open
// - schedules processing the PI timescale queue; incoming PI may have to be
//   added to both UI and ACQ targets, if they refer to the same database
//
void TimeScale_AcqPiAdded( void )
{
   AddMainIdleEvent(TimeScale_ProcessAcqQueue, NULL, TRUE);
}

// ----------------------------------------------------------------------------
// Notification from merge context that PI have been added to the merged db
// - only used while UI timescale window is open
// - schedules processing the merged PI timescale queue
//
void TimeScale_AcqPiMerged( void )
{
   AddMainIdleEvent(TimeScale_ProcessMergeQueue, NULL, TRUE);
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

   memset(tscaleState, 0, sizeof(tscaleState));
}

