/*
 *  Nextview EPG PI timescale queue
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
 *    Maintains a queue to fill the timescales in the statistical popups.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgtscqueue.h"

#if DEBUG_SWITCH == ON
// ----------------------------------------------------------------------------
// Check consistancy of the doubly-linked queue and the buffers contained within
//
static bool EpgTscQueue_CheckConsistancy( EPGDB_PI_TSC * pQueue )
{
   EPGDB_PI_TSC_BUF * pWalk;
   EPGDB_PI_TSC_BUF * pPrev;

   pWalk = pQueue->pHead;
   pPrev = NULL;

   while (pWalk != NULL)
   {
      // check the consistancy of this buffer
      // note: a similar consistancy check if performed in the Push function
      assert(pWalk->provCni != 0);

      assert(pWalk->fillCount <= PI_TSC_GET_BUF_COUNT());
      assert(pWalk->popIdx <= pWalk->fillCount);  // == is allowed

      // check the queue linkage
      assert(pWalk->pPrev == pPrev);

      pPrev = pWalk;
      pWalk = pWalk->pNext;
   }
   assert (pPrev == pQueue->pTail);

   // dummy return value to allow call from inside assert
   return TRUE;
}
#endif  // DEBUG_GLOBAL_SWITCH == ON

// ----------------------------------------------------------------------------
// Initialize a PI timescale queue
// - Warning: an init must only be done during startup; when there are any elements
//   in the queue when this function is called, the allocated memory is lost.
//
void EpgTscQueue_Init( EPGDB_PI_TSC * pQueue )
{
   if (pQueue != NULL)
   {
      memset(pQueue, 0, sizeof(EPGDB_PI_TSC));

      assert(EpgTscQueue_CheckConsistancy(pQueue));
   }
   else
      fatal0("EpgTscQueue-Init: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Free resources used by a PI timescale queue
//
void EpgTscQueue_Clear( EPGDB_PI_TSC * pQueue )
{
   EPGDB_PI_TSC_BUF  * pTscBuf;
   EPGDB_PI_TSC_BUF  * pNext;

   if (pQueue != NULL)
   {
      pTscBuf = pQueue->pHead;
      while (pTscBuf != NULL)
      {
         pNext = pTscBuf->pNext;
         xfree(pTscBuf);
         pTscBuf = pNext;
      }
      pQueue->pHead = NULL;
      pQueue->pTail = NULL;

      assert(EpgTscQueue_CheckConsistancy(pQueue));
   }
   else
      fatal0("EpgTscQueue-Clear: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Unlink the buffer from the double-linked queue
//
static void EpgTscQueue_Unlink( EPGDB_PI_TSC * pQueue, EPGDB_PI_TSC_BUF * pTscBuf )
{
   if (pTscBuf->pPrev != NULL)
      pTscBuf->pPrev->pNext = pTscBuf->pNext;
   else
      pQueue->pHead = pTscBuf->pNext;

   if (pTscBuf->pNext != NULL)
      pTscBuf->pNext->pPrev = pTscBuf->pPrev;
   else
      pQueue->pTail = pTscBuf->pPrev;

   pTscBuf->pPrev = NULL;
   pTscBuf->pNext = NULL;

   assert(EpgTscQueue_CheckConsistancy(pQueue));
}

// ----------------------------------------------------------------------------
// Store parameters for subsequent read accesses in queue head
// - used in subsequent add or pop operations
// - they are copied into each buffer struct
// - storing the parameters in the head avoids having to pass them as arguments
//   through all functions down to the low-level add function. where new buffers
//   are allocated and initialized
//
void EpgTscQueue_SetProvCni( EPGDB_PI_TSC * pQueue, uint provCni )
{
   if (pQueue != NULL)
   {
      assert(provCni != 0);  // doesn't harm to set it even if 0

      pQueue->readProvCni = provCni;
   }
   else
      fatal0("EpgTscQueue-SetMode: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Retrieve the oldest element from the PI timescale queue
// - the oldest element if the first unpopped index in the last buffer in the queue
// - when all elements have been popped from a buffer it's freed - but not before
//   the next access, because the function returns a pointer into the buffer
//
const EPGDB_PI_TSC_ELEM * EpgTscQueue_PopElem( EPGDB_PI_TSC * pQueue, time_t * pBaseTime )
{
   EPGDB_PI_TSC_BUF  * pTscBuf;
   EPGDB_PI_TSC_BUF  * pTemp;
   EPGDB_PI_TSC_ELEM * pTscElem = NULL;

   if (pQueue != NULL)
   {
      // start at the tail, i.e. with the oldest data in the queue
      pTscBuf = pQueue->pTail;

      if (pTscBuf != NULL)
      {  // queue is not empty

         // search for a non-empty buffer with matching CNI
         do
         {
            if (pTscBuf->popIdx >= pTscBuf->fillCount)
            {  // found a fully popped buffer -> free it

               pTemp = pTscBuf->pPrev;
               // first unlink the buffer from the queue...
               EpgTscQueue_Unlink(pQueue, pTscBuf);
               // ...then free it
               xfree(pTscBuf);
               pTscBuf = pTemp;
            }
            else
            {  // buffer is not empty: check if it's the expected provider
               if ( (pTscBuf->locked == FALSE) &&
                    (pTscBuf->provCni == pQueue->readProvCni) )
               {  // found one
                  break;
               }
               // try the next buffer
               pTscBuf = pTscBuf->pPrev;
            }
         } while (pTscBuf != NULL);

         if (pTscBuf != NULL)
         {
            // result pointer: first unpopped element
            pTscElem = pTscBuf->pi + pTscBuf->popIdx;
            pTscBuf->popIdx += 1;

            // return base time: required to decode start and stop times
            if (pBaseTime != NULL)
               *pBaseTime = pTscBuf->baseTime;
         }
      }
   }
   else
      fatal0("EpgTscQueue-PopElem: illegal NULL ptr param");

   return pTscElem;
}

// ----------------------------------------------------------------------------
// Adjust base time for all entries in a buffer
// - called when a PI is added whose start time is lower than the old base time
// - when the base time is changed all offsets must be adjusted
//
static void EpgTscQueue_AdjustBaseTime( EPGDB_PI_TSC_BUF * pTscBuf, time_t startTime )
{
   EPGDB_PI_TSC_ELEM  * pTsc;
   uint   diff;
   uint   idx;

   // adjust by more than required to reduce probability of future adjustments
   diff = (pTscBuf->baseTime - startTime + 1*24*60*60) / 60;

   pTsc = pTscBuf->pi;
   for (idx=0; idx < pTscBuf->fillCount; idx++, pTsc++)
   {
      pTsc->startOffMins += diff;
   }

   debug2("EpgTscQueue-AdjustBaseTime: readjusted base time of %d queue entries by %d minutes", pTscBuf->fillCount, (uint)diff);
   pTscBuf->baseTime = startTime;
}

// ----------------------------------------------------------------------------
// Create and insert a new empty buffer
//
static EPGDB_PI_TSC_BUF * EpgTscQueue_CreateNew( EPGDB_PI_TSC * pQueue )
{
   EPGDB_PI_TSC_BUF   * pTscBuf;
   uint  maxCount;

   maxCount = PI_TSC_GET_BUF_COUNT();
   pTscBuf = (EPGDB_PI_TSC_BUF*) xmalloc(PI_TSC_GET_BUF_SIZE(maxCount));

   // insert the buffer at the head of the queue
   if (pQueue->pHead != NULL)
   {
      pQueue->pHead->pPrev = pTscBuf;
      pTscBuf->pNext = pQueue->pHead;
      pTscBuf->pPrev = NULL;
      pQueue->pHead = pTscBuf;
   }
   else
   {
      pTscBuf->pPrev = pTscBuf->pNext = NULL;
      pQueue->pHead = pQueue->pTail = pTscBuf;
   }

   // initialize the buffer parameters
   pTscBuf->provCni   = pQueue->writeProvCni;
   pTscBuf->locked    = FALSE;
   pTscBuf->fillCount = 0;
   pTscBuf->popIdx    = 0;

   if ((pTscBuf->pNext != NULL) && (pTscBuf->provCni == pTscBuf->pNext->provCni))
      pTscBuf->baseTime = pTscBuf->pNext->baseTime;
   else
      pTscBuf->baseTime = 0;

   assert(EpgTscQueue_CheckConsistancy(pQueue));

   return pTscBuf;
}

// ----------------------------------------------------------------------------
// Append a data element to the timescale buffer
// - if the start time equals the stop time of the previous element and network
//   and flags are identical, it's merged with the previous entry
//
static void EpgTscQueue_Append( EPGDB_PI_TSC * pQueue, time_t startTime, time_t stopTime, uint netwop, uchar flags )
{
   EPGDB_PI_TSC_BUF   * pTscBuf;
   EPGDB_PI_TSC_ELEM  * pTsc;
   bool concatenated;

   if (pQueue != NULL)
   {
      pTscBuf = pQueue->pHead;
      if (pTscBuf == NULL)
      {
         pTscBuf = EpgTscQueue_CreateNew(pQueue);
      }

      if (pTscBuf->baseTime == 0)
      {  // first PI: estimate a db base time
         assert(pTscBuf->fillCount == 0);
         // add a large offset to make (quite) sure all PI start later
         pTscBuf->baseTime = startTime - 10*24*60*60;
      }

      concatenated = FALSE;
      if (pTscBuf->fillCount > 0)
      {
         // check if the PI continues exactly where the last one ended and has equal parameters
         pTsc = pTscBuf->pi + pTscBuf->fillCount - 1;
         if ( (startTime == pTscBuf->baseTime + (60 * (time_t)(pTsc->startOffMins + pTsc->durationMins))) &&
              (netwop == pTsc->netwop) &&
              ((flags & ~PI_TSC_MASK_IS_LAST) == (pTsc->flags & ~PI_TSC_MASK_IS_LAST)) )
         {  // concatenate PI
            pTsc->durationMins += (stopTime - startTime) / 60;
            pTsc->flags        |= flags & PI_TSC_MASK_IS_LAST;
            pTsc->concatCount  += 1;
            concatenated = TRUE;
         }
      }

      if (concatenated == FALSE)
      {
         if (pTscBuf->fillCount >= PI_TSC_GET_BUF_COUNT())
         {  // buffer is full -> allocate a new one and insert it at the head of the chain
            pTscBuf = EpgTscQueue_CreateNew(pQueue);
         }

         // check if the base time is too high -> must shift base time (should happen very rarely)
         if (startTime < pTscBuf->baseTime)
         {
            EpgTscQueue_AdjustBaseTime(pTscBuf, startTime);
         }

         pTsc = pTscBuf->pi + pTscBuf->fillCount;
         pTscBuf->fillCount += 1;

         pTsc->startOffMins = (startTime - pTscBuf->baseTime) / 60;
         pTsc->durationMins = (stopTime - startTime) / 60;
         pTsc->netwop       = netwop;
         pTsc->concatCount  = 1;
         pTsc->flags        = flags;
      }
   }
   else
      fatal0("EpgTscQueue-Append: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// display coverage by PI currently in database
//
void EpgTscQueue_AddAll( EPGDB_PI_TSC * pQueue, EPGDB_CONTEXT * dbc,
                         time_t now, time_t tsAcq )
{
   const EPGDB_BLOCK * pBlock;
   const AI_BLOCK  *pAi;
   const PI_BLOCK  *pPi;
   time_t lastStopTime;
   uchar  flags;
   uint   netwop;
   const time_t tsDay1 = tsAcq + 24*60*60;
   const time_t tsDay2 = tsDay1 + 24*60*60;

   if ((pQueue != NULL) && (dbc != NULL))
   {
      EpgDbLockDatabase(dbc, TRUE);
      pAi = EpgDbGetAi(dbc);
      if (pAi != NULL)
      {
         // set parameters for buffer creation
         pQueue->writeProvCni = EpgDbContextGetCni(dbc);

         // loop over all PI in the database (i.e. in parallel across all netwops)
         for (netwop=0; netwop < pAi->netwopCount; netwop++)
         {
            pBlock = dbc->pFirstNetwopPi[netwop];
            lastStopTime = 0;
            while (pBlock != NULL)
            {
               pPi = &pBlock->blk.pi;
               flags = 0;

               if (pBlock->acqTimestamp < tsAcq)
                  flags |= PI_TSC_MASK_IS_OLD_VERSION;

               if (pPi->stop_time < now)
                  flags |= PI_TSC_MASK_IS_EXPIRED;
               else if (pBlock->blk.pi.start_time < tsDay1)
                  flags |= (0 << PI_TSC_MASK_DAY_SHIFT);
               else if (pBlock->blk.pi.start_time < tsDay2)
                  flags |= (1 << PI_TSC_MASK_DAY_SHIFT);
               else
                  flags |= (2 << PI_TSC_MASK_DAY_SHIFT);

               if (PI_HAS_DESC_TEXT(pPi))
                  flags |= PI_TSC_MASK_HAS_DESC_TEXT;

               if ((pPi->start_time > lastStopTime) && (lastStopTime != 0))
               {  // block missing between the previous and the last -> insert missing range
                  EpgTscQueue_Append(pQueue, lastStopTime, pPi->start_time, netwop, PI_TSC_MASK_IS_MISSING);
               }

               EpgTscQueue_Append(pQueue, pPi->start_time, pPi->stop_time, netwop, flags);

               lastStopTime = pPi->stop_time;
               pBlock = pBlock->pNextNetwopBlock;
            }
         }

         // loop over all defective PI in the database
         pBlock = dbc->pObsoletePi;
         while (pBlock != NULL)
         {
            pPi = &pBlock->blk.pi;
            flags = PI_TSC_MASK_IS_DEFECTIVE;

            if (pBlock->pNextNetwopBlock == NULL)
               flags |= PI_TSC_MASK_IS_LAST;

            if (PI_HAS_DESC_TEXT(pPi))
               flags |= PI_TSC_MASK_HAS_DESC_TEXT;

            EpgTscQueue_Append(pQueue, pPi->start_time, pPi->stop_time, pPi->netwop_no, flags);

            pBlock = pBlock->pNextBlock;
         }
      }

      EpgDbLockDatabase(dbc, FALSE);
   }
   else
      fatal2("EpgTscQueue-AddAll: illegal NULL ptr params: pQueue=%lx dbc=%lx", (long)pQueue, (long)dbc);
}
