/*
 *  Nextview EPG block queue (FIFO)
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
 *    This module implements a FIFO queue for incoming EPG blocks.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgqueue.c,v 1.7 2002/02/16 11:18:42 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DEBUG_EPGQUEUE_CONSISTANCY  OFF
#define DPRINTF_OFF

#include <time.h>
#include <string.h>
#include <math.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgqueue.h"


#if DEBUG_EPGQUEUE_CONSISTANCY == ON
// ----------------------------------------------------------------------------
// Check consistancy of the queue data structure
//
static bool EpgDbQueue_CheckConsistancy( EPGDB_QUEUE * pQueue )
{
   EPGDB_BLOCK *pWalk, *pPrev;
   uint        count;

   pWalk = pQueue->pFirstBlock;
   pPrev = NULL;
   count = 0;

   while (pWalk != NULL)
   {
      count += 1;
      pPrev = pWalk;
      pWalk = pWalk->pNextBlock;
   }

   assert ((count == pQueue->blockCount) && (pPrev == pQueue->pLastBlock));
   // dummy return value to allow call from inside assert
   return TRUE;
}
#else  // DEBUG_EPGQUEUE_CONSISTANCY == OFF
#define EpgDbQueue_CheckConsistancy(Q)   TRUE
#endif

// ----------------------------------------------------------------------------
// Append a block to the end of a queue
//
void EpgDbQueue_Add( EPGDB_QUEUE * pQueue, EPGDB_BLOCK * pBlock )
{
   if ((pQueue != NULL) && (pBlock != NULL))
   {
      dprintf2("EpgDbQueue-Add: add block type %d, new queue len=%d\n", pBlock->type, pQueue->blockCount + 1);
      if (pQueue->pLastBlock != NULL)
      {  // append the block after the last in the queue
         pQueue->blockCount += 1;
         pBlock->pNextBlock = NULL;
         pQueue->pLastBlock->pNextBlock = pBlock;
         pQueue->pLastBlock = pBlock;
      }
      else
      {  // very first block in the queue
         pQueue->blockCount  = 1;
         pQueue->pFirstBlock = pBlock;
         pQueue->pLastBlock  = pBlock;
      }

      assert(EpgDbQueue_CheckConsistancy(pQueue));
   }
   else
      debug0("EpgDbQueue-Add: called with NULL ptr");
}

// ----------------------------------------------------------------------------
// Pop the first block from the queue
// - returns NULL if empty
//
EPGDB_BLOCK * EpgDbQueue_Get( EPGDB_QUEUE * pQueue )
{
   EPGDB_BLOCK * pBlock = NULL;

   if (pQueue != NULL)
   {
      if (pQueue->pFirstBlock != NULL)
      {
         pQueue->blockCount -= 1;
         pBlock = pQueue->pFirstBlock;

         if (pQueue->pFirstBlock != pQueue->pLastBlock)
            pQueue->pFirstBlock = pQueue->pFirstBlock->pNextBlock;
         else
            pQueue->pFirstBlock = pQueue->pLastBlock = NULL;

         pBlock->pNextBlock = NULL;
      }
      // else: queue empty

      dprintf2("EpgDbQueue-Get: get block type %d, remaining %d\n", ((pBlock != NULL) ? pBlock->type : -1), pQueue->blockCount);
      assert(EpgDbQueue_CheckConsistancy(pQueue));
   }
   else
      debug0("EpgDbQueue-Get: called with NULL ptr");

   return pBlock;
}

// ----------------------------------------------------------------------------
// Return a block with the given type from the scratch buffer
//
EPGDB_BLOCK * EpgDbQueue_GetByType( EPGDB_QUEUE * pQueue, BLOCK_TYPE type )
{
   EPGDB_BLOCK *pWalk, *pPrev;

   if (pQueue != NULL)
   {
      pPrev = NULL;
      pWalk = pQueue->pFirstBlock;
      while (pWalk != NULL)
      {
         if ( (pWalk->type == type) &&
              ((type >= BLOCK_TYPE_GENERIC_COUNT) || (pWalk->blk.all.block_no == 0)) )
         {  // found -> remove block from chain
            if (pPrev != NULL)
               pPrev->pNextBlock = pWalk->pNextBlock;
            else
               pQueue->pFirstBlock = pWalk->pNextBlock;

            if (pWalk->pNextBlock == NULL)
               pQueue->pLastBlock = pPrev;

            pQueue->blockCount -= 1;
            pWalk->pNextBlock = NULL;
            dprintf2("EpgDbQueue-GetByType: get block type %d, remaining %d\n", ((pWalk != NULL) ? pWalk->type : -1), pQueue->blockCount);
            return pWalk;
         }
         pPrev = pWalk;
         pWalk = pWalk->pNextBlock;
      }

      assert(EpgDbQueue_CheckConsistancy(pQueue));
   }
   else
      debug0("EpgDbQueue-GetByType: called with NULL ptr");

   return NULL;
}

// ----------------------------------------------------------------------------
// Free all blocks in the queue
//
void EpgDbQueue_Clear( EPGDB_QUEUE * pQueue )
{
   EPGDB_BLOCK *pWalk, *pNext;

   dprintf0("EpgDbQueue-Clear\n");
   if (pQueue != NULL)
   {
      pWalk = pQueue->pFirstBlock;
      while (pWalk != NULL)
      {
         pNext = pWalk->pNextBlock;
         xfree(pWalk);
         pWalk = pNext;
      }

      pQueue->blockCount  = 0;
      pQueue->pFirstBlock = NULL;
      pQueue->pLastBlock  = NULL;

      assert(EpgDbQueue_CheckConsistancy(pQueue));
   }
   else
      debug0("EpgDbQueue-Clear: called with NULL ptr");
}

// ----------------------------------------------------------------------------
// Create a new, empty queue
//
void EpgDbQueue_Init( EPGDB_QUEUE * pQueue )
{
   dprintf0("EpgDbQueue-Init\n");
   if (pQueue != NULL)
   {
      pQueue->blockCount  = 0;
      pQueue->pFirstBlock = NULL;
      pQueue->pLastBlock  = NULL;

      assert(EpgDbQueue_CheckConsistancy(pQueue));
   }
   else
      debug0("EpgDbQueue-Create: called with NULL ptr");
}

// ----------------------------------------------------------------------------
// Check if there are any blocks in the queue
//
uint EpgDbQueue_GetBlockCount( EPGDB_QUEUE * pQueue )
{
   uint count = 0;

   if (pQueue != NULL)
   {
      assert(EpgDbQueue_CheckConsistancy(pQueue));
      count = pQueue->blockCount;

      dprintf1("EpgDbQueue-GeBlockCount: %s\n", (result ? "yes" : "no"));
   }
   else
      debug0("EpgDbQueue-GeBlockCount: called with NULL ptr");

   return count;
}

// ----------------------------------------------------------------------------
// Peek at the first block in the queue
// - the difference to the pop operator is that the block remains in the queue
//
const EPGDB_BLOCK * EpgDbQueue_Peek( EPGDB_QUEUE * pQueue )
{
   EPGDB_BLOCK * pBlock = NULL;

   if (pQueue != NULL)
   {
      pBlock = pQueue->pFirstBlock;

      dprintf2("EpgDbQueue-Peek: block type %d, %d in queue\n", ((pBlock != NULL) ? pBlock->type : -1), pQueue->blockCount);
      assert(EpgDbQueue_CheckConsistancy(pQueue));
   }
   else
      debug0("EpgDbQueue-Get: called with NULL ptr");

   return pBlock;
}

