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
 *  $Id: epgacqctl.c,v 1.7 2000/06/15 17:10:57 tom Exp tom $
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
static ulong dumpTime;

// ---------------------------------------------------------------------------
// Reset statistic values
// - should be called right after start/reset acq and once after first AI
// - note: call this function only AFTER setting acqState
//
static void EpgAcqCtl_StatisticsReset( void )
{
   memset(&acqStats, 0, sizeof(acqStats));

   acqStats.acqStartTime  = time(NULL);
   acqStats.lastAiTime    = acqStats.acqStartTime;
   acqStats.minAiDistance = 0;
   acqStats.maxAiDistance = 0;
   acqStats.aiCount       = ((acqState > ACQSTATE_WAIT_BI) ? 1 : 0);
   acqStats.histIdx       = STATS_HIST_WIDTH - 1;

   EpgDbGetStat(pAcqDbContext, &acqStats.blockCount1, &acqStats.blockCount2,
                               &acqStats.curBlockCount1, &acqStats.curBlockCount2, &acqStats.blockCountAi);
}

// ---------------------------------------------------------------------------
// Maintain statistic values for AcqStat window
// - should be called after each received AI block
// - the history is also maintained when no stats window is open, because
//   once one is opened, a complete history can be displayed
//
static void EpgAcqCtl_StatisticsUpdate( void )
{
   ulong now = time(NULL);

   // determine block counts in current database
   EpgDbGetStat(pAcqDbContext, &acqStats.blockCount1, &acqStats.blockCount2,
                               &acqStats.curBlockCount1, &acqStats.curBlockCount2, &acqStats.blockCountAi);
   // maintain history of block counts per stream
   acqStats.histIdx = (acqStats.histIdx + 1) % STATS_HIST_WIDTH;
   acqStats.hist_s1cur[acqStats.histIdx] = (uchar)((double)acqStats.curBlockCount1 / acqStats.blockCountAi * 128.0);
   acqStats.hist_s1old[acqStats.histIdx] = (uchar)((double)acqStats.blockCount1 / acqStats.blockCountAi * 128.0);
   acqStats.hist_s2cur[acqStats.histIdx] = (uchar)((double)(acqStats.blockCount1 + acqStats.curBlockCount2) / acqStats.blockCountAi * 128.0);
   acqStats.hist_s2old[acqStats.histIdx] = (uchar)((double)(acqStats.blockCount1 + acqStats.blockCount2) / acqStats.blockCountAi * 128.0);

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

   acqStats.lastAiTime = now;
   acqStats.aiCount += 1;
}

// ---------------------------------------------------------------------------
// Return statistic values for AcqStat window
//
const EPGDB_STATS * EpgAcqCtl_GetStatistics( void )
{
   if (acqState != ACQSTATE_OFF)
   {
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
               EpgAcqCtl_StatisticsUpdate();
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
      debug1("EpgAcqCtl: illegal acq state %d", acqState);

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
            SHOULD_NOT_BE_REACHED;
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
// Open a database for a specific (or the last used) provider
// - Careful! both ui and acq context pointers may change inside this function!
//   Caller stack must not contain (and use) any copies of the old pointers
//
void EpgAcqCtl_OpenDb( int target, uint cni )
{
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
            else if ((acqState == ACQSTATE_WAIT_BI) && (pAcqDbContext->modified == FALSE))
            {  // acq uses different db, but there's no EPG reception
               EpgAcqCtl_CloseDb(DB_TARGET_ACQ);
               // use new db context for both ui and acq
               pUiDbContext = pAcqDbContext = EpgDbCreate();
               EpgDbReload(pUiDbContext, cni);
               // reset acquisition
               EpgAcqCtl_ChannelChange();
            }
            else
            {
               pUiDbContext = EpgDbCreate();
               EpgDbReload(pUiDbContext, cni);
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
               EpgDbReload(pAcqDbContext, cni);
            }
            break;

         default:
            SHOULD_NOT_BE_REACHED;
      }
   }
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

