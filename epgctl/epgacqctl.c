/*
 *  Acquisition main control
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
 *    It contains the top-level start and stop acquisition functions.
 *    Control is then passed on to Nextview, Teletext and network
 *    acquisition sub-modules.
 *
 *    This module maintains acquisition parameters, in particular the
 *    list of providers.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgacqctl.c,v 1.94 2009/03/29 19:17:16 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgdb/ttxgrab.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgtscqueue.h"

#include "epgui/uictrl.h"
#include "epgui/epgmain.h"
#include "epgctl/epgctxctl.h"
#include "epgctl/epgscan.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgctl/epgacqnxt.h"
#include "epgctl/epgacqttx.h"


// ---------------------------------------------------------------------------
// Declaration of module state variables

typedef struct
{
   bool           acqEnabled;
   EPGACQ_MODE    mode;
   EPGACQ_PASSIVE passiveReason;
   EPGACQ_PHASE   cyclePhase;
   EPGACQ_PHASE   stopPhase;
   uint           cycleIdx;
   uint           cniCount;
   uint           cniTab[MAX_MERGED_DB_COUNT];
   uint           ttxSrcCount;
   uint           inputSource;
   uint           currentSlicerType;
   bool           autoSlicerType;
   bool           haveWarnedInpSrc;
   bool           advanceCycle;
   bool           isTtxSrc;
   uint32_t       acqCniInd[VPSPDC_REQ_COUNT];
   uint32_t       acqPilInd[VPSPDC_REQ_COUNT];
   EPG_ACQ_VPS_PDC  acqVpsPdc;
} EPGACQCTL_STATE;

static EPGACQCTL_STATE    acqCtl;


static void EpgAcqCtl_InitCycle( void );
static uint EpgAcqCtl_GetReqProv( void );
static void EpgAcqCtl_ChannelChange( void );
static bool EpgAcqCtl_UpdateProvider( bool changeDb );

#define IS_TTX_ACQ_CNI(CNI)    ((CNI) == XMLTV_TTX_PROV_CNI)

// ---------------------------------------------------------------------------
// Determine state of acquisition for user information
// - mainly used for the main window status line and acq statistics window
// - the main state is also used for some GUI controls
// - in network acq mode it returns info about the remote acq process
//   and additionally the state of the network connection
//
void EpgAcqCtl_DescribeAcqState( EPGACQ_DESCR * pAcqState )
{
   if (pAcqState != NULL)
   {
      if (EpgScan_IsActive())
      {
         memset(pAcqState, 0, sizeof(EPGACQ_DESCR));
         pAcqState->nxtvState = ACQDESCR_SCAN;
         pAcqState->ttxGrabState = ACQDESCR_DISABLED;
      }
      else if (acqCtl.acqEnabled == FALSE)
      {
         memset(pAcqState, 0, sizeof(EPGACQ_DESCR));
         pAcqState->nxtvState = ACQDESCR_DISABLED;
         pAcqState->ttxGrabState = ACQDESCR_DISABLED;
      }
      else
      {
         if (acqCtl.mode != ACQMODE_NETWORK)
         {
            pAcqState->mode          = acqCtl.mode;
            pAcqState->passiveReason = acqCtl.passiveReason;
            pAcqState->cyclePhase    = acqCtl.cyclePhase;
            pAcqState->cniCount      = acqCtl.cniCount;
            pAcqState->cycleIdx      = acqCtl.cycleIdx;
            pAcqState->cycleCni      = EpgAcqCtl_GetReqProv();
            pAcqState->isNetAcq      = FALSE;

            // query grabber states and sources
            EpgAcqNxtv_DescribeAcqState(pAcqState);
            EpgAcqTtx_DescribeAcqState(pAcqState);

            if ((acqCtl.mode != ACQMODE_PASSIVE) &&
                (acqCtl.mode != ACQMODE_EXTERNAL))
               pAcqState->isTtxSrc   = acqCtl.isTtxSrc;
            else
               pAcqState->isTtxSrc   = (pAcqState->nxtvState < pAcqState->ttxGrabState);
         }
         else
         {  // network acq mode -> return info forwarded by acq daemon
            #ifdef USE_DAEMON
            EpgAcqClient_DescribeAcqState(pAcqState);
            pAcqState->isNetAcq      = TRUE;
            #endif
         }
      }
   }
   else
      fatal0("EpgAcqCtl-DescribeAcqState: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Return text strings line which describes the current acq mode
//
void EpgAcqCtl_GetAcqModeStr( const EPGACQ_DESCR * pAcqState, bool forTtx,
                              const char ** ppModeStr, const char ** ppPasvStr )
{
   if (pAcqState->passiveReason == ACQPASSIVE_NONE)
   {
      switch (pAcqState->mode)
      {
         case ACQMODE_PASSIVE:
            *ppModeStr = "passive";
            break;
         case ACQMODE_EXTERNAL:
            *ppModeStr = "external";
            break;
         case ACQMODE_FOLLOW_UI:
         case ACQMODE_FOLLOW_MERGED:
            if ((forTtx ?
                  (pAcqState->cniCount - ((pAcqState->ttxSrcCount > 0) ? 1 : 0)) :
                  (pAcqState->ttxSrcCount)) == 0)
               *ppModeStr = "follow browser database";
            else
               *ppModeStr = "follow browser database (merged)";
            break;
         case ACQMODE_CYCLIC_2:
            if (pAcqState->cniCount <= 1)
               *ppModeStr = "manual";
            else
            {
               if (pAcqState->cyclePhase == ACQMODE_PHASE_STREAM2)
                  *ppModeStr = "manual, phase 'All'";
               else
                  *ppModeStr = "manual, phase 'Complete'";
            }
            break;
         default:
            switch (pAcqState->cyclePhase)
            {
               case ACQMODE_PHASE_NOWNEXT:
                  *ppModeStr = "cyclic, phase 'Now'";
                  break;
               case ACQMODE_PHASE_STREAM1:
                  *ppModeStr = "cyclic, phase 'Near'";
                  break;
               case ACQMODE_PHASE_STREAM2:
                  *ppModeStr = "cyclic, phase 'All'";
                  break;
               case ACQMODE_PHASE_MONITOR:
                  *ppModeStr = "cyclic, phase 'Complete'";
                  break;
               default:
                  break;
            }
            break;
      }
      *ppPasvStr = NULL;
   }
   else
   {
      if (pAcqState->nxtvState == ACQDESCR_DISABLED)
      {
         *ppModeStr = "disabled";
      }
      else
      {
         *ppModeStr = "forced passive";

         switch (pAcqState->passiveReason)
         {
            case ACQPASSIVE_NO_TUNER:
               *ppPasvStr = "input source is not a tuner";
               break;
            case ACQPASSIVE_NO_FREQ:
               *ppPasvStr = "frequency unknown";
               break;
            case ACQPASSIVE_NO_DB:
               *ppPasvStr = "database missing";
               break;
            case ACQPASSIVE_ACCESS_DEVICE:
               *ppPasvStr = "video device busy";
               break;
            default:
               *ppPasvStr = "unknown";
               break;
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Return database block counts: number blocks in AI, number expired, etc.
// - in network acq mode the info is forwarded by the acquisition daemon
//   (even if the database is fully open on client-side)
//
bool EpgAcqCtl_GetDbStats( EPGDB_BLOCK_COUNT * pDbStats, uint * pNowMaxAcqNetCount )
{
   EPG_ACQ_STATS acqStats;
   bool result = FALSE;

   if (acqCtl.acqEnabled)
   {
      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         result = EpgAcqNxtv_GetAcqStats(&acqStats.nxtv);
      }
      else
      {
         #ifdef USE_DAEMON
         result = EpgAcqClient_GetAcqStats(&acqStats);
         #endif
      }
   }

   if (result)
   {
      memcpy(pDbStats, acqStats.nxtv.count, sizeof(EPGDB_BLOCK_COUNT)*2);
      if (pNowMaxAcqNetCount != NULL)
         *pNowMaxAcqNetCount = acqStats.nxtv.nowMaxAcqNetCount;
   }
   else
   {
      memset(pDbStats, 0, sizeof(EPGDB_BLOCK_COUNT)*2);
      if (pNowMaxAcqNetCount != NULL)
         *pNowMaxAcqNetCount = 0;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Return complete set of acq state and statistic values
// - used by "View acq statistics" popup window
// - in network acq mode it returns info about the remote acquisition process
//   requires the extended stats report mode to be enabled; else only the
//   acq state (next function) is available
//
bool EpgAcqCtl_GetAcqStats( EPG_ACQ_STATS * pAcqStats )
{
   time_t ttx_start_t;
   bool result = FALSE;

   if (acqCtl.acqEnabled)
   {
      memset(pAcqStats, 0, sizeof(EPG_ACQ_STATS));

      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         EpgAcqNxtv_GetAcqStats(&pAcqStats->nxtv);
         EpgAcqTtx_GetAcqStats(&pAcqStats->ttx_grab);
         pAcqStats->lastStatsUpdate = time(NULL);

         // retrieve additional data from TTX packet decoder
         TtxDecode_GetStatistics(&pAcqStats->ttx_dec, &ttx_start_t);
         pAcqStats->ttx_duration = pAcqStats->lastStatsUpdate - ttx_start_t;

         pAcqStats->nxtvMaster = !acqCtl.isTtxSrc;
         result = TRUE;
      }
      else
      {
         #ifdef USE_DAEMON
         result = EpgAcqClient_GetAcqStats(pAcqStats);
         #endif
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Query CNI of the current provider
//
uint EpgAcqCtl_GetProvCni( void )
{
   EPGDB_CONTEXT * pDbContext;
   uint cni = 0;

   if (acqCtl.acqEnabled)
   {
      if (acqCtl.mode != ACQMODE_NETWORK)
         pDbContext = EpgAcqNxtv_GetDbContext();
      else
         pDbContext = EpgAcqClient_GetDbContext();

      if (pDbContext != NULL)
      {
         cni = EpgDbContextGetCni(pDbContext);
      }
   }
   return cni;
}

// ---------------------------------------------------------------------------
// Get acquisition database context
// - must be called twice: once with lock set to TRUE, then FALSE
//
EPGDB_CONTEXT * EpgAcqCtl_GetDbContext( bool lock )
{
   EPGDB_CONTEXT * pDbContext = NULL;

   if (acqCtl.acqEnabled)
   {
      if (acqCtl.mode != ACQMODE_NETWORK)
         pDbContext = EpgAcqNxtv_GetDbContext();
      else
         pDbContext = EpgAcqClient_GetDbContext();

      if (pDbContext != NULL)
      {
         EpgDbLockDatabase(pDbContext, lock);
      }
   }
   return pDbContext;
}

// ---------------------------------------------------------------------------
// Get Pointer to PI timescale queue
// - used by the GUI to retrieve info from the queue (and thereby emptying it)
// - returns NULL if acq is off (queue might not be initialized yet)
//
EPGDB_PI_TSC * EpgAcqCtl_GetTimescaleQueue( void )
{
#ifdef USE_DAEMON
   if (acqCtl.mode == ACQMODE_NETWORK)
      return EpgAcqClient_GetTimescaleQueue();
   else
#endif
      return EpgAcqNxtv_GetTimescaleQueue();
}

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
   EpgAcqNxtv_EnableTimescales(enable);

#ifdef USE_DAEMON
   // Pass through the setting to the network client (even while acq is off or not
   // in network mode, because it's needed as soon as a connection is established)
   EpgAcqClient_SetAcqTscMode(enable, allProviders);
#endif
}

// ---------------------------------------------------------------------------
// Reset VPS/PDC acquisition after channel change caused by connected TV app.
//
void EpgAcqCtl_ResetVpsPdc( void )
{
   if (acqCtl.acqEnabled)
   {
      dprintf0("EpgAcqCtl-ResetVpsPdc: cleared CNI\n");
      // clear the cache
      acqCtl.acqVpsPdc.cni = 0;

      if (acqCtl.mode != ACQMODE_NETWORK)
      {  // reset EPG and ttx decoder state machine
         EpgAcqCtl_ChannelChange();
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
// Return VPS CNI received on EPG input channel
// - used for EPG acq stats output; esp. useful in forced-passive mode, where
//   it allows to see what channel is tuned in instead of the acq provider's
// - in network acq mode the VPS CNI is forwarded by the remote acq server
//
bool EpgAcqCtl_GetVpsPdc( EPG_ACQ_VPS_PDC * pVpsPdc, VPSPDC_REQ_ID clientId, bool force )
{
   CNI_TYPE cniType;
   uint  newCni;
   uint  newPil;
   bool  result = FALSE;

   if (clientId < VPSPDC_REQ_COUNT)
   {
      if (acqCtl.acqEnabled)
      {
         if (acqCtl.mode != ACQMODE_NETWORK)
         {
            if (force)
            {
               acqCtl.acqCniInd[clientId] = 0;
               acqCtl.acqPilInd[clientId] = 0;
            }
            // poll for new VPS/PDC data
            // if there are results which have not been given yet to the client, return them
            if ( TtxDecode_GetCniAndPil(&newCni, &newPil, &cniType,
                                        &acqCtl.acqCniInd[clientId], &acqCtl.acqPilInd[clientId], NULL) )
            {
               pVpsPdc->cniType = cniType;
               pVpsPdc->pil = newPil;
               pVpsPdc->cni = newCni;
               result = TRUE;
            }
         }
         else
         {  // retrieve data from network client
            result = EpgAcqClient_GetVpsPdc(pVpsPdc, &acqCtl.acqCniInd[clientId]);
         }
      }
   }
   else
      fatal1("EpgAcqCtl-GetVpsPdc: illegal client id %d", clientId);

   return result;
}

// ---------------------------------------------------------------------------
// Check the tuner device status
// - tune the frequency of the current provider once again
// - this is called by the GUI when start of a TV application is detected.
//   it's used to quickly update the forced-passive state.
//
bool EpgAcqCtl_CheckDeviceAccess( void )
{
   bool result = FALSE;

   if ( (acqCtl.acqEnabled) &&
        (acqCtl.mode != ACQMODE_PASSIVE) &&
        (EpgScan_IsActive() == FALSE) )
   {
      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         result = EpgAcqCtl_UpdateProvider(FALSE);

         if (result == FALSE)
            UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);

         dprintf1("EpgAcqCtl-CheckDeviceAccess: device is now %s\n", result ? "free" : "busy");
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Start the acquisition
// - in network acq mode it establishes a connection to the remote acq process
//
bool EpgAcqCtl_Start( void )
{
   bool result = FALSE;

   dprintf0("EpgAcqCtl-Start: starting acquisition\n");

   if (acqCtl.acqEnabled == FALSE)
   {
      if (acqCtl.mode == ACQMODE_NETWORK)
      {
         EpgAcqCtl_InitCycle();

         #ifdef USE_DAEMON
         // VPS/PDC currently always enabled
         EpgAcqClient_SetVpsPdcMode(TRUE, FALSE);
         EpgAcqClient_SetProviders(acqCtl.cniTab, acqCtl.cniCount);
         result = EpgAcqClient_Start();
         #endif
      }
      else
      {
         EpgAcqCtl_InitCycle();

         if (BtDriver_StartAcq())
         {
            result = EpgAcqCtl_UpdateProvider(TRUE);
         }
      }

      if (result)
      {  // success -> initialize state
         UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
         acqCtl.acqEnabled = TRUE;
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
   if (acqCtl.acqEnabled)
   {
      dprintf0("EpgAcqCtl-Stop: stopping acquisition\n");

      if (acqCtl.mode == ACQMODE_NETWORK)
      {
         #ifdef USE_DAEMON
         EpgAcqClient_Stop();
         #endif
      }
      else
      {
         BtDriver_StopAcq();
      }
      EpgAcqNxtv_Stop();
      EpgAcqTtx_Stop();

      UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);

      acqCtl.acqEnabled = FALSE;
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
      EpgAcqNxtv_Suspend();
      EpgAcqTtx_Suspend();

      acqCtl.acqEnabled = FALSE;
   }
}

// ---------------------------------------------------------------------------
// Query if acquisition is currently enabled
//
bool EpgAcqCtl_IsActive( void )
{
   return acqCtl.acqEnabled;
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
// Set input source and tuner frequency for a provider
// - errors are reported to the user interface
//
bool EpgAcqCtl_TuneProvider( bool isTtx, uint freq, uint cni, EPGACQ_PASSIVE * pMode )
{
   bool isTuner;
   bool result = FALSE;

   assert(acqCtl.mode != ACQMODE_PASSIVE);
   assert(isTtx == acqCtl.isTtxSrc);  // alternate source should be in passive mode

   // reset forced-passive state; will be set upon errors below
   acqCtl.passiveReason = ACQPASSIVE_NONE;

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
            dprintf2("EpgAcqCtl-TuneProv: tuned freq %d for provider 0x%04X\n", freq, cni);
            result = TRUE;
         }
         else
         {
            dprintf1("EpgAcqCtl-TuneProv: no freq in db for provider 0x%04X\n", cni);
            if (cni != 0)
            {  // inform the user that acquisition will not be possible
               acqCtl.passiveReason = ACQPASSIVE_NO_FREQ;
               UiControlMsg_MissingTunerFreq(cni);
            }
            else
            {
               acqCtl.passiveReason = ACQPASSIVE_NO_DB;
            }
         }
      }
      else
      {
         dprintf0("EpgAcqCtl-TuneProv: input is no tuner -> force to passive mode\n");
         acqCtl.passiveReason = ACQPASSIVE_NO_TUNER;

         if (freq != 0)
         {
            if ((acqCtl.mode != ACQMODE_EXTERNAL) && (acqCtl.haveWarnedInpSrc == FALSE))
            {  // warn the user, but only once
               UiControlMsg_AcqPassive();
            }
            acqCtl.haveWarnedInpSrc = TRUE;
         }
      }

      if (acqCtl.autoSlicerType)
      {  // in auto-mode fall back to default slicer for every channel change
         acqCtl.currentSlicerType = VBI_SLICER_TRIVIAL;
         BtDriver_SelectSlicer(acqCtl.currentSlicerType);
      }

      // inform the other control module about the channel change
      if (isTtx)
         EpgAcqNxtv_ChannelChange(TRUE);
      else
         EpgAcqTtx_ChannelChange();
   }
   else
   {
      dprintf0("EpgAcqCtl-TuneProv: failed to set input source -> force to passive mode\n");
      acqCtl.passiveReason = ACQPASSIVE_ACCESS_DEVICE;
   }

   *pMode = acqCtl.passiveReason;

   return result;
}

// ---------------------------------------------------------------------------
// Chooses a provider for the acquisition
//
static uint EpgAcqCtl_GetReqProv( void )
{
   uint cni;

   // use original user configured mode to ignore possible current error state
   // because the caller wants to know which TV channel ought to be tuned
   switch (acqCtl.mode)
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
   bool result;

   if (acqCtl.mode != ACQMODE_NETWORK)
   {
      if (acqCtl.isTtxSrc == FALSE)
      {
         dprintf1("EpgAcqCtl-UpdateProvider: start nxtv CNI 0x%04X\n", EpgAcqCtl_GetReqProv());

         result = EpgAcqNxtv_Start(acqCtl.mode, acqCtl.cyclePhase, EpgAcqCtl_GetReqProv());

         if (acqCtl.ttxSrcCount != 0)
            EpgAcqTtx_Start(ACQMODE_PASSIVE, acqCtl.cyclePhase);
         else
            EpgAcqTtx_Stop();
      }
      else
      {
         dprintf0("EpgAcqCtl-UpdateProvider: start TTX acq\n");

         result = EpgAcqTtx_Start(acqCtl.mode, acqCtl.cyclePhase);

         EpgAcqNxtv_Start(ACQMODE_PASSIVE, acqCtl.cyclePhase, 0);
      }
   }
   else
      result = FALSE;

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
   acqCtl.isTtxSrc = IS_TTX_ACQ_CNI(acqCtl.cniTab[0]);
   acqCtl.advanceCycle = FALSE;

   EpgAcqTtx_InitCycle(acqCtl.isTtxSrc, acqCtl.cyclePhase);
}

// ---------------------------------------------------------------------------
// Advance the acquisition cycle: switch to the next provider or phase
// - called when an acq sub-module reports that the current provider is done
//   (also called when acq has stalled for a certain time)
// - only used if there's more than one provider or source, or if an acq mode
//   with phases is enabled (in particular it's not used for 1 prov in
//   "follow-ui" acq mode)
//
static void EpgAcqCtl_AdvanceCyclePhase( void )
{
   if (acqCtl.cycleIdx + 1 < acqCtl.cniCount)
   {
      acqCtl.cycleIdx += 1;
      dprintf2("EpgAcqCtl: advance to CNI #%d of %d\n", acqCtl.cycleIdx, acqCtl.cniCount);
      acqCtl.isTtxSrc = IS_TTX_ACQ_CNI(acqCtl.cniTab[acqCtl.cycleIdx]);
      EpgAcqTtx_InitCycle(acqCtl.isTtxSrc, acqCtl.cyclePhase);
   }
   else if (acqCtl.cyclePhase >= acqCtl.stopPhase)
   {  // max. phase reached -> stop acq.
      EpgAcqCtl_Stop();
   }
   else
   {  // phase complete for all databases -> switch to next
      // determine next phase
      switch (acqCtl.mode)
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
         default:
            break;
      }
      dprintf1("EpgAcqCtl: advance to phase #%d\n", acqCtl.cyclePhase);
      // restart with first database
      acqCtl.cycleIdx = 0;
      acqCtl.isTtxSrc = IS_TTX_ACQ_CNI(acqCtl.cniTab[0]);
      EpgAcqTtx_InitCycle(acqCtl.isTtxSrc, acqCtl.cyclePhase);
   }

   if (acqCtl.acqEnabled)
   {  // note about parameter "TRUE": open the new db immediately because
      // it may need to be accessed, e.g. to reset rep counters
      EpgAcqCtl_UpdateProvider(TRUE);
   }

   assert(acqCtl.mode < ACQMODE_COUNT);
   assert((acqCtl.passiveReason == ACQPASSIVE_NONE) || (acqCtl.mode != ACQMODE_PASSIVE));
}

#ifdef USE_DAEMON
// ---------------------------------------------------------------------------
// Update provider list only
// - used by daemon (server process) if in follow-ui mode (in this case the
//   connected clients' GUI database selection determine the acq prov. list)
// - note: this function is a reduced version of "SelectMode"
//
void EpgAcqCtl_UpdateProvList( uint cniCount, const uint * pCniTab )
{
   if ( (acqCtl.mode == ACQMODE_FOLLOW_UI) ||
        (acqCtl.mode == ACQMODE_FOLLOW_MERGED) )
   {
      if ( (cniCount != acqCtl.cniCount) ||
           (memcmp(acqCtl.cniTab, pCniTab, cniCount * sizeof(*pCniTab)) != 0) )
      {
         // copy the new parameters
         acqCtl.cniCount = cniCount;
         memcpy(acqCtl.cniTab, pCniTab, sizeof(acqCtl.cniTab));

         // reset acquisition and start with the new parameters
         if (acqCtl.acqEnabled)
         {
            dprintf2("EpgAcqCtl-UpdateNetProvList: reset acq for new CNI count=%d, CNI#0=%04X\n", cniCount, pCniTab[0]);

            EpgAcqCtl_InitCycle();
            EpgAcqCtl_UpdateProvider(TRUE);
         }
      }
   }
}
#endif  // USE_DAEMON

// ---------------------------------------------------------------------------
// Select acquisition mode and provider list
//
bool EpgAcqCtl_SelectMode( EPGACQ_MODE newAcqMode, EPGACQ_PHASE maxPhase,
                           uint cniCount, const uint * pCniTab,
                           uint ttxSrcCount, const char * pTtxNames, const uint * pTtxFreqs )
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
         {
            debug2("EpgAcqCtl-SelectMode: WARNING: max CNI count exceeded: %d>%d", cniCount, MAX_MERGED_DB_COUNT);
            cniCount = MAX_MERGED_DB_COUNT;
         }
         if ((ttxSrcCount != 0) && ((pTtxFreqs == NULL) || (pTtxNames == NULL)))
         {
            fatal3("EpgAcqCtl-SelectMode: no name or freq table for %d TTX sources: %lX,%lX", ttxSrcCount, (long)pTtxNames, (long)pTtxFreqs);
            ttxSrcCount = 0;
         }

         if ( (newAcqMode != acqCtl.mode) &&
              (newAcqMode != ACQMODE_NETWORK) )
         {
            acqCtl.haveWarnedInpSrc = FALSE;
         }

         // check if the same parameters are already set -> if yes skip
         // note: compare actual mode instead of user mode, to reset if acq is stalled
         if ( (newAcqMode != acqCtl.mode) ||
              (maxPhase   != acqCtl.stopPhase) ||
              (cniCount   != acqCtl.cniCount) ||
              (memcmp(acqCtl.cniTab, pCniTab, cniCount * sizeof(*pCniTab)) != 0) ||
              (EpgAcqTtx_CompareParams(ttxSrcCount, pTtxNames, pTtxFreqs) == FALSE) )
         {
            // if network mode was toggled, acq must be completely restarted because different "drivers" are used
            restart = ( ((newAcqMode == ACQMODE_NETWORK) ^ (acqCtl.mode == ACQMODE_NETWORK)) &&
                        (acqCtl.acqEnabled) );
            if (restart)
               EpgAcqCtl_Stop();

            // lock automatic database dump for network mode (only the server may write the db)
            EpgContextCtl_LockDump(newAcqMode == ACQMODE_NETWORK);

            // save the new parameters
            acqCtl.cniCount         = cniCount;
            acqCtl.ttxSrcCount      = ttxSrcCount;
            acqCtl.mode             = newAcqMode;
            acqCtl.stopPhase        = maxPhase;
            memcpy(acqCtl.cniTab, pCniTab, sizeof(acqCtl.cniTab));

            // pass through teletext params
            EpgAcqTtx_SetParams(ttxSrcCount, pTtxNames, pTtxFreqs);

            // reset acquisition and start with the new parameters
            if (acqCtl.acqEnabled)
            {
               dprintf3("EpgAcqCtl-SelectMode: reset acq with new params: mode=%d, CNI count=%d, CNI#0=%04X\n", newAcqMode, cniCount, pCniTab[0]);
               if (acqCtl.mode == ACQMODE_NETWORK)
               {  // send the provider list to the acquisition daemon
                  #ifdef USE_DAEMON
                  EpgAcqClient_SetProviders(pCniTab, cniCount);
                  #endif
               }
               EpgAcqCtl_InitCycle();
               result = EpgAcqCtl_UpdateProvider(TRUE);
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

   if (acqCtl.inputSource != inputIdx)
      acqCtl.haveWarnedInpSrc = FALSE;

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

   if ( (acqCtl.acqEnabled) &&
        (acqCtl.mode != ACQMODE_PASSIVE) &&
        (acqCtl.mode != ACQMODE_NETWORK) )
   {
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
// Handle channel changes
// - called when a channel is tuned internally.
//
static void EpgAcqCtl_ChannelChange( void )
{
   assert(EpgScan_IsActive() == FALSE);  // EPG scan uses a separate callback
   assert(acqCtl.mode != ACQMODE_NETWORK);  // callback not used by the net client

   EpgAcqNxtv_ChannelChange(FALSE);
   EpgAcqTtx_ChannelChange();
}

// ---------------------------------------------------------------------------
// Process notification about VBI driver failure
//
static void EpgAcqCtl_Stopped( void )
{
   if (acqCtl.acqEnabled)
   {
      if (acqCtl.mode == ACQMODE_NETWORK)
      {
         if (acqCtl.haveWarnedInpSrc == FALSE)
         {  // error during initial connect -> stop acq and inform user
            #ifdef USE_DAEMON
            EpgAcqClient_Stop();
            #endif
            EpgAcqCtl_Stop();

            UiControlMsg_NetAcqError();
            acqCtl.haveWarnedInpSrc = TRUE;
         }
         else
         {  // error occured while connected -> try to reconnect periodically
            memset(&acqCtl.acqVpsPdc, 0, sizeof(acqCtl.acqVpsPdc));
            UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
         }
      }
      else
      {  // stop acquisition
         EpgAcqCtl_Stop();
      }
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
   CNI_TYPE cniType;
   bool change = FALSE;
   bool update = FALSE;

   if ( (acqCtl.acqEnabled) &&
        (acqCtl.mode != ACQMODE_NETWORK) )
   {
      if ( TtxDecode_GetCniAndPil(&newCni, &newPil, &cniType,
                                  &acqCtl.acqCniInd[VPSPDC_REQ_POLL], &acqCtl.acqPilInd[VPSPDC_REQ_POLL], NULL) )
      {
         if ( (acqCtl.acqVpsPdc.cni != newCni) ||
              ((newPil != acqCtl.acqVpsPdc.pil) && VPS_PIL_IS_VALID(newPil)) )
         {
            acqCtl.acqVpsPdc.cniType = cniType;
            acqCtl.acqVpsPdc.pil = newPil;
            acqCtl.acqVpsPdc.cni = newCni;
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
// Process TTX packets in the VBI ringbuffer
// - called about once a second (most providers send one TTX page with EPG data
//   per second); assembled EPG blocks are queued
// - in network acq mode incoming blocks are read asynchronously (select(2) on
//   network socket); this func only checks if blocks are already in the queue
// - also handles error indications: acq stop, channel change, EPG param update
//
bool EpgAcqCtl_ProcessPackets( void )
{
   bool slicerOk;
   bool * pCheckNxtvSlicer;
   bool * pCheckTtxSlicer;
   bool stopped;
   bool result = FALSE;

   if (acqCtl.acqEnabled)
   {
      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         // query if VBI device has been freed by higher-prio users
         if (BtDriver_QueryChannelToken())
            EpgAcqCtl_CheckDeviceAccess();

         // check if the current slicer type is adequate
         pCheckNxtvSlicer = NULL;
         pCheckTtxSlicer = NULL;
         slicerOk = TRUE;
         if ( (acqCtl.autoSlicerType) &&
              (acqCtl.currentSlicerType + 1 < VBI_SLICER_COUNT) )
         {
            if (acqCtl.isTtxSrc)
               pCheckTtxSlicer = &slicerOk;
            else
               pCheckNxtvSlicer = &slicerOk;
         }

         // check if new data is available in the VBI ringbuffer
         // if yes, return TRUE -> trigger high-level processing (e.g. insert blocks to DB)
         result = TtxDecode_CheckForPackets(&stopped);

         if (stopped == FALSE)
         {
            EpgAcqNxtv_ProcessPackets(pCheckNxtvSlicer);
            EpgAcqTtx_ProcessPackets(pCheckTtxSlicer);

            TtxDecoder_ReleasePackets();

            if (slicerOk == FALSE)
            {
               debug3("EpgAcqCtl: upgrading slicer type to #%d (provider #%d of %d)", acqCtl.currentSlicerType + 1, acqCtl.cycleIdx, acqCtl.cniCount);
               acqCtl.currentSlicerType += 1;
               BtDriver_SelectSlicer(acqCtl.currentSlicerType);
            }

            // check timers (acq. stalls, forced passive mode, ...)
            if (acqCtl.isTtxSrc == FALSE)
            {
               acqCtl.advanceCycle = EpgAcqNxtv_MonitorSource();
            }
            else
               acqCtl.advanceCycle = EpgAcqTtx_MonitorSources();
         }
         else
         {  // acquisition was stopped, e.g. due to death of the acq slave
            EpgAcqCtl_Stopped();
         }
      }
      else
      {  // network acq mode -> handle timeouts
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
   return result || acqCtl.advanceCycle;
}

// ---------------------------------------------------------------------------
// Insert newly acquired blocks into the EPG db
// - to be called when ProcessPackets returns TRUE, i.e. EPG blocks in input queue
// - database MUST NOT be locked by GUI
//
void EpgAcqCtl_ProcessBlocks( void )
{
   bool * pNxtvAdvance = NULL;

   if (acqCtl.acqEnabled)
   {
      assert(EpgScan_IsActive() == FALSE);

      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         if ( (acqCtl.advanceCycle == FALSE) &&
              (acqCtl.isTtxSrc == FALSE) &&
              ( (acqCtl.stopPhase != ACQMODE_PHASE_COUNT) ||
                (ACQMODE_IS_CYCLIC(acqCtl.mode) && (acqCtl.cniCount > 1)) ))
         {
            // in cyclic mode and when nxtv is master: ask if acq for current provider is done
            pNxtvAdvance = &acqCtl.advanceCycle;
         }

         // process incoming Nextview EPG blocks: add to database
         EpgAcqNxtv_ProcessBlocks(pNxtvAdvance);

         if (acqCtl.advanceCycle)
         {
            acqCtl.advanceCycle = FALSE;
            EpgAcqCtl_AdvanceCyclePhase();
         }
      }
      else
      {  // network acq mode
         #ifdef USE_DAEMON
         EpgAcqClient_ProcessBlocks();
         #endif

         // once connection is established, network errors are no longer reported by popups
         acqCtl.haveWarnedInpSrc = TRUE;
      }
      //dprintf4("EpgAcqCtl: state=%d, phase=%d, CNI-idx=%d, cyCni=0x%04X\n", acqCtl.state, acqCtl.cyclePhase, acqCtl.cycleIdx, acqCtl.cniTab[acqCtl.cycleIdx]);
   }
}

// ---------------------------------------------------------------------------
// Stop driver and free allocated resources
// - in emergency mode only the driver is stopped and acq is not shut down
//   cleanly; intended for irregular termination only (i.e. crash)
//
void EpgAcqCtl_Destroy( bool isEmergency )
{
   if (isEmergency == FALSE)
   {
      EpgAcqNxtv_Destroy();
      EpgAcqTtx_Destroy();
   }
   BtDriver_Exit();
}

// ---------------------------------------------------------------------------
// Initialize the module
//
void EpgAcqCtl_Init( void )
{
   memset(&acqCtl, 0, sizeof(acqCtl));

   EpgAcqNxtv_Init();
   EpgAcqTtx_Init();

   BtDriver_Init();
}

