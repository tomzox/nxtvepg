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
 *    This module controls the Nextview EPG acquisition process. This
 *    covers tuning a provider's TV channel, controlling the EPG stream
 *    decoder and forwarding decoded blocks to the database, monitoring
 *    for channel changes, maintaining statistics and determing when
 *    the current acqisition stage is complete to allow switching to
 *    the next provider.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgacqnxt.c,v 1.8 2009/05/02 19:25:17 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"

#include "epgui/uictrl.h"
#include "epgui/epgmain.h"
#include "epgctl/epgctxmerge.h"
#include "epgctl/epgctxctl.h"
#include "epgctl/epgscan.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgctl/epgacqnxt.h"


// ---------------------------------------------------------------------------
// Declaration of module state variables
//
typedef enum
{
   ACQSTATE_OFF,            // acq is disabled
   ACQSTATE_WAIT_BI,        // wait for BI block
   ACQSTATE_WAIT_AI,        // wait for AI block
   ACQSTATE_RUNNING         // acq is up & running
} EPGACQ_STATE;

typedef struct
{
   EPGACQ_STATE   state;
   EPGACQ_MODE    mode;
   EPGACQ_PASSIVE passiveReason;
   EPGACQ_PHASE   cyclePhase;
   uint           reqCni;
   time_t         dumpTime;
   time_t         chanChangeTime;
   bool           chanChangePend;
   uint           currentPageNo;

   EPG_NXTV_ACQ_STATS acqStats;
   EPGDB_CONTEXT  *pAcqDbContext;
   bool           acqStatsUpdate;
   EPGDB_QUEUE    acqDbQueue;
   EPGDB_PI_TSC   acqTscQueue;
   bool           acqTscEnabled;
} EPGACQCTL_STATE;

static EPGACQCTL_STATE  acqCtl;


static bool EpgAcqNxtv_UpdateProvider( bool changeDb );

// when very many new blocks are in the queue, connection to UI must be locked
#define EPG_QUEUE_OVERFLOW_LEN      250

#define EPGACQ_SLICER_INTV       20   // check slicer after X secs
#define EPGACQ_DB_DUMP_INTV      60   // interval between db dumps
#define EPGACQ_ADV_CHK_INTV      15   // interval between cycle complete checks
#define EPGACQ_CHN_FIX_INTV      20   // interval between channel change attempts when forced passive
#define EPGACQ_RECV_CHK_INTV     60   // interval between channel updates while no reception
#define EPGACQ_TIMEOUT_RECV     180   // max. interval without reception (any blocks)

#define EPGACQ_STREAM1_UPD_INTV    (1*60*60)
#define EPGACQ_STREAM2_UPD_INTV   (14*60*60)

// number of seconds without AI after which acq is considered "stalled"
#define EPGACQ_DESCR_STALLED_TIMEOUT  30

#define MIN_CYCLE_QUOTE     0.33
#define MIN_CYCLE_VARIANCE  0.25
#define MAX_CYCLE_VAR_DIFF  0.01

#define MAX_CYCLE_ACQ_REP_COUNT   1.1

#define NOWNEXT_TIMEOUT_AI_COUNT  5
#define NOWNEXT_TIMEOUT          (5*60)
#define STREAM1_TIMEOUT         (12*60)
#define STREAM2_TIMEOUT         (35*60)

// ---------------------------------------------------------------------------
// Reset statistic values
// - should be called right after start/reset acq and once after first AI
//
static void EpgAcqNxtv_StatisticsReset( void )
{
   memset(&acqCtl.acqStats, 0, sizeof(acqCtl.acqStats));

   acqCtl.acqStats.acqStartTime  = time(NULL);
   acqCtl.acqStats.histIdx       = STATS_HIST_WIDTH - 1;

   // reset acquisition repetition counters for all PI
   if (acqCtl.pAcqDbContext != NULL)
   {
      EpgDbResetAcqRepCounters(acqCtl.pAcqDbContext);
   }

   acqCtl.acqStatsUpdate = TRUE;
}

