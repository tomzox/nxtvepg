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
 *  $Id: epgctxmerge.c,v 1.4 2001/09/02 16:30:54 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgui/statswin.h"
#include "epgctl/epgctxctl.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
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
      if (pMergeContext->pDbContext[dbIdx] != NULL)
      {
         EpgContextCtl_Close(pMergeContext->pDbContext[dbIdx]);
         pMergeContext->pDbContext[dbIdx] = NULL;
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
      if (pMergeContext->pDbContext[dbIdx] == NULL)
      {
         cni = pMergeContext->cnis[dbIdx];
         pMergeContext->pDbContext[dbIdx] = EpgContextCtl_Open(cni, (isForAcq ? CTX_RELOAD_ERR_ACQ : CTX_RELOAD_ERR_REQ));

         if (EpgDbContextGetCni(pMergeContext->pDbContext[dbIdx]) != cni)
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
// Destroy the merge-context of a merged database context
// - this function is used by the regular context destruction function
//   if the destroyed context is flagged as merged
//
void EpgContextMergeDestroy( void * pMergeContextPtr )
{
   EpgDbMergeCloseDatabases((EPGDB_MERGE_CONTEXT *) pMergeContextPtr);
   xfree(pMergeContextPtr);
}

// ---------------------------------------------------------------------------
// Returns a list of merged databases, in form of provider CNIs
//
bool EpgContextMergeGetCnis( const EPGDB_CONTEXT * dbc, uint * pCniCount, uint *pCniTab )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   bool result = FALSE;

   if ((pCniCount != NULL) && (pCniTab != NULL))
   {
      if (dbc->pMergeContext != NULL)
      {
         dbmc = pUiDbContext->pMergeContext;

         *pCniCount = dbmc->dbCount;
         memcpy(pCniTab, dbmc->cnis, dbmc->dbCount * sizeof(uint));

         result = TRUE;
      }
      else
         debug0("EpgDbMerge-GetCnis: db has no merged context");
   }
   else
      debug0("EpgDbMerge-GetCnis: illegal NULL ptr param");

   return result;
}

// ---------------------------------------------------------------------------
// Determine the index of a given CNI (i.e. source db) in the merged context
// - the determined index is cached in the context
// - Note: if there's more than one acq process this will get VERY inefficient
//   particularily if one of the acq processes works on a db that's not merged
//
static bool EpgDbMergeOpenAcqContext( CPDBC dbc, uint cni )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   uint dbIdx;
   bool result = FALSE;

   if ( (pUiDbContext != NULL) && (pUiDbContext->pMergeContext != NULL) )
   {
      dbmc = pUiDbContext->pMergeContext;

      // get the index of the acq db in the merge context
      if ( (dbmc->acqIdx >= dbmc->dbCount) || (cni != dbmc->cnis[dbmc->acqIdx]) )
      {  // no index cached or wrong CNI -> search CNI in list
         dbmc->acqIdx = 0xff;
         for (dbIdx=0; dbIdx < dbmc->dbCount; dbIdx++)
         {
            if (dbmc->cnis[dbIdx] == cni)
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
         if (dbmc->pDbContext[0] == NULL)
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
   if (EpgDbMergeOpenAcqContext(pAcqContext, AI_GET_CNI(&pAcqContext->pAiBlock->blk.ai)))
   {
      dprintf6("MERGE PI ptr=%lx: dbidx=%d, netwop=%d->%d, blockno=%d, start=%ld\n", (ulong)pNewBlock, dbmc->acqIdx, pNewBlock->blk.pi.netwop_no, dbmc->netwopMap[dbmc->acqIdx][pNewBlock->blk.pi.netwop_no], pNewBlock->blk.pi.block_no, pNewBlock->blk.pi.start_time);

      EpgDbMergeInsertPi(pUiDbContext->pMergeContext, pNewBlock);
   }
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

   if ( EpgDbMergeOpenAcqContext(pAcqContext, AI_GET_CNI(&pAcqContext->pAiBlock->blk.ai)) )
   {
      // copy the network list from the previously merged AI block as it will not be changed
      pNetwops    = AI_GET_NETWOPS(&pUiDbContext->pAiBlock->blk.ai);
      netwopCount = pUiDbContext->pAiBlock->blk.ai.netwopCount;
      for (idx=0; idx < netwopCount; idx++)
         netwopCniTab[idx] = (pNetwops++)->cni;

      // free the old AI block in the merged context
      xfree(pUiDbContext->pAiBlock);
      pUiDbContext->pAiBlock = NULL;

      EpgDbMergeAiBlocks(pUiDbContext, netwopCount, netwopCniTab);

      pUiDbContext->lastAiUpdate = time(NULL);

      // reset version bit for all PI merged from this db
      dbmc = pUiDbContext->pMergeContext;
      EpgDbMerge_ResetPiVersion(pUiDbContext, dbmc->acqIdx);
      StatsWin_ProvChange(DB_TARGET_UI);
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

   if (dbCount > MAX_MERGED_DB_COUNT)
   {
      debug2("EpgDb-Merge: too many dbs %d > %d", dbCount, MAX_MERGED_DB_COUNT);
      dbCount = MAX_MERGED_DB_COUNT;
   }

   // initialize context
   pMergeContext = xmalloc(sizeof(EPGDB_MERGE_CONTEXT));
   memset(pMergeContext, 0, sizeof(EPGDB_MERGE_CONTEXT));

   pMergeContext->dbCount = dbCount;
   pMergeContext->acqIdx  = 0xff;
   memcpy(pMergeContext->cnis, pCni, sizeof(uint) * dbCount);
   memcpy(pMergeContext->max, pMax, sizeof(MERGE_ATTRIB_MATRIX));

   if ( EpgDbMergeOpenDatabases(pMergeContext, FALSE) )
   {
      // create target database
      pDbContext = EpgDbCreate();

      pDbContext->merged = TRUE;
      pDbContext->pMergeContext = pMergeContext;

      // create AI block
      EpgDbMergeAiBlocks(pDbContext, netwopCount, pNetwopList);
      pDbContext->lastAiUpdate = time(NULL);

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

