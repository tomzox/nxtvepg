/*
 *  Nextview EPG block database interface
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
 *    Provides access to the Nextview block database. All blocks
 *    are uniquely identified by either their block number alone
 *    or their block and network number. In case of PI you also
 *    can iterate over the database using a GetFirst/GetLast,
 *    GetNext/GetPrev scheme, possibly limited to those PI blocks
 *    matching criteria given in a filter context. For a list of
 *    blocks and their content, see ETS 300 707 (Nextview spec.)
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgdbif.c,v 1.41 2002/03/29 17:27:08 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <time.h>
#include <string.h>
#include <math.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgstream.h"


// internal shortcuts
typedef       EPGDB_CONTEXT *PDBC;
typedef const EPGDB_CONTEXT *CPDBC;

// ---------------------------------------------------------------------------
// Lock database for modification by acquisition
//
void EpgDbLockDatabase( PDBC dbc, uchar enable )
{
   if ( (enable & 1) == FALSE )
   {
      if (dbc->lockLevel > 0)
      {
         dbc->lockLevel -= 1;
      }
      else
         fatal0("EpgDb-LockDatabase: db already unlocked");
   }
   else
   {
      if (dbc->lockLevel < 255)
      {
         dbc->lockLevel += 1;
      }
      else
      {
         fatal0("EpgDb-LockDatabase: max lock level exceeded");
      }
   }
}

// ---------------------------------------------------------------------------
// Query if database is locked
//
bool EpgDbIsLocked( CPDBC dbc )
{
   return (dbc->lockLevel > 0);
}

// ---------------------------------------------------------------------------
// Query the CNI of a context
//
uint EpgDbContextGetCni( CPDBC dbc )
{
   uint cni;

   if ((dbc != NULL) && (dbc->pAiBlock != NULL))
   {
      if (dbc->merged == FALSE)
         cni = AI_GET_CNI(&dbc->pAiBlock->blk.ai);
      else
         cni = 0x00FF;
   }
   else
      cni = 0;  // NULL ptr is permitted

   return cni;
}

// ---------------------------------------------------------------------------
// Query if the db is merged
//
bool EpgDbContextIsMerged( CPDBC dbc )
{
   if (dbc != NULL)
      return dbc->merged;
   else
      return FALSE;
}

// ---------------------------------------------------------------------------
// Query time of last AI update (by acquisition)
// - in network acq mode, this reflects the time the block was captured by the
//   daemon (the time is arrived at the client is irrelevant)
//
time_t EpgDbGetAiUpdateTime( CPDBC dbc )
{
   time_t  aiUpdateTime;

   if ((dbc != NULL) && (dbc->pAiBlock != NULL))
   {
      aiUpdateTime = dbc->pAiBlock->acqTimestamp;
   }
   else
      aiUpdateTime = 0;

   return aiUpdateTime;
}

// ---------------------------------------------------------------------------
// Set time of last AI update
// - only to be used by network acquisition client, because there AI blocks
//   are not transmitted if unchanged
//
void EpgDbSetAiUpdateTime( const EPGDB_CONTEXT * dbc, time_t acqTimestamp )
{
   if (dbc != NULL)
   {
      if (dbc->pAiBlock != NULL)
      {
         dbc->pAiBlock->acqTimestamp = acqTimestamp;
      }
      else
         debug0("EpgDb-SetAiUpdateTime: no AI block in db");
   }
   else
      fatal0("EpgDb-SetAiUpdateTime: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Get a pointer to the AI block in the database
//
const AI_BLOCK * EpgDbGetAi( CPDBC dbc )
{
   if ( EpgDbIsLocked(dbc) )
   {
      if (dbc->pAiBlock != NULL)
      {
         return &dbc->pAiBlock->blk.ai;
      }
   }
   else
      fatal0("EpgDb-GetAi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Look up a generic-type block by number
// - blocks are sorted by block number, i.e. the search can be aborted
//   when a block with higher number as the requested is found
//
static const EPGDB_BLOCK * EpgDbSearchGenericBlock( CPDBC dbc, BLOCK_TYPE type, uint block_no )
{
   const EPGDB_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      if (type < BLOCK_TYPE_GENERIC_COUNT)
      {
         pBlock = dbc->pFirstGenericBlock[type];
         while ( (pBlock != NULL ) &&
                 (pBlock->blk.all.block_no <= block_no) )
         {
            if (pBlock->blk.all.block_no == block_no)
            {
               // found -> return the address
               return pBlock;
            }
            pBlock = pBlock->pNextBlock;
         }
         // not found
      }
      else
         fatal1("EpgDb-Search-GenericBlock: illegal type %d", type);
   }
   else
      fatal0("EpgDb-Search-GenericBlock: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Look up a OI block by number
//
const OI_BLOCK * EpgDbGetOi( CPDBC dbc, uint block_no )
{
   const EPGDB_BLOCK * pBlock;

   pBlock = EpgDbSearchGenericBlock(dbc, BLOCK_TYPE_OI, block_no);

   if (pBlock != NULL)
      return &pBlock->blk.oi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Look up a NI block by number
//
const NI_BLOCK * EpgDbGetNi( CPDBC dbc, uint block_no )
{
   const EPGDB_BLOCK * pBlock;

   pBlock = EpgDbSearchGenericBlock(dbc, BLOCK_TYPE_NI, block_no);

   if (pBlock != NULL)
      return &pBlock->blk.ni;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Look up a MI block by number
//
const MI_BLOCK * EpgDbGetMi( CPDBC dbc, uint block_no )
{
   const EPGDB_BLOCK * pBlock;

   pBlock = EpgDbSearchGenericBlock(dbc, BLOCK_TYPE_MI, block_no);

   if (pBlock != NULL)
      return &pBlock->blk.mi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Look up a LI block by number
//  - see also description of TI
//
const LI_BLOCK * EpgDbGetLi( CPDBC dbc, uint block_no, uchar netwop )
{
   const EPGDB_BLOCK * pBlock;

   if (block_no == 0x0000)
   {  // netwop is used for block sorting
      pBlock = EpgDbSearchGenericBlock(dbc, BLOCK_TYPE_LI, netwop);
   }
   else if (block_no == 0x8000)
   {  // This-Channel LI block
      pBlock = EpgDbSearchGenericBlock(dbc, BLOCK_TYPE_LI, 0x8000);
   }
   else
   {
      fatal1("EpgDb-GetLi: WARNING: unsupported block_no %d requested", block_no);
      pBlock = NULL;
   }

   if (pBlock != NULL)
      return &pBlock->blk.li;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Look up a TI block by number
// - only block #0 of each netwop and block 0x8000 of this_netwop
//   are kept in the DB in this version; block_no is abused internally
//   to reflect the netwop_no - this allows to use the generic search
//   algorithm.
//
const TI_BLOCK * EpgDbGetTi( CPDBC dbc, uint block_no, uchar netwop )
{
   const EPGDB_BLOCK * pBlock;

   if (block_no == 0x0000)
   {  // netwop is used for block sorting
      pBlock = EpgDbSearchGenericBlock(dbc, BLOCK_TYPE_TI, netwop);
   }
   else if (block_no == 0x8000)
   {  // This-Channel TI block
      pBlock = EpgDbSearchGenericBlock(dbc, BLOCK_TYPE_TI, 0x8000);
   }
   else
   {
      fatal1("EpgDb-GetTi: WARNING: unsupported block_no %d requested", block_no);
      pBlock = NULL;
   }

   if (pBlock != NULL)
      return &pBlock->blk.ti;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Get the first block from the obsolete PI list
//
const PI_BLOCK * EpgDbGetFirstObsoletePi( CPDBC dbc )
{
   if ( EpgDbIsLocked(dbc) )
   {
      if (dbc->pObsoletePi != NULL)
      {
         return &dbc->pObsoletePi->blk.pi;
      }
   }
   else
      fatal0("EpgDb-GetFirstObsoletePi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Get the next obsolete PI block after a given block
//
const PI_BLOCK * EpgDbGetNextObsoletePi( CPDBC dbc, const PI_BLOCK * pPiBlock )
{
   const EPGDB_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      if (pPiBlock != NULL)
      {
         pBlock = (const EPGDB_BLOCK *)((ulong)pPiBlock - BLK_UNION_OFF);
         pBlock = pBlock->pNextBlock;

         if (pBlock != NULL)
         {
            return &pBlock->blk.pi;
         }
      }
      else
         fatal0("EpgDb-GetNextObsoletePi: illegal NULL ptr param");
   }
   else
      fatal0("EpgDb-GetNextObsoletePi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for a PI block in the obsolete list that overlaps the given time window
//
const PI_BLOCK * EpgDbSearchObsoletePi( CPDBC dbc, uchar netwop_no, time_t start_time, time_t stop_time )
{
   const EPGDB_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pObsoletePi;
      while (pBlock != NULL)
      {
         if ( (pBlock->blk.pi.netwop_no == netwop_no) &&
              (pBlock->blk.pi.start_time < stop_time) &&
              (pBlock->blk.pi.stop_time > start_time) ) 
         {
            return &pBlock->blk.pi;
         }
         pBlock = pBlock->pNextBlock;
      }
   }
   else
      fatal0("EpgDb-SearchObsoletePi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for a PI block in the database
// - if the filter context parameter is not NULL, only blocks that match
//   these settings are considered; see the epgdbfil module on how to
//   create a filter context
//
const PI_BLOCK * EpgDbSearchPi( CPDBC dbc, time_t start_time, uchar netwop_no )
{
   EPGDB_BLOCK * pBlock;

   assert(netwop_no < MAX_NETWOP_COUNT);

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pFirstNetwopPi[netwop_no];

      while (pBlock != NULL)
      {
         if (pBlock->blk.pi.start_time == start_time)
         {
            assert(pBlock->blk.pi.netwop_no == netwop_no);

            return &pBlock->blk.pi;
         }
         pBlock = pBlock->pNextNetwopBlock;
      }
   }
   else
      fatal0("EpgDb-SearchPi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for the first matching PI block in database
// - The first is the the block with the lowest start time.
//   The start time may lie in the past if the stop time is
//   still in the future
//
const PI_BLOCK * EpgDbSearchFirstPi( CPDBC dbc, const FILTER_CONTEXT *fc )
{
   const EPGDB_BLOCK * pBlock = NULL;

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pFirstPi;

      if (fc != NULL)
      {  // skip forward until a matching block is found
         while ( (pBlock != NULL) &&
                 (EpgDbFilterMatches(dbc, fc, &pBlock->blk.pi) == FALSE) )
         {
            pBlock = pBlock->pNextBlock;
         }
      }
   }
   else
      fatal0("EpgDb-SearchFirstPi: DB not locked");

   if (pBlock != NULL)
      return &pBlock->blk.pi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Search for the last matching PI block in database
//
const PI_BLOCK * EpgDbSearchLastPi( CPDBC dbc, const FILTER_CONTEXT *fc )
{
   const EPGDB_BLOCK * pBlock = NULL;

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pLastPi;

      if (fc != NULL)
      {
         while ( (pBlock != NULL) &&
                 (EpgDbFilterMatches(dbc, fc, &pBlock->blk.pi) == FALSE) )
         {
            pBlock = pBlock->pPrevBlock;
         }
      }
   }
   else
      fatal0("EpgDb-SearchLastPi: DB not locked");

   if (pBlock != NULL)
      return &pBlock->blk.pi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Search for the next matching PI block after a given block
// - blocks are sorted by starting time, then netwop_no, i.e. this
//   function returns the first block which matches the filter criteria
//   and has a higher start time (or the same start time and a higher
//   netwop index) as the given block.
//
const PI_BLOCK * EpgDbSearchNextPi( CPDBC dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock )
{
   const EPGDB_BLOCK * pBlock = NULL;

   if ( EpgDbIsLocked(dbc) )
   {
      if (pPiBlock != NULL)
      {
         pBlock = (const EPGDB_BLOCK *)((ulong)pPiBlock - BLK_UNION_OFF);
         pBlock = pBlock->pNextBlock;

         if (fc != NULL)
         {  // skip forward until a matching block is found
            while ( (pBlock != NULL) &&
                    (EpgDbFilterMatches(dbc, fc, &pBlock->blk.pi) == FALSE) )
            {
               pBlock = pBlock->pNextBlock;
            }
         }
      }
      else
         fatal0("EpgDb-SearchNextPi: illegal NULL ptr param");
   }
   else
      fatal0("EpgDb-SearchNextPi: DB not locked");

   if (pBlock != NULL)
      return &pBlock->blk.pi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Search for the next matching PI block before a given block
// - same as SearchNext(), just reverse
//
const PI_BLOCK * EpgDbSearchPrevPi( CPDBC dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock )
{
   const EPGDB_BLOCK * pBlock = NULL;

   if ( EpgDbIsLocked(dbc) )
   {
      if (pPiBlock != NULL)
      {
         pBlock = (const EPGDB_BLOCK *)((ulong)pPiBlock - BLK_UNION_OFF);
         pBlock = pBlock->pPrevBlock;

         if (fc != NULL)
         {
            while ( (pBlock != NULL) &&
                    (EpgDbFilterMatches(dbc, fc, &pBlock->blk.pi) == FALSE) )
            {
               pBlock = pBlock->pPrevBlock;
            }
         }
      }
      else
         fatal0("EpgDb-SearchPrevPi: illegal NULL ptr param");
   }
   else
      fatal0("EpgDb-SearchPrevPi: DB not locked");

   if (pBlock != NULL)
      return &pBlock->blk.pi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Search for the PI block with the given PIL
// - also search through the obsolete blocks, because the planned program
//   stop time might already have elapsed when the program is still running
//
const PI_BLOCK * EpgDbSearchPiByPil( CPDBC dbc, uchar netwop_no, uint pil )
{
   const EPGDB_BLOCK * pBlock;

   assert(netwop_no < MAX_NETWOP_COUNT);

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pFirstNetwopPi[netwop_no];
      while (pBlock != NULL)
      {
         if (pBlock->blk.pi.pil == pil)
         {
            assert(pBlock->blk.pi.netwop_no == netwop_no);
            return &pBlock->blk.pi;
         }
         pBlock = pBlock->pNextNetwopBlock;
      }
   }
   else
      fatal0("EpgDb-SearchPiByPil: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Get the index of a PI in his netwop's chain
// - The index of a netwop's first PI is 0, or 1, if it's start time lies in
//   the future. This is required by the spec of the Nextview prog-no filter.
//
uint EpgDbGetProgIdx( CPDBC dbc, const PI_BLOCK * pPiBlock )
{
   EPGDB_BLOCK * pBlock;
   ulong blockNo, startNo, firstBlockNo;
   uint  nowIdx;
   uchar netwop;
   time_t now = time(NULL);
   uint  result = 0xffff;

   if (dbc->pAiBlock != NULL)
   {
      netwop = pPiBlock->netwop_no;
      if (netwop < dbc->pAiBlock->blk.ai.netwopCount)
      {
         pBlock = dbc->pFirstNetwopPi[netwop];
         // scan forward for the first unexpired block on that network
         while ((pBlock != NULL) && (pBlock->blk.pi.stop_time <= now))
         {
            pBlock = pBlock->pNextNetwopBlock;
         }

         if (pBlock != NULL)
         {
            if (pBlock->blk.pi.start_time <= now)
            {  // found a currently running block -> that block is #0
               nowIdx = 0;
            }
            else
            {  // 1. there is no current programme on this netwop OR
               // 2. there is a block missing in the database
               // We could try to distinguish these two cases, but there's
               // really no advantage because we couldn't find a better guess
               // than giving the first present block #1, i.e. NEXT
               nowIdx = 1;
            }

            if (dbc->merged == FALSE)
            {
               startNo = AI_GET_NETWOP_N(&dbc->pAiBlock->blk.ai, netwop)->startNo;

               firstBlockNo = pBlock->blk.pi.block_no;
               if (firstBlockNo < startNo)
                  firstBlockNo += 0x10000;

               blockNo = pPiBlock->block_no;
               if (blockNo < startNo)
                  blockNo += 0x10000;

               if (blockNo >= firstBlockNo)
               {
                  result = blockNo - firstBlockNo + nowIdx;
               }
               else
                  debug4("EpgDb-GetProgIdx: block #%d,net#%d should already have expired; start=%ld,first=%ld", pPiBlock->block_no, netwop, startNo, firstBlockNo);
            }
            else
            {  // merged database: no block numbers available
               if (pPiBlock == &pBlock->blk.pi)
                  result = nowIdx;
               else if ( (nowIdx == 0) &&
                         (pBlock->blk.pi.stop_time == pPiBlock->start_time) )
                  result = 1;
               // else: neither Now nor Next -> return invalid result code
            }
         }
         else
            debug1("EpgDb-GetProgIdx: no unexpired blocks in db for netwop %d", netwop);
      }
      else
         fatal2("EpgDb-GetProgIdx: invalid netwop #%d of %d", netwop, dbc->pAiBlock->blk.ai.netwopCount);
   }
   else
      fatal0("EpgDb-GetProgIdx: no AI in db");

   return result;
}

// ---------------------------------------------------------------------------
// Returns the stream a block was received in
//
uchar EpgDbGetStream( const void * pUnion )
{
   const EPGDB_BLOCK *pBlock;
   uchar stream;

   pBlock = (const EPGDB_BLOCK *)((ulong)pUnion - BLK_UNION_OFF);
   stream = pBlock->stream;

   return stream;
}

// ---------------------------------------------------------------------------
// Returns the stream a PI block belongs to according to the current AI
// - since the AI allocation may have changed since receiving the block,
//   the result is not neccessarily the same as the stream the block was
//   received in
//
uchar EpgDbGetStreamByBlockNo( CPDBC dbc, const EPGDB_BLOCK * pBlock )
{
   const AI_NETWOP *pNetwop;
   uint  block_no;
   uchar netwop;
   uchar stream;

   if (dbc->pAiBlock != NULL)
   {
      netwop   = pBlock->blk.pi.netwop_no;
      block_no = pBlock->blk.pi.block_no;

      if (netwop < dbc->pAiBlock->blk.ai.netwopCount)
      {
         pNetwop = AI_GET_NETWOP_N(&dbc->pAiBlock->blk.ai, netwop);

         if (pNetwop->startNo <= pNetwop->stopNo)
         {
            if ((pNetwop->startNo == 0x0000) && (pNetwop->stopNo == 0xffff))
            {  // case 1: stream 1 empty (startNo == stopNo + 1 modulo 2^16)
               stream = NXTV_STREAM_NO_2;
            }
            else
            {  // case 2: no block sequence overflow -> simple comparison
               stream = ((block_no <= pNetwop->stopNo) ? NXTV_STREAM_NO_1 : NXTV_STREAM_NO_2);
            }
         }
         else
         {
            if (pNetwop->startNo == pNetwop->stopNo + 1)
            {  // case 3: stream 1 empty (as defined in ETS 300 707)
               stream = NXTV_STREAM_NO_2;
            }
            else
            {  // case 4: block sequence overflow -> range of stream 1 block numbers consists of
               //         two separate ranges at the top and bottom of the 16-bit number space
               stream = (( (block_no >= pNetwop->startNo) ||
                           (block_no <= pNetwop->stopNo) ) ? NXTV_STREAM_NO_1 : NXTV_STREAM_NO_2);
            }
         }
      }
      else
      {
         debug2("EpgDb-GetStreamByBlockNo: invalid netwop=%d >= count %d", netwop, dbc->pAiBlock->blk.ai.netwopCount);
         stream = NXTV_STREAM_NO_1;
      }
   }
   else
   {
      fatal0("EpgDb-GetStreamByBlockNo: no AI block in db");
      stream = NXTV_STREAM_NO_1;
   }

   return stream;
}

// ---------------------------------------------------------------------------
// Returns the version number of a PI block
//
uchar EpgDbGetVersion( const void * pUnion )
{
   const EPGDB_BLOCK *pBlock;
   uchar version;

   pBlock = (const EPGDB_BLOCK *)((ulong)pUnion - BLK_UNION_OFF);
   version = pBlock->version;

   return version;
}

// ---------------------------------------------------------------------------
// Count the number of PI blocks in the database, separately for stream 1 & 2
// - db needs not be locked since this is an atomic operation
//   and no pointers into the db are returned
// - returns counts separately for both streams - pCount points to array[2] !!
//
bool EpgDbGetStat( CPDBC dbc, EPGDB_BLOCK_COUNT * pCount, time_t * acqMinTime, uint maxNowRepCount )
{
   const EPGDB_BLOCK * pBlock;
   const AI_NETWOP *pNetwops;
   uint blockCount[2][MAX_NETWOP_COUNT];
   register uint cur_stream;
   uint32_t  count1, count2;
   uchar  ai_version[2];
   double avgPercentage1, avgPercentage2, variance1, variance2;
   uint   acqRepSum[2];
   time_t now;
   uchar  netwop;
   bool   result;

   memset(pCount, 0, 2 * sizeof(EPGDB_BLOCK_COUNT));
   memset(blockCount, 0, sizeof(blockCount));
   acqRepSum[0] = acqRepSum[1] = 0;
   now = time(NULL);

   maxNowRepCount = ((maxNowRepCount > 3) ? (maxNowRepCount - 3) : 1);

   if (dbc->pAiBlock != NULL)
   {
      pNetwops = AI_GET_NETWOPS(&dbc->pAiBlock->blk.ai);
      ai_version[0] = dbc->pAiBlock->blk.ai.version;
      ai_version[1] = dbc->pAiBlock->blk.ai.version_swo;

      pBlock = dbc->pFirstPi;
      while (pBlock != NULL)
      {
         // Use the block number to determine which stream currently contains the block.
         // Cannot use the stream number from the block header because that's the stream
         // in which the block was received; However here we need the up-to-date stream
         // or the block counts will be inconsistent with the AI counts!
         cur_stream = EpgDbGetStreamByBlockNo(dbc, pBlock);
         if (pBlock->blk.pi.stop_time > now)
         {
            pCount[cur_stream].allVersions += 1;
            // note about the stream comparison:
            // the borderline between stream 1 and 2 may change without a version change.
            // since with a move from stream 2 to 1 more information may have been added
            // we then consider the block out of date.
            if ( (pBlock->version == ai_version[cur_stream]) &&
                 (pBlock->stream == cur_stream) )
            {
               pCount[cur_stream].curVersion += 1;
            }
         }
         else
            pCount[cur_stream].expired += 1;

         // note about "<=" in stream comparison: there's a bug in the TARA Nextview encoder,
         // where blocks are assigned to stream 2 in AI, but transmitted in stream 1 anyways
         // (e.g. when stream 1 is empty because there are no programmes in the time range
         // that's usually covered by stream 1)
         if ( (pBlock->acqTimestamp >= acqMinTime[cur_stream]) &&
              (pBlock->version == ai_version[cur_stream]) &&
              (pBlock->stream <= cur_stream) )
         {
            pCount[cur_stream].sinceAcq += 1;
            blockCount[cur_stream][pBlock->blk.pi.netwop_no] += 1;
         }

         if (cur_stream == 1)
         {  // stream 2
            acqRepSum[cur_stream] += pBlock->acqRepCount;
         }
         else
         {  // stream 1 needs special handling to split off rep counts from Now & Next
            // PI in now & next sub-stream count exactly once; other PI may count more often
            if (pBlock->acqRepCount >= maxNowRepCount)
               acqRepSum[0] += 1;
            else
               acqRepSum[0] += pBlock->acqRepCount;
         }

         pBlock = pBlock->pNextBlock;
      }

      // count number of defective blocks
      // - no version comparison required, since these are discarded when AI version changes
      pBlock = dbc->pObsoletePi;
      while (pBlock != NULL)
      {
         cur_stream = EpgDbGetStreamByBlockNo(dbc, pBlock);

         pCount[cur_stream].defective += 1;
         if ( (pBlock->acqTimestamp >= acqMinTime[cur_stream]) &&
              (pBlock->stream <= cur_stream) )
         {
            pCount[cur_stream].sinceAcq += 1;
            blockCount[cur_stream][pBlock->blk.pi.netwop_no] += 1;
         }

         if (cur_stream == 1)
         {  // stream 2
            acqRepSum[cur_stream] += pBlock->acqRepCount;
         }
         else
         {  // stream 1 needs special handling to split off rep counts from Now & Next
            if (pBlock->acqRepCount >= maxNowRepCount)
               acqRepSum[0] += 1;
            else
               acqRepSum[0] += pBlock->acqRepCount;
         }

         pBlock = pBlock->pNextBlock;
      }

      // determine total block sum from AI block start- and stop numbers per netwop
      avgPercentage1 = avgPercentage2 = 0.0;

      for (netwop=0; netwop < dbc->pAiBlock->blk.ai.netwopCount; netwop++)
      {
         count1 = EpgDbGetPiBlockCount(pNetwops[netwop].startNo, pNetwops[netwop].stopNo);
         count2 = EpgDbGetPiBlockCount(pNetwops[netwop].startNo, pNetwops[netwop].stopNoSwo) - count1;

         if (count1 > 0)
         {
            pCount[0].ai += count1;
            blockCount[0][netwop] = (uint)(1000L * blockCount[0][netwop] / count1);
         }
         else
            blockCount[0][netwop] = 1000L;
         avgPercentage1 += blockCount[0][netwop];

         if (count2 > 0)
         {
            pCount[1].ai += count2;
            blockCount[1][netwop] = (uint)(1000L * blockCount[1][netwop] / count2);
         }
         else
            blockCount[1][netwop] = 1000L;
         avgPercentage2 += blockCount[1][netwop];
      }
      avgPercentage1 /= (double) dbc->pAiBlock->blk.ai.netwopCount;
      avgPercentage2 /= (double) dbc->pAiBlock->blk.ai.netwopCount;

      // determine variance of coverages per netwop
      variance1 = variance2 = 0.0;
      for (netwop=0; netwop < dbc->pAiBlock->blk.ai.netwopCount; netwop++)
      {
         variance1 += pow(fabs(blockCount[0][netwop] - avgPercentage1) / 1000L, 2.0);
         variance2 += pow(fabs(blockCount[1][netwop] - avgPercentage2) / 1000L, 2.0);
      }
      pCount[0].variance = sqrt(variance1);
      pCount[1].variance = sqrt(variance2);

      // calculate average acquisition repetition count in both streams
      if (pCount[0].ai > 0)
         pCount[0].avgAcqRepCount = (double)acqRepSum[0] / (double)pCount[0].ai;
      else
         pCount[0].avgAcqRepCount = 0.0;
      if (pCount[1].ai > 0)
         pCount[1].avgAcqRepCount = (double)acqRepSum[1] / (double)pCount[1].ai;
      else
         pCount[1].avgAcqRepCount = 0.0;

      result = TRUE;
   }
   else
   {  // empty database -> return 0 for all counts
      dprintf0("EpgDb-GetStat: no AI in db\n");
      result = FALSE;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Compute the distance between two PI block numbers
// - modulo 0x10000
// - if stop == start - 1  =>  distance == 0  (see ETS 300 707)
//
uint EpgDbGetPiBlockCount( uint startNo, uint stopNo )
{
   uint result;

   if (stopNo >= startNo)
   {
      if ((startNo == 0) && (stopNo == 0xffff))
         result = 0;
      else
         result = stopNo - startNo + 1;
   }
   else
   {  // stopNo < startNo
      if (stopNo + 1 == startNo)
         result = 0;
      else
         result = (0x10000 + stopNo) - startNo + 1;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get the index of a PI block relative to the start number given in AI
//
uint EpgDbGetPiBlockIndex( uint startNo, uint blockNo )
{
   uint result;

   if (blockNo >= startNo)
   {
      result = blockNo - startNo;
   }
   else
   {  // blockNo < startNo
      result = (blockNo + 0x10000) - startNo;
   }

   return result;
}
 
// ----------------------------------------------------------------------------
// Reset acquisition repetition counters on all PI blocks
// - used by the epgacqctl module, in the cycle state machine
//
void EpgDbResetAcqRepCounters( PDBC dbc )
{
   EPGDB_BLOCK * pBlock;

   if (dbc != NULL)
   {
      pBlock = dbc->pFirstPi;
      while (pBlock != NULL)
      {
         pBlock->acqRepCount = 0;

         pBlock = pBlock->pNextBlock;
      }
   }
   else
      fatal0("EpgDb-ResetAcqRepCounters: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Check the maximum repetition count for the first PI block of each netwop
//
uint EpgDbGetNowCycleMaxRepCounter( CPDBC dbc )
{
   uint maxNowPiRepCount;
   uchar netwop;
   time_t now;

   now = time(NULL);
   maxNowPiRepCount = 0;

   if (dbc != NULL)
   {
      if (dbc->pAiBlock != NULL)
      {
         for (netwop=0; netwop < dbc->pAiBlock->blk.ai.netwopCount; netwop++)
         {
            if ( (dbc->pFirstNetwopPi[netwop] != NULL) &&
                 (dbc->pFirstNetwopPi[netwop]->blk.pi.start_time < now) )
            {  // PI is currently running, i.e. it's a "NOW" block

               if (dbc->pFirstNetwopPi[netwop]->acqRepCount > maxNowPiRepCount)
               {  // found new maximum
                  maxNowPiRepCount = dbc->pFirstNetwopPi[netwop]->acqRepCount;
               }
            }
         }
         dprintf1("EpgDb-GetNowCycleMaxRepCounter: max rep count = %d\n", maxNowPiRepCount);
      }
   }
   else
      fatal0("EpgDb-GetNowCycleMaxRepCounter: illegal NULL ptr param");

   return maxNowPiRepCount;
}

