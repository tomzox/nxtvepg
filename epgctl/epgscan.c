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
 *  $Id: epgscan.c,v 1.20 2002/05/04 18:17:01 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <time.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgdbacq.h"
#include "epgdb/epgdbmgmt.h"

#include "epgui/uictrl.h"
#include "epgctl/epgctxctl.h"
#include "epgctl/epgacqctl.h"
#include "epgvbi/cni_tables.h"
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
   uint             newProvCount;     // number of providers found
   uint             badProvCount;     // number of defective providers found (refresh mode)
   time_t           startTime;        // start of acq for the current channel
   bool             doSlow;           // user option: do not skip channels w/o reception
   bool             doRefresh;        // user option: check already known providers only
   bool             useXawtv;         // user option: work on predefined channel list
   uint             provFreqCount;    // number of known frequencies (xawtv and refresh mode)
   ulong          * provFreqTab;      // list of known frequencies (xawtv and refresh mode)
   uint           * provCniTab;       // list of known provider CNIs (refresh mode)
   EPGDB_CONTEXT  * pDbContext;       // database context for BI/AI search and reload
   EPGDB_QUEUE      dbQueue;          // queue for incoming EPG blocks
   bool             foundBi;          // TRUE when BI found on current channel
   void          (* MsgCallback)( const char * pMsg );
} EPGSCANCTL_STATE;

static EPGSCANCTL_STATE scanCtl = {SCAN_STATE_OFF};

static bool EpgScan_BiCallback( const BI_BLOCK *pNewBi );
static bool EpgScan_AiCallback( const AI_BLOCK *pNewAi );
static const EPGDB_ADD_CB epgScanCb =
{
   EpgScan_AiCallback,
   EpgScan_BiCallback,
};

// ----------------------------------------------------------------------------
// Tune the next channel
//
static bool EpgScan_NextChannel( ulong * pFreq )
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
   uchar msgbuf[80];
   bool accept = FALSE;

   if ((scanCtl.state >= SCAN_STATE_WAIT_EPG) && (AI_GET_CNI(pNewAi) != 0))
   {
      dprintf3("EpgScan: AI found, CNI=0x%04X version %d/%d\n", AI_GET_CNI(pNewAi), pNewAi->version, pNewAi->version_swo);
      EpgDbLockDatabase(scanCtl.pDbContext, TRUE);

      pOldAi = EpgDbGetAi(scanCtl.pDbContext);
      if (pOldAi == NULL)
      {
         // save the parameters found during the scan
         oldTunerFreq = scanCtl.pDbContext->tunerFreq;
         oldAppId = scanCtl.pDbContext->appId;

         EpgDbLockDatabase(scanCtl.pDbContext, FALSE);
         EpgContextCtl_Close(scanCtl.pDbContext);
         scanCtl.pDbContext = EpgContextCtl_Open(AI_GET_CNI(pNewAi), CTX_FAIL_RET_CREATE, CTX_RELOAD_ERR_NONE);

         if (EpgDbContextGetCni(scanCtl.pDbContext) == 0)
         {  // new provider -> nothing to do here, just add the AI and keep all params
            sprintf(msgbuf, "Found %sprovider: %s", (scanCtl.doRefresh ? "" : "new "), AI_GET_NETWOP_NAME(pNewAi, pNewAi->thisNetwop));
            scanCtl.MsgCallback(msgbuf);
            // count the number of newly found providers
            scanCtl.newProvCount += 1;
         }
         else
         {
            sprintf(msgbuf, "Provider already available: %s", AI_GET_NETWOP_NAME(pNewAi, pNewAi->thisNetwop));
            scanCtl.MsgCallback(msgbuf);
         }

         // update parameters with the new ones, if neccessary
         scanCtl.pDbContext->appId = oldAppId;

         if (scanCtl.pDbContext->tunerFreq != oldTunerFreq)
         {
            dprintf2("EpgScan: updating tuner freq: %.2f -> %.2f\n", (double)scanCtl.pDbContext->tunerFreq/16, (double)oldTunerFreq/16);
            scanCtl.pDbContext->modified = TRUE;
            scanCtl.pDbContext->tunerFreq = oldTunerFreq;

            if (EpgDbContextGetCni(scanCtl.pDbContext) != 0)
            {
               scanCtl.MsgCallback("storing provider's tuner frequency");
               EpgContextCtl_UpdateFreq(AI_GET_CNI(pNewAi), scanCtl.pDbContext->tunerFreq);
            }

            // store the provider channel frequency in the rc/ini file
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
         // save info that a BI was found on this channel
         scanCtl.foundBi = TRUE;

         if (scanCtl.pDbContext->appId != EPG_ILLEGAL_APPID)
         {
            if (scanCtl.pDbContext->appId != pNewBi->app_id)
            {  // not the default id
               dprintf2("EpgCtl: app-ID changed from %d to %d\n", scanCtl.pDbContext->appId, scanCtl.pDbContext->appId);
               EpgDbAcqReset(scanCtl.pDbContext, &scanCtl.dbQueue, EPG_ILLEGAL_PAGENO, pNewBi->app_id);
            }
         }
         else
            dprintf1("EpgScan-BiCallback: BI now in db, appID=%d\n", pNewBi->app_id);
      }
   }

   // the BI block is never added to the db
   return FALSE;
}

