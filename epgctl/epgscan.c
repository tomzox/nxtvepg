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
 *    The purpose of the EPG scan is to find out the tuner frequencies of all
 *    EPG providers. This would not be needed it the Nextview decoder was
 *    integrated into a TV application. But as a separate application, there
 *    is no other way to find out the frequencies, since /dev/video can not
 *    be opened twice, not even to only query the actual tuner frequency.
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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgscan.c,v 1.7 2001/02/04 20:15:21 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <time.h>
#include <tcl.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbacq.h"

#include "epgui/uictrl.h"
#include "epgui/menucmd.h"
#include "epgctl/epgmain.h"
#include "epgctl/epgctxctl.h"
#include "epgctl/epgacqctl.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/tvchan.h"
#include "epgvbi/btdrv.h"

#include "epgctl/epgscan.h"


// ---------------------------------------------------------------------------
// Declaration of module state variables

typedef struct
{
   EPGSCAN_STATE    state;            // state machine
   Tcl_TimerToken   evHandler;        // timer handle
   bool             acqWasEnabled;    // flag if acq was active before start of scan
   uint             channel;          // currently scanned channel
   uint             channelIdx;       // index of channel currently checked
   uint             signalFound;      // number of channels with signal detected
   time_t           startTime;        // start of acq for the current channel
   bool             doSlow;           // user option: do not skip channels w/o reception
   bool             doRefresh;        // user option: check already known providers only
   ulong          * provFreqTab;      // list of known providers (refresh mode)
   uint             provFreqCount;    // number of known providers (refresh mode)
} EPGSCANCTL_STATE;

static EPGSCANCTL_STATE scanCtl = {SCAN_STATE_OFF, NULL};


