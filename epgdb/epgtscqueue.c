/*
 *  Nextview EPG PI timescale queue
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
 *    Maintains a queue to fill the timescale poup windows.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgtscqueue.c,v 1.8 2008/02/03 15:42:36 tom Exp tom $
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
#include "epgdb/epgstream.h"
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
      assert(pWalk->mode < PI_TSC_MODE_COUNT);
      assert(pWalk->provCni != 0);

      assert(pWalk->fillCount <= PI_TSC_GET_BUF_COUNT(pWalk->mode));
      assert(pWalk->popIdx <= pWalk->fillCount);  // == is allowed

      // check the queue linkage
      assert(pWalk->u.p.pPrev == pPrev);

      pPrev = pWalk;
      pWalk = pWalk->u.p.pNext;
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
         pNext = pTscBuf->u.p.pNext;
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
   if (pTscBuf->u.p.pPrev != NULL)
      pTscBuf->u.p.pPrev->u.p.pNext = pTscBuf->u.p.pNext;
   else
      pQueue->pHead = pTscBuf->u.p.pNext;

   if (pTscBuf->u.p.pNext != NULL)
      pTscBuf->u.p.pNext->u.p.pPrev = pTscBuf->u.p.pPrev;
   else
      pQueue->pTail = pTscBuf->u.p.pPrev;

   pTscBuf->u.p.pPrev = NULL;
   pTscBuf->u.p.pNext = NULL;

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
// Check if there are any elements in the queue
//
bool EpgTscQueue_HasElems( EPGDB_PI_TSC * pQueue )
{
   bool result = FALSE;

   if (pQueue != NULL)
   {
      result = (pQueue->pHead != NULL);
   }
   else
      fatal0("EpgTscQueue-HasElems: illegal NULL ptr param");

   return result;
}

// ----------------------------------------------------------------------------
// Unlocks all buffers in the queue
// - called after the EPG block queue has been processed
// - required for AI version and provider changes; the new AI block must processed
//   before the timescale data which refers to the new network table.
//
void EpgTscQueue_UnlockBuffers( EPGDB_PI_TSC * pQueue )
{
   EPGDB_PI_TSC_BUF  * pTscBuf;

   if (pQueue != NULL)
   {
      pTscBuf = pQueue->pHead;
      while (pTscBuf != NULL)
      {
         if ( pTscBuf->locked )
         {
            pTscBuf->locked = FALSE;
         }
         pTscBuf = pTscBuf->u.p.pNext;
      }
   }
   else
      fatal0("EpgTscQueue-UnlockBuffers: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Clears all incremental data of a given provider
// - used when initial data is pushed onto the buffer
//   for net acq mode only, when initial data is generated by the daemon
//
static void EpgTscQueue_ClearIncremental( EPGDB_PI_TSC * pQueue, uint provCni )
{
   EPGDB_PI_TSC_BUF  * pTscBuf;
   EPGDB_PI_TSC_BUF  * pTemp;

   if (pQueue != NULL)
   {
      pTscBuf = pQueue->pHead;
      while (pTscBuf != NULL)
      {
         if ( (pTscBuf->provCni == provCni) &&
              (pTscBuf->mode == PI_TSC_MODE_INCREMENTAL) )
         {
            // first unlink the buffer from the queue...
            pTemp = pTscBuf->u.p.pNext;
            EpgTscQueue_Unlink(pQueue, pTscBuf);
            // ...then free it
            xfree(pTscBuf);
            pTscBuf = pTemp;
         }
         else
            pTscBuf = pTscBuf->u.p.pNext;
      }
   }
   else
      fatal0("EpgTscQueue-ClearIncremental: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Discards all unlocked buffers
// - used to discard data from previous provider after switch
//
void EpgTscQueue_ClearUnprocessed( EPGDB_PI_TSC * pQueue )
{
   EPGDB_PI_TSC_BUF  * pTscBuf;
   EPGDB_PI_TSC_BUF  * pTemp;

   if (pQueue != NULL)
   {
      pTscBuf = pQueue->pHead;
      while (pTscBuf != NULL)
      {
         if (pTscBuf->locked == FALSE)
         {
            // first unlink the buffer from the queue...
            pTemp = pTscBuf->u.p.pNext;
            EpgTscQueue_Unlink(pQueue, pTscBuf);
            // ...then free it
            xfree(pTscBuf);
            pTscBuf = pTemp;
         }
         else
            pTscBuf = pTscBuf->u.p.pNext;
      }
   }
   else
      fatal0("EpgTscQueue-ClearUnprocessed: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Retrieve one buffer from the queue
// - only to be used by the network deamon when timscale buffers are
//   transmitted through a network connection
// - retrieves the last buffer in the queue, which contains the oldest data
//
const EPGDB_PI_TSC_BUF * EpgTscQueue_PopBuffer( EPGDB_PI_TSC * pQueue, uint * pSize )
{
   EPGDB_PI_TSC_BUF  * pTscBuf = NULL;

   if (pQueue != NULL)
   {
      pTscBuf = pQueue->pTail;

      if (pTscBuf != NULL)
      {
         // disconnect the buffer from the chain
         EpgTscQueue_Unlink(pQueue, pTscBuf);

         if (pTscBuf->fillCount > 0)
         {
            // return the size of the buffer
            if (pSize != NULL)
               *pSize = PI_TSC_GET_BUF_SIZE(pTscBuf->fillCount);
            else
               debug0("EpgTscQueue-PopBuffer: NULL ptr for pSize");
         }
         else
         {  // empty buffer - should never happen -> discard it
            xfree(pTscBuf);
            pTscBuf = NULL;
         }
      }
   }
   else
      fatal0("EpgTscQueue-PopBuffer: illegal NULL ptr param");

   return pTscBuf;
}

// ----------------------------------------------------------------------------
// Add a full buffer to the queue
// - only to be used by the client in network acquisition mode when timscale
//   buffers are received through a network connection
// - the buffer is inserted at the head of the queue, where the newest info is kept
// - the buffer is marked as locked; this is neccessary for AI version and provider
//   changes; the new AI block must processed before the timescale data which refers
//   to the new network table
// - if initial-mode data is added, all older incremental data of the same provider
//   is removed, because it would confuse the GUI (the minimum start time for the
//   provider is guessed from the first TSC buffer)
//
bool EpgTscQueue_PushBuffer( EPGDB_PI_TSC * pQueue, EPGDB_PI_TSC_BUF * pTscBuf, uint size )
{
   bool result = FALSE;

   if (pQueue != NULL)
   {
      // check consistancy of size, mode and fill count
      if ( (size >= sizeof(EPGDB_PI_TSC_BUF)) &&
           (pTscBuf->mode < PI_TSC_MODE_COUNT) &&
           (pTscBuf->fillCount <= PI_TSC_GET_BUF_COUNT(pTscBuf->mode)) &&
           (size == PI_TSC_GET_BUF_SIZE(pTscBuf->fillCount)) )
      {
         // reset the reader index to 0
         assert(pTscBuf->popIdx == 0);
         pTscBuf->popIdx = 0;

         // lock the buffer for reading (see comment above)
         pTscBuf->locked = TRUE;

         // insert the buffer at the head of the queue
         if (pQueue->pHead != NULL)
         {
            pQueue->pHead->u.p.pPrev = pTscBuf;
            pTscBuf->u.p.pNext = pQueue->pHead;
            pTscBuf->u.p.pPrev = NULL;
            pQueue->pHead = pTscBuf;
         }
         else
         {
            pTscBuf->u.p.pPrev = pTscBuf->u.p.pNext = NULL;
            pQueue->pHead = pQueue->pTail = pTscBuf;
         }

         // check for redundant incremental data and remove it
         if (pTscBuf->mode == PI_TSC_MODE_INITIAL)
         {
            EpgTscQueue_ClearIncremental(pQueue, pTscBuf->provCni);
         }

         assert(EpgTscQueue_CheckConsistancy(pQueue));
         result = TRUE;
      }
      else
         debug3("EpgTscQueue-PushBuffer: buffer with %d elements has illegal mode %d or size %d", pTscBuf->fillCount, pTscBuf->mode, size);
   }
   else
      fatal0("EpgTscQueue-PushBuffer: illegal NULL ptr param");

   return result;
}

// ----------------------------------------------------------------------------
// Check if the PI timescale queue contains incremental mode data only
//
bool EpgTscQueue_IsIncremental( EPGDB_PI_TSC * pQueue )
{
   EPGDB_PI_TSC_BUF  * pTscBuf;
   bool  isIncremental;

   isIncremental = TRUE;

   if (pQueue != NULL)
   {
      pTscBuf = pQueue->pTail;

      while (pTscBuf != NULL)
      {
         if ( (pTscBuf->locked == FALSE) &&
              (pTscBuf->provCni == pQueue->readProvCni) )
         {
            if (pTscBuf->mode != PI_TSC_MODE_INCREMENTAL)
            {
               isIncremental = FALSE;
               break;
            }
         }
         pTscBuf = pTscBuf->u.p.pPrev;
      }
   }
   else
      fatal0("EpgTscQueue-IsIncremental: illegal NULL ptr param");

   return isIncremental;
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

               pTemp = pTscBuf->u.p.pPrev;
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
               pTscBuf = pTscBuf->u.p.pPrev;
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
// Get last received netwop index from PI timescale queue
// - the last received is the last entry in the head of the queue, because
//   timescale buffer elements are chained latest first
// - returns NULL pointer if the queue is empty
//
const EPGDB_PI_TSC_ELEM * EpgTscQueue_PeekTail( EPGDB_PI_TSC * pQueue, uint provCni )
{
   EPGDB_PI_TSC_BUF  * pTscBuf;
   EPGDB_PI_TSC_ELEM * result = NULL;

   if (pQueue != NULL)
   {
      pTscBuf = pQueue->pHead;
      while (pTscBuf != NULL)
      {
         if ( (pTscBuf->locked == FALSE) &&
              (pTscBuf->provCni == provCni) )
         {
            if (pTscBuf->fillCount > 0)
               result = pTscBuf->pi + pTscBuf->fillCount - 1;
            else
               debug2("EpgTscQueue-PeekTail: empty TSC buffer 0x%lx, provCni 0x%04X", (long)pTscBuf, provCni);
            break;
         }
         pTscBuf = pTscBuf->u.p.pNext;
      }
   }
   else
      fatal0("EpgTscQueue-PeekTail: illegal NULL ptr param");

   return result;
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

   maxCount = PI_TSC_GET_BUF_COUNT(pQueue->writeMode);
   pTscBuf = xmalloc(PI_TSC_GET_BUF_SIZE(maxCount));

   // insert the buffer at the head of the queue
   if (pQueue->pHead != NULL)
   {
      pQueue->pHead->u.p.pPrev = pTscBuf;
      pTscBuf->u.p.pNext = pQueue->pHead;
      pTscBuf->u.p.pPrev = NULL;
      pQueue->pHead = pTscBuf;
   }
   else
   {
      pTscBuf->u.p.pPrev = pTscBuf->u.p.pNext = NULL;
      pQueue->pHead = pQueue->pTail = pTscBuf;
   }

   // initialize the buffer parameters
   pTscBuf->provCni   = pQueue->writeProvCni;
   pTscBuf->mode      = pQueue->writeMode;
   pTscBuf->locked    = FALSE;
   pTscBuf->fillCount = 0;
   pTscBuf->popIdx    = 0;

   if ((pTscBuf->u.p.pNext != NULL) && (pTscBuf->provCni == pTscBuf->u.p.pNext->provCni))
      pTscBuf->baseTime = pTscBuf->u.p.pNext->baseTime;
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
static void EpgTscQueue_Append( EPGDB_PI_TSC * pQueue, time_t startTime, time_t stopTime, uchar netwop, uchar flags, uint blockIdx )
{
   EPGDB_PI_TSC_BUF   * pTscBuf;
   EPGDB_PI_TSC_ELEM  * pTsc;
   bool concatenated;

   if (pQueue != NULL)
   {
      pTscBuf = pQueue->pHead;
      if ( (pTscBuf == NULL) ||
           (pQueue->writeProvCni != pTscBuf->provCni) || (pQueue->writeMode != pTscBuf->mode) )
      {  // queue is empty or params changed -> allocate new buffer
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
         if (pTscBuf->fillCount >= PI_TSC_GET_BUF_COUNT(pTscBuf->mode))
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
         pTsc->blockIdx     = (blockIdx <= 0xff) ? blockIdx : 0xff;
         pTsc->concatCount  = 1;
         pTsc->flags        = flags;
      }
   }
   else
      fatal0("EpgTscQueue-Append: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Add the time range covered by a new PI to the timescale buffer
//
void EpgTscQueue_AddPi( EPGDB_PI_TSC * pQueue, EPGDB_CONTEXT * dbc, const PI_BLOCK *pPi, uchar stream )
{
   const AI_BLOCK  *pAi;
   const AI_NETWOP *pNetwop;
   time_t now;
   uint   blockIdx;
   uchar  flags;
   bool   isMerged;

   if ((pQueue != NULL) && (dbc != NULL))
   {
      EpgDbLockDatabase(dbc, TRUE);
      pAi = EpgDbGetAi(dbc);
      if ((pAi != NULL) && (pPi->netwop_no < pAi->netwopCount))
      {
         isMerged = EpgDbContextIsMerged(dbc);
         // set parameters for buffer creation
         pQueue->writeProvCni = EpgDbContextGetCni(dbc);
         pQueue->writeMode    = PI_TSC_MODE_INCREMENTAL;

         now      = time(NULL);
         pNetwop  = AI_GET_NETWOP_N(pAi, pPi->netwop_no);
         flags    = PI_TSC_MASK_IS_CUR_VERSION;

         if (isMerged == FALSE)
         {  // check if the block is one of the first 5 for that netwop
            blockIdx = EpgDbGetPiBlockIndex(pNetwop->startNo, pPi->block_no);
         }
         else
            blockIdx = 0xff;

         // check if the block is the very last for that netwop -> clear "missing" range
         if (!isMerged && (pPi->block_no == pNetwop->stopNoSwo))
            flags |= PI_TSC_MASK_IS_LAST;

         // check for conflict with other block -> mark as faulty
         if (EpgDbSearchObsoletePi(dbc, pPi->netwop_no, pPi->start_time, pPi->stop_time) != NULL)
            flags |= PI_TSC_MASK_IS_DEFECTIVE;

         if (pPi->stop_time < now)
            flags |= PI_TSC_MASK_IS_EXPIRED;

         if (stream == NXTV_STREAM_NO_1)
            flags |= PI_TSC_MASK_IS_STREAM_1;

         if (PI_HAS_SHORT_INFO(pPi))
            flags |= PI_TSC_MASK_HAS_SHORT_I;

         if (PI_HAS_LONG_INFO(pPi))
            flags |= PI_TSC_MASK_HAS_LONG_I;

         EpgTscQueue_Append(pQueue, pPi->start_time, pPi->stop_time, pPi->netwop_no, flags, blockIdx);
      }
      EpgDbLockDatabase(dbc, FALSE);
   }
   else
      fatal2("EpgTscQueue-AddPi: illegal NULL ptr params: pQueue=%lx dbc=%lx", (long)pQueue, (long)dbc);
}

// ----------------------------------------------------------------------------
// display coverage by PI currently in database
//
void EpgTscQueue_AddAll( EPGDB_PI_TSC * pQueue, EPGDB_CONTEXT * dbc )
{
   const EPGDB_BLOCK * pBlock;
   const AI_BLOCK  *pAi;
   const PI_BLOCK  *pPi;
   const AI_NETWOP *pNetwops;
   time_t now;
   time_t max_time;
   time_t lastStopTime;
   uint   lastBlockNo;
   uchar  cur_stream;
   uchar  flags;
   uint   netwop;
   ulong  bcnt;
   uint   blockIdx;
   bool   isMerged;

   if ((pQueue != NULL) && (dbc != NULL))
   {
      EpgDbLockDatabase(dbc, TRUE);
      pAi = EpgDbGetAi(dbc);
      if (pAi != NULL)
      {
         isMerged = EpgDbContextIsMerged(dbc);
         // set parameters for buffer creation
         pQueue->writeProvCni = EpgDbContextGetCni(dbc);
         pQueue->writeMode    = PI_TSC_MODE_INITIAL;

         now      = time(NULL);
         pNetwops = AI_GET_NETWOPS(pAi);

         // loop over all PI in the database (i.e. in parallel across all netwops)
         for (netwop=0; netwop < pAi->netwopCount; netwop++)
         {
            pBlock = dbc->pFirstNetwopPi[netwop];
            lastBlockNo  = 0x10000;
            lastStopTime = 0;
            while (pBlock != NULL)
            {
               pPi = &pBlock->blk.pi;
               flags = 0;

               if (isMerged == FALSE)
               {
                  cur_stream = EpgDbGetStreamByBlockNo(dbc, pBlock);
                  // the following is required in case the blocks are listed in AI as belonging
                  // to stream 2, but are really transmitted in stream 1 (e.g. when the first PI
                  // is very far in the future) -> assign to stream 1 to avoid inconsistency with
                  // newly rx'ed blocks (which are always assigned the stream they were rx'ed in)
                  if (pBlock->stream < cur_stream)
                     cur_stream = pBlock->stream;
               }
               else
                  cur_stream = pBlock->stream;

               if (pBlock->blk.pi.block_no_in_ai)
               {
                  if (cur_stream == 0)
                  {
                     if ((pBlock->version == pAi->version) && (pBlock->stream == cur_stream))
                        flags |= PI_TSC_MASK_IS_CUR_VERSION;
                     flags |= PI_TSC_MASK_IS_STREAM_1;
                  }
                  else
                  {
                     if ((pBlock->version == pAi->version_swo) && (pBlock->stream == cur_stream))
                        flags |= PI_TSC_MASK_IS_CUR_VERSION;
                  }
               }

               if (pPi->stop_time < now)
                  flags |= PI_TSC_MASK_IS_EXPIRED;

               if (isMerged == FALSE)
               {
                  blockIdx = EpgDbGetPiBlockIndex(pNetwops[netwop].startNo, pPi->block_no);
               }
               else
                  blockIdx = 0xff;

               if (PI_HAS_SHORT_INFO(pPi))
                  flags |= PI_TSC_MASK_HAS_SHORT_I;

               if (PI_HAS_LONG_INFO(pPi))
                  flags |= PI_TSC_MASK_HAS_LONG_I;

               if (isMerged == FALSE)
               {
                  if (lastBlockNo <= 0xffff)
                  {
                     if (pPi->block_no != ((lastBlockNo + 1) % 0x10000))
                     {  // block missing between the previous and the last -> insert missing range
                        EpgTscQueue_Append(pQueue, lastStopTime, pPi->start_time, netwop, PI_TSC_MASK_IS_MISSING, 0xff);
                     }
                  }
                  else
                  {
                     if (pPi->block_no != pNetwops[netwop].startNo)
                     {  // block missing before the first PI -> insert missing range
                        EpgTscQueue_Append(pQueue, dbc->pFirstPi->blk.pi.start_time, pPi->start_time, netwop, PI_TSC_MASK_IS_MISSING, 0xff);
                     }
                  }
               }

               EpgTscQueue_Append(pQueue, pPi->start_time, pPi->stop_time, netwop, flags, blockIdx);

               lastBlockNo  = pPi->block_no;
               lastStopTime = pPi->stop_time;
               pBlock = pBlock->pNextNetwopBlock;
            }

            if (isMerged == FALSE)
            {
               if (lastBlockNo <= 0xffff)
               {
                  if (lastBlockNo != pNetwops[netwop].stopNoSwo)
                  {  // at least one block missing at the end -> mark estimated range as missing
                     max_time = EpgDbGetAiUpdateTime(dbc) + pNetwops[netwop].dayCount * (24*60*60);
                     if (max_time <= lastStopTime)
                        max_time = lastStopTime + 60*60;

                     EpgTscQueue_Append(pQueue, lastStopTime, max_time, netwop, PI_TSC_MASK_IS_MISSING, 0xff);
                  }
               }
               else
               {  // no PI at all found for this network
                  // calculate sum of all PI listed in AI for the current network
                  bcnt = EpgDbGetPiBlockCount(pNetwops[netwop].startNo, pNetwops[netwop].stopNoSwo);
                  if (bcnt > 0)
                  {  // however there should be some -> mark as missing
                     max_time = EpgDbGetAiUpdateTime(dbc) + pNetwops[netwop].dayCount * (24*60*60);

                     EpgTscQueue_Append(pQueue, ((dbc->pFirstPi != NULL) ? dbc->pFirstPi->blk.pi.start_time : now),
                                        max_time, netwop, PI_TSC_MASK_IS_MISSING, 0xff);
                  }
               }
            }
         }

         // loop over all defective PI in the database
         pBlock = dbc->pObsoletePi;
         while (pBlock != NULL)
         {
            pPi = &pBlock->blk.pi;
            flags = PI_TSC_MASK_IS_DEFECTIVE;

            if (isMerged == FALSE)
            {
               blockIdx = EpgDbGetPiBlockIndex(AI_GET_NETWOP_N(pAi, pPi->netwop_no)->startNo, pPi->block_no);
            }
            else
               blockIdx = 0xff;

            if (!isMerged && (pPi->block_no == AI_GET_NETWOP_N(pAi, pPi->netwop_no)->stopNoSwo))
               flags |= PI_TSC_MASK_IS_LAST;

            if (PI_HAS_SHORT_INFO(pPi))
               flags |= PI_TSC_MASK_HAS_SHORT_I;

            if (PI_HAS_LONG_INFO(pPi))
               flags |= PI_TSC_MASK_HAS_LONG_I;

            EpgTscQueue_Append(pQueue, pPi->start_time, pPi->stop_time, pPi->netwop_no, flags, blockIdx);

            pBlock = pBlock->pNextBlock;
         }
      }

      EpgDbLockDatabase(dbc, FALSE);
   }
   else
      fatal2("EpgTscQueue-AddAll: illegal NULL ptr params: pQueue=%lx dbc=%lx", (long)pQueue, (long)dbc);
}

