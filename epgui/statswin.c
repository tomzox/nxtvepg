/*
 *  Nextview EPG GUI: Database statistics and main window status line
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
 *    Implements pop-up windows with statistics about the current state
 *    of the databases and the acquisition process.  They can be opened
 *    separately for the browser and acquisition database and are updated
 *    dynamically while acquisition is running.
 *
 *    The statistics window is generated by Tcl/Tk procedures. It consists
 *    of two vertically separated parts: the upper part contains db stats,
 *    such as number of PI in db, percentage of expired PI etc. The lower
 *    part describes the acq state, e.g. number of received AI blocks etc.
 *    Both parts consist of a canvas widget (graphics output) at the left
 *    and a message widget (multi-line, fixed-width string output) at the
 *    right.
 *
 *    This module also generates the status line at the bottom of the
 *    main window, which is a single line of text which is assembled from
 *    carefully selected db stats and acq state information.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: statswin.c,v 1.80 2020/06/17 19:32:20 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/ttxdecode.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgscan.h"
#include "epgctl/epgctxmerge.h"
#include "epgctl/epgacqclnt.h"
#include "epgui/wintvcfg.h"
#include "epgui/menucmd.h"
#include "epgui/epgsetup.h"
#include "epgui/epgmain.h"
#include "epgui/cmdline.h"
#include "epgui/uictrl.h"
#include "epgui/statswin.h"


// state of DbStatsWin window
static struct
{
   bool   open;
   bool   isForAcq;
   int    lastHistPos;
   time_t lastHistReset;
   Tcl_TimerToken updateHandler;
} dbStatsWinState[2];

const char * const dbswn[2] =
{
   ".dbstats_ui",
   ".dbstats_acq"
};
#define STATS_WIN_TTX_WNAM  ".dbstats_ttx"

static struct
{
   bool   open;
   Tcl_TimerToken updateHandler;
} statsWinTtx;


static void StatsWin_UpdateDbStatsWinTimeout( ClientData clientData );

// ----------------------------------------------------------------------------
// Update the history diagram in the db statistics window
//
static void StatsWin_UpdateHist( int target )
{
   EPG_ACQ_STATS sv;
   uint idx;

   if ( EpgAcqCtl_GetAcqStats(&sv) )
   {
      if (dbStatsWinState[target].lastHistReset != sv.nxtv.acqStartTime)
      {  // acquisition was reset -> clear history
         dbStatsWinState[target].lastHistReset = sv.nxtv.acqStartTime;
         dbStatsWinState[target].lastHistPos   = STATS_HIST_WIDTH;

         if (dbStatsWinState[target].lastHistReset != 0)
         {
            sprintf(comm, "DbStatsWin_ClearHistory %s\n", dbswn[target]);
            eval_check(interp, comm);
         }
      }

      if ( (dbStatsWinState[target].lastHistPos != sv.nxtv.histIdx) &&
           (sv.nxtv.histIdx > 1) &&
           (sv.nxtv.histIdx != STATS_HIST_WIDTH - 1) )
      {
         idx = (dbStatsWinState[target].lastHistPos + 1) % STATS_HIST_WIDTH;

         for (idx=0; idx < sv.nxtv.histIdx; idx++)
         {
            sprintf(comm, "DbStatsWin_AddHistory %s %d %d %d %d %d %d\n",
                          dbswn[target], idx+1,
                          sv.nxtv.hist[idx].expir,
                          sv.nxtv.hist[idx].s1cur, sv.nxtv.hist[idx].s1old,
                          sv.nxtv.hist[idx].s2cur, sv.nxtv.hist[idx].s2old);
            eval_check(interp, comm);
         }
         dbStatsWinState[target].lastHistPos = sv.nxtv.histIdx;
      }
   }
   else
   {  // acq not running -> clear history
      sprintf(comm, "DbStatsWin_ClearHistory %s\n", dbswn[target]);
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// Fill window with database statistics and pie chart
//
static void StatsWin_PrintDbStats( EPGDB_CONTEXT * dbc, EPGDB_BLOCK_COUNT * count, const char * pWid )
{
   const AI_BLOCK *pAi;
   time_t lastAiUpdate;
   char  netname[60+1], datestr[25+1];
   uchar version;
   uint  total, allVersionsCount, curVersionCount, obsolete;
   Tcl_DString cmd_dstr;

   if ( (EpgDbContextIsMerged(dbc) == FALSE) &&
        (EpgDbContextIsXmltv(dbc) == FALSE) )
   {
      // default values if database is empty
      strcpy(netname, "none yet");
      strcpy(datestr, "none yet");
      version = 0;

      if (dbc != NULL)
      {  // get provider name and db version from AI block
         EpgDbLockDatabase(dbc, TRUE);
         pAi = EpgDbGetAi(dbc);
         if (pAi != NULL)
         {
            strncpy(netname, AI_GET_SERVICENAME(pAi), sizeof(netname) - 1);
            netname[sizeof(netname) - 1] = 0;
            lastAiUpdate = EpgDbGetAiUpdateTime(dbc);
            strftime(datestr, 25, "%H:%M:%S %a %d.%m.", localtime(&lastAiUpdate));
            version = pAi->version;
         }
         EpgDbLockDatabase(dbc, FALSE);
      }

      total            = count->ai;
      obsolete         = count->expired + count->defective;
      allVersionsCount = count->allVersions + obsolete;
      curVersionCount  = count->curVersion + obsolete;

      sprintf(comm, "EPG provider:     %s (CNI %04X)\n"
                    "last AI update:   %s\n"
                    "Database version: %d\n"
                    "Blocks in AI:     %d\n"
                    "Block count db:   %d\n"
                    "current version:  %d\n"
                    "filled:           %d%% / %d%% current version\n"
                    "expired stream:   %d%%: %d\n"
                    "expired total:    %d%%: %d\n"
                    "defective blocks: %d%%: %d",
                    netname, EpgDbContextGetCni(dbc),
                    datestr,
                    version,
                    // TODO blocks in AI no longer exists
                    total,
                    allVersionsCount,
                    curVersionCount,
                    ((total > 0) ? (int)((double)allVersionsCount * 100.0 / total) : 100),
                    ((total > 0) ? (int)((double)curVersionCount * 100.0 / total) : 100),
                    ((allVersionsCount > 0) ? ((int)((double)(count->expired + count[1].expired) * 100.0 / allVersionsCount)) : 0),
                       count->expired,
                    ((allVersionsCount + count->extra > 0) ? ((int)((double)count->extra * 100.0 / (allVersionsCount + count->extra))) : 0),
                       count->extra,
                    ((allVersionsCount > 0) ? ((int)(count->defective * 100.0 / allVersionsCount)) : 0),
                       count->defective
             );
      {
         Tcl_DStringInit(&cmd_dstr);
         Tcl_DStringAppend(&cmd_dstr, pWid, -1);
         Tcl_DStringAppend(&cmd_dstr, ".browser.stat configure -text", -1);
         // append message as list element, so that '{' etc. is escaped properly
         Tcl_DStringAppendElement(&cmd_dstr, comm);

         eval_check(interp, Tcl_DStringValue(&cmd_dstr));

         Tcl_DStringFree(&cmd_dstr);
      }

      if (total > 0)
      {
         sprintf(comm, "DbStatsWin_PaintPie %s %d %d %d %d\n",
                       pWid,
                       (int)((double)(count->defective + count->expired) / total * 359.9),
                       (int)((double)(count->curVersion + count->defective + count->expired) / total * 359.9),
                       (int)((double)(count->allVersions + count->defective + count->expired) / total * 359.9),
                       (int)((double)count->ai / total * 359.9));
         // TODO ai / total no longer make sense
      }
      else
      {
         sprintf(comm, "DbStatsWin_ClearPie %s\n", pWid);
      }
      eval_check(interp, comm);
   }
   else
   {  // Merged database

      allVersionsCount = count->allVersions + count->expired + count->defective + count->extra;

      if ( EpgDbContextIsMerged(dbc) )
      {
         sprintf(comm, "%s.browser.stat configure -text \""
                       "EPG Provider:     Merged database\n"
                       "Block count db:   %d\n"
                       "\nFor more info please refer to the\n"
                       "original databases (more statistics\n"
                       "will be added here in the future)\n"
                       "\"\n",
                       pWid, allVersionsCount);
      }
      else
      {
         sprintf(comm, "%s.browser.stat configure -text \""
                       "EPG Provider:     Imported database\n"
                       "Block count db:   %d\n\"\n", pWid,
                       allVersionsCount);
      }
      eval_check(interp, comm);

      sprintf(comm, "DbStatsWin_ClearPie %s\n", pWid);
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// Fill window with Nextview acquisition status information
//
static void StatsWin_PrintNxtvAcqStats( EPGDB_CONTEXT * dbc, EPGACQ_DESCR * pAcqState,
                                        EPG_ACQ_STATS * sv, EPG_ACQ_VPS_PDC * pVpsPdc, const char * pWid )
{
   double ttxRate;
   time32_t acq_duration;
   const char * pAcqModeStr;
   const char * pAcqPasvStr;
   Tcl_DString cmd_dstr;

   comm[0] = 0;

   #ifdef USE_DAEMON
   if (pAcqState->isNetAcq)
   {
      EPGDBSRV_DESCR netState;
      EpgAcqClient_DescribeNetState(&netState);
      switch(netState.state)
      {
         case NETDESCR_ERROR:
            sprintf(comm + strlen(comm), "Network state:    error\nError cause:      %s\n", ((netState.cause != NULL) ? netState.cause : "unknown"));
            break;
         case NETDESCR_CONNECT:
            sprintf(comm + strlen(comm), "Network state:    server contacted\n");
            break;
         case NETDESCR_STARTUP:
            sprintf(comm + strlen(comm), "Network state:    connect in progress\n");
            break;
         case NETDESCR_LOADING:
            sprintf(comm + strlen(comm), "Network state:    loading database\n");
            break;
         case NETDESCR_RUNNING:
            sprintf(comm + strlen(comm), "Network state:    connected (rx %ld bytes)\n", netState.rxTotal);
            break;
         case NETDESCR_DISABLED:
         default:
            sprintf(comm + strlen(comm), "Network state:    unconnected\n");
            break;
      }
   }
   #endif

   if ((sv->nxtv.acqStartTime > 0) && (sv->nxtv.acqStartTime <= sv->lastStatsUpdate))
      acq_duration = sv->lastStatsUpdate - sv->nxtv.acqStartTime;
   else
      acq_duration = 0;

   sprintf(comm + strlen(comm), "Acq Runtime:      %02d:%02d\n",
                                (uint)(acq_duration / 60), (uint)(acq_duration % 60));

   if (pVpsPdc->cni != 0)
   {
      if ( VPS_PIL_IS_VALID(pVpsPdc->pil) )
      {
         sprintf(comm + strlen(comm), "Channel VPS/PDC:  CNI %04X, PIL %02d.%02d. %02d:%02d\n",
                                      pVpsPdc->cni,
                                      (pVpsPdc->pil >> 15) & 0x1F, (pVpsPdc->pil >> 11) & 0x0F,
                                      (pVpsPdc->pil >>  6) & 0x1F, (pVpsPdc->pil) & 0x3F);
      }
      else
      {
         sprintf(comm + strlen(comm), "Channel VPS/PDC:  CNI %04X\n", pVpsPdc->cni);
      }
   }
   else
      strcat(comm, "Channel VPS/PDC:  ---\n");

   ttxRate  =(double)sv->ttx_dec.ttxPkgRate / (1 << TTX_PKG_RATE_FIXP);

   sprintf(comm + strlen(comm),
                 "TTX pkg/frame:    %.1f (%.0f baud)\n"
                 "PI rx repetition: %d/%.2f now/next\n",
                 ttxRate, ttxRate * 42 * 8 * 25,
                 sv->nxtv.nowMaxAcqRepCount, sv->nxtv.count.avgAcqRepCount
          );

   EpgAcqCtl_GetAcqModeStr(pAcqState, /* forTtx := FALSE,*/ &pAcqModeStr, &pAcqPasvStr);
