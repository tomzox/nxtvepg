/*
 *  Nextview EPG block database management
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
 *    Implements a database with all types of Nextview structures
 *    sorted by block number, start time and/or network. This module
 *    contains only functions that modify the database; queries are
 *    implemented in the epgdbif.c module.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DEBUG_EPGDBMGMT_CONSISTANCY OFF
#define DPRINTF_OFF

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbmgmt.h"
#include "epgctl/epgctxmerge.h"
#include "epgctl/epgacqsrv.h"


// internal shortcuts
typedef       EPGDB_CONTEXT *PDBC;
typedef const EPGDB_CONTEXT *CPDBC;

#if DEBUG_EPGDBMGMT_CONSISTANCY == ON
#define EpgDbMgmtCheckChains(DBC)  EpgDbCheckChains(DBC)
#else
#define EpgDbMgmtCheckChains(DBC)  TRUE
#endif

// ---------------------------------------------------------------------------
// Create and initialize a database context
//
EPGDB_CONTEXT * EpgDbCreate( void )
{
   EPGDB_CONTEXT *pDbContext;

   pDbContext = (EPGDB_CONTEXT *) xmalloc(sizeof(EPGDB_CONTEXT));
   memset(pDbContext, 0, sizeof(EPGDB_CONTEXT));

   return pDbContext;
}

// ---------------------------------------------------------------------------
// Frees all blocks in a database
// - if keepAi if TRUE the context struct, AI block is not freed.
//   used when a database is stripped down to a "peek"
//
void EpgDbDestroy( PDBC dbc, bool keepAi )
{
   EPGDB_BLOCK *pNext, *pWalk;

   if ( dbc->lockLevel == 0 )
   {
      // free PI blocks
      pWalk = dbc->pFirstPi;
      while (pWalk != NULL)
      {
         pNext = pWalk->pNextBlock;
         xfree(pWalk);
         pWalk = pNext;
      }
      dbc->pFirstPi = NULL;
      dbc->pLastPi = NULL;

      // free all defect PI blocks
      pWalk = dbc->pObsoletePi;
      while (pWalk != NULL)
      {
         pNext = pWalk->pNextBlock;
         xfree(pWalk);
         pWalk = pNext;
      }
      dbc->pObsoletePi = NULL;

      if (keepAi == FALSE)
      {
         // free AI
         if (dbc->pAiBlock != NULL)
         {
            xfree(dbc->pAiBlock);
            dbc->pAiBlock = NULL;
         }

         if (dbc->pFirstNetwopPi != NULL)
         {
             xfree(dbc->pFirstNetwopPi);
             dbc->pFirstNetwopPi = NULL;
         }

         // free the database context
         xfree(dbc);
      }
      else
      {
         // reset remaining block pointers
         memset(dbc->pFirstNetwopPi, 0, sizeof(dbc->pFirstNetwopPi[0]) * dbc->netwopCount);

         assert(EpgDbMgmtCheckChains(dbc));
      }
   }
   else
      debug0("EpgDb-Destroy: cannot destroy locked db");
}

// ---------------------------------------------------------------------------
// DEBUG ONLY: check pointer chains
//
#if DEBUG_GLOBAL_SWITCH == ON
bool EpgDbCheckChains( CPDBC dbc )
{
   EPGDB_BLOCK *pPrev, *pWalk;
   EPGDB_BLOCK **pPrevNetwop;
   sint  blocks;
   uint  netwop;

   if (dbc->pFirstPi != NULL)
   {
      assert(dbc->pAiBlock != NULL);
      assert(dbc->pLastPi != NULL);
      assert(dbc->pFirstPi->pPrevBlock == NULL);
      assert(dbc->pLastPi->pNextBlock == NULL);

      blocks = 0;
      pWalk = dbc->pFirstPi;
      pPrev = NULL;
      pPrevNetwop = xmalloc(sizeof(pPrevNetwop[0]) * dbc->netwopCount);
      memset(pPrevNetwop, 0, sizeof(pPrevNetwop[0]) * dbc->netwopCount);

      while (pWalk != NULL)
      {
         blocks += 1;
         netwop = pWalk->blk.pi.netwop_no;
         assert(netwop < dbc->netwopCount);
         assert(pWalk->type == BLOCK_TYPE_PI);
         assert(pWalk->blk.pi.start_time < pWalk->blk.pi.stop_time);
         pPrev = pPrevNetwop[netwop];
         assert(pWalk->pPrevNetwopBlock == pPrev);
         if (pPrev != NULL)
         {
            assert(pPrev->pNextNetwopBlock == pWalk);
            assert(pWalk->blk.pi.start_time >= pPrev->blk.pi.stop_time);
         }
         else
            assert(dbc->pFirstNetwopPi[netwop] == pWalk);
         pPrevNetwop[netwop] = pWalk;

         pPrev = pWalk;
         pWalk = pWalk->pNextBlock;
         if (pWalk != NULL)
         {
            assert(pWalk->pPrevBlock == pPrev);
            assert((pWalk->blk.pi.start_time > pPrev->blk.pi.start_time) ||
                   ((pWalk->blk.pi.start_time == pPrev->blk.pi.start_time) &&
                    (pWalk->blk.pi.netwop_no > pPrev->blk.pi.netwop_no) ));
         }
      }
      assert(dbc->pLastPi == pPrev);
      xfree(pPrevNetwop);
      pPrevNetwop = NULL;

      for (netwop=0; netwop < dbc->netwopCount; netwop++)
      {
         pWalk = dbc->pFirstNetwopPi[netwop];
         assert((pWalk == NULL) || (pWalk->pPrevNetwopBlock == NULL));
         pPrev = NULL;
         while (pWalk != NULL)
         {
            assert(pWalk->blk.pi.netwop_no == netwop);
            blocks -= 1;
            pPrev = pWalk;
            pWalk = pWalk->pNextNetwopBlock;
            assert((pWalk == NULL) || (pWalk->blk.pi.start_time >= pPrev->blk.pi.start_time));
         }
      }
      for (netwop=dbc->pAiBlock->blk.ai.netwopCount; netwop < dbc->netwopCount; netwop++)
      {
         assert(dbc->pFirstNetwopPi[netwop] == NULL);
      }
      assert(blocks == 0);
   }
   else
   {  // no PI in the DB -> all netwop pointers must be invalid
      assert(dbc->pLastPi == NULL);
      for (netwop=0; netwop < dbc->netwopCount; netwop++)
      {
         assert(dbc->pFirstNetwopPi[netwop] == NULL);
      }
   }

   // check defect PI: unsorted -> just check linkage
   pWalk = dbc->pObsoletePi;
   while (pWalk != NULL)
   {
      assert(pWalk->type == BLOCK_TYPE_PI);
      pWalk = pWalk->pNextBlock;
   }

   // check AI
   if (dbc->pAiBlock != NULL)
   {
      assert(dbc->pAiBlock->type == BLOCK_TYPE_AI);
      assert((dbc->pAiBlock->pNextBlock == NULL) && (dbc->pAiBlock->pPrevBlock == NULL));
      assert(dbc->pAiBlock->blk.ai.netwopCount == dbc->netwopCount);
      assert(dbc->pFirstNetwopPi != NULL);
   }

   return TRUE;
}
#endif // DEBUG_GLOBAL_SWITCH == ON

// ---------------------------------------------------------------------------
// Append a PI block to the database
//
static void EpgDbMergeAddPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK *pPrevBlock )
{
   uint netwop;

   netwop = pBlock->blk.pi.netwop_no;
   if (dbc->pFirstPi == NULL)
   {  // db empty -> insert very first item
      assert(dbc->pLastPi == NULL);
      assert(dbc->pFirstNetwopPi[netwop] == NULL);
      pBlock->pNextNetwopBlock = NULL;
      pBlock->pPrevNetwopBlock = NULL;
      dbc->pFirstNetwopPi[netwop] = pBlock;
      pBlock->pPrevBlock = NULL;
      pBlock->pNextBlock = NULL;
      dbc->pFirstPi = pBlock;
      dbc->pLastPi  = pBlock;
   }
   else
   {  // append to the end

      // set pointers of netwop chain
      if (dbc->pFirstNetwopPi[netwop] == NULL)
      {
         dbc->pFirstNetwopPi[netwop] = pBlock;
         pBlock->pPrevNetwopBlock = NULL;
      }
      else
      {
         pPrevBlock->pNextNetwopBlock = pBlock;
         pBlock->pPrevNetwopBlock = pPrevBlock;
      }
      pBlock->pNextNetwopBlock = NULL;

      // set pointers of start time chain
      pBlock->pPrevBlock = dbc->pLastPi;
      pBlock->pNextBlock = NULL;
      dbc->pLastPi->pNextBlock = pBlock;
      dbc->pLastPi = pBlock;
   }
}

// ---------------------------------------------------------------------------
// Combine linked PI of separated networks into one database
//
void EpgDbMergeLinkNetworkPi( PDBC dbc, EPGDB_BLOCK ** pFirstNetwopBlock )
{
   EPGDB_BLOCK **pPrevNetwopBlock;
   EPGDB_BLOCK *pBlock;
   time_t minStartTime;
   uint minNetwop;
   uint netwop, netCount;

   // reset start links in DB context
   dbc->pFirstPi = NULL;
   dbc->pLastPi = NULL;
   memset(dbc->pFirstNetwopPi, 0, sizeof(dbc->pFirstNetwopPi[0]));

   netCount = dbc->pAiBlock->blk.ai.netwopCount;
   pPrevNetwopBlock = xmalloc(sizeof(pPrevNetwopBlock[0]) * netCount);
   memset(pPrevNetwopBlock, 0, sizeof(pPrevNetwopBlock[0]) * netCount);

   // combine blocks of separately merged networks into one database
   while (1)
   {
      minStartTime = 0;
      minNetwop = INVALID_NETWOP_IDX;

      // search across all networks for the oldest block
      for (netwop = 0; netwop < netCount; netwop++)
      {
         if (pFirstNetwopBlock[netwop] != NULL)
         {
            if ( (minStartTime == 0) ||
                 (pFirstNetwopBlock[netwop]->blk.pi.start_time < minStartTime) )
            {
               minStartTime = pFirstNetwopBlock[netwop]->blk.pi.start_time;
               minNetwop = netwop;
            }
         }
      }

      if (minNetwop == INVALID_NETWOP_IDX)
         break;

      // pop block off temporary per-network list
      pBlock = pFirstNetwopBlock[minNetwop];
      pFirstNetwopBlock[minNetwop] = pBlock->pNextNetwopBlock;
      pBlock->pNextNetwopBlock = NULL;

      EpgDbMergeAddPiBlock(dbc, pBlock, pPrevNetwopBlock[minNetwop]);
      pPrevNetwopBlock[minNetwop] = pBlock;
   }
   xfree(pPrevNetwopBlock);
}

// ---------------------------------------------------------------------------
// Add a defective (e.g. overlapping) PI block to a separate list
// - called by XMLTV load when insertion of PI fails;
//   this list is kept only for DB statistics
// - the blocks are single-chained and NOT sorted;
//   there is no check for duplicate blocks either
// - returns FALSE if block was not added and has to be free()'d by caller
//
bool EpgDbAddDefectPi( PDBC dbc, EPGDB_BLOCK *pBlock )
{
   dprintf4("ADD OBSOLETE PI ptr=%lx: netwop=%d, start=%ld, stop=%ld\n", (ulong)pBlock, pBlock->blk.pi.netwop_no, pBlock->blk.pi.start_time, pBlock->blk.pi.stop_time);

   // reset unused pointers
   pBlock->pPrevBlock = NULL;
   pBlock->pNextNetwopBlock = NULL;
   pBlock->pPrevNetwopBlock = NULL;

   pBlock->pNextBlock = dbc->pObsoletePi;
   dbc->pObsoletePi = pBlock;

   return TRUE;
}
