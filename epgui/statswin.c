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
 *  $Id: statswin.c,v 1.26 2001/01/21 20:47:36 tom Exp tom $
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
#include "epgdb/epgdbacq.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgmain.h"
#include "epgctl/epgacqctl.h"
#include "epgui/statswin.h"


// state of DbStatsWin window
static struct
{
   bool   open;
   bool   isForAcq;
   int    lastHistPos;
   Tcl_TimerToken updateHandler;
} dbStatsWinState[2];

const char * const dbswn[2] =
{
   ".dbstats_ui",
   ".dbstats_acq"
};

// state of TimeScale windows
static struct
{
   bool   open;
   time_t startTime;         // for DisplayPi
   int    highlighted;       // for HighlightNetwop()
   uchar  lastStream;        // dito
} tscaleState[2];

const char * const tscn[2] =
{
   ".tscale_ui",
   ".tscale_acq"
};

#define STREAM_COLOR_COUNT  5
const char * const streamColors[STREAM_COLOR_COUNT] =
{
   "red",           // stream 1, current version -> red
   "#4040ff",       // stream 1, current version -> blue
   "#A52A2A",       // stream 2, obsolete version -> brown
   "#483D8B",       // stream 2, obsolete version -> DarkSlate
   "yellow"         // defect block -> yellow
};


static void StatsWin_UpdateDbStatsWinTimeout( ClientData clientData );

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
      stopOff  = stop  * 256 / (5*60*60*24);

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
      // remove the highlighting from the previous netwop
      if (tscaleState[target].highlighted != 0xff)
      {
         sprintf(comm, "%s.top.n%d.name config -bg $default_bg\n", tscn[target], tscaleState[target].highlighted);
         eval_global(interp, comm);

         tscaleState[target].highlighted = 0xff;
      }

      // highlight the new netwop
      if (stream < 2)
      {
         sprintf(comm, "%s.top.n%d.name config -bg %s\n",
                       tscn[target], netwop, (stream == 0) ? "#ffc0c0" : "#c0c0ff");
         eval_check(interp, comm);

         tscaleState[target].highlighted = netwop;
         tscaleState[target].lastStream  = stream;
      }
   }
}

// ----------------------------------------------------------------------------
//  display time covered by a new PI in the canvas
//
void StatsWin_NewPi( EPGDB_CONTEXT * dbc, const PI_BLOCK *pPi, uchar stream )
{
   const AI_BLOCK  *pAi;
   const AI_NETWOP *pNetwop;
   uint blockOff;
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
         if (blockOff < 5)
         {
            sprintf(comm, "TimeScale_MarkNow %s.top.n%d %d %s\n",
                          tscn[target], pPi->netwop_no, blockOff,
                          (pPi->stop_time >= now) ? streamColors[0] : streamColors[4]);
            eval_check(interp, comm);
         }

         if (EpgDbSearchObsoletePi(dbc, pPi->netwop_no, pPi->start_time, pPi->stop_time) != NULL)
         {  // conflict with other block -> mark as faulty
            StatsWin_DisplayPi(target, pPi->netwop_no, 4, pPi->start_time, pPi->stop_time, TRUE, TRUE);
         }
         else
         {
            StatsWin_DisplayPi(target, pPi->netwop_no, stream, pPi->start_time, pPi->stop_time,
                               PI_HAS_SHORT_INFO(pPi), PI_HAS_LONG_INFO(pPi));
         }

         StatsWin_HighlightNetwop(target, pPi->netwop_no, stream);
      }
      EpgDbLockDatabase(dbc, FALSE);
   }
}

