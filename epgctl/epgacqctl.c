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
 *    Controls the acquisition process.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgacqctl.c,v 1.13 2000/07/08 18:31:47 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <string.h>
#include <time.h>

#include "tcl.h"

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbacq.h"
#include "epgui/pifilter.h"
#include "epgui/menucmd.h"
#include "epgui/statswin.h"
#include "epgctl/epgmain.h"
#include "epgctl/vbidecode.h"

#include "epgctl/epgacqctl.h"


static EPGACQ_STATE acqState    = ACQSTATE_OFF;
static EPGDB_STATS acqStats;
static bool acqStatsUpdate;
static ulong dumpTime;

static EPGSCAN_STATE  scanState = SCAN_STATE_OFF;
static Tcl_TimerToken scanHandler = NULL;
static bool           scanAcqWasEnabled;
static uint           scanChannel;
static uint           scanChannelCount;


// ---------------------------------------------------------------------------
// Reset statistic values
// - should be called right after start/reset acq and once after first AI
//
static void EpgAcqCtl_StatisticsReset( void )
{
   memset(&acqStats, 0, sizeof(acqStats));

   acqStats.acqStartTime  = time(NULL);
   acqStats.lastAiTime    = acqStats.acqStartTime;
   acqStats.minAiDistance = 0;
   acqStats.maxAiDistance = 0;
   acqStats.aiCount       = 0;
   acqStats.histIdx       = STATS_HIST_WIDTH - 1;

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
   ulong total, obsolete;
   ulong now = time(NULL);

   acqStatsUpdate = FALSE;

   // determine block counts in current database
   EpgDbGetStat(pAcqDbContext, acqStats.count);

   if (acqState == ACQSTATE_RUNNING)
   {
      acqStats.lastAiTime = now;
      acqStats.aiCount += 1;

      // maintain history of block counts per stream
      total = acqStats.count[0].ai + acqStats.count[1].ai;
      obsolete = acqStats.count[0].obsolete + acqStats.count[1].obsolete;
      dprintf2("EpgAcqCtl-StatisticsUpdate: AI #%d, db filled %.2f%%\n", acqStats.aiCount, (double)(acqStats.count[0].allVersions + obsolete + acqStats.count[1].allVersions) / total * 100.0);

      acqStats.histIdx = (acqStats.histIdx + 1) % STATS_HIST_WIDTH;
      acqStats.hist_expir[acqStats.histIdx] = (uchar)((double)obsolete / total * 128.0);
      acqStats.hist_s1cur[acqStats.histIdx] = (uchar)((double)(acqStats.count[0].curVersion + obsolete) / total * 128.0);
      acqStats.hist_s1old[acqStats.histIdx] = (uchar)((double)(acqStats.count[0].allVersions + obsolete) / total * 128.0);
      acqStats.hist_s2cur[acqStats.histIdx] = (uchar)((double)(acqStats.count[0].allVersions + obsolete + acqStats.count[1].curVersion) / total * 128.0);
      acqStats.hist_s2old[acqStats.histIdx] = (uchar)((double)(acqStats.count[0].allVersions + obsolete + acqStats.count[1].allVersions) / total * 128.0);

      // compute minimum and maximum distance between AI blocks (in seconds)
      if (acqStats.minAiDistance > 0)
      {
         if (now - acqStats.lastAiTime < acqStats.minAiDistance)
            acqStats.minAiDistance = now - acqStats.lastAiTime;
         if (now - acqStats.lastAiTime > acqStats.maxAiDistance)
            acqStats.maxAiDistance = now - acqStats.lastAiTime;
      }
      else
         acqStats.maxAiDistance = acqStats.minAiDistance = now - acqStats.lastAiTime;
   }
}

