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
 *    This module implements the main control of the acquisition process.
 *    It contains the top-level start and stop acquisition functions
 *    which invoke functions in other acq related modules as needed.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgacqctl.c,v 1.85 2004/07/11 19:02:59 tom Exp tom $
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


// ---------------------------------------------------------------------------
// Declaration of module state variables

typedef struct
{
   EPGACQ_STATE   state;
   EPGACQ_MODE    mode;
   EPGACQ_MODE    userMode;
   EPGACQ_PASSIVE passiveReason;
   EPGACQ_PHASE   cyclePhase;
   EPGACQ_PHASE   stopPhase;
   uint           cycleIdx;
   uint           cniCount;
   uint           cniTab[MAX_MERGED_DB_COUNT];
   time_t         dumpTime;
   time_t         chanChangeTime;
   uint           inputSource;
   uint           currentSlicerType;
   bool           autoSlicerType;
   bool           haveWarnedInpSrc;
} EPGACQCTL_STATE;

static EPGACQCTL_STATE  acqCtl = {ACQSTATE_OFF, ACQMODE_FOLLOW_UI, ACQMODE_FOLLOW_UI};
static EPGDB_STATS      acqStats;
static bool             acqStatsUpdate;
static bool             acqVpsPdcUpdate[VPSPDC_REQ_COUNT];
static EPGACQ_DESCR     acqNetDescr;
static bool             acqContextIsPeek;
static EPGDB_QUEUE      acqDbQueue;
static EPGDB_PI_TSC     acqTsc;
static bool             acqTscEnabled = FALSE;

EPGDB_CONTEXT * pAcqDbContext = NULL;


static void EpgAcqCtl_InitCycle( void );
static bool EpgAcqCtl_UpdateProvider( bool changeDb );

// Interface for notifications from acquisition
static void EpgAcqCtl_ChannelChange( bool changeDb );
static bool EpgAcqCtl_AiCallback( const AI_BLOCK *pNewAi );
static bool EpgAcqCtl_BiCallback( const BI_BLOCK *pBi );

static const EPGDB_ADD_CB epgQueueCb =
{
   EpgAcqCtl_AiCallback,
   EpgAcqCtl_BiCallback,
};

// when very many new blocks are in the queue, connection to UI must be locked
#define EPG_QUEUE_OVERFLOW_LEN      250


// ---------------------------------------------------------------------------
// Reset statistic values
// - should be called right after start/reset acq and once after first AI
// - in network acq mode it resets the cached information from the remote acq process
//
static void EpgAcqCtl_StatisticsReset( void )
{
   memset(&acqStats, 0, sizeof(acqStats));

   memset(&acqNetDescr, 0, sizeof(acqNetDescr));
   memset(&acqVpsPdcUpdate, FALSE, sizeof(acqVpsPdcUpdate));

   acqStats.acqStartTime  = time(NULL);
   acqStats.histIdx       = STATS_HIST_WIDTH - 1;

   // reset acquisition repetition counters for all PI
   if (pAcqDbContext != NULL)
   {
      EpgDbResetAcqRepCounters(pAcqDbContext);
   }

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
   time_t acqMinTime[2], aiDiff;
   time_t aiAcqTimestamp;
   time_t now = time(NULL);
   EPGDB_HIST * pHist;
   uint total, obsolete;
   uint stream, idx;

   acqStatsUpdate = FALSE;

   acqStats.nowNextMaxAcqRepCount = EpgDbGetNowCycleMaxRepCounter(pAcqDbContext);

   // determine min. acq time for PI blocks to count as "up to date" in the GetStat function
   if (acqCtl.cyclePhase == ACQMODE_PHASE_MONITOR)
   {  // in the final phase, e.g. stream 1 should be updated after max. 60 mins
      acqMinTime[0] = now - EPGACQCTL_STREAM1_UPD_INTV;
      acqMinTime[1] = now - EPGACQCTL_STREAM2_UPD_INTV;
   }
   else
   {  // initially disregard acq time (only the PI version is evaluated)
      memset(acqMinTime, 0, sizeof(acqMinTime));
   }

   // determine block counts in current database
   EpgDbGetStat(pAcqDbContext, acqStats.count, acqMinTime, acqStats.nowNextMaxAcqRepCount);

   if (acqCtl.state == ACQSTATE_RUNNING)
   {
      // compute minimum and maximum distance between AI blocks (in seconds)
      aiAcqTimestamp = EpgDbGetAiUpdateTime(pAcqDbContext);
      if ((acqStats.ai.lastAiTime != 0) && (aiAcqTimestamp > acqStats.ai.lastAiTime))
      {
         aiDiff = aiAcqTimestamp - acqStats.ai.lastAiTime;
         if ((aiDiff < acqStats.ai.minAiDistance) || (acqStats.ai.minAiDistance == 0))
            acqStats.ai.minAiDistance = aiDiff;
         if (aiDiff > acqStats.ai.maxAiDistance)
            acqStats.ai.maxAiDistance = aiDiff;
         acqStats.ai.sumAiDistance += aiDiff;
      }
      acqStats.ai.lastAiTime = EpgDbGetAiUpdateTime(pAcqDbContext);
      acqStats.ai.aiCount += 1;

      // maintain history of block counts per stream
      total = acqStats.count[0].ai + acqStats.count[1].ai;
      obsolete = acqStats.count[0].expired + acqStats.count[0].defective +
                 acqStats.count[1].expired + acqStats.count[1].defective;
      dprintf4("EpgAcqCtl-StatisticsUpdate: AI #%d, db filled %.2f%%, variance %1.2f/%1.2f\n", acqStats.ai.aiCount, (double)(acqStats.count[0].allVersions + obsolete + acqStats.count[1].allVersions) / total * 100.0, acqStats.count[0].variance, acqStats.count[1].variance);

      acqStats.histIdx = (acqStats.histIdx + 1) % STATS_HIST_WIDTH;
      pHist = &acqStats.hist[acqStats.histIdx];

      if (total > 0)
      {
         pHist->expir = (uchar)((double)obsolete / total * 128.0);
         pHist->s1cur = (uchar)((double)(acqStats.count[0].curVersion + obsolete) / total * 128.0);
         pHist->s1old = (uchar)((double)(acqStats.count[0].allVersions + obsolete) / total * 128.0);
         pHist->s2cur = (uchar)((double)(acqStats.count[0].allVersions + obsolete + acqStats.count[1].curVersion) / total * 128.0);
         pHist->s2old = (uchar)((double)(acqStats.count[0].allVersions + obsolete + acqStats.count[1].allVersions) / total * 128.0);
      }
      else
         memset(pHist, 0, sizeof(*pHist));

      for (stream=0; stream <= 1; stream++)
      {
         if (acqStats.varianceHist[stream].count < VARIANCE_HIST_COUNT)
         {  // history buffer not yet filled -> just append the new sample
            idx = acqStats.varianceHist[stream].count;
            acqStats.varianceHist[stream].count += 1;
         }
         else
         {  // history buffer filled -> insert into ring buffer after the last written sample
            acqStats.varianceHist[stream].lastIdx = (acqStats.varianceHist[stream].lastIdx + 1) % VARIANCE_HIST_COUNT;
            idx = acqStats.varianceHist[stream].lastIdx;
         }

         acqStats.varianceHist[stream].buf[idx] = acqStats.count[stream].variance;
      }
   }
}

// ---------------------------------------------------------------------------
// Return database block counts: number blocks in AI, number expired, etc.
// - in network acq mode the info is forwarded by the acquisition daemon
//   (even if the database is fully open on client-side)
//
const EPGDB_BLOCK_COUNT * EpgAcqCtl_GetDbStats( void )
{
   const EPGDB_BLOCK_COUNT  * pDbStats;

   assert(acqStatsUpdate == FALSE);  // must not be called asynchronously

   if (acqCtl.state != ACQSTATE_OFF)
   {
      pDbStats = acqStats.count;
   }
   else
      pDbStats = NULL;

   return pDbStats;
}

// ---------------------------------------------------------------------------
// Return complete set of acq state and statistic values
// - used by "View acq statistics" popup window
// - in network acq mode it returns info about the remote acquisition process
//   requires the extended stats report mode to be enabled; else only the
//   acq state (next function) is available
//
const EPGDB_STATS * EpgAcqCtl_GetAcqStats( void )
{
   const EPGDB_STATS   * pAcqStats;

   if (acqCtl.state != ACQSTATE_OFF)
   {
      assert(acqStatsUpdate == FALSE);  // must not be called asynchronously

      if (acqCtl.mode != ACQMODE_NETWORK)
      {  // retrieve additional data from TTX packet decoder
         TtxDecode_GetStatistics(&acqStats.ttx.ttxPkgCount,
                                 &acqStats.ttx.epgPkgCount, &acqStats.ttx.epgPagCount);
         EpgStreamGetStatistics(&acqStats.stream);

         acqStats.lastStatsUpdate = time(NULL);
      }

      pAcqStats = &acqStats;
   }
   else
      pAcqStats = NULL;

   return pAcqStats;
}

