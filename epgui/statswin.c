/*
 *  Nextview EPG GUI: Statistics output
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
 *    Implements pop-up windows with statistics about the current
 *    state of the databases and the acquisition process. There are
 *    two different popup types: The timescale popups have one scale
 *    for each network covered by the provider which represent the
 *    next five days; the ranges which are actually covered by PI
 *    blocks in the database are marked red or blue in the scales.
 *    The database statistics popups contain statistic summaries
 *    for the database, both in textual format and as a pie chart.
 *
 *    Both popup types are updated dynamically while acquisition is
 *    running. They can be opened separately for the browser and
 *    acquisition database.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: statswin.c,v 1.41 2001/09/12 18:44:07 tom Exp tom $
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
#include "epgctl/epgacqctl.h"
#include "epgctl/epgscan.h"
#include "epgui/epgmain.h"
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

typedef enum
{
   STREAM_COLOR_CURRENT_V1,
   STREAM_COLOR_CURRENT_V2,
   STREAM_COLOR_OLD_V1,
   STREAM_COLOR_OLD_V2,
   STREAM_COLOR_DEFECTIVE,
   STREAM_COLOR_EXPIRED,
   STREAM_COLOR_MISSING,
   STREAM_COLOR_COUNT
} STREAM_COLOR;

const char * const streamColors[STREAM_COLOR_COUNT] =
{
   "red",           // stream 1, current version -> red
   "#4040ff",       // stream 1, current version -> blue
   "#A52A2A",       // stream 2, obsolete version -> brown
   "#483D8B",       // stream 2, obsolete version -> DarkSlate
   "yellow",        // defect block -> yellow
   "yellow",        // expired block -> yellow too
   "#888888"        // missing block -> gray
};

// colors to mark the name of the network which currently receives PI in the timescales popup
#define HIGHILIGHT_BG_COL_1     "#ffc0c0"      // light red
#define HIGHILIGHT_BG_COL_2     "#c0c0ff"      // light blue

static bool isStatsDemoMode;


static void StatsWin_UpdateDbStatsWinTimeout( ClientData clientData );

// ----------------------------------------------------------------------------
// display PI time range and Now box
//
static void StatsWin_DisplayPi( int target, uchar netwop, STREAM_COLOR streamCol,
                                time_t start, time_t stop, uchar hasShortInfo, uchar hasLongInfo, bool isLast )
{
   uint startOff, stopOff;

   if ((stop > tscaleState[target].startTime) && (streamCol < STREAM_COLOR_COUNT))
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

      sprintf(comm, "TimeScale_AddPi %s.top.n%d %d %d %s %d %d %d\n",
              tscn[target], netwop, startOff, stopOff,
              streamColors[streamCol],
              hasShortInfo, hasLongInfo, isLast);
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
         tscaleState[target].lastStream  = 0xff;
      }

      // highlight the new netwop
      if (stream < 2)
      {
         sprintf(comm, "%s.top.n%d.name config -bg %s\n",
                       tscn[target], netwop, (stream == 0) ? HIGHILIGHT_BG_COL_1 : HIGHILIGHT_BG_COL_2);
         eval_check(interp, comm);

         tscaleState[target].highlighted = netwop;
         tscaleState[target].lastStream  = stream;
      }
   }
}

// ----------------------------------------------------------------------------
// Display the time range covered by a new PI in the timescale canvas
// - The first 5 blocks (according to the numbers in the AI per netwop) are
//   additionally inserted to the 5 separate buttons in front of the scales
//   to reflect NOW coverage.
// - If the new PI was added to the list of expired/defective blocks
//   or if a PI in that list overlaps with the new block, the time range
//   is marked in yellow.
//
void StatsWin_NewPi( EPGDB_CONTEXT * dbc, const PI_BLOCK *pPi, uchar stream )
{
   const AI_BLOCK  *pAi;
   const AI_NETWOP *pNetwop;
   uint blockOff;
   time_t now = time(NULL);
   int target;
   bool isMerged, isLast;

   for (target=0; target < 2; target++)
   {
      if ( tscaleState[target].open &&
           (dbc == ((target == DB_TARGET_UI) ? pUiDbContext : pAcqDbContext)) )
      {
         isMerged = EpgDbContextIsMerged(dbc);
         EpgDbLockDatabase(dbc, TRUE);
         pAi = EpgDbGetAi(dbc);
         if ((pAi != NULL) && (pPi->netwop_no < pAi->netwopCount))
         {
            pNetwop = AI_GET_NETWOP_N(pAi, pPi->netwop_no);
            if (isMerged == FALSE)
            {
               blockOff = EpgDbGetPiBlockIndex(pNetwop->startNo, pPi->block_no);
               if (blockOff < 5)
               {
                  sprintf(comm, "TimeScale_MarkNow %s.top.n%d %d %s\n",
                                tscn[target], pPi->netwop_no, blockOff,
                                (pPi->stop_time >= now) ? streamColors[STREAM_COLOR_CURRENT_V1] : streamColors[STREAM_COLOR_EXPIRED]);
                  eval_check(interp, comm);
               }
            }

            isLast = (!isMerged && (pPi->block_no == pNetwop->stopNoSwo));
            if (EpgDbSearchObsoletePi(dbc, pPi->netwop_no, pPi->start_time, pPi->stop_time) != NULL)
            {  // conflict with other block -> mark as faulty
               StatsWin_DisplayPi(target, pPi->netwop_no, STREAM_COLOR_DEFECTIVE, pPi->start_time, pPi->stop_time, TRUE, TRUE, isLast);
            }
            else
            {
               StatsWin_DisplayPi(target, pPi->netwop_no, stream, pPi->start_time, pPi->stop_time,
                                  PI_HAS_SHORT_INFO(pPi), PI_HAS_LONG_INFO(pPi), isLast);
            }

            StatsWin_HighlightNetwop(target, pPi->netwop_no, stream);
         }
         EpgDbLockDatabase(dbc, FALSE);
      }
   }
}

// ----------------------------------------------------------------------------
// Update the database statistics window
// - consists of two parts:
//   + the upper one describes the current state of the database, with a pie chart
//     and statistics of PI block counts against the numbers from the AI block
//   + the lower one describes the current acquisition state and a histroy diagram
//     of current and past PI block count percentages
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
   time_t acqMinTime[2];
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
         memset(acqMinTime, 0, sizeof(acqMinTime));
         EpgDbGetStat(dbc, myCount, acqMinTime, 0);
         count = myCount;
      }
      else
         count = sv->count;

      if (EpgDbContextIsMerged(dbc) == FALSE)
      {
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

         total            = count[0].ai + count[1].ai;
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
                       "expired blocks:   %d%%: %ld (%ld + %ld)\n"
                       "defective blocks: %d%%: %ld (%ld + %ld)"
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
                       ((total > 0) ? (int)((double)allVersionsCount * 100.0 / total) : 100),
                       ((total > 0) ? (int)((double)curVersionCount * 100.0 / total) : 100),
                       ((allVersionsCount > 0) ? ((int)((double)(count[0].expired + count[1].expired) * 100.0 / allVersionsCount)) : 0),
                          count[0].expired + count[1].expired, count[0].expired, count[1].expired,
                       ((allVersionsCount > 0) ? ((int)((double)(count[0].defective + count[1].defective) * 100.0 / allVersionsCount)) : 0),
                          count[0].defective + count[1].defective, count[0].defective, count[1].defective
                );
         eval_check(interp, comm);

         if (total > 0)
         {
            sprintf(comm, "DbStatsWin_PaintPie %s %d %d %d %d %d %d %d\n",
                          dbswn[target],
                          (int)((double)(count[0].defective + count[0].expired) / total * 359.9),
                          (int)((double)(count[0].curVersion + count[0].defective + count[0].expired) / total * 359.9),
                          (int)((double)(count[0].allVersions + count[0].defective + count[0].expired) / total * 359.9),
                          (int)((double)count[0].ai / total * 359.9),
                          (int)((double)(count[0].ai + count[1].defective + count[1].expired) / total * 359.9),
                          (int)((double)(count[0].ai + count[1].curVersion + count[1].defective + count[1].expired) / total * 359.9),
                          (int)((double)(count[0].ai + count[1].allVersions + count[1].defective + count[1].expired) / total * 359.9));
         }
         else
         {
            sprintf(comm, "DbStatsWin_ClearPie %s\n", dbswn[target]);
         }
         eval_check(interp, comm);
      }
      else
      {  // Merged database

         allVersionsCount = count[0].allVersions + count[0].expired + count[0].defective +
                            count[1].allVersions + count[1].expired + count[1].defective;

         sprintf(comm, "%s.browser.stat configure -text \""
                       "EPG Provider:     Merged database\n"
                       "Block count db:   %ld\n"
                       "\nFor more info please refer to the\n"
                       "original databases (more statistics\n"
                       "will be added here in the future)\n"
                       "\"\n",
                       dbswn[target],
                       allVersionsCount
                );
         eval_check(interp, comm);

         sprintf(comm, "DbStatsWin_ClearPie %s\n", dbswn[target]);
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
                       sv->histIdx + 1,
                       sv->hist[sv->histIdx].expir,
                       sv->hist[sv->histIdx].s1cur, sv->hist[sv->histIdx].s1old,
                       sv->hist[sv->histIdx].s2cur, sv->hist[sv->histIdx].s2old);
         eval_check(interp, comm);
         dbStatsWinState[target].lastHistPos = sv->histIdx;

         EpgAcqCtl_DescribeAcqState(&acqState);

         EpgDbAcqGetScanResults(&cni, &waitNi, &foo);
         EpgDbAcqResetVpsPdc();

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
                       "AI min/avg/max:   %d/%2.2f/%d sec\n"
                       "PI rx repetition: %d/%.2f/%.2f now/s1/s2\n",
                       ((duration > 0) ? (int)((sv->ttxPkgCount*45*8)/duration) : 0),
                       ((duration > 0) ? (int)((sv->epgPkgCount*45*8)/duration) : 0),
                       ((sv->ttxPkgCount > 0) ? ((double)sv->epgPkgCount*100.0/sv->ttxPkgCount) : 0.0),
                       ((duration > 0) ? ((double)sv->epgPagCount / duration) : 0),
                       sv->aiCount,
                       (int)sv->minAiDistance,
                          ((sv->aiCount > 1) ? ((double)sv->sumAiDistance / (sv->aiCount - 1)) : 0),
                          (int)sv->maxAiDistance,
                       sv->nowNextMaxAcqRepCount, sv->count[0].avgAcqRepCount, sv->count[1].avgAcqRepCount
                );

         switch (acqState.mode)
         {
            case ACQMODE_PASSIVE:
               strcat(comm, "Acq mode:         passive\n");
               break;
            case ACQMODE_EXTERNAL:
               strcat(comm, "Acq mode:         external\n");
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
                  case ACQPASSIVE_NO_DB:
                     strcat(comm, "Passive reason:   database missing\n");
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
               if (acqState.cniCount <= 1)
                  strcat(comm, "Acq mode:         manual\n");
               else
               {
                  if (acqState.cyclePhase == ACQMODE_PHASE_STREAM2)
                     strcat(comm, "Acq mode:         manual, phase 'All'\n");
                  else
                     strcat(comm, "Acq mode:         manual, phase 'Complete'\n");
               }
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
                  case ACQMODE_PHASE_MONITOR:
                     strcat(comm, "Acq mode:         cyclic, phase 'Complete'\n");
                     break;
                  default:
                     break;
               }
               break;
         }

         if (ACQMODE_IS_CYCLIC(acqState.mode))
         {
            sprintf(comm + strlen(comm), "Network variance: %1.2f / %1.2f",
                                         sv->count[0].variance, sv->count[1].variance);
         }

         strcat(comm, "}\n");
         eval_check(interp, comm);

         // set up a timer to re-display the stats in case there's no EPG reception
         if (dbStatsWinState[target].updateHandler != NULL)
            Tcl_DeleteTimerHandler(dbStatsWinState[target].updateHandler);
         dbStatsWinState[target].updateHandler =
            Tcl_CreateTimerHandler(((sv->aiCount < 2) ? 2*1000 : 15*1000), StatsWin_UpdateDbStatsWinTimeout, (ClientData)target);
      }
      else
      {  // acq not running (at least for the same db)
         if (dbStatsWinState[target].isForAcq)
         {  // acq stats are still being displayed -> remove them
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
// - only called when acquisition is running for the respective database
// - status line contains the provider's network name and
//   PI fill percentages for stream 1 and stream 1+2
//
static void StatsWin_UpdateTimescaleStatusLine( ClientData clientData )
{
   const EPGDB_STATS *sv;
   const AI_BLOCK *pAi;
   ulong total, curVersionCount;
   uint nearPerc;
   int target;

   for (target=0; target < 2; target++)
   {
      if ( tscaleState[target].open && (pAcqDbContext != NULL) &&
           ((target == DB_TARGET_ACQ) || (pUiDbContext == pAcqDbContext)) )
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

                  if (sv->count[0].ai > 0)
                     nearPerc = (int)((double)(sv->count[0].expired + sv->count[0].defective + sv->count[0].curVersion) * 100.0 / sv->count[0].ai);
                  else
                     nearPerc = 100;

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
   time_t dbAge, acqMinTime[2];

   strcpy(comm, "set dbstatus_line {");

   if (isStatsDemoMode)
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
         memset(acqMinTime, 0, sizeof(acqMinTime));
         EpgDbGetStat(pUiDbContext, myCount, acqMinTime, 0);
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
              (acqState.mode != ACQMODE_FORCED_PASSIVE) &&
              (acqState.mode != ACQMODE_PASSIVE) &&
              (acqState.mode != ACQMODE_EXTERNAL) )
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
         if ( (acqState.mode != ACQMODE_PASSIVE) &&
              (acqState.mode != ACQMODE_EXTERNAL) &&
              (acqState.mode != ACQMODE_FORCED_PASSIVE) )
         {
            EpgDbLockDatabase(pAcqDbContext, TRUE);
            pAi = EpgDbGetAi(pAcqDbContext);
            if (pAi != NULL)
            {
               sprintf(comm + strlen(comm), " on %s", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
            }
            EpgDbLockDatabase(pAcqDbContext, FALSE);
         }
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
      else if (acqState.mode == ACQMODE_EXTERNAL)
      {
         if ((acqState.state == ACQDESCR_RUNNING) && (sv != NULL))
         {
            sprintf(comm + strlen(comm), ", %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
         }
         strcat(comm, " (external)");
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
         debug2("StatsWin-UpdateDbStatusLine: prov change pending: db %04X  cycle %04X", acqState.dbCni, acqState.cycleCni);
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
            case ACQMODE_PHASE_MONITOR:
               if (sv != NULL)
                  sprintf(comm + strlen(comm), " phase 'Complete', %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
               else
                  strcat(comm, ", phase 'Complete'");
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
   dprintf1("StatsWin-NewAi: called for %s\n", ((dbc==pUiDbContext) && (dbc==pAcqDbContext)) ? "ui=acq" : ((dbc==pUiDbContext) ? "ui" : ((dbc==pAcqDbContext)?"acq":"?")));

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
   time_t now, max_time;
   bool   has_short_info[MAX_NETWOP_COUNT];
   bool   has_long_info[MAX_NETWOP_COUNT];
   uchar  this_version, cur_stream, rx_stream;
   STREAM_COLOR  streamCol[MAX_NETWOP_COUNT], this_streamCol;
   uint   block_no[MAX_NETWOP_COUNT];
   uchar  netwop_no;
   ulong  bcnt, bsum, total;
   int    idx;
   bool   isMerged, isLast;

   EpgDbLockDatabase(dbc, TRUE);
   memset(start_time, 0, sizeof(start_time));
   now = time(NULL);
   bsum = total = 0;

   isMerged = EpgDbContextIsMerged(dbc);
   pAi = EpgDbGetAi(dbc);
   pPi = EpgDbSearchFirstPi(dbc, NULL);
   if ((pAi != NULL) && (pPi != NULL))
   {
      // loop over all PI in the database (i.e. in parallel across all netwops)
      do
      {
         netwop_no     = pPi->netwop_no;
         this_version  = EpgDbGetVersion(pPi);
         rx_stream     = EpgDbGetStream(pPi);
         cur_stream    = (isMerged ? rx_stream : EpgDbGetStreamByBlockNo(dbc, pPi->block_no, pPi->netwop_no));

         // determine color of the current PI: depends on its stream and version
         this_streamCol = cur_stream;
         if ((cur_stream == 0) && ((this_version != pAi->version) || (rx_stream != cur_stream)))
            this_streamCol = STREAM_COLOR_OLD_V1;
         else if ((rx_stream == 1) && ((this_version != pAi->version_swo) || (rx_stream != cur_stream)))
            this_streamCol = STREAM_COLOR_OLD_V2;

         if ((this_version == pAi->version) && (rx_stream == cur_stream))
            bsum += 1;

         if (isMerged == FALSE)
         {
            idx = EpgDbGetPiBlockIndex(AI_GET_NETWOP_N(pAi, netwop_no)->startNo, pPi->block_no);
            if (idx < 5)
            {
               sprintf(comm, "TimeScale_MarkNow %s.top.n%d %d %s\n", tscn[target], netwop_no, idx,
                             (pPi->stop_time >= now) ? streamColors[STREAM_COLOR_CURRENT_V1] : streamColors[STREAM_COLOR_EXPIRED]);
               eval_check(interp, comm);
            }
         }

         // check if there's a PI in the cache for this network
         if (start_time[netwop_no] != 0)
         {
            // check if the parameters of the current PI match the one in the cache
            if ((pPi->start_time == stop_time[netwop_no]) &&
                ((PI_HAS_SHORT_INFO(pPi) ^ has_short_info[netwop_no]) == FALSE) &&
                ((PI_HAS_LONG_INFO(pPi) ^ has_long_info[netwop_no]) == FALSE) &&
                (this_streamCol == streamCol[netwop_no]) )
            {
               // match -> just enlarge the time range of the cached PI
               stop_time[netwop_no] = pPi->stop_time;
               block_no[netwop_no]  = pPi->block_no;
            }
            else
            {  // no match -> draw the cached PI
               StatsWin_DisplayPi(target, netwop_no, streamCol[netwop_no], start_time[netwop_no], stop_time[netwop_no], has_short_info[netwop_no], has_long_info[netwop_no], FALSE);
               if ((pPi->block_no != ((block_no[netwop_no] + 1) % 0x10000)) && !isMerged)
               {  // block missing between the previous and the last -> mark gray
                  StatsWin_DisplayPi(target, netwop_no, STREAM_COLOR_MISSING, stop_time[netwop_no], pPi->start_time, FALSE, FALSE, FALSE);
               }
               // mark the cache as empty
               start_time[netwop_no] = 0L;
            }
         }

         if (start_time[netwop_no] == 0)
         {
            // place the current item in the cache
            start_time[netwop_no]     = pPi->start_time;
            stop_time[netwop_no]      = pPi->stop_time;
            has_short_info[netwop_no] = PI_HAS_SHORT_INFO(pPi);
            has_long_info[netwop_no]  = PI_HAS_LONG_INFO(pPi);
            streamCol[netwop_no]      = this_streamCol;
            block_no[netwop_no]       = pPi->block_no;
         }

         pPi = EpgDbSearchNextPi(dbc, NULL, pPi);
      }
      while (pPi != NULL);

      // display the last (cached) PI of all networks
      pNetwops = AI_GET_NETWOPS(pAi);
      for (netwop_no=0; netwop_no < pAi->netwopCount; netwop_no++)
      {
         // calculate sum of all PI listed in AI for all networks
         bcnt = EpgDbGetPiBlockCount(pNetwops[netwop_no].startNo, pNetwops[netwop_no].stopNoSwo);
         total += bcnt;

         if (start_time[netwop_no] != 0)
         {
            StatsWin_DisplayPi(target, netwop_no, streamCol[netwop_no], start_time[netwop_no], stop_time[netwop_no], has_short_info[netwop_no], has_long_info[netwop_no], FALSE);

            if ((block_no[netwop_no] != pNetwops[netwop_no].stopNoSwo) && !isMerged)
            {  // at least one block missing at the end -> mark estimated range as missing
               max_time = dbc->lastAiUpdate + pNetwops[netwop_no].dayCount * (24*60*60);
               if (max_time <= stop_time[netwop_no])
                  max_time = stop_time[netwop_no] + 60*60;
               StatsWin_DisplayPi(target, netwop_no, STREAM_COLOR_MISSING, stop_time[netwop_no], max_time, FALSE, FALSE, FALSE);
            }
         }
         else
         {  // no PI at all found for this network
            if ((bcnt > 0) && !isMerged)
            {  // however there should be some -> mark as missing
               max_time = dbc->lastAiUpdate + pNetwops[netwop_no].dayCount * (24*60*60);
               StatsWin_DisplayPi(target, netwop_no, STREAM_COLOR_MISSING, 0, max_time, FALSE, FALSE, FALSE);
            }
         }
      }
   }

   // loop over all defective PI in the database and mark them yellow
   pPi = EpgDbGetFirstObsoletePi(dbc);
   if ((pAi != NULL) && (pPi != NULL))
   {
      do
      {
         bsum += 1;
         if (isMerged == FALSE)
         {
            idx = EpgDbGetPiBlockIndex(AI_GET_NETWOP_N(pAi, pPi->netwop_no)->startNo, pPi->block_no);
            if (idx < 5)
            {  // mark NOW button as defective/expired
               sprintf(comm, "TimeScale_MarkNow %s.top.n%d %d %s\n", tscn[target],
                             pPi->netwop_no, idx, streamColors[STREAM_COLOR_EXPIRED]);
               eval_check(interp, comm);
            }
         }
         isLast = (!isMerged && (pPi->block_no == AI_GET_NETWOP_N(pAi, pPi->netwop_no)->stopNoSwo));
         StatsWin_DisplayPi(target, pPi->netwop_no, STREAM_COLOR_DEFECTIVE, pPi->start_time, pPi->stop_time,
                            PI_HAS_SHORT_INFO(pPi), PI_HAS_LONG_INFO(pPi), isLast);

         pPi = EpgDbGetNextObsoletePi(dbc, pPi);
      }
      while (pPi != NULL);
   }

   // display summary at the bottom of the window
   if (EpgDbContextIsMerged(dbc) == FALSE)
   {
      sprintf(comm, "%s.bottom.l configure -text {%s database %d%% complete.}\n",
                    tscn[target],
                    ((pAi != NULL) ? (char*)AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop) : ""),
                    ((total > 0) ? (int)((double)bsum * 100 / total) : 100));
      eval_check(interp, comm);
   }
   else
      sprintf(comm, "%s.bottom.l configure -text {Merged database.}\n", tscn[target]);
   eval_check(interp, comm);

   EpgDbLockDatabase(dbc, FALSE);
}

// ----------------------------------------------------------------------------
// Update time scale popup window for new provider or AI version
//
static void StatsWin_RebuildCanvas( ClientData clientData )
{
   EPGDB_CONTEXT   *dbc;
   uint  target = (uint) clientData;

   dprintf2("StatsWin-RebuildCanvas: called for %d: %s\n", target, ((target == DB_TARGET_UI) ? "ui" : ((target == DB_TARGET_ACQ) ? "acq" : "unknown")));

   if ( tscaleState[target].open )
   {
      dbc = ((target == DB_TARGET_UI) ? pUiDbContext : pAcqDbContext);

      if (dbc != NULL)
      {
         // reset the highlight index
         StatsWin_HighlightNetwop(target, 0xff, 0xff);

         // update the network labels in the popup window
         sprintf(comm, "TimeScale_Open %s 0x%04X %s %d\n",
                       tscn[target], EpgDbContextGetCni(dbc),
                       ((target == DB_TARGET_UI) ? "ui" : "acq"),
                       EpgDbContextIsMerged(dbc));
         eval_check(interp, comm);

         // preload the timescales
         StatsWin_Load(target, dbc);
      }
      else
      {  // context no longer exists (e.g. acq stopped) -> close the popup
         sprintf(comm, "destroy %s", tscn[target]);
         eval_check(interp, comm);
      }
   }
}

// ----------------------------------------------------------------------------
// Notify timescale windows about an AI version change
// - number and names of netwops might have changed,
//   range of valid PI might have changed, and PI hence gotten removed
// - scale contents have to be recolored (shaded) to reflect obsolete version
//
void StatsWin_VersionChange( void )
{
   dprintf0("StatsWin-VersionChange: called\n");

   if ( tscaleState[DB_TARGET_ACQ].open &&
        tscaleState[DB_TARGET_UI].open && (pAcqDbContext == pUiDbContext) )
   {
      // browser timescales refer to acq db too -> update both timescale popups
      // note: connections to merged db are not detected here
      AddMainIdleEvent(StatsWin_RebuildCanvas, (ClientData) DB_TARGET_UI, FALSE);
      AddMainIdleEvent(StatsWin_RebuildCanvas, (ClientData) DB_TARGET_ACQ, FALSE);
   }
   else if ( tscaleState[DB_TARGET_ACQ].open )
   {
      AddMainIdleEvent(StatsWin_RebuildCanvas, (ClientData) DB_TARGET_ACQ, FALSE);
   }
   else if ( tscaleState[DB_TARGET_UI].open && (pAcqDbContext == pUiDbContext) )
   {
      AddMainIdleEvent(StatsWin_RebuildCanvas, (ClientData) DB_TARGET_UI, FALSE);
   }
}

// ----------------------------------------------------------------------------
// Open or close the timescales popup window
// - called by the "Statistics" button in the main window
// - also called when the window is destroyed (e.g. by the wm)
// - can not be opened until an AI block has been received
//   because we need to know the number and names of netwops in the EPG
//
static int StatsWin_ToggleTimescale( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_StatsWin_ToggleTimescale ui|acq [0|1]";
   EPGDB_CONTEXT *dbc = NULL;
   int target, newState;
   int result = TCL_OK;

   dbc = NULL;  // avoid compiler warnings
   target = 0;

   if ((argc == 2) || (argc == 3))
   {
      // determine target from first parameter: ui or acq db context
      if (strcmp(argv[1], "ui") == 0)
      {
         dbc = pUiDbContext;
         target = DB_TARGET_UI;
      }
      else if (strcmp(argv[1], "acq") == 0)
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
                  sprintf(comm, "TimeScale_Open %s 0x%04X %s %d\n",
                                tscn[target], EpgDbContextGetCni(dbc),
                                ((target == DB_TARGET_UI) ? "ui" : "acq"),
                                EpgDbContextIsMerged(dbc));
                  eval_check(interp, comm);
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
                     for (lastHistPos=0; lastHistPos < sv->histIdx; lastHistPos++)
                     {
                        sprintf(comm, "DbStatsWin_AddHistory %s %d %d %d %d %d %d\n",
                                      dbswn[target], lastHistPos+1,
                                      sv->hist[lastHistPos].expir,
                                      sv->hist[lastHistPos].s1cur, sv->hist[lastHistPos].s1old,
                                      sv->hist[lastHistPos].s2cur, sv->hist[lastHistPos].s2old);
                        eval_check(interp, comm);
                     }
                     dbStatsWinState[target].lastHistPos = sv->histIdx - 1;
                  }
               }
               EpgDbAcqResetVpsPdc();
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
            if (EpgScan_IsActive() == FALSE)
            {
               if ( ((target == DB_TARGET_UI) && (dbStatsWinState[DB_TARGET_ACQ].open == FALSE)) ||
                    ((target == DB_TARGET_ACQ) && ((dbStatsWinState[DB_TARGET_UI].open == FALSE) ||
                                                   (dbStatsWinState[DB_TARGET_UI].isForAcq == FALSE))) )
               {
                  EpgDbAcqResetVpsPdc();
               }
            }
         }
         // set the state of the checkbutton of the according menu entry
         sprintf(comm, "set menuStatusStatsOpen(%s) %d\n",
                       ((target == DB_TARGET_UI) ? "ui" : "acq"),
                       dbStatsWinState[target].open);
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
   dprintf2("StatsWin-ProvChange: called for %d: %s\n", target, ((target == DB_TARGET_UI) ? "ui" : ((target == DB_TARGET_ACQ) ? "acq" : "unknown")));

   if (target < 2)
   {
      // update & reload the respective network timescale popup
      // when the context is closed the popup window is destroyed
      if (tscaleState[target].open)
      {
         AddMainIdleEvent(StatsWin_RebuildCanvas, (ClientData) target, FALSE);
      }

      // if a network in the inactive UI window is still marked, unmark it
      if (tscaleState[DB_TARGET_UI].open)
      {
         StatsWin_HighlightNetwop(DB_TARGET_UI, 0xff, 0xff);
      }

      // if the respective db stats window is open, update its content
      if (dbStatsWinState[target].open)
      {
         AddMainIdleEvent(StatsWin_UpdateDbStatsWin, (ClientData)target, TRUE);
      }

      // update the status line when acq is stopped through the driver (e.g. signal HUP)
      if ((target == DB_TARGET_ACQ) && (pAcqDbContext == NULL))
      {
         AddMainIdleEvent(StatsWin_UpdateDbStatusLine, NULL, TRUE);
      }
   }
   else
      debug1("StatsWin_ProvChange: illegal target: %d", target);
}

// ----------------------------------------------------------------------------
// create and initialize (but do NOT show) the statistics window
// - this should be called only once during start-up
//
void StatsWin_Create( bool isDemoMode )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_StatsWin_ToggleTimescale", &cmdInfo) == 0)
   {
      Tcl_CreateCommand(interp, "C_StatsWin_ToggleTimescale", StatsWin_ToggleTimescale, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_StatsWin_ToggleDbStats", StatsWin_ToggleDbStats, (ClientData) NULL, NULL);
      isStatsDemoMode = isDemoMode;
   }
   else
      debug0("StatsWin-Create: commands are already created");

   memset(tscaleState, 0, sizeof(tscaleState));
   memset(dbStatsWinState, 0, sizeof(dbStatsWinState));
}

