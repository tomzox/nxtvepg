/*
 *  Scan TV channels for Nextview EPG content providers
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
 *    The purpose of the EPG scan is to find all Nextview EPG providers.
 *   
 *    The scan is designed to perform as fast as possible. We stay max. 2 secs
 *    on each channel. If a CNI is found and it's a known and loaded provider,
 *    we go on immediately. If no CNI is found or if it's not a known provider,
 *    we wait if any syntactically correct packets are received on any of the
 *    8*24 possible EPG teletext pages. If yes, we try for 45 seconds to
 *    receive the BI and AI blocks. This period is chosen that long, because
 *    some providers have gaps of over 45 seconds between cycles (e.g. RTL-II)
 *    and others do not transmit on the default page 1DF, so we have to wait
 *    for the MIP, which usually is transmitted about every 30 seconds. For
 *    any provider that's found, a database is created and the frequency
 *    saved in its header.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgscan.c,v 1.51 2020/06/17 19:31:21 tom Exp tom $
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

typedef struct
{
   EPGSCAN_STATE    state;            // state machine
   bool             acqWasEnabled;    // flag if acq was active before start of scan
   uint             inputSrc;         // driver input source (only TV tuner is allowed)
   uint             channel;          // currently scanned channel
   uint             channelIdx;       // index of channel currently checked
   uint             signalFound;      // number of channels with signal detected
   time_t           startTime;        // start of acq for the current channel
   time_t           extraWait;        // extra seconds to wait until timeout
   bool             doSlow;           // user option: do not skip channels w/o reception
   bool             useXawtv;         // user option: work on predefined channel list
   uint             provFreqCount;    // number of known frequencies (xawtv and refresh mode)
   char           * chnNames;         // table of channel names (xawtv mode), or NULL
   EPGACQ_TUNER_PAR * provFreqTab;    // list of known frequencies (xawtv and refresh mode)
   uint           * provCniTab;       // list of known provider CNIs (refresh mode)
   EPGDB_QUEUE      dbQueue;          // queue for incoming EPG blocks
   bool             foundBi;          // TRUE when BI found on current channel
   EPGSCAN_MSGCB  * MsgCallback;      // callback function to print messages
} EPGSCANCTL_STATE;

static EPGSCANCTL_STATE scanCtl = {SCAN_STATE_OFF};

// ----------------------------------------------------------------------------
// Tune the next channel
//
static bool EpgScan_NextChannel( EPGACQ_TUNER_PAR * pFreq )
{
   bool result = FALSE;

   if (scanCtl.provFreqCount > 0)
   {
      if (scanCtl.channelIdx < scanCtl.provFreqCount)
      {
         *pFreq = scanCtl.provFreqTab[scanCtl.channelIdx];
         scanCtl.channelIdx += 1;
         scanCtl.channel = scanCtl.channelIdx;
         result = TRUE;
      }
   }
   else
   {
      uint freq = 0;

      if (TvChannels_GetNext(&scanCtl.channel, &freq))
      {
         pFreq->freq = EPGDB_TUNER_GET_FREQ(freq);
         pFreq->norm = EPGDB_TUNER_GET_NORM(freq);

         scanCtl.channelIdx += 1;
         result = TRUE;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Helper function which looks up a channel's name
// - name table is available only when freq table was loaded from TV-app
// - name table is a concatenation of zero-terminated strings
//
static const char * EpgAcqTtx_GetChannelName( uint channelIdx )
{
   const char * pNames = NULL;
   assert(scanCtl.chnNames != NULL);  // to be checked by caller

   if (channelIdx <= scanCtl.provFreqCount)
   {
      pNames = scanCtl.chnNames;
      for (uint idx = 0; idx < channelIdx; idx++)
         pNames += strlen(pNames) + 1;
   }
   else
      debug1("EpgScan-GetChannelName: invalid idx:%d", channelIdx);

   return pNames;
}

// ----------------------------------------------------------------------------
// EPG scan timer event handler - called every 250ms
// 
uint EpgScan_EvHandler( void )
{
   const char * pName, * pCountry;
   time_t now = time(NULL);
   char chanName[64], msgbuf[300], dispText[PDC_TEXT_LEN + 1];
   EPGACQ_TUNER_PAR freq;
   TTX_DEC_STATS ttxStats;
   time_t ttxStart;
   uint cni;
   time_t delay;
   uint rescheduleMs;
   bool isTuner;
   bool niWait;
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

   dispText[0] = 0;
   rescheduleMs = 0;
   if (scanCtl.state != SCAN_STATE_OFF)
   {
      if (scanCtl.state == SCAN_STATE_RESET)
      {  // reset state again 50ms after channel change
         scanCtl.state = SCAN_STATE_WAIT_SIGNAL;
         dprintf1("WAIT_SIGNAL channel %d\n", scanCtl.channel);
      }
      else if (scanCtl.state == SCAN_STATE_WAIT_SIGNAL)
      {  // skip this channel if there's no stable signal
         TtxDecode_GetStatistics(0, &ttxStats, &ttxStart);
         if ( scanCtl.doSlow || scanCtl.useXawtv ||
              BtDriver_IsVideoPresent() || (ttxStats.ttxPkgCount > 0) )
         {
            dprintf2("WAIT for data on channel %d (%d ttx pkgs)\n", scanCtl.channel, ttxStats.ttxPkgCount);
            if (BtDriver_IsVideoPresent())
               scanCtl.signalFound += 1;
            scanCtl.state = SCAN_STATE_WAIT_ANY;
         }
         else
         {
            dprintf1("DONE with %d: neither video signal nor ttx data\n", scanCtl.channel);
            scanCtl.state = SCAN_STATE_DONE;
         }
      }
      else
      {
         TtxDecode_GetScanResults(&cni, &niWait, dispText, sizeof(dispText));

         if ((cni != 0) && (scanCtl.state <= SCAN_STATE_WAIT_NI))
         {
            dprintf2("Found VPS/PDC/NI 0x%04X on channel %d\n", cni, scanCtl.channel);
            // determine network name (e.g. "EuroNews") and country
            pName = CniGetDescription(cni, &pCountry);
            // determine channel name (e.g. "SE10")
            if (scanCtl.provFreqCount == 0)
               TvChannels_GetName(scanCtl.channel, chanName, sizeof(chanName));
            else if (scanCtl.chnNames != NULL)
               snprintf(chanName, sizeof(chanName), "#%d (\"%.40s\")", scanCtl.channel, EpgAcqTtx_GetChannelName(scanCtl.channel - 1));
            else
               snprintf(chanName, sizeof(chanName), "#%d", scanCtl.channel);
            chanName[sizeof(chanName) - 1] = 0;
            if (pName != NULL)
            {
               sprintf(msgbuf, "Channel %s: CNI 0x%04X %s", chanName, cni, pName);
               // append country if available
               if ((pCountry != NULL) && (strstr(pName, pCountry) == NULL))
                  sprintf(msgbuf + strlen(msgbuf), " (%s)", pCountry);
            }
            else if (dispText[0] != 0)
               sprintf(msgbuf, "Channel %s: CNI 0x%04X \"%s\"", chanName, cni, dispText);
            else
               sprintf(msgbuf, "Channel %s: CNI 0x%04X", chanName, cni);
            scanCtl.MsgCallback(msgbuf, FALSE);

            scanCtl.state = SCAN_STATE_DONE;
         }
         else if ((scanCtl.state < SCAN_STATE_WAIT_NI) && niWait)
         {
            dprintf1("WAIT_NI on channel %d\n", scanCtl.channel);
            scanCtl.state = SCAN_STATE_WAIT_NI;
         }
      }

      // determine timeout for the current state
      switch (scanCtl.state)
      {
         case SCAN_STATE_WAIT_SIGNAL:    delay =  2; break;
         case SCAN_STATE_WAIT_ANY:       delay =  2; break;
         case SCAN_STATE_WAIT_NI:        delay =  6; break;
         default:                        delay =  0; break;
      }
      if (scanCtl.doSlow)
         delay *= 2;

      if ( (scanCtl.state == SCAN_STATE_DONE) ||
           ((now - scanCtl.startTime - scanCtl.extraWait) >= delay) )
      {  // max wait exceeded -> next channel

         if ( (scanCtl.useXawtv) &&
              (scanCtl.state <= SCAN_STATE_WAIT_NI) )
         {  // no CNI found on a predefined channel -> inform user
            if (scanCtl.provFreqCount == 0)
               TvChannels_GetName(scanCtl.channel, chanName, sizeof(chanName));
            else if (scanCtl.chnNames != NULL)
               sprintf(chanName, "#%d (\"%.40s\")", scanCtl.channel, EpgAcqTtx_GetChannelName(scanCtl.channel - 1));
            else
               sprintf(chanName, "#%d", scanCtl.channel);
            chanName[sizeof(chanName) - 1] = 0;
            if (dispText[0] != 0)
               sprintf(msgbuf, "Channel %s: CNI 0x0000 \"%s\"", chanName, dispText);
            else
               sprintf(msgbuf, "Channel %s: no network ID received", chanName);
            scanCtl.MsgCallback(msgbuf, FALSE);
         }

         if ( EpgScan_NextChannel(&freq) )
         {
            if ( BtDriver_TuneChannel(scanCtl.inputSrc, &freq, TRUE, &isTuner) )
            {
               BtDriver_TuneDvbPid(&freq.ttxPid, 1);  // TODO concurrency
               TtxDecode_StartScan();

               dprintf1("RESET channel %d\n", scanCtl.channel);
               scanCtl.startTime = now;
               scanCtl.extraWait = 0;
               scanCtl.state = SCAN_STATE_RESET;
               scanCtl.foundBi = FALSE;

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
            if (scanCtl.signalFound == 0)
            {
               scanCtl.MsgCallback("No signal found on any channels!\n"
                                   "Please check your settings in the\n"
                                   "TV card input popup (Configure menu)\n"
                                   "or check your antenna cable.", FALSE);
            }
         }
      }
      else
      {  // continue scan on current channel
         dprintf3("Continue waiting... state %d waited %d of max. %d secs\n", scanCtl.state, (int)(now - scanCtl.startTime - scanCtl.extraWait), (int)delay);
         if (scanCtl.state == SCAN_STATE_WAIT_SIGNAL)
            rescheduleMs = 100;
         else
            rescheduleMs = 250;
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
                                    char * chnNames, EPGACQ_TUNER_PAR *freqTab,
                                    uint freqCount, uint * pRescheduleMs,
                                    EPGSCAN_MSGCB * pMsgCallback )
{
   EPGACQ_TUNER_PAR  freq;
   bool  isTuner;
   EPGSCAN_START_RESULT result;
   char chanName[10], msgbuf[300];
   uint rescheduleMs;

   scanCtl.inputSrc      = inputSource;
   scanCtl.doSlow        = doSlow;
   scanCtl.useXawtv      = useXawtv;
   scanCtl.acqWasEnabled = EpgAcqCtl_IsActive();
   scanCtl.provFreqCount = freqCount;
   scanCtl.provFreqTab   = freqTab;
   scanCtl.chnNames      = chnNames;
   scanCtl.MsgCallback   = pMsgCallback;
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
      scanCtl.channelIdx = 0;
      scanCtl.channel = 0;
      if (EpgScan_NextChannel(&freq))
      {
         BtDriver_SetChannelProfile(VBI_CHANNEL_PRIO_INTERACTIVE);
         BtDriver_SelectSlicer(VBI_SLICER_ZVBI);

         if ( BtDriver_TuneChannel(scanCtl.inputSrc, &freq, TRUE, &isTuner) )
         {
            if (isTuner)
            {
               BtDriver_TuneDvbPid(&freq.ttxPid, 1);  // TODO concurrency
               TtxDecode_StartScan();

               dprintf1("RESET channel %d\n", scanCtl.channel);
               scanCtl.state = SCAN_STATE_RESET;
               scanCtl.startTime = time(NULL);
               scanCtl.extraWait = 0;
               scanCtl.foundBi = FALSE;
               rescheduleMs = 50;
               scanCtl.signalFound = 0;

               if (scanCtl.provFreqCount == 0)
               {
                  TvChannels_GetName(scanCtl.channel, chanName, sizeof(chanName));
                  sprintf(msgbuf, "Starting scan on channel %s", chanName);
               }
               else
               {
                  sprintf(msgbuf, "Starting scan on %d known TV channels", scanCtl.provFreqCount);
               }
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
      if (scanCtl.provCniTab != NULL)
      {
         xfree(scanCtl.provCniTab);
         scanCtl.provCniTab = NULL;
      }
      if (scanCtl.chnNames != NULL)
      {
         xfree(scanCtl.chnNames);
         scanCtl.chnNames = NULL;
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
   if (scanCtl.state != SCAN_STATE_OFF)
   {
      if (scanCtl.provFreqTab != NULL)
      {
         xfree(scanCtl.provFreqTab);
         scanCtl.provFreqTab = NULL;
      }
      if (scanCtl.provCniTab != NULL)
      {
         xfree(scanCtl.provCniTab);
         scanCtl.provCniTab = NULL;
      }
      if (scanCtl.chnNames != NULL)
      {
         xfree(scanCtl.chnNames);
         scanCtl.chnNames = NULL;
      }
      scanCtl.state = SCAN_STATE_OFF;

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
      return (double)scanCtl.channelIdx / count;
   else
      return 100.0;
}

// ----------------------------------------------------------------------------
// Return if EPG scan is currently running
// 
bool EpgScan_IsActive( void )
{
   return (scanCtl.state != SCAN_STATE_OFF);
}