// ---------------------------------------------------------------------------
// Maintain statistic values for AcqStat window
// - should be called after each received AI block
// - the history is also maintained when no stats window is open, because
//   once one is opened, a complete history can be displayed
//
static void EpgAcqNxtv_StatisticsUpdate( void )
{
   time_t acqMinTime[2], aiDiff;
   time_t aiAcqTimestamp;
   time_t now = time(NULL);
   EPGDB_HIST * pHist;
   uint total, obsolete;
   uint stream, idx;

   acqCtl.acqStatsUpdate = FALSE;

   EpgDbGetNowCycleMaxRepCounter(acqCtl.pAcqDbContext, &acqCtl.acqStats.nowMaxAcqRepCount,
                                                       &acqCtl.acqStats.nowMaxAcqNetCount);

   // determine min. acq time for PI blocks to count as "up to date" in the GetStat function
   if (acqCtl.cyclePhase == ACQMODE_PHASE_MONITOR)
   {  // in the final phase, e.g. stream 1 should be updated after max. 60 mins
      acqMinTime[0] = now - EPGACQ_STREAM1_UPD_INTV;
      acqMinTime[1] = now - EPGACQ_STREAM2_UPD_INTV;
   }
   else
   {  // initially disregard acq time (only the PI version is evaluated)
      memset(acqMinTime, 0, sizeof(acqMinTime));
   }

   // determine block counts in current database
   EpgDbGetStat(acqCtl.pAcqDbContext, acqCtl.acqStats.count, acqMinTime, acqCtl.acqStats.nowMaxAcqRepCount);

   if (acqCtl.state == ACQSTATE_RUNNING)
   {
      // compute minimum and maximum distance between AI blocks (in seconds)
      aiAcqTimestamp = EpgDbGetAiUpdateTime(acqCtl.pAcqDbContext);
      if ((acqCtl.acqStats.ai.lastAiTime != 0) && (aiAcqTimestamp > acqCtl.acqStats.ai.lastAiTime))
      {
         aiDiff = aiAcqTimestamp - acqCtl.acqStats.ai.lastAiTime;
         if ((aiDiff < acqCtl.acqStats.ai.minAiDistance) || (acqCtl.acqStats.ai.minAiDistance == 0))
            acqCtl.acqStats.ai.minAiDistance = aiDiff;
         if (aiDiff > acqCtl.acqStats.ai.maxAiDistance)
            acqCtl.acqStats.ai.maxAiDistance = aiDiff;
         acqCtl.acqStats.ai.sumAiDistance += aiDiff;
      }
      acqCtl.acqStats.ai.lastAiTime = EpgDbGetAiUpdateTime(acqCtl.pAcqDbContext);
      acqCtl.acqStats.ai.aiCount += 1;

      // maintain history of block counts per stream
      total = acqCtl.acqStats.count[0].ai + acqCtl.acqStats.count[1].ai;
      obsolete = acqCtl.acqStats.count[0].expired + acqCtl.acqStats.count[0].defective +
                 acqCtl.acqStats.count[1].expired + acqCtl.acqStats.count[1].defective;
      dprintf4("EpgAcqNxtv-StatisticsUpdate: AI #%d, db filled %.2f%%, variance %1.2f/%1.2f\n", acqCtl.acqStats.ai.aiCount, (double)(acqCtl.acqStats.count[0].allVersions + obsolete + acqCtl.acqStats.count[1].allVersions) / total * 100.0, acqCtl.acqStats.count[0].variance, acqCtl.acqStats.count[1].variance);

      acqCtl.acqStats.histIdx = (acqCtl.acqStats.histIdx + 1) % STATS_HIST_WIDTH;
      pHist = &acqCtl.acqStats.hist[acqCtl.acqStats.histIdx];

      if (total > 0)
      {
         pHist->expir = (uchar)((double)obsolete / total * 128.0);
         pHist->s1cur = (uchar)((double)(acqCtl.acqStats.count[0].curVersion + obsolete) / total * 128.0);
         pHist->s1old = (uchar)((double)(acqCtl.acqStats.count[0].allVersions + obsolete) / total * 128.0);
         pHist->s2cur = (uchar)((double)(acqCtl.acqStats.count[0].allVersions + obsolete + acqCtl.acqStats.count[1].curVersion) / total * 128.0);
         pHist->s2old = (uchar)((double)(acqCtl.acqStats.count[0].allVersions + obsolete + acqCtl.acqStats.count[1].allVersions) / total * 128.0);
      }
      else
         memset(pHist, 0, sizeof(*pHist));

      for (stream=0; stream <= 1; stream++)
      {
         if (acqCtl.acqStats.varianceHist[stream].count < VARIANCE_HIST_COUNT)
         {  // history buffer not yet filled -> just append the new sample
            idx = acqCtl.acqStats.varianceHist[stream].count;
            acqCtl.acqStats.varianceHist[stream].count += 1;
         }
         else
         {  // history buffer filled -> insert into ring buffer after the last written sample
            acqCtl.acqStats.varianceHist[stream].lastIdx = (acqCtl.acqStats.varianceHist[stream].lastIdx + 1) % VARIANCE_HIST_COUNT;
            idx = acqCtl.acqStats.varianceHist[stream].lastIdx;
         }

         acqCtl.acqStats.varianceHist[stream].buf[idx] = acqCtl.acqStats.count[stream].variance;
      }
   }
}

// ---------------------------------------------------------------------------
// Determine if netwop coverage variance is stable enough
//
static bool EpgAcqNxtv_CheckVarianceStable( uint streamIdx )
{
   EPGDB_VAR_HIST  * pHist;
   double min, max;
   uint idx;
   bool result = FALSE;

   if (streamIdx < 2)
   {
      pHist = acqCtl.acqStats.varianceHist + streamIdx;
      if (pHist->count >= VARIANCE_HIST_COUNT)
      {
         // get min and max variance from the history list
         min = max = pHist->buf[0];
         for (idx=1; idx < VARIANCE_HIST_COUNT; idx++)
         {
            if (pHist->buf[idx] < min)
               min = pHist->buf[idx];
            if (pHist->buf[idx] > max)
               max = pHist->buf[idx];
         }
         // variance slope has to be below threshold
         result = ((max - min) <= MAX_CYCLE_VAR_DIFF);
      }
   }
   else
      fatal1("EpgAcqNxtv-CheckVarianceStable: invalid stream index %d", streamIdx);

   return result;
}

// ---------------------------------------------------------------------------
// Determine if the current phase is complete
// - phase "NOW" is complete if the first programme of each channel has been 
//   captured; the other phases are done when all blocks in the respective
//   stream have been captured (ideally)
// - Important: all criteria must have a decent timeout to catch abnormal cases
//
static bool EpgAcqNxtv_AdvanceCyclePhase( void )
{
   time_t now = time(NULL);
   double quote;
   bool advance = FALSE;

   if (acqCtl.state == ACQSTATE_RUNNING)
   {
      switch (acqCtl.cyclePhase)
      {
         case ACQMODE_PHASE_NOWNEXT:
            advance = (acqCtl.acqStats.nowMaxAcqRepCount >= 2) ||
                      ((acqCtl.acqStats.nowMaxAcqRepCount == 0) && (acqCtl.acqStats.ai.aiCount >= NOWNEXT_TIMEOUT_AI_COUNT));
            advance |= (now >= acqCtl.acqStats.acqStartTime + NOWNEXT_TIMEOUT);
            break;

         case ACQMODE_PHASE_STREAM1:
            quote   = ((acqCtl.acqStats.count[0].ai > 0) ? ((double)acqCtl.acqStats.count[0].sinceAcq / acqCtl.acqStats.count[0].ai) : 100.0);
            advance = (quote >= MIN_CYCLE_QUOTE) &&
                      (acqCtl.acqStats.count[0].variance < MIN_CYCLE_VARIANCE) &&
                      (EpgAcqNxtv_CheckVarianceStable(0) || (acqCtl.acqStats.count[0].variance == 0.0));
            advance |= ((acqCtl.acqStats.count[0].avgAcqRepCount >= MAX_CYCLE_ACQ_REP_COUNT) ||
                        (acqCtl.acqStats.count[1].avgAcqRepCount >= MAX_CYCLE_ACQ_REP_COUNT));
            advance |= (now >= acqCtl.acqStats.acqStartTime + STREAM1_TIMEOUT);
            break;

         case ACQMODE_PHASE_STREAM2:
         case ACQMODE_PHASE_MONITOR:
            quote   = (((acqCtl.acqStats.count[0].ai + acqCtl.acqStats.count[1].ai) > 0) ?
                       (((double)acqCtl.acqStats.count[0].sinceAcq + acqCtl.acqStats.count[1].sinceAcq) /
                        ((double)acqCtl.acqStats.count[0].ai + acqCtl.acqStats.count[1].ai)) : 100.0);
            advance = (quote >= MIN_CYCLE_QUOTE) &&
                      (acqCtl.acqStats.count[0].variance < MIN_CYCLE_VARIANCE) &&
                      (acqCtl.acqStats.count[1].variance < MIN_CYCLE_VARIANCE) &&
                      EpgAcqNxtv_CheckVarianceStable(0) &&
                      EpgAcqNxtv_CheckVarianceStable(1);
            advance |= (acqCtl.acqStats.count[1].avgAcqRepCount >= MAX_CYCLE_ACQ_REP_COUNT);
            advance |= (now >= acqCtl.acqStats.acqStartTime + STREAM2_TIMEOUT);
            break;

         default:
            SHOULD_NOT_BE_REACHED;
            advance = TRUE;
            break;
      }
   }

   return advance;
}

