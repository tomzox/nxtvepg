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
 *  Author: Tom Zoerner
 *
 *  $Id: epgscan.c,v 1.14 2001/06/10 08:33:13 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbacq.h"

#include "epgui/uictrl.h"
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
   bool             acqWasEnabled;    // flag if acq was active before start of scan
   uint             channel;          // currently scanned channel
   uint             channelIdx;       // index of channel currently checked
   uint             signalFound;      // number of channels with signal detected
   time_t           startTime;        // start of acq for the current channel
   bool             doSlow;           // user option: do not skip channels w/o reception
   bool             doRefresh;        // user option: check already known providers only
   ulong          * provFreqTab;      // list of known providers (refresh mode)
   uint             provFreqCount;    // number of known providers (refresh mode)
   EPGDB_CONTEXT  * pDbContext;       // database context for BI/AI search and reload
   void          (* MsgCallback)( const char * pMsg );
} EPGSCANCTL_STATE;

static EPGSCANCTL_STATE scanCtl = {SCAN_STATE_OFF};


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

// ---------------------------------------------------------------------------
// AI callback: invoked before a new AI block is inserted into the database
//
static bool EpgScan_AiCallback( const AI_BLOCK *pNewAi )
{
   const AI_BLOCK *pOldAi;
   ulong oldTunerFreq;
   uint oldAppId;
   bool accept = FALSE;

   if (scanCtl.state >= SCAN_STATE_WAIT_EPG)
   {
      dprintf3("EpgScan: AI found, CNI=0x%04X version %d/%d\n", AI_GET_CNI(pNewAi), pNewAi->version, pNewAi->version_swo);
      EpgDbLockDatabase(scanCtl.pDbContext, TRUE);
      pOldAi = EpgDbGetAi(scanCtl.pDbContext);
      if (pOldAi == NULL)
      {
         oldTunerFreq = scanCtl.pDbContext->tunerFreq;
         oldAppId = scanCtl.pDbContext->appId;
         EpgDbLockDatabase(scanCtl.pDbContext, FALSE);
         EpgContextCtl_Close(scanCtl.pDbContext);

         scanCtl.pDbContext = EpgContextCtl_Open(AI_GET_CNI(pNewAi), CTX_RELOAD_ERR_NONE);

         if (scanCtl.pDbContext->appId == EPG_ILLEGAL_APPID)
            scanCtl.pDbContext->appId = oldAppId;

         if ((scanCtl.pDbContext->tunerFreq != oldTunerFreq) && (oldTunerFreq != 0))
         {
            dprintf2("EpgScan: updating tuner freq: %.2f -> %.2f\n", (double)scanCtl.pDbContext->tunerFreq/16, (double)oldTunerFreq/16);
            scanCtl.pDbContext->modified = TRUE;
            scanCtl.pDbContext->tunerFreq = oldTunerFreq;
         }

         if (scanCtl.pDbContext->tunerFreq != 0)
         {  // store the provider channel frequency in the rc/ini file
            UiControlMsg_NewProvFreq(AI_GET_CNI(pNewAi), scanCtl.pDbContext->tunerFreq);
         }

         accept = TRUE;
      }
      else
      {
         debug1("EpgScan: 2nd AI block received - ignored (prev CNI 0x%04X)", AI_GET_CNI(pOldAi));
         EpgDbLockDatabase(scanCtl.pDbContext, FALSE);
      }
   }
   else
   {  // should be reached only during epg scan -> discard block
      dprintf1("EpgScan: during scan, new AI 0x%04X\n", AI_GET_CNI(pNewAi));
   }

   return accept;
}

