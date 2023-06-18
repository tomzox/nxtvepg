/*
 *  Nextview EPG merged context management
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
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgctxmerge.h"


// internal shortcut
typedef EPGDB_CONTEXT *PDBC;
typedef const EPGDB_CONTEXT *CPDBC;

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

      if (pMergeContext->prov[dbIdx].netwopMap != NULL)
      {
         xfree(pMergeContext->prov[dbIdx].netwopMap);
         pMergeContext->prov[dbIdx].netwopMap = NULL;
      }
      if (pMergeContext->prov[dbIdx].revNetwopMap != NULL)
      {
         xfree(pMergeContext->prov[dbIdx].revNetwopMap);
         pMergeContext->prov[dbIdx].revNetwopMap = NULL;
      }
   }
}

// ---------------------------------------------------------------------------
// Open all source databases for complete merge or acquisition
//
static bool EpgDbMergeOpenDatabases( EPGDB_MERGE_CONTEXT * pMergeContext, CONTEXT_RELOAD_ERR_HAND errHand )
{
   uint dbIdx, cni;
   bool result = TRUE;

   for (dbIdx=0; dbIdx < pMergeContext->dbCount; dbIdx++)
   {
      if (pMergeContext->prov[dbIdx].pDbContext == NULL)
      {
         cni = pMergeContext->prov[dbIdx].provCni;
         pMergeContext->prov[dbIdx].pDbContext = EpgContextCtl_Open(cni, FALSE, errHand);

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
// Allocate netwop index mapping and reverse tables for each database
// - tables have different size, as they map different arrays
// - initially map all networks to "unused"
//
static void EpgContextMergeAllocNetwopMap( EPGDB_MERGE_CONTEXT * dbmc, uint cniCount )
{
   for (uint dbIdx=0; dbIdx < dbmc->dbCount; dbIdx++)
   {
      // "netwopMap" maps from local AI netwop index to merged DB netwop index
      uint aiNetwopCount = dbmc->prov[dbIdx].pDbContext->pAiBlock->blk.ai.netwopCount;
      dbmc->prov[dbIdx].netwopMap = xmalloc(sizeof(uint) * aiNetwopCount);

      for (uint idx = 0; idx < aiNetwopCount; ++idx)
      {
          dbmc->prov[dbIdx].netwopMap[idx] = INVALID_NETWOP_IDX;
      }

      // "revNetwopMap" maps from merged DB netwop index to local AI netwop index
      dbmc->prov[dbIdx].revNetwopMap = xmalloc(sizeof(uint) * cniCount);

      for (uint idx = 0; idx < cniCount; ++idx)
      {
          dbmc->prov[dbIdx].revNetwopMap[idx] = INVALID_NETWOP_IDX;
      }
   }
}

// ---------------------------------------------------------------------------
// Build netwop index mapping and reverse tables for each database
// - note de-duplication and user-configured suppression already done by caller
//
static void EpgContextMergeBuildNetwopMap( EPGDB_MERGE_CONTEXT * pMergeContext,
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
         if (pCniTab[netIdx] == pCniTab[tabIdx])
            break;

      // check if it's a valid CNI
      if ((netIdx >= netwopCount) &&
          (pCniTab[tabIdx] != 0) && (pCniTab[tabIdx] != 0x00ff))
      {
         for (dbIdx=0; dbIdx < pMergeContext->dbCount; dbIdx++)
         {
            pAi = (AI_BLOCK *) &pMergeContext->prov[dbIdx].pDbContext->pAiBlock->blk.ai;
            pNetwops = AI_GET_NETWOPS(pAi);

            for (netIdx = 0; netIdx < pAi->netwopCount; netIdx++, pNetwops++)
               if (AI_GET_NET_CNI(pNetwops) == pCniTab[tabIdx])
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
         else
            debug1("EpgContextMerge-BuildNetwopMap: skipping CNI 0x%04X", pCniTab[tabIdx]);
      }
      else
         debug1("EpgContextMerge-BuildNetwopMap: dropping invalid CNI 0x%04X", pCniTab[tabIdx]);
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
   EPGDB_MERGE_CONTEXT * dbmc = (EPGDB_MERGE_CONTEXT*) pMergeContextPtr;

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
         dbmc = (EPGDB_MERGE_CONTEXT*) dbc->pMergeContext;

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
            dbmc = (EPGDB_MERGE_CONTEXT*) dbc->pMergeContext;

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
// Add new providers to a pre-existing merge context
//
static void EpgContextMergeExtendProvList( uint addCount, const uint * pProvCni, int errHand )
{
   EPGDB_MERGE_CONTEXT * dbmc = (EPGDB_MERGE_CONTEXT*) pUiDbContext->pMergeContext;
   const AI_BLOCK * pOldAi = &pUiDbContext->pAiBlock->blk.ai;
   uint netwopCount = pOldAi->netwopCount;
   uint * netCnis = xmalloc(sizeof(uint) * netwopCount);

   for (uint idx = 0; idx < netwopCount; ++idx)
      netCnis[idx] = AI_GET_NET_CNI_N(pOldAi, idx);

   for (uint dbIdx = 0; (dbIdx < addCount) && (dbmc->dbCount < MAX_MERGED_DB_COUNT); ++dbIdx)
   {
      // open the new database
      dbmc->prov[dbmc->dbCount].pDbContext = EpgContextCtl_Open(pProvCni[dbIdx], FALSE, errHand);
      if (dbmc->prov[dbmc->dbCount].pDbContext != NULL)
      {
         const AI_BLOCK * pTmpAi = &dbmc->prov[dbmc->dbCount].pDbContext->pAiBlock->blk.ai;
         netCnis = xrealloc(netCnis, sizeof(uint) * (netwopCount + pTmpAi->netwopCount));

         // append all CNIs of this provider to merged network table;
         // omitting check for duplicates, as this is done already during the function called below
         for (uint idx = 0; idx < pTmpAi->netwopCount; ++idx)
            netCnis[netwopCount++] = AI_GET_NET_CNI_N(pTmpAi, idx);

         // append new provider to attribute tables
         for (uint maxIdx = 0; maxIdx < MERGE_TYPE_COUNT; ++maxIdx)
         {
            for (uint idx = 0; idx < dbmc->dbCount; ++idx)
            {
               if (dbmc->max[maxIdx][idx] == 0xff)
               {
                  dbmc->max[maxIdx][idx] = dbmc->dbCount;
                  break;
               }
            }
         }

         dbmc->prov[dbmc->dbCount].netwopMap = NULL;
         dbmc->prov[dbmc->dbCount].revNetwopMap = NULL;
         dbmc->prov[dbmc->dbCount].provCni = pProvCni[dbIdx];
         dbmc->dbCount += 1;
      }
   }

   // rebuild netwop mapping tables for all providers, so that new networks are included
   EpgContextMergeAllocNetwopMap(dbmc, netwopCount);
   EpgContextMergeBuildNetwopMap(dbmc, &netwopCount, netCnis);
   pUiDbContext->netwopCount = netwopCount;

   // discard old AI block and merge a new one
   xfree(pUiDbContext->pAiBlock);
   pUiDbContext->pAiBlock = NULL;
   EpgDbMergeAiBlocks(pUiDbContext, netwopCount, netCnis);
}

// ---------------------------------------------------------------------------
// Update all PI in one of the merged databases, or add new providers
// - only PI of networks that originate from updated/new providers are merged
// - optimization for the special case where each DB covers only few networks
// - updCount: number of CNIs in array pProvCni;
//   addCount: number of new providers included in updCount
// - CNIs of new providers are expected at the end of the CNI list
//
bool EpgContextMergeUpdateDb( uint updCount, uint addCount, const uint * pProvCni, int errHand )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   bool result = FALSE;

   // open databases of pre-existing providers
   if ( (pUiDbContext != NULL) && (pUiDbContext->pMergeContext != NULL) )
   {
      dbmc = (EPGDB_MERGE_CONTEXT*) pUiDbContext->pMergeContext;

      // open the db contexts of all dbs in the merge context
      // assume that if one db is open, then all are
      if ( (dbmc->prov[0].pDbContext != NULL) ||
           EpgDbMergeOpenDatabases(dbmc, errHand) )
      {
         if (addCount > 0)
         {
            dprintf1("EpgContextMerge-UpdateDb: adding %d DBs\n", addCount);
            // extend merge context parameters to new providers
            EpgContextMergeExtendProvList(addCount, pProvCni + updCount - addCount, errHand);
         }

         // merge PI of all updated or added networks
         EpgDbMergeUpdateNetworks(pUiDbContext, updCount, pProvCni);

         // update modification time of merged AI
         time_t mergeAcqTime = EpgDbGetAiUpdateTime(pUiDbContext);
         for (uint dbIdx = 0; dbIdx < dbmc->dbCount; ++dbIdx)
         {
            time_t acqTime = EpgDbGetAiUpdateTime(dbmc->prov[dbIdx].pDbContext);
            if (acqTime > mergeAcqTime)
               mergeAcqTime = acqTime;
         }
         EpgDbSetAiUpdateTime(pUiDbContext, mergeAcqTime);

         EpgDbMergeCloseDatabases(dbmc);

         result = TRUE;
      }
   }
   else
      fatal2("EpgContextMerge-UpdateDb: invalid NULL ptr params %p,%p", pUiDbContext, pUiDbContext->pMergeContext);

   return result;
}

// ---------------------------------------------------------------------------
// Start complete merge
//
EPGDB_CONTEXT * EpgContextMerge( uint dbCount, const uint * pCni, MERGE_ATTRIB_VECTOR_PTR pMax,
                                 uint netwopCount, uint * pNetwopList, int errHand )
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
   dprintf2("EpgContextMerge: Merging %d DBs with %d networks\n", dbCount, netwopCount);

   // initialize context
   pMergeContext = (EPGDB_MERGE_CONTEXT*) xmalloc(sizeof(EPGDB_MERGE_CONTEXT));
   memset(pMergeContext, 0, sizeof(EPGDB_MERGE_CONTEXT));

   pMergeContext->dbCount = dbCount;
   memcpy(pMergeContext->max, pMax, sizeof(MERGE_ATTRIB_MATRIX));

   for (dbIdx=0; dbIdx < pMergeContext->dbCount; dbIdx++)
   {
      pMergeContext->prov[dbIdx].provCni = pCni[dbIdx];

      pMergeContext->prov[dbIdx].netwopMap = NULL;
      pMergeContext->prov[dbIdx].revNetwopMap = NULL;
   }

   if ( EpgDbMergeOpenDatabases(pMergeContext, errHand) )
   {
      // create target database
      pDbContext = EpgDbCreate();

      pDbContext->provCni = MERGED_PROV_CNI;
      pDbContext->merged = TRUE;
      pDbContext->pMergeContext = pMergeContext;

      EpgContextMergeAllocNetwopMap(pMergeContext, netwopCount);
      EpgContextMergeBuildNetwopMap(pMergeContext, &netwopCount, pNetwopList);
      pMergeContext->netwopCount = netwopCount;

      pDbContext->pFirstNetwopPi = xmalloc(netwopCount * sizeof(pDbContext->pFirstNetwopPi[0]));
      memset(pDbContext->pFirstNetwopPi, 0, netwopCount * sizeof(pDbContext->pFirstNetwopPi[0]));
      pDbContext->netwopCount = netwopCount;

      // create AI block
      EpgDbMergeAiBlocks(pDbContext, netwopCount, pNetwopList);

      // merge all PI from all databases into the new one
      EpgDbMergeAllPiBlocks(pDbContext);

      // close the databases
      EpgDbMergeCloseDatabases(pMergeContext);
   }
   else
   {
      pDbContext = NULL;
      xfree(pMergeContext);
   }

   return pDbContext;
}
