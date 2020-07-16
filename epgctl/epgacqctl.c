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
 *  $Id: epgacqctl.c,v 1.95 2009/05/02 19:24:12 tom Exp tom $
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

typedef uint32_t ACQ_VPS_PIL_IND[CNI_TYPE_COUNT];

typedef struct
{
   bool           acqEnabled;
   EPGACQ_MODE    mode;
   EPGACQ_PASSIVE passiveReason;
   EPGACQ_PHASE   cyclePhase;
   EPGACQ_PHASE   stopPhase;
   uint           ttxSrcCount;
   uint           inputSource;
   uint           currentSlicerType;
   bool           autoSlicerType;
   bool           haveWarnedInpSrc;
   bool           advanceCycle;
   ACQ_VPS_PIL_IND  acqCniInd[VPSPDC_REQ_COUNT];
   ACQ_VPS_PIL_IND  acqPilInd[VPSPDC_REQ_COUNT];
   EPG_ACQ_VPS_PDC  acqVpsPdc;
} EPGACQCTL_STATE;

static EPGACQCTL_STATE    acqCtl;


static void EpgAcqCtl_InitCycle( void );
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
         pAcqState->ttxGrabState = ACQDESCR_DISABLED;
      }
      else if (acqCtl.acqEnabled == FALSE)
      {
         memset(pAcqState, 0, sizeof(EPGACQ_DESCR));
         pAcqState->ttxGrabState = ACQDESCR_DISABLED;
      }
      else
      {
         if (acqCtl.mode != ACQMODE_NETWORK)
         {
            pAcqState->mode          = acqCtl.mode;
            pAcqState->passiveReason = acqCtl.passiveReason;
            pAcqState->cyclePhase    = acqCtl.cyclePhase;
            pAcqState->isNetAcq      = FALSE;

            // query grabber states and sources
            EpgAcqTtx_DescribeAcqState(pAcqState);
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
void EpgAcqCtl_GetAcqModeStr( const EPGACQ_DESCR * pAcqState,
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
         default:
            switch (pAcqState->cyclePhase)
            {
               case ACQMODE_PHASE_NOWNEXT:
                  *ppModeStr = "acq. phase 'Now'";
                  break;
               case ACQMODE_PHASE_FULL:
                  *ppModeStr = "acq. phase 'Full'";
                  break;
               case ACQMODE_PHASE_MONITOR:
                  *ppModeStr = "acq. phase 'Complete'";
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
      if (pAcqState->ttxGrabState == ACQDESCR_DISABLED)
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
         EpgAcqTtx_GetAcqStats(&pAcqStats->ttx_grab);
         pAcqStats->lastStatsUpdate = time(NULL);

         // retrieve additional data from TTX packet decoder
         TtxDecode_GetStatistics(0, &pAcqStats->ttx_dec, &ttx_start_t);
         pAcqStats->ttx_duration = pAcqStats->lastStatsUpdate - ttx_start_t;

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
#ifdef USE_DAEMON
         if (acqCtl.mode == ACQMODE_NETWORK)
         {  // retrieve data from network client
            result = EpgAcqClient_GetVpsPdc(pVpsPdc, &acqCtl.acqCniInd[clientId][0]);
         }
         else
#endif
         {
            if (force)
            {
               memset(acqCtl.acqCniInd[clientId], 0, sizeof(acqCtl.acqCniInd[clientId]));
               memset(acqCtl.acqPilInd[clientId], 0, sizeof(acqCtl.acqPilInd[clientId]));
            }
            // poll for new VPS/PDC data
            // if there are results which have not been given yet to the client, return them
            if ( TtxDecode_GetCniAndPil(0, &newCni, &newPil, &cniType,
                                        acqCtl.acqCniInd[clientId], acqCtl.acqPilInd[clientId],
                                        NULL) )
            {
               pVpsPdc->cniType = cniType;
               pVpsPdc->pil = newPil;
               pVpsPdc->cni = newCni;
               result = TRUE;
            }
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
bool EpgAcqCtl_TuneProvider( const EPGACQ_TUNER_PAR * par, EPGACQ_PASSIVE * pMode )
{
   bool isTuner;
   bool result = FALSE;

   assert(acqCtl.mode != ACQMODE_PASSIVE);

   // reset forced-passive state; will be set upon errors below
   acqCtl.passiveReason = ACQPASSIVE_NONE;

   BtDriver_SetChannelProfile(VBI_CHANNEL_PRIO_BACKGROUND);

   // tune onto the provider's channel (before starting acq, to avoid catching false data)
   // always set the input source - may have been changed externally (since we don't hog the video device permanently)
   if ( BtDriver_TuneChannel(acqCtl.inputSource, par, FALSE, &isTuner) )
   {
      if (isTuner)
      {
         dprintf1("EpgAcqCtl-TuneProv: tuned freq %ld\n", par->freq);
         result = TRUE;
      }
      else
      {
         dprintf0("EpgAcqCtl-TuneProv: input is no tuner -> force to passive mode\n");
         acqCtl.passiveReason = ACQPASSIVE_NO_TUNER;

         if (par->freq != 0)
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
      dprintf0("EpgAcqCtl-UpdateProvider: start TTX acq\n");

      result = EpgAcqTtx_Start(acqCtl.mode, acqCtl.cyclePhase);
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
      case ACQMODE_CYCLIC_02:
         acqCtl.cyclePhase = ACQMODE_PHASE_NOWNEXT;
         break;
      case ACQMODE_CYCLIC_2:
         acqCtl.cyclePhase = ACQMODE_PHASE_FULL;
         break;
      case ACQMODE_PASSIVE:
      case ACQMODE_EXTERNAL:
      case ACQMODE_NETWORK:
      default:
         // phase value not used in these modes; set arbritrary value
         acqCtl.cyclePhase = ACQMODE_PHASE_FULL;
         break;
   }

   // limit to max phase (because acq. will stop upon advance from this phase)
   if (acqCtl.cyclePhase > acqCtl.stopPhase)
   {
      acqCtl.cyclePhase = acqCtl.stopPhase;
   }

   // use the first provider in the list
   acqCtl.advanceCycle = FALSE;

   EpgAcqTtx_InitCycle(acqCtl.cyclePhase);
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
   if (acqCtl.cyclePhase >= acqCtl.stopPhase)
   {  // max. phase reached -> stop acq.
      EpgAcqCtl_Stop();
   }
   else
   {  // phase complete for all databases -> switch to next
      // determine next phase
      switch (acqCtl.mode)
      {
         case ACQMODE_CYCLIC_02:
            if (acqCtl.cyclePhase == ACQMODE_PHASE_NOWNEXT)
               acqCtl.cyclePhase = ACQMODE_PHASE_FULL;
            else
               acqCtl.cyclePhase = ACQMODE_PHASE_MONITOR;
            break;
         case ACQMODE_CYCLIC_2:
            acqCtl.cyclePhase = ACQMODE_PHASE_MONITOR;
            break;
         default:
            break;
      }
      dprintf1("EpgAcqCtl: advance to phase #%d\n", acqCtl.cyclePhase);
      // start new phase with first database
      EpgAcqTtx_InitCycle(acqCtl.cyclePhase);
   }

   if (acqCtl.acqEnabled)
   {  // note about parameter "TRUE": open the new db immediately because
      // it may need to be accessed, e.g. to reset rep counters
      EpgAcqCtl_UpdateProvider(TRUE);
   }

   assert(acqCtl.mode < ACQMODE_COUNT);
   assert((acqCtl.passiveReason == ACQPASSIVE_NONE) || (acqCtl.mode != ACQMODE_PASSIVE));
}

// ---------------------------------------------------------------------------
// Select acquisition mode and provider list
//
bool EpgAcqCtl_SelectMode( EPGACQ_MODE newAcqMode, EPGACQ_PHASE maxPhase,
                           uint ttxSrcCount, const char * pTtxNames,
                           const EPGACQ_TUNER_PAR * pTtxFreqs )
{
   bool restart;
   bool result = FALSE;

   if (newAcqMode < ACQMODE_COUNT)
   {
      {
         result = TRUE;

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
              (EpgAcqTtx_CompareParams(ttxSrcCount, pTtxNames, pTtxFreqs) == FALSE) )
         {
            // if network mode was toggled, acq must be completely restarted because different "drivers" are used
            restart = ( ((newAcqMode == ACQMODE_NETWORK) ^ (acqCtl.mode == ACQMODE_NETWORK)) &&
                        (acqCtl.acqEnabled) );
            if (restart)
               EpgAcqCtl_Stop();

            // save the new parameters
            acqCtl.ttxSrcCount      = ttxSrcCount;
            acqCtl.mode             = newAcqMode;
            acqCtl.stopPhase        = maxPhase;

            // pass through teletext params
            EpgAcqTtx_SetParams(ttxSrcCount, pTtxNames, pTtxFreqs);

            // reset acquisition and start with the new parameters
            if (acqCtl.acqEnabled)
            {
               dprintf1("EpgAcqCtl-SelectMode: reset acq with new mode=%d\n", newAcqMode);
               EpgAcqCtl_InitCycle();
               result = EpgAcqCtl_UpdateProvider(TRUE);
            }
            else if (restart)
            {
               dprintf1("EpgAcqCtl-SelectMode: restart acq with new mode=%d\n", newAcqMode);
               EpgAcqCtl_Start();
            }

            // if there was no provider change, at least notify about acq state change
            UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
         }
      }
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
   #ifdef USE_DAEMON
   bool change = FALSE;
   #endif
   bool update = FALSE;

   if ( (acqCtl.acqEnabled) &&
        (acqCtl.mode != ACQMODE_NETWORK) )
   {
      if ( TtxDecode_GetCniAndPil(0, &newCni, &newPil, &cniType,
                                  acqCtl.acqCniInd[VPSPDC_REQ_POLL],
                                  acqCtl.acqPilInd[VPSPDC_REQ_POLL], NULL) )
      {
         if ( (acqCtl.acqVpsPdc.cni != newCni) ||
              ((newPil != acqCtl.acqVpsPdc.pil) && VPS_PIL_IS_VALID(newPil)) )
         {
            acqCtl.acqVpsPdc.cniType = cniType;
            acqCtl.acqVpsPdc.pil = newPil;
            acqCtl.acqVpsPdc.cni = newCni;
            dprintf5("EpgAcqCtl-PollVpsPil: %02d.%02d. %02d:%02d (0x%04X)\n", (newPil >> 15) & 0x1F, (newPil >> 11) & 0x0F, (newPil >>  6) & 0x1F, (newPil      ) & 0x3F, newCni );

            UiControlMsg_AcqEvent(ACQ_EVENT_VPS_PDC);
            #ifdef USE_DAEMON
            change = TRUE;
            #endif
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
   bool * pCheckTtxSlicer;
   bool stopped;
   bool result = FALSE;

   if (acqCtl.acqEnabled)
   {
      if (acqCtl.mode != ACQMODE_NETWORK)
      {
         // check if the current slicer type is adequate
         pCheckTtxSlicer = NULL;
         slicerOk = TRUE;
         if ( (acqCtl.autoSlicerType) &&
              (acqCtl.currentSlicerType + 1 < VBI_SLICER_COUNT) )
         {
            pCheckTtxSlicer = &slicerOk;
         }

         // check if new data is available in the VBI ringbuffer
         // if yes, return TRUE -> trigger high-level processing (e.g. insert blocks to DB)
         result = TtxDecode_CheckForPackets(&stopped);

         if (stopped == FALSE)
         {
            EpgAcqTtx_ProcessPackets(pCheckTtxSlicer);

            TtxDecoder_ReleasePackets();

            if (slicerOk == FALSE)
            {
               debug1("EpgAcqCtl: upgrading slicer type to #%d", acqCtl.currentSlicerType + 1);
               acqCtl.currentSlicerType += 1;
               BtDriver_SelectSlicer(acqCtl.currentSlicerType);
            }

            // check timers (acq. stalls, forced passive mode, ...)
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
   if (acqCtl.acqEnabled)
   {
      assert(EpgScan_IsActive() == FALSE);

      if (acqCtl.mode != ACQMODE_NETWORK)
      {
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
      //dprintf2("EpgAcqCtl: state=%d, phase=%d\n", acqCtl.state, acqCtl.cyclePhase);
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

   EpgAcqTtx_Init();

   BtDriver_Init();
}