// ----------------------------------------------------------------------------
// update history diagram and stats text
//
void StatsWin_UpdateDbStatsWin( ClientData clientData )
{
   const EPGDB_BLOCK_COUNT * count;
   EPGDB_BLOCK_COUNT myCount[2];
   EPGDB_CONTEXT * dbc;
   EPGACQ_DESCR acqState;
   const EPGDB_STATS *sv;
   const AI_BLOCK *pAi;
   uchar netname[20+1], datestr[25+1];
   uchar version, versionSwo;
   ulong duration;
   uint  cni, foo;
   bool  waitNi;
   ulong total, allVersionsCount, curVersionCount, obsolete;
   time_t now = time(NULL);
   int target;

   target = (uint) clientData;
   if (target == DB_TARGET_ACQ)
      dbc = pAcqDbContext;
   else if (target == DB_TARGET_UI)
      dbc = pUiDbContext;
   else
      dbc = NULL;
   sv = NULL;

   if ((dbc != NULL) && dbStatsWinState[target].open)
   {
      if ( (target == DB_TARGET_ACQ) || (pAcqDbContext == pUiDbContext) )
      {
         sv = EpgAcqCtl_GetStatistics();
      }
      if (sv == NULL)
      {  // acq not running for this database -> display db stats only
         EpgDbGetStat(pUiDbContext, myCount, 0);
         count = myCount;
      }
      else
         count = sv->count;

      // get provider's network name and db version from AI block
      EpgDbLockDatabase(dbc, TRUE);
      pAi = EpgDbGetAi(dbc);
      if (pAi != NULL)
      {
         strncpy(netname, AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop), 20);
         netname[20] = 0;
         strftime(datestr, 25, "%H:%M:%S %a %d.%m.", localtime(&dbc->lastAiUpdate));
         version = pAi->version;
         versionSwo = pAi->version_swo;
         cni = AI_GET_CNI(pAi);
      }
      else
      {
         strcpy(netname, "none yet");
         strcpy(datestr, "none yet");
         version = versionSwo = 0;
         cni = 0;
      }
      EpgDbLockDatabase(dbc, FALSE);

      total = count[0].ai + count[1].ai;
      obsolete         = count[0].expired + count[0].defective +
                         count[1].expired + count[1].defective;
      allVersionsCount = count[0].allVersions + count[1].allVersions + obsolete;
      curVersionCount  = count[0].curVersion + count[1].curVersion + obsolete;

      sprintf(comm, "%s.browser.stat configure -text \""
                    "EPG provider:     %s (CNI %04X)\n"
                    "last AI update:   %s\n"
                    "Database version: %d/%d\n"
                    "Blocks in AI:     %ld (%ld + %ld swo)\n"
                    "Block count db:   %ld (%ld + %ld swo)\n"
                    "current version:  %ld (%ld + %ld swo)\n"
                    "filled:           %d%% / %d%% current version\n"
                    "expired blocks:   %ld (%d%%)"
                    "\"\n",
                    dbswn[target],
                    netname, cni,
                    datestr,
                    version, versionSwo,
                    total, count[0].ai, count[1].ai,
                    allVersionsCount,
                       count[0].allVersions + count[0].expired + count[0].defective,
                       count[1].allVersions + count[1].expired + count[1].defective,
                    curVersionCount,
                       count[0].curVersion + count[0].expired + count[0].defective,
                       count[1].curVersion + count[1].expired + count[1].defective,
                    ((total > 0) ? (int)((double)allVersionsCount * 100.0 / total) : 0),
                    ((total > 0) ? (int)((double)curVersionCount * 100.0 / total) : 0),
                    count[0].expired + count[1].expired,
                       ((curVersionCount > 0) ? ((int)((double)(count[0].expired + count[1].expired) * 100.0 / allVersionsCount)) : 0)
             );
      eval_check(interp, comm);

      if ((sv != NULL) && (sv->aiCount > 0))
      {
         sprintf(comm, "DbStatsWin_PaintPie %s %d %d %d %d %d\n",
                       dbswn[target],
                       sv->hist_expir[sv->histIdx],
                       sv->hist_s1cur[sv->histIdx], sv->hist_s1old[sv->histIdx],
                       sv->hist_s2cur[sv->histIdx], sv->hist_s2old[sv->histIdx]);
         eval_check(interp, comm);
      }
      else
      {
         sprintf(comm, "DbStatsWin_PaintPie %s %d %d %d %d %d\n",
                       dbswn[target],
                       (uint)((double)obsolete / total * 128.0),
                       (uint)((double)(count[0].curVersion + obsolete) / total * 128.0),
                       (uint)((double)(count[0].allVersions + obsolete) / total * 128.0),
                       (uint)((double)(count[0].allVersions + obsolete + count[1].curVersion) / total * 128.0),
                       (uint)((double)(count[0].allVersions + obsolete + count[1].allVersions) / total * 128.0));
         eval_check(interp, comm);
      }

      if (sv != NULL)
      {
         if (dbStatsWinState[target].isForAcq == FALSE)
         {
            sprintf(comm, "pack %s.acq -side top -anchor nw -fill both -after %s.browser\n",
                          dbswn[target], dbswn[target]);
            eval_check(interp, comm);
            dbStatsWinState[target].isForAcq = TRUE;
         }

         if ( (dbStatsWinState[target].lastHistPos != sv->histIdx) &&
              (((dbStatsWinState[target].lastHistPos + 1) % STATS_HIST_WIDTH) != sv->histIdx) )
         {
            sprintf(comm, "DbStatsWin_ClearHistory %s\n", dbswn[target]);
            eval_check(interp, comm);
         }
         sprintf(comm, "DbStatsWin_AddHistory %s %d %d %d %d %d %d\n",
                       dbswn[target],
                       sv->histIdx+1,
                       sv->hist_expir[sv->histIdx],
                       sv->hist_s1cur[sv->histIdx], sv->hist_s1old[sv->histIdx],
                       sv->hist_s2cur[sv->histIdx], sv->hist_s2old[sv->histIdx]);
         eval_check(interp, comm);
         dbStatsWinState[target].lastHistPos = sv->histIdx;

         EpgAcqCtl_DescribeAcqState(&acqState);

         EpgDbAcqGetScanResults(&cni, &waitNi, &foo);
         EpgDbAcqEnableVpsPdc(TRUE);

         duration = now - sv->acqStartTime;
         if (duration == 0)
            duration = 1;

         sprintf(comm, "%s.acq.stat configure -text {"
                       "Acq Runtime:      %02d:%02d\n",
                       dbswn[target],
                       (uint)(duration / 60), (uint)(duration % 60));

         if (cni != 0)
            sprintf(comm + strlen(comm), "Channel VPS/PDC:  CNI %04X\n", cni);
         else
            strcat(comm, "Channel VPS/PDC:  ---\n");

         sprintf(comm + strlen(comm),
                       "TTX data rate:    %d baud\n"
                       "EPG data rate:    %d baud (%1.1f%% of TTX)\n"
                       "EPG page rate:    %1.2f pages/sec\n"
                       "AI recv. count:   %d\n"
                       "AI min/avg/max:   %d/%2.2f/%d sec\n",
                       (int)((sv->ttxPkgCount*45*8)/duration),
                       (int)((sv->epgPkgCount*45*8)/duration),
                       ((sv->ttxPkgCount > 0) ? ((double)sv->epgPkgCount*100.0/sv->ttxPkgCount) : 0.0),
                       ((double)sv->epgPagCount / duration),
                       sv->aiCount,
                       (int)sv->minAiDistance, sv->avgAiDistance, (int)sv->maxAiDistance
                );

         switch (acqState.mode)
         {
            case ACQMODE_PASSIVE:
               strcat(comm, "Acq mode:         passive\n");
               break;
            case ACQMODE_FORCED_PASSIVE:
               strcat(comm, "Acq mode:         forced passive\n");
               switch (acqState.passiveReason)
               {
                  case ACQPASSIVE_NO_TUNER:
                     strcat(comm, "Passive reason:   input source is not a tuner\n");
                     break;
                  case ACQPASSIVE_NO_FREQ:
                     strcat(comm, "Passive reason:   frequency unknown\n");
                     break;
                  case ACQPASSIVE_ACCESS_DEVICE:
                     strcat(comm, "Passive reason:   video device busy\n");
                     break;
                  default:
                     break;
               }
               break;

            case ACQMODE_FOLLOW_UI:
               strcat(comm, "Acq mode:         follow browser database\n");
               break;
            case ACQMODE_FOLLOW_MERGED:
               strcat(comm, "Acq mode:         merged database\n");
               break;
            case ACQMODE_CYCLIC_2:
               strcat(comm, "Acq mode:         manual\n");
               break;
            default:
               switch (acqState.cyclePhase)
               {
                  case ACQMODE_PHASE_NOWNEXT:
                     strcat(comm, "Acq mode:         cyclic, phase 'Now'\n");
                     break;
                  case ACQMODE_PHASE_STREAM1:
                     strcat(comm, "Acq mode:         cyclic, phase 'Near'\n");
                     break;
                  case ACQMODE_PHASE_STREAM2:
                     strcat(comm, "Acq mode:         cyclic, phase 'All'\n");
                     break;
                  default:
                     break;
               }
               sprintf(comm + strlen(comm),
                                "Network variance: %1.2f / %1.2f",
                                sv->count[0].variance, sv->count[1].variance
                         );
               break;
         }

         strcat(comm, "}\n");
         eval_check(interp, comm);

         // set up a time to re-display the stats if there's no EPG reception
         if (dbStatsWinState[target].updateHandler != NULL)
            Tcl_DeleteTimerHandler(dbStatsWinState[target].updateHandler);
         dbStatsWinState[target].updateHandler =
            Tcl_CreateTimerHandler(((sv->aiCount < 2) ? 2*1000 : 15*1000), StatsWin_UpdateDbStatsWinTimeout, (ClientData)target);
      }
      else
      {  // acq not running (at least for the same db) -> remove acq stats
         if (dbStatsWinState[target].isForAcq)
         {
            sprintf(comm, "%s.acq.stat configure -text {}\n"
                          "pack forget %s.acq\n",
                          dbswn[target], dbswn[target]);
            eval_check(interp, comm);

            sprintf(comm, "DbStatsWin_ClearHistory %s\n", dbswn[target]);
            eval_check(interp, comm);

            dbStatsWinState[target].isForAcq = FALSE;
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Schedule update of stats window after timeout
// - used only for stats windows with acquisition
// - timeout occurs when no AI is received
//
static void StatsWin_UpdateDbStatsWinTimeout( ClientData clientData )
{
   uint target = (uint) clientData;

   if (target < 2)
   {
      dbStatsWinState[target].updateHandler = NULL;
      AddMainIdleEvent(StatsWin_UpdateDbStatsWin, clientData, FALSE);
   }
   else
      debug1("StatsWin-UpdateDbStatsWinTimeout: illegal target %d", target);
}

// ----------------------------------------------------------------------------
// Update status line at the bottom of timescale windows
// - It contains the provider's network name and
//   PI fill percentages for stream 1 and stream 1+2
//
static void StatsWin_UpdateTimescaleStatusLine( ClientData clientData )
{
   const EPGDB_STATS *sv;
   const AI_BLOCK *pAi;
   ulong total, curVersionCount;
   uint nearPerc;
   int target;

   if (tscaleState[DB_TARGET_UI].open && (pAcqDbContext == pUiDbContext))
      target = DB_TARGET_UI;
   else if (tscaleState[DB_TARGET_ACQ].open)
      target = DB_TARGET_ACQ;
   else
      target = -1;

   if (target != -1)
   {
      sv = EpgAcqCtl_GetStatistics();
      if ((sv != NULL) && (sv->aiCount > 0))
      {
         total = sv->count[0].ai + sv->count[1].ai;
         curVersionCount  = sv->count[0].curVersion + sv->count[0].expired + sv->count[0].defective +
                            sv->count[1].curVersion + sv->count[1].expired + sv->count[1].defective;

         EpgDbLockDatabase(pAcqDbContext, TRUE);
         pAi = EpgDbGetAi(pAcqDbContext);
         if (pAi != NULL)
         {
            if (curVersionCount < total)
            {  // db not complete -> print percentage for far & near

               nearPerc = (int)((double)(sv->count[0].expired + sv->count[0].defective + sv->count[0].curVersion) * 100.0 / sv->count[0].ai);

               sprintf(comm, "%s.bottom.l configure -text {%s database %d%% complete, near data %d%%.}\n",
                             tscn[target],
                             AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop),
                             (int)((double)curVersionCount * 100.0 / total), nearPerc);
            }
            else
            {
               sprintf(comm, "%s.bottom.l configure -text {%s database 100%% complete.}\n",
                             tscn[target],
                             AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
            }
            eval_global(interp, comm);
         }
         EpgDbLockDatabase(pAcqDbContext, FALSE);
      }
   }
}

// ----------------------------------------------------------------------------
// Update status line of browser window
// - possible bits of information about browser db:
//   + allversion percentage of PI
//   + time & date of last AI reception
//   + percentage of expired PI
// - possible bits of information about acq:
//   + state: Off, Startup, Running, Stalled
//   + CNI in acq db
//   + mode: forced-passive & reason, passive, cycle state
//
void StatsWin_UpdateDbStatusLine( ClientData clientData )
{
   const EPGDB_BLOCK_COUNT * count;
   EPGDB_BLOCK_COUNT myCount[2];
   const EPGDB_STATS *sv;
   EPGACQ_DESCR acqState;
   const AI_BLOCK *pAi;
   ulong aiTotal, nearCount, allCount, expiredCount;
   time_t dbAge;

   strcpy(comm, "set dbstatus_line {");

   if (pDemoDatabase != NULL)
   {  // Demo database -> do not display statistics
      strcat(comm, "Demo database: start times are not real. ");
   }
   else
   if ((EpgDbContextIsMerged(pUiDbContext) == FALSE) && (EpgDbContextGetCni(pUiDbContext) != 0))
   {
      sv = NULL;
      if (pAcqDbContext == pUiDbContext)
      {  // acq runs for the same db -> reuse acq statistics
         sv = EpgAcqCtl_GetStatistics();
      }
      if (sv == NULL)
      {  // not the same db or acq not ready yet -> compute statistics now
         EpgDbGetStat(pUiDbContext, myCount, 0);
         count = myCount;
      }
      else
         count = sv->count;

      // get name of provider network
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAi = EpgDbGetAi(pUiDbContext);
      if (pAi != NULL)
         sprintf(comm + strlen(comm), "%s database", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
      else
         strcat(comm, "Browser database");
      EpgDbLockDatabase(pUiDbContext, FALSE);

      aiTotal      = count[0].ai + count[1].ai;
      allCount     = count[0].allVersions + count[0].expired + count[0].defective +
                     count[1].allVersions + count[1].expired + count[1].defective;
      expiredCount = count[0].expired + count[1].expired;

      // print fill percentage across all db versions
      if ( (allCount == 0) || (aiTotal == 0) ||
           (expiredCount + count[0].defective + count[1].defective >= allCount) )
      {
         strcat(comm, " is empty");
      }
      else if (allCount < aiTotal)
      {
         sprintf(comm + strlen(comm), " %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
      }
      else
         strcat(comm, " 100% complete");

      // warn about age of database if more than on hour
      dbAge = (time(NULL) - pUiDbContext->lastAiUpdate) / 60;
      if (dbAge >= 24*60)
      {
         sprintf(comm + strlen(comm), ", %1.1f days old", (double)dbAge / (24*60));
      }
      else if (dbAge >= 60)
      {
         sprintf(comm + strlen(comm), ", %ld:%02ld hours old", dbAge / 60, dbAge % 60);
      }

      // print how much of PI are expired
      if ( (allCount > 0) && (expiredCount > 0) &&
           ((dbAge >= 60) || (expiredCount * 10 >= allCount)) )
      {
         sprintf(comm + strlen(comm), ", %d%% expired", ACQ_COUNT_TO_PERCENT(expiredCount, allCount));
      }
      else if ((allCount > 0) && (allCount < aiTotal) && (count[0].ai > 0) && (aiTotal > 0))
      {  // append near percentage
         if ( ((count[0].allVersions + count[0].defective) / count[0].ai) > (allCount / aiTotal) )
         {
            sprintf(comm + strlen(comm), ", near data %d%%", ACQ_COUNT_TO_PERCENT(count[0].allVersions + count[0].defective, count[0].ai));
         }
      }
      strcat(comm, ". ");
   }

   // fetch current state and acq db statistics from acq control
   EpgAcqCtl_DescribeAcqState(&acqState);
   sv = NULL;
   aiTotal = nearCount = allCount = 0;
   if (acqState.state >= ACQDESCR_STARTING)
   {
      sv = EpgAcqCtl_GetStatistics();
      if ((sv != NULL) && (sv->aiCount > 0))
      {
         // evaluate some totals for later use in percentage evaluations
         aiTotal   = sv->count[0].ai + sv->count[1].ai;
         nearCount = sv->count[0].curVersion + sv->count[0].expired + sv->count[0].defective;
         allCount  = nearCount +
                     sv->count[1].curVersion + sv->count[1].expired + sv->count[1].defective;
      }
      else
         sv = NULL;
   }

   // describe acq state (off, wait AI, running)
   switch (acqState.state)
   {
      case ACQDESCR_DISABLED:
         strcat(comm, "Acquisition is disabled");
         break;
      case ACQDESCR_SCAN:
         break;
      case ACQDESCR_STARTING:
         if ( (acqState.dbCni == acqState.cycleCni) && (acqState.dbCni != 0) &&
              (acqState.state != ACQMODE_FORCED_PASSIVE) && (acqState.state != ACQMODE_PASSIVE) )
         {
            EpgDbLockDatabase(pAcqDbContext, TRUE);
            pAi = EpgDbGetAi(pAcqDbContext);
            if (pAi != NULL)
            {
               sprintf(comm + strlen(comm), "Acquisition is starting up for %s", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
            }
            else
               strcat(comm, "Acquisition is waiting for reception");
            EpgDbLockDatabase(pAcqDbContext, FALSE);
         }
         else
            strcat(comm, "Acquisition is waiting for reception");
         break;
      case ACQDESCR_NO_RECEPTION:
         strcat(comm, "Acquisition: no reception");
         break;
      case ACQDESCR_STALLED:
         strcat(comm, "Acquisition stalled");
         break;
      case ACQDESCR_RUNNING:
         EpgDbLockDatabase(pAcqDbContext, TRUE);
         pAi = EpgDbGetAi(pAcqDbContext);
         if (pAi != NULL)
         {
            sprintf(comm + strlen(comm), "Acquisition working on %s", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
         }
         EpgDbLockDatabase(pAcqDbContext, FALSE);
         break;
   }

   // describe acq mode, warn the user if passive
   if ((acqState.state != ACQDESCR_DISABLED) && (acqState.state != ACQDESCR_SCAN))
   {
      if (acqState.mode == ACQMODE_PASSIVE)
      {
         if ((acqState.state == ACQDESCR_RUNNING) && (sv != NULL))
         {
            sprintf(comm + strlen(comm), ", %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
         }
         strcat(comm, " (passive mode)");
      }
      else if (acqState.mode == ACQMODE_FORCED_PASSIVE)
      {
         if ((acqState.state == ACQDESCR_RUNNING) && (sv != NULL))
         {
            sprintf(comm + strlen(comm), ", %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
         }
         strcat(comm, " (forced passive)");
      }
      else if ((acqState.dbCni != acqState.cycleCni) && (acqState.cycleCni != 0))
      {
         debug2("db %04X  cycle %04X", acqState.dbCni, acqState.cycleCni);
         strcat(comm, " (provider change pending)");
      }
      else if ( (acqState.mode == ACQMODE_FOLLOW_UI) ||
                (acqState.mode == ACQMODE_FOLLOW_MERGED) ||
                (acqState.mode == ACQMODE_CYCLIC_2) )
      {
         if (sv != NULL)
         {
            sprintf(comm + strlen(comm), ", %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
         }
      }
      else
      {
         switch (acqState.cyclePhase)
         {
            case ACQMODE_PHASE_NOWNEXT:
               strcat(comm, ", phase 'Now'");
               break;
            case ACQMODE_PHASE_STREAM1:
               if (sv != NULL)
                  sprintf(comm + strlen(comm), " phase 'Near', %d%% complete", ACQ_COUNT_TO_PERCENT(nearCount, sv->count[0].ai));
               else
                  strcat(comm, " phase 'Near'");
               break;
            case ACQMODE_PHASE_STREAM2:
               if (sv != NULL)
                  sprintf(comm + strlen(comm), " phase 'All', %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
               else
                  strcat(comm, ", phase 'All'");
               break;
            default:
               break;
         }
      }
   }

   if (acqState.state == ACQDESCR_STARTING)
      strcat(comm, "...");
   else if (acqState.state != ACQDESCR_SCAN)
      strcat(comm, ".");

   strcat(comm, "}\n");
   eval_global(interp, comm);
}

// ----------------------------------------------------------------------------
// Schedule an update of all statistics output
// - called from db management after new AI was received
// - execution is delayed until acq control has finished processing new blocks
//
void StatsWin_NewAi( EPGDB_CONTEXT * dbc )
{
   dprintf1("StatsWin-NewAi: called for %s\n", (dbc==pUiDbContext)?"ui" : ((dbc==pAcqDbContext)?"acq":"?"));

   if (dbStatsWinState[DB_TARGET_ACQ].open)
   {
      AddMainIdleEvent(StatsWin_UpdateDbStatsWin, (ClientData)DB_TARGET_ACQ, FALSE);
   }
   if ( dbStatsWinState[DB_TARGET_UI].open &&
        ((pUiDbContext == pAcqDbContext) || dbStatsWinState[DB_TARGET_UI].isForAcq) )
   {
      AddMainIdleEvent(StatsWin_UpdateDbStatsWin, (ClientData)DB_TARGET_UI, FALSE);
   }

   if (tscaleState[DB_TARGET_UI].open || tscaleState[DB_TARGET_ACQ].open )
   {
      AddMainIdleEvent(StatsWin_UpdateTimescaleStatusLine, NULL, TRUE);
   }

   AddMainIdleEvent(StatsWin_UpdateDbStatusLine, NULL, TRUE);
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
         netwop_no = pPi->netwop_no;
         this_stream = EpgDbGetStream(pPi);
         this_version = EpgDbGetVersion(pPi);
         if ((this_stream == 0) && (this_version != pAi->version))
            this_stream = 2;
         else if ((this_stream == 1) && (this_version != pAi->version_swo))
            this_stream = 3;
         if (this_version == pAi->version)
            bsum += 1;

         idx = EpgDbGetPiBlockIndex(AI_GET_NETWOP_N(pAi, netwop_no)->startNo, pPi->block_no);
         if (idx < 5)
         {
            sprintf(comm, "TimeScale_MarkNow %s.top.n%d %d red\n", tscn[target], netwop_no, idx);
            eval_check(interp, comm);
         }

         if (start_time[netwop_no] != 0)
         {
            if ((pPi->start_time == stop_time[netwop_no]) &&
                ((PI_HAS_SHORT_INFO(pPi) ^ has_short_info[netwop_no]) == FALSE) &&
                ((PI_HAS_LONG_INFO(pPi) ^ has_long_info[netwop_no]) == FALSE) &&
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
            has_short_info[netwop_no] = PI_HAS_SHORT_INFO(pPi);
            has_long_info[netwop_no]  = PI_HAS_LONG_INFO(pPi);
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

   pPi = EpgDbGetFirstObsoletePi(dbc);
   if ((pAi != NULL) && (pPi != NULL))
   {
      do
      {
         bsum += 1;
         idx = EpgDbGetPiBlockIndex(AI_GET_NETWOP_N(pAi, pPi->netwop_no)->startNo, pPi->block_no);
         if (idx < 5)
         {  // mark NOW button as expired
            sprintf(comm, "TimeScale_MarkNow %s.top.n%d %d yellow\n", tscn[target], pPi->netwop_no, idx);
            eval_check(interp, comm);
         }
         StatsWin_DisplayPi(target, pPi->netwop_no, 4, pPi->start_time, pPi->stop_time,
                            PI_HAS_SHORT_INFO(pPi), PI_HAS_LONG_INFO(pPi));

         pPi = EpgDbGetNextObsoletePi(dbc, pPi);
      }
      while (pPi != NULL);
   }

   // display summary at the bottom of the window
   sprintf(comm, "%s.bottom.l configure -text {%s database %d%% complete.}\n",
                 tscn[target],
                 (EpgDbContextIsMerged(dbc) ?
                   "Merged" :
                   ((pAi != NULL) ? (char*)AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop) : "")),
                 ((total > 0) ? (int)((double)bsum * 100 / total) : 0));
   eval_check(interp, comm);

   EpgDbLockDatabase(dbc, FALSE);
}

// ----------------------------------------------------------------------------
// update history diagram and stats text
//
static void StatsWin_RebuildCanvas( ClientData clientData )
{
   const AI_BLOCK  *pAi;
   const AI_NETWOP *pNetwops;
   EPGDB_CONTEXT   *dbc;
   uchar netwop;
   uint  target;

   target = (int) clientData;
   if (target < 2)
   {
      if (tscaleState[target].open)
      {
         dbc = ((target == 0) ? pUiDbContext : pAcqDbContext);

         // remove the previous timescales
         sprintf(comm, "foreach temp [info command %s.top.n*] {pack forget $temp; destroy $temp}\n", tscn[target]);
         eval_check(interp, comm);
         tscaleState[target].highlighted = 0xff;

         // create the timescales anew
         EpgDbLockDatabase(dbc, TRUE);
         pAi = EpgDbGetAi(dbc);
         if (pAi != NULL)
         {
            pNetwops = AI_GET_NETWOPS(pAi);
            for (netwop=0; netwop < pAi->netwopCount; netwop++)
            {
               sprintf(comm, "TimeScale_Create %s.top.n%d {%s}\n", tscn[target], netwop, AI_GET_STR_BY_OFF(pAi, pNetwops[netwop].off_name));
               eval_check(interp, comm);
            }
         }
         EpgDbLockDatabase(dbc, FALSE);

         // preload the timescales
         StatsWin_Load(target, dbc);
      }
   }
   else
      debug1("StatsWin_RebuildCanvas: illegal target: %d", target);
}

// ----------------------------------------------------------------------------
// Notify timescale windows about an AI version change
// - number and names of netwops might have changed,
//   range of valid PI might have changed, and PI hence gotten removed
// - scale contents have to be recolored (shaded) to reflect obsolete version
//
void StatsWin_VersionChange( void )
{
   // determine in which of the opened timescale windows acq is running
   if ( tscaleState[DB_TARGET_ACQ].open )
   {
      AddMainIdleEvent(StatsWin_RebuildCanvas, (ClientData) DB_TARGET_ACQ, TRUE);
   }
   else if ( tscaleState[DB_TARGET_UI].open && (pAcqDbContext == pUiDbContext) )
   {
      AddMainIdleEvent(StatsWin_RebuildCanvas, (ClientData) DB_TARGET_UI, TRUE);
   }
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
                    "wm resizable %s 0 0\n"
                    "wm group %s .\n"
                    "frame %s.top\n",
                    tscn[target], tscn[target], tscn[target], tscn[target], tscn[target]);
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
   const char * const pUsage = "Usage: C_StatsWin_ToggleTimescaleUi ui|acq [0|1]";
   EPGDB_CONTEXT *dbc = NULL;
   int target, newState;
   int result = TCL_OK;

   dbc = NULL;  // avoid compiler warnings
   target = 0;

   if ((argc == 2) || (argc == 3))
   {
      // determine target from first parameter: ui or acq db context
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
         if (Tcl_GetBoolean(interp, argv[2], &newState))
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

            sprintf(comm, "bind %s.bottom <Destroy> {}\n"
                          "destroy %s; update",
                          tscn[target], tscn[target]);
            eval_check(interp, comm);
         }
         sprintf(comm, "set menuStatusTscaleOpen(%s) %d\n", argv[1], tscaleState[target].open);
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
// Create window with acq statistics
//
static int StatsWin_ToggleDbStats( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_StatsWin_ToggleDbStats ui|acq [0|1]";
   const EPGDB_STATS * sv;
   EPGDB_CONTEXT * dbc;
   int target, newState, lastHistPos;
   int result = TCL_OK;

   dbc = NULL;  // avoid compiler warnings
   target = -1;

   if ((argc == 2) || (argc == 3))
   {
      // determine target from first parameter: ui or acq db context
      if ((strcmp(argv[1], "ui") == 0) || (strcmp(argv[1], dbswn[DB_TARGET_UI]) == 0))
      {
         dbc = pUiDbContext;
         target = DB_TARGET_UI;
      }
      else if ((strcmp(argv[1], "acq") == 0) || (strcmp(argv[1], dbswn[DB_TARGET_ACQ]) == 0))
      {
         dbc = pAcqDbContext;
         target = DB_TARGET_ACQ;
      }
   }

   if (target != -1)
   {
      result = TCL_OK;
      // determine new state from optional second parameter: 0, 1 or toggle
      if (argc == 2)
      {  // no state parameter -> toggle state
         newState = ! dbStatsWinState[target].open;
      }
      else
      {
         result = Tcl_GetBoolean(interp, argv[2], &newState);
      }

      if ( (result == TCL_OK) && (newState != dbStatsWinState[target].open) )
      {
         if (dbStatsWinState[target].open == FALSE)
         {  // window shall be opened
            sprintf(comm, "DbStatsWin_Create %s\n", dbswn[target]);
            eval_check(interp, comm);

            dbStatsWinState[target].open = TRUE;
            dbStatsWinState[target].isForAcq = FALSE;
            dbStatsWinState[target].lastHistPos = 0;
            dbStatsWinState[target].updateHandler = NULL;

            if ((target == DB_TARGET_ACQ) || (pAcqDbContext == pUiDbContext))
            {
               sv = EpgAcqCtl_GetStatistics();
               if (sv != NULL)
               {
                  if ((sv->histIdx > 1) && (sv->histIdx != STATS_HIST_WIDTH - 1))
                  {
                     sprintf(comm, "DbStatsWin_PaintPie %s %d %d %d %d %d\n",
                                   dbswn[target],
                                   sv->hist_expir[sv->histIdx],
                                   sv->hist_s1cur[sv->histIdx], sv->hist_s1old[sv->histIdx],
                                   sv->hist_s2cur[sv->histIdx], sv->hist_s2old[sv->histIdx]);
                     eval_check(interp, comm);

                     for (lastHistPos=0; lastHistPos < sv->histIdx; lastHistPos++)
                     {
                        sprintf(comm, "DbStatsWin_AddHistory %s %d %d %d %d %d %d\n",
                                      dbswn[target], lastHistPos+1,
                                      sv->hist_expir[lastHistPos],
                                      sv->hist_s1cur[lastHistPos], sv->hist_s1old[lastHistPos],
                                      sv->hist_s2cur[lastHistPos], sv->hist_s2old[lastHistPos]);
                        eval_check(interp, comm);
                     }
                     dbStatsWinState[target].lastHistPos = sv->histIdx - 1;
                  }
               }
               EpgDbAcqEnableVpsPdc(TRUE);
            }

            // display initial summary
            StatsWin_UpdateDbStatsWin((ClientData)target);
         }
         else
         {  // destroy the window
            sprintf(comm, "bind %s <Destroy> {}; destroy %s", dbswn[target], dbswn[target]);
            eval_check(interp, comm);

            dbStatsWinState[target].open = FALSE;

            // disable the VPS/PDC acquisition when the last stats
            if (EpgAcqCtl_ScanIsActive() == FALSE)
            {
               if ( ((target == DB_TARGET_UI) && (dbStatsWinState[DB_TARGET_ACQ].open == FALSE)) ||
                    ((target == DB_TARGET_ACQ) && ((dbStatsWinState[DB_TARGET_UI].open == FALSE) ||
                                                   (dbStatsWinState[DB_TARGET_UI].isForAcq == FALSE))) )
               {
                  EpgDbAcqEnableVpsPdc(FALSE);
               }
            }
         }
         // set the state of the checkbutton of the according menu entry
         sprintf(comm, "set menuStatusStatsOpen(%s) %d\n", argv[1], dbStatsWinState[target].open);
         eval_check(interp, comm);
      }
   }
   else
   {  // parameter error -> display usage
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Notify the windows of this module about a database provider change
// - when provider changes, the according window is rebuild
// - when acq shift from ui db to a separate acq db, the highlighted
//   netwop in the ui window must be unmarked
//
void StatsWin_ProvChange( int target )
{
   if (target < 2)
   {
      // close separate acq timescale window, if ui now has the same db
      if ( (tscaleState[DB_TARGET_ACQ].open) &&
           ((pAcqDbContext == pUiDbContext) || (pAcqDbContext == NULL)) )
      {
         sprintf(comm, "destroy %s", tscn[DB_TARGET_ACQ]);
         eval_check(interp, comm);
      }

      // rebuild network timescales
      if (tscaleState[target].open)
      {
         AddMainIdleEvent(StatsWin_RebuildCanvas, (ClientData) target, TRUE);
      }

      // if a network in the inactive UI window is still marked, unmark it
      if ( (tscaleState[DB_TARGET_UI].open) &&
           (tscaleState[DB_TARGET_UI].highlighted != 0xff) )
      {
         StatsWin_HighlightNetwop(DB_TARGET_UI, 0, 3);
      }

      // if the acq stats window is open, update its content
      if (dbStatsWinState[target].open)
      {
         AddMainIdleEvent(StatsWin_UpdateDbStatsWin, (ClientData)target, TRUE);
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
      Tcl_CreateCommand(interp, "C_StatsWin_ToggleDbStats", StatsWin_ToggleDbStats, (ClientData) NULL, NULL);
   }
   else
      debug0("StatsWin-Create: commands are already created");

   memset(tscaleState, 0, sizeof(tscaleState));
   memset(dbStatsWinState, 0, sizeof(dbStatsWinState));
}