// ---------------------------------------------------------------------------
// Return statistic values for AcqStat window
//
const EPGDB_STATS * EpgAcqCtl_GetStatistics( void )
{
   assert(acqStatsUpdate == FALSE);  // must not be called asynchronously

   if (acqState != ACQSTATE_OFF)
   {
      dprintf1("EpgAcqCtl_GetStatistics: stats requested: ai #%d\n", acqStats.aiCount);
      // retrieve additional values from ttx decoder
      EpgDbAcqGetStatistics(&acqStats.ttxPkgCount, &acqStats.epgPkgCount, &acqStats.epgPagCount);

      return (const EPGDB_STATS *) &acqStats;
   }
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Start the acquisition
//
bool EpgAcqCtl_Start( void )
{
   bool result;

   if (acqState == ACQSTATE_OFF)
   {
      // initialize teletext decoder
      #ifndef WIN32
      EpgDbAcqInit(pVbiBuf);
      #else
      EpgDbAcqInit(NULL);
      #endif

      assert(pAcqDbContext == NULL);
      EpgAcqCtl_OpenDb(DB_TARGET_ACQ, 0);

      #ifndef WIN32
      // try to tune onto the provider's channel
      // (before starting acq, to avoid catching false data)
      if (pAcqDbContext->tunerFreq != 0)
         VbiTuneChannel(pAcqDbContext->tunerFreq, FALSE);
      #endif

      EpgDbAcqStart(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);

      #ifndef WIN32
      if (VbiDecodeWakeUp() == FALSE)
      {
         EpgDbAcqStop();
         EpgAcqCtl_CloseDb(DB_TARGET_ACQ);
         result = FALSE;
      }
      else
      #endif
      {
         acqState = ACQSTATE_WAIT_BI;

         EpgAcqCtl_StatisticsReset();
         EpgAcqCtl_StatisticsUpdate();
         dumpTime = acqStats.acqStartTime;

         result = TRUE;
      }
   }
   else
      result = TRUE;

   return result;
}

// ---------------------------------------------------------------------------
// Stop the acquisition
// - the acq-sub process is terminated and the VBI device is freed
// - this allows other processes (e.g. a teletext decoder) to access VBI
//
void EpgAcqCtl_Stop( void )
{
   if (acqState != ACQSTATE_OFF)
   {
      assert(pAcqDbContext != NULL);

      EpgDbAcqStop();
      EpgAcqCtl_CloseDb(DB_TARGET_ACQ);

      acqState = ACQSTATE_OFF;
   }
}

// ---------------------------------------------------------------------------
// Start or stop the acquisition (called from UI button callback)
// - returns the new status of acq: TRUE=running, FALSE=stopped
//
int EpgAcqCtl_Toggle( int newState )
{
   if (newState != acqState)
   {
      if (acqState == ACQSTATE_OFF)
      {
         EpgAcqCtl_Start();
      }
      else
      {
         EpgAcqCtl_Stop();
      }
   }
   else
   {  // no change
      debug1("EpgAcqCtl-Toggle: requested state %d already set", newState);
   }

   return acqState;
}

// ---------------------------------------------------------------------------
// AI callback: invoked before a new AI block is inserted into the database
//
bool EpgAcqCtl_AiCallback( const AI_BLOCK *pNewAi )
{
   const AI_BLOCK *pOldAi;
   bool accept = FALSE;

   if (acqState == ACQSTATE_WAIT_BI)
   {
      accept = FALSE;
   }
   else if ((acqState == ACQSTATE_WAIT_AI) || (acqState == ACQSTATE_RUNNING))
   {
      DBGONLY(if (acqState == ACQSTATE_WAIT_AI))
         dprintf3("EpgCtl: AI found, CNI=%X version %d/%d\n", AI_GET_CNI(pNewAi), pNewAi->version, pNewAi->version_swo);
      EpgDbLockDatabase(pAcqDbContext, TRUE);
      pOldAi = EpgDbGetAi(pAcqDbContext);

      if (pOldAi == NULL)
      {  // the current db is empty
         EpgDbLockDatabase(pAcqDbContext, FALSE);
         DBGONLY(pOldAi = NULL);

         if (EpgDbContextGetCni(pUiDbContext) == AI_GET_CNI(pNewAi))
         {  // the UI has already opened that db -> use that one
            // no need to save current db, since there's not even an AI
            EpgDbDestroy(pAcqDbContext);
            pAcqDbContext = pUiDbContext;
         }
         else
         { // if a db for this provider is already available -> load it
            EpgDbReload(pAcqDbContext, AI_GET_CNI(pNewAi));
         }
         // update the ui netwop list if neccessary
         Tcl_DoWhenIdle(PiFilter_UpdateNetwopList, NULL);
         acqState = ACQSTATE_RUNNING;
         EpgAcqCtl_StatisticsReset();
         accept = TRUE;

         // if switched to ui db or reload succeeded -> must check AI again
         EpgDbLockDatabase(pAcqDbContext, TRUE);
         pOldAi = EpgDbGetAi(pAcqDbContext);
         if (pOldAi == NULL)
         {  // new provider -> dump asap, to show up in prov select menu
            dumpTime = 0;
         }
      }

      if (pOldAi != NULL)
      {
         if (AI_GET_CNI(pOldAi) == AI_GET_CNI(pNewAi))
         {
            if ( (pOldAi->version != pNewAi->version) ||
                 (pOldAi->version_swo != pNewAi->version_swo) )
            {
               dprintf2("EpgAcqCtl: version number has changed, was: %d/%d\n", pOldAi->version, pOldAi->version_swo);
               Tcl_DoWhenIdle(PiFilter_UpdateNetwopList, NULL);
               EpgAcqCtl_StatisticsReset();
               dumpTime = 0;  // dump asap
            }
            else
            {  // just a regular AI repetition, no news
               acqStatsUpdate = TRUE;
            }

            acqState = ACQSTATE_RUNNING;
            accept = TRUE;
         }
         else
         {  // wrong provider -> switch acq database
            dprintf2("EpgAcqCtl: switching acq db from %04X to %04X\n", AI_GET_CNI(pOldAi), AI_GET_CNI(pNewAi));
            EpgDbLockDatabase(pAcqDbContext, FALSE);
            EpgAcqCtl_CloseDb(DB_TARGET_ACQ);
            EpgAcqCtl_OpenDb(DB_TARGET_ACQ, AI_GET_CNI(pNewAi));

            EpgDbLockDatabase(pAcqDbContext, TRUE);
            assert((EpgDbContextGetCni(pAcqDbContext) == AI_GET_CNI(pNewAi)) || (EpgDbContextGetCni(pAcqDbContext) == 0));
            EpgDbAcqReset(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
            EpgAcqCtl_StatisticsReset();
            // dump db asap to allow user to select the new provider
            pOldAi = EpgDbGetAi(pAcqDbContext);
            if (pOldAi == NULL)
               dumpTime = 0;
            else
               dumpTime = time(NULL);
            StatsWin_ProvChange(DB_TARGET_ACQ);
            acqState = ACQSTATE_WAIT_BI;
            accept = TRUE;
         }
      }
      EpgDbLockDatabase(pAcqDbContext, FALSE);
   }
   else
   {  // should be reached only during epg scan -> discard block
      assert(scanState != SCAN_STATE_OFF);
   }

   return accept;
}

// ---------------------------------------------------------------------------
// Called by the database management before a new BI block is inserted
//
bool EpgAcqCtl_BiCallback( const BI_BLOCK *pNewBi )
{
   const BI_BLOCK *pOldBi;
   bool  accept = FALSE;

   if (pNewBi->app_id == EPG_ILLEGAL_APPID)
   {
      dprintf0("EpgCtl: EPG not listed in BI - stop acq\n");
   }
   else
   {
      EpgDbLockDatabase(pAcqDbContext, TRUE);
      pOldBi = EpgDbGetBi(pAcqDbContext);
      if (pOldBi != NULL)
      {
         if ((pOldBi != NULL) && (pOldBi->app_id != pNewBi->app_id))
         {  // not the default id
            dprintf2("EpgCtl: app-ID changed from %d to %d\n", pOldBi->app_id, pNewBi->app_id);
            EpgDbAcqReset(pAcqDbContext, EPG_ILLEGAL_PAGENO, pNewBi->app_id);
         }
      }
      else
         dprintf1("EpgAcqCtl-BiCallback: BI now in db, appID=%d\n", pNewBi->app_id);
      EpgDbLockDatabase(pAcqDbContext, FALSE);

      switch (acqState)
      {
         case ACQSTATE_WAIT_BI:
            dprintf1("EpgCtl: BI found, appId=%d\n", pNewBi->app_id);
            acqState = ACQSTATE_WAIT_AI;
            // BI can not be accepted until CNI in the following AI block is checked
            accept = FALSE;
            break;

         case ACQSTATE_WAIT_AI:
            // BI can not be accepted until CNI in the following AI block is checked
            accept = FALSE;
            break;

         case ACQSTATE_RUNNING:
            accept = TRUE;
            break;

         default:
            // should be reached only during epg scan -> discard block
            assert(scanState != SCAN_STATE_OFF);
            break;
      }
   }

   return accept;
}

// ---------------------------------------------------------------------------
// Notification from acquisition about channel change
//
void EpgAcqCtl_ChannelChange( void )
{
   if (acqState != ACQSTATE_OFF)
   {
      if ((pAcqDbContext != pUiDbContext) && (pUiDbContext != NULL))
      {  // close acq db and fall back to ui db
         EpgAcqCtl_CloseDb(DB_TARGET_ACQ);
         EpgAcqCtl_OpenDb(DB_TARGET_ACQ, 0);
      }

      EpgDbAcqReset(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
      acqState = ACQSTATE_WAIT_BI;
      EpgAcqCtl_StatisticsReset();
   }
}

// ---------------------------------------------------------------------------
// Periodic check of the acquisition process
//
void EpgAcqCtl_Idle( void )
{
   uint pageNo;

   if ( (acqState == ACQSTATE_WAIT_BI) || (acqState == ACQSTATE_WAIT_AI) )
   {
      // check the MIP if EPG is transmitted on a different page
      pageNo = EpgDbAcqGetMipPageNo();
      if ( (pageNo != EPG_ILLEGAL_PAGENO) && (pageNo != pAcqDbContext->pageNo))
      {  // found a different page number in MIP
         dprintf1("EpgAcqCtl-Idle: non-default MIP page no for EPG %x -> restart acq\n", pageNo);

         pAcqDbContext->pageNo = pageNo;
         EpgDbAcqReset(pAcqDbContext, pAcqDbContext->pageNo, EPG_ILLEGAL_APPID);
      }
   }
   else if (acqState == ACQSTATE_RUNNING)
   {
      if (pAcqDbContext->modified)
      {
         if (time(NULL) - dumpTime > EPGACQCTL_DUMP_INTV)
         {
            EpgDbLockDatabase(pAcqDbContext, TRUE);
            // check for special state directly after new provider found: BI still missing -> keep waiting
            if ( EpgDbGetBi(pAcqDbContext) != NULL )
            {
               if ( EpgDbDump(pAcqDbContext) )
               {
                  dprintf1("EpgAcqCtl-Idle: dumped db %04X to file\n", EpgDbContextGetCni(pAcqDbContext));
                  dumpTime = time(NULL);
               }
            }
            EpgDbLockDatabase(pAcqDbContext, FALSE);
         }
      }
      else
      {  // acq running, but no reception -> wait for timeout, then reset acq
         if (time(NULL) - dumpTime > EPGACQCTL_MODIF_INTV)
         {
            dprintf1("EpgAcqCtl-Idle: no reception from provider %04X - reset acq\n", EpgDbContextGetCni(pAcqDbContext));
            EpgAcqCtl_ChannelChange();
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Process all available lines from VBI and insert blocks to db
//
void EpgAcqCtl_ProcessPackets( void )
{
   acqStatsUpdate = FALSE;

   EpgDbAcqProcessPackets(&pAcqDbContext);

   // if new AI received, update statistics (after AI was inserted into db)
   if (acqStatsUpdate)
   {
      EpgAcqCtl_StatisticsUpdate();
   }
}

// ---------------------------------------------------------------------------
// Open a database for a specific (or the last used) provider
// - Careful! both ui and acq context pointers may change inside this function!
//   Caller stack must not contain (and use) any copies of the old pointers
//
bool EpgAcqCtl_OpenDb( int target, uint cni )
{
   bool result = TRUE;

   if (cni == 0)
   {  // load any provider -> use an already opene one or search for the best
      if ((target == DB_TARGET_UI) && (pAcqDbContext != NULL))
      {
         pUiDbContext = pAcqDbContext;
      }
      else if ((target == DB_TARGET_ACQ) && (pUiDbContext != NULL))
      {
         pAcqDbContext = pUiDbContext;
      }
      else
      {  // no db open yet -> search for the best
         cni = EpgDbReloadScan(".", -1);

         if (cni == 0)
         {  // no database found that could be opened -> create an empty one
            if (target == DB_TARGET_UI)
               pUiDbContext = EpgDbCreate();
            else
               pAcqDbContext = EpgDbCreate();
         }
      }
   }

   if (cni != 0)
   {
      switch (target)
      {
         case DB_TARGET_UI:
            if (EpgDbContextGetCni(pAcqDbContext) == cni)
            {  // db is already opened for acq
               pUiDbContext = pAcqDbContext;
            }
            else
            {
               pUiDbContext = EpgDbCreate();
               result = EpgDbReload(pUiDbContext, cni);

               #ifndef WIN32
               if ( (pUiDbContext->tunerFreq != 0) &&
                    VbiTuneChannel(pUiDbContext->tunerFreq, FALSE) )
               {  // tuned onto provider's channel
                  // XXX do nothing - channel change is handled automatically (but not very well)
               }
               else
               #endif
               if ((acqState == ACQSTATE_WAIT_BI) && (pAcqDbContext->modified == FALSE))
               {  // acq uses different db, but there's no EPG reception
                  EpgAcqCtl_CloseDb(DB_TARGET_ACQ);
                  // use ui context for acq
                  pAcqDbContext = pUiDbContext;
                  // reset acquisition
                  EpgAcqCtl_ChannelChange();
               }
            }
            break;

         case DB_TARGET_ACQ:
            if (EpgDbContextGetCni(pUiDbContext) == cni)
            {  // db is already opened for UI
               pAcqDbContext = pUiDbContext;
            }
            else
            {
               pAcqDbContext = EpgDbCreate();
               result = EpgDbReload(pAcqDbContext, cni);
            }
            break;

         default:
            SHOULD_NOT_BE_REACHED;
            result = FALSE;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Close a database
// - if the db was modified, it's automatically saved before the close
// - the db may be kept open by another party (ui or acq repectively)
//   so actually only the context pointer may be invalidated for the
//   calling party
//
void EpgAcqCtl_CloseDb( int target )
{
   switch (target)
   {
      case DB_TARGET_UI:
         if (pUiDbContext != NULL)
         {
            if (pAcqDbContext != pUiDbContext)
            {  // acq not running or using a different db -> save & free db
               EpgDbDump(pUiDbContext);
               EpgDbDestroy(pUiDbContext);
            }
            pUiDbContext = NULL;
         }
         else
            debug0("EpgAcqCtl-CloseDb: ui db context not opened");
         break;

      case DB_TARGET_ACQ:
         if (pAcqDbContext != NULL)
         {
            if (pAcqDbContext != pUiDbContext)
            {  // ui is using a different db -> save & free db
               EpgDbDump(pAcqDbContext);
               EpgDbDestroy(pAcqDbContext);
            }
            pAcqDbContext = NULL;
         }
         else
            debug0("EpgAcqCtl-CloseDb: acq db context not opened");
         break;

      default:
         SHOULD_NOT_BE_REACHED;
   }
}

#ifndef WIN32
// ----------------------------------------------------------------------------
//                             E P G    S C A N
// ----------------------------------------------------------------------------

// The purpose of the EPG scan is to find out the tuner frequencies of all
// EPG providers. This would not be needed it the Nextview decoder was
// integrated into a TV application. But as a separate application, there
// is no other way to find out the frequencies, since /dev/video can not
// be opened twice, not even to only query the actual tuner frequency.

// The scan is designed to perform as fast as possible. We stay max. 2 secs
// on each channel. If a CNI is found and it's a known and loaded provider,
// we go on immediately. If no CNI is found or if it's not a known provider,
// we wait if any syntactically correct packets are received on any of the
// 8*24 possible EPG teletext pages. If yes, we try for 45 seconds to
// receive the BI and AI blocks. This period is chosen that long, because
// some providers have gaps of over 45 seconds between cycles (e.g. RTL-II)
// and others do not transmit on the default page 1DF, so we have to wait
// for the MIP, which usually is transmitted about every 30 seconds. For
// any provider that's found, a database is created and the frequency
// saved in its header.

// ----------------------------------------------------------------------------
// EPG scan timer event handler - called every 250ms
// 
static void EventHandler_EpgScan( ClientData clientData )
{
   const EPGDBSAV_PEEK *pPeek;
   const AI_BLOCK *pAi;
   time_t now = time(NULL);
   ulong freq;
   uint cni, dataPageCnt;
   uint delay;
   bool niWait;

   scanHandler = NULL;
   if (scanState != SCAN_STATE_OFF)
   {
      if (scanState == SCAN_STATE_RESET)
      {  // reset state again 50ms after channel change
         EpgDbAcqInitScan();
         //printf("Start channel %d\n", scanChannel);
         if (VbiTuneGetSignalStrength() < 32768)
            scanState = SCAN_STATE_DONE;
         else
            scanState = SCAN_STATE_WAIT;
      }
      else
      {
         EpgDbAcqGetScanResults(&cni, &niWait, &dataPageCnt);

         if (scanState == SCAN_STATE_WAIT_EPG)
         {
            if ( (acqState == ACQSTATE_RUNNING) && (pAcqDbContext->pBiBlock != NULL) )
            {  // both AI and BI have been received and inserted into the db
               assert((pAcqDbContext->pAiBlock != NULL) && pAcqDbContext->modified);

               EpgDbDump(pAcqDbContext);

               EpgDbLockDatabase(pAcqDbContext, TRUE);
               pAi = EpgDbGetAi(pAcqDbContext);
               if (pAi != NULL)
               {
                  sprintf(comm, "Found new provider: %s", AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
                  MenuCmd_AddEpgScanMsg(comm);
                  scanState = SCAN_STATE_DONE;
               }
               EpgDbLockDatabase(pAcqDbContext, FALSE);
            }
         }
         else if ((cni != 0) && (scanState <= SCAN_STATE_WAIT_NI))
         {
            sprintf(comm, "Channel %d: network id 0x%04X", scanChannel, cni);
            MenuCmd_AddEpgScanMsg(comm);
            pPeek = EpgDbPeek(cni);
            if (pPeek != NULL)
            {  // provider already loaded -> skip
               sprintf(comm, "provider already known: %s",
                             AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, pPeek->pAiBlock->blk.ai.thisNetwop));
               MenuCmd_AddEpgScanMsg(comm);
               scanState = SCAN_STATE_DONE;
               if (pPeek->tunerFreq != pAcqDbContext->tunerFreq)
               {
                  MenuCmd_AddEpgScanMsg("storing provider's tuner frequency");
                  EpgDbDumpUpdateHeader(pAcqDbContext, cni, pAcqDbContext->tunerFreq);
               }
               EpgDbPeekDestroy(pPeek);
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
                     MenuCmd_AddEpgScanMsg("checking for EPG transmission...");
                     scanState = SCAN_STATE_WAIT_EPG;
                     // enable normal EPG acq callback handling
                     acqState = ACQSTATE_WAIT_BI;
                     break;
                  default:
                     // CNI not known as provider -> keep checking for data page
                     scanState = SCAN_STATE_WAIT_DATA;
                     break;
               }
            }
         }
         else if ((dataPageCnt != 0) && (scanState <= SCAN_STATE_WAIT_DATA))
         {
            sprintf(comm, "Found ETS-708 syntax on %d pages", dataPageCnt);
            MenuCmd_AddEpgScanMsg(comm);
            MenuCmd_AddEpgScanMsg("checking for EPG transmission...");
            scanState = SCAN_STATE_WAIT_EPG;
            // enable normal EPG acq callback handling
            acqState = ACQSTATE_WAIT_BI;
         }
         else if (niWait)
         {
            scanState = SCAN_STATE_WAIT_NI;
         }
      }

      // determine timeout for the current state
      switch (scanState)
      {
         case SCAN_STATE_WAIT:      delay =  2; break;
         case SCAN_STATE_WAIT_NI:   delay =  4; break;
         case SCAN_STATE_WAIT_DATA: delay =  2; break;
         case SCAN_STATE_WAIT_EPG:  delay = 45; break;
         default:                   delay =  0; break;
      }

      if ( (scanState == SCAN_STATE_DONE) || ((now - acqStats.acqStartTime) >= delay) )
      {  // max wait exceeded -> next channel
         if ( VbiGetNextChannel(&scanChannel, &freq) )
         {
            if ( VbiTuneChannel(freq, TRUE) )
            {
               EpgDbAcqReset(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
               EpgDbAcqInitScan();

               EpgDbDestroy(pAcqDbContext);
               pAcqDbContext = EpgDbCreate();
               pAcqDbContext->tunerFreq = freq;

               acqStats.acqStartTime = now;
               scanState = SCAN_STATE_RESET;
               acqState = ACQSTATE_OFF;
               scanChannelCount += 1;
               sprintf(comm, ".epgscan.all.baro.bari configure -width %d\n",
                             (int)((double)scanChannelCount/VbiGetChannelCount()*140.0));
               eval_check(interp, comm);

               scanHandler = Tcl_CreateTimerHandler(150, EventHandler_EpgScan, NULL);
            }
            else
            {
               EpgAcqCtl_StopScan();
               MenuCmd_AddEpgScanMsg("channel change failed - abort.");
               eval_check(interp, "bell");
               MenuCmd_StopEpgScan(NULL, interp, 0, NULL);
            }
         }
         else
         {
            EpgAcqCtl_StopScan();
            MenuCmd_AddEpgScanMsg("EPG scan finished.");
            eval_check(interp, "bell");
            MenuCmd_StopEpgScan(NULL, interp, 0, NULL);
         }
      }
      else
      {  // continue scan on current channel
         scanHandler = Tcl_CreateTimerHandler(250, EventHandler_EpgScan, NULL);
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
bool EpgAcqCtl_StartScan( void )
{
   ulong freq;
   bool result = FALSE;

   scanChannel = 0;
   if ( VbiGetNextChannel(&scanChannel, &freq) &&
        VbiTuneChannel(freq, TRUE) )
   {
      // stop acq if running, but remember the state for when scan finishes
      scanAcqWasEnabled = (acqState != ACQSTATE_OFF);
      EpgAcqCtl_Stop();

      // set up an empty db and start EPG and CNI acquisition
      pAcqDbContext = EpgDbCreate();
      pAcqDbContext->tunerFreq = freq;
      EpgDbAcqInit(pVbiBuf);
      EpgDbAcqStart(pAcqDbContext, EPG_ILLEGAL_PAGENO, EPG_ILLEGAL_APPID);
      EpgDbAcqInitScan();

      if (VbiDecodeWakeUp())
      {
         scanState = SCAN_STATE_RESET;
         acqState = ACQSTATE_OFF;
         acqStats.acqStartTime = time(NULL);
         scanHandler = Tcl_CreateTimerHandler(150, EventHandler_EpgScan, NULL);
         scanChannelCount = 0;

         sprintf(comm, "Starting scan on channel %d", scanChannel);
         MenuCmd_AddEpgScanMsg(comm);
         sprintf(comm, ".epgscan.all.baro.bari configure -width 1\n");
         eval_check(interp, comm);

         result = TRUE;
      }
      else
      {  // failed to start acquisition
         EpgDbAcqStop();
         EpgDbDestroy(pAcqDbContext);
         pAcqDbContext = NULL;
         // restart normal acq, if it was enabled before the scan started
         if (scanAcqWasEnabled)
            EpgAcqCtl_Start();
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Stop EPG scan
// - might be called repeatedly by UI to ensure background activity has stopped
//
void EpgAcqCtl_StopScan( void )
{
   if (scanState != SCAN_STATE_OFF)
   {
      Tcl_DeleteTimerHandler(scanHandler);
      scanHandler = NULL;

      VbiTuneCloseDevice();

      EpgDbAcqStop();
      EpgDbDestroy(pAcqDbContext);
      pAcqDbContext = NULL;

      if (scanAcqWasEnabled)
         EpgAcqCtl_Start();

      scanState = SCAN_STATE_OFF;
   }
}
#endif //WIN32