// ---------------------------------------------------------------------------
// Determine state of acquisition for user information
// - note: only fills Nextview-specific parts of the struct
//
void EpgAcqNxtv_DescribeAcqState( EPGACQ_DESCR * pAcqState )
{
   time_t lastAi, now;

   if (acqCtl.state != ACQSTATE_OFF)
   {
      now = time(NULL);
      lastAi = EpgDbGetAiUpdateTime(acqCtl.pAcqDbContext);
      if (lastAi < acqCtl.acqStats.acqStartTime)
         lastAi = acqCtl.acqStats.acqStartTime;

      // check reception
      if (acqCtl.state != ACQSTATE_RUNNING)
      {
         if (now - lastAi > EPGACQ_DESCR_STALLED_TIMEOUT)
         {
            if (acqCtl.state >= ACQSTATE_WAIT_AI)
               pAcqState->nxtvState = ACQDESCR_DEC_ERRORS;
            else
               pAcqState->nxtvState = ACQDESCR_NO_RECEPTION;
         }
         else
            pAcqState->nxtvState = ACQDESCR_STARTING;
      }
      else
      {
         if (now - lastAi > EPGACQ_DESCR_STALLED_TIMEOUT)
            pAcqState->nxtvState = ACQDESCR_STALLED;
         else
            pAcqState->nxtvState = ACQDESCR_RUNNING;
      }

      pAcqState->nxtvDbCni = EpgDbContextGetCni(acqCtl.pAcqDbContext);
   }
   else
      pAcqState->nxtvState = ACQDESCR_DISABLED;
}