// ----------------------------------------------------------------------------
// Tune the next channel
//
static bool EpgScan_NextChannel( ulong * pFreq )
{
   bool result = FALSE;

   if (scanCtl.doRefresh)
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
      if (TvChannels_GetNext(&scanCtl.channel, pFreq))
      {
         scanCtl.channelIdx += 1;
         result = TRUE;
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// EPG scan timer event handler - called every 250ms
// 
static void EventHandler_EpgScan( ClientData clientData )
{
   const EPGDBSAV_PEEK *pPeek;
   const AI_BLOCK *pAi;
   time_t now = time(NULL);
   ulong freq;
   ulong ttxPkgCount, epgPkgCount, epgPageCount;
   uint cni, dataPageCnt;
   time_t delay;
   bool niWait;

   scanCtl.evHandler = NULL;
   if (scanCtl.state != SCAN_STATE_OFF)
   {
      if (scanCtl.state == SCAN_STATE_RESET)
      {  // reset state again 50ms after channel change
         // make sure all data of the previous channel is removed from the ring buffer
         EpgDbAcqInitScan();
         scanCtl.state = SCAN_STATE_WAIT_SIGNAL;
         dprintf1("WAIT_SIGNAL channel %d\n", scanCtl.channel);
      }
      else if (scanCtl.state == SCAN_STATE_WAIT_SIGNAL)
      {  // skip this channel if there's no stable signal
         EpgDbAcqGetStatistics(&ttxPkgCount, &epgPkgCount, &epgPageCount);
         if ( scanCtl.doSlow || scanCtl.doRefresh ||
              BtDriver_IsVideoPresent() || (ttxPkgCount > 0) )
         {
            dprintf2("WAIT for data on channel %d (%ld ttx pkgs)\n", scanCtl.channel, ttxPkgCount);
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
         EpgDbAcqGetScanResults(&cni, &niWait, &dataPageCnt);

         if (scanCtl.state == SCAN_STATE_WAIT_EPG)
         {
            if (EpgDbContextGetCni(pAcqDbContext) != 0)
            {  // AI block has been received
               assert((pAcqDbContext->pAiBlock != NULL) && pAcqDbContext->modified);

               pPeek = EpgDbPeek(cni, NULL);
               if (pPeek != NULL)
               {
                  sprintf(comm, "provider already known: %s",
                                AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, pPeek->pAiBlock->blk.ai.thisNetwop));
                  MenuCmd_AddEpgScanMsg(comm);
                  scanCtl.state = SCAN_STATE_DONE;
                  if (pPeek->tunerFreq != pAcqDbContext->tunerFreq)
                  {
                     MenuCmd_AddEpgScanMsg("storing provider's tuner frequency");
                     EpgContextCtl_UpdateFreq(cni, pAcqDbContext->tunerFreq);
                  }
                  UiControlMsg_NewProvFreq(cni, pAcqDbContext->tunerFreq);
                  EpgDbPeekDestroy(pPeek);
               }
               else
               {
                  EpgDbDump(pAcqDbContext);

                  EpgDbLockDatabase(pAcqDbContext, TRUE);
                  pAi = EpgDbGetAi(pAcqDbContext);
                  if (pAi != NULL)
                  {
                     sprintf(comm, "Found new provider: %s", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
                     MenuCmd_AddEpgScanMsg(comm);
                     scanCtl.state = SCAN_STATE_DONE;
                  }
                  EpgDbLockDatabase(pAcqDbContext, FALSE);
               }
            }
         }
         if ((cni != 0) && ((scanCtl.state <= SCAN_STATE_WAIT_NI) || (scanCtl.state == SCAN_STATE_WAIT_NI_OR_EPG)))
         {
            dprintf2("Found CNI 0x%04X on channel %d\n", cni, scanCtl.channel);
            sprintf(comm, "Channel %d: network id 0x%04X", scanCtl.channel, cni);
            MenuCmd_AddEpgScanMsg(comm);
            pPeek = EpgDbPeek(cni, NULL);
            if (pPeek != NULL)
            {  // provider already loaded -> skip
               sprintf(comm, "provider already known: %s",
                             AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, pPeek->pAiBlock->blk.ai.thisNetwop));
               MenuCmd_AddEpgScanMsg(comm);

               if (pPeek->tunerFreq != pAcqDbContext->tunerFreq)
               {
                  MenuCmd_AddEpgScanMsg("storing provider's tuner frequency");
                  EpgContextCtl_UpdateFreq(cni, pAcqDbContext->tunerFreq);
               }
               UiControlMsg_NewProvFreq(cni, pAcqDbContext->tunerFreq);
               EpgDbPeekDestroy(pPeek);

               scanCtl.state = SCAN_STATE_DONE;
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
                     if (scanCtl.state <= SCAN_STATE_WAIT_NI)
                        MenuCmd_AddEpgScanMsg("checking for EPG transmission...");
                     scanCtl.state = SCAN_STATE_WAIT_EPG;
                     // enable normal EPG acq callback handling
                     EpgAcqCtl_ToggleAcqForScan(TRUE);
                     break;

                  default:
                     // CNI not known as provider -> keep checking for data page
                     scanCtl.state = SCAN_STATE_WAIT_DATA;
                     break;
               }
            }
         }
         else if ((dataPageCnt != 0) && (scanCtl.state <= SCAN_STATE_WAIT_DATA))
         {
            dprintf2("Found %d data pages on channel %d\n", dataPageCnt, scanCtl.channel);
            sprintf(comm, "Found ETS-708 syntax on %d pages", dataPageCnt);
            MenuCmd_AddEpgScanMsg(comm);
            MenuCmd_AddEpgScanMsg("checking for EPG transmission...");

            if (scanCtl.state <= SCAN_STATE_WAIT_NI)
               scanCtl.state = SCAN_STATE_WAIT_NI_OR_EPG;
            else
               scanCtl.state = SCAN_STATE_WAIT_EPG;

            // enable normal EPG acq callback handling
            EpgAcqCtl_ToggleAcqForScan(TRUE);
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
         case SCAN_STATE_WAIT_NI:        delay =  4; break;
         case SCAN_STATE_WAIT_DATA:      delay =  2; break;
         case SCAN_STATE_WAIT_NI_OR_EPG: delay = 45; break;
         case SCAN_STATE_WAIT_EPG:       delay = 45; break;
         default:                        delay =  0; break;
      }
      if (scanCtl.doSlow)
         delay *= 2;

      if ( (scanCtl.state == SCAN_STATE_DONE) || ((now - scanCtl.startTime) >= delay) )
      {  // max wait exceeded -> next channel
         if ( EpgScan_NextChannel(&freq) )
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

               dprintf1("RESET channel %d\n", scanCtl.channel);
               scanCtl.startTime = now;
               scanCtl.state = SCAN_STATE_RESET;
               EpgAcqCtl_ToggleAcqForScan(FALSE);
               sprintf(comm, ".epgscan.all.baro.bari configure -width %d\n",
                             (int)((double)scanCtl.channelIdx / (scanCtl.doRefresh ? (scanCtl.provFreqCount + 1) : TvChannels_GetCount())*140.0));
               eval_check(interp, comm);

               scanCtl.evHandler = Tcl_CreateTimerHandler(50, EventHandler_EpgScan, NULL);
            }
            else
            {
               EpgScan_Stop();
               MenuCmd_AddEpgScanMsg("channel change failed - abort.");
               eval_check(interp, "bell");
               MenuCmd_StopEpgScan(NULL, interp, 0, NULL);
            }
         }
         else
         {
            EpgScan_Stop();
            MenuCmd_AddEpgScanMsg("EPG scan finished.");
            eval_check(interp, ".epgscan.all.baro.bari configure -width 140\n");
            if (scanCtl.signalFound == 0)
            {
               MenuCmd_AddEpgScanMsg("No signal found on any channels!\n"
                                     "Please check your settings in the\n"
                                     "TV card input popup (Configure menu)\n"
                                     "or check your antenna cable.");
            }
            eval_check(interp, "bell");
            MenuCmd_StopEpgScan(NULL, interp, 0, NULL);
         }
      }
      else
      {  // continue scan on current channel
         dprintf2("Continue waiting... waited %d of max. %d secs\n", (int)(now - scanCtl.startTime), (int)delay);
         if (scanCtl.state == SCAN_STATE_WAIT_SIGNAL)
            scanCtl.evHandler = Tcl_CreateTimerHandler(100, EventHandler_EpgScan, NULL);
         else
            scanCtl.evHandler = Tcl_CreateTimerHandler(250, EventHandler_EpgScan, NULL);
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
EPGSCAN_START_RESULT EpgScan_Start( int inputSource, bool doSlow, bool doRefresh, ulong *freqTab, uint freqCount )
{
   ulong freq;
   bool  isTuner;
   EPGSCAN_START_RESULT result;

   scanCtl.doRefresh     = doRefresh;
   scanCtl.doSlow        = doSlow;
   scanCtl.acqWasEnabled = (pAcqDbContext != NULL);
   scanCtl.provFreqTab   = freqTab;
   scanCtl.provFreqCount = freqCount;
   result = EPGSCAN_OK;

   // set up an empty db
   if (scanCtl.acqWasEnabled)
   {
      EpgContextCtl_Close(pAcqDbContext);
      pAcqDbContext = NULL;
   }
   pAcqDbContext = EpgContextCtl_CreateNew();

   // start EPG acquisition
   if (scanCtl.acqWasEnabled == FALSE)
   {
      EpgDbAcqInit();
      EpgDbAcqStart(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
      if (BtDriver_StartAcq() == FALSE)
         result = EPGSCAN_ACCESS_DEV_VIDEO;
   }

   if (result == EPGSCAN_OK)
   {
      if ( BtDriver_SetInputSource(inputSource, TRUE, &isTuner) )
      {
         if (isTuner)
         {
            scanCtl.channelIdx = 0;
            scanCtl.channel = 0;
            if ( EpgScan_NextChannel(&freq) &&
                 BtDriver_TuneChannel(freq, TRUE) )
            {
               pAcqDbContext->tunerFreq = freq;

               EpgDbAcqReset(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
               EpgDbAcqInitScan();
               EpgAcqCtl_ToggleAcqForScan(FALSE);

               dprintf1("RESET channel %d\n", scanCtl.channel);
               scanCtl.state = SCAN_STATE_RESET;
               scanCtl.startTime = time(NULL);
               scanCtl.evHandler = Tcl_CreateTimerHandler(50, EventHandler_EpgScan, NULL);
               scanCtl.signalFound = 0;

               if (scanCtl.doRefresh == FALSE)
                  sprintf(comm, "Starting scan on channel %d", scanCtl.channel);
               else
                  sprintf(comm, "Starting scan on %d known channels", scanCtl.provFreqCount);
               MenuCmd_AddEpgScanMsg(comm);
               sprintf(comm, ".epgscan.all.baro.bari configure -width %d\n",
                             (int)((double)scanCtl.channelIdx / (scanCtl.doRefresh ? (scanCtl.provFreqCount + 1) : TvChannels_GetCount())*140.0));
               eval_check(interp, comm);
            }
            else
               result = EPGSCAN_ACCESS_DEV_VIDEO;
         }
         else
            result = EPGSCAN_NO_TUNER;
      }
      else
         result = EPGSCAN_ACCESS_DEV_VIDEO;

      if ((result != EPGSCAN_OK) && (scanCtl.acqWasEnabled == FALSE))
      {
         if (scanCtl.acqWasEnabled == FALSE)
            BtDriver_StopAcq();
         BtDriver_CloseDevice();
      }
   }

   if (result != EPGSCAN_OK)
   {
      EpgContextCtl_Close(pAcqDbContext);
      pAcqDbContext = NULL;
      EpgAcqCtl_ToggleAcqForScan(FALSE);
      if (scanCtl.acqWasEnabled == FALSE)
         EpgDbAcqStop();
      else
         EpgAcqCtl_Start();

      if (scanCtl.provFreqTab != NULL)
      {
         xfree(scanCtl.provFreqTab);
         scanCtl.provFreqTab = NULL;
      }
   }

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
      Tcl_DeleteTimerHandler(scanCtl.evHandler);
      scanCtl.evHandler = NULL;

      if (scanCtl.provFreqTab != NULL)
      {
         xfree(scanCtl.provFreqTab);
         scanCtl.provFreqTab = NULL;
      }
      scanCtl.state = SCAN_STATE_OFF;

      EpgAcqCtl_ToggleAcqForScan(FALSE);
      EpgContextCtl_Close(pAcqDbContext);
      pAcqDbContext = NULL;

      BtDriver_CloseDevice();
      if (scanCtl.acqWasEnabled == FALSE)
      {
         BtDriver_StopAcq();
         EpgDbAcqStop();
      }
      else
      {
         EpgAcqCtl_Start();
      }
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
// Return if EPG scan is currently running
// 
bool EpgScan_IsActive( void )
{
   return (scanCtl.state != SCAN_STATE_OFF);
}

