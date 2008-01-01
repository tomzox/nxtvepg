/*
 *  Nextview EPG merged context management
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
 *    Merges serveral databases into one: At the start there's always a
 *    complete merge of all PI in a number of given databases into a newly
 *    created database. The first provider in the list is the 'master',
 *    i.e. if there are differing start times between providers, only PI
 *    that do not conflict with the master will be added. There's no
 *    comparison of other creteria than start and stop time, so it is
 *    not detected if two providers list two different programmes on one
 *    network with accidentially the same start and stop times.
 *
 *    After the initial merge the original databases are freed again,
 *    unless acquisition is enabled. During acquisition new blocks are
 *    inserted into their original databases first, and then if added
 *    successfully handed over to this module.
 *
 *    The functionality is split into two levels: the 'upper' half in
 *    this module controls the merged context and source databases. The
 *    'lower' half in the epgdb directory is called for the actual
 *    merging of AI and PI database blocks.
 *
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgctxmerge.c,v 1.15 2007/01/21 11:39:48 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgctl/epgctxctl.h"
#include "epgvbi/cni_tables.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgctxmerge.h"


// internal shortcut
typedef EPGDB_CONTEXT *PDBC;
typedef const EPGDB_CONTEXT *CPDBC;

#define NORM_CNI(C)  (IS_NXTV_CNI(C) ? CniConvertUnknownToPdc(C) : (C))

// ---------------------------------------------------------------------------
// Close all source databases of a merged context
//
static void EpgDbMergeCloseDatabases( EPGDB_MERGE_CONTEXT * pMergeContext )
{
   uint dbIdx;

   for (dbIdx=0; dbIdx < pMergeContext->dbCount; dbIdx++)
   {
      if (pMergeContext->prov[dbIdx].pDbContext != NULL)
      {
         EpgContextCtl_Close(pMergeContext->prov[dbIdx].pDbContext);
         pMergeContext->prov[dbIdx].pDbContext = NULL;
      }
   }

   pMergeContext->acqIdx = 0xff;
}

// ---------------------------------------------------------------------------
// Open all source databases for complete merge or acquisition
//
static bool EpgDbMergeOpenDatabases( EPGDB_MERGE_CONTEXT * pMergeContext, bool isForAcq )
{
   uint dbIdx, cni;
   bool result = TRUE;

   for (dbIdx=0; dbIdx < pMergeContext->dbCount; dbIdx++)
   {
      if (pMergeContext->prov[dbIdx].pDbContext == NULL)
      {
         cni = pMergeContext->prov[dbIdx].provCni;
         pMergeContext->prov[dbIdx].pDbContext = EpgContextCtl_Open(cni, FALSE, CTX_FAIL_RET_NULL, (isForAcq ? CTX_RELOAD_ERR_ACQ : CTX_RELOAD_ERR_REQ));

         if (EpgDbContextGetCni(pMergeContext->prov[dbIdx].pDbContext) != cni)
         {  // one database could not be loaded -> abort merge
            result = FALSE;
            break;
         }
      }
   }

   if (result == FALSE)
   {  // failure -> free already opened databases
      EpgDbMergeCloseDatabases(pMergeContext);
   }
   return result;
}

// ---------------------------------------------------------------------------
// Normalize CNI table
// - determine "canonical" CNI for each network
// - remove duplicate CNIs after normalization
// - also build netwop mapping and reverse tables for each database
//
static void EpgContextMergeNormalizeCnis( EPGDB_MERGE_CONTEXT * pMergeContext,
                                          uint * pCniCount, uint * pCniTab )
{
   const AI_BLOCK  * pAi;
   const AI_NETWOP * pNetwops;
   uint dbIdx;
   uint netIdx;
   uint tabIdx;
   bool found;
   uint netwopCount;

   netwopCount = 0;
   for (tabIdx = 0; tabIdx < *pCniCount; tabIdx++)
   {
      found = FALSE;

      // check if the same CNI already was in the list
      for (netIdx = 0; netIdx < netwopCount; netIdx++)
         if (NORM_CNI(pCniTab[netIdx]) == NORM_CNI(pCniTab[tabIdx]))
            break;

      // check if it's a valid CNI
      if ((netIdx >= netwopCount) && (pCniTab[tabIdx] != 0) && (pCniTab[tabIdx] != 0x00ff) &&
          (netwopCount < MAX_NETWOP_COUNT))
      {
         for (dbIdx=0; dbIdx < pMergeContext->dbCount; dbIdx++)
         {
            pAi = (AI_BLOCK *) &pMergeContext->prov[dbIdx].pDbContext->pAiBlock->blk.ai;
            pNetwops = AI_GET_NETWOPS(pAi);

            for (netIdx = 0; netIdx < pAi->netwopCount; netIdx++, pNetwops++)
               if (NORM_CNI(AI_GET_NET_CNI(pNetwops)) == NORM_CNI(pCniTab[tabIdx]))
                  break;
            if (netIdx < pAi->netwopCount)
            {  // found
               if (found == FALSE)
               {  // first db with that CNI -> keep in the list
                  pCniTab[netwopCount] = pCniTab[tabIdx];
                  found = TRUE;
               }
               pMergeContext->prov[dbIdx].netwopMap[netIdx] = netwopCount;
               pMergeContext->prov[dbIdx].revNetwopMap[netwopCount] = netIdx;
            }
         }
         if (found)
            netwopCount += 1;
      }
      // if the CNI is in none of the databases, it's skipped
      ifdebug1(found == FALSE, "EpgContextMerge-NormalizeCnis: skipping CNI 0x%04X", pCniTab[tabIdx]);
   }
   *pCniCount = netwopCount;
}

// ---------------------------------------------------------------------------
// Destroy the merge-context of a merged database context
// - this function is used by the regular context destruction function
//   if the destroyed context is flagged as merged
//
void EpgContextMergeDestroy( void * pMergeContextPtr )
{
   EPGDB_MERGE_CONTEXT * dbmc = pMergeContextPtr;

   EpgTscQueue_Clear(&dbmc->tscQueue);

   EpgDbMergeCloseDatabases(dbmc);
   xfree(pMergeContextPtr);
}

// ---------------------------------------------------------------------------
// Returns a list of merged databases, in form of provider CNIs
// - array must be allocated by caller and have MAX_MERGED_DB_COUNT elements
//
bool EpgContextMergeGetCnis( const EPGDB_CONTEXT * dbc, uint * pCniCount, uint *pCniTab )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   uint dbIdx;
   bool result = FALSE;

   if ((pCniCount != NULL) && (pCniTab != NULL))
   {
      if (dbc->pMergeContext != NULL)
      {
         dbmc = dbc->pMergeContext;

         *pCniCount = dbmc->dbCount;

         for (dbIdx=0; (dbIdx < dbmc->dbCount) && (dbIdx < MAX_MERGED_DB_COUNT); dbIdx++)
         {
            pCniTab[dbIdx] = dbmc->prov[dbIdx].provCni;
         }

         result = TRUE;
      }
      else
         fatal0("EpgDbMerge-GetCnis: db has no merged context");
   }
   else
      fatal0("EpgDbMerge-GetCnis: illegal NULL ptr param");

   return result;
}

// ---------------------------------------------------------------------------
// Checks if the given CNI is one of the merged providers
//
bool EpgContextMergeCheckForCni( const EPGDB_CONTEXT * dbc, uint cni )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   uint dbIdx;
   bool result = FALSE;

   if (dbc != NULL)
   {
      if (dbc->pMergeContext != NULL)
      {
         if (cni != 0)
         {
            dbmc = dbc->pMergeContext;

            // check if the current acq CNI is one of the merged
            for (dbIdx=0; dbIdx < dbmc->dbCount; dbIdx++)
            {
               if (cni == dbmc->prov[dbIdx].provCni)
               {
                  result = TRUE;
                  break;
               }
            }
         }
         else
            debug0("EpgDbMerge-CheckForCni: searching for illegal CNI 0");
      }
      else
         fatal0("EpgDbMerge-CheckForCni: db has no merged context");
   }
   else
      fatal0("EpgDbMerge-CheckForCni: illegal NULL ptr param");

   return result;
}

// ---------------------------------------------------------------------------
// En-/Disable generation of PI timescale information
//
void EpgContextMergeEnableTimescale( const EPGDB_CONTEXT * dbc, bool enable )
{
   EPGDB_MERGE_CONTEXT * dbmc;

   if ( (pUiDbContext != NULL) && (pUiDbContext->pMergeContext != NULL) )
   {
      dbmc = pUiDbContext->pMergeContext;

      if (enable == FALSE)
         EpgTscQueue_Clear(&dbmc->tscQueue);

      dbmc->tscEnable = enable;
   }
   else
      fatal1("EpgContextMerge-EnableTimescale: not a merged context, CNI 0x%04X", EpgDbContextGetCni(dbc));
}

// ---------------------------------------------------------------------------
// Get Pointer to PI timescale queue
// - used by the GUI to retrieve info from the queue (and thereby emptying it)
// 
EPGDB_PI_TSC * EpgContextMergeGetTimescaleQueue( const EPGDB_CONTEXT * dbc )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_PI_TSC        * pQueue;

   if ( (pUiDbContext != NULL) && (pUiDbContext->pMergeContext != NULL) )
   {
      dbmc = pUiDbContext->pMergeContext;
      pQueue = &dbmc->tscQueue;
   }
   else
      pQueue = NULL;

   return pQueue;
}

// ---------------------------------------------------------------------------
// Determine the index of a given CNI (i.e. source db) in the merged context
// - the determined index is cached in the context
// - Note: if there's more than one acq process this will get VERY inefficient
//   particularily if one of the acq processes works on a db that's not merged
//
static bool EpgDbMergeOpenAcqContext( uint cni )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   uint dbIdx;
   bool result = FALSE;

   if ( (pUiDbContext != NULL) && (pUiDbContext->pMergeContext != NULL) )
   {
      dbmc = pUiDbContext->pMergeContext;

      // get the index of the acq db in the merge context
      if ( (dbmc->acqIdx >= dbmc->dbCount) || (cni != dbmc->prov[dbmc->acqIdx].provCni) )
      {  // no index cached or wrong CNI -> search CNI in list
         dbmc->acqIdx = 0xff;
         for (dbIdx=0; dbIdx < dbmc->dbCount; dbIdx++)
         {
            if (dbmc->prov[dbIdx].provCni == cni)
            {
               dbmc->acqIdx = dbIdx;
               result = TRUE;
               break;
            }
         }
      }
      else
         result = TRUE;

      if (result)
      {  // open the db contexts of all dbs in the merge context
         // assume that if one db is open, then all are
         if (dbmc->prov[0].pDbContext == NULL)
         {
            if (EpgDbMergeOpenDatabases(dbmc, TRUE) == FALSE)
            {  // open failed -> cannot insert
               dbmc->acqIdx = 0xff;
               result = FALSE;
            }
         }
      }
      else
      {  // unknown cni -> close db contexts, if neccessary
         EpgDbMergeCloseDatabases(dbmc);
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Insert a PI block into the merged db
// - called after the block was inserted to its provider's database
// 
void EpgContextMergeInsertPi( CPDBC pAcqContext, EPGDB_BLOCK * pNewBlock )
{
   EPGDB_MERGE_CONTEXT * dbmc;

   if ( EpgDbMergeOpenAcqContext(EpgDbContextGetCni(pAcqContext)) )
   {
      dbmc = pUiDbContext->pMergeContext;
      dprintf6("MERGE PI ptr=%lx: dbidx=%d, netwop=%d->%d, blockno=%d, start=%ld\n", (ulong)pNewBlock, dbmc->acqIdx, pNewBlock->blk.pi.netwop_no, dbmc->prov[dbmc->acqIdx].netwopMap[pNewBlock->blk.pi.netwop_no], pNewBlock->blk.pi.block_no, pNewBlock->blk.pi.start_time);

      EpgDbMergeInsertPi(pUiDbContext, pNewBlock);

      if (EpgTscQueue_HasElems(&dbmc->tscQueue))
         UiControlMsg_AcqEvent(ACQ_EVENT_PI_MERGED);
   }
}

// ---------------------------------------------------------------------------
// Update all PI in one of the merged databases
// - optimization for the special case where each DB covers only one (or few) networks
// 
bool EpgContextMergeUpdateDb( CPDBC pAcqContext )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_BLOCK * pPiBlock;
   uint netwopCount;
   uint idx;
   bool result = FALSE;

   if (EpgDbMergeOpenAcqContext( EpgDbContextGetCni(pAcqContext)) )
   {
      dbmc = pUiDbContext->pMergeContext;

      netwopCount = pAcqContext->pAiBlock->blk.ai.netwopCount;
      dprintf1("EpgContextMerge-UpdateDb: merge all PI of %d netwops\n", netwopCount);

      for (idx = 0; idx < netwopCount; idx++)
      {
         pPiBlock = pAcqContext->pFirstNetwopPi[idx];
         if (pPiBlock != NULL)
         {
            EpgDbMergeUpdateNetwork(pUiDbContext, idx, pPiBlock);
         }
      }

      if (EpgTscQueue_HasElems(&dbmc->tscQueue))
         UiControlMsg_AcqEvent(ACQ_EVENT_PI_MERGED);

      result = TRUE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Update AI block when an AI in one of the dbs has changed
// - Only called after change of version number in one of the blocks.
//   More frequent updates are not required because changes of blockno range
//   are not of any interest to the merged database.
// - when this func is called the new AI is already part of the context
//
void EpgContextMergeAiUpdate( CPDBC pAcqContext )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   const AI_NETWOP *pNetwops;
   uint  netwopCount, netwopCniTab[MAX_NETWOP_COUNT];
   uint  idx;

   if ( EpgDbMergeOpenAcqContext( EpgDbContextGetCni(pAcqContext)) )
   {
      // copy the network list from the previously merged AI block as it will not be changed
      pNetwops    = AI_GET_NETWOPS(&pUiDbContext->pAiBlock->blk.ai);
      netwopCount = pUiDbContext->pAiBlock->blk.ai.netwopCount;
      for (idx=0; idx < netwopCount; idx++, pNetwops++)
         netwopCniTab[idx] = AI_GET_NET_CNI(pNetwops);

      // free the old AI block in the merged context
      xfree(pUiDbContext->pAiBlock);
      pUiDbContext->pAiBlock = NULL;

      EpgDbMergeAiBlocks(pUiDbContext, netwopCount, netwopCniTab);

      // reset version bit for all PI merged from this db
      dbmc = pUiDbContext->pMergeContext;
      EpgDbMerge_ResetPiVersion(pUiDbContext, dbmc->acqIdx);
   }
}

// ---------------------------------------------------------------------------
// Start complete merge
//
EPGDB_CONTEXT * EpgContextMerge( uint dbCount, const uint * pCni, MERGE_ATTRIB_VECTOR_PTR pMax,
                                 uint netwopCount, uint * pNetwopList )
{
   EPGDB_CONTEXT * pDbContext;
   EPGDB_MERGE_CONTEXT * pMergeContext;
   uint dbIdx;

   if (dbCount > MAX_MERGED_DB_COUNT)
   {
      debug2("EpgContext-Merge: too many dbs %d > %d", dbCount, MAX_MERGED_DB_COUNT);
      dbCount = MAX_MERGED_DB_COUNT;
   }
   ifdebug2(netwopCount == 0, "EpgContext-Merge: netwop count is zero (%d DBs, first CNI 0x%04X)", dbCount, *pCni);

   // initialize context
   pMergeContext = xmalloc(sizeof(EPGDB_MERGE_CONTEXT));
   memset(pMergeContext, 0, sizeof(EPGDB_MERGE_CONTEXT));

   pMergeContext->dbCount = dbCount;
   pMergeContext->acqIdx  = 0xff;
   memcpy(pMergeContext->max, pMax, sizeof(MERGE_ATTRIB_MATRIX));

   for (dbIdx=0; dbIdx < pMergeContext->dbCount; dbIdx++)
   {
      pMergeContext->prov[dbIdx].provCni = pCni[dbIdx];
      memset(pMergeContext->prov[dbIdx].revNetwopMap, 0xff, sizeof(pMergeContext->prov[0].revNetwopMap));
      memset(pMergeContext->prov[dbIdx].netwopMap, 0xff, sizeof(pMergeContext->prov[0].netwopMap));
   }

   if ( EpgDbMergeOpenDatabases(pMergeContext, FALSE) )
   {
      // create target database
      pDbContext = EpgDbCreate();

      pDbContext->provCni = MERGED_PROV_CNI;
      pDbContext->merged = TRUE;
      pDbContext->pMergeContext = pMergeContext;
      pDbContext->expireDelayPi = pMergeContext->prov[0].pDbContext->expireDelayPi;

      EpgContextMergeNormalizeCnis(pMergeContext, &netwopCount, pNetwopList);

      // create AI block
      EpgDbMergeAiBlocks(pDbContext, netwopCount, pNetwopList);

      // merge all PI from all databases into the new one
      EpgDbMergeAllPiBlocks(pDbContext);

      // close the databases
      EpgDbMergeCloseDatabases(pMergeContext);

      // initialize the PI timescale queue (used during acquisition)
      EpgTscQueue_Init(&pMergeContext->tscQueue);
   }
   else
   {
      pDbContext = NULL;
      xfree(pMergeContext);
   }

   return pDbContext;
}