// ---------------------------------------------------------------------------
// Called by the database management when a new BI block was received
// - the BI block is never inserted into the database
// - only the application ID is extracted and saved in the db context
//
static bool EpgScan_BiCallback( const BI_BLOCK *pNewBi )
{
   if (scanCtl.state >= SCAN_STATE_WAIT_EPG)
   {
      if (pNewBi->app_id == EPG_ILLEGAL_APPID)
      {
         dprintf0("EpgCtl: EPG not listed in BI\n");
      }
      else
      {
         if (scanCtl.pDbContext->appId != EPG_ILLEGAL_APPID)
         {
            if (scanCtl.pDbContext->appId != pNewBi->app_id)
            {  // not the default id
               dprintf2("EpgCtl: app-ID changed from %d to %d\n", scanCtl.pDbContext->appId, scanCtl.pDbContext->appId);
               EpgDbAcqReset(scanCtl.pDbContext, EPG_ILLEGAL_PAGENO, pNewBi->app_id);
            }
         }
         else
            dprintf1("EpgScan-BiCallback: BI now in db, appID=%d\n", pNewBi->app_id);
      }
   }

   // the BI block is never added to the db
   return FALSE;
}

// ---------------------------------------------------------------------------
// Notification from acquisition about channel change
// - dummy, not required during scan
// - since the video and vbi devices are kepty busy, no external channel changes can occur
//
static void EpgScan_ChannelChange( bool changeDb )
{
   SHOULD_NOT_BE_REACHED;
}

static const EPGDB_ACQ_CB scanAcqCb =
{
   EpgScan_AiCallback,
   EpgScan_BiCallback,
   EpgScan_ChannelChange,
   EpgScan_Stop
};

// ----------------------------------------------------------------------------
// Process all available lines from VBI and check for BI and AI blocks
// 
void EpgScan_ProcessPackets( void )
{
   uint pageNo;

   if (scanCtl.pDbContext != NULL)
   {
      // the check function must be called even if EPG acq is not yet enabled
      // because it also handles channel changes and watches over the slave's life
      if (EpgDbAcqCheckForPackets())
      {
         if (scanCtl.state >= SCAN_STATE_WAIT_EPG)
         {
            EpgDbAcqProcessPackets(&scanCtl.pDbContext);
         }
      }

      pageNo = EpgDbAcqGetMipPageNo();
      if ((pageNo != EPG_ILLEGAL_PAGENO) && (pageNo != scanCtl.pDbContext->pageNo))
      {  // found a different page number in MIP
         dprintf2("EpgScan-ProcessPackets: non-default MIP page no for EPG: %03X (was %003X) -> restart acq\n", pageNo, scanCtl.pDbContext->pageNo);

         scanCtl.pDbContext->pageNo = pageNo;
         EpgDbAcqReset(scanCtl.pDbContext, pageNo, EPG_ILLEGAL_APPID);
      }
   }
}