// ----------------------------------------------------------------------------
// Process all available lines from VBI and check for BI and AI blocks
// 
void EpgScan_ProcessPackets( void )
{
   bool stopped;
   uint pageNo;

   if (scanCtl.pDbContext != NULL)
   {
      // the check function must be called even if EPG acq is not yet enabled
      // because it also handles channel changes and watches over the slave's life
      if (EpgDbAcqCheckForPackets(&stopped))
      {
         if (scanCtl.state >= SCAN_STATE_WAIT_EPG)
         {
            if (EpgDbAcqProcessPackets() == FALSE)
            {
               // Notification from acquisition about channel change: should not be reached, because
               // the video and vbi devices are kepty busy, so no external channel changes can occur
               fatal0("EpgScan-ProcessPackets: uncontrolled channel change detected");
            }
            EpgDbProcessQueueByType(&scanCtl.pDbContext, &scanCtl.dbQueue, BLOCK_TYPE_BI, &epgScanCb);
            EpgDbProcessQueueByType(&scanCtl.pDbContext, &scanCtl.dbQueue, BLOCK_TYPE_AI, &epgScanCb);

            // accept OI block #0 because its message is displayed in the prov selection dialog
            if (EpgDbContextGetCni(scanCtl.pDbContext) != 0)
               EpgDbProcessQueueByType(&scanCtl.pDbContext, &scanCtl.dbQueue, BLOCK_TYPE_OI, &epgScanCb);
         }
      }

      if (stopped == FALSE)
      {
         pageNo = EpgDbAcqGetMipPageNo();
         if ((pageNo != EPG_ILLEGAL_PAGENO) && (pageNo != scanCtl.pDbContext->pageNo))
         {  // found a different page number in MIP
            dprintf2("EpgScan-ProcessPackets: non-default MIP page no for EPG: %03X (was %03X) -> restart acq\n", pageNo, scanCtl.pDbContext->pageNo);

            scanCtl.pDbContext->pageNo = pageNo;
            EpgDbAcqReset(scanCtl.pDbContext, &scanCtl.dbQueue, pageNo, EPG_ILLEGAL_APPID);
         }
      }
      else
      {
         EpgScan_Stop();
      }
   }
}