// ---------------------------------------------------------------------------
// Return complete set of acq state and statistic values
// - used by "View acq statistics" popup window
//
bool EpgAcqNxtv_GetAcqStats( EPG_NXTV_ACQ_STATS * pNxtvAcqStats )
{
   bool result = FALSE;

   if (acqCtl.state != ACQSTATE_OFF)
   {
      memcpy(pNxtvAcqStats, &acqCtl.acqStats, sizeof(*pNxtvAcqStats));

      EpgStreamGetStatistics(&pNxtvAcqStats->stream);
      result = TRUE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Get acquisition database context
//
EPGDB_CONTEXT * EpgAcqNxtv_GetDbContext( void )
{
   return acqCtl.pAcqDbContext;
}

// ---------------------------------------------------------------------------
// Get Pointer to PI timescale queue
// - used by the GUI to retrieve info from the queue (and thereby emptying it)
// - returns NULL if acq is off (queue might not be initialized yet)
//
EPGDB_PI_TSC * EpgAcqNxtv_GetTimescaleQueue( void )
{
   if (acqCtl.state != ACQSTATE_OFF)
      return &acqCtl.acqTscQueue;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// En-/Disable sending of PI timescale information
//
void EpgAcqNxtv_EnableTimescales( bool enable )
{
   if (enable == FALSE)
   {
      EpgTscQueue_Clear(&acqCtl.acqTscQueue);
   }
   acqCtl.acqTscEnabled = enable;
}

// ---------------------------------------------------------------------------
// Start teletext decoding and initialize the EPG stream decoder
//
static void EpgAcqNxtv_TtxStart( EPGDB_CONTEXT *dbc, EPGDB_QUEUE * pQueue, uint pageNo, uint appId )
{
   uint epgPageNo;
   uint epgAppId;
   bool bWaitForBiAi;

   if (VALID_EPG_PAGENO(pageNo))
      epgPageNo = pageNo;
   else if (VALID_EPG_PAGENO(dbc->pageNo))
      epgPageNo = dbc->pageNo;
   else
      epgPageNo = EPG_DEFAULT_PAGENO;

   if (appId != EPG_ILLEGAL_APPID)
      epgAppId = appId;
   else if (dbc->appId != EPG_ILLEGAL_APPID)
      epgAppId = dbc->appId;
   else
      epgAppId = EPG_DEFAULT_APPID;

   if (dbc->pAiBlock != NULL)
   {  // provider already known
      // set up a list of alphabets for string decoding
      EpgBlockSetAlphabets(&dbc->pAiBlock->blk.ai);
      // since alphabets are known PI can be collected right from the start
      bWaitForBiAi = FALSE;
   }
   else
   {  // unknown provider -> wait for AI before allowing PI
      bWaitForBiAi = TRUE;
   }

   // initialize the state of the streams decoder
   EpgStreamInit(pQueue, bWaitForBiAi, epgAppId, epgPageNo);

   TtxDecode_StartEpgAcq(epgPageNo, FALSE);

   acqCtl.currentPageNo = epgPageNo;
}

// ---------------------------------------------------------------------------
// Stop teletext decoding and clear the EPG stream and its queue
//
static void EpgAcqNxtv_TtxStop( void )
{
   TtxDecode_StopEpgAcq();
   EpgStreamClear();
}

// ---------------------------------------------------------------------------
// Stop and Re-Start teletext acquisition
// - called after change of channel or internal parameters
//
static void EpgAcqNxtv_TtxReset( EPGDB_CONTEXT *dbc, EPGDB_QUEUE * pQueue, uint pageNo, uint appId )
{
   // discard remaining blocks in the scratch buffer
   EpgStreamClear();

   EpgAcqNxtv_TtxStart(dbc, pQueue, pageNo, appId);
}

// ---------------------------------------------------------------------------
// Close the acq database
// - automatically chooses the right close function for the acq database
//   depending on how it was opened, i.e. as peek or fully
//
static void EpgAcqNxtv_CloseDb( void )
{
   if (acqCtl.pAcqDbContext != NULL)
   {
      EpgContextCtl_Close(acqCtl.pAcqDbContext);
      acqCtl.pAcqDbContext = NULL;
   }
   else
      fatal0("EpgAcqNxtv-CloseDb: db not open");
}

// ---------------------------------------------------------------------------
// Switch provider database to a new provider
// - called by AI callback upon provider change
//   and called upon GUI provider list update
//
static void EpgAcqNxtv_SwitchDb( uint acqCni )
{
   EpgAcqNxtv_CloseDb();
   acqCtl.pAcqDbContext = EpgContextCtl_Open(acqCni, FALSE, CTX_FAIL_RET_CREATE, CTX_RELOAD_ERR_ANY);
}

// ---------------------------------------------------------------------------
// Start the acquisition
//
bool EpgAcqNxtv_Start( EPGACQ_MODE mode, EPGACQ_PHASE cyclePhase, uint cni )
{
   bool result = FALSE;

   assert(acqCtl.mode != ACQMODE_NETWORK);
   dprintf3("EpgAcqNxtv-Start: starting acquisition for prov 0x%04X phase:%d mode:%d\n", cni, cyclePhase, mode);

   acqCtl.mode       = mode;
   acqCtl.cyclePhase = cyclePhase;
   acqCtl.reqCni     = cni;

   if (acqCtl.state == ACQSTATE_OFF)
   {
      assert(acqCtl.pAcqDbContext == NULL);

      // initialize teletext decoder
      EpgDbQueue_Init(&acqCtl.acqDbQueue);
      EpgTscQueue_Init(&acqCtl.acqTscQueue);

      acqCtl.state = ACQSTATE_WAIT_BI;
      acqCtl.dumpTime = acqCtl.acqStats.acqStartTime;
      acqCtl.chanChangeTime = 0;

      // set input source and tuner frequency (also detect if device is busy)
      EpgAcqNxtv_UpdateProvider(FALSE);
      EpgAcqNxtv_TtxStart(acqCtl.pAcqDbContext, &acqCtl.acqDbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);

      EpgAcqNxtv_StatisticsReset();
      EpgAcqNxtv_StatisticsUpdate();

      result = TRUE;
   }
   else
   {
      result = EpgAcqNxtv_UpdateProvider(TRUE);
   }

   return result;
}

// ---------------------------------------------------------------------------
// Stop the acquisition
// - the acq-sub process is terminated and the VBI device is freed
// - this allows other processes (e.g. a teletext decoder) to access VBI
//
void EpgAcqNxtv_Stop( void )
{
   if (acqCtl.state != ACQSTATE_OFF)
   {
      dprintf0("EpgAcqNxtv-Stop: stopping acquisition\n");
      assert(acqCtl.pAcqDbContext != NULL);

      EpgAcqNxtv_TtxStop();
      EpgTscQueue_Clear(&acqCtl.acqTscQueue);

      EpgAcqNxtv_CloseDb();

      acqCtl.state = ACQSTATE_OFF;
   }
}

// ---------------------------------------------------------------------------
// Stop acquisition for the EPG scan but keep the device open/driver loaded
//
void EpgAcqNxtv_Suspend( void )
{
   EpgAcqNxtv_CloseDb();
   EpgStreamClear();
   EpgTscQueue_Clear(&acqCtl.acqTscQueue);

   acqCtl.state = ACQSTATE_OFF;
}

// ---------------------------------------------------------------------------
// Set input source and tuner frequency for a provider
// - errors are reported to the user interface
//
static bool EpgAcqNxtv_TuneProvider( uint freq, uint cni, bool * pInputChanged )
{
   bool result;

   // remember time of channel change
   acqCtl.chanChangeTime = time(NULL);

   result = EpgAcqCtl_TuneProvider(FALSE, freq, cni, &acqCtl.passiveReason);

   *pInputChanged = ( (acqCtl.passiveReason == ACQPASSIVE_NONE) ||
                      (acqCtl.passiveReason != ACQPASSIVE_ACCESS_DEVICE) );

   return result;
}

// ---------------------------------------------------------------------------
// Switch database and TV channel to the current acq provider & handle errors
// - called upon start of acquisition or configuration changes (db or acq mode)
// - called after end of a cycle phase for one db
//
static bool EpgAcqNxtv_UpdateProvider( bool changeDb )
{
   EPGDB_CONTEXT * pPeek;
   uint freq = 0;
   uint cni;
   bool warnInputError;
   bool dbChanged, inputChanged, dbInitial;
   bool result = TRUE;

   dbChanged = inputChanged = dbInitial = FALSE;

   // determine current CNI depending on acq mode and index
   cni = acqCtl.reqCni;

   // for acq start: open the requested db (or empty database for passive mode)
   if (acqCtl.pAcqDbContext == NULL)
   {
      dbInitial = TRUE;
      if (cni == 0)
         acqCtl.pAcqDbContext = EpgContextCtl_OpenDummy();
      else
         acqCtl.pAcqDbContext = EpgContextCtl_Open(cni, FALSE, CTX_FAIL_RET_DUMMY, CTX_RELOAD_ERR_NONE);
   }

   if (cni != 0)
   {
      if ( (changeDb == FALSE) &&
           (acqCtl.state == ACQSTATE_WAIT_BI) && (acqCtl.pAcqDbContext->modified == FALSE))
      {
         changeDb = TRUE;
      }
      warnInputError = FALSE;

      if (cni == EpgDbContextGetCni(acqCtl.pAcqDbContext))
      {
         freq = UiControlMsg_QueryProvFreq(cni);
         if (freq == 0)
            freq = acqCtl.pAcqDbContext->tunerFreq;
         warnInputError = TRUE;
      }
      else
      {
         // query the rc/ini file for a frequency for this provider
         freq = UiControlMsg_QueryProvFreq(cni);

         if ( changeDb )
         {  // switch to the new database
            EpgAcqNxtv_CloseDb();
            acqCtl.pAcqDbContext = EpgContextCtl_Open(cni, FALSE, CTX_FAIL_RET_DUMMY, ((freq == 0) ? CTX_RELOAD_ERR_ACQ : CTX_RELOAD_ERR_ANY));
            dbChanged = TRUE;
            if (EpgDbContextGetCni(acqCtl.pAcqDbContext) == cni)
               warnInputError = TRUE;
            if (freq == 0)
               freq = acqCtl.pAcqDbContext->tunerFreq;
            acqCtl.dumpTime = time(NULL);
         }
         else if (freq == 0)
         {  // query the database file for a frequency for this provider
            pPeek = EpgContextCtl_Peek(cni, CTX_RELOAD_ERR_ACQ);
            if (pPeek != NULL)
            {
               freq = pPeek->tunerFreq;
               EpgContextCtl_ClosePeek(pPeek);
               warnInputError = TRUE;
            }
            else
               debug1("EpgAcqNxtv-UpdateProvider: peek for 0x%04X failed", cni);
         }
      }

      if ( (acqCtl.mode != ACQMODE_PASSIVE) &&
           ((acqCtl.passiveReason == ACQPASSIVE_NONE) || (acqCtl.passiveReason != ACQPASSIVE_NO_TUNER)) )
      {
         result = EpgAcqNxtv_TuneProvider(freq, (warnInputError ? cni: 0), &inputChanged);
      }
   }
   else
   {  // CNI 0
      // close previous database
      if (changeDb && (EpgDbContextGetCni(acqCtl.pAcqDbContext) != 0))
      {
         EpgAcqNxtv_CloseDb();
         acqCtl.pAcqDbContext = EpgContextCtl_OpenDummy();
         dbChanged = TRUE;
      }
      // no provider to be tuned onto -> set at least the input source
      if (acqCtl.mode != ACQMODE_PASSIVE)
      {
         result = EpgAcqNxtv_TuneProvider(0, 0, &inputChanged);
      }
   }

   if ((dbChanged || inputChanged) && !dbInitial)
   {  // acq database and/or input channel/frequency have been changed
      // -> it's MANDATORY to reset all acq state machines or data from
      //    a wrong provider might be added to the database
      if (acqCtl.state != ACQSTATE_OFF)
      {  // reset acquisition: check provider CNI of next AI block
         EpgAcqNxtv_ChannelChange(FALSE);
      }
      UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
   }

   #ifdef USE_DAEMON
   // inform server module about the current provider
   EpgAcqServer_SetProvider(EpgDbContextGetCni(acqCtl.pAcqDbContext));
   #endif

   return result;
}

// ---------------------------------------------------------------------------
// AI callback: invoked before a new AI block is inserted into the database
//
static bool EpgAcqNxtv_AiCallback( const AI_BLOCK *pNewAi )
{
   EPGACQ_STATE   oldState;
   const AI_BLOCK *pOldAi;
   uint ai_cni;
   bool accept = FALSE;

   assert(EpgScan_IsActive() == FALSE);  // EPG scan uses a separate callback
   oldState = acqCtl.state;

   if (AI_GET_THIS_NET_CNI(pNewAi) == 0)
   {
      debug0("EpgAcqNxtv-AiCallback: AI with illegal CNI 0 rejected");
      accept = FALSE;
   }
   else if (acqCtl.state == ACQSTATE_WAIT_BI)
   {
      dprintf2("EpgCtl: AI rejected in state WAIT-BI: CNI=0x%04X db-CNI=0x%04X\n", AI_GET_THIS_NET_CNI(pNewAi), ((acqCtl.pAcqDbContext->pAiBlock != NULL) ? AI_GET_THIS_NET_CNI(&acqCtl.pAcqDbContext->pAiBlock->blk.ai) : 0));
      accept = FALSE;
   }
   else if ((acqCtl.state == ACQSTATE_WAIT_AI) || (acqCtl.state == ACQSTATE_RUNNING))
   {
      DBGONLY(if (acqCtl.state == ACQSTATE_WAIT_AI))
         dprintf3("EpgCtl: AI found, CNI=0x%04X version %d/%d\n", AI_GET_THIS_NET_CNI(pNewAi), pNewAi->version, pNewAi->version_swo);
      EpgDbLockDatabase(acqCtl.pAcqDbContext, TRUE);
      pOldAi = EpgDbGetAi(acqCtl.pAcqDbContext);
      ai_cni = AI_GET_THIS_NET_CNI(pNewAi);

      if (pOldAi == NULL)
      {  // the current db is empty
         EpgDbLockDatabase(acqCtl.pAcqDbContext, FALSE);

         // this is just like a provider change...

         EpgAcqNxtv_SwitchDb(ai_cni);
         dprintf2("EpgAcqCtl: empty acq db, AI found: CNI 0x%04X (%s)\n", ai_cni, ((EpgDbContextGetCni(acqCtl.pAcqDbContext) == 0) ? "new" : "reload ok"));

         #ifdef USE_DAEMON
         // inform server module about the current provider
         EpgAcqServer_SetProvider(ai_cni);
         #endif

         // store the tuner frequency if none is known yet
         if (acqCtl.pAcqDbContext->tunerFreq == 0)
         {
            uint  freq, chan;
            bool  isTuner;
            // try to obtain the frequency from the driver (not supported by all drivers)
            if ( BtDriver_QueryChannel(&freq, &chan, &isTuner) && (isTuner) && (freq != 0) )
            {  // store the provider channel frequency in the rc/ini file
               debug2("EpgAcqCtl: setting current tuner frequency %.2f for CNI 0x%04X", (double)(freq & 0xffffff)/16, ai_cni);
               acqCtl.pAcqDbContext->tunerFreq = freq;
               UiControlMsg_NewProvFreq(ai_cni, freq);
            }
         }

         // new provider -> dump asap
         if (EpgDbContextGetCni(acqCtl.pAcqDbContext) == 0)
            acqCtl.dumpTime = 0;

         acqCtl.state = ACQSTATE_RUNNING;
         acqCtl.acqStatsUpdate = TRUE;

         // update the ui netwop list and acq stats output if neccessary
         UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);

         accept = TRUE;
      }
      else
      {  // already an AI in the database

         if (acqCtl.pAcqDbContext->provCni == AI_GET_THIS_NET_CNI(pNewAi))
         {
            if ( (pOldAi->version != pNewAi->version) ||
                 (pOldAi->version_swo != pNewAi->version_swo) )
            {
               dprintf2("EpgAcqCtl: version number has changed, was: %d/%d\n", pOldAi->version, pOldAi->version_swo);
               UiControlMsg_AcqEvent(ACQ_EVENT_AI_VERSION_CHANGE);
               #ifdef USE_DAEMON
               // inform server module about the current provider
               EpgAcqServer_SetProvider(AI_GET_THIS_NET_CNI(pNewAi));
               #endif
            }
            else
            {  // same AI version
               if (EpgDbComparePiRanges(acqCtl.pAcqDbContext, pOldAi, pNewAi))
               {
                  // new PI added at the end -> redraw timescales to mark ranges with missing PI
                  UiControlMsg_AcqEvent(ACQ_EVENT_AI_PI_RANGE_CHANGE);
                  #ifdef USE_DAEMON
                  // inform server module about the current provider
                  EpgAcqServer_SetProvider(AI_GET_THIS_NET_CNI(pNewAi));
                  #endif
               }
               else
                  UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
            }
            EpgDbLockDatabase(acqCtl.pAcqDbContext, FALSE);

            acqCtl.acqStatsUpdate = TRUE;
            acqCtl.state = ACQSTATE_RUNNING;
            accept = TRUE;
         }
         else
         {  // wrong provider -> switch acq database
            dprintf2("EpgAcqCtl: switching acq db from %04X to %04X\n", acqCtl.pAcqDbContext->provCni, ai_cni);
            EpgDbLockDatabase(acqCtl.pAcqDbContext, FALSE);

            EpgAcqNxtv_SwitchDb(ai_cni);

            if (acqCtl.state == ACQSTATE_RUNNING)
            {  // should normally not happen, because channel changes are detected by TTX page header supervision
               debug1("EpgAcqCtl: unexpected prov change to CNI %04X - resetting EPG Acq", ai_cni);
               EpgAcqNxtv_TtxReset(acqCtl.pAcqDbContext, &acqCtl.acqDbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);

               // if db is new, dump asap
               if (EpgDbContextGetCni(acqCtl.pAcqDbContext) == 0)
                  acqCtl.dumpTime = 0;
               else
                  acqCtl.dumpTime = time(NULL);

               acqCtl.state = ACQSTATE_WAIT_BI;
            }
            else
               acqCtl.state = ACQSTATE_RUNNING;

            #ifdef USE_DAEMON
            // inform server module about the current provider
            EpgAcqServer_SetProvider(ai_cni);
            #endif

            if (acqCtl.state != ACQSTATE_RUNNING)
               EpgAcqNxtv_StatisticsReset();
            else
               acqCtl.acqStatsUpdate = TRUE;
            UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);

            accept = TRUE;
         }

         // Store the TV tuner frequency if none is known yet
         // (Note: the tuner frequency in the db is never overwritten because we can
         // not be 100% sure that we know the current TV tuner freq here, esp. if it's
         // coming from a remote TV application; only the EPG scan updates the freq.)
         if ( (acqCtl.pAcqDbContext->tunerFreq == 0) &&
              (oldState == ACQSTATE_WAIT_AI) && (accept) &&
              (acqCtl.state == ACQSTATE_RUNNING) )
         {
            uint  freq, chan;
            bool  isTuner;
            // try to obtain the frequency from the driver (not supported by all drivers)
            if ( BtDriver_QueryChannel(&freq, &chan, &isTuner) && (isTuner) && (freq != 0) )
            {  // store the provider channel frequency in the rc/ini file
               debug2("EpgAcqCtl: setting current tuner frequency %.2f for CNI 0x%04X", (double)(freq & 0xffffff)/16, ai_cni);
               acqCtl.pAcqDbContext->tunerFreq = freq;
               UiControlMsg_NewProvFreq(ai_cni, freq);
            }
         }
      }
      // update teletext page number in the database if changed
      // note: store is delayed until AI reception to be sure the db matches the current stream
      if ( (acqCtl.currentPageNo != acqCtl.pAcqDbContext->pageNo) &&
           VALID_EPG_PAGENO(acqCtl.currentPageNo) )
      {
         acqCtl.pAcqDbContext->pageNo = acqCtl.currentPageNo;
      }
   }
   else
      assert(acqCtl.state != ACQSTATE_OFF);

   assert(EpgDbIsLocked(acqCtl.pAcqDbContext) == FALSE);
   return accept;
}

// ---------------------------------------------------------------------------
// Called by the database management when a new BI block was received
// - the BI block is never inserted into the database
// - only the application ID is extracted and saved in the db context
//
static bool EpgAcqNxtv_BiCallback( const BI_BLOCK *pNewBi )
{
   assert(EpgScan_IsActive() == FALSE);  // EPG scan uses a separate callback

   if (pNewBi->app_id == EPG_ILLEGAL_APPID)
   {
      dprintf0("EpgCtl-BiCallback: EPG not listed in BI\n");
   }
   else
   {
      if (acqCtl.pAcqDbContext->appId != EPG_ILLEGAL_APPID)
      {
         if (acqCtl.pAcqDbContext->appId != pNewBi->app_id)
         {  // not the default id
            dprintf2("EpgCtl: app-ID changed from %d to %d\n", acqCtl.pAcqDbContext->appId, acqCtl.pAcqDbContext->appId);
            EpgStreamClear();
            EpgStreamInit(&acqCtl.acqDbQueue, TRUE, pNewBi->app_id, acqCtl.pAcqDbContext->pageNo);
         }
      }
      else
         dprintf1("EpgAcqNxtv-BiCallback: BI received, appID=%d\n", pNewBi->app_id);

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
            acqCtl.pAcqDbContext->appId = pNewBi->app_id;
            break;

         default:
            SHOULD_NOT_BE_REACHED;
            break;
      }
   }

   // the BI block is never added to the db
   return FALSE;
}