// ---------------------------------------------------------------------------
// Return VPS CNI received on EPG input channel
// - used for EPG acq stats output; esp. useful in forced-passive mode, where
//   it allows to see what channel is tuned in instead of the acq provider's
// - in network acq mode the VPS CNI is forwarded by the remote acq server
//
const EPGDB_ACQ_VPS_PDC * EpgAcqCtl_GetVpsPdc( VPSPDC_REQ_ID clientId )
{
   EPGDB_ACQ_VPS_PDC * result = NULL;
   uint idx;

   if (clientId < VPSPDC_REQ_COUNT)
   {
      if (acqCtl.state != ACQSTATE_OFF)
      {
         // poll for new VPS/PDC data
         if (EpgAcqCtl_ProcessVps())
         {
            for (idx = 0; idx < VPSPDC_REQ_COUNT; idx++)
            {
               acqVpsPdcUpdate[idx] = TRUE;
            }
         }

         // if there are results which have not been given yet to the client, return them
         if ( acqVpsPdcUpdate[clientId] || (clientId == VPSPDC_REQ_DAEMON) )
         {
            acqVpsPdcUpdate[clientId] = FALSE;
            result = &acqStats.vpsPdc;
         }
      }
   }
   else
      debug1("EpgAcqCtl-GetVpsPdc: illegal client id %d", clientId);

   return result;
}

#ifdef USE_DAEMON
// ---------------------------------------------------------------------------
// Update statistics about acquisition running on a remote server
// - called after the block input queue was processed
// - 4 modes are supported (to minimize required bandwidth): initial, initial
//   with prov info, update with prov info, minimal update (prov info must be
//   forwarded when acq runs on a database whose blocks are not forwarded)
// - depending on the mode a different structure is enclosed
//
static void EpgAcqCtl_NetStatsUpdate( void )
{
   uint idx;
   bool aiFollows;

   if (EpgAcqClient_GetNetStats(&acqStats, &acqNetDescr, &aiFollows))
   {
      ifdebug2(acqNetDescr.dbCni != EpgDbContextGetCni(pAcqDbContext), "EpgAcqCtl-NetStatsUpdate: stats dbCni=0x%04X != acq db CNI=0x%04X", acqNetDescr.dbCni, EpgDbContextGetCni(pAcqDbContext));

      for (idx = 0; idx < VPSPDC_REQ_COUNT; idx++)
      {
         acqVpsPdcUpdate[idx] = TRUE;
      }
      if (aiFollows == FALSE)
      {  // no AI transmitted (no reception, or AI unchanged) -> trigger status update from here
         UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
      }
   }
}

// ---------------------------------------------------------------------------
// Callback for incoming VPS/PDC report
//
void EpgAcqCtl_AddNetVpsPdc( const EPGDB_ACQ_VPS_PDC * pVpsPdcUpd )
{
   uint idx;

   acqStats.vpsPdc = *pVpsPdcUpd;

   for (idx = 0; idx < VPSPDC_REQ_COUNT; idx++)
   {
      acqVpsPdcUpdate[idx] = TRUE;
   }
   UiControlMsg_AcqEvent(ACQ_EVENT_VPS_PDC);

   dprintf5("EpgAcqCtl-VpsPdcCallback: %02d.%02d. %02d:%02d (0x%04X)\n", (acqStats.vpsPdc.pil >> 15) & 0x1F, (acqStats.vpsPdc.pil >> 11) & 0x0F, (acqStats.vpsPdc.pil >>  6) & 0x1F, (acqStats.vpsPdc.pil      ) & 0x3F, acqStats.vpsPdc.cni);
}

// ---------------------------------------------------------------------------
// Update daemon acq provider list for follow-ui mode
// - but only if in follow-ui mode
//
void EpgAcqCtl_UpdateNetProvList( uint cniCount, const uint * pCniList )
{
   if ( (acqCtl.userMode == ACQMODE_FOLLOW_UI) ||
        (acqCtl.userMode == ACQMODE_FOLLOW_MERGED) )
   {
      EpgAcqCtl_SelectMode(ACQMODE_FOLLOW_UI, ACQMODE_PHASE_COUNT, cniCount, pCniList);
   }
}
#endif  // USE_DAEMON

// ---------------------------------------------------------------------------
// En-/Disable sending of extended acq statistics
// - note: when disabling extended reports, the according data is marked
//   invalid upon reception of the next statistics message; this is simpler
//   and has the advantage that a statistics message arrives inbetween and
//   makes the data valid again
//
void EpgAcqCtl_EnableAcqStats( bool enable )
{
#ifdef USE_DAEMON
   // Pass through the setting to the network client (even while acq is off or not
   // in network mode, because it's needed as soon as a connection is established)
   EpgAcqClient_SetAcqStatsMode(enable);
#endif
}

// ---------------------------------------------------------------------------
// En-/Disable sending of PI timescale information
// - param enable is set to TRUE if any timescale window is open, i.e. UI or ACQ
// - param allProviders is set to TRUE if the acq timescale window is open;
//   this flag is only used in network mode; see the epgdbsrv.c module
//
void EpgAcqCtl_EnableTimescales( bool enable, bool allProviders )
{
   if (enable == FALSE)
   {
      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         EpgTscQueue_Clear(&acqTsc);
      }
   }
   // this flag is only use when NOT in network acq mode
   acqTscEnabled = enable;

#ifdef USE_DAEMON
   // Pass through the setting to the network client (even while acq is off or not
   // in network mode, because it's needed as soon as a connection is established)
   EpgAcqClient_SetAcqTscMode(enable, allProviders);
#endif
}

