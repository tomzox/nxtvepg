/*
 *  Nextview EPG GUI: Statistics window
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
 *  Description:
 *
 *    Implements pop-up windows with statistics about the current
 *    state of the EPG database. In the timescale windows, for each
 *    network in the database five buttons reflect the availability
 *    of the first five PI blocks and a bar shows the time spans
 *    covered by all blocks.  In the acquisition statistics window,
 *    a history diagram reflects the percentage of received blocks,
 *    separated in stream 1 (red) and 2 (blue).
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: statswin.c,v 1.12 2000/06/15 18:05:02 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <string.h>
#include <time.h>

#include "tcl.h"
#include "tk.h"

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbacq.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgmain.h"
#include "epgctl/epgacqctl.h"
#include "epgui/statswin.h"


// state of AcqStat window
static bool   acqStatOpen  = FALSE;
static int    lastHistPos;

// state of TimeScale windows
static struct
{
   bool   open;
   time_t startTime;         // for DisplayPi
   int    highlighted;       // for HighlightNetwop()
   uchar  lastStream;        // dito
} tscaleState[2];

const char *tscn[2] =
{
   ".tscale_ui",
   ".tscale_acq"
};

#define STREAM_COLOR_COUNT  5
const char *streamColors[STREAM_COLOR_COUNT] =
{
   "red",           // stream 1, current version -> red
   "#4040ff",       // stream 1, current version -> blue
   "#A52A2A",       // stream 2, obsolete version -> brown
   "#483D8B",       // stream 2, obsolete version -> DarkSlate
   "yellow"         // defect block -> yellow
};

// ----------------------------------------------------------------------------
// display PI time range and Now box
//
static void StatsWin_DisplayPi( int target, uchar netwop, uchar stream,
                                time_t start, time_t stop, uchar hasShortInfo, uchar hasLongInfo )
{
   uint startOff, stopOff;

   if ((stop > tscaleState[target].startTime) && (stream < STREAM_COLOR_COUNT))
   {
      if (start < tscaleState[target].startTime)
         start = 0;
      else
         start -= tscaleState[target].startTime;

      stop -= tscaleState[target].startTime;
      if (stop > (5 * 60*60*24))
         stop = 5 * 60*60*24;

      startOff = start * 256 / (5*60*60*24);
      stopOff  = stop  * 256 / (5*60*60*24) + 1;

      sprintf(comm, "TimeScale_AddPi %s.top.n%d %d %d %s %d %d\n",
              tscn[target], netwop, startOff, stopOff,
              streamColors[stream],
              hasShortInfo, hasLongInfo);
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// mark the last netwop that a PI was received from
// - can be called with invalid stream index to unmark last netwop only
//
static void StatsWin_HighlightNetwop( int target, uchar netwop, uchar stream )
{
   if ( (netwop != tscaleState[target].highlighted) ||
        (stream != tscaleState[target].lastStream) )
   {
      if (tscaleState[target].highlighted != 0xff)
      {
         sprintf(comm, "%s.top.n%d.name config -bg $default_bg\n", tscn[target], tscaleState[target].highlighted);
         eval_check(interp, comm);
      }

      if (stream < 2)
      {
         sprintf(comm, "%s.top.n%d.name config -bg %s\n",
                       tscn[target], netwop, (stream == 0) ? "#ffc0c0" : "#c0c0ff");
         eval_check(interp, comm);
      }

      tscaleState[target].highlighted = netwop;
      tscaleState[target].lastStream  = stream;
   }
}

// ----------------------------------------------------------------------------
//  display time covered by a new PI in the canvas
//
void StatsWin_NewPi( EPGDB_CONTEXT * dbc, const PI_BLOCK *pPi, uchar stream, bool inserted )
{
   const AI_BLOCK  *pAi;
   const AI_NETWOP *pNetwop;
   int blockOff;
   time_t now = time(NULL);
   int target;

   if (tscaleState[DB_TARGET_UI].open && (dbc == pUiDbContext))
      target = DB_TARGET_UI;
   else if (tscaleState[DB_TARGET_ACQ].open && (dbc == pAcqDbContext))
      target = DB_TARGET_ACQ;
   else
      target = -1;

   if (target != -1)
   {
      EpgDbLockDatabase(dbc, TRUE);
      pAi = EpgDbGetAi(dbc);
      if ((pAi != NULL) && (pPi->netwop_no < pAi->netwopCount))
      {
         pNetwop = AI_GET_NETWOP_N(pAi, pPi->netwop_no);
         blockOff = EpgDbGetPiBlockIndex(pNetwop->startNo, pPi->block_no);
         if ((blockOff < EpgDbGetPiBlockCount(pNetwop->startNo, pNetwop->stopNoSwo)) && (blockOff >= 0))
         {
            if (blockOff < 5)
            {
               sprintf(comm, "TimeScale_MarkNow %s.top.n%d %d %s\n",
                             tscn[target], pPi->netwop_no, blockOff,
                             (pPi->stop_time >= now) ? streamColors[0] : streamColors[4]);
               eval_check(interp, comm);
            }

            if (inserted == FALSE)
            {  // conflict with other block -> mark as faulty
               StatsWin_DisplayPi(target, pPi->netwop_no, 4, pPi->start_time, pPi->stop_time, TRUE, TRUE);
            }
            else
            {
               StatsWin_DisplayPi(target, pPi->netwop_no, stream, pPi->start_time, pPi->stop_time,
                                  pPi->short_info_length>0, pPi->long_info_length>0);
            }
         }

         StatsWin_HighlightNetwop(target, pPi->netwop_no, stream);
      }
      EpgDbLockDatabase(dbc, FALSE);
   }
}

// ----------------------------------------------------------------------------
// update history diagram and stats text
//
void StatsWin_NewAi( void )
{
   const EPGDB_STATS *sv;
   const AI_BLOCK *pAi;
   double avgAiDistance;
   uchar netname[20+1];
   uchar version, versionSwo;
   ulong duration;
   time_t now = time(NULL);
   int target;

   if (tscaleState[DB_TARGET_UI].open && (pAcqDbContext == pUiDbContext))
      target = DB_TARGET_UI;
   else if (tscaleState[DB_TARGET_ACQ].open)
      target = DB_TARGET_ACQ;
   else
      target = -1;

   if (acqStatOpen || (target >= 0))
   {
      sv = EpgAcqCtl_GetStatistics();
      if ((pAcqDbContext != NULL) && (sv != NULL))
      {
         // get provider's network name
         EpgDbLockDatabase(pAcqDbContext, TRUE);
         pAi = EpgDbGetAi(pAcqDbContext);
         if (pAi != NULL)
         {
            strncpy(netname, AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop), 20);
            netname[20] = 0;
            version = pAi->version;
            versionSwo = pAi->version_swo;
         }
         else
         {
            strcpy(netname, "none yet");
            version = versionSwo = 0;
         }
         EpgDbLockDatabase(pAcqDbContext, FALSE);

         if (acqStatOpen)
         {
            if ( ((lastHistPos + 1) % STATS_HIST_WIDTH) != sv->histIdx )
            {
               sprintf(comm, "AcqStat_ClearHistory\n");
               eval_check(interp, comm);
            }
            sprintf(comm, "AcqStat_AddHistory %d %d %d %d %d\n",
                          sv->histIdx+1,
                          sv->hist_s1cur[sv->histIdx], sv->hist_s1old[sv->histIdx],
                          sv->hist_s2cur[sv->histIdx], sv->hist_s2old[sv->histIdx]);
            eval_check(interp, comm);
            lastHistPos = sv->histIdx;

            if (sv->aiCount > 1)
               avgAiDistance = (sv->lastAiTime - sv->acqStartTime) / (sv->aiCount - 1);
            else
               avgAiDistance = 0.0;

            duration = now - sv->acqStartTime;
            if (duration == 0)
               duration = 1;

            sprintf(comm, ".acqstat.statistics configure -text \""
                          "EPG provider:     %s\n"
                          "Database version: %d/%d\n"
                          "Acq Runtime:      %02d:%02d\n"
                          "TTX data rate:    %d baud\n"
                          "EPG data rate:    %d baud (%1.1f%% of TTX)\n"
                          "EPG page rate:    %1.2f pages/sec\n"
                          "AI recv. count:   %d\n"
                          "AI min/avg/max:   %d/%2.2f/%d sec\n"
                          "Block count db:   %ld (%ld + %ld swo)\n"
                          "current version:  %ld (%ld + %ld swo)\n"
                          "Blocks in AI:     %ld (%d%%/%d%% complete)\n"
                          "                                              \"",
                          netname,
                          version, versionSwo,
                          (uint)(duration / 60), (uint)(duration % 60),
                          (int)((sv->ttxPkgCount*45*8)/duration),
                          (int)((sv->epgPkgCount*45*8)/duration),
                          ((sv->ttxPkgCount > 0) ? ((double)sv->epgPkgCount*100.0/sv->ttxPkgCount) : 0.0),
                          ((double)sv->epgPagCount / duration),
                          sv->aiCount,
                          sv->minAiDistance, avgAiDistance, sv->maxAiDistance,
                          sv->blockCount1 + sv->blockCount2, sv->blockCount1, sv->blockCount2,
                          sv->curBlockCount1 + sv->curBlockCount2, sv->curBlockCount1, sv->curBlockCount2,
                          sv->blockCountAi,
                          ((sv->blockCountAi > 0) ? (int)((double)(sv->blockCount1 + sv->blockCount2) * 100.0 / sv->blockCountAi) : 0),
                          ((sv->blockCountAi > 0) ? (int)((double)(sv->curBlockCount1 + sv->curBlockCount2) * 100.0 / sv->blockCountAi) : 0)
                   );
            eval_check(interp, comm);
         }

         if ((target >= 0) && (sv->aiCount > 0))
         {
            sprintf(comm, "%s.bottom.l configure -text {%s database %d%% complete.}\n",
                          tscn[target], netname, (int)((double)sv->hist_s2old[sv->histIdx]/1.28));
            eval_check(interp, comm);
         }
      }
   }
}

// ----------------------------------------------------------------------------
// display coverage by PI currently in database
//
static void StatsWin_Load( int target, EPGDB_CONTEXT *dbc )
{
   const AI_BLOCK  *pAi;
   const PI_BLOCK  *pPi;
   const AI_NETWOP *pNetwops;
   time_t start_time[MAX_NETWOP_COUNT];
   time_t stop_time[MAX_NETWOP_COUNT];
   time_t now;
   bool   has_short_info[MAX_NETWOP_COUNT];
   bool   has_long_info[MAX_NETWOP_COUNT];
   uchar  stream[MAX_NETWOP_COUNT], this_stream, this_version;
   uchar  netwop_no;
   ulong  bsum, total;
   int    idx;

   EpgDbLockDatabase(dbc, TRUE);
   memset(start_time, 0, sizeof(start_time));
   now = time(NULL);
   bsum = total = 0;

   pAi = EpgDbGetAi(dbc);
   pPi = EpgDbSearchFirstPi(dbc, NULL);
   if ((pAi != NULL) && (pPi != NULL))
   {
      do
      {
         bsum += 1;
         netwop_no = pPi->netwop_no;
         this_stream = EpgDbGetStream(pPi);
         this_version = EpgDbGetVersion(pPi);
         if ((this_stream == 0) && (this_version != pAi->version))
            this_stream = 2;
         else if ((this_stream == 1) && (this_version != pAi->version_swo))
            this_stream = 3;

         idx = EpgDbGetPiBlockIndex(AI_GET_NETWOP_N(pAi, netwop_no)->startNo, pPi->block_no);
         if (idx < 5)
         {
            sprintf(comm, "TimeScale_MarkNow %s.top.n%d %d red\n", tscn[target], netwop_no, idx);
            eval_check(interp, comm);
            if (pPi->start_time < now)
            {  // this item is currently running -> all previous itmes must be expired
               for (idx=idx-1; idx >= 0; idx--)
               {
                  sprintf(comm, "TimeScale_MarkNow %s.top.n%d %d yellow\n", tscn[target], netwop_no, idx);
                  eval_check(interp, comm);
               }
            }
         }

         if (start_time[netwop_no] != 0)
         {
            if ((pPi->start_time == stop_time[netwop_no]) &&
                (((pPi->short_info_length>0) ^ has_short_info[netwop_no]) == FALSE) &&
                (((pPi->long_info_length>0) ^ has_long_info[netwop_no]) == FALSE) &&
                (this_stream == stream[netwop_no]) )
            {
               stop_time[netwop_no] = pPi->stop_time;
            }
            else
            {
               StatsWin_DisplayPi(target, netwop_no, stream[netwop_no], start_time[netwop_no], stop_time[netwop_no], has_short_info[netwop_no], has_long_info[netwop_no]);
               start_time[netwop_no] = 0L;
            }
         }

         if (start_time[netwop_no] == 0)
         {
            start_time[netwop_no]     = pPi->start_time;
            stop_time[netwop_no]      = pPi->stop_time;
            has_short_info[netwop_no] = (pPi->short_info_length>0);
            has_long_info[netwop_no]  = (pPi->long_info_length>0);
            stream[netwop_no]         = this_stream;
         }
         pPi = EpgDbSearchNextPi(dbc, NULL, pPi);
      }
      while (pPi != NULL);

      pNetwops = AI_GET_NETWOPS(pAi);
      for (netwop_no=0; netwop_no < pAi->netwopCount; netwop_no++)
      {
         if (start_time[netwop_no] != 0)
         {
            StatsWin_DisplayPi(target, netwop_no, stream[netwop_no], start_time[netwop_no], stop_time[netwop_no], has_short_info[netwop_no], has_long_info[netwop_no]);
         }

         total += EpgDbGetPiBlockCount(pNetwops[netwop_no].startNo, pNetwops[netwop_no].stopNoSwo);
      }

   }

   // display summary at the bottom of the window
   sprintf(comm, "%s.bottom.l configure -text {%s database %d%% complete.}\n",
                 tscn[target],
                 ((pAi != NULL) ? (char*)AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop) : ""),
                 ((total > 0) ? (int)((double)bsum * 100 / total) : 0));
   eval_check(interp, comm);

   EpgDbLockDatabase(dbc, FALSE);
}

// ----------------------------------------------------------------------------
// build the sclaes for each network in the current EPG
//
static void StatsWin_BuildCanvas( int target, EPGDB_CONTEXT *dbc )
{
   const AI_BLOCK  *pAi;
   const AI_NETWOP *pNetwops;
   uchar netwop;

   EpgDbLockDatabase(dbc, TRUE);
   pAi = EpgDbGetAi(dbc);
   if (pAi != NULL)
   {
      sprintf(comm, "toplevel %s\n"
                    "wm title %s {Nextview database timescales}\n"
                    "wm resizable . 0 0\n"
                    "wm group %s .\n"
                    "frame %s.top\n",
                    tscn[target], tscn[target], tscn[target], tscn[target]);
      eval_check(interp, comm);

      pNetwops = AI_GET_NETWOPS(pAi);

      for (netwop=0; netwop < pAi->netwopCount; netwop++)
      {
         sprintf(comm, "TimeScale_Create %s.top.n%d {%s}\n", tscn[target], netwop, AI_GET_STR_BY_OFF(pAi, pNetwops[netwop].off_name));
         eval_check(interp, comm);
      }
      sprintf(comm, "pack %s.top -padx 5 -pady 5 -side top -fill x\n"
                    "frame %s.bottom -borderwidth 2 -relief sunken\n"
                    "label %s.bottom.l -text {}\n"
                    "pack %s.bottom.l -side left -anchor w\n"
                    "pack %s.bottom -side top -fill x\n"
                    "bind %s.bottom <Destroy> {+ C_StatsWin_ToggleTimescale %s 0}\n",
                    tscn[target], tscn[target], tscn[target], tscn[target], tscn[target], tscn[target],
                    ((target == DB_TARGET_UI) ? "ui" : "acq"));
      eval_check(interp, comm);

   }
   EpgDbLockDatabase(dbc, FALSE);
}

// ----------------------------------------------------------------------------
// open or close the window
// - called by the "Statistics" button in the main window
// - also called when the window is destroyed (e.g. by the wm)
// - can not be opened until an AI block has been received
//   because we need to know the number and names of netwops in the EPG
//
static int StatsWin_ToggleTimescale( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char *pUsage = "Usage: C_StatsWin_ToggleTimescaleUi ui|acq [0|1]";
   EPGDB_CONTEXT *dbc = NULL;
   int target, newState;
   int result = TCL_OK;

   dbc = NULL;  // avoid compiler warnings
   target = 0;

   if ((argc == 2) || (argc == 3))
   {
      // dtermine target from first parameter: ui or acq db context
      if (!strcmp(argv[1], "ui"))
      {
         dbc = pUiDbContext;
         target = DB_TARGET_UI;
      }
      else if (!strcmp(argv[1], "acq"))
      {
         dbc = pAcqDbContext;
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
      if (argc == 2)
      {  // no parameter -> toggle current state
         newState = ! tscaleState[target].open;
      }
      else
      {
         if (Tcl_GetInt(interp, argv[2], &newState) || ((uint)newState > 1))
            result = TCL_ERROR;
      }
   }

   if (result == TCL_OK)
   {
      if (newState != tscaleState[target].open)
      {
         if (tscaleState[target].open == FALSE)
         {  // window shall be opened
            if (dbc != NULL)
            {
               EpgDbLockDatabase(dbc, TRUE);
               if (EpgDbGetAi(dbc) != NULL)
               {
                  tscaleState[target].startTime   = time(NULL);
                  tscaleState[target].highlighted = 0xff;
                  // create the window with a timescale for each network
                  StatsWin_BuildCanvas(target, dbc);
                  // mark all time spans covered by PI in the current db
                  StatsWin_Load(target, dbc);

                  tscaleState[target].open = TRUE;
               }
               EpgDbLockDatabase(dbc, FALSE);
            }
         }
         else
         {  // destroy the window
            tscaleState[target].open = FALSE;

            sprintf(comm, "destroy %s; update", tscn[target]);
            eval_check(interp, comm);
         }
      }
      sprintf(comm, "set menuStatusTscaleOpen(%s) %d\n", argv[1], tscaleState[target].open);
      eval_check(interp, comm);
   }
   else
   {  // error of any kind -> display usage
      interp->result = (char *) pUsage;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Create window with acq statistics
//
static int StatsWin_ToggleStatistics( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char *pUsage = "Usage: C_StatsWin_ToggleStatistics [0|1]";
   int newState;
   int result;

   if (argc == 1)
   {  // no state parameter -> toggle state
      newState = ! acqStatOpen;
      result = TCL_OK;
   }
   else if (argc == 2)
   {
      if (Tcl_GetInt(interp, argv[1], &newState) || ((uint)newState > 1))
      {  // parameter is not a boolean
         interp->result = (char *) pUsage;
         result = TCL_ERROR;
      }
      else
         result = TCL_OK;
   }
   else
   {  // illegal parameter count
      interp->result = (char *) pUsage;
      result = TCL_ERROR;
   }

   if ( (result == TCL_OK) && (newState != acqStatOpen) )
   {
      if (acqStatOpen == FALSE)
      {  // window shall be opened
         const EPGDB_STATS *sv = EpgAcqCtl_GetStatistics();
         if (sv != NULL)
         {
            sprintf(comm, "AcqStat_Create\n");
            eval_check(interp, comm);

            acqStatOpen = TRUE;

            if ((sv->histIdx > 1) && (sv->histIdx != STATS_HIST_WIDTH - 1))
            {
               for (lastHistPos=0; lastHistPos < sv->histIdx; lastHistPos++)
               {
                  sprintf(comm, "AcqStat_AddHistory %d %d %d %d %d\n",
                                lastHistPos+1,
                                sv->hist_s1cur[lastHistPos], sv->hist_s1old[lastHistPos],
                                sv->hist_s2cur[lastHistPos], sv->hist_s2old[lastHistPos]);
                  eval_check(interp, comm);
               }
               lastHistPos = sv->histIdx - 1;
            }
            else
               lastHistPos = 0;

            // display initial summary
            StatsWin_NewAi();
         }
      }
      else
      {  // destroy the window
         sprintf(comm, "destroy .acqstat");
         eval_check(interp, comm);

         acqStatOpen = FALSE;
      }
      // set the state of the checkbutton of the according menu entry
      sprintf(comm, "set menuStatusAcqStatOpen %d\n", acqStatOpen);
      eval_check(interp, comm);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Notify the windows of this module about a database provider change
// - when provider changes, the according window is destroyed
// - when acq shift from ui db to a separate acq db, the highlighted
//   netwop in the ui window must be unmarked
//
void StatsWin_ProvChange( int target )
{
   if (target < 2)
   {
      if (tscaleState[target].open)
      {
         tscaleState[target].open = FALSE;

         sprintf(comm, "destroy %s", tscn[target]);
         eval_check(interp, comm);
      }

      // close separate acq timescale window, if ui now has the same db
      if ( (tscaleState[DB_TARGET_ACQ].open) && (pAcqDbContext == pUiDbContext) )
      {
         tscaleState[DB_TARGET_ACQ].open = FALSE;

         sprintf(comm, "destroy %s", tscn[DB_TARGET_ACQ]);
         eval_check(interp, comm);
      }

      // if a network in the inactive UI window is still marked, unmark it
      if ( (tscaleState[DB_TARGET_UI].open) &&
           (tscaleState[DB_TARGET_UI].highlighted != 0xff) )
      {
         StatsWin_HighlightNetwop(DB_TARGET_UI, 0, 3);
      }

      // if the acq stats window is open, update its content
      if (acqStatOpen && (target == DB_TARGET_ACQ))
      {
         StatsWin_NewAi();
      }
   }
   else
      debug1("StatsWin_ProvChange: illegal target: %d", target);
}

// ----------------------------------------------------------------------------
// create and initialize (but do NOT show) the statistics window
// - this should be called only once during start-up
//
void StatsWin_Create( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_StatsWin_ToggleTimescale", &cmdInfo) == 0)
   {
      Tcl_CreateCommand(interp, "C_StatsWin_ToggleTimescale", StatsWin_ToggleTimescale, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_StatsWin_ToggleStatistics", StatsWin_ToggleStatistics, (ClientData) NULL, NULL);
   }
   else
      debug0("StatsWin-Create: commands are already created");

   tscaleState[DB_TARGET_UI].open = FALSE;
   tscaleState[DB_TARGET_ACQ].open = FALSE;
}