// ---------------------------------------------------------------------------
// Handle channel changes
// - called both when the TTX decoder detects an external channel change
//   and when a channel is tuned internally.
// - changeDb: TRUE if we're pretty sure the channel has changed, close the
//   old DB until the new provider is detected (this avoids entering "acq.
//   stalled" state)
//
void EpgAcqNxtv_ChannelChange( bool changeDb )
{
   if (acqCtl.state != ACQSTATE_OFF)
   {
      dprintf3("EpgAcqNxtv-ChannelChange: reset acq for db 0x%04X, modified=%d, change-DB=%d\n", acqCtl.pAcqDbContext->provCni, acqCtl.pAcqDbContext->modified, changeDb);
      acqCtl.chanChangeTime = time(NULL);

      if ( changeDb )
      {
         EpgAcqNxtv_CloseDb();
         acqCtl.pAcqDbContext = EpgContextCtl_OpenDummy();

         // notify GUI about state change
         UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);

#ifdef USE_DAEMON
         // inform server module about the current provider
         EpgAcqServer_SetProvider(0);
#endif
         EpgAcqNxtv_StatisticsReset();
         EpgAcqNxtv_StatisticsUpdate();
      }

      EpgAcqNxtv_TtxReset(acqCtl.pAcqDbContext, &acqCtl.acqDbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
      acqCtl.state = ACQSTATE_WAIT_BI;
   }
}

