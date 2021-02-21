/*
 *  Scan TV channels for teletext service
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *    Originally, the purpose of the EPG scan was finding providers and
 *    creating an initial database for each, so that the user could be prsented
 *    with choices in the provider selection dialog. This is no longer needed
 *    for the Teletext EPG grabber as new databased can be merged on the fly.
 *    Therefore this functionality is currently looking for a purpose. At best,
 *    it can serve for allowing the user to check which networks transmit
 *    teletext, maybe to determine which channel count to enter into the
 *    Teletext grabber configuration dialog.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/cni_tables.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/tvchan.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"

#include "epgctl/epgctxctl.h"
#include "epgctl/epgacqctl.h"
#include "epgui/uictrl.h"

#include "epgctl/epgscan.h"


// ---------------------------------------------------------------------------
// Declaration of module state variables

#define TTX_DETECTION(PKG,PAG) (((PKG) >= 15) && ((PAG) >= 2))

typedef enum
{
   SCAN_STATE_DONE,
   SCAN_STATE_RESET,
   SCAN_STATE_WAIT_SIGNAL,
   SCAN_STATE_WAIT_DVB_PID,
   SCAN_STATE_WAIT_ANY,
   SCAN_STATE_WAIT_NI,
} EPGSCAN_STATE;

typedef struct
{
   bool             isActive;         // state machine
   bool             acqWasEnabled;    // flag if acq was active before start of scan
   uint             inputSrc;         // driver input source (only TV tuner is allowed)
   uint             actChannelCnt;    // number of channels currently checked
   struct {
      EPGSCAN_STATE    state;            // state machine
      uint             channel;          // currently scanned channel
   } act[MAX_VBI_DVB_STREAMS];
   uint             sumSignalFound;   // number of channels with signal detected
   uint             sumTtxFound;      // number of channels with teletext
   time_t           startTime;        // start of acq for the current channel
   bool             doSlow;           // user option: do not skip channels w/o reception
   bool             useXawtv;         // user option: work on predefined channel list
   uint             provFreqCount;    // number of known frequencies (xawtv and refresh mode)
   uint             chnDoneCnt;       // number of scanned frequencies
   bool           * chnDone;          // flag marking each channel pending/done
   char           * chnNames;         // table of channel names (xawtv mode), or NULL
   EPGACQ_TUNER_PAR * provFreqTab;    // list of known frequencies (xawtv and refresh mode)
   EPGSCAN_MSGCB  * MsgCallback;      // callback function to print messages
} EPGSCANCTL_STATE;

static EPGSCANCTL_STATE scanCtl;

// ----------------------------------------------------------------------------
// Get tuner parameters for the next channel to be scanned
// - for table based search, this simply retrieves the next item from the table
// - for band scan, the next frequency is determined from external module
//
static bool EpgScan_NextChannel( EPGACQ_TUNER_PAR * pFreq )
{
   bool result = FALSE;

   if (scanCtl.provFreqCount > 0)
   {
      uint idx;

      for (idx = 0; idx < scanCtl.provFreqCount; ++idx)
         if (scanCtl.chnDone[idx] == FALSE)
            break;

      if (idx < scanCtl.provFreqCount)
      {
         *pFreq = scanCtl.provFreqTab[idx];
         scanCtl.chnDoneCnt += 1;
         scanCtl.chnDone[idx] = TRUE;
         scanCtl.act[0].channel = idx;
         scanCtl.act[0].state = SCAN_STATE_RESET;
         scanCtl.actChannelCnt = 1;
         result = TRUE;
      }
   }
   else
   {
      uint freq = 0;

      if (TvChannels_GetNext(&scanCtl.act[0].channel, &freq))
      {
         pFreq->freq = TV_CHAN_GET_FREQ(freq);
         pFreq->norm = TV_CHAN_GET_NORM(freq);

         scanCtl.chnDoneCnt += 1;
         //scanCtl.act[0].channel: already set in query above
         scanCtl.act[0].state = SCAN_STATE_RESET;
         scanCtl.actChannelCnt = 1;
         result = TRUE;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Helper function which looks up a name in the channel table
// - name table is available only when freq table was loaded from TV-app
// - name table is a concatenation of zero-terminated strings
//
static const char * EpgScan_GetNameFromTable( uint chnIdx )
{
   const char * pNames = "*unknown*";
   assert(scanCtl.chnNames != NULL);  // to be checked by caller

   if (chnIdx <= scanCtl.provFreqCount)
   {
      pNames = scanCtl.chnNames;
      for (uint idx = 0; idx < chnIdx; idx++)
         pNames += strlen(pNames) + 1;
   }
   else
      debug1("EpgScan-GetChannelName: invalid idx:%d", chnIdx);

   return pNames;
}

// ---------------------------------------------------------------------------
// Helper function which return a channel's name
//
static const char * EpgScan_AcqChannelName( uint actIdx )
{
   static char chanName[64];

   if (scanCtl.provFreqCount == 0)
      TvChannels_GetName(scanCtl.act[actIdx].channel, chanName, sizeof(chanName));
   else if (scanCtl.chnNames != NULL)
      sprintf(chanName, "#%d (\"%.40s\")", scanCtl.act[actIdx].channel, EpgScan_GetNameFromTable(scanCtl.act[actIdx].channel));
   else
      sprintf(chanName, "#%d", scanCtl.act[actIdx].channel);

   chanName[sizeof(chanName) - 1] = 0;

   return chanName;
}

// ---------------------------------------------------------------------------
// Tune DVB PID & piggy-back PIDs from the channel table on the same transponder
//
static void EpgScan_TunePid( EPGACQ_TUNER_PAR * par )
{
   int pidList[MAX_VBI_DVB_STREAMS];
   int serviceIds[MAX_VBI_DVB_STREAMS];
   uint pidCount;

   if (EPGACQ_TUNER_NORM_IS_DVB(par->norm))
   {
      assert (scanCtl.actChannelCnt == 1);

      pidCount = 1;
      pidList[0] = par->ttxPid;
      serviceIds[0] = par->serviceId;

      for (uint idx = 0; (idx < scanCtl.provFreqCount) && (pidCount < MAX_VBI_DVB_STREAMS); ++idx)
      {
         if ( (scanCtl.provFreqTab[idx].freq == par->freq) &&
              (scanCtl.chnDone[idx] == FALSE) &&
              (idx != scanCtl.act[0].channel) )
         {
            dprintf4("EpgScan-TunePid: piggy-backing srv:%d PID:%d channel %d (%s)\n", scanCtl.provFreqTab[idx].serviceId, scanCtl.provFreqTab[idx].ttxPid, idx, EpgScan_GetNameFromTable(idx));
            pidList[pidCount] = scanCtl.provFreqTab[idx].ttxPid;
            serviceIds[pidCount] = scanCtl.provFreqTab[idx].serviceId;

            scanCtl.act[pidCount].channel = idx;
            scanCtl.act[pidCount].state = SCAN_STATE_RESET;

            scanCtl.chnDoneCnt += 1;
            scanCtl.chnDone[idx] = TRUE;
            pidCount += 1;
         }
      }
      scanCtl.actChannelCnt = pidCount;

      BtDriver_TuneDvbPid(pidList, serviceIds, pidCount);
   }
}

// ---------------------------------------------------------------------------
// Per-channel state machine
//
static uint EpgScan_HandleChannel( uint actIdx, time_t now )
{
   const char * pName, * pCountry;
   TTX_DEC_STATS ttxStats;
   time_t ttxStart;
   uint cni;
   time_t delay;
   bool niWait;
   char msgbuf[300];
   char dispText[PDC_TEXT_LEN + 1];
   uint rescheduleMs = 0;

   TtxDecode_GetStatistics(actIdx, &ttxStats, &ttxStart);
   dispText[0] = 0;

   if (scanCtl.act[actIdx].state == SCAN_STATE_RESET)
   {  // reset state again 50ms after channel change
      scanCtl.act[actIdx].state = SCAN_STATE_WAIT_SIGNAL;
      dprintf2("Channel[%d] %d: WAIT_SIGNAL\n", actIdx, scanCtl.act[actIdx].channel);
   }
   else if (scanCtl.act[actIdx].state == SCAN_STATE_WAIT_SIGNAL)
   {  // skip this channel if there's no stable signal
      if ( scanCtl.doSlow || scanCtl.useXawtv ||
           BtDriver_IsVideoPresent() || (ttxStats.ttxPkgCount > 0) )
      {
         if (BtDriver_IsVideoPresent())
            scanCtl.sumSignalFound += 1;

         if ( (scanCtl.provFreqTab != NULL) &&
              EPGACQ_TUNER_NORM_IS_DVB(scanCtl.provFreqTab[scanCtl.act[actIdx].channel].norm) &&
              (scanCtl.provFreqTab[scanCtl.act[actIdx].channel].ttxPid <= 0) )
         {
            dprintf2("Channel[%d] %d: WAIT for TTX PID\n", actIdx, scanCtl.act[actIdx].channel);
            scanCtl.act[actIdx].state = SCAN_STATE_WAIT_DVB_PID;
         }
         else
         {
            dprintf3("Channel[%d] %d: WAIT for data (%d ttx pkgs)\n", actIdx, scanCtl.act[actIdx].channel, ttxStats.ttxPkgCount);
            scanCtl.act[actIdx].state = SCAN_STATE_WAIT_ANY;
         }
      }
      else
      {
         dprintf2("Channel[%d] %d: DONE: neither video signal nor ttx data\n", actIdx, scanCtl.act[actIdx].channel);
         scanCtl.act[actIdx].state = SCAN_STATE_DONE;
      }
   }
   else if (scanCtl.act[actIdx].state == SCAN_STATE_WAIT_DVB_PID)
   {
      int pid[MAX_VBI_DVB_STREAMS];
      if (BtDriver_GetDvbPid(pid) > actIdx)
      {
         dprintf3("Channel[%d]: %d: DVB-PID=%d\n", actIdx, scanCtl.act[actIdx].channel, pid[actIdx]);
         if (pid[actIdx] <= 0)
         {
            sprintf(msgbuf, "Channel %s: no teletext DVB stream", EpgScan_AcqChannelName(actIdx));
            scanCtl.MsgCallback(msgbuf, FALSE);
            scanCtl.act[actIdx].state = SCAN_STATE_DONE;
         }
         else
         {
            scanCtl.act[actIdx].state = SCAN_STATE_WAIT_ANY;
            scanCtl.startTime = now;
         }
      }
      else
         dprintf2("Channel[%d]: %d: Keep waiting for PID\n", actIdx, scanCtl.act[actIdx].channel);
   }
   else if (scanCtl.act[actIdx].state != SCAN_STATE_DONE)
   {
      TtxDecode_GetScanResults(actIdx, &cni, &niWait, dispText, sizeof(dispText));

      if ((cni != 0) && (scanCtl.act[actIdx].state <= SCAN_STATE_WAIT_NI))
      {
         dprintf3("Channel[%d] %d: Found VPS/PDC/NI 0x%04X\n", actIdx, scanCtl.act[actIdx].channel, cni);
         // determine network name (e.g. "EuroNews") and country
         pName = CniGetDescription(cni, &pCountry);
         // determine channel name (e.g. "SE10")
         if (pName != NULL)
         {
            sprintf(msgbuf, "Channel %s: teletext ID %04X %s", EpgScan_AcqChannelName(actIdx), cni, pName);
            // append country if available
            if ((pCountry != NULL) && (strstr(pName, pCountry) == NULL))
               sprintf(msgbuf + strlen(msgbuf), " (%s)", pCountry);
         }
         else if (dispText[0] != 0)
            sprintf(msgbuf, "Channel %s: teletext ID %04X (unknown), ID text \"%s\"", EpgScan_AcqChannelName(actIdx), cni, dispText);
         else
            sprintf(msgbuf, "Channel %s: teletext ID %04X", EpgScan_AcqChannelName(actIdx), cni);
         scanCtl.MsgCallback(msgbuf, FALSE);

         scanCtl.sumTtxFound += 1;
         scanCtl.act[actIdx].state = SCAN_STATE_DONE;
      }
      else if ((scanCtl.act[actIdx].state < SCAN_STATE_WAIT_NI) && niWait)
      {
         dprintf2("Channel[%d] %d: WAIT_NI\n", actIdx, scanCtl.act[actIdx].channel);
         scanCtl.act[actIdx].state = SCAN_STATE_WAIT_NI;
      }
   }

   if (scanCtl.act[actIdx].state != SCAN_STATE_DONE)
   {
      bool haveTtx = TTX_DETECTION(ttxStats.scanPkgCount, ttxStats.scanPagCount);

      // determine timeout for the current state
      switch (scanCtl.act[actIdx].state)
      {
         case SCAN_STATE_WAIT_SIGNAL:    delay =  2; break;
         case SCAN_STATE_WAIT_DVB_PID:   delay =  4; break;
         case SCAN_STATE_WAIT_ANY:
            if ((ttxStats.scanPkgCount > 1) && !haveTtx)
               delay                           =  4;
            else
               delay                           =  2;
            break;
         case SCAN_STATE_WAIT_NI:        delay =  6; break;
         default:                        delay =  0; break;
      }
      if (scanCtl.doSlow)
         delay *= 2;

      if (now >= scanCtl.startTime + delay)
      {  // max wait exceeded -> next channel
         dprintf4("Channel[%d] %d: TIMEOUT in state:%d TTX?:%d\n", actIdx, scanCtl.act[actIdx].channel, scanCtl.act[actIdx].state, haveTtx);

         if ( (scanCtl.useXawtv) &&
              (scanCtl.act[actIdx].state <= SCAN_STATE_WAIT_NI) )
         {  // no CNI found on a predefined channel -> inform user
            if (dispText[0] != 0)
               sprintf(msgbuf, "Channel %s: teletext ID text \"%s\"", EpgScan_AcqChannelName(actIdx), dispText);
            else if (haveTtx)
               sprintf(msgbuf, "Channel %s: teletext detected (but no ID)", EpgScan_AcqChannelName(actIdx));
            else
               sprintf(msgbuf, "Channel %s: no teletext detected", EpgScan_AcqChannelName(actIdx));
            scanCtl.MsgCallback(msgbuf, FALSE);
         }

         if (haveTtx)
            scanCtl.sumTtxFound += 1;
         scanCtl.act[actIdx].state = SCAN_STATE_DONE;
      }
      else
      {  // continue scan on current channel
         //dprintf5("Channel[%d] %d: Continue waiting... state %d waited %d of max. %d secs\n", actIdx, scanCtl.act[actIdx].channel, scanCtl.act[actIdx].state, (int)(now - scanCtl.startTime), (int)delay);
         if (scanCtl.act[actIdx].state == SCAN_STATE_WAIT_SIGNAL)
            rescheduleMs = 100;
         else
            rescheduleMs = 250;
      }
   }
   return rescheduleMs;
}

// ----------------------------------------------------------------------------
// EPG scan timer event handler - called every 250ms
//
uint EpgScan_EvHandler( void )
{
   time_t now = time(NULL);
   char msgbuf[300];
   EPGACQ_TUNER_PAR freq;
   uint rescheduleMs;
   bool isTuner;
   bool stopped;

   // Process all available lines from VBI and check for BI and AI blocks
   // (note: the check function must be called even if EPG acq is not yet enabled
   // because it also handles channel changes and watches over the slave's life)
   TtxDecode_CheckForPackets(&stopped);
   if ( stopped )
   {
      #ifndef WIN32
      const char * pErrStr = BtDriver_GetLastError();
      sprintf(msgbuf, "\nFatal error: video/VBI device error:\n%.200s.\n",
              ((pErrStr != NULL) ? pErrStr : "(unknown - internal error, please report)"));
      scanCtl.MsgCallback(msgbuf, TRUE);
      #else
      scanCtl.MsgCallback("Fatal error in video/VBI device (driver error) - abort.", TRUE);
      #endif

      EpgScan_Stop();
   }

   rescheduleMs = 0;
   if (scanCtl.isActive)
   {
      for (uint actIdx = 0; actIdx < scanCtl.actChannelCnt; ++actIdx)
      {
         uint ms = EpgScan_HandleChannel(actIdx, now);
         if (ms > rescheduleMs)
            rescheduleMs = ms;
      }

      if (rescheduleMs == 0)
      {
         if ( EpgScan_NextChannel(&freq) )
         {
            if ( BtDriver_TuneChannel(scanCtl.inputSrc, &freq, TRUE, &isTuner) )
            {
               EpgScan_TunePid(&freq);
               TtxDecode_StartScan();

               scanCtl.startTime = now;
               rescheduleMs = 50;
            }
            else
            {
               #ifndef WIN32
               const char * pErrStr = BtDriver_GetLastError();
               sprintf(msgbuf, "\nFatal error: channel change failed:\n%.200s.\n",
                       ((pErrStr != NULL) ? pErrStr : "(unknown - internal error, please report)"));
               scanCtl.MsgCallback(msgbuf, TRUE);
               #else
               scanCtl.MsgCallback("channel change failed (driver error) - abort.", TRUE);
               #endif

               EpgScan_Stop();
            }
         }
         else
         {
            dprintf0("EPG scan finished\n");
            EpgScan_Stop();
            scanCtl.MsgCallback("EPG scan finished.", FALSE);

            if (scanCtl.sumSignalFound == 0)
            {
               scanCtl.MsgCallback("\nNo signal found on any channels!\n"
                                   "Check your TV card setup (click the button below),\n"
                                   "or check your antenna cable.", FALSE);
            }
            else
            {
               sprintf(msgbuf, "\nFound teletext on %d of %d channels.\n",
                       scanCtl.sumTtxFound, scanCtl.chnDoneCnt);
               scanCtl.MsgCallback(msgbuf, FALSE);
            }
         }
      }
   }
   else
      debug0("EventHandler-EpgScan: scan not running");

   return rescheduleMs;
}

// ----------------------------------------------------------------------------
// Start EPG scan
// - sets up the scan for the first channel; the real work is done in the
//   timer event handler, which must be called every 250 ms
// - returns FALSE if either /dev/video or /dev/vbi could not be opened
// - when acquisition is already running it's not stopped first, because
//   there's a significant delay when the VBI driver is closed and reopened;
//   instead a special "suspend" command is invoked in normal acqctl.
// - user-configured parameters:
//   doSlow:    doubles all timeouts; can be changed during run-time
//   useXawtv:  indicates that a predefined channel table is used
//              skip video signal test; warn about missing CNIs
//
EPGSCAN_START_RESULT EpgScan_Start( int inputSource, bool doSlow, bool useXawtv,
                                    const char * chnNames, const EPGACQ_TUNER_PAR *freqTab,
                                    uint freqCount, uint * pRescheduleMs,
                                    EPGSCAN_MSGCB * pMsgCallback )
{
   EPGACQ_TUNER_PAR  freq;
   bool  isTuner;
   EPGSCAN_START_RESULT result;
   char msgbuf[300];
   uint rescheduleMs;
   uint nameTabLen;

   scanCtl.inputSrc      = inputSource;
   scanCtl.doSlow        = doSlow;
   scanCtl.useXawtv      = useXawtv;
   scanCtl.acqWasEnabled = EpgAcqCtl_IsActive();
   scanCtl.provFreqCount = freqCount;
   scanCtl.provFreqTab   = NULL;
   scanCtl.chnNames      = NULL;
   scanCtl.MsgCallback   = pMsgCallback;
   scanCtl.chnDone       = NULL;
   scanCtl.chnDoneCnt    = 0;
   result = EPGSCAN_OK;

   rescheduleMs = 0;

   if ( ((useXawtv) && ((freqTab == NULL) || (freqCount == 0))) ||
        (pMsgCallback == NULL) ||
        (pRescheduleMs == NULL) )
   {
      fatal5("EpgScan-Start: illegal NULL ptr param (useXawtv=%d): freqCount=%d, freqTab=%ld, msgCb=%ld, resched=%ld", useXawtv, freqCount, (long)freqTab, (long)pMsgCallback, (long)pRescheduleMs);
      result = EPGSCAN_INTERNAL;
   }
   else
   {  // start teletext acquisition
      if (scanCtl.acqWasEnabled == FALSE)
      {
         if (BtDriver_StartAcq() == FALSE)
         {
            #ifndef WIN32
            const char * pErrStr = BtDriver_GetLastError();
            sprintf(msgbuf, "\nFailed to open the VBI (i.e. teletext) device:\n%.200s.\n",
                    ((pErrStr != NULL) ? pErrStr : "(unknown - internal error, please report)"));
            scanCtl.MsgCallback(msgbuf, FALSE);
            #endif
            result = EPGSCAN_ACCESS_DEV_VBI;
         }
      }
      else
         EpgAcqCtl_Suspend(TRUE);
   }

   if (result == EPGSCAN_OK)
   {
      scanCtl.isActive = TRUE;
      scanCtl.sumSignalFound = 0;
      scanCtl.sumTtxFound = 0;
      scanCtl.act[0].channel = 0;
      scanCtl.actChannelCnt = 0;

      if (scanCtl.provFreqCount > 0)
      {
         // determine total length of name table
         nameTabLen = 0;
         for (uint idx = 0; idx < freqCount; ++idx)
            nameTabLen += strlen(chnNames + nameTabLen) + 1;

         // copy names
         scanCtl.chnNames = xmalloc(nameTabLen);
         memcpy(scanCtl.chnNames, chnNames, nameTabLen);

         // copy frequency table
         scanCtl.provFreqTab = xmalloc(freqCount * sizeof(*freqTab));
         memcpy(scanCtl.provFreqTab, freqTab, freqCount * sizeof(*freqTab));

         scanCtl.chnDone = xmalloc(freqCount * sizeof(bool));
         memset(scanCtl.chnDone, 0, freqCount * sizeof(bool));
      }

      if (EpgScan_NextChannel(&freq))
      {
         BtDriver_SetChannelProfile(VBI_CHANNEL_PRIO_INTERACTIVE);
         BtDriver_SelectSlicer(VBI_SLICER_ZVBI);

         if ( BtDriver_TuneChannel(scanCtl.inputSrc, &freq, TRUE, &isTuner) )
         {
            if (isTuner)
            {
               EpgScan_TunePid(&freq);
               TtxDecode_StartScan();

               scanCtl.startTime = time(NULL);
               rescheduleMs = 50;

               if (scanCtl.provFreqCount == 0)
                  sprintf(msgbuf, "Starting scan on channel %s", EpgScan_AcqChannelName(0));
               else
                  sprintf(msgbuf, "Starting scan on %d known TV channels", scanCtl.provFreqCount);
               scanCtl.MsgCallback(msgbuf, FALSE);
            }
            else
               result = EPGSCAN_NO_TUNER;
         }
         else
         {
            #ifndef WIN32
            const char * pErrStr = BtDriver_GetLastError();
            sprintf(msgbuf, "\nFailed to open video input channel:\n%.200s.\n",
                            ((pErrStr != NULL) ? pErrStr : "(unknown - internal error, please report)"));
            #else
            sprintf(msgbuf, "\nFailed to switch TV channels.\nPlease check your tuner setting in the TV card setup.\n");
            #endif
            scanCtl.MsgCallback(msgbuf, FALSE);
            result = EPGSCAN_ACCESS_DEV_VIDEO;
         }
      }
      else
         result = EPGSCAN_INTERNAL;

      if (result != EPGSCAN_OK)
      {
         if (scanCtl.acqWasEnabled == FALSE)
            BtDriver_StopAcq();
         BtDriver_CloseDevice();
      }
   }

   if (result != EPGSCAN_OK)
   {
      if (scanCtl.provFreqTab != NULL)
      {
         xfree(scanCtl.provFreqTab);
         scanCtl.provFreqTab = NULL;
      }
      if (scanCtl.chnNames != NULL)
      {
         xfree(scanCtl.chnNames);
         scanCtl.chnNames = NULL;
      }
      if (scanCtl.chnDone != NULL)
      {
         xfree(scanCtl.chnDone);
         scanCtl.chnDone = NULL;
      }
   }

   if (pRescheduleMs != NULL)
      *pRescheduleMs = rescheduleMs;
   else
      debug0("EpgScan-Start: illegal NULL ptr param pRescheduleMs");

   return result;
}

// ----------------------------------------------------------------------------
// Stop EPG scan
// - might be called repeatedly by UI to ensure background activity has stopped
//
void EpgScan_Stop( void )
{
   if (scanCtl.isActive)
   {
      if (scanCtl.provFreqTab != NULL)
      {
         xfree(scanCtl.provFreqTab);
         scanCtl.provFreqTab = NULL;
      }
      if (scanCtl.chnNames != NULL)
      {
         xfree(scanCtl.chnNames);
         scanCtl.chnNames = NULL;
      }
      if (scanCtl.chnDone != NULL)
      {
         xfree(scanCtl.chnDone);
         scanCtl.chnDone = NULL;
      }
      scanCtl.isActive = FALSE;

      BtDriver_CloseDevice();
      if (scanCtl.acqWasEnabled == FALSE)
      {
         TtxDecode_StopScan();
         BtDriver_StopAcq();
      }
      else
         EpgAcqCtl_Suspend(FALSE);
   }
}

// ----------------------------------------------------------------------------
// Modify speed of scan while it's running
//
void EpgScan_SetSpeed( bool doSlow )
{
   scanCtl.doSlow = doSlow;
}

// ----------------------------------------------------------------------------
// Return progress percentage for the progress bar
//
double EpgScan_GetProgressPercentage( void )
{
   uint count;

   if (scanCtl.provFreqCount > 0)
      count = scanCtl.provFreqCount + 1;
   else
      count = TvChannels_GetCount();

   if (count != 0)
      return (double)scanCtl.chnDoneCnt / count;
   else
      return 100.0;
}

// ----------------------------------------------------------------------------
// Return if EPG scan is currently running
//
bool EpgScan_IsActive( void )
{
   return scanCtl.isActive;
}