// ---------------------------------------------------------------------------
// Get Pointer to PI timescale queue
// - used by the GUI to retrieve info from the queue (and thereby emptying it)
// - returns NULL if acq is off (queue might not be initialized yet)
//
EPGDB_PI_TSC * EpgAcqCtl_GetTimescaleQueue( void )
{
   if (acqCtl.state != ACQSTATE_OFF)
      return &acqTsc;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Reset VPS/PDC acquisition after channel change caused by connected TV app.
//
void EpgAcqCtl_ResetVpsPdc( void )
{
   if (acqCtl.state != ACQSTATE_OFF)
   {
      dprintf0("EpgAcqCtl-ResetVpsPdc: cleared CNI\n");
      // clear the cache
      acqStats.vpsPdc.cni = 0;

      if (acqCtl.mode != ACQMODE_NETWORK)
      {  // reset EPG and ttx decoder state machine
         EpgAcqCtl_ChannelChange(FALSE);
         EpgAcqCtl_StatisticsUpdate();
      }
      else
      {  // tell server to send the next newly received VPS/PDC CNI & PIL
         #ifdef USE_DAEMON
         EpgAcqClient_SetVpsPdcMode(TRUE, TRUE);
         #endif
      }
   }
}

// ---------------------------------------------------------------------------
// Start teletext decoding and initialize the EPG stream decoder
//
static void EpgAcqCtl_TtxStart( EPGDB_CONTEXT *dbc, EPGDB_QUEUE * pQueue, uint pageNo, uint appId )
{
   uint epgPageNo;
   uint epgAppId;
   bool bWaitForBiAi;

   if (pageNo != EPG_ILLEGAL_PAGENO)
      epgPageNo = dbc->pageNo = pageNo;
   else if ((dbc->pageNo != EPG_ILLEGAL_PAGENO) && VALID_EPG_PAGENO(dbc->pageNo))
      epgPageNo = dbc->pageNo;
   else
      epgPageNo = dbc->pageNo = EPG_DEFAULT_PAGENO;

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
}

// ---------------------------------------------------------------------------
// Stop teletext decoding and clear the EPG stream and its queue
//
static void EpgAcqCtl_TtxStop( void )
{
   TtxDecode_StopAcq();
   EpgStreamClear();
}

// ---------------------------------------------------------------------------
// Stop and Re-Start teletext acquisition
// - called after change of channel or internal parameters
//
static void EpgAcqCtl_TtxReset( EPGDB_CONTEXT *dbc, EPGDB_QUEUE * pQueue, uint pageNo, uint appId )
{
   // discard remaining blocks in the scratch buffer
   EpgStreamClear();

   EpgAcqCtl_TtxStart(dbc, pQueue, pageNo, appId);
}

// ---------------------------------------------------------------------------
// Close the acq database
// - automatically chooses the right close function for the acq database
//   depending on how it was opened, i.e. as peek or fully
//
static void EpgAcqCtl_CloseDb( void )
{
   if (pAcqDbContext != NULL)
   {
      if (acqContextIsPeek == FALSE)
         EpgContextCtl_Close(pAcqDbContext);
      else
         EpgContextCtl_ClosePeek(pAcqDbContext);
      pAcqDbContext = NULL;
   }
   else
      fatal0("EpgAcqCtl-CloseDb: db not open");
}

// ---------------------------------------------------------------------------
// Switch provider database to a new provider
// - called by AI callback upon provider change
//   and called upon GUI provider list update
// - in network mode it the acquisition database is only opened as "peek"
//   if the acq db is not in the GUI provider list; when the GUI list is
//   updated it may be required to up-/downgrade the db
//
static void EpgAcqCtl_SwitchDb( uint acqCni )
{
   uint  oldCni;
   uint  idx;

   if (acqCtl.mode != ACQMODE_NETWORK)
   {
      EpgAcqCtl_CloseDb();
      acqContextIsPeek = FALSE;
      pAcqDbContext = EpgContextCtl_Open(acqCni, CTX_FAIL_RET_CREATE, CTX_RELOAD_ERR_ANY);
   }
   else
   {
      oldCni = EpgDbContextGetCni(pAcqDbContext);

      // check if current acq provider is used by the new GUI providers
      for (idx=0; idx < acqCtl.cniCount; idx++)
         if (acqCtl.cniTab[idx] == acqCni)
            break;

      if (idx < acqCtl.cniCount)
      {  // used by GUI
         if (acqCni != oldCni)
         {  // used by GUI -> open the db completely
            EpgAcqCtl_CloseDb();
            acqContextIsPeek = FALSE;
            pAcqDbContext = EpgContextCtl_Open(acqCni, CTX_FAIL_RET_CREATE, CTX_RELOAD_ERR_ANY);
         }
         else if (acqContextIsPeek == TRUE)
         {  // currently open as peek -> upgrade to full open
            dprintf1("EpgAcqCtl-Switchdb: upgrade acq db 0x%04X to full open\n", acqCni);
            EpgAcqCtl_CloseDb();
            pAcqDbContext = EpgContextCtl_Open(acqCni, CTX_FAIL_RET_DUMMY, CTX_RELOAD_ERR_ANY);
            acqContextIsPeek = FALSE;
         }
      }
      else
      {  // current acq db is not in GUI list
         if (acqCni != oldCni)
         {  // just open a peek, i.e. AI and OI #0
            EpgAcqCtl_CloseDb();
            acqContextIsPeek = FALSE;
            pAcqDbContext = EpgContextCtl_Peek(acqCni, CTX_RELOAD_ERR_ANY);
            if (pAcqDbContext != NULL)
               acqContextIsPeek = TRUE;
            else
               pAcqDbContext = EpgContextCtl_Open(acqCni, CTX_FAIL_RET_CREATE, CTX_RELOAD_ERR_ANY);
         }
         else if (acqContextIsPeek)
         {  // currently fully open -> downgrade to peek
            dprintf1("EpgAcqCtl-Switchdb: downgrade acq db 0x%04X to peek\n", acqCni);
            EpgAcqCtl_CloseDb();
            acqContextIsPeek = FALSE;
            pAcqDbContext = EpgContextCtl_Peek(acqCni, CTX_RELOAD_ERR_ANY);
            if (pAcqDbContext != NULL)
               acqContextIsPeek = TRUE;
            else
               pAcqDbContext = EpgContextCtl_OpenDummy();
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Start the acquisition
// - in network acq mode it establishes a connection to the remote acq process
//
bool EpgAcqCtl_Start( void )
{
   bool result = FALSE;

   dprintf0("EpgAcqCtl-Start: starting acquisition\n");
   assert(pAcqDbContext == NULL);

   if (acqCtl.state == ACQSTATE_OFF)
   {
      if (acqCtl.mode == ACQMODE_NETWORK)
      {
         #ifdef USE_DAEMON
         EpgAcqCtl_InitCycle();
         // open the dummy database
         pAcqDbContext = EpgContextCtl_OpenDummy();
         acqContextIsPeek = FALSE;

         // VPS/PDC currently always enabled
         EpgAcqClient_SetVpsPdcMode(TRUE, FALSE);

         EpgDbQueue_Init(&acqDbQueue);
         EpgTscQueue_Init(&acqTsc);
         result = EpgAcqClient_StartAcq(acqCtl.cniTab, acqCtl.cniCount, &acqDbQueue, &acqTsc);
         #endif
      }
      else
      {
         // initialize teletext decoder
         EpgDbQueue_Init(&acqDbQueue);
         EpgTscQueue_Init(&acqTsc);

         acqCtl.chanChangeTime = 0;
         EpgAcqCtl_InitCycle();

         if (BtDriver_StartAcq())
         {
            // set input source and tuner frequency (also detect if device is busy)
            EpgAcqCtl_UpdateProvider(FALSE);
            EpgAcqCtl_TtxStart(pAcqDbContext, &acqDbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
            result = TRUE;
         }
      }

      if (result)
      {  // success -> initialize state

         // in network mode no BI block is transmitted
         if (acqCtl.mode == ACQMODE_NETWORK)
            acqCtl.state = ACQSTATE_WAIT_AI;
         else
            acqCtl.state = ACQSTATE_WAIT_BI;

         acqCtl.haveWarnedInpSrc = FALSE;

         EpgAcqCtl_StatisticsReset();
         EpgAcqCtl_StatisticsUpdate();
         acqCtl.dumpTime = acqStats.acqStartTime;

         UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
      }
      else if (pAcqDbContext != NULL)
      {  // failed to start acq -> clean up
         EpgAcqCtl_CloseDb();
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Stop the acquisition
// - the acq-sub process is terminated and the VBI device is freed
// - this allows other processes (e.g. a teletext decoder) to access VBI
// - in network acq mode the connection to the remote acq process is closed;
//   note: neither the daemon nor acq by the daemon are stopped
//
void EpgAcqCtl_Stop( void )
{
   if (acqCtl.state != ACQSTATE_OFF)
   {
      dprintf0("EpgAcqCtl-Stop: stopping acquisition\n");
      assert(pAcqDbContext != NULL);

      if (acqCtl.mode == ACQMODE_NETWORK)
      {
         #ifdef USE_DAEMON
         EpgAcqClient_StopAcq();
         #endif
      }
      else
      {
         BtDriver_StopAcq();
         EpgAcqCtl_TtxStop();
         EpgTscQueue_Clear(&acqTsc);
      }

      EpgAcqCtl_CloseDb();

      UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);

      acqCtl.state = ACQSTATE_OFF;
   }
}

// ---------------------------------------------------------------------------
// Stop acquisition for the EPG scan but keep the device open/driver loaded
//
void EpgAcqCtl_Suspend( bool suspend )
{
   if (suspend == FALSE)
   {
      EpgAcqCtl_Start();
   }
   else
   {
      EpgAcqCtl_CloseDb();
      EpgStreamClear();
      EpgTscQueue_Clear(&acqTsc);

      acqCtl.state = ACQSTATE_OFF;
   }
}

// ---------------------------------------------------------------------------
// Query cause for the last acquisition failure
// - must only be called when acq start failed
//
const char * EpgAcqCtl_GetLastError( void )
{
   if (acqCtl.mode != ACQMODE_NETWORK)
      return BtDriver_GetLastError();
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Initialize the module for daemon mode
//
#ifdef USE_DAEMON
void EpgAcqCtl_InitDaemon( void )
{
}
#endif

// ---------------------------------------------------------------------------
// Set input source and tuner frequency for a provider
// - errors are reported to the user interface
//
static bool EpgAcqCtl_TuneProvider( uint freq, uint cni )
{
   bool isTuner;
   bool result = FALSE;

   assert(acqCtl.mode != ACQMODE_PASSIVE);

   // reset forced-passive state; will be set upon errors below
   acqCtl.mode = acqCtl.userMode;
   acqCtl.passiveReason = ACQPASSIVE_NONE;
   // remember time of channel change
   acqCtl.chanChangeTime = time(NULL);

#ifdef USE_PROXY
   // XXX FIXME
   BtDriver_SetChannelProfile( VBI_CHANNEL_PRIO_BACKGROUND,
                               ((acqCtl.cyclePhase == ACQMODE_PHASE_MONITOR) ? VBI_CHN_SUBPRIO_INITIAL : VBI_CHN_SUBPRIO_CHECK),
                               ((acqCtl.cyclePhase == ACQMODE_PHASE_NOWNEXT) ? 3*60 : 25*60),
                               ((acqCtl.cyclePhase == ACQMODE_PHASE_MONITOR) ? 60 : 10) );
#else
   BtDriver_SetChannelProfile(VBI_CHANNEL_PRIO_BACKGROUND, 0, 0, 0);
#endif

   // tune onto the provider's channel (before starting acq, to avoid catching false data)
   // always set the input source - may have been changed externally (since we don't hog the video device permanently)
   if ( BtDriver_TuneChannel(acqCtl.inputSource, freq, FALSE, &isTuner) )
   {
      if (isTuner)
      {
         if (freq != 0)
         {
            result = TRUE;
         }
         else
         {
            dprintf0("EpgAcqCtl-TuneProv: no freq in db\n");
            // close the device, which was kept open after setting the input source
            if (cni != 0)
            {  // inform the user that acquisition will not be possible
               UiControlMsg_MissingTunerFreq(cni);
               acqCtl.passiveReason = ACQPASSIVE_NO_FREQ;
            }
            else
            {
               acqCtl.passiveReason = ACQPASSIVE_NO_DB;
            }
            acqCtl.mode = ACQMODE_FORCED_PASSIVE;
         }
      }
      else
      {
         dprintf0("EpgAcqCtl-TuneProv: input is no tuner -> force to passive mode\n");
         if ((acqCtl.mode != ACQMODE_EXTERNAL) && (acqCtl.haveWarnedInpSrc == FALSE))
         {  // warn the user, but only once
            UiControlMsg_AcqPassive();
         }
         acqCtl.haveWarnedInpSrc = TRUE;
         acqCtl.mode = ACQMODE_FORCED_PASSIVE;
         acqCtl.passiveReason = ACQPASSIVE_NO_TUNER;
      }

      if (acqCtl.autoSlicerType)
      {  // in auto-mode fall back to default slicer for every channel change
         acqCtl.currentSlicerType = VBI_SLICER_TRIVIAL;
         BtDriver_SelectSlicer(acqCtl.currentSlicerType);
      }
   }
   else
   {
      dprintf0("EpgAcqCtl-TuneProv: failed to set input source -> force to passive mode\n");
      acqCtl.mode = ACQMODE_FORCED_PASSIVE;
      acqCtl.passiveReason = ACQPASSIVE_ACCESS_DEVICE;
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
      case ACQMODE_FOLLOW_MERGED:
      case ACQMODE_CYCLIC_2:
      case ACQMODE_CYCLIC_012:
      case ACQMODE_CYCLIC_02:
      case ACQMODE_CYCLIC_12:
         if (acqCtl.cycleIdx < acqCtl.cniCount)
            cni = acqCtl.cniTab[acqCtl.cycleIdx];
         else
            cni = 0;
         break;

      case ACQMODE_NETWORK:
      case ACQMODE_EXTERNAL:
      case ACQMODE_PASSIVE:
      default:
         cni = 0;
         break;
   }
   return cni;
}

// ---------------------------------------------------------------------------
// Switch database and TV channel to the current acq provider & handle errors
// - called upon start of acquisition
// - called when UI db is changed by user, or merge started
// - called when acq mode is changed by the user
// - called after end of a cycle phase for one db
//
static bool EpgAcqCtl_UpdateProvider( bool changeDb )
{
   EPGDB_CONTEXT * pPeek;
   uint freq = 0;
   uint cni;
   bool warnInputError;
   bool dbChanged, inputChanged, dbInitial;
   bool result = TRUE;

   dbChanged = inputChanged = dbInitial = FALSE;

   // in network acq mode the db is selected by the server only
   if (acqCtl.mode != ACQMODE_NETWORK)
   {
      // determine current CNI depending on acq mode and index
      cni = EpgAcqCtl_GetProvider();

      // for acq start: open the requested db (or empty database for passive or network mode)
      if (pAcqDbContext == NULL)
      {
         dbInitial = TRUE;
         acqContextIsPeek = FALSE;
         if (cni == 0)
            pAcqDbContext = EpgContextCtl_OpenDummy();
         else
            pAcqDbContext = EpgContextCtl_Open(cni, CTX_FAIL_RET_DUMMY, CTX_RELOAD_ERR_NONE);
      }

      if (cni != 0)
      {
         if ( (changeDb == FALSE) &&
              (acqCtl.state == ACQSTATE_WAIT_BI) && (pAcqDbContext->modified == FALSE))
         {
            changeDb = TRUE;
         }
         warnInputError = FALSE;

         if (cni == EpgDbContextGetCni(pAcqDbContext))
         {
            freq = UiControlMsg_QueryProvFreq(cni);
            if (freq == 0)
               freq = pAcqDbContext->tunerFreq;
            warnInputError = TRUE;
         }
         else
         {
            // query the rc/ini file for a frequency for this provider
            freq = UiControlMsg_QueryProvFreq(cni);

            if ( changeDb )
            {  // switch to the new database
               EpgAcqCtl_CloseDb();
               pAcqDbContext = EpgContextCtl_Open(cni, CTX_FAIL_RET_DUMMY, ((freq == 0) ? CTX_RELOAD_ERR_ACQ : CTX_RELOAD_ERR_ANY));
               dbChanged = TRUE;
               if (EpgDbContextGetCni(pAcqDbContext) == cni)
                  warnInputError = TRUE;
               if (freq == 0)
                  freq = pAcqDbContext->tunerFreq;
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
                  debug1("EpgAcqCtl-UpdateProvider: peek for 0x%04X failed", cni);
            }
         }

         if ( (acqCtl.mode != ACQMODE_PASSIVE) &&
              ((acqCtl.mode != ACQMODE_FORCED_PASSIVE) || (acqCtl.passiveReason != ACQPASSIVE_NO_TUNER)) )
         {
            result = EpgAcqCtl_TuneProvider(freq, (warnInputError ? cni: 0));
            inputChanged = ((acqCtl.mode != ACQMODE_FORCED_PASSIVE) || (acqCtl.passiveReason != ACQPASSIVE_ACCESS_DEVICE));
         }
      }
      else
      {  // no provider to be tuned onto -> set at least the input source
         if (acqCtl.mode != ACQMODE_PASSIVE)
         {
            result = EpgAcqCtl_TuneProvider(0, 0);
            inputChanged = ((acqCtl.mode != ACQMODE_FORCED_PASSIVE) || (acqCtl.passiveReason != ACQPASSIVE_ACCESS_DEVICE));
         }
      }

      if ((dbChanged || inputChanged) && !dbInitial)
      {  // acq database and/or input channel/frequency have been changed
         // -> it's MANDATORY to reset all acq state machines or data from
         //    a wrong provider might be added to the database
         if (acqCtl.state != ACQSTATE_OFF)
         {  // reset acquisition: check provider CNI of next AI block
            EpgAcqCtl_ChannelChange(FALSE);
            EpgAcqCtl_StatisticsUpdate();
         }
         UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
      }

      #ifdef USE_DAEMON
      // inform server module about the current provider
      EpgAcqServer_SetProvider(EpgDbContextGetCni(pAcqDbContext));
      #endif
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
      case ACQMODE_FOLLOW_UI:
      case ACQMODE_FOLLOW_MERGED:
      case ACQMODE_CYCLIC_2:
         acqCtl.cyclePhase = ACQMODE_PHASE_STREAM2;
         break;
      case ACQMODE_PASSIVE:
      case ACQMODE_EXTERNAL:
      case ACQMODE_NETWORK:
      case ACQMODE_FORCED_PASSIVE:
      default:
         // phase value not used in these modes; set arbritrary value
         acqCtl.cyclePhase = ACQMODE_PHASE_STREAM2;
         break;
   }

   // limit to max phase (because acq. will stop upon advance from this phase)
   if (acqCtl.cyclePhase > acqCtl.stopPhase)
   {
      acqCtl.cyclePhase = acqCtl.stopPhase;
   }

   // use the first provider in the list
   acqCtl.cycleIdx = 0;
}

// ---------------------------------------------------------------------------
// Determine if netwop coverage variance is stable enough
//
static bool EpgAcqCtl_CheckVarianceStable( uint stream )
{
   EPGDB_VAR_HIST  * pHist;
   double min, max;
   uint idx;
   bool result;

   assert(stream < 2);

   pHist = acqStats.varianceHist + stream;
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
   else
      result = FALSE;

   return result;
}

// ---------------------------------------------------------------------------
// Advance the cycle phase
//
static void EpgAcqCtl_AdvanceCyclePhase( bool forceAdvance )
{
   time_t now = time(NULL);
   double quote;
   bool advance, wrongCni;
   uint cni;

   wrongCni = FALSE;
   if ( !ACQMODE_IS_PASSIVE(acqCtl.mode) && (acqCtl.mode != ACQMODE_EXTERNAL) &&
        !((acqCtl.mode == ACQMODE_FORCED_PASSIVE) && (acqCtl.passiveReason != ACQPASSIVE_ACCESS_DEVICE)) &&
        (EpgScan_IsActive() == FALSE) )
   {
      cni = EpgAcqCtl_GetProvider();
      if ((cni != 0) && (cni != EpgDbContextGetCni(pAcqDbContext)))
      {  // not the expected provider -> try to switch
         EpgAcqCtl_UpdateProvider(FALSE);
         wrongCni = TRUE;
      }
   }

   if ( ((acqCtl.state == ACQSTATE_RUNNING) || forceAdvance) &&
        (wrongCni == FALSE) &&
        (EpgScan_IsActive() == FALSE) &&
        ( ACQMODE_IS_CYCLIC(acqCtl.mode) ||
          (acqCtl.stopPhase != ACQMODE_PHASE_COUNT) ||
          ((acqCtl.mode == ACQMODE_FORCED_PASSIVE) && ACQMODE_IS_CYCLIC(acqCtl.userMode) && (acqCtl.passiveReason == ACQPASSIVE_ACCESS_DEVICE)) ) &&
        ( (acqCtl.cniCount > 1) ||
          (acqCtl.stopPhase != ACQMODE_PHASE_COUNT) ) )
   {
      // determine if acq for the current phase is complete
      // note: all criteria must have a decent timeout to catch abnormal cases
      switch (acqCtl.cyclePhase)
      {
         case ACQMODE_PHASE_NOWNEXT:
            advance = (acqStats.nowNextMaxAcqRepCount >= 2) ||
                      ((acqStats.nowNextMaxAcqRepCount == 0) && (acqStats.ai.aiCount >= NOWNEXT_TIMEOUT_AI_COUNT));
            advance |= (now >= acqStats.acqStartTime + NOWNEXT_TIMEOUT);
            break;
         case ACQMODE_PHASE_STREAM1:
            quote   = ((acqStats.count[0].ai > 0) ? ((double)acqStats.count[0].sinceAcq / acqStats.count[0].ai) : 100.0);
            advance = (quote >= MIN_CYCLE_QUOTE) &&
                      (acqStats.count[0].variance < MIN_CYCLE_VARIANCE) &&
                      (EpgAcqCtl_CheckVarianceStable(0) || (acqStats.count[0].variance == 0.0));
            advance |= ((acqStats.count[0].avgAcqRepCount >= MAX_CYCLE_ACQ_REP_COUNT) ||
                        (acqStats.count[1].avgAcqRepCount >= MAX_CYCLE_ACQ_REP_COUNT));
            advance |= (now >= acqStats.acqStartTime + STREAM1_TIMEOUT);
            break;
         case ACQMODE_PHASE_STREAM2:
         case ACQMODE_PHASE_MONITOR:
            quote   = (((acqStats.count[0].ai + acqStats.count[1].ai) > 0) ?
                       (((double)acqStats.count[0].sinceAcq + acqStats.count[1].sinceAcq) /
                        ((double)acqStats.count[0].ai + acqStats.count[1].ai)) : 100.0);
            advance = (quote >= MIN_CYCLE_QUOTE) &&
                      (acqStats.count[0].variance < MIN_CYCLE_VARIANCE) &&
                      (acqStats.count[1].variance < MIN_CYCLE_VARIANCE) &&
                      EpgAcqCtl_CheckVarianceStable(0) &&
                      EpgAcqCtl_CheckVarianceStable(1);
            advance |= (acqStats.count[1].avgAcqRepCount >= MAX_CYCLE_ACQ_REP_COUNT);
            advance |= (now >= acqStats.acqStartTime + STREAM2_TIMEOUT);
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
         else if (acqCtl.cyclePhase >= acqCtl.stopPhase)
         {  // max. phase reached -> stop acq.
            EpgAcqCtl_Stop();
         }
         else
         {  // phase complete for all databases -> switch to next
            // determine next phase
            switch (acqCtl.userMode)
            {
               case ACQMODE_CYCLIC_012:
                  if (acqCtl.cyclePhase < ACQMODE_PHASE_MONITOR)
                     acqCtl.cyclePhase += 1;
                  break;
               case ACQMODE_CYCLIC_02:
                  if (acqCtl.cyclePhase == ACQMODE_PHASE_NOWNEXT)
                     acqCtl.cyclePhase = ACQMODE_PHASE_STREAM2;
                  else
                     acqCtl.cyclePhase = ACQMODE_PHASE_MONITOR;
                  break;
               case ACQMODE_CYCLIC_12:
                  if (acqCtl.cyclePhase == ACQMODE_PHASE_STREAM1)
                     acqCtl.cyclePhase = ACQMODE_PHASE_STREAM2;
                  else
                     acqCtl.cyclePhase = ACQMODE_PHASE_MONITOR;
                  break;
               case ACQMODE_FOLLOW_MERGED:
               case ACQMODE_CYCLIC_2:
                  acqCtl.cyclePhase = ACQMODE_PHASE_MONITOR;
                  break;
               case ACQMODE_FORCED_PASSIVE:
               default:
                  break;
            }
            dprintf1("EpgAcqCtl: advance to phase #%d\n", acqCtl.cyclePhase);
            // restart with first database
            acqCtl.cycleIdx = 0;
         }

         if (acqCtl.state != ACQSTATE_OFF)
         { // note about parameter "TRUE": open the new db immediately because
           // it may need to be accessed, e.g. to reset rep counters
            EpgAcqCtl_UpdateProvider(TRUE);
         }
      }
   }
   assert((acqCtl.userMode != ACQMODE_FORCED_PASSIVE) && (acqCtl.userMode < ACQMODE_COUNT));
   assert((acqCtl.mode != ACQMODE_FORCED_PASSIVE) || (acqCtl.userMode != ACQMODE_PASSIVE));
}

// ---------------------------------------------------------------------------
// Check the tuner device status
// - tune the frequency of the current provider once again
// - this is called by the GUI when start of a TV application is detected.
//   it's used to quickly update the forced-passive state.
//
bool EpgAcqCtl_CheckDeviceAccess( void )
{
   bool result;

   if ( (acqCtl.state != ACQSTATE_OFF) && (acqCtl.mode != ACQMODE_PASSIVE) &&
        (EpgScan_IsActive() == FALSE) )
   {
      result = EpgAcqCtl_UpdateProvider(FALSE);

      if (result == FALSE)
         UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);

      dprintf1("EpgAcqCtl-CheckDeviceAccess: device is now %s\n", result ? "free" : "busy");
   }
   else
      result = FALSE;

   return result;
}

// ---------------------------------------------------------------------------
// Select acquisition mode and provider list
//
bool EpgAcqCtl_SelectMode( EPGACQ_MODE newAcqMode, EPGACQ_PHASE maxPhase,
                           uint cniCount, const uint * pCniTab )
{
   bool restart;
   bool result = FALSE;

   if (newAcqMode < ACQMODE_COUNT)
   {
      if ( !ACQMODE_IS_CYCLIC(newAcqMode) || ((cniCount > 0) && (pCniTab != NULL)) )
      {
         result = TRUE;

         // special handling for merged database: more than one provider despite follow-ui mode
         if ((newAcqMode == ACQMODE_FOLLOW_UI) && (cniCount > 1))
         {
            newAcqMode = ACQMODE_FOLLOW_MERGED;
         }
         if (cniCount > MAX_MERGED_DB_COUNT)
            cniCount = MAX_MERGED_DB_COUNT;

         // check if the same parameters are already set -> if yes skip
         // note: compare actual mode instead of user mode, to reset if acq is stalled
         if ( (newAcqMode != acqCtl.mode) ||
              (maxPhase   != acqCtl.stopPhase) ||
              (cniCount   != acqCtl.cniCount) ||
              (memcmp(acqCtl.cniTab, pCniTab, cniCount * sizeof(*pCniTab)) != 0) )
         {
            // if network mode was toggled, acq must be completely restarted because different "drivers" are used
            restart = ( ((newAcqMode == ACQMODE_NETWORK) ^ (acqCtl.userMode == ACQMODE_NETWORK)) &&
                        (acqCtl.state != ACQSTATE_OFF) );
            if (restart)
               EpgAcqCtl_Stop();

            // lock automatic database dump for network mode (only the server may write the db)
            EpgContextCtl_LockDump(newAcqMode == ACQMODE_NETWORK);

            // save the new parameters
            acqCtl.cniCount         = cniCount;
            acqCtl.userMode         = newAcqMode;
            acqCtl.mode             = newAcqMode;
            acqCtl.stopPhase        = maxPhase;
            memcpy(acqCtl.cniTab, pCniTab, sizeof(acqCtl.cniTab));

            // reset acquisition and start with the new parameters
            if (acqCtl.state != ACQSTATE_OFF)
            {
               dprintf3("EpgAcqCtl-SelectMode: reset acq with new params: mode=%d, CNI count=%d, CNI#0=%04X\n", cniCount, newAcqMode, pCniTab[0]);
               if (acqCtl.mode != ACQMODE_NETWORK)
               {
                  acqCtl.haveWarnedInpSrc = FALSE;
                  EpgAcqCtl_InitCycle();
                  result = EpgAcqCtl_UpdateProvider(TRUE);
               }
               else
               {  // send the provider list to the acquisition daemon

                  // upgrade/downgrade current acq db from/to peek if now/not-anymore in GUI list
                  uint acqCni = EpgDbContextGetCni(pAcqDbContext);
                  if (acqCni != 0)
                     EpgAcqCtl_SwitchDb(acqCni);

                  #ifdef USE_DAEMON
                  EpgAcqClient_ChangeProviders(pCniTab, cniCount);
                  #endif
               }
            }
            else if (restart)
            {
               dprintf3("EpgAcqCtl-SelectMode: restart acq with new params: mode=%d, CNI count=%d, CNI#0=%04X\n", cniCount, newAcqMode, pCniTab[0]);
               EpgAcqCtl_Start();
            }

            // if there was no provider change, at least notify about acq state change
            UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
         }
      }
      else
         debug2("EpgAcqCtl-SelectMode: cyclic mode without provider list (count=%d,ptr=0x%lx)", cniCount, (ulong)pCniTab);
   }
   else
      debug1("EpgAcqCtl-SelectMode: called with illegal acq mode: %d", newAcqMode);

   return result;
}

// ---------------------------------------------------------------------------
// TV card config: select input source and slicer type
// - distinguishing tuner/external is important to acq control, because
//   + when a tuner is input source, the channel can be controlled
//   + else only passive acq mode is possible
//   note: parameter is not used in passive and network acq modes
// - slicer type is passed through to driver, except for "automatic"
//
bool EpgAcqCtl_SetInputSource( uint inputIdx, uint slicerType )
{
   bool result;

   acqCtl.inputSource = inputIdx;

   // save slicer type; if automatic start with "trivial"
   if (slicerType == VBI_SLICER_AUTO)
   {
      acqCtl.autoSlicerType = TRUE;
      acqCtl.currentSlicerType = VBI_SLICER_TRIVIAL;
   }
   else
   {
      acqCtl.autoSlicerType = FALSE;
      acqCtl.currentSlicerType = slicerType;
   }

   if ( (acqCtl.state != ACQSTATE_OFF) &&
        (acqCtl.mode != ACQMODE_PASSIVE) &&
        (acqCtl.mode != ACQMODE_NETWORK) )
   {
      acqCtl.haveWarnedInpSrc = FALSE;

      if (acqCtl.autoSlicerType == FALSE)
      {
         BtDriver_SelectSlicer(acqCtl.currentSlicerType);
      }

      EpgAcqCtl_InitCycle();
      result = EpgAcqCtl_UpdateProvider(TRUE);

      // if there was no provider change, at least notify about acq state change
      UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
   }
   else
      result = TRUE;

   return result;
}

// ---------------------------------------------------------------------------
// Determine state of acquisition for user information
// - in network acq mode it returns info about the remove acq process
//
void EpgAcqCtl_DescribeAcqState( EPGACQ_DESCR * pAcqState )
{
   time_t lastAi, now;

   if (EpgScan_IsActive())
   {
      memset(pAcqState, 0, sizeof(EPGACQ_DESCR));
      pAcqState->state = ACQDESCR_SCAN;
   }
   else if (acqCtl.state == ACQSTATE_OFF)
   {
      memset(pAcqState, 0, sizeof(EPGACQ_DESCR));
      pAcqState->state = ACQDESCR_DISABLED;
   }
   else
   {
      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         now = time(NULL);
         lastAi = EpgDbGetAiUpdateTime(pAcqDbContext);
         if (lastAi < acqStats.acqStartTime)
            lastAi = acqStats.acqStartTime;

         // check reception
         if (acqCtl.state != ACQSTATE_RUNNING)
         {
            if (now - lastAi > ACQ_DESCR_STALLED_TIMEOUT)
            {
               if (acqCtl.state >= ACQSTATE_WAIT_AI)
                  pAcqState->state = ACQDESCR_DEC_ERRORS;
               else
                  pAcqState->state = ACQDESCR_NO_RECEPTION;
            }
            else
               pAcqState->state = ACQDESCR_STARTING;
         }
         else
         {
            if (now - lastAi > ACQ_DESCR_STALLED_TIMEOUT)
               pAcqState->state = ACQDESCR_STALLED;
            else
               pAcqState->state = ACQDESCR_RUNNING;
         }

         pAcqState->mode          = acqCtl.mode;
         pAcqState->passiveReason = acqCtl.passiveReason;
         pAcqState->cyclePhase    = acqCtl.cyclePhase;
         pAcqState->cniCount      = acqCtl.cniCount;
         pAcqState->dbCni         = EpgDbContextGetCni(pAcqDbContext);
         pAcqState->cycleCni      = EpgAcqCtl_GetProvider();
         pAcqState->isNetAcq      = FALSE;
      }
      else
      {  // network acq mode -> return info forwarded by acq daemon
         #ifdef USE_DAEMON
         // if state is set to INVALID (zero) caller should assume network state "loading db"
         // because it takes a short while after loading the db until the first stats report
         // is available
         memcpy(pAcqState, &acqNetDescr, sizeof(*pAcqState));
         pAcqState->isLocalServer = EpgAcqClient_IsLocalServer();
         pAcqState->isNetAcq      = TRUE;
         #endif
      }
   }
}

// ---------------------------------------------------------------------------
// AI callback: invoked before a new AI block is inserted into the database
//
static bool EpgAcqCtl_AiCallback( const AI_BLOCK *pNewAi )
{
   EPGACQ_STATE   oldState;
   const AI_BLOCK *pOldAi;
   uint ai_cni;
   bool accept = FALSE;

   assert(EpgScan_IsActive() == FALSE);  // EPG scan uses a separate callback
   oldState = acqCtl.state;

   if (AI_GET_CNI(pNewAi) == 0)
   {
      debug0("EpgAcqCtl-AiCallback: AI with illegal CNI 0 rejected");
      accept = FALSE;
   }
   else if (acqCtl.state == ACQSTATE_WAIT_BI)
   {
      dprintf2("EpgCtl: AI rejected in state WAIT-BI: CNI=0x%04X db-CNI=0x%04X\n", AI_GET_CNI(pNewAi), ((pAcqDbContext->pAiBlock != NULL) ? AI_GET_CNI(&pAcqDbContext->pAiBlock->blk.ai) : 0));
      accept = FALSE;
   }
   else if ((acqCtl.state == ACQSTATE_WAIT_AI) || (acqCtl.state == ACQSTATE_RUNNING))
   {
      DBGONLY(if (acqCtl.state == ACQSTATE_WAIT_AI))
         dprintf3("EpgCtl: AI found, CNI=0x%04X version %d/%d\n", AI_GET_CNI(pNewAi), pNewAi->version, pNewAi->version_swo);
      EpgDbLockDatabase(pAcqDbContext, TRUE);
      pOldAi = EpgDbGetAi(pAcqDbContext);
      ai_cni = AI_GET_CNI(pNewAi);

      if (pOldAi == NULL)
      {  // the current db is empty
         EpgDbLockDatabase(pAcqDbContext, FALSE);

         // this is just like a provider change...

         EpgAcqCtl_SwitchDb(ai_cni);
         dprintf2("EpgAcqCtl: empty acq db, AI found: CNI 0x%04X (%s)\n", ai_cni, ((EpgDbContextGetCni(pAcqDbContext) == 0) ? "new" : "reload ok"));

         #ifdef USE_DAEMON
         // inform server module about the current provider
         EpgAcqServer_SetProvider(ai_cni);
         #endif

         // store the tuner frequency if none is known yet
         if ( (pAcqDbContext->tunerFreq == 0) && (acqCtl.mode != ACQMODE_NETWORK) )
         {
            uint  freq, chan;
            bool  isTuner;
            // try to obtain the frequency from the driver (not supported by all drivers)
            if ( BtDriver_QueryChannel(&freq, &chan, &isTuner) && (isTuner) && (freq != 0) )
            {  // store the provider channel frequency in the rc/ini file
               debug2("EpgAcqCtl: setting current tuner frequency %.2f for CNI 0x%04X", (double)(freq & 0xffffff)/16, ai_cni);
               pAcqDbContext->tunerFreq = freq;
               UiControlMsg_NewProvFreq(ai_cni, freq);
            }
         }

         // new provider -> dump asap
         if (EpgDbContextGetCni(pAcqDbContext) == 0)
            acqCtl.dumpTime = 0;

         acqCtl.state = ACQSTATE_RUNNING;
         acqStatsUpdate = TRUE;

         // update the ui netwop list and acq stats output if neccessary
         UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);

         accept = TRUE;
      }
      else
      {  // already an AI in the database

         if (AI_GET_CNI(pOldAi) == AI_GET_CNI(pNewAi))
         {
            if ( (pOldAi->version != pNewAi->version) ||
                 (pOldAi->version_swo != pNewAi->version_swo) )
            {
               dprintf2("EpgAcqCtl: version number has changed, was: %d/%d\n", pOldAi->version, pOldAi->version_swo);
               UiControlMsg_AcqEvent(ACQ_EVENT_AI_VERSION_CHANGE);
               #ifdef USE_DAEMON
               // inform server module about the current provider
               EpgAcqServer_SetProvider(AI_GET_CNI(pNewAi));
               #endif
            }
            else
            {  // same AI version
               const AI_NETWOP *pOldNets, *pNewNets;
               uint netwop;

               // check if PI range of any network has changed
               pOldNets = AI_GET_NETWOPS(pOldAi);
               pNewNets = AI_GET_NETWOPS(pNewAi);
               for (netwop=0; netwop < pNewAi->netwopCount; netwop++)
               {
                  if ( (pOldNets[netwop].startNo != pNewNets[netwop].startNo) ||
                       (pOldNets[netwop].stopNoSwo != pNewNets[netwop].stopNoSwo) )
                  {
                     // new PI added at the end -> redraw timescales to mark ranges with missing PI
                     UiControlMsg_AcqEvent(ACQ_EVENT_AI_PI_RANGE_CHANGE);
                     #ifdef USE_DAEMON
                     // inform server module about the current provider
                     EpgAcqServer_SetProvider(AI_GET_CNI(pNewAi));
                     #endif
                     break;
                  }
               }
               // if no version change, trigger status update in the stats module
               if (netwop >= pNewAi->netwopCount)
                  UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
            }
            EpgDbLockDatabase(pAcqDbContext, FALSE);

            acqStatsUpdate = TRUE;
            acqCtl.state = ACQSTATE_RUNNING;
            accept = TRUE;
         }
         else
         {  // wrong provider -> switch acq database
            dprintf2("EpgAcqCtl: switching acq db from %04X to %04X\n", AI_GET_CNI(pOldAi), ai_cni);
            EpgDbLockDatabase(pAcqDbContext, FALSE);

            EpgAcqCtl_SwitchDb(ai_cni);

            if (acqCtl.mode != ACQMODE_NETWORK)
            {
               if (acqCtl.state == ACQSTATE_RUNNING)
               {  // should normally not happen, because channel changes are detected by TTX page header supervision
                  debug1("EpgAcqCtl: unexpected prov change to CNI %04X - resetting EPG Acq", ai_cni);
                  EpgAcqCtl_TtxReset(pAcqDbContext, &acqDbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);

                  // if db is new, dump asap
                  if (EpgDbContextGetCni(pAcqDbContext) == 0)
                     acqCtl.dumpTime = 0;
                  else
                     acqCtl.dumpTime = time(NULL);

                  acqCtl.state = ACQSTATE_WAIT_BI;
               }
               else
                  acqCtl.state = ACQSTATE_RUNNING;
            }

            #ifdef USE_DAEMON
            // inform server module about the current provider
            EpgAcqServer_SetProvider(ai_cni);
            #endif

            if (acqCtl.state != ACQSTATE_RUNNING)
               EpgAcqCtl_StatisticsReset();
            else
               acqStatsUpdate = TRUE;
            UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);

            accept = TRUE;
         }

         // Store the TV tuner frequency if none is known yet
         // (Note: the tuner frequency in the db is never overwritten because we can
         // not be 100% sure that we know the current TV tuner freq here, esp. if it's
         // coming from a remote TV application; only the EPG scan updates the freq.)
         if ( (pAcqDbContext->tunerFreq == 0) &&
              (oldState == ACQSTATE_WAIT_AI) && (accept) &&
              (acqCtl.state == ACQSTATE_RUNNING) &&
              (acqCtl.mode != ACQMODE_NETWORK) )
         {
            uint  freq, chan;
            bool  isTuner;
            // try to obtain the frequency from the driver (not supported by all drivers)
            if ( BtDriver_QueryChannel(&freq, &chan, &isTuner) && (isTuner) && (freq != 0) )
            {  // store the provider channel frequency in the rc/ini file
               debug2("EpgAcqCtl: setting current tuner frequency %.2f for CNI 0x%04X", (double)(freq & 0xffffff)/16, ai_cni);
               pAcqDbContext->tunerFreq = freq;
               UiControlMsg_NewProvFreq(ai_cni, freq);
            }
         }
      }
   }
   else
      assert(acqCtl.state != ACQSTATE_OFF);

   assert(EpgDbIsLocked(pAcqDbContext) == FALSE);
   return accept;
}

// ---------------------------------------------------------------------------
// Called by the database management when a new BI block was received
// - the BI block is never inserted into the database
// - only the application ID is extracted and saved in the db context
// - in network acq mode the BI state is skipped -> function not used
//
static bool EpgAcqCtl_BiCallback( const BI_BLOCK *pNewBi )
{
   assert(EpgScan_IsActive() == FALSE);  // EPG scan uses a separate callback
   assert(acqCtl.mode != ACQMODE_NETWORK);  // callback not used by the dbsrv module

   if (pNewBi->app_id == EPG_ILLEGAL_APPID)
   {
      dprintf0("EpgCtl-BiCallback: EPG not listed in BI\n");
   }
   else
   {
      if (pAcqDbContext->appId != EPG_ILLEGAL_APPID)
      {
         if (pAcqDbContext->appId != pNewBi->app_id)
         {  // not the default id
            dprintf2("EpgCtl: app-ID changed from %d to %d\n", pAcqDbContext->appId, pAcqDbContext->appId);
            EpgStreamClear();
            EpgStreamInit(&acqDbQueue, TRUE, pNewBi->app_id, pAcqDbContext->pageNo);
         }
      }
      else
         dprintf1("EpgAcqCtl-BiCallback: BI received, appID=%d\n", pNewBi->app_id);

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
            SHOULD_NOT_BE_REACHED;
            break;
      }
   }

   // the BI block is never added to the db
   return FALSE;
}

// ---------------------------------------------------------------------------
// Notification from acquisition about error
//
static void EpgAcqCtl_Stopped( void )
{
   if (acqCtl.state != ACQSTATE_OFF)
   {
      if (acqCtl.mode == ACQMODE_NETWORK)
      {
         assert(EpgDbQueue_GetBlockCount(&acqDbQueue) == 0);  // cleared in acq clnt

         if (acqCtl.haveWarnedInpSrc == FALSE)
         {  // error during initial connect -> stop acq and inform user
            EpgAcqCtl_Stop();

            UiControlMsg_NetAcqError();
            acqCtl.haveWarnedInpSrc = TRUE;
         }
         else
         {  // error occured while connected -> try to reconnect periodically
            EpgAcqCtl_StatisticsReset();
            acqStatsUpdate = FALSE;

            acqCtl.state = ACQSTATE_WAIT_AI;
            acqNetDescr.state = ACQDESCR_DISABLED;

            UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
         }
      }
      else
      {  // stop acquisition
         EpgAcqCtl_Stop();
      }
   }
}

// ---------------------------------------------------------------------------
// Handle channel changes
// - called both when the TTX decoder detects an external channel change
//   and when a channel is tuned internally.
// - changeDb: TRUE to fall back to the default db (ui db)
//
static void EpgAcqCtl_ChannelChange( bool changeDb )
{
   uint uiCni;

   assert(EpgScan_IsActive() == FALSE);  // EPG scan uses a separate callback
   assert(acqCtl.mode != ACQMODE_NETWORK);  // callback not used by the dbsrv module

   if (acqCtl.state != ACQSTATE_OFF)
   {
      dprintf4("EpgAcqCtl-ChannelChange: reset acq for db 0x%04X (0x%lx) modified=%d changedb=%d\n", ((pAcqDbContext->pAiBlock != NULL) ? AI_GET_CNI(&pAcqDbContext->pAiBlock->blk.ai) : 0), (long)pAcqDbContext, pAcqDbContext->modified, changeDb);
      acqCtl.chanChangeTime = time(NULL);

      if ( changeDb )
      {
         if ( ((uiCni = EpgDbContextGetCni(pUiDbContext)) != 0) &&
              (uiCni != EpgDbContextGetCni(pAcqDbContext)) &&
              (EpgDbContextIsMerged(pUiDbContext) == FALSE) )
         {  // close acq db and fall back to ui db
            EpgAcqCtl_CloseDb();
            acqContextIsPeek = FALSE;
            pAcqDbContext = EpgContextCtl_Open(uiCni, CTX_FAIL_RET_DUMMY, CTX_RELOAD_ERR_ANY);

            #ifdef USE_DAEMON
            // inform server module about the current provider
            EpgAcqServer_SetProvider(EpgDbContextGetCni(pAcqDbContext));
            #endif

            // notify GUI about state change
            UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
         }
         else
            UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
      }

      EpgAcqCtl_TtxReset(pAcqDbContext, &acqDbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
      acqCtl.state = ACQSTATE_WAIT_BI;
      EpgAcqCtl_StatisticsReset();
   }
}

// ----------------------------------------------------------------------------
// Poll VPS/PDC for channel changes
// - invoked when local client requests VPS/PDC data
// - called every 250 ms from daemon main loop
// - if CNI or PIL changes, it's forwarded to connected clients (if requested)
//
bool EpgAcqCtl_ProcessVps( void )
{
   uint newCni, newPil;
   bool change = FALSE;
   bool update = FALSE;

   if ( (acqCtl.state != ACQSTATE_OFF) && (EpgScan_IsActive() == FALSE) &&
        (acqCtl.mode != ACQMODE_NETWORK) )
   {
      if (TtxDecode_GetCniAndPil(&newCni, &newPil, NULL))
      {
         if ( (acqStats.vpsPdc.cni != newCni) ||
              ((newPil != acqStats.vpsPdc.pil) && VPS_PIL_IS_VALID(newPil)) )
         {
            acqStats.vpsPdc.pil = newPil;
            acqStats.vpsPdc.cni = newCni;
            dprintf5("EpgAcqCtl-PollVpsPil: %02d.%02d. %02d:%02d (0x%04X)\n", (newPil >> 15) & 0x1F, (newPil >> 11) & 0x0F, (newPil >>  6) & 0x1F, (newPil      ) & 0x3F, newCni );

            UiControlMsg_AcqEvent(ACQ_EVENT_VPS_PDC);

            change = TRUE;
         }

         #ifdef USE_DAEMON
         // inform server module about the current CNI and PIL (even if no change)
         EpgAcqServer_SetVpsPdc(change);
         #endif

         update = TRUE;
      }
   }
   return update;
}

// ---------------------------------------------------------------------------
// Periodic check if the acquisition process is still alive
// - called from main loop
//
void EpgAcqCtl_Idle( void )
{
   time_t now = time(NULL);

   if ( (acqCtl.state != ACQSTATE_OFF) && (EpgScan_IsActive() == FALSE) &&
        (acqCtl.mode != ACQMODE_NETWORK) )
   {
      // periodic db dump
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
                  acqCtl.dumpTime = now;
               }
               EpgDbLockDatabase(pAcqDbContext, FALSE);
            }
         }
      }

      // if acq running, but no reception -> wait for timeout, then reset acq
      // but make sure the channel isn't changed too often (need to wait at least 10 secs for AI)
      if ( (now >= acqCtl.dumpTime + EPGACQCTL_MODIF_INTV) &&
           (now >= acqCtl.chanChangeTime + EPGACQCTL_MODIF_INTV) )
      {
         dprintf1("EpgAcqCtl-Idle: no reception from provider 0x%04X - reset acq\n", EpgDbContextGetCni(pAcqDbContext));
         // use user-configured mode, because the actual mode might be forced-passive
         if (ACQMODE_IS_CYCLIC(acqCtl.userMode))
         {  // no reception -> advance cycle
            EpgAcqCtl_AdvanceCyclePhase(TRUE);
         }
         else if (acqCtl.userMode == ACQMODE_FOLLOW_UI)
         {
            EpgAcqCtl_UpdateProvider(FALSE);
         }
         else
         {
            EpgAcqCtl_ChannelChange(TRUE);
            EpgAcqCtl_StatisticsUpdate();
         }
      }
      assert(acqStatsUpdate == FALSE);
   }
}

// ---------------------------------------------------------------------------
// Process TTX packets in the VBI ringbuffer
// - called about once a second (most providers send one TTX page with EPG data
//   per second); assembled EPG blocks are queued
// - in network acq mode incoming blocks are read asynchronously (select(2) on
//   network socket); this func only checks if blocks are already in the queue
// - also handles error indications: acq stop, channel change, EPG param update
//
bool EpgAcqCtl_ProcessPackets( void )
{
   uint pageNo;
   bool stopped;
   time_t now;
   bool result = FALSE;

   if (pAcqDbContext != NULL)
   {
      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         // query if VBI device has been freed by higher-prio users
         if (BtDriver_QueryChannelToken())
            EpgAcqCtl_CheckDeviceAccess();

         // check if new data is available in the VBI ringbuffer
         if (TtxDecode_CheckForPackets(&stopped))
         {  // assemble VBI lines, i.e. TTX packets to EPG blocks and put them in the queue
            if (EpgStreamProcessPackets() == FALSE)
            {  // channel change detected -> restart acq for the current provider
               dprintf0("EpgAcqCtl: uncontrolled channel change detected\n");
               EpgAcqCtl_ChannelChange(TRUE);
               EpgAcqCtl_StatisticsUpdate();
            }

            // check if the current slicer type is adequate
            if ( (acqCtl.autoSlicerType) &&
                 (acqCtl.currentSlicerType + 1 < VBI_SLICER_COUNT) )
            {
               now = time(NULL);
               if ( (now >= acqCtl.chanChangeTime +
                     (acqCtl.currentSlicerType - VBI_SLICER_TRIVIAL + 1) * EPGACQCTL_SLICER_INTV) &&
                    (EpgStreamCheckSlicerQuality() == FALSE) )
               {
                  debug4("EpgAcqCtl: upgrading slicer type to #%d after %d secs (provider #%d of %d)", acqCtl.currentSlicerType + 1, (int)(now - acqCtl.chanChangeTime), acqCtl.cycleIdx, acqCtl.cniCount);
                  acqCtl.currentSlicerType += 1;
                  BtDriver_SelectSlicer(acqCtl.currentSlicerType);
               }
            }
            result = TRUE;
         }

         if (stopped == FALSE)
         {
            // check the MIP if EPG is transmitted on a different page
            pageNo = TtxDecode_GetMipPageNo();
            if ((pageNo != EPG_ILLEGAL_PAGENO) && (pageNo != pAcqDbContext->pageNo))
            {  // found a different page number in MIP
               dprintf2("EpgAcqCtl: non-default MIP page no for EPG: %03X (was %03X) -> restart acq\n", pageNo, pAcqDbContext->pageNo);

               // XXX the pageno should not be saved before RUNNING since this might be the wrong db
               pAcqDbContext->pageNo = pageNo;
               EpgAcqCtl_TtxReset(pAcqDbContext, &acqDbQueue, pageNo, EPG_ILLEGAL_APPID);
            }
         }
         else
         {  // acquisition was stopped, e.g. due to death of the acq slave
            EpgAcqCtl_Stopped();
         }
      }
      else
      {  // network acq mode -> handle timeouts; check for EPG blocks in input queue
         #ifdef USE_DAEMON
         stopped = EpgAcqClient_CheckTimeouts();
         if (stopped)
         {  // an error has occurred in the connection to the server
            EpgAcqCtl_Stopped();
         }

         // block queue processing is triggered by the socket handler in the main module
         // so we never trigger it here and always return FALSE
         result = FALSE;
         #endif
      }
   }
   // return TRUE if blocks are in queue -> caller must schedule block insertion
   return result;
}