// ----------------------------------------------------------------------------
// EPG scan timer event handler - called every 250ms
// 
uint EpgScan_EvHandler( void )
{
   EPGDB_CONTEXT  * pDbContext;
   const AI_BLOCK * pAi;
   const char * pName, * pCountry;
   time_t now = time(NULL);
   uchar chanName[10], msgbuf[300];
   ulong freq;
   uint32_t ttxPkgCount, epgPkgCount, epgPageCount;
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
         if ( scanCtl.doSlow || scanCtl.useXawtv || scanCtl.doRefresh ||
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
            EpgDbLockDatabase(scanCtl.pDbContext, TRUE);
            pAi = EpgDbGetAi(scanCtl.pDbContext);
            if (pAi != NULL)
            {  // AI block has been received -> done
               assert(scanCtl.pDbContext->modified);

               scanCtl.state = SCAN_STATE_DONE;
            }
            EpgDbLockDatabase(scanCtl.pDbContext, FALSE);
         }
         if ((cni != 0) && ((scanCtl.state <= SCAN_STATE_WAIT_NI) || (scanCtl.state == SCAN_STATE_WAIT_NI_OR_EPG)))
         {
            dprintf2("Found VPS/PDC/NI 0x%04X on channel %d\n", cni, scanCtl.channel);
            // determine network name (e.g. "EuroNews") and country
            pName = CniGetDescription(cni, &pCountry);
            // determine channel name (e.g. "SE10")
            if (scanCtl.provFreqCount == 0)
               TvChannels_GetName(scanCtl.channel, chanName, sizeof(chanName));
            else
               sprintf(chanName, "#%d", scanCtl.channel);
            // append country if available
            sprintf(msgbuf, "Channel %s: CNI 0x%04X %s", chanName, cni, ((pName != NULL) ? pName : ""));
            if ((pCountry != NULL) && (strstr(pName, pCountry) == NULL))
               sprintf(msgbuf + strlen(msgbuf), " (%s)", pCountry);
            scanCtl.MsgCallback(msgbuf);

            // check if there's already a database for this provider
            // note: must do a complete open, not only a peek, to make sure the db is intact
            pDbContext = EpgContextCtl_Open(cni, CTX_FAIL_RET_NULL, CTX_RELOAD_ERR_NONE);
            if (pDbContext != NULL)
            {  // database available
               EpgDbLockDatabase(pDbContext, TRUE);
               pAi = EpgDbGetAi(pDbContext);
               if (pAi != NULL)
               {
                  sprintf(msgbuf, "Provider '%s' is already known - skipping.",
                                  AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
                  scanCtl.MsgCallback(msgbuf);
                  // in refresh mode count this provider as done (used in the summary)
                  if (scanCtl.doRefresh)
                     scanCtl.newProvCount += 1;
               }
               if (pDbContext->tunerFreq != scanCtl.pDbContext->tunerFreq)
               {
                  scanCtl.MsgCallback("storing provider's tuner frequency");
                  EpgContextCtl_UpdateFreq(cni, scanCtl.pDbContext->tunerFreq);
               }
               UiControlMsg_NewProvFreq(cni, scanCtl.pDbContext->tunerFreq);

               EpgDbLockDatabase(pDbContext, FALSE);
               EpgContextCtl_Close(pDbContext);

               scanCtl.state = SCAN_STATE_DONE;
            }
            else
            {  // no database for this CNI yet: check if it's a known provider
               if (CniIsKnownProvider(cni))
               {
                  // known provider -> wait for BI/AI
                  if (scanCtl.state <= SCAN_STATE_WAIT_NI)
                     scanCtl.MsgCallback("checking for EPG transmission...");
                  scanCtl.state = SCAN_STATE_WAIT_EPG;
               }
               else
               {  // CNI not known as provider -> keep checking for data page
                  scanCtl.state = SCAN_STATE_WAIT_DATA;
               }
            }
         }
         else if ((dataPageCnt != 0) && (scanCtl.state <= SCAN_STATE_WAIT_DATA))
         {
            dprintf2("Found %d data pages on channel %d\n", dataPageCnt, scanCtl.channel);
            if (scanCtl.state != SCAN_STATE_WAIT_DATA)
            {  // CNI not known -> prepend message with channel info
               if (scanCtl.provFreqCount == 0)
                  TvChannels_GetName(scanCtl.channel, chanName, sizeof(chanName));
               else
                  sprintf(chanName, "#%d", scanCtl.channel);
               sprintf(msgbuf, "Channel %s: found some kind of data transmission", chanName);
            }
            else
            {  // CNI already reported to the user -> just print the reason we start waiting
               sprintf(msgbuf, "Found some kind of data transmission on %d TTX page%s", dataPageCnt, ((dataPageCnt == 1) ? "" : "s"));
            }
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
      if (scanCtl.doRefresh && (delay < 25))
         delay = 25;
      if (scanCtl.doSlow)
         delay *= 2;

      if ( (scanCtl.state == SCAN_STATE_DONE) || ((now - scanCtl.startTime) >= delay) )
      {  // max wait exceeded -> next channel

         // print a message why the search is aborted
         if ( (scanCtl.state == SCAN_STATE_WAIT_NI_OR_EPG) ||
              (scanCtl.state == SCAN_STATE_WAIT_EPG) )
         {
            if (scanCtl.foundBi)
            {
               if (EpgDbQueue_GetBlockCount(&scanCtl.dbQueue) > 0)
                  scanCtl.MsgCallback("Nextview transmission found, but no application info received");
               else if (scanCtl.foundBi)
                  scanCtl.MsgCallback("Nextview announced in \"Bundle Inventory\", but no data received");
               scanCtl.MsgCallback("Giving up, try again later.");
            }
            else
               scanCtl.MsgCallback("No EPG transmission found on this channel - giving up.");
         }
         else if ( (scanCtl.useXawtv || scanCtl.doRefresh) &&
                   (scanCtl.state <= SCAN_STATE_WAIT_NI) )
         {  // no CNI found one a predefined channel -> inform user
            if (scanCtl.provFreqCount == 0)
               TvChannels_GetName(scanCtl.channel, chanName, sizeof(chanName));
            else
               sprintf(chanName, "#%d", scanCtl.channel);
            sprintf(msgbuf, "Channel %s: no CNI received - skipping", chanName);
            scanCtl.MsgCallback(msgbuf);
         }

         if (scanCtl.doRefresh && (scanCtl.state != SCAN_STATE_DONE))
         {  // refresh failed on one providr
            // load the database to check if it's defective
            bool isDefective = TRUE;
            pDbContext = EpgContextCtl_Open(scanCtl.provCniTab[scanCtl.channel - 1], CTX_FAIL_RET_NULL, CTX_RELOAD_ERR_NONE);
            if (pDbContext != NULL)
            {  // load succeeded -> not defective
               EpgDbLockDatabase(pDbContext, TRUE);
               pAi = EpgDbGetAi(pDbContext);
               if (pAi != NULL)
               {
                  sprintf(msgbuf, "Provider '%s' not refreshed, but database OK.", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
                  isDefective = FALSE;
               }
               EpgDbLockDatabase(pDbContext, FALSE);
               EpgContextCtl_Close(pDbContext);
            }
            if (isDefective)
            {
               sprintf(msgbuf, "Defective database 0x%04X remains to be repaired.", scanCtl.provCniTab[scanCtl.channel - 1]);
               scanCtl.badProvCount += 1;
            }
            scanCtl.MsgCallback(msgbuf);
         }

         if ( EpgScan_NextChannel(&freq) )
         {
            EpgDbAcqNotifyChannelChange();
            if ( BtDriver_TuneChannel(freq, TRUE) )
            {
               // automatically dump db if provider was found, then free resources
               assert((scanCtl.pDbContext->pAiBlock == NULL) || (scanCtl.pDbContext->tunerFreq != 0));
               EpgContextCtl_Close(scanCtl.pDbContext);

               scanCtl.pDbContext = EpgContextCtl_OpenDummy();
               scanCtl.pDbContext->tunerFreq = freq;

               // Note: Reset initializes the app-ID and ttx page no, hence it must be called after the db switch
               EpgDbAcqReset(scanCtl.pDbContext, &scanCtl.dbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
               EpgDbAcqInitScan();

               dprintf1("RESET channel %d\n", scanCtl.channel);
               scanCtl.startTime = now;
               scanCtl.state = SCAN_STATE_RESET;
               scanCtl.foundBi = FALSE;

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
            else if (scanCtl.doRefresh == FALSE)
            {
               if (scanCtl.newProvCount > 0)
               {
                  sprintf(msgbuf, "Found %d new Nextview provider%s.", scanCtl.newProvCount, ((scanCtl.newProvCount == 1) ? "" : "s"));
                  scanCtl.MsgCallback(msgbuf);
               }
               else
               {
                  if (EpgContextCtl_GetProvCount() > 0)
                     scanCtl.MsgCallback("\nSorry, no new providers found.");
                  else
                     scanCtl.MsgCallback("\nSorry, no providers found.");

                  scanCtl.MsgCallback("Please try again at a different time of day, as not all\n"
                                      "providers transmit at all hours (due to channel sharing\n"
                                      "or technical difficulties). Press \"Help\" for more info.");
               }
            }
            else
            {
               sprintf(msgbuf, "\nSummary:\n"
                               "%d provider database%s confirmed\n"
                               "%d database%s not refreshed\n"
                               "%d of these %s defective or missing",
                               scanCtl.newProvCount, ((scanCtl.newProvCount == 1) ? "" : "s"),
                               scanCtl.provFreqCount - scanCtl.newProvCount, ((scanCtl.provFreqCount - scanCtl.newProvCount == 1) ? "" : "s"),
                               scanCtl.badProvCount, ((scanCtl.badProvCount == 1) ? "is" : "are"));
               scanCtl.MsgCallback(msgbuf);
               if (scanCtl.badProvCount > 0)
                  scanCtl.MsgCallback("\nPlease remove remaining defective databases manually.");
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
// - when acquisition is already running it's not stopped first, because
//   there's a significant delay when the VBI driver is closed and reopened;
//   instead a special "suspend" command is invoked in normal acqctl.
// - user-configured parameters:
//   doSlow:    doubles all timeouts; can be changed during run-time
//   useXawtv:  indicates that a predefined channel table is used
//              skip video signal test; warn about missing CNIs
//   doRefresh: work only on freqs of already known providers
//
EPGSCAN_START_RESULT EpgScan_Start( int inputSource, bool doSlow, bool useXawtv, bool doRefresh,
                                    uint * cniTab, ulong *freqTab, uint freqCount,
                                    uint * pRescheduleMs, void (* MsgCallback)(const char * pMsg) )
{
   ulong freq;
   bool  isTuner;
   EPGSCAN_START_RESULT result;
   uchar chanName[10], msgbuf[80];
   uint rescheduleMs;

   scanCtl.doRefresh     = doRefresh;
   scanCtl.doSlow        = doSlow;
   scanCtl.useXawtv      = useXawtv;
   scanCtl.acqWasEnabled = (pAcqDbContext != NULL);
   scanCtl.provFreqCount = freqCount;
   scanCtl.provFreqTab   = freqTab;
   scanCtl.provCniTab    = cniTab;
   scanCtl.MsgCallback   = MsgCallback;
   scanCtl.pDbContext    = NULL;
   result = EPGSCAN_OK;

   rescheduleMs = 0;

   if ( ((useXawtv || doRefresh) && ((freqTab == NULL) || (freqCount == 0))) ||
        (doRefresh && (cniTab == NULL)) ||
        (MsgCallback == NULL) ||
        (pRescheduleMs == NULL) )
   {
      fatal7("EpgScan-Start: illegal NULL ptr param (doRefresh=%d, useXawtv=%d): freqCount=%d, cniTab=%ld, freqTab=%ld, msgCb=%ld, resched=%ld", doRefresh, useXawtv, freqCount, (long)cniTab, (long)freqTab, (long)MsgCallback, (long)pRescheduleMs);
      result = EPGSCAN_INTERNAL;
   }
   else
   {  // start EPG acquisition
      scanCtl.pDbContext = EpgContextCtl_OpenDummy();
      if (scanCtl.acqWasEnabled == FALSE)
      {
         EpgDbAcqInit();
         EpgDbQueue_Init(&scanCtl.dbQueue);
         EpgDbAcqStart(scanCtl.pDbContext, &scanCtl.dbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
         if (BtDriver_StartAcq() == FALSE)
            result = EPGSCAN_ACCESS_DEV_VBI;
      }
      else
         EpgAcqCtl_Suspend(TRUE);
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

               EpgDbAcqReset(scanCtl.pDbContext, &scanCtl.dbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
               EpgDbAcqInitScan();

               dprintf1("RESET channel %d\n", scanCtl.channel);
               scanCtl.state = SCAN_STATE_RESET;
               scanCtl.startTime = time(NULL);
               scanCtl.foundBi = FALSE;
               rescheduleMs = 50;
               scanCtl.signalFound = 0;
               scanCtl.newProvCount = 0;
               scanCtl.badProvCount = 0;

               if (scanCtl.provFreqCount == 0)
               {
                  TvChannels_GetName(scanCtl.channel, chanName, sizeof(chanName));
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
      if (scanCtl.pDbContext != NULL)
      {
         EpgContextCtl_Close(scanCtl.pDbContext);
         scanCtl.pDbContext = NULL;

         if (scanCtl.acqWasEnabled == FALSE)
            EpgDbAcqStop();
         else if (pAcqDbContext == NULL)
            EpgAcqCtl_Suspend(FALSE);
      }

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
      // do a reset to clear the db queue
      EpgDbAcqReset(scanCtl.pDbContext, &scanCtl.dbQueue, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);

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
      scanCtl.state = SCAN_STATE_OFF;

      if (scanCtl.pDbContext != NULL)
      {
         EpgContextCtl_Close(scanCtl.pDbContext);
         scanCtl.pDbContext = NULL;
      }

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

