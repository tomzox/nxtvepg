/*
 *  Nextview acquisition control
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
 *    Controls the acquisition process.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgacqctl.c,v 1.28 2000/12/26 15:54:44 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <string.h>
#include <time.h>

#include "tcl.h"

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/epgdbacq.h"

#include "epgui/pifilter.h"
#include "epgui/menucmd.h"
#include "epgui/statswin.h"
#include "epgctl/epgmain.h"
#include "epgctl/epgctxctl.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/tvchan.h"
#include "epgvbi/btdrv.h"

#include "epgctl/epgacqctl.h"


// ---------------------------------------------------------------------------
// Declaration of module state variables

typedef struct {
   EPGACQ_STATE   state;
   EPGACQ_MODE    mode;
   EPGACQ_MODE    userMode;
   EPGACQ_PASSIVE passiveReason;
   EPGACQ_PHASE   cyclePhase;
   uint           cycleIdx;
   uint           cniCount;
   uint           cniTab[MAX_MERGED_DB_COUNT];
   time_t         dumpTime;
   uint           inputSource;
   bool           hasNoFrequency;
} EPGACQCTL_STATE;

static EPGACQCTL_STATE  acqCtl = {ACQSTATE_OFF, ACQMODE_FOLLOW_UI};
static EPGDB_STATS      acqStats;
static bool             acqStatsUpdate;

static EPGSCAN_STATE    scanState = SCAN_STATE_OFF;
static Tcl_TimerToken   scanHandler = NULL;
static bool             scanAcqWasEnabled;
static uint             scanChannel;
static uint             scanChannelCount;


static void EpgAcqCtl_InitCycle( void );
static uint EpgAcqCtl_UpdateProvider( bool changeDb );

// ---------------------------------------------------------------------------
// Reset statistic values
// - should be called right after start/reset acq and once after first AI
//
static void EpgAcqCtl_StatisticsReset( void )
{
   memset(&acqStats, 0, sizeof(acqStats));

   acqStats.acqStartTime  = time(NULL);
   acqStats.lastAiTime    = acqStats.acqStartTime;
   acqStats.minAiDistance = 0;
   acqStats.maxAiDistance = 0;
   acqStats.aiCount       = 0;
   acqStats.histIdx       = STATS_HIST_WIDTH - 1;

   acqStats.varianceHistCount = 0;
   acqStats.varianceHistIdx   = 0;

   acqStatsUpdate = TRUE;
}

// ---------------------------------------------------------------------------
// Maintain statistic values for AcqStat window
// - should be called after each received AI block
// - the history is also maintained when no stats window is open, because
//   once one is opened, a complete history can be displayed
//
static void EpgAcqCtl_StatisticsUpdate( void )
{
   ulong total, obsolete;
   time_t intv, now = time(NULL);
   uint idx;

   acqStatsUpdate = FALSE;

   // determine block counts in current database
   intv = ((acqCtl.cyclePhase == ACQMODE_PHASE_STREAM1) ? EPGACQCTL_STREAM1_UPD_INTV : EPGACQCTL_STREAM2_UPD_INTV);
   EpgDbGetStat(pAcqDbContext, acqStats.count, intv);

   if (acqCtl.state == ACQSTATE_RUNNING)
   {
      // compute minimum and maximum distance between AI blocks (in seconds)
      if (acqStats.aiCount > 0)
      {
         if ((now - acqStats.lastAiTime < acqStats.minAiDistance) || (acqStats.aiCount == 1))
            acqStats.minAiDistance = now - acqStats.lastAiTime;
         if (now - acqStats.lastAiTime > acqStats.maxAiDistance)
            acqStats.maxAiDistance = now - acqStats.lastAiTime;
         acqStats.avgAiDistance = ((double)acqStats.lastAiTime - acqStats.acqStartTime) / acqStats.aiCount;

      }
      else
      {
         acqStats.avgAiDistance =
         acqStats.minAiDistance =
         acqStats.maxAiDistance = now - acqStats.lastAiTime;
      }
      acqStats.lastAiTime = now;
      acqStats.aiCount += 1;

      // maintain history of block counts per stream
      total = acqStats.count[0].ai + acqStats.count[1].ai;
      obsolete = acqStats.count[0].obsolete + acqStats.count[1].obsolete;
      dprintf4("EpgAcqCtl-StatisticsUpdate: AI #%d, db filled %.2f%%, variance %1.2f/%1.2f\n", acqStats.aiCount, (double)(acqStats.count[0].allVersions + obsolete + acqStats.count[1].allVersions) / total * 100.0, acqStats.count[0].variance, acqStats.count[1].variance);

      acqStats.histIdx = (acqStats.histIdx + 1) % STATS_HIST_WIDTH;
      acqStats.hist_expir[acqStats.histIdx] = (uchar)((double)obsolete / total * 128.0);
      acqStats.hist_s1cur[acqStats.histIdx] = (uchar)((double)(acqStats.count[0].curVersion + obsolete) / total * 128.0);
      acqStats.hist_s1old[acqStats.histIdx] = (uchar)((double)(acqStats.count[0].allVersions + obsolete) / total * 128.0);
      acqStats.hist_s2cur[acqStats.histIdx] = (uchar)((double)(acqStats.count[0].allVersions + obsolete + acqStats.count[1].curVersion) / total * 128.0);
      acqStats.hist_s2old[acqStats.histIdx] = (uchar)((double)(acqStats.count[0].allVersions + obsolete + acqStats.count[1].allVersions) / total * 128.0);

      // maintain history of netwop coverage variance for cycle state machine
      if (ACQMODE_IS_CYCLIC(acqCtl.mode))
      {
         if (acqStats.varianceHistCount < VARIANCE_HIST_COUNT)
         {
            idx = acqStats.varianceHistCount;
            acqStats.varianceHistCount += 1;
         }
         else
         {
            acqStats.varianceHistIdx = (acqStats.varianceHistIdx + 1) % VARIANCE_HIST_COUNT;
            idx = acqStats.varianceHistIdx;
         }

         if (acqCtl.cyclePhase == ACQMODE_PHASE_STREAM1)
            acqStats.varianceHist[idx] = acqStats.count[0].variance;
         else if (acqCtl.cyclePhase == ACQMODE_PHASE_STREAM2)
            acqStats.varianceHist[idx] = acqStats.count[1].variance;
      }
   }
}

// ---------------------------------------------------------------------------
// Return statistic values for AcqStat window
//
const EPGDB_STATS * EpgAcqCtl_GetStatistics( void )
{
   assert(acqStatsUpdate == FALSE);  // must not be called asynchronously

   if (acqCtl.state != ACQSTATE_OFF)
   {
      // retrieve additional values from ttx decoder
      EpgDbAcqGetStatistics(&acqStats.ttxPkgCount, &acqStats.epgPkgCount, &acqStats.epgPagCount);

      return (const EPGDB_STATS *) &acqStats;
   }
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Start the acquisition
//
bool EpgAcqCtl_Start( void )
{
   bool result;

   assert(pAcqDbContext == NULL);

   if (acqCtl.state == ACQSTATE_OFF)
   {
      acqCtl.state = ACQSTATE_WAIT_BI;

      // initialize teletext decoder
      EpgDbAcqInit();

      EpgAcqCtl_InitCycle();
      EpgAcqCtl_UpdateProvider(FALSE);

      EpgDbAcqStart(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);

      if (BtDriver_StartAcq() == FALSE)
      {  // VBI slave process/thread does not exist or failed to start acq -> hold acq
         acqCtl.state = ACQSTATE_OFF;
         EpgDbAcqStop();
         EpgContextCtl_Close(pAcqDbContext);
         pAcqDbContext = NULL;
         result = FALSE;
      }
      else
      {
         EpgAcqCtl_StatisticsReset();
         EpgAcqCtl_StatisticsUpdate();
         acqCtl.dumpTime = acqStats.acqStartTime;

         result = TRUE;
      }
   }
   else
      result = TRUE;

   return result;
}

// ---------------------------------------------------------------------------
// Stop the acquisition
// - the acq-sub process is terminated and the VBI device is freed
// - this allows other processes (e.g. a teletext decoder) to access VBI
//
void EpgAcqCtl_Stop( void )
{
   if (acqCtl.state != ACQSTATE_OFF)
   {
      assert(pAcqDbContext != NULL);

      BtDriver_StopAcq();

      EpgDbAcqStop();
      EpgContextCtl_Close(pAcqDbContext);
      pAcqDbContext = NULL;

      StatsWin_ProvChange(DB_TARGET_ACQ);

      acqCtl.state = ACQSTATE_OFF;
   }
}

// ---------------------------------------------------------------------------
// Start or stop the acquisition (called from UI button callback)
// - returns the new status of acq: TRUE=running, FALSE=stopped
//
int EpgAcqCtl_Toggle( int newState )
{
   if (newState != acqCtl.state)
   {
      if (acqCtl.state == ACQSTATE_OFF)
      {
         EpgAcqCtl_Start();
      }
      else
      {
         EpgAcqCtl_Stop();
      }
   }
   else
   {  // no change
      debug1("EpgAcqCtl-Toggle: requested state %d already set", newState);
   }

   return acqCtl.state;
}

// ---------------------------------------------------------------------------
// Set input source and tuner frequency for a provider and check the result
//
static bool EpgAcqCtl_TuneProvider( ulong freq )
{
   bool isTuner;
   bool result = TRUE;

   // reset forced-passive state; will be set upon errors below
   acqCtl.mode = acqCtl.userMode;
   acqCtl.passiveReason = ACQPASSIVE_NONE;

   // always set the input source - may have been changed externally (since we don't hog the video device permanently)
   if ( BtDriver_SetInputSource(acqCtl.inputSource, TRUE, &isTuner) )
   {
      if (isTuner)
      {
         if (freq != 0)
         {
            // tune onto the provider's channel (before starting acq, to avoid catching false data)
            if (BtDriver_TuneChannel(freq, FALSE) == FALSE)
            {
               dprintf0("EpgAcqCtl-TuneProv: failed to tune channel -> force to passive mode\n");
               acqCtl.mode = ACQMODE_FORCED_PASSIVE;
               acqCtl.passiveReason = ACQPASSIVE_ACCESS_DEVICE;
               result = FALSE;
            }
         }
         else
         {
            debug0("EpgAcqCtl-TuneProv: no freq in db");
            acqCtl.mode = ACQMODE_FORCED_PASSIVE;
            acqCtl.passiveReason = ACQPASSIVE_NO_FREQ;
            result = FALSE;
         }
      }
      else
      {
         dprintf0("EpgAcqCtl-TuneProv: input is no tuner -> force to passive mode\n");
         acqCtl.mode = ACQMODE_FORCED_PASSIVE;
         acqCtl.passiveReason = ACQPASSIVE_NO_TUNER;
         result = FALSE;
      }
   }
   else
   {
      dprintf0("EpgAcqCtl-TuneProv: failed to set input source -> force to passive mode\n");
      acqCtl.mode = ACQMODE_FORCED_PASSIVE;
      acqCtl.passiveReason = ACQPASSIVE_ACCESS_DEVICE;
      result = FALSE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Chooses a provider for the acquisition
//
static uint EpgAcqCtl_GetProvider( void )
{
   uint cni;

   // use original user configured mode to ignore possible current error state
   // because the caller wants to know which TV channel ought to be tuned
   switch (acqCtl.userMode)
   {
      case ACQMODE_FOLLOW_UI:
         if (EpgDbContextIsMerged(pUiDbContext) == FALSE)
            cni = EpgDbContextGetCni(pUiDbContext);
         else  // this is an error case that should normally not be reached
            cni = 0;
         break;

      case ACQMODE_FOLLOW_MERGED:
      case ACQMODE_CYCLIC_2:
      case ACQMODE_CYCLIC_012:
      case ACQMODE_CYCLIC_02:
      case ACQMODE_CYCLIC_12:
         cni = acqCtl.cniTab[acqCtl.cycleIdx];
         break;

      case ACQMODE_PASSIVE:
      default:
         cni = 0;
         break;
   }
   return cni;
}

// ---------------------------------------------------------------------------
// Check the acq provider and change if neccessary
// - called upon start of acquisition
// - called when UI db is changed by user, or merge started
// - called when acq mode is changed by the user
// - called after end of a cycle phase for one db
//
static uint EpgAcqCtl_UpdateProvider( bool changeDb )
{
   const EPGDBSAV_PEEK * pPeek;
   ulong freq = 0;
   uint cni;
   bool result = TRUE;

   // determine current CNI depending on acq mode and index
   cni = EpgAcqCtl_GetProvider();

   // for acq start: open the requested db (or any database for passive mode)
   if (pAcqDbContext == NULL)
   {
      pAcqDbContext = EpgContextCtl_Open(cni);
      cni = EpgDbContextGetCni(pAcqDbContext);
   }

   if (cni != 0)
   {
      if ( (changeDb == FALSE) &&
           (acqCtl.state == ACQSTATE_WAIT_BI) && (pAcqDbContext->modified == FALSE))
      {
         changeDb = TRUE;
      }

      if (cni == EpgDbContextGetCni(pAcqDbContext))
      {
         freq = pAcqDbContext->tunerFreq;
      }
      else if ( changeDb )
      {  // switch to the new database
         EpgContextCtl_Close(pAcqDbContext);
         pAcqDbContext = EpgContextCtl_Open(cni);
         // notify the statistics GUI module
         StatsWin_ProvChange(DB_TARGET_ACQ);
         if (acqCtl.state != ACQSTATE_OFF)
         {  // reset acquisition
            EpgAcqCtl_ChannelChange(FALSE);
            EpgAcqCtl_StatisticsUpdate();
         }
         // reset acquisition repetition counters for all PI
         if (ACQMODE_IS_CYCLIC(acqCtl.mode) && (acqCtl.cyclePhase == ACQMODE_PHASE_NOWNEXT))
         {
            EpgDbResetAcqRepCounters(pAcqDbContext);
         }
         freq = pAcqDbContext->tunerFreq;
         acqCtl.dumpTime = time(NULL);
      }
      else
      {  // fetch the tuner frequency from the new provider
         pPeek = EpgDbPeek(cni, NULL);
         if (pPeek != 0)
         {
            freq = pPeek->tunerFreq;
            EpgDbPeekDestroy(pPeek);
         }
         else
            debug1("EpgAcqCtl-UpdateProvider: peek for 0x%04X failed", cni);
      }

      if ( (acqCtl.mode != ACQMODE_PASSIVE) &&
           ((acqCtl.mode != ACQMODE_FORCED_PASSIVE) || (acqCtl.passiveReason != ACQPASSIVE_NO_TUNER)) )
      {
         result = EpgAcqCtl_TuneProvider(freq);
      }
   }
   else
   {  // no provider to be tuned onto -> set at least the input source
      if (acqCtl.mode != ACQMODE_PASSIVE)
      {
         result = EpgAcqCtl_TuneProvider(0L);

         if ((result == FALSE) && (acqCtl.mode == ACQMODE_FORCED_PASSIVE) && (acqCtl.passiveReason == ACQPASSIVE_NO_FREQ))
         {  // ignore "no freq in db" error, since we don't have a db yet
            acqCtl.mode = acqCtl.userMode;
            acqCtl.passiveReason = ACQPASSIVE_NONE;
            result = TRUE;
         }
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Initialize the cyclic acquisition state machine
// - has to be called during acq start or after acq mode change
// - has to be called before UpdateProvider, because the cycle index must be initialized first
//
static void EpgAcqCtl_InitCycle( void )
{
   // for merged db follow-ui mode is replaced by cycle across all merged providers
   if ( (acqCtl.mode == ACQMODE_FOLLOW_UI) &&
        EpgDbContextIsMerged(pUiDbContext) )
   {
      if ( EpgDbMergeGetCnis(pUiDbContext, &acqCtl.cniCount, acqCtl.cniTab) )
      {
         dprintf1("EpgAcqCtl-InitCycle: switching from follow-ui to follow-merged for merged db with %d CNIs\n", acqCtl.cniCount);
         acqCtl.userMode = ACQMODE_FOLLOW_MERGED;
      }
   }

   // reset forced-passive state; will be set upon errors in UpdateProv func
   acqCtl.mode = acqCtl.userMode;
   acqCtl.passiveReason = ACQPASSIVE_NONE;

   switch (acqCtl.mode)
   {
      case ACQMODE_CYCLIC_012:
      case ACQMODE_CYCLIC_02:
         acqCtl.cyclePhase = ACQMODE_PHASE_NOWNEXT;
         break;
      case ACQMODE_CYCLIC_12:
         acqCtl.cyclePhase = ACQMODE_PHASE_STREAM1;
         break;
      case ACQMODE_FOLLOW_MERGED:
      case ACQMODE_CYCLIC_2:
         acqCtl.cyclePhase = ACQMODE_PHASE_STREAM2;
         break;
      case ACQMODE_PASSIVE:
      case ACQMODE_FORCED_PASSIVE:
      default:
         acqCtl.cyclePhase = ACQMODE_PHASE_STREAM2;
         break;
         break;
   }

   // use the first provider in the list
   acqCtl.cycleIdx = 0;
}

// ---------------------------------------------------------------------------
// Determine if netwop coverage variance is stable enough
//
static bool EpgAcqCtl_CheckVarianceStable( void )
{
   double min, max;
   uint idx;
   bool result;

   if (acqStats.varianceHistCount >= VARIANCE_HIST_COUNT)
   {
      // get min and max variance from the history list
      min = max = acqStats.varianceHist[0];
      for (idx=1; idx < VARIANCE_HIST_COUNT; idx++)
      {
         if (acqStats.varianceHist[idx] < min)
            min = acqStats.varianceHist[idx];
         if (acqStats.varianceHist[idx] > max)
            max = acqStats.varianceHist[idx];
      }
      // variance slope has to be below threshold
      result = ((max - min) <= MAX_CYCLE_VAR_DIFF);
   }
   else
      result = FALSE;

   return result;
}

// ---------------------------------------------------------------------------
// Advance the cycle phase
//
static void EpgAcqCtl_AdvanceCyclePhase( bool forceAdvance )
{
   double quote1, quote2;
   bool advance, wrongCni;
   uint cni;

   wrongCni = FALSE;
   if ( (acqCtl.mode != ACQMODE_PASSIVE) &&
        !((acqCtl.mode == ACQMODE_FORCED_PASSIVE) && (acqCtl.passiveReason != ACQPASSIVE_ACCESS_DEVICE)) )
   {
      cni = EpgAcqCtl_GetProvider();
      if ((cni != 0) && (EpgAcqCtl_GetProvider() != EpgDbContextGetCni(pAcqDbContext)))
      {  // not the expected provider -> try to switch
         EpgAcqCtl_UpdateProvider(FALSE);
         wrongCni = TRUE;
      }
   }

   if ( ((acqCtl.state == ACQSTATE_RUNNING) || forceAdvance) &&
        (wrongCni == FALSE) &&
        (scanState == SCAN_STATE_OFF) &&
        ( ACQMODE_IS_CYCLIC(acqCtl.mode) ||
          ((acqCtl.mode == ACQMODE_FORCED_PASSIVE) && ACQMODE_IS_CYCLIC(acqCtl.userMode) && (acqCtl.passiveReason == ACQPASSIVE_ACCESS_DEVICE)) ) &&
        (acqCtl.cniCount > 1) )
   {
      // determine if acq for the current phase is complete
      switch (acqCtl.cyclePhase)
      {
         case ACQMODE_PHASE_NOWNEXT:
            advance = (EpgDbGetNowCycleMaxRepCounter(pAcqDbContext) >= 2);
            break;
         case ACQMODE_PHASE_STREAM1:
            quote1 = (double)acqStats.count[0].sinceAcq / acqStats.count[0].ai;
            advance = ((acqStats.count[0].variance < MIN_CYCLE_VARIANCE) && (quote1 >=MIN_CYCLE_QUOTE)) &&
                      (EpgAcqCtl_CheckVarianceStable() || (acqStats.count[0].variance == 0.0));
            break;
         case ACQMODE_PHASE_STREAM2:
            quote1 = (double)acqStats.count[0].sinceAcq / acqStats.count[0].ai;
            quote2 = (double)acqStats.count[1].sinceAcq / acqStats.count[1].ai; ;
            advance = ((acqStats.count[0].variance < MIN_CYCLE_VARIANCE) && (quote1 >=MIN_CYCLE_QUOTE)) &&
                      ((acqStats.count[1].variance < MIN_CYCLE_VARIANCE) && (quote2 >= MIN_CYCLE_QUOTE)) &&
                      EpgAcqCtl_CheckVarianceStable();
            break;
         default:
            SHOULD_NOT_BE_REACHED;
            advance = TRUE;
            break;
      }

      if (advance || forceAdvance)
      {
         // determine if all databases were covered in the current phase
         if (acqCtl.cycleIdx + 1 < acqCtl.cniCount)
         {
            acqCtl.cycleIdx += 1;
            dprintf2("EpgAcqCtl: advance to CNI #%d of %d\n", acqCtl.cycleIdx, acqCtl.cniCount);
         }
         else
         {  // phase complete for all databases -> switch to next
            // determine next phase
            switch (acqCtl.mode)
            {
               case ACQMODE_CYCLIC_012:
                  acqCtl.cyclePhase = (acqCtl.cyclePhase + 1) % ACQMODE_PHASE_COUNT;
                  break;
               case ACQMODE_CYCLIC_02:
                  if (acqCtl.cyclePhase == ACQMODE_PHASE_NOWNEXT)
                     acqCtl.cyclePhase = ACQMODE_PHASE_STREAM2;
                  else
                     acqCtl.cyclePhase = ACQMODE_PHASE_NOWNEXT;
                  break;
               case ACQMODE_CYCLIC_12:
                  if (acqCtl.cyclePhase == ACQMODE_PHASE_STREAM1)
                     acqCtl.cyclePhase = ACQMODE_PHASE_STREAM2;
                  else
                     acqCtl.cyclePhase = ACQMODE_PHASE_STREAM1;
                  break;
               case ACQMODE_FOLLOW_MERGED:
               case ACQMODE_CYCLIC_2:
               default:
                  assert(acqCtl.cyclePhase == ACQMODE_PHASE_STREAM2);
                  break;
            }
            dprintf1("EpgAcqCtl: advance to phase #%d\n", acqCtl.cyclePhase);
            // restart with first database
            acqCtl.cycleIdx = 0;
         }

         // note about parameter "TRUE": open the new db immediately because
         // it may need to be accessed, e.g. to reset rep counters
         EpgAcqCtl_UpdateProvider(TRUE);
      }
   }
}

// ---------------------------------------------------------------------------
// Notification about UI browser database change
// - this func is for the follow-ui mode what AdvanceCycle is for cyclic mode
//
bool EpgAcqCtl_UiProvChange( void )
{
   bool result;

   if ( (acqCtl.state != ACQSTATE_OFF) &&
        ((acqCtl.userMode == ACQMODE_FOLLOW_UI) || (acqCtl.userMode == ACQMODE_FOLLOW_MERGED)) &&
        (scanState == SCAN_STATE_OFF) )
   {
      // reset mode from follow-merged to follow-ui
      acqCtl.userMode = ACQMODE_FOLLOW_UI;

      EpgAcqCtl_InitCycle();
      result = EpgAcqCtl_UpdateProvider(FALSE);
   }
   else
      result = TRUE;

   return result;
}

// ---------------------------------------------------------------------------
// Select acquisition mode and provider
//
bool EpgAcqCtl_SelectMode( EPGACQ_MODE newAcqMode, uint cniCount, const uint * pCniTab )
{
   bool result;

   assert(newAcqMode < ACQMODE_COUNT);

   acqCtl.cniCount = cniCount;
   if ((cniCount > 0) && (pCniTab != NULL))
   {  // save the new CNI table into the allocated array
      memcpy(acqCtl.cniTab, pCniTab, sizeof(acqCtl.cniTab));
   }
   else if (ACQMODE_IS_CYCLIC(newAcqMode))
   {  // error case, should not be reached
      debug2("EpgAcqCtl-SelectMode: cyclic mode without provider list (count=%d,ptr=%lx) -> using follow-ui", cniCount, (ulong)pCniTab);
      newAcqMode = ACQMODE_FOLLOW_UI;
   }

   acqCtl.userMode = newAcqMode;
   acqCtl.mode = newAcqMode;

   if (acqCtl.state != ACQSTATE_OFF)
   {
      EpgAcqCtl_InitCycle();
      result = EpgAcqCtl_UpdateProvider(TRUE);
   }
   else
      result = TRUE;

   return result;
}

// ---------------------------------------------------------------------------
// Select input source: tuner or composite
// - this info is very important to the acq control, since only when a
//   tuner is the input source, the channel can be controlled; else only
//   passive acq mode is possible
//
bool EpgAcqCtl_SetInputSource( uint inputIdx )
{
   bool result;

   acqCtl.inputSource = inputIdx;

   if ((acqCtl.state != ACQSTATE_OFF) && (acqCtl.mode != ACQMODE_PASSIVE))
   {
      EpgAcqCtl_InitCycle();
      result = EpgAcqCtl_UpdateProvider(TRUE);
   }
   else
      result = TRUE;

   return result;
}

// ---------------------------------------------------------------------------
// Translate forced-passive reason to db state
// - Help function for GetDbState
//
static EPGDB_STATE EpgAcqCtl_GetForcedPassiveState( void )
{
   EPGDB_STATE state;

   assert(acqCtl.mode == ACQMODE_FORCED_PASSIVE);

   switch (acqCtl.passiveReason)
   {
      case ACQPASSIVE_NO_TUNER:
         state = EPGDB_ACQ_NO_TUNER;
         break;
      case ACQPASSIVE_NO_FREQ:
         state = EPGDB_ACQ_NO_FREQ;
         break;
      case ACQPASSIVE_ACCESS_DEVICE:
         state = EPGDB_ACQ_ACCESS_DEVICE;
         break;
      default:
         SHOULD_NOT_BE_REACHED;
         state = EPGDB_ACQ_PASSIVE;
         break;
   }
   return state;
}
// ---------------------------------------------------------------------------
// Determine state of UI database and acquisition
// - this is used to display a helpful message in the PI listbox as long as
//   the database is empty
//
EPGDB_STATE EpgAcqCtl_GetDbState( void )
{
   FILTER_CONTEXT *pPreFilterContext;
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK *pPiBlock;
   EPGDB_STATE state;
   uint cni, idx;

   if (pUiDbContext != NULL)
   {
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      if (pAiBlock == NULL)
      {  // no AI block in current database
         if (EpgDbReloadScan(-1) == 0)
         {
            if ((acqCtl.state != ACQSTATE_OFF) && (acqCtl.mode == ACQMODE_FORCED_PASSIVE) && (acqCtl.passiveReason == ACQPASSIVE_NO_TUNER))
               state = EPGDB_PROV_WAIT;
            else
               state = EPGDB_PROV_SCAN;
         }
         else
            state = EPGDB_PROV_SEL;
      }
      else
      {  // provider present -> check for PI
         pPiBlock = EpgDbSearchFirstPi(pUiDbContext, NULL);
         if (pPiBlock == NULL)
         {  // no PI in database (probably all expired)
            if (acqCtl.state != ACQSTATE_OFF)
            {  // acq is running
               cni = EpgDbContextGetCni(pUiDbContext);
               if (acqCtl.mode == ACQMODE_PASSIVE)
               {
                  state = EPGDB_ACQ_PASSIVE;
               }
               else if (acqCtl.mode == ACQMODE_FORCED_PASSIVE)
               {
                  state = EpgAcqCtl_GetForcedPassiveState();
               }
               else if ( (ACQMODE_IS_CYCLIC(acqCtl.mode) == FALSE) ||
                         (EpgDbContextGetCni(pAcqDbContext) == cni) )
               {  // acq is running for the same provider as the UI waits for
                  state = EPGDB_ACQ_WAIT;
               }
               else
               {  // check if the provider is in the acq provider list
                  for (idx=0; idx < acqCtl.cniCount; idx++)
                     if (acqCtl.cniTab[idx] == cni)
                        break;
                  if (idx < acqCtl.cniCount)
                  {  // CNI not active, but in list -> try to switch
                     dprintf2("EpgAcqCtl-GetDbState: tuning provider 0x%04X, cycle idx %d\n", cni, idx);
                     acqCtl.cycleIdx = idx;
                     EpgAcqCtl_UpdateProvider(TRUE);

                     if (acqCtl.mode == ACQMODE_FORCED_PASSIVE)
                        state = EpgAcqCtl_GetForcedPassiveState();
                     else
                        state = EPGDB_ACQ_WAIT;
                  }
                  else
                     state = EPGDB_ACQ_OTHER_PROV;
               }
            }
            else
               state = EPGDB_EMPTY;
         }
         else
         {
            pPreFilterContext = EpgDbFilterCopyContext(pPiFilterContext);
            EpgDbFilterDisable(pPreFilterContext, FILTER_ALL & ~FILTER_NETWOP_PRE);

            pPiBlock = EpgDbSearchFirstPi(pUiDbContext, pPreFilterContext);
            if (pPiBlock == NULL)
            {  // PI present, but none match the prefilter
               state = EPGDB_PREFILTERED_EMPTY;
            }
            else
            {  // everything is ok
               state = EPGDB_OK;
            }
            EpgDbFilterDestroyContext(pPreFilterContext);
         }
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
   }
   else
      state = EPGDB_NOT_INIT;

   return state;
}

// ---------------------------------------------------------------------------
// AI callback: invoked before a new AI block is inserted into the database
//
bool EpgAcqCtl_AiCallback( const AI_BLOCK *pNewAi )
{
   const AI_BLOCK *pOldAi;
   ulong oldTunerFreq;
   uint acqCni;
   bool accept = FALSE;

   if (acqCtl.state == ACQSTATE_WAIT_BI)
   {
      accept = FALSE;
   }
   else if ((acqCtl.state == ACQSTATE_WAIT_AI) || (acqCtl.state == ACQSTATE_RUNNING))
   {
      DBGONLY(if (acqCtl.state == ACQSTATE_WAIT_AI))
         dprintf3("EpgCtl: AI found, CNI=%X version %d/%d\n", AI_GET_CNI(pNewAi), pNewAi->version, pNewAi->version_swo);
      EpgDbLockDatabase(pAcqDbContext, TRUE);
      pOldAi = EpgDbGetAi(pAcqDbContext);

      if (pOldAi == NULL)
      {  // the current db is empty
         EpgDbLockDatabase(pAcqDbContext, FALSE);
         DBGONLY(pOldAi = NULL);

         oldTunerFreq = pAcqDbContext->tunerFreq;
         EpgContextCtl_Close(pAcqDbContext);
         pAcqDbContext = EpgContextCtl_Open(AI_GET_CNI(pNewAi));
         if ((pAcqDbContext->tunerFreq != oldTunerFreq) && (oldTunerFreq != 0))
         {
            dprintf2("EpgAcqCtl: updating tuner freq: %ld -> %ld\n", pAcqDbContext->tunerFreq, oldTunerFreq);
            pAcqDbContext->modified = TRUE;
            pAcqDbContext->tunerFreq = oldTunerFreq;
         }

         // update the ui netwop list if neccessary
         Tcl_DoWhenIdle(PiFilter_UpdateNetwopList, NULL);
         acqCtl.state = ACQSTATE_RUNNING;
         EpgAcqCtl_StatisticsReset();
         accept = TRUE;

         // if reload succeeded -> must check AI again
         if (EpgDbContextGetCni(pAcqDbContext) != AI_GET_CNI(pNewAi))
         {  // new provider -> dump asap, to show up in prov select menu
            acqCtl.dumpTime = 0;
         }
         EpgDbLockDatabase(pAcqDbContext, TRUE);
      }

      if (pOldAi != NULL)
      {
         if (AI_GET_CNI(pOldAi) == AI_GET_CNI(pNewAi))
         {
            if ( (pOldAi->version != pNewAi->version) ||
                 (pOldAi->version_swo != pNewAi->version_swo) )
            {
               dprintf2("EpgAcqCtl: version number has changed, was: %d/%d\n", pOldAi->version, pOldAi->version_swo);
               Tcl_DoWhenIdle(PiFilter_UpdateNetwopList, NULL);
               StatsWin_VersionChange();
               EpgAcqCtl_StatisticsReset();
               acqCtl.dumpTime = 0;  // dump asap
            }
            else
            {  // just a regular AI repetition, no news
               acqStatsUpdate = TRUE;
            }

            acqCtl.state = ACQSTATE_RUNNING;
            accept = TRUE;
         }
         else
         {  // wrong provider -> switch acq database
            dprintf2("EpgAcqCtl: switching acq db from %04X to %04X\n", AI_GET_CNI(pOldAi), AI_GET_CNI(pNewAi));
            acqCni = EpgAcqCtl_GetProvider();
            if ((acqCni != 0) && (acqCni != AI_GET_CNI(pNewAi)))
            {  // not the provider we want -> go to passive mode and alert the user
               //XXX TODO
            }
            EpgDbLockDatabase(pAcqDbContext, FALSE);
            EpgContextCtl_Close(pAcqDbContext);
            pAcqDbContext = EpgContextCtl_Open(AI_GET_CNI(pNewAi));

            EpgDbLockDatabase(pAcqDbContext, TRUE);
            assert((EpgDbContextGetCni(pAcqDbContext) == AI_GET_CNI(pNewAi)) || (EpgDbContextGetCni(pAcqDbContext) == 0));
            EpgDbAcqReset(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
            EpgAcqCtl_StatisticsReset();
            // dump db asap to allow user to select the new provider
            pOldAi = EpgDbGetAi(pAcqDbContext);
            if (pOldAi == NULL)
               acqCtl.dumpTime = 0;
            else
               acqCtl.dumpTime = time(NULL);
            StatsWin_ProvChange(DB_TARGET_ACQ);
            acqCtl.state = ACQSTATE_WAIT_BI;
            accept = TRUE;
         }
      }
      EpgDbLockDatabase(pAcqDbContext, FALSE);
   }
   else
   {  // should be reached only during epg scan -> discard block
      assert(scanState != SCAN_STATE_OFF);
   }

   return accept;
}

// ---------------------------------------------------------------------------
// Called by the database management when a new BI block was received
// - the BI block is never inserted into the database
// - only the application ID is extracted and saved in the db context
//
bool EpgAcqCtl_BiCallback( const BI_BLOCK *pNewBi )
{
   if (pNewBi->app_id == EPG_ILLEGAL_APPID)
   {
      dprintf0("EpgCtl: EPG not listed in BI - stop acq\n");
   }
   else
   {
      if (pAcqDbContext->appId != EPG_ILLEGAL_APPID)
      {
         if (pAcqDbContext->appId != pNewBi->app_id)
         {  // not the default id
            dprintf2("EpgCtl: app-ID changed from %d to %d\n", pAcqDbContext->appId, pAcqDbContext->appId);
            EpgDbAcqReset(pAcqDbContext, EPG_ILLEGAL_PAGENO, pNewBi->app_id);
         }
      }
      else
         dprintf1("EpgAcqCtl-BiCallback: BI now in db, appID=%d\n", pNewBi->app_id);

      switch (acqCtl.state)
      {
         case ACQSTATE_WAIT_BI:
            dprintf1("EpgCtl: BI found, appId=%d\n", pNewBi->app_id);
            acqCtl.state = ACQSTATE_WAIT_AI;
            // app-ID can not be accepted until CNI in the following AI block is checked
            break;

         case ACQSTATE_WAIT_AI:
            // app-ID can not be accepted until CNI in the following AI block is checked
            break;

         case ACQSTATE_RUNNING:
            pAcqDbContext->appId = pNewBi->app_id;
            break;

         default:
            // should be reached only during epg scan -> discard block
            assert(scanState != SCAN_STATE_OFF);
            break;
      }
   }

   // the BI block is never added to the db
   return FALSE;
}

// ---------------------------------------------------------------------------
// Notification from acquisition about channel change
//
void EpgAcqCtl_ChannelChange( bool changeDb )
{
   if (acqCtl.state != ACQSTATE_OFF)
   {
      if ( changeDb &&
           (pAcqDbContext != pUiDbContext) && (pUiDbContext != NULL) &&
           (EpgDbContextIsMerged(pUiDbContext) == FALSE) )
      {  // close acq db and fall back to ui db
         EpgContextCtl_Close(pAcqDbContext);
         pAcqDbContext = EpgContextCtl_Open(0);
         StatsWin_ProvChange(DB_TARGET_ACQ);
      }

      EpgDbAcqReset(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
      acqCtl.state = ACQSTATE_WAIT_BI;
      EpgAcqCtl_StatisticsReset();
   }
}

// ---------------------------------------------------------------------------
// Periodic check if the acquisition process is still alive
// - called from main loop
//
void EpgAcqCtl_Idle( void )
{
   time_t now = time(NULL);

   if ( (acqCtl.state != ACQSTATE_OFF) && (scanState == SCAN_STATE_OFF) )
   {
      // preriodic db dump
      if (acqCtl.state == ACQSTATE_RUNNING)
      {
         if (pAcqDbContext->modified)
         {
            if (now >= acqCtl.dumpTime + EPGACQCTL_DUMP_INTV)
            {
               EpgDbLockDatabase(pAcqDbContext, TRUE);
               if ( EpgDbDump(pAcqDbContext) )
               {
                  dprintf1("EpgAcqCtl-Idle: dumped db %04X to file\n", EpgDbContextGetCni(pAcqDbContext));
                  acqCtl.dumpTime = time(NULL);
               }
               EpgDbLockDatabase(pAcqDbContext, FALSE);
            }
         }
      }

      // acq running, but no reception -> wait for timeout, then reset acq
      if (now >= acqCtl.dumpTime + EPGACQCTL_MODIF_INTV)
      {
         dprintf1("EpgAcqCtl-Idle: no reception from provider 0x%04X - reset acq\n", EpgDbContextGetCni(pAcqDbContext));
         if (ACQMODE_IS_CYCLIC(acqCtl.mode) == FALSE)
         {
            EpgAcqCtl_ChannelChange(TRUE);
            EpgAcqCtl_StatisticsUpdate();
         }
         else
         {  // no reception -> advance cycle
            EpgAcqCtl_AdvanceCyclePhase(TRUE);
         }
      }
      assert(acqStatsUpdate == FALSE);
   }
}

// ---------------------------------------------------------------------------
// Process all available lines from VBI and insert blocks to db
//
void EpgAcqCtl_ProcessPackets( void )
{
   uint pageNo;

   if (pAcqDbContext != NULL)
   {
      assert(EpgDbIsLocked(pAcqDbContext) == FALSE);

      if (EpgDbAcqCheckForPackets())
      {
         acqStatsUpdate = FALSE;

         EpgDbAcqProcessPackets(&pAcqDbContext);

         dprintf4("EpgAcqCtl: state=%d phase=%d CNI-idx=%d (0x%04x)\n", acqCtl.state, acqCtl.cyclePhase, acqCtl.cycleIdx, acqCtl.cniTab[acqCtl.cycleIdx]);

         // if new AI received, update statistics (after AI was inserted into db)
         if (acqStatsUpdate)
         {
            EpgAcqCtl_StatisticsUpdate();

            EpgAcqCtl_AdvanceCyclePhase(FALSE);
         }
         else if (acqCtl.dumpTime == 0)
         {  // immediate dump
            EpgDbLockDatabase(pAcqDbContext, TRUE);
            if ( EpgDbDump(pAcqDbContext) )
            {
               dprintf1("EpgAcqCtl-Idle: dumped db %04X to file\n", EpgDbContextGetCni(pAcqDbContext));
               acqCtl.dumpTime = time(NULL);
            }
            EpgDbLockDatabase(pAcqDbContext, FALSE);
         }
      }

      // check the MIP if EPG is transmitted on a different page
      pageNo = EpgDbAcqGetMipPageNo();
      if ( (pageNo != EPG_ILLEGAL_PAGENO) && (pageNo != pAcqDbContext->pageNo))
      {  // found a different page number in MIP
         dprintf1("EpgAcqCtl-Idle: non-default MIP page no for EPG %x -> restart acq\n", pageNo);

         // XXX the pageno should not be saved before RUNNING since this might be the wrong db
         pAcqDbContext->pageNo = pageNo;
         EpgDbAcqReset(pAcqDbContext, pageNo, EPG_ILLEGAL_APPID);
      }
   }
}

// ----------------------------------------------------------------------------
//                             E P G    S C A N
// ----------------------------------------------------------------------------

// The purpose of the EPG scan is to find out the tuner frequencies of all
// EPG providers. This would not be needed it the Nextview decoder was
// integrated into a TV application. But as a separate application, there
// is no other way to find out the frequencies, since /dev/video can not
// be opened twice, not even to only query the actual tuner frequency.

// The scan is designed to perform as fast as possible. We stay max. 2 secs
// on each channel. If a CNI is found and it's a known and loaded provider,
// we go on immediately. If no CNI is found or if it's not a known provider,
// we wait if any syntactically correct packets are received on any of the
// 8*24 possible EPG teletext pages. If yes, we try for 45 seconds to
// receive the BI and AI blocks. This period is chosen that long, because
// some providers have gaps of over 45 seconds between cycles (e.g. RTL-II)
// and others do not transmit on the default page 1DF, so we have to wait
// for the MIP, which usually is transmitted about every 30 seconds. For
// any provider that's found, a database is created and the frequency
// saved in its header.

// ----------------------------------------------------------------------------
// EPG scan timer event handler - called every 250ms
// 
static void EventHandler_EpgScan( ClientData clientData )
{
   const EPGDBSAV_PEEK *pPeek;
   const AI_BLOCK *pAi;
   time_t now = time(NULL);
   ulong freq;
   uint cni, dataPageCnt;
   time_t delay;
   bool niWait;

   scanHandler = NULL;
   if (scanState != SCAN_STATE_OFF)
   {
      if (scanState == SCAN_STATE_RESET)
      {  // reset state again 50ms after channel change
         EpgDbAcqInitScan();
         //printf("Start channel %d\n", scanChannel);
         if (BtDriver_IsVideoPresent() == FALSE)
            scanState = SCAN_STATE_DONE;
         else
            scanState = SCAN_STATE_WAIT;
      }
      else
      {
         EpgDbAcqGetScanResults(&cni, &niWait, &dataPageCnt);

         if (scanState == SCAN_STATE_WAIT_EPG)
         {
            if (acqCtl.state == ACQSTATE_RUNNING)
            {  // AI block has been received
               assert((pAcqDbContext->pAiBlock != NULL) && pAcqDbContext->modified);

               EpgDbDump(pAcqDbContext);

               EpgDbLockDatabase(pAcqDbContext, TRUE);
               pAi = EpgDbGetAi(pAcqDbContext);
               if (pAi != NULL)
               {
                  sprintf(comm, "Found new provider: %s", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
                  MenuCmd_AddEpgScanMsg(comm);
                  scanState = SCAN_STATE_DONE;
               }
               EpgDbLockDatabase(pAcqDbContext, FALSE);
            }
         }
         else if ((cni != 0) && (scanState <= SCAN_STATE_WAIT_NI))
         {
            sprintf(comm, "Channel %d: network id 0x%04X", scanChannel, cni);
            MenuCmd_AddEpgScanMsg(comm);
            pPeek = EpgDbPeek(cni, NULL);
            if (pPeek != NULL)
            {  // provider already loaded -> skip
               sprintf(comm, "provider already known: %s",
                             AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, pPeek->pAiBlock->blk.ai.thisNetwop));
               MenuCmd_AddEpgScanMsg(comm);
               scanState = SCAN_STATE_DONE;
               if (pPeek->tunerFreq != pAcqDbContext->tunerFreq)
               {
                  MenuCmd_AddEpgScanMsg("storing provider's tuner frequency");
                  EpgContextCtl_UpdateFreq(cni, pAcqDbContext->tunerFreq);
               }
               EpgDbPeekDestroy(pPeek);
            }
            else
            {  // no database for this CNI yet
               switch (cni)
               {
                  case 0x0D8F:  // RTL-II (Germany)
                  case 0x1D8F:
                  case 0x0D94:  // PRO7 (Germany)
                  case 0x1D94:
                  case 0x0DC7:  // 3SAT (Germany)
                  case 0x1DC7:
                  case 0x2FE1:  // EuroNews (Germany)
                  case 0xFE01:
                  case 0x24C2:  // TSR1 (Switzerland)
                  case 0x2FE5:  // TV5 (France)
                  case 0xF500:
                  case 0x2F06:  // M6 (France)
                     // known provider -> wait for BI/AI
                     MenuCmd_AddEpgScanMsg("checking for EPG transmission...");
                     scanState = SCAN_STATE_WAIT_EPG;
                     // enable normal EPG acq callback handling
                     acqCtl.state = ACQSTATE_WAIT_BI;
                     break;
                  default:
                     // CNI not known as provider -> keep checking for data page
                     scanState = SCAN_STATE_WAIT_DATA;
                     break;
               }
            }
         }
         else if ((dataPageCnt != 0) && (scanState <= SCAN_STATE_WAIT_DATA))
         {
            sprintf(comm, "Found ETS-708 syntax on %d pages", dataPageCnt);
            MenuCmd_AddEpgScanMsg(comm);
            MenuCmd_AddEpgScanMsg("checking for EPG transmission...");
            scanState = SCAN_STATE_WAIT_EPG;
            // enable normal EPG acq callback handling
            acqCtl.state = ACQSTATE_WAIT_BI;
         }
         else if (niWait)
         {
            scanState = SCAN_STATE_WAIT_NI;
         }
      }

      // determine timeout for the current state
      switch (scanState)
      {
         case SCAN_STATE_WAIT:      delay =  2; break;
         case SCAN_STATE_WAIT_NI:   delay =  4; break;
         case SCAN_STATE_WAIT_DATA: delay =  2; break;
         case SCAN_STATE_WAIT_EPG:  delay = 45; break;
         default:                   delay =  0; break;
      }

      if ( (scanState == SCAN_STATE_DONE) || ((now - acqStats.acqStartTime) >= delay) )
      {  // max wait exceeded -> next channel
         if ( TvChannels_GetNext(&scanChannel, &freq) )
         {
            if ( BtDriver_TuneChannel(freq, TRUE) )
            {
               EpgDbAcqReset(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
               EpgDbAcqInitScan();

               // automatically dump db if provider was found, then free resources
               assert((pAcqDbContext->pAiBlock == NULL) || (pAcqDbContext->tunerFreq != 0));
               EpgContextCtl_Close(pAcqDbContext);

               pAcqDbContext = EpgContextCtl_CreateNew();
               pAcqDbContext->tunerFreq = freq;

               acqStats.acqStartTime = now;
               scanState = SCAN_STATE_RESET;
               acqCtl.state = ACQSTATE_OFF;
               scanChannelCount += 1;
               sprintf(comm, ".epgscan.all.baro.bari configure -width %d\n",
                             (int)((double)scanChannelCount/TvChannels_GetCount()*140.0));
               eval_check(interp, comm);

               scanHandler = Tcl_CreateTimerHandler(150, EventHandler_EpgScan, NULL);
            }
            else
            {
               EpgAcqCtl_StopScan();
               MenuCmd_AddEpgScanMsg("channel change failed - abort.");
               eval_check(interp, "bell");
               MenuCmd_StopEpgScan(NULL, interp, 0, NULL);
            }
         }
         else
         {
            EpgAcqCtl_StopScan();
            MenuCmd_AddEpgScanMsg("EPG scan finished.");
            eval_check(interp, "bell");
            MenuCmd_StopEpgScan(NULL, interp, 0, NULL);
         }
      }
      else
      {  // continue scan on current channel
         scanHandler = Tcl_CreateTimerHandler(250, EventHandler_EpgScan, NULL);
      }
   }
   else
      debug0("EventHandler-EpgScan: scan not running");
}

// ----------------------------------------------------------------------------
// Start EPG scan
// - sets up the scan for the first channel; the real work is done in the
//   timer event handler, which must be called every 250 ms
// - returns FALSE if either /dev/video or /dev/vbi could not be opened
//
EPGSCAN_START_RESULT EpgAcqCtl_StartScan( void )
{
   ulong freq;
   bool  isTuner;
   EPGSCAN_START_RESULT result;

   if ( BtDriver_SetInputSource(acqCtl.inputSource, TRUE, &isTuner) )
   {
      if (isTuner)
      {
         scanChannel = 0;
         if ( TvChannels_GetNext(&scanChannel, &freq) &&
              BtDriver_TuneChannel(freq, TRUE) )
         {
            // stop acq if running, but remember the state for when scan finishes
            scanAcqWasEnabled = (acqCtl.state != ACQSTATE_OFF);
            EpgAcqCtl_Stop();

            // set up an empty db and start EPG and CNI acquisition
            pAcqDbContext = EpgContextCtl_CreateNew();
            pAcqDbContext->tunerFreq = freq;
            EpgDbAcqInit();
            EpgDbAcqStart(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
            EpgDbAcqInitScan();

            if (BtDriver_StartAcq())
            {
               scanState = SCAN_STATE_RESET;
               acqCtl.state = ACQSTATE_OFF;
               acqStats.acqStartTime = time(NULL);
               scanHandler = Tcl_CreateTimerHandler(150, EventHandler_EpgScan, NULL);
               scanChannelCount = 0;

               sprintf(comm, "Starting scan on channel %d", scanChannel);
               MenuCmd_AddEpgScanMsg(comm);
               sprintf(comm, ".epgscan.all.baro.bari configure -width 1\n");
               eval_check(interp, comm);

               result = EPGSCAN_OK;
            }
            else
            {  // failed to start acquisition
               EpgDbAcqStop();
               EpgContextCtl_Close(pAcqDbContext);
               pAcqDbContext = NULL;

               result = EPGSCAN_ACCESS_DEV_VBI;
            }
         }
         else
            result = EPGSCAN_ACCESS_DEV_VIDEO;
      }
      else
         result = EPGSCAN_NO_TUNER;
   }
   else
      result = EPGSCAN_ACCESS_DEV_VIDEO;

   return result;
}

// ----------------------------------------------------------------------------
// Stop EPG scan
// - might be called repeatedly by UI to ensure background activity has stopped
//
void EpgAcqCtl_StopScan( void )
{
   if (scanState != SCAN_STATE_OFF)
   {
      Tcl_DeleteTimerHandler(scanHandler);
      scanHandler = NULL;

      #ifndef WIN32
      BtDriver_CloseDevice();
      #endif
      BtDriver_StopAcq();

      EpgDbAcqStop();
      EpgContextCtl_Close(pAcqDbContext);
      pAcqDbContext = NULL;
      acqCtl.state = ACQSTATE_OFF;

      if (scanAcqWasEnabled)
         EpgAcqCtl_Start();

      scanState = SCAN_STATE_OFF;
   }
}

