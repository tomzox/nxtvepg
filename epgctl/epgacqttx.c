/*
 *  Teletext EPG acquisition control
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
 *    This module controls the Teletext EPG grabbing process. This
 *    covers cycling through the channel table, controlling the teletext
 *    packet grabber and post-processing, monitoring for channel changes
 *    and monitoring the acquisition progress.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgacqttx.c,v 1.4 2011/01/09 18:02:36 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgdb/ttxgrab.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"

#include "epgctl/epgctxctl.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqttx.h"
#include "epgui/uictrl.h"
#include "epgui/epgmain.h"


// ---------------------------------------------------------------------------
// Declaration of module state variables
//
typedef enum
{
   TTXACQ_STATE_OFF,               // acquisition disabled
   TTXACQ_STATE_STARTUP,           // now-scan / wait for page header
   TTXACQ_STATE_GRAB,              // grab teletext (wait for timeout or 'til range done)
   TTXACQ_STATE_GRAB_PASSIVE,      // passive capture
   TTXACQ_STATE_IDLE,              // passive done or channel unknown
} TTXACQ_STATE;

typedef struct
{
   bool           srcDone;
   time_t         predictStart;
   time_t         mtime;
   EPGACQ_TUNER_PAR freq;
} TTXACQ_SOURCE;

typedef struct
{
   TTXACQ_STATE   state;
   EPGACQ_MODE    mode;
   EPGACQ_PASSIVE passiveReason;
   EPGACQ_PHASE   cyclePhase;
   uint           ttxSrcCount;
   uint           ttxSrcIdx[MAX_VBI_DVB_STREAMS];
   uint           ttxActiveCount;
   time_t         acqStartTime;
   time_t         chanChangeTime;
   bool           allSrcDone;
   sint           lastDoneSrc;
   time_t         lastDoneTime;
   char          *pTtxNames;
   TTXACQ_SOURCE *pSources;
   uint           acqStartPg;
   uint           acqEndPg;
   uint           acqDuration;
} EPGACQTTX_CTL;

static EPGACQTTX_CTL  acqCtl;

#define TTX_START_TIMEOUT_NOW         5
#define TTX_START_TIMEOUT_FULL       15
#define TTX_START_TIMEOUT_IDLE       60

#define TTX_GRAB_TIMEOUT_SCAN        20
#define TTX_GRAB_TIMEOUT_NOW         60
#define TTX_GRAB_MIN_DURATION        30
#define TTX_GRAB_TIMEOUT_IDLE       (30*60)

#define TTX_FORCED_PASV_INTV         20
#define TTX_SLICER_CHECK_INTV        20

// number of seconds without TTX header after which acq is considered "stalled"
#define ACQ_DESCR_TTX_STALLED_TIMEOUT  7

#define CMP_DVB_FREQ(F) (((F) + EPGACQ_TUNER_DVB_FREQ_TOL/2) / EPGACQ_TUNER_DVB_FREQ_TOL)

#ifdef USE_TTX_GRABBER
static bool EpgAcqTtx_UpdateProvider( void );
static bool EpgAcqTtx_DetectSource( void );

// ---------------------------------------------------------------------------
// Helper function which looks up a channel's XML file name
//
static const char * EpgAcqTtx_GetChannelName( uint srcIdx )
{
   const char * pNames = "";
   uint idx;

   if (acqCtl.pTtxNames != NULL)
   {
      if (srcIdx < acqCtl.ttxSrcCount)
      {
         pNames = acqCtl.pTtxNames;
         for (idx = 0; idx < srcIdx; idx++)
            while(*(pNames++) != 0)
               ;
      }
      else
         debug2("EpgAcqTtx-GetChannelName: invalid idx %d (>=%d)", srcIdx, acqCtl.ttxSrcCount);
   }

   return pNames;
}
#endif

// ---------------------------------------------------------------------------
// Determine state of acquisition
//
void EpgAcqTtx_DescribeAcqState( EPGACQ_DESCR * pAcqState )
{
   uint idx;
   time_t now = time(NULL);

   switch (acqCtl.state)
   {
      case TTXACQ_STATE_OFF:
         pAcqState->ttxGrabState = ACQDESCR_DISABLED;
         break;

      case TTXACQ_STATE_STARTUP:
         if (now - acqCtl.chanChangeTime > ACQ_DESCR_TTX_STALLED_TIMEOUT)
            pAcqState->ttxGrabState = ACQDESCR_NO_RECEPTION;
         else if (acqCtl.cyclePhase == ACQMODE_PHASE_NOWNEXT)
            pAcqState->ttxGrabState = ACQDESCR_TTX_PG_SEQ_SCAN;
         else
            pAcqState->ttxGrabState = ACQDESCR_STARTING;
         break;

      case TTXACQ_STATE_GRAB:
      case TTXACQ_STATE_GRAB_PASSIVE:
         pAcqState->ttxGrabState = ACQDESCR_RUNNING;
         break;

      case TTXACQ_STATE_IDLE:
         pAcqState->ttxGrabState = ACQDESCR_IDLE;
         break;
   }

   if (acqCtl.state != TTXACQ_STATE_OFF)
   {
      pAcqState->ttxSrcCount = acqCtl.ttxSrcCount;
      pAcqState->ttxGrabCount = acqCtl.ttxActiveCount;

      for (idx = 0; idx < acqCtl.ttxActiveCount; idx++)
         pAcqState->ttxGrabIdx[idx] = acqCtl.ttxSrcIdx[idx];

      pAcqState->ttxGrabDone = 0;
      for (idx = 0; idx < acqCtl.ttxSrcCount; idx++)
         if (acqCtl.pSources[idx].srcDone)
            pAcqState->ttxGrabDone += 1;
   }
}

// ---------------------------------------------------------------------------
// Return complete set of acq state and statistic values
// - used by "View acq statistics" popup window
//
bool EpgAcqTtx_GetAcqStats( EPG_ACQ_STATS * pAcqStats )
{
   bool result = FALSE;

#ifdef USE_TTX_GRABBER
   if (acqCtl.state != TTXACQ_STATE_OFF)
   {
      time_t decStartTime;

      pAcqStats->lastStatsUpdate = time(NULL);
      pAcqStats->acqStartTime    = acqCtl.acqStartTime;
      pAcqStats->acqDuration     = pAcqStats->lastStatsUpdate - decStartTime;

      TtxGrab_GetStatistics(0, &pAcqStats->pkgStats);

      // retrieve additional data from TTX packet decoder
      TtxDecode_GetStatistics(0, &pAcqStats->ttx_dec, &decStartTime);

      pAcqStats->srcIdx = acqCtl.ttxSrcIdx[0];

      strncpy(pAcqStats->srcName, EpgAcqTtx_GetChannelName(acqCtl.ttxSrcIdx[0]), EPG_TTX_STATS_NAMLEN - 1);
      pAcqStats->srcName[EPG_TTX_STATS_NAMLEN - 1] = 0;

      result = TRUE;
   }
#endif
   return result;
}

#ifdef USE_TTX_GRABBER
// ---------------------------------------------------------------------------
// Start teletext decoding and initialize the EPG stream decoder
//
static bool EpgAcqTtx_TtxStart( void )
{
   bool result = FALSE;

   dprintf1("EpgAcqTtx-TtxStart: active:%d\n", acqCtl.ttxActiveCount);

   // TODO: use generic params if source is yet unknown
   if (acqCtl.ttxActiveCount > 0)
   {
      result = TtxGrab_Start(acqCtl.acqStartPg, acqCtl.acqEndPg, TRUE);
      TtxDecode_StartTtxAcq(TRUE, acqCtl.acqStartPg, acqCtl.acqEndPg);
   }
   else
   {
      result = TtxGrab_Start(0x100, 0x100, FALSE);
      TtxDecode_StartTtxAcq(TRUE, 0x100, 0x100);
   }

#ifdef USE_DAEMON
   EpgAcqServer_TriggerStats();
#endif
   return result;
}

// ---------------------------------------------------------------------------
// Stop teletext decoding and clear the EPG stream and its queue
//
static void EpgAcqTtx_TtxStop( void )
{
   TtxDecode_StopTtxAcq();
   TtxGrab_Stop();
}
#endif // USE_TTX_GRABBER

// ---------------------------------------------------------------------------
// Start the acquisition
//
bool EpgAcqTtx_Start( EPGACQ_MODE mode, EPGACQ_PHASE cyclePhase )
{
   bool result = FALSE;

#ifdef USE_TTX_GRABBER
   if (acqCtl.ttxSrcCount > 0)
   {
      dprintf2("EpgAcqTtx-Start: starting acquisition phase %d, mode %d\n", cyclePhase, mode);

      acqCtl.mode       = mode;
      acqCtl.cyclePhase = cyclePhase;
      // note ttxSrcIdx is initialized separately

      acqCtl.acqStartTime = time(NULL);
      acqCtl.chanChangeTime = acqCtl.acqStartTime;
      acqCtl.state = TTXACQ_STATE_STARTUP;

      // set input source and tuner frequency (also detect if device is busy)
      EpgAcqTtx_UpdateProvider();
      result = EpgAcqTtx_TtxStart();

      UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
   }
#endif // USE_TTX_GRABBER

   return result;
}

// ---------------------------------------------------------------------------
// Stop the acquisition
//
void EpgAcqTtx_Stop( void )
{
#ifdef USE_TTX_GRABBER
   if (acqCtl.state != TTXACQ_STATE_OFF)
   {
      dprintf0("EpgAcqTtx-Stop: stopping acquisition\n");

      EpgAcqTtx_TtxStop();

      acqCtl.state = TTXACQ_STATE_OFF;
   }
#endif
}

// ---------------------------------------------------------------------------
// Stop acquisition for the EPG scan but keep the device open/driver loaded
//
void EpgAcqTtx_Suspend( void )
{
#ifdef USE_TTX_GRABBER
   EpgAcqTtx_Stop();
#endif
}

#ifdef USE_TTX_GRABBER
// ---------------------------------------------------------------------------
// Tune the selected provider & piggy-back channels on the same transponder
//
static void EpgAcqTtx_TunePid( void )
{
   const EPGACQ_TUNER_PAR * par = &acqCtl.pSources[acqCtl.ttxSrcIdx[0]].freq;
   int pidList[MAX_VBI_DVB_STREAMS];
   uint pidCount = 1;

   pidList[0] = par->ttxPid;

   if (acqCtl.ttxActiveCount == 1)
   {
      for (uint idx = 0; (idx < acqCtl.ttxSrcCount) && (pidCount < MAX_VBI_DVB_STREAMS); ++idx)
      {
         if (   (acqCtl.pSources[idx].freq.freq == par->freq)
             && (idx != acqCtl.ttxSrcIdx[0]) )
         {
            dprintf2("EpgAcqTtx-TunePid: piggy-backing channel %d (%s)\n", idx, EpgAcqTtx_GetChannelName(idx));
            pidList[pidCount] = acqCtl.pSources[idx].freq.ttxPid;
            acqCtl.ttxSrcIdx[pidCount] = idx;
            pidCount += 1;
         }
      }
      acqCtl.ttxActiveCount = pidCount;
   }
   BtDriver_TuneDvbPid(pidList, pidCount);
}

// ---------------------------------------------------------------------------
// Switch database and TV channel to the current acq provider & handle errors
// - called upon start of acquisition or configuration changes (db or acq mode)
// - called upon the start of a cycle phase for one db, or to get out of
//   forced passive mode
//
static bool EpgAcqTtx_UpdateProvider( void )
{
   bool result = FALSE;
   time_t now = time(NULL);

   if ( (acqCtl.mode != ACQMODE_PASSIVE) && (acqCtl.ttxActiveCount > 0) )
   {
      const EPGACQ_TUNER_PAR * par = &acqCtl.pSources[acqCtl.ttxSrcIdx[0]].freq;
      if (par->freq != 0)
      {
         if ( (acqCtl.mode != ACQMODE_PASSIVE) &&
              ((acqCtl.passiveReason == ACQPASSIVE_NONE) || (acqCtl.passiveReason != ACQPASSIVE_NO_TUNER)) )
         {
            acqCtl.chanChangeTime = now;

            result = EpgAcqCtl_TuneProvider(par, &acqCtl.passiveReason);

            if (result)
            {
               // request PID for selected channel and others sharing frequency
               EpgAcqTtx_TunePid();
            }
         }
      }
      else
      {  // no channel to be tuned onto -> set at least the input source
         acqCtl.chanChangeTime = now;

         result = EpgAcqCtl_TuneProvider(par, &acqCtl.passiveReason);
      }
   }
   else
   {  // passive mode
      acqCtl.chanChangeTime = now;
   }

   acqCtl.state = TTXACQ_STATE_STARTUP;
   if (result)
   {
      acqCtl.acqStartTime = now;
   }
   else
   {
      acqCtl.ttxActiveCount = 0;
      EpgAcqTtx_DetectSource();
   }

   return result;
}
#endif // USE_TTX_GRABBER

// ---------------------------------------------------------------------------
// Initialize the state machine which cycles through TTX sources
// - called upon start and when switching from Nextview to teletext
//
void EpgAcqTtx_InitCycle( EPGACQ_PHASE phase )
{
   uint idx;

   for (idx = 0; idx < acqCtl.ttxSrcCount; idx++)
   {
      acqCtl.pSources[idx].srcDone = FALSE;
      acqCtl.pSources[idx].predictStart = 0;
   }
   acqCtl.ttxSrcIdx[0] = 0;
   acqCtl.ttxActiveCount = 1;
   acqCtl.allSrcDone = FALSE;
   acqCtl.lastDoneSrc = -1;
   acqCtl.lastDoneTime = 0;
}

#ifdef USE_TTX_GRABBER
// ---------------------------------------------------------------------------
// Search source table for next unfinished source
// - skip sources which already have been processed (done set to TRUE)
// - return -1 if all sources have been done
// - always start at the beginning to work in order of user priority
//
static sint EpgAcqTtx_GetNextSrc( sint ttxSrcIdx )
{
   uint idx;
   time_t now = time(NULL);
   sint result = -1;

   for (idx = 0; idx < acqCtl.ttxSrcCount; idx++)
   {
      if ( (acqCtl.pSources[idx].srcDone == FALSE) &&
           (now + 1 <= acqCtl.pSources[idx].predictStart) &&
           (now + 2 >= acqCtl.pSources[idx].predictStart) )
      {
         result = idx;
         break;
      }
   }
   if (result == -1)
   {
      uint skipCount = 0;
      do
      {
         if (ttxSrcIdx + 1 < acqCtl.ttxSrcCount)
            ttxSrcIdx = ttxSrcIdx + 1;
         else
            ttxSrcIdx = 0;

         if (acqCtl.pSources[ttxSrcIdx].srcDone == FALSE)
         {
            result = ttxSrcIdx;
            break;
         }
         skipCount += 1;
      } while (skipCount < acqCtl.ttxSrcCount);
   }

   return result;
}

// ---------------------------------------------------------------------------
// Try to determine current source in (forced) passive mode
//
static bool EpgAcqTtx_DetectSource( void )
{
   EPGACQ_TUNER_PAR par;
   uint input;
   bool isTuner;
   uint idx;
   bool result = FALSE;

   if (BtDriver_QueryChannel(&par, &input, &isTuner))
   {
      if (isTuner)
      {
         for (idx = 0; idx < acqCtl.ttxSrcCount; idx++)
            if (   (par.norm == acqCtl.pSources[idx].freq.norm)
                && (  (par.norm == EPGACQ_TUNER_NORM_DVB)
                    ? (CMP_DVB_FREQ(acqCtl.pSources[idx].freq.freq) == CMP_DVB_FREQ(par.freq))
                    : (acqCtl.pSources[idx].freq.freq == par.freq) ))
               break;

         if (idx < acqCtl.ttxSrcCount)
         {
            dprintf3("EpgAcqTtx-DetectSource: freq %ld -> src=%d (%s)\n", par.freq, idx, EpgAcqTtx_GetChannelName(idx));
            acqCtl.ttxActiveCount = 1;
            acqCtl.ttxSrcIdx[0] = idx;
            // request PID for selected channel and others sharing frequency
            EpgAcqTtx_TunePid();
            result = TRUE;
         }
         else
         {
            dprintf1("EpgAcqTtx-DetectSource: freq %ld unknown\n", par.freq);
            acqCtl.ttxActiveCount = 0;
         }
      }
      else
         dprintf0("EpgAcqTtx-DetectSource: source is not tuner\n");
   }
   else
      debug0("EpgAcqTtx-DetectSource: failed to query freq from driver");

   return result;
}

// ---------------------------------------------------------------------------
// Check if the requested channel is still tuned
//
static bool EpgAcqTtx_CheckSourceFreq( uint idx )
{
   EPGACQ_TUNER_PAR par;
   uint input;
   bool isTuner;
   bool result = FALSE;

   if (idx < acqCtl.ttxSrcCount)
   {
      if (BtDriver_QueryChannel(&par, &input, &isTuner))
      {
         if (isTuner)
         {
            if (par.norm == EPGACQ_TUNER_NORM_DVB)
               result = (CMP_DVB_FREQ(acqCtl.pSources[idx].freq.freq) == CMP_DVB_FREQ(par.freq));
            else
               result = (acqCtl.pSources[idx].freq.freq == par.freq);

            ifdebug3(!result, "EpgAcqTtx-CheckSourceFreq: freq %ld mismatch to freq %ld src=%d", par.freq, acqCtl.pSources[idx].freq.freq, idx);
         }
      }
      else
         debug0("EpgAcqTtx-CheckSourceFreq: failed to query freq from driver");
   }
   else
      debug2("EpgAcqTtx-CheckSourceFreq: invalid idx=%d (>=%d)", idx, acqCtl.ttxSrcCount);

   return result;
}
#endif // USE_TTX_GRABBER

// ---------------------------------------------------------------------------
// Handle external channel changes
// - note: NOT called upon internally controlled channel changes
//
void EpgAcqTtx_ChannelChange( void )
{
#ifdef USE_TTX_GRABBER
   time_t now;

   if (acqCtl.state != TTXACQ_STATE_OFF)
   {
      if ( (acqCtl.state == TTXACQ_STATE_GRAB) ||
           (acqCtl.state == TTXACQ_STATE_GRAB_PASSIVE) )
      {
         assert(acqCtl.ttxActiveCount > 0);

         now = time(NULL);
         if (now >= acqCtl.chanChangeTime + TTX_GRAB_MIN_DURATION)
         {
            for (uint idx = 0; idx < acqCtl.ttxActiveCount; ++idx)
            {
               dprintf2("EpgAcqTtx-ChannelChange: post-processing %d (%s)\n", acqCtl.ttxSrcIdx[idx], EpgAcqTtx_GetChannelName(acqCtl.ttxSrcIdx[idx]));
               TtxGrab_PostProcess(idx, EpgAcqTtx_GetChannelName(acqCtl.ttxSrcIdx[idx]), TRUE);
            }
         }
      }

      // restart acquisition
      acqCtl.acqStartTime = time(NULL);
      acqCtl.chanChangeTime = acqCtl.acqStartTime;
      acqCtl.state = TTXACQ_STATE_STARTUP;
      EpgAcqTtx_DetectSource();

      EpgAcqTtx_TtxStart();
      UiControlMsg_AcqEvent(ACQ_EVENT_CTL);
   }
#endif // USE_TTX_GRABBER
}

#ifdef USE_TTX_GRABBER
// ---------------------------------------------------------------------------
// Check if teletext pages are in range on any of the monitored channels
//
static bool EpgAcqTtx_MonitorInRange( time_t now, bool * pLock )
{
   bool anyLock = FALSE;
   bool result = FALSE;

   for (uint idx = 0; idx < acqCtl.ttxActiveCount; ++idx)
   {
      bool inRange;
      bool lock;
      uint predictDelay;

      TtxGrab_GetPageStats(idx, &inRange, NULL, &lock, &predictDelay);
      anyLock |= lock;
      result |= inRange;

      if ((predictDelay > 0) && (predictDelay <= 15))
         acqCtl.pSources[acqCtl.ttxSrcIdx[idx]].predictStart = now + predictDelay;
      else
         acqCtl.pSources[acqCtl.ttxSrcIdx[idx]].predictStart = now;
   }
   *pLock = anyLock;
   return result;
}

// ---------------------------------------------------------------------------
// Check if teletext pages are done once on all monitored channels
//
static bool EpgAcqTtx_MonitorRangeDone( void )
{
   bool rangeDone;
   bool result = TRUE;

   for (uint idx = 0; idx < acqCtl.ttxActiveCount; ++idx)
   {
      TtxGrab_GetPageStats(idx, NULL, &rangeDone, NULL, NULL);
      result &= rangeDone;
   }
   return result;
}
#endif // USE_TTX_GRABBER

// ---------------------------------------------------------------------------
// Monitor acquisition progress
// - Now&Next mode: switch to next source if target page range is not near
// - check timeouts (no packets received; page range complete)
// - determine slicer type on all pages (i.e. also outside of page range)
//   (needs faster adjustment than nxtv. to support Now&Next scan)
// - returns TRUE when all teletext sources are done (advance the cycle phase)
//   Note this differs from the Nextview state machine: here we advance only
//   after ALL teletext sources are done, i.e. switching between teletext
//   sources is handled internally (since it's not always sequential)
//
bool EpgAcqTtx_MonitorSources( void )
{
#ifdef USE_TTX_GRABBER
   sint ttxSrcIdx;
   time_t now = time(NULL);
   bool advance = FALSE;

   // FIXME obsolete - move code to call of TtxGrab_PostProcess()
   if (TtxGrab_CheckPostProcess(0))
   {
#ifdef USE_DAEMON
      // trigger the server to notify all connected clients
      EpgAcqServer_TriggerDbUpdate(time(NULL));
#endif
      UiControlMsg_AcqEvent(ACQ_EVENT_NEW_DB);
   }

   if (acqCtl.state == TTXACQ_STATE_STARTUP)
   {
      bool lock;
      bool inRange = EpgAcqTtx_MonitorInRange(now, &lock);

      if ( (acqCtl.mode != ACQMODE_PASSIVE) &&
           (acqCtl.passiveReason == ACQPASSIVE_NONE) )
      {
         assert(acqCtl.ttxActiveCount > 0);
         assert(acqCtl.ttxSrcIdx[0] < acqCtl.ttxSrcCount);

         if (acqCtl.cyclePhase == ACQMODE_PHASE_NOWNEXT)
         {
            // NOW mode: query page sequence prediction
            if ( lock && (inRange == FALSE) &&
                 ((now < acqCtl.lastDoneTime + TTX_GRAB_TIMEOUT_SCAN) || (acqCtl.lastDoneTime == 0)) )
            {
               dprintf3("EpgAcqTtx-MonitorSources: SKIP channel %d (%s) delay:%d\n",
                        acqCtl.ttxSrcIdx[0], EpgAcqTtx_GetChannelName(acqCtl.ttxSrcIdx[0]),
                        (uint)(acqCtl.pSources[acqCtl.ttxSrcIdx[0]].predictStart - now));
               advance = TRUE;
            }
            else if (lock)
            {
               if (EpgAcqTtx_CheckSourceFreq(acqCtl.ttxSrcIdx[0]))
               {
                  dprintf4("EpgAcqTtx-MonitorSources: load NOW from channel %d (%s) inRange?:%d timeout?:%d\n",
                           acqCtl.ttxSrcIdx[0], EpgAcqTtx_GetChannelName(acqCtl.ttxSrcIdx[0]),
                           inRange, (now >= acqCtl.lastDoneTime + TTX_GRAB_TIMEOUT_SCAN));
                  acqCtl.state = TTXACQ_STATE_GRAB;
                  UiControlMsg_AcqEvent(ACQ_EVENT_CTL);
               }
               else
                  EpgAcqTtx_ChannelChange();
            }
            else if (now >= acqCtl.chanChangeTime + TTX_START_TIMEOUT_NOW)
            {
               debug3("EpgAcqTtx-MonitorSources: no reception on %d PIDs, first:%d (%s) - mark DONE", acqCtl.ttxActiveCount, acqCtl.ttxSrcIdx[0], EpgAcqTtx_GetChannelName(acqCtl.ttxSrcIdx[0]));
               for (uint idx = 0; idx < acqCtl.ttxActiveCount; ++idx)
                  acqCtl.pSources[acqCtl.ttxSrcIdx[idx]].srcDone = TRUE;
               advance = TRUE;
            }
         }
         else
         {
            if (lock)
            {
               if (EpgAcqTtx_CheckSourceFreq(acqCtl.ttxSrcIdx[0]))
               {
                  acqCtl.state = TTXACQ_STATE_GRAB;
                  UiControlMsg_AcqEvent(ACQ_EVENT_CTL);
               }
               else
                  EpgAcqTtx_ChannelChange();
            }
            else if (now >= acqCtl.chanChangeTime + TTX_START_TIMEOUT_FULL)
            {
               debug3("EpgAcqTtx-MonitorSources: no reception on %d PIDs, first:%d (%s) - mark DONE", acqCtl.ttxActiveCount, acqCtl.ttxSrcIdx[0], EpgAcqTtx_GetChannelName(acqCtl.ttxSrcIdx[0]));
               for (uint idx = 0; idx < acqCtl.ttxActiveCount; ++idx)
                  acqCtl.pSources[acqCtl.ttxSrcIdx[idx]].srcDone = TRUE;
               advance = TRUE;
            }
         }
      }
      else
      {  // passive mode
         if (lock)
         {
            if (acqCtl.ttxActiveCount == 0)
            {  // channel yet unknown -> try to detect by frequency
               if ( EpgAcqTtx_DetectSource() )
               {  // identified -> restart acq with correct parameters
                  acqCtl.acqStartTime = time(NULL);
                  EpgAcqTtx_TtxStart();
               }
               else
               {  // unknown
                  acqCtl.state = TTXACQ_STATE_IDLE;
               }
            }
            else
            {  // channel known (i.e. we restarted after a frequency match - see above)
               // check if frequency still matches
               if (EpgAcqTtx_CheckSourceFreq(acqCtl.ttxSrcIdx[0]))
               {
                  // match -> enable acquisition
                  dprintf2("EpgAcqTtx-MonitorSources: load passive from channel %d (%s)\n", acqCtl.ttxSrcIdx[0], EpgAcqTtx_GetChannelName(acqCtl.ttxSrcIdx[0]));
                  acqCtl.state = TTXACQ_STATE_GRAB_PASSIVE;
                  UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
               }
               else
               {  // frequency mismatch -> restart
                  EpgAcqTtx_UpdateProvider();
                  EpgAcqTtx_TtxStart();
               }
            }
         }
         else if (now >= acqCtl.chanChangeTime + TTX_START_TIMEOUT_IDLE)
         {
            // reset acquisition to prevent getting a huge output file
            EpgAcqTtx_TtxStart();
         }
      }
   }
   else if ( (acqCtl.state == TTXACQ_STATE_GRAB) ||
             (acqCtl.state == TTXACQ_STATE_GRAB_PASSIVE) )
   {
      assert(acqCtl.ttxActiveCount > 0);
      assert(acqCtl.ttxSrcIdx[0] < acqCtl.ttxSrcCount);

      if (acqCtl.cyclePhase == ACQMODE_PHASE_NOWNEXT)
      {
         bool allDone = EpgAcqTtx_MonitorRangeDone();

         if (allDone || (now >= acqCtl.chanChangeTime + TTX_GRAB_TIMEOUT_NOW))
         {
            advance = TRUE;
         }
         //if ((inRange == FALSE) && (pageCount <= 1))
         // -> revert decisition to stay on this channel -> advance
      }
      else
      {
         advance = (now >= acqCtl.chanChangeTime + acqCtl.acqDuration);
      }
   }
   else if (acqCtl.state == TTXACQ_STATE_IDLE)
   {
      advance = (now >= acqCtl.chanChangeTime + TTX_GRAB_TIMEOUT_IDLE);
   }
   else
      fatal1("EpgAcqTtx-MonitorSources: invalid acq state %d", acqCtl.state);

   if (advance)
   {
      if ( (acqCtl.state == TTXACQ_STATE_GRAB) ||
           (acqCtl.state == TTXACQ_STATE_GRAB_PASSIVE) )
      {
         for (uint idx = 0; idx < acqCtl.ttxActiveCount; ++idx)
         {
            uint srcIdx = acqCtl.ttxSrcIdx[idx];
            dprintf2("EpgAcqTtx-MonitorSources: post-processing %d (%s)\n", srcIdx, EpgAcqTtx_GetChannelName(srcIdx));
            TtxGrab_PostProcess(idx, EpgAcqTtx_GetChannelName(srcIdx), TRUE);
            acqCtl.pSources[srcIdx].srcDone = TRUE;
            acqCtl.lastDoneSrc = srcIdx;
         }
         acqCtl.lastDoneTime = time(NULL);
      }
      if (acqCtl.mode != ACQMODE_PASSIVE)
      {
         if ( (acqCtl.cyclePhase == ACQMODE_PHASE_NOWNEXT) &&
              (acqCtl.pSources[acqCtl.ttxSrcIdx[0]].srcDone == FALSE) )
            ttxSrcIdx = EpgAcqTtx_GetNextSrc(acqCtl.ttxSrcIdx[0]);
         else // note: -1 to restore normal order after passive mode
            ttxSrcIdx = EpgAcqTtx_GetNextSrc(-1);

         if ((acqCtl.ttxActiveCount > 0) && (ttxSrcIdx == acqCtl.ttxSrcIdx[0]))
         {
            // same as before - nothing to do
         }
         else if (ttxSrcIdx >= 0)
         {
            dprintf5("EpgAcqTtx-MonitorSources: switch from %d (%s) (done?:%d) to next channel %d (%s)\n",
                     acqCtl.ttxSrcIdx[0], EpgAcqTtx_GetChannelName(acqCtl.ttxSrcIdx[0]),
                     acqCtl.pSources[acqCtl.ttxSrcIdx[0]].srcDone,
                     ttxSrcIdx, EpgAcqTtx_GetChannelName(ttxSrcIdx));
            acqCtl.ttxSrcIdx[0] = ttxSrcIdx;
            acqCtl.ttxActiveCount = 1;

            EpgAcqTtx_UpdateProvider();
            EpgAcqTtx_TtxStart();

            UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
         }
         else
         {
            acqCtl.allSrcDone = TRUE;
            acqCtl.ttxActiveCount = 0;
            // fail-safe: enter "idle" state until top-level scheduler resets us
            acqCtl.chanChangeTime = time(NULL);
            acqCtl.state = TTXACQ_STATE_IDLE;
            EpgAcqTtx_TtxStart();
         }
      }
      else
      {  // passive capture done -> disable capturing
         acqCtl.chanChangeTime = time(NULL);
         acqCtl.state = TTXACQ_STATE_IDLE;
         acqCtl.ttxActiveCount = 0;
         EpgAcqTtx_TtxStart();
         UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
      }
   }
#endif // USE_TTX_GRABBER

   return acqCtl.allSrcDone;
}

// ---------------------------------------------------------------------------
// Process TTX packets in the VBI ringbuffer
// - called about once a second (even if no packets were captured)
// - checks for unsolicited channel changes (by comparing teletext headers)
//
bool EpgAcqTtx_ProcessPackets( bool * pCheckSlicer )
{
#ifdef USE_TTX_GRABBER
   time_t now;

   if (acqCtl.state != TTXACQ_STATE_OFF)
   {
      uint chnChanged = 0;

      for (uint idx = 0; idx < acqCtl.ttxActiveCount; ++idx)
      {
         if (TtxGrab_ProcessPackets(idx) == FALSE)
            chnChanged += 1;
      }

      if (0) // TODO DVB
      {
         dprintf0("EpgAcqTtx-ProcessPackets: uncontrolled channel change detected\n");
         EpgAcqTtx_ChannelChange();
      }

      // check if the current slicer type is adequate
      if (pCheckSlicer != NULL)
      {
         now = time(NULL);
         // FIXME: should be less for NOW mode
         if ( (now >= acqCtl.chanChangeTime + TTX_SLICER_CHECK_INTV) &&
              (TtxGrab_CheckSlicerQuality(0) == FALSE) )
         {
            *pCheckSlicer = FALSE;
         }
      }
   }
#endif // USE_TTX_GRABBER

   return FALSE;
}

// ---------------------------------------------------------------------------
// Copy parameters
// - can be already called before acquisition starts, i.e. params remain valid
//   across acquisition stop and re-enable
//
void EpgAcqTtx_SetParams( uint ttxSrcCount, const char * pTtxNames, const EPGACQ_TUNER_PAR * pTtxFreqs,
                          uint ttxStartPg, uint ttxEndPg, uint ttxDuration )
{
#ifdef USE_TTX_GRABBER
   uint idx;
   const char * pNames;

   if (acqCtl.pTtxNames != NULL)
   {
      xfree(acqCtl.pTtxNames);
      acqCtl.pTtxNames = NULL;
   }
   if (acqCtl.pSources != NULL)
   {
      xfree(acqCtl.pSources);
      acqCtl.pSources = NULL;
   }
   dprintf5("EpgAcqTtx-SetParams: pg:%03X-%03X dur:%d srcCnt:%d (was:%d)\n",
            ttxStartPg, ttxEndPg, ttxDuration, ttxSrcCount, acqCtl.ttxSrcCount);

   acqCtl.acqStartPg  = ttxStartPg;
   acqCtl.acqEndPg    = ttxEndPg;
   acqCtl.acqDuration = ttxDuration;

   acqCtl.ttxSrcCount = ttxSrcCount;
   if (ttxSrcCount != 0)
   {
      // determine total length of name table
      pNames = pTtxNames;
      for (idx = 0; idx < ttxSrcCount; idx++)
         while(*(pNames++) != 0)
            ;
      // copy names
      acqCtl.pTtxNames = xmalloc(pNames - pTtxNames);
      memcpy(acqCtl.pTtxNames, pTtxNames, (pNames - pTtxNames));

      // copy frequency table
      acqCtl.pSources = xmalloc(ttxSrcCount * sizeof(*acqCtl.pSources));
      for (idx = 0; idx < ttxSrcCount; idx++)
      {
         acqCtl.pSources[idx].freq = pTtxFreqs[idx];
         acqCtl.pSources[idx].mtime = 0; //TODO
      }
   }

   // reset state if incompatible with new params
   if (acqCtl.state != TTXACQ_STATE_OFF)
   {
      if (acqCtl.ttxSrcIdx[0] >= acqCtl.ttxSrcCount)
         acqCtl.ttxActiveCount = 0;
      if (acqCtl.ttxSrcCount == 0)
         acqCtl.mode = ACQMODE_PASSIVE;
   }
#endif // USE_TTX_GRABBER
}

// ---------------------------------------------------------------------------
// Check if parameters have changed
// - used to check if acq must be reset for parameters changes while acq is running
// - returns FALSE upon changes
//
bool EpgAcqTtx_CompareParams( uint ttxSrcCount, const char * pTtxNames, const EPGACQ_TUNER_PAR * pTtxFreqs,
                              uint ttxStartPg, uint ttxEndPg, uint ttxDuration )
{
#ifdef USE_TTX_GRABBER
   const char * pNames;
   uint idx;
   bool change;

   if ( (acqCtl.acqStartPg  == ttxStartPg) &&
        (acqCtl.acqEndPg    == ttxEndPg) &&
        (acqCtl.acqDuration == ttxDuration) &&
        (acqCtl.ttxSrcCount == ttxSrcCount) )
   {
      if (ttxSrcCount > 0)
      {
         assert((pTtxNames != NULL) && (pTtxFreqs != NULL));
         assert((acqCtl.pTtxNames != NULL) && (acqCtl.pSources != NULL));

         // compare frequencies
         change = FALSE;
         for (idx = 0; (idx < ttxSrcCount) && !change; idx++)
            if ((pTtxFreqs[idx].freq != acqCtl.pSources[idx].freq.freq) ||
                (pTtxFreqs[idx].ttxPid != acqCtl.pSources[idx].freq.ttxPid))
               change = TRUE;

         if (change == FALSE)
         {
            // determine total length of name table
            pNames = pTtxNames;
            for (idx = 0; idx < ttxSrcCount; idx++)
               while(*(pNames++) != 0)
                  ;
            // compare names with memcmp instead of strcmp because of 0-separators between names
            change = (memcmp(acqCtl.pTtxNames, pTtxNames, (pNames - pTtxNames)) != 0);
         }
      }
      else
      {  // acquisition is disabled and will stay disabled
         change = FALSE;
      }
   }
   else
      change = TRUE;

   return !change;
#else // USE_TTX_GRABBER
   return TRUE;
#endif
}

// ---------------------------------------------------------------------------
// Stop acquisition and free allocated resources
//
void EpgAcqTtx_Destroy( void )
{
#ifdef USE_TTX_GRABBER
   EpgAcqTtx_Stop();
   TtxGrab_Exit();

   if (acqCtl.pTtxNames != NULL)
   {
      xfree(acqCtl.pTtxNames);
      acqCtl.pTtxNames = NULL;
   }
   if (acqCtl.pSources != NULL)
   {
      xfree(acqCtl.pSources);
      acqCtl.pSources = NULL;
   }
#endif // USE_TTX_GRABBER
}

// ---------------------------------------------------------------------------
// Initialize the module
//
void EpgAcqTtx_Init( void )
{
   memset(&acqCtl, 0, sizeof(acqCtl));
   acqCtl.state     = TTXACQ_STATE_OFF;

#ifdef USE_TTX_GRABBER
   TtxGrab_Init();
#endif
}