// ----------------------------------------------------------------------------
// EPG scan timer event handler - called every 250ms
// 
uint EpgScan_EvHandler( void )
{
   const EPGDBSAV_PEEK *pPeek;
   const AI_BLOCK *pAi;
   time_t now = time(NULL);
   uchar chanName[10], msgbuf[80];
   ulong freq;
   ulong ttxPkgCount, epgPkgCount, epgPageCount;
   uint cni, dataPageCnt;
   time_t delay;
   uint rescheduleMs;
   bool niWait;

   rescheduleMs = 0;
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
            if (EpgDbContextGetCni(scanCtl.pDbContext) != 0)
            {  // AI block has been received
               assert((scanCtl.pDbContext->pAiBlock != NULL) && scanCtl.pDbContext->modified);

               pPeek = EpgDbPeek(cni, NULL);
               if (pPeek != NULL)
               {
                  sprintf(msgbuf, "provider already known: %s",
                                AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, pPeek->pAiBlock->blk.ai.thisNetwop));
                  scanCtl.MsgCallback(msgbuf);
                  scanCtl.state = SCAN_STATE_DONE;
                  if (pPeek->tunerFreq != scanCtl.pDbContext->tunerFreq)
                  {
                     scanCtl.MsgCallback("storing provider's tuner frequency");
                     EpgContextCtl_UpdateFreq(cni, scanCtl.pDbContext->tunerFreq);
                  }
                  UiControlMsg_NewProvFreq(cni, scanCtl.pDbContext->tunerFreq);
                  EpgDbPeekDestroy(pPeek);
               }
               else
               {
                  EpgDbDump(scanCtl.pDbContext);

                  EpgDbLockDatabase(scanCtl.pDbContext, TRUE);
                  pAi = EpgDbGetAi(scanCtl.pDbContext);
                  if (pAi != NULL)
                  {
                     sprintf(msgbuf, "Found new provider: %s", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
                     scanCtl.MsgCallback(msgbuf);
                     scanCtl.state = SCAN_STATE_DONE;
                  }
                  EpgDbLockDatabase(scanCtl.pDbContext, FALSE);
               }
            }
         }
         if ((cni != 0) && ((scanCtl.state <= SCAN_STATE_WAIT_NI) || (scanCtl.state == SCAN_STATE_WAIT_NI_OR_EPG)))
         {
            dprintf2("Found VPS/PDC/NI 0x%04X on channel %d\n", cni, scanCtl.channel);
            if (scanCtl.doRefresh == FALSE)
               TvChannels_GetName(scanCtl.channel, chanName, 10);
            else
               sprintf(chanName, "#%d", scanCtl.channel);
            sprintf(msgbuf, "Channel %s: network id 0x%04X", chanName, cni);
            scanCtl.MsgCallback(msgbuf);
            pPeek = EpgDbPeek(cni, NULL);
            if (pPeek != NULL)
            {  // provider already loaded -> skip
               sprintf(msgbuf, "provider already known: %s",
                               AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, pPeek->pAiBlock->blk.ai.thisNetwop));
               scanCtl.MsgCallback(msgbuf);

               if (pPeek->tunerFreq != scanCtl.pDbContext->tunerFreq)
               {
                  scanCtl.MsgCallback("storing provider's tuner frequency");
                  EpgContextCtl_UpdateFreq(cni, scanCtl.pDbContext->tunerFreq);
               }
               UiControlMsg_NewProvFreq(cni, scanCtl.pDbContext->tunerFreq);
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
                  case 0x9001:  // TRT-1 (Turkey)
                     // known provider -> wait for BI/AI
                     if (scanCtl.state <= SCAN_STATE_WAIT_NI)
                        scanCtl.MsgCallback("checking for EPG transmission...");
                     scanCtl.state = SCAN_STATE_WAIT_EPG;
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
            sprintf(msgbuf, "Found ETS-708 syntax on %d pages", dataPageCnt);
            scanCtl.MsgCallback(msgbuf);
            scanCtl.MsgCallback("checking for EPG transmission...");

            if (scanCtl.state <= SCAN_STATE_WAIT_NI)
               scanCtl.state = SCAN_STATE_WAIT_NI_OR_EPG;
            else
               scanCtl.state = SCAN_STATE_WAIT_EPG;
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
            EpgDbAcqNotifyChannelChange();
            if ( BtDriver_TuneChannel(freq, TRUE) )
            {
               // automatically dump db if provider was found, then free resources
               assert((scanCtl.pDbContext->pAiBlock == NULL) || (scanCtl.pDbContext->tunerFreq != 0));
               EpgContextCtl_Close(scanCtl.pDbContext);

               scanCtl.pDbContext = EpgContextCtl_CreateNew();
               scanCtl.pDbContext->tunerFreq = freq;

               // Note: Reset initializes the app-ID and ttx page no, hence it must be called after the db switch
               EpgDbAcqReset(scanCtl.pDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
               EpgDbAcqInitScan();

               dprintf1("RESET channel %d\n", scanCtl.channel);
               scanCtl.startTime = now;
               scanCtl.state = SCAN_STATE_RESET;

               rescheduleMs = 50;
            }
            else
            {
               EpgScan_Stop();
               scanCtl.MsgCallback("channel change failed - abort.");
            }
         }
         else
         {
            dprintf0("EPG scan finished\n");
            EpgScan_Stop();
            scanCtl.MsgCallback("EPG scan finished.");
            if (scanCtl.signalFound == 0)
            {
               scanCtl.MsgCallback("No signal found on any channels!\n"
                                   "Please check your settings in the\n"
                                   "TV card input popup (Configure menu)\n"
                                   "or check your antenna cable.");
            }
         }
      }
      else
      {  // continue scan on current channel
         dprintf2("Continue waiting... waited %d of max. %d secs\n", (int)(now - scanCtl.startTime), (int)delay);
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
//
EPGSCAN_START_RESULT EpgScan_Start( int inputSource, bool doSlow, bool doRefresh,
                                    ulong *freqTab, uint freqCount, uint * pRescheduleMs,
                                    void (* MsgCallback)(const char * pMsg) )
{
   ulong freq;
   bool  isTuner;
   EPGSCAN_START_RESULT result;
   uchar chanName[10], msgbuf[80];
   uint rescheduleMs;

   scanCtl.doRefresh     = doRefresh;
   scanCtl.doSlow        = doSlow;
   scanCtl.acqWasEnabled = (pAcqDbContext != NULL);
   scanCtl.provFreqTab   = freqTab;
   scanCtl.provFreqCount = freqCount;
   scanCtl.MsgCallback   = MsgCallback;
   scanCtl.pDbContext     = EpgContextCtl_CreateNew();
   result = EPGSCAN_OK;

   rescheduleMs = 0;

   // start EPG acquisition
   if (scanCtl.acqWasEnabled == FALSE)
   {
      EpgDbAcqInit();
      EpgDbAcqStart(scanCtl.pDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
      if (BtDriver_StartAcq() == FALSE)
         result = EPGSCAN_ACCESS_DEV_VIDEO;
   }
   else
   {
      EpgContextCtl_Close(pAcqDbContext);
      pAcqDbContext = NULL;
   }

   if (result == EPGSCAN_OK)
   {
      EpgDbAcqNotifyChannelChange();
      if ( BtDriver_SetInputSource(inputSource, TRUE, &isTuner) )
      {
         if (isTuner)
         {
            scanCtl.channelIdx = 0;
            scanCtl.channel = 0;
            if ( EpgScan_NextChannel(&freq) &&
                 BtDriver_TuneChannel(freq, TRUE) )
            {
               scanCtl.pDbContext->tunerFreq = freq;

               EpgDbAcqReset(scanCtl.pDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
               EpgDbAcqInitScan();
               EpgAcqCtl_Suspend(TRUE);
               EpgDbAcqSetCallbacks(&scanAcqCb);

               dprintf1("RESET channel %d\n", scanCtl.channel);
               scanCtl.state = SCAN_STATE_RESET;
               scanCtl.startTime = time(NULL);
               rescheduleMs = 50;
               scanCtl.signalFound = 0;

               if (scanCtl.doRefresh == FALSE)
               {
                  TvChannels_GetName(scanCtl.channel, chanName, 10);
                  sprintf(msgbuf, "Starting scan on channel %s", chanName);
               }
               else
                  sprintf(msgbuf, "Starting scan on %d known channels", scanCtl.provFreqCount);
               scanCtl.MsgCallback(msgbuf);
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
      EpgContextCtl_Close(scanCtl.pDbContext);
      scanCtl.pDbContext = NULL;
      if (scanCtl.acqWasEnabled == FALSE)
         EpgDbAcqStop();
      else
         EpgAcqCtl_Suspend(FALSE);

      if (scanCtl.provFreqTab != NULL)
      {
         xfree(scanCtl.provFreqTab);
         scanCtl.provFreqTab = NULL;
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
      scanCtl.state = SCAN_STATE_OFF;

      EpgContextCtl_Close(scanCtl.pDbContext);
      scanCtl.pDbContext = NULL;

      BtDriver_CloseDevice();
      if (scanCtl.acqWasEnabled == FALSE)
      {
         BtDriver_StopAcq();
         EpgDbAcqStop();
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

   if (scanCtl.doRefresh)
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