// ---------------------------------------------------------------------------
// Periodic check if the acquisition process is still alive
// - maintains timers to check for acq. stall or restart while forced passive
//   note: done here, because "process blocks" isn't called without reception
// - called after processing the teletext buffer (i.e. once a second)
//
bool EpgAcqNxtv_MonitorSource( void )
{
   time_t now = time(NULL);
   bool advance = FALSE;

   assert(acqCtl.state != ACQSTATE_OFF);
   assert(acqCtl.acqStatsUpdate == FALSE);

   // Check if the correct channel is tuned (i.e. if current CNI does not match requested CNI)
   if ( (acqCtl.reqCni != 0) && (acqCtl.reqCni != EpgDbContextGetCni(acqCtl.pAcqDbContext)) &&
        (acqCtl.mode != ACQMODE_PASSIVE) && (acqCtl.mode != ACQMODE_EXTERNAL) )
   {
      // not the expected provider -> consider channel switch
      if ( (now >= acqCtl.chanChangeTime + EPGACQ_CHN_FIX_INTV) &&
           ((acqCtl.passiveReason == ACQPASSIVE_NONE) || (acqCtl.passiveReason == ACQPASSIVE_ACCESS_DEVICE)) )
      {
         EpgAcqNxtv_UpdateProvider(FALSE);
      }
      // set flag to suppress advance to next provider (only valid until next "process blocks" call)
      // (i.e. prevent advancing in cycle based on a false provider's database statistics)
      acqCtl.chanChangePend = TRUE;
   }

   // check if acq stalled (no AI or no new PI) -> reset acq
   // but make sure the channel isn't changed too often (need to wait at least 10 secs for AI)
   if ( (now >= acqCtl.dumpTime + EPGACQ_RECV_CHK_INTV) &&
        (now >= acqCtl.acqStats.ai.lastAiTime + EPGACQ_RECV_CHK_INTV) &&
        (now >= acqCtl.chanChangeTime + EPGACQ_RECV_CHK_INTV) )
   {
      dprintf3("EpgAcqNxtv-MonitorSource: no reception from provider 0x%04X (%d,%d since dump,chan) - reset acq\n", EpgDbContextGetCni(acqCtl.pAcqDbContext), (int)(now - acqCtl.dumpTime), (int)(now - acqCtl.chanChangeTime));

      if (now >= acqCtl.acqStats.acqStartTime + EPGACQ_TIMEOUT_RECV)
      {
         advance = TRUE;
         acqCtl.chanChangePend = TRUE;
      }
      else
      {
         EpgAcqNxtv_UpdateProvider(FALSE);
         acqCtl.chanChangePend = TRUE;
      }
   }

   return advance;
}