#ifdef WIN32
   if (EpgSetup_CheckTvCardConfig() == FALSE)
   {
      pAcqPasvStr = "TV card not configured";
   }
#endif
   sprintf(comm + strlen(comm), "Acq mode:         %s\n", pAcqModeStr);
   if (pAcqPasvStr != NULL)
      sprintf(comm + strlen(comm), "Passive reason:   %s\n", pAcqPasvStr);

   sprintf(comm + strlen(comm), "Network variance: %1.2f\n",
                                sv->nxtv.count.variance);

   {
      Tcl_DStringInit(&cmd_dstr);
      Tcl_DStringAppend(&cmd_dstr, pWid, -1);
      Tcl_DStringAppend(&cmd_dstr, ".acq.stat configure -text", -1);
      // append message as list element, so that '{' etc. is escaped properly
      Tcl_DStringAppendElement(&cmd_dstr, comm);

      eval_check(interp, Tcl_DStringValue(&cmd_dstr));

      Tcl_DStringFree(&cmd_dstr);
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
static void StatsWin_UpdateDbStatsWin( ClientData clientData )
{
   EPGDB_BLOCK_COUNT count;
   EPGDB_CONTEXT * dbc;
   EPGDB_CONTEXT * pAcqDbContext;
   EPGACQ_DESCR acqState;
   EPG_ACQ_STATS sv;
   EPG_ACQ_VPS_PDC vpsPdc;
   uint target;

   pAcqDbContext = NULL; //TODO EpgAcqCtl_GetDbContext(TRUE);

   target = PVOID2UINT(clientData);
   if (target == DB_TARGET_ACQ)
      dbc = pAcqDbContext;
   else if (target == DB_TARGET_UI)
      dbc = pUiDbContext;
   else
      dbc = NULL;

   if ((target < 2) && dbStatsWinState[target].open)
   {
      dprintf1("StatsWin-UpdateDbStatsWin: for %s\n", ((target == DB_TARGET_UI) ? "ui" : "acq"));

#if 0  // TTX?
      if ( (target == DB_TARGET_ACQ) || (pAcqDbContext == pUiDbContext) )
      {
         EpgAcqCtl_GetDbStats(&count, NULL);
      }
      else
#endif
      {  // acq not running for this database -> display db stats only
         memset(&count, 0, sizeof(count));
         if (dbc != NULL)
         {
            EpgDbGetStat(dbc, &count, 0, 0);
         }
      }

      // print database statistics and pie chart
      StatsWin_PrintDbStats(dbc, &count, dbswn[target]);

      if ((target == DB_TARGET_ACQ) || (pAcqDbContext == pUiDbContext))
      {
         if (EpgAcqCtl_GetAcqStats(&sv) == FALSE)
         {
            memset(&sv, 0, sizeof(sv));
         }
         if (dbStatsWinState[target].isForAcq == FALSE)
         {
            sprintf(comm, "pack %s.acq -side top -anchor nw -fill both -after %s.browser\n",
                          dbswn[target], dbswn[target]);
            eval_check(interp, comm);
            dbStatsWinState[target].isForAcq = TRUE;
         }

         EpgAcqCtl_DescribeAcqState(&acqState);

         if (EpgAcqCtl_GetVpsPdc(&vpsPdc, VPSPDC_REQ_STATSWIN, FALSE) == FALSE)
         {
            vpsPdc.cni = 0;
         }

         // display graphical database fill percentage histogramm
         StatsWin_UpdateHist(target);

         // text description
         StatsWin_PrintNxtvAcqStats(dbc, &acqState, &sv, &vpsPdc, dbswn[target]);

         // remove the old update timer
         if (dbStatsWinState[target].updateHandler != NULL)
         {
            Tcl_DeleteTimerHandler(dbStatsWinState[target].updateHandler);
         }
         // set up a timer to re-display the stats in case there's no EPG reception
         if ((acqState.ttxGrabState != ACQDESCR_DISABLED) && (acqState.isNetAcq == FALSE))
         {
            dbStatsWinState[target].updateHandler =
               Tcl_CreateTimerHandler(10*1000, StatsWin_UpdateDbStatsWinTimeout, UINT2PVOID(target));
         }
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
// Update the teletext grabber statistics window
//
static void StatsWin_UpdateTtxStats( ClientData clientData )
{
   EPGACQ_DESCR acqState;
   EPG_ACQ_STATS sv;
   EPG_ACQ_VPS_PDC vpsPdc;
   time32_t acq_duration;
   const char * pAcqModeStr;
   const char * pAcqPasvStr;
   Tcl_DString cmd_dstr;

   if (statsWinTtx.open)
   {
      if (EpgAcqCtl_GetAcqStats(&sv) == FALSE)
      {
         memset(&sv, 0, sizeof(sv));
      }

      EpgAcqCtl_DescribeAcqState(&acqState);

      if ((sv.ttx_grab.acqStartTime > 0) && (sv.ttx_grab.acqStartTime <= sv.lastStatsUpdate))
      {
         acq_duration = sv.lastStatsUpdate - sv.ttx_grab.acqStartTime;
      }
      else
         acq_duration = 0;

      comm[0] = 0;

      sprintf(comm + strlen(comm), "Acq Runtime:      %02d:%02d\n"
                                   "Channel Index:    %d of %d\n",
                                   (uint)(acq_duration / 60), (uint)(acq_duration % 60),
                                   acqState.ttxGrabDone, acqState.ttxSrcCount);

      sprintf(comm + strlen(comm), "Channel Name:     %s\n", sv.ttx_grab.srcName);

      if (EpgAcqCtl_GetVpsPdc(&vpsPdc, VPSPDC_REQ_STATSWIN, FALSE) && (vpsPdc.cni != 0))
      {
         if ( VPS_PIL_IS_VALID(vpsPdc.pil) )
         {
            sprintf(comm + strlen(comm), "Channel VPS/PDC:  CNI %04X, PIL %02d.%02d. %02d:%02d\n",
                                         vpsPdc.cni,
                                         (vpsPdc.pil >> 15) & 0x1F, (vpsPdc.pil >> 11) & 0x0F,
                                         (vpsPdc.pil >>  6) & 0x1F, (vpsPdc.pil) & 0x3F);
         }
         else
         {
            sprintf(comm + strlen(comm), "Channel VPS/PDC:  CNI %04X\n", vpsPdc.cni);
         }
      }
      else
         strcat(comm, "Channel VPS/PDC:  ---\n");

      sprintf(comm + strlen(comm),
                    "TTX data rate:    %d baud\n"
                    "Captured range:   %03X-%03X\n"
                    "Captured pages:   %d (%d pkg, %1.1f%% of TTX)\n"
                    "Decoder quality:  blanked %d of %d chars (%d%%)\n",
                    ((sv.ttx_duration > 0) ? (int)((sv.ttx_dec.ttxPkgCount*45*8)/sv.ttx_duration) : 0),
                    sv.ttx_grab.pkgStats.ttxPageStartNo,
                       sv.ttx_grab.pkgStats.ttxPageStopNo,
                    sv.ttx_grab.pkgStats.ttxPagCount,
                       sv.ttx_grab.pkgStats.ttxPkgCount,
                       ((sv.ttx_dec.ttxPkgCount > 0) ? ((double)sv.ttx_grab.pkgStats.ttxPkgCount*100.0/sv.ttx_dec.ttxPkgCount) : 0.0),
                    sv.ttx_grab.pkgStats.ttxPkgParErr, sv.ttx_grab.pkgStats.ttxPkgStrSum,
                    ((sv.ttx_grab.pkgStats.ttxPkgStrSum > 0) ? (sv.ttx_grab.pkgStats.ttxPkgParErr * 100 / sv.ttx_grab.pkgStats.ttxPkgStrSum) : 0)
             );

      EpgAcqCtl_GetAcqModeStr(&acqState, &pAcqModeStr, &pAcqPasvStr);
#ifdef WIN32
      if (EpgSetup_CheckTvCardConfig() == FALSE)
      {
         pAcqPasvStr = "TV card not configured";
      }
#endif
      sprintf(comm + strlen(comm), "Acq mode:         %s\n", pAcqModeStr);
      if (pAcqPasvStr != NULL)
         sprintf(comm + strlen(comm), "Passive reason:   %s\n", pAcqPasvStr);

      {
         Tcl_DStringInit(&cmd_dstr);
         Tcl_DStringAppend(&cmd_dstr, STATS_WIN_TTX_WNAM, -1);
         Tcl_DStringAppend(&cmd_dstr, ".acq.stat configure -text", -1);
         // append message as list element, so that '{' etc. is escaped properly
         Tcl_DStringAppendElement(&cmd_dstr, comm);

         eval_check(interp, Tcl_DStringValue(&cmd_dstr));

         Tcl_DStringFree(&cmd_dstr);
      }

      // remove the old update timer
      if (statsWinTtx.updateHandler != NULL)
         Tcl_DeleteTimerHandler(statsWinTtx.updateHandler);
      // set up a timer to re-display the stats in case there's no EPG reception
      if ((acqState.ttxGrabState != ACQDESCR_DISABLED) && (acqState.isNetAcq == FALSE))
         statsWinTtx.updateHandler =
            Tcl_CreateTimerHandler(2*1000, StatsWin_UpdateTtxStats, NULL);
   }
}

// ----------------------------------------------------------------------------
// Schedule update of stats window after timeout
// - used only for stats windows with acquisition
// - timeout occurs when no AI is received
//
static void StatsWin_UpdateDbStatsWinTimeout( ClientData clientData )
{
   uint target = PVOID2UINT(clientData);

   if (target < 2)
   {
      dbStatsWinState[target].updateHandler = NULL;
      AddMainIdleEvent(StatsWin_UpdateDbStatsWin, clientData, FALSE);
   }
   else
      fatal1("StatsWin-UpdateDbStatsWinTimeout: illegal target %d", target);
}

// ----------------------------------------------------------------------------
// Update status line of browser main window
// - possible bits of information about browser db:
//   + allversion percentage of PI
//   + time & date of last AI reception
//   + percentage of expired PI
// - possible bits of information about acq:
//   + state: Off, Startup, Running, Stalled
//   + CNI in acq db
//   + mode: forced-passive & reason, passive, cycle state
//
static void StatsWin_UpdateDbStatusLine( ClientData clientData )
{
   EPGDB_BLOCK_COUNT count;
   EPGACQ_DESCR acqState;
   const AI_BLOCK *pAi;
   char * pProvName, provName[40];
   ulong aiTotal, nearCount, allCount, expiredCount, expiredBase;
   time_t dbAge;
   bool isPassiveAcq;
   #ifdef USE_DAEMON
   EPGDBSRV_DESCR netState;
   #endif

   dprintf0("StatsWin-UpdateDbStatusLine: called\n");

   comm[0] = 0;

   if ((EpgDbContextIsMerged(pUiDbContext) == FALSE) &&
       (EpgDbContextIsXmltv(pUiDbContext) == FALSE) &&
       (EpgDbContextGetCni(pUiDbContext) != 0))
   {
      // compute statistics
      EpgDbGetStat(pUiDbContext, &count, 0, 0);

      // get name of UI provider network
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAi = EpgDbGetAi(pUiDbContext);
      if (pAi != NULL)
         sprintf(comm + strlen(comm), "%s database", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
      else
         strcat(comm, "Browser database");
      EpgDbLockDatabase(pUiDbContext, FALSE);

      aiTotal      = count.ai;
      allCount     = count.allVersions + count.expired + count.defective;
      expiredCount = count.expired;
      expiredBase  = count.allVersions + expiredCount;

      // print fill percentage across all db versions
      if ( (allCount == 0) || (aiTotal == 0) ||
           (expiredCount + count.defective >= allCount) )
      {
         strcat(comm, " is empty");
      }
      else if (allCount < aiTotal)
      {
         sprintf(comm + strlen(comm), " %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
      }
      else
         strcat(comm, " 100% complete");

      // warn about age of database if more than one hour old
      dbAge = (time(NULL) - EpgDbGetAiUpdateTime(pUiDbContext)) / 60;
      if (dbAge >= 24*60)
      {
         sprintf(comm + strlen(comm), ", %1.1f days old", (double)dbAge / (24*60));
      }
      else if (dbAge >= 60)
      {
         sprintf(comm + strlen(comm), ", %ld:%02ld hours old", dbAge / 60, dbAge % 60);
      }

      // print how much of PI are expired
      if ( (expiredBase > 0) && (expiredCount > 0) &&
           ((dbAge >= 60) || (expiredCount * 10 >= expiredBase)) )
      {
         sprintf(comm + strlen(comm), ", %d%% expired", ACQ_COUNT_TO_PERCENT(expiredCount, expiredBase));
      }
      else if ((allCount > 0) && (allCount < aiTotal) && (count.ai > 0) && (aiTotal > 0))
      {  // append near percentage
         if ( ((count.allVersions + count.defective) / count.ai) > (allCount / aiTotal) )
         {
            sprintf(comm + strlen(comm), ", near data %d%%", ACQ_COUNT_TO_PERCENT(count.allVersions + count.defective, count.ai));
         }
      }
      strcat(comm, ". ");
   }

   // fetch current state and acq db statistics from acq control
   EpgAcqCtl_DescribeAcqState(&acqState);
   aiTotal = nearCount = allCount = 0;
   pProvName = NULL;

#if 0  // TTX
   {
      EPGDB_CONTEXT * pAcqDbContext = EpgAcqCtl_GetDbContext(TRUE);
      if (pAcqDbContext != NULL)
      {
         assert (EpgDbContextIsXmltv(pAcqDbContext) == FALSE); // cannot happen for non-TTX db
         // get name of acq provider network
         EpgDbLockDatabase(pAcqDbContext, TRUE);
         pAi = EpgDbGetAi(pAcqDbContext);
         if (pAi != NULL)
         {
            strncpy(provName, AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop), sizeof(provName) - 1);
            provName[sizeof(provName) - 1] = 0;
            pProvName = provName;
         }
         EpgDbLockDatabase(pAcqDbContext, FALSE);

         if (acqState.ttxGrabState == ACQDESCR_RUNNING)
         {
            if (EpgAcqCtl_GetDbStats(&count, &nowMaxAcqNetCount))
            {
               // evaluate some totals for later use in percentage evaluations
               aiTotal   = count.ai;
               nearCount = count.curVersion + count.expired + count.defective;
               allCount  = nearCount; //TODO clean-up
            }
         }
      }
      EpgAcqCtl_GetDbContext(FALSE);
   }
#endif
   {
      char * pTtxNames = NULL;
      char * pName;
      uint chanCount;
      uint idx;

      if ( WintvCfg_GetFreqTab(&pTtxNames, NULL, &chanCount, NULL) &&
           (acqState.ttxGrabIdx >= 0) && (chanCount > acqState.ttxGrabIdx))
      {
         pName = pTtxNames;
         for (idx = 0; idx < acqState.ttxGrabIdx; idx++)
            while(*(pName++) != 0)
               ;
         strncpy(provName, pName, sizeof(provName) - 1);
         if (strlen(pName) + 9+1 <= sizeof(provName))
            strcat(provName, " teletext");
         else
            strcpy(provName + sizeof(provName) - (12+1), "... teletext");
         pProvName = provName;
      }
      if (pTtxNames != NULL)
         xfree(pTtxNames);
   }


   // describe acq state (disabled, network connect, wait AI, running)

   #ifdef USE_DAEMON
   if (acqState.isNetAcq)
   {
      if ( EpgAcqClient_DescribeNetState(&netState) )
      {
         // network connection to acquisition server is not set up -> describe network state instead
         switch (netState.state)
         {
            case NETDESCR_CONNECT:
            case NETDESCR_STARTUP:
               strcat(comm, "Acquisition: connecting to server..");
               break;
            case NETDESCR_ERROR:
            case NETDESCR_DISABLED:
               strcat(comm, "Acquisition stalled: network error");
               break;
            case NETDESCR_LOADING:
               strcat(comm, "Acquisition: connecting to server..");
               break;
            case NETDESCR_RUNNING:
               if (acqState.ttxGrabState == ACQDESCR_NET_CONNECT)
               {  // note: in state RUNNING display "loading" until the first stats report is available
                  strcat(comm, "Acquisition: connecting to server..");
               }
               break;
            default:
               fatal1("StatsWin-UpdateDbStatusLine: unknown net state %d", netState.state);
               break;
         }

         if (netState.state != NETDESCR_RUNNING)
         {  // set acq state to suppress additional output (e.g. acq phase)
            acqState.ttxGrabState = ACQDESCR_NET_CONNECT;
         }
      }
   }
   #endif

   isPassiveAcq = (acqState.mode == ACQMODE_PASSIVE) ||
                  (acqState.passiveReason != ACQPASSIVE_NONE);

   // TODO: in passive mode use either source (careful: use matching provname!)
   switch (acqState.ttxGrabState)
   {
      case ACQDESCR_DISABLED:
         strcat(comm, "Acquisition is disabled");
         break;
      case ACQDESCR_NET_CONNECT:
      case ACQDESCR_SCAN:
         break;
      case ACQDESCR_STARTING:
         if ((isPassiveAcq == FALSE) && (pProvName != NULL))
         {
            sprintf(comm + strlen(comm), "Acquisition is starting up for %s", pProvName);
         }
         else
            strcat(comm, "Acquisition is waiting for reception");
         break;
      case ACQDESCR_TTX_PG_SEQ_SCAN:
         strcat(comm, "Acquisition scanning teletext");
         break;
      case ACQDESCR_NO_RECEPTION:
         #ifdef WIN32
         if (EpgSetup_CheckTvCardConfig() == FALSE)
         {
            strcat(comm, "Acquisition: TV card not configured");
         }
         else
         #endif
         if ((isPassiveAcq == FALSE) && (pProvName != NULL))
         {
            sprintf(comm + strlen(comm), "Acquisition: no reception on %s", pProvName);
         }
         else
            strcat(comm, "Acquisition: no teletext reception");
         break;
      case ACQDESCR_IDLE:
         strcat(comm, "Acquisition: idle");
         break;
      case ACQDESCR_RUNNING:
         if (pProvName != NULL)
         {
            sprintf(comm + strlen(comm), "Acquisition working on %s", pProvName);
         }
         else
         {  // internal inconsistancy
            //TODO fatal0("StatsWin-UpdateDbStatusLine: no AI block while in state RUNNING");
            sprintf(comm + strlen(comm), "Acquisition in progress..");
            // patch state to prevent further output
            acqState.ttxGrabState = ACQDESCR_NET_CONNECT;
         }
         break;

      default:
         fatal1("StatsWin-UpdateDbStatusLine: unknown acq state %d", acqState.ttxGrabState);
   }

   // describe acq mode, warn the user if passive
   if ( (acqState.ttxGrabState != ACQDESCR_NET_CONNECT) &&
        (acqState.ttxGrabState != ACQDESCR_DISABLED) &&
        (acqState.ttxGrabState != ACQDESCR_SCAN))
   {
      if (acqState.mode == ACQMODE_PASSIVE)
      {
         if ((acqState.ttxGrabState == ACQDESCR_RUNNING) && (aiTotal != 0))
         {
            sprintf(comm + strlen(comm), ", %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
         }
         strcat(comm, " (passive mode)");
      }
      else if (acqState.passiveReason != ACQPASSIVE_NONE)
      {
         if ((acqState.ttxGrabState == ACQDESCR_RUNNING) && (aiTotal != 0))
         {
            sprintf(comm + strlen(comm), ", %d%% complete", ACQ_COUNT_TO_PERCENT(allCount, aiTotal));
         }
         strcat(comm, " (forced passive)");
      }
      else if (acqState.mode == ACQMODE_CYCLIC_2)
      {
         sprintf(comm + strlen(comm), ", overall %d%% complete",
                 ACQ_COUNT_TO_PERCENT(acqState.ttxGrabDone, acqState.ttxSrcCount));
      }
      else
      {
         switch (acqState.cyclePhase)
         {
            case ACQMODE_PHASE_NOWNEXT:
               sprintf(comm + strlen(comm), " phase 'Now', %d%% complete",
                       ACQ_COUNT_TO_PERCENT(acqState.ttxGrabDone, acqState.ttxSrcCount));
               break;
            case ACQMODE_PHASE_FULL:
               sprintf(comm + strlen(comm), " phase 'Full', %d%% complete",
                       ACQ_COUNT_TO_PERCENT(acqState.ttxGrabDone, acqState.ttxSrcCount));
               break;
            case ACQMODE_PHASE_MONITOR:
               sprintf(comm + strlen(comm), " phase 'Complete', %d%% complete",
                       ACQ_COUNT_TO_PERCENT(acqState.ttxGrabDone, acqState.ttxSrcCount));
               break;
            default:
               break;
         }
      }
   }

   switch (acqState.ttxGrabState)
   {
      case ACQDESCR_SCAN:
         break;
      case ACQDESCR_STARTING:
      case ACQDESCR_TTX_PG_SEQ_SCAN:
         strcat(comm, "...");
         break;
      default:
         strcat(comm, ".");
         break;
   }

   {
      if (Tcl_SetVar(interp, "dbstatus_line", comm, TCL_GLOBAL_ONLY) == NULL)
      {
         debugTclErr(interp, "assigning to var dbstatus_line");
      }
   }
}

// ----------------------------------------------------------------------------
// Open or close window with db and acq statistics
// - called by the "View (acq) statistics" menu entries (toggle mode, argc=2)
//   or as a callback when a statistics window is destroyed
//
static int StatsWin_ToggleDbStats( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   const char * const pUsage = "Usage: C_StatsWin_ToggleDbStats ui|acq [0|1]";
   int target, newState;
   int result = TCL_OK;

   target = -1;

   if ((argc == 2) || (argc == 3))
   {
      // determine target from first parameter: ui or acq db context
      if ((strcmp(argv[1], "ui") == 0) || (strcmp(argv[1], dbswn[DB_TARGET_UI]) == 0))
      {
         target = DB_TARGET_UI;
      }
      else if ((strcmp(argv[1], "acq") == 0) || (strcmp(argv[1], dbswn[DB_TARGET_ACQ]) == 0))
      {
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
            dbStatsWinState[target].lastHistReset = 0;
            dbStatsWinState[target].updateHandler = NULL;

            if (target == DB_TARGET_ACQ)
            {
               StatsWin_UpdateHist(target);
            }
            // enable extended statistics reports
            EpgAcqCtl_EnableAcqStats(TRUE);

            // display initial summary
            StatsWin_UpdateDbStatsWin(UINT2PVOID(target));
         }
         else
         {  // destroy the window

            // note: set to FALSE before window destruction to avoid recursion
            dbStatsWinState[target].open = FALSE;

            sprintf(comm, "destroy %s", dbswn[target]);
            eval_check(interp, comm);

            // disable extended statistics reports when all windows are closed
            if ( (dbStatsWinState[DB_TARGET_ACQ].open == FALSE) &&
                 (dbStatsWinState[DB_TARGET_UI].open == FALSE) &&
                 (statsWinTtx.open == FALSE) )
            {
               EpgAcqCtl_EnableAcqStats(FALSE);
            }
         }
         // set the state of the checkbutton of the according menu entry
         Tcl_SetVar2Ex(interp, "menuStatusStatsOpen",
                               ((target == DB_TARGET_UI) ? "ui" : "acq"),
                               Tcl_NewIntObj(dbStatsWinState[target].open), 0);
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
// Open or close window with teletext grabber statistics
//
static int StatsWin_ToggleTtxStats( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   const char * const pUsage = "Usage: C_StatsWin_ToggleTtxStats [0|1]";
   int newState;
   int result;

   if ( (argc < 1) || (argc > 2) ||
        ((argc == 2) && (Tcl_GetBoolean(interp, argv[1], &newState) != TCL_OK)) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (argc == 1)
      {
         newState = ! statsWinTtx.open;
      }
      if (newState != statsWinTtx.open)
      {
         if (statsWinTtx.open == FALSE)
         {  // window shall be opened
            sprintf(comm, "StatsWinTtx_Create %s\n", STATS_WIN_TTX_WNAM);
            eval_check(interp, comm);

            statsWinTtx.open = TRUE;
            statsWinTtx.updateHandler = NULL;

            // enable extended statistics reports
            EpgAcqCtl_EnableAcqStats(TRUE);

            // display initial summary
            StatsWin_UpdateTtxStats(NULL);
         }
         else
         {  // destroy the window

            // note: set to FALSE before window destruction to avoid recursion
            statsWinTtx.open = FALSE;

            sprintf(comm, "destroy %s", STATS_WIN_TTX_WNAM);
            eval_check(interp, comm);

            // disable extended statistics reports when all windows are closed
            if ( (dbStatsWinState[DB_TARGET_ACQ].open == FALSE) &&
                 (dbStatsWinState[DB_TARGET_UI].open == FALSE) )
            {
               EpgAcqCtl_EnableAcqStats(FALSE);
            }
         }
         // set the state of the checkbutton of the according menu entry
         Tcl_SetVar2Ex(interp, "menuStatusStatsOpen", "ttx_acq", Tcl_NewIntObj(statsWinTtx.open), 0);
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Schedule an update of all statistics output
// - argument target says which statistics popup needs to be updated.
//   Since the main window status line contains info about both acq and ui db
//   its update is unconditional.
// - the function is triggered by acq control regularily upon every AI reception
//   and after provider changes or after acquisition parameters are changed
//   or after a provider change by the user for the browser database
// - the function is also triggered by GUI provider changes
//   plus regularily every minute (to update expiration statistics)
// - execution is delayed, mainly for calls from acq control; then we must wait
//   until the EPG block queue is processed and the latest acq db stats are
//   available (the function is called from a callback before the AI block is
//   actually in the db)
//
void StatsWin_StatsUpdate( int target )
{
   // update the db statistics window of the given db, if open
   if (dbStatsWinState[target].open)
   {
      AddMainIdleEvent(StatsWin_UpdateDbStatsWin, UINT2PVOID(target), FALSE);
   }

   // if the ui db is the same as the acq db, forward updates from acq to ui stats popup
   if ( (target == DB_TARGET_ACQ) &&
        dbStatsWinState[DB_TARGET_UI].open &&
        dbStatsWinState[DB_TARGET_UI].isForAcq )
   {
      AddMainIdleEvent(StatsWin_UpdateDbStatsWin, (ClientData)DB_TARGET_UI, FALSE);
   }

   if (statsWinTtx.open)
   {
      AddMainIdleEvent(StatsWin_UpdateTtxStats, NULL, TRUE);
   }

   // update the main window status line
   AddMainIdleEvent(StatsWin_UpdateDbStatusLine, NULL, TRUE);
}

// ----------------------------------------------------------------------------
// Initialize module state variables
// - this should be called only once during start-up
//
void StatsWin_Create( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_StatsWin_ToggleDbStats", &cmdInfo) == 0)
   {
      Tcl_CreateCommand(interp, "C_StatsWin_ToggleDbStats", StatsWin_ToggleDbStats, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_StatsWin_ToggleTtxStats", StatsWin_ToggleTtxStats, (ClientData) NULL, NULL);
   }
   else
      fatal0("StatsWin-Create: commands are already created");

   memset(dbStatsWinState, 0, sizeof(dbStatsWinState));
}