// ---------------------------------------------------------------------------
// Insert newly acquired blocks into the EPG db
// - to be called when ProcessPackets returns TRUE, i.e. EPG blocks in input queue
// - database MUST NOT be locked by GUI
//
void EpgAcqCtl_ProcessBlocks( void )
{
   const EPGDB_BLOCK  * pBlock;
   bool overflow;

   if (pAcqDbContext != NULL)
   {
      assert(EpgDbIsLocked(pAcqDbContext) == FALSE);
      assert(acqStatsUpdate == FALSE);

      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         if ( (acqCtl.state == ACQSTATE_WAIT_BI) || (acqCtl.state == ACQSTATE_WAIT_AI) )
         {  // still waiting for the initial BI block
            EpgDbProcessQueueByType(&pAcqDbContext, &acqDbQueue, BLOCK_TYPE_BI, &epgQueueCb);
         }

         if (acqCtl.state == ACQSTATE_WAIT_AI)
         {  // BI received, but still waiting for the initial AI block
            EpgDbProcessQueueByType(&pAcqDbContext, &acqDbQueue, BLOCK_TYPE_AI, &epgQueueCb);
         }

         if (acqCtl.state == ACQSTATE_RUNNING)
         {  // add all queued blocks to the db
            overflow = (EpgDbQueue_GetBlockCount(&acqDbQueue) >= EPG_QUEUE_OVERFLOW_LEN);
            if (overflow)
               overflow = UiControlMsg_AcqQueueOverflow(TRUE);

            if (acqTscEnabled)
            {  // also add PI timescale info to the queue
               EpgDbProcessQueue(&pAcqDbContext, &acqDbQueue, &acqTsc, &epgQueueCb);
               if (EpgTscQueue_HasElems(&acqTsc))
                  UiControlMsg_AcqEvent(ACQ_EVENT_PI_ADDED);
            }
            else
               EpgDbProcessQueue(&pAcqDbContext, &acqDbQueue, NULL, &epgQueueCb);

            if (overflow)
               UiControlMsg_AcqQueueOverflow(FALSE);
         }
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
               dprintf1("EpgAcqCtl: dumped db %04X to file\n", EpgDbContextGetCni(pAcqDbContext));
               acqCtl.dumpTime = time(NULL);
            }
            EpgDbLockDatabase(pAcqDbContext, FALSE);
         }
      }
      else
      {  // network acq mode
         overflow = (EpgDbQueue_GetBlockCount(&acqDbQueue) >= EPG_QUEUE_OVERFLOW_LEN);
         if (overflow)
            overflow = UiControlMsg_AcqQueueOverflow(TRUE);

         if (acqCtl.state == ACQSTATE_WAIT_AI)
         {  // fail-safe: check if the first block is a AI block
            while ( ((pBlock = EpgDbQueue_Peek(&acqDbQueue)) != NULL) &&
                    (pBlock->type != BLOCK_TYPE_AI) )
            {
               debug1("EpgAcqCtl: illegal block type %d in queue while waiting for AI", pBlock->type);
               xfree((void *) EpgDbQueue_Get(&acqDbQueue));
            }
         }
         EpgDbProcessQueue(&pAcqDbContext, &acqDbQueue, NULL, &epgQueueCb);

         if (overflow)
            UiControlMsg_AcqQueueOverflow(FALSE);

         #ifdef USE_DAEMON
         EpgAcqCtl_NetStatsUpdate();
         #endif
         // statistics are sent automatically by the server, so no update required
         acqStatsUpdate = FALSE;

         // once connection is established, network errors are no longer reported by popups
         acqCtl.haveWarnedInpSrc = TRUE;

         if (EpgTscQueue_HasElems(&acqTsc))
         {  // PI timescale info has been received -> unlock new buffers and trigger GUI
            EpgTscQueue_UnlockBuffers(&acqTsc);
            UiControlMsg_AcqEvent(ACQ_EVENT_PI_ADDED);
         }
      }
      dprintf5("EpgAcqCtl: state=%d, phase=%d, CNI-idx=%d, cyCni=0x%04X, dbCni=0x%04X\n", acqCtl.state, acqCtl.cyclePhase, acqCtl.cycleIdx, acqCtl.cniTab[acqCtl.cycleIdx], EpgDbContextGetCni(pAcqDbContext));
   }
}