// ---------------------------------------------------------------------------
// Process TTX packets in the VBI ringbuffer
// - called about once a second (most providers send one TTX page with EPG data
//   per second); assembled EPG blocks are queued
// - also handles error indications: acq stop, channel change, EPG param update
//
bool EpgAcqNxtv_ProcessPackets( bool * pCheckSlicer )
{
   uint pageNo;
   time_t now;

   assert(acqCtl.state != ACQSTATE_OFF);

   // assemble VBI lines, i.e. TTX packets to EPG blocks and put them in the queue
   if (EpgStreamProcessPackets() == FALSE)
   {  // channel change detected -> restart acq for the current provider
      dprintf0("EpgAcqCtl: uncontrolled channel change detected\n");
      EpgAcqNxtv_ChannelChange(TRUE);
   }

   // check if the current slicer type is adequate
   if (pCheckSlicer != NULL)
   {
      now = time(NULL);
      if ( (now >= acqCtl.chanChangeTime + EPGACQ_SLICER_INTV) &&
           (EpgStreamCheckSlicerQuality() == FALSE) )
      {
         *pCheckSlicer = FALSE;
      }
   }

   // check the MIP if EPG is transmitted on a different page
   pageNo = TtxDecode_GetMipPageNo();
   if ((pageNo != EPG_ILLEGAL_PAGENO) && (pageNo != acqCtl.pAcqDbContext->pageNo))
   {  // found a different page number in MIP
      dprintf2("EpgAcqCtl: non-default MIP page no for EPG: %03X (was %03X) -> restart acq\n", pageNo, acqCtl.pAcqDbContext->pageNo);

      EpgAcqNxtv_TtxReset(acqCtl.pAcqDbContext, &acqCtl.acqDbQueue, pageNo, EPG_ILLEGAL_APPID);
   }

   // return TRUE if blocks are in queue -> caller must schedule block insertion
   return TRUE;
}

