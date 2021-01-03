/*
 *  Nextview EPG block database management
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgdbmgmt.c,v 1.54 2014/04/23 21:18:50 tom Exp tom $
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
// - if keepAiOi if TRUE the context struct, AI and OI block are not freed.
//   used when a database is stripped down to a "peek"
//
void EpgDbDestroy( PDBC dbc, bool keepAiOi )
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

      if (keepAiOi == FALSE)
      {
         // free AI
         if (dbc->pAiBlock != NULL)
         {
            xfree(dbc->pAiBlock);
            dbc->pAiBlock = NULL;
         }
         if (dbc->pOiBlock != NULL)
         {
            xfree(dbc->pOiBlock);
            dbc->pOiBlock = NULL;
         }

         // free the database context
         xfree(dbc);
      }
      else
      {
         // reset remaining block pointers
         memset(dbc->pFirstNetwopPi, 0, sizeof(dbc->pFirstNetwopPi));
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
   EPGDB_BLOCK *pPrevNetwop[MAX_NETWOP_COUNT];
   sint  blocks;
   uchar netwop;

   if (dbc->pFirstPi != NULL)
   {
      assert(dbc->pAiBlock != NULL);
      assert(dbc->pLastPi != NULL);
      assert(dbc->pFirstPi->pPrevBlock == NULL);
      assert(dbc->pLastPi->pNextBlock == NULL);

      blocks = 0;
      pWalk = dbc->pFirstPi;
      pPrev = NULL;
      memset(pPrevNetwop, 0, sizeof(pPrevNetwop));

      while (pWalk != NULL)
      {
         blocks += 1;
         netwop = pWalk->blk.pi.netwop_no;
         assert(netwop < MAX_NETWOP_COUNT);
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

      for (netwop=0; netwop < MAX_NETWOP_COUNT; netwop++)
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
      for (netwop=dbc->pAiBlock->blk.ai.netwopCount; netwop < MAX_NETWOP_COUNT; netwop++)
      {
         assert(dbc->pFirstNetwopPi[netwop] == NULL);
      }
      assert(blocks == 0);
   }
   else
   {  // no PI in the DB -> all netwop pointers must be invalid
      assert(dbc->pLastPi == NULL);
      for (netwop=0; netwop < MAX_NETWOP_COUNT; netwop++)
      {
         assert(dbc->pFirstNetwopPi[netwop] == NULL);
      }
   }

   // check defect PI: unsorted -> just check linkage
   pWalk = dbc->pObsoletePi;
   while (pWalk != NULL)
   {
      assert(pWalk->type == BLOCK_TYPE_PI);
      assert(pWalk->version == dbc->pAiBlock->blk.ai.version);
      pWalk = pWalk->pNextBlock;
   }

   // check AI
   if (dbc->pAiBlock != NULL)
   {
      assert(dbc->pAiBlock->type == BLOCK_TYPE_AI);
      assert((dbc->pAiBlock->pNextBlock == NULL) && (dbc->pAiBlock->pPrevBlock == NULL));
      assert(dbc->pAiBlock->blk.ai.netwopCount < MAX_NETWOP_COUNT);
   }

   // check OI
   if (dbc->pOiBlock != NULL)
   {
      assert(dbc->pOiBlock->type == BLOCK_TYPE_OI);
      assert((dbc->pOiBlock->pNextBlock == NULL) && (dbc->pOiBlock->pPrevBlock == NULL));
   }

   return TRUE;
}
#endif // DEBUG_GLOBAL_SWITCH == ON

// ---------------------------------------------------------------------------
// Removes obsolete PI or complete networks from the database
// - if a specific network is to be deleted (usually after a change of the AI
//   CNI table) the according entry in the filter array must be set to TRUE
// - netwop count is a parameter and not taken from the AI so that the function
//   can be used before the new AI is inserted into the DB (e.g. from inside
//   an AI callback function)
//
static void EpgDbRemoveObsoleteNetwops( PDBC dbc, uchar netwopCount, uchar filter[MAX_NETWOP_COUNT] )
{
   EPGDB_BLOCK *pPrev, *pWalk, *pNext;
   uchar netwop;
   bool  do_remove;

   //time_t expireTime = time(NULL) - dbc->expireDelayPi;

   pWalk = dbc->pFirstPi;
   while (pWalk != NULL)
   {
      netwop = pWalk->blk.pi.netwop_no;

      if ( (netwop >= netwopCount) || filter[netwop] )
      {  // the whole network has become obsolete -> remove all PI belonging to it
         do_remove = TRUE;
      }
      else
      {  // check if PI's block number is still registered in the current AI block
         do_remove = FALSE;
      }

      if (do_remove)
      {
         dprintf3("free obsolete PI ptr=%lx, netwop=%d >= %d or filtered\n", (ulong)pWalk, pWalk->blk.pi.netwop_no, netwopCount);
         // notify the GUI
         if (dbc->pPiAcqCb != NULL)
            dbc->pPiAcqCb(dbc, EPGDB_PI_REMOVED, &pWalk->blk.pi, NULL);

         pPrev = pWalk->pPrevBlock;
         pNext = pWalk->pNextBlock;

         // remove from start time pointer chain
         if (pPrev != NULL)
            pPrev->pNextBlock = pNext;
         else
            dbc->pFirstPi = pNext;

         if (pNext != NULL)
            pNext->pPrevBlock = pPrev;
         else
            dbc->pLastPi = pPrev;

         // remove from network pointer chain
         if (pWalk->pPrevNetwopBlock != NULL)
            pWalk->pPrevNetwopBlock->pNextNetwopBlock = pWalk->pNextNetwopBlock;
         else
            dbc->pFirstNetwopPi[netwop] = pWalk->pNextNetwopBlock;

         if (pWalk->pNextNetwopBlock != NULL)
            pWalk->pNextNetwopBlock->pPrevNetwopBlock = pWalk->pPrevNetwopBlock;

         xfree(pWalk);
         pWalk = pNext;
      }
      else
      {
         pWalk = pWalk->pNextBlock;
      }
   }

   assert(EpgDbMgmtCheckChains(dbc));
}

// ---------------------------------------------------------------------------
// Remove expired PI blocks from the database
//
void EpgDbExpire( EPGDB_CONTEXT * dbc )
{
   uchar filter[MAX_NETWOP_COUNT];

   if (dbc->lockLevel == 0)
   {
      if (dbc->pAiBlock != NULL)
      {
         if (dbc->pPiAcqCb != NULL)
            dbc->pPiAcqCb(dbc, EPGDB_PI_PROC_START, NULL, NULL);

         memset(filter, FALSE, sizeof(filter));
         EpgDbRemoveObsoleteNetwops(dbc, dbc->pAiBlock->blk.ai.netwopCount, filter);

         if (dbc->pPiAcqCb != NULL)
            dbc->pPiAcqCb(dbc, EPGDB_PI_PROC_DONE, NULL, NULL);
      }
   }
   else
      fatal0("EpgDb-Expire: database is locked");
}

// ---------------------------------------------------------------------------
// Link a new PI block into the database
//
void EpgDbLinkPi( PDBC dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK * pPrev, EPGDB_BLOCK * pNext )
{
   EPGDB_BLOCK *pWalk;
   uchar netwop = pBlock->blk.pi.netwop_no;

   // Insertion point must lie between the previous and given PI of the given network
   if (pPrev != NULL)
   {
      pWalk = pPrev;
      assert(pPrev->blk.pi.start_time < pBlock->blk.pi.start_time);
   }
   else
      pWalk = dbc->pFirstPi;

   while ( (pWalk != NULL) &&
           ( (pWalk->blk.pi.start_time < pBlock->blk.pi.start_time) ||
             ((pWalk->blk.pi.start_time == pBlock->blk.pi.start_time) && (pWalk->blk.pi.netwop_no < netwop)) ))
   {
      pWalk = pWalk->pNextBlock;
   }
   assert((pNext == NULL) || (pWalk != NULL));
   // determined the exact insertion point: just before walk

   // insert into the network pointer chain
   if (pPrev == NULL)
   {
      pBlock->pNextNetwopBlock = dbc->pFirstNetwopPi[netwop];
      dbc->pFirstNetwopPi[netwop] = pBlock;
   }
   else
   {
      pBlock->pNextNetwopBlock = pPrev->pNextNetwopBlock;
      pPrev->pNextNetwopBlock = pBlock;
   }
   if (pNext != NULL)
      pNext->pPrevNetwopBlock = pBlock;
   pBlock->pPrevNetwopBlock = pPrev;

   // insert into the start time pointer chain
   if (pWalk != NULL)
   {
      pPrev = pWalk->pPrevBlock;
      pNext = pWalk;
   }
   else
   {  // append as very last element (of all netwops)
      pPrev = dbc->pLastPi;
      pNext = NULL;
   }
   if (pPrev != NULL) dprintf3("add pi after  ptr=%lx: netwop=%d, start=%ld\n", (ulong)pPrev, pPrev->blk.pi.netwop_no, pPrev->blk.pi.start_time);
   if (pNext != NULL) dprintf3("add pi before ptr=%lx: netwop=%d, start=%ld\n", (ulong)pNext, pNext->blk.pi.netwop_no, pNext->blk.pi.start_time);
   pBlock->pPrevBlock = pPrev;
   pBlock->pNextBlock = pNext;
   if (pPrev != NULL)
      pPrev->pNextBlock = pBlock;
   else
      dbc->pFirstPi = pBlock;
   if (pNext != NULL)
      pNext->pPrevBlock = pBlock;
   else
      dbc->pLastPi = pBlock;

   // notify the GUI
   if (dbc->pPiAcqCb != NULL)
      dbc->pPiAcqCb(dbc, EPGDB_PI_INSERTED, &pBlock->blk.pi, NULL);
}