// ---------------------------------------------------------------------------
// Insert newly acquired blocks into the EPG db
// - to be called when ProcessPackets returns TRUE, i.e. EPG blocks in input queue
// - database MUST NOT be locked by GUI
//
void EpgAcqNxtv_ProcessBlocks( bool * pAdvance )
{
   static const EPGDB_ADD_CB epgQueueCb = { EpgAcqNxtv_AiCallback, EpgAcqNxtv_BiCallback };
   time_t now = time(NULL);
   bool overflow;

   assert(acqCtl.state != ACQSTATE_OFF);
   assert(EpgDbIsLocked(acqCtl.pAcqDbContext) == FALSE);
   assert(acqCtl.acqStatsUpdate == FALSE);

   if ( (acqCtl.state == ACQSTATE_WAIT_BI) || (acqCtl.state == ACQSTATE_WAIT_AI) )
   {  // still waiting for the initial BI block
      EpgDbProcessQueueByType(&acqCtl.pAcqDbContext, &acqCtl.acqDbQueue, BLOCK_TYPE_BI, &epgQueueCb);
   }

   if (acqCtl.state == ACQSTATE_WAIT_AI)
   {  // BI received, but still waiting for the initial AI block
      EpgDbProcessQueueByType(&acqCtl.pAcqDbContext, &acqCtl.acqDbQueue, BLOCK_TYPE_AI, &epgQueueCb);
   }

   if (acqCtl.state == ACQSTATE_RUNNING)
   {  // add all queued blocks to the db
      overflow = (EpgDbQueue_GetBlockCount(&acqCtl.acqDbQueue) >= EPG_QUEUE_OVERFLOW_LEN);
      if (overflow)
         overflow = UiControlMsg_AcqQueueOverflow(TRUE);

      if (acqCtl.acqTscEnabled)
      {  // also add PI timescale info to the queue
         EpgDbProcessQueue(&acqCtl.pAcqDbContext, &acqCtl.acqDbQueue, &acqCtl.acqTscQueue, &epgQueueCb);
         if (EpgTscQueue_HasElems(&acqCtl.acqTscQueue))
            UiControlMsg_AcqEvent(ACQ_EVENT_PI_ADDED);
      }
      else
         EpgDbProcessQueue(&acqCtl.pAcqDbContext, &acqCtl.acqDbQueue, NULL, &epgQueueCb);

      if (overflow)
         UiControlMsg_AcqQueueOverflow(FALSE);
   }

   // determine if the current phase is complete (if needed, i.e. in case of multiple providers)
   // suppressed during exceptional states - in these cases advancing is based on timers only
   if ( (pAdvance != NULL) &&
        (acqCtl.chanChangePend == FALSE) &&
        (acqCtl.acqStatsUpdate || (now >= acqCtl.acqStats.ai.lastAiTime + EPGACQ_ADV_CHK_INTV) ))
   {
      *pAdvance = EpgAcqNxtv_AdvanceCyclePhase();

      if (*pAdvance) {dprintf2("EpgAcqNxtv-ProcessBlocks: stats:%d AI-timer:%d\n", acqCtl.acqStatsUpdate, (int)(now - acqCtl.acqStats.ai.lastAiTime));}
   }
   acqCtl.chanChangePend = FALSE;

   // if new AI received, update statistics (after AI was inserted into db)
   if (acqCtl.acqStatsUpdate)
   {
      EpgAcqNxtv_StatisticsUpdate();
   }
   else if (acqCtl.dumpTime == 0)
   {  // immediate dump for new providers
      EpgDbLockDatabase(acqCtl.pAcqDbContext, TRUE);
      if ( EpgDbDump(acqCtl.pAcqDbContext) )
      {
         dprintf1("EpgAcqNxtv-ProcessBlocks: initial dump of db %04X\n", EpgDbContextGetCni(acqCtl.pAcqDbContext));
         acqCtl.dumpTime = time(NULL);
      }
      EpgDbLockDatabase(acqCtl.pAcqDbContext, FALSE);
   }
   if ( (acqCtl.state == ACQSTATE_RUNNING) &&
        (acqCtl.pAcqDbContext->modified) &&
        (now >= acqCtl.dumpTime + EPGACQ_DB_DUMP_INTV) )
   {
      // periodic db dump
      EpgDbLockDatabase(acqCtl.pAcqDbContext, TRUE);
      if ( EpgDbDump(acqCtl.pAcqDbContext) )
      {
         dprintf1("EpgAcqNxtv-ProcessBlocks: dumped db %04X to file\n", EpgDbContextGetCni(acqCtl.pAcqDbContext));
      }
      // update timestamp (even upon failure to avoid retries in 1-second intervals)
      acqCtl.dumpTime = now;
      EpgDbLockDatabase(acqCtl.pAcqDbContext, FALSE);
   }
}

// ---------------------------------------------------------------------------
// Stop acquisition and free allocated resources
//
void EpgAcqNxtv_Destroy( void )
{
   EpgAcqNxtv_Stop();
}

// ---------------------------------------------------------------------------
// Initialize the module
//
void EpgAcqNxtv_Init( void )
{
   memset(&acqCtl, 0, sizeof(acqCtl));
   acqCtl.state     = ACQSTATE_OFF;
   acqCtl.mode      = ACQMODE_FOLLOW_UI;
}

