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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgdbif.c,v 1.13 2000/06/26 18:21:37 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <time.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgstream.h"


// internal shortcut
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
         debug0("EpgDb-LockDatabase: db already unlocked");
   }
   else
   {
      if (dbc->lockLevel < 255)
      {
         dbc->lockLevel += 1;
      }
      else
      {
         SHOULD_NOT_BE_REACHED;
         debug0("EpgDb-LockDatabase: max lock level exceeded");
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
      cni = AI_GET_CNI(&dbc->pAiBlock->blk.ai);
   }
   else
      cni = 0;  // NULL ptr is permitted

   return cni;
}

// ---------------------------------------------------------------------------
// Get a pointer to the BI block in the database
//
const BI_BLOCK * EpgDbGetBi( CPDBC dbc )
{
   if ( EpgDbIsLocked(dbc) )
   {
      if (dbc->pBiBlock != NULL)
      {
         return &dbc->pBiBlock->blk.bi;
      }
   }
   else
      debug0("EpgDb-GetBi: DB not locked");

   return NULL;
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
      debug0("EpgDb-GetAi: DB not locked");

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
               // gefunden -> Rueckgabe der Adresse
               return pBlock;
            }
            pBlock = pBlock->pNextBlock;
         }
         // nicht gefunden
      }
      else
         debug1("EpgDb-Search-GenericBlock: illegal type %d", type);
   }
   else
      debug0("EpgDb-Search-GenericBlock: DB not locked");

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
      debug1("EpgDb-GetLi: WARNING: unsupported block_no %d requested", block_no);
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
      debug1("EpgDb-GetTi: WARNING: unsupported block_no %d requested", block_no);
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
      debug0("EpgDb-GetFirstObsoletePi: DB not locked");

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
         debug0("EpgDb-GetNextObsoletePi: illegal NULL ptr param");
   }
   else
      debug0("EpgDb-GetNextObsoletePi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for a PI block in the obsolete list that overlaps the given time window
//
const PI_BLOCK * EpgDbSearchObsoletePi( CPDBC dbc, uchar netwop_no, ulong start_time, ulong stop_time )
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
      debug0("EpgDb-SearchObsoletePi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Get the address of the specified PI block in the database
// - no search filters are used
//
const PI_BLOCK * EpgDbGetPi( CPDBC dbc, uint block_no, uchar netwop_no )
{
   return EpgDbSearchPi(dbc, NULL, block_no, netwop_no);
}

// ---------------------------------------------------------------------------
// Search for a PI block in the database
// - if the filter context parameter is not NULL, only blocks that match
//   these settings are considered; see the epgdbfil module on how to
//   create a filter context
//
const PI_BLOCK * EpgDbSearchPi( CPDBC dbc, const FILTER_CONTEXT *fc, uint block_no, uchar netwop_no )
{
   EPGDB_BLOCK * pBlock;

   assert(netwop_no < MAX_NETWOP_COUNT);

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pFirstNetwopPi[netwop_no];

      while (pBlock != NULL)
      {
         if ( (pBlock->blk.pi.block_no == block_no) &&
              (pBlock->blk.pi.netwop_no == netwop_no) )
         {
            if ( (fc == NULL) || EpgDbFilterMatches(dbc, fc, &pBlock->blk.pi) )
            {
               return &pBlock->blk.pi;
            }
         }
         pBlock = pBlock->pNextNetwopBlock;
      }
   }
   else
      debug0("EpgDb-SearchPi: DB not locked");

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
   const EPGDB_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pFirstPi;

      while (pBlock != NULL)
      {
         if ( (fc == NULL) || EpgDbFilterMatches(dbc, fc, &pBlock->blk.pi) )
         {
            return &pBlock->blk.pi;
         }
         pBlock = pBlock->pNextBlock;
      }
   }
   else
      debug0("EpgDb-SearchFirstPi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for the last matching PI block in database
//
const PI_BLOCK * EpgDbSearchLastPi( CPDBC dbc, const FILTER_CONTEXT *fc )
{
   const EPGDB_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pLastPi;

      while (pBlock != NULL)
      {
         if ( (fc == NULL) || EpgDbFilterMatches(dbc, fc, &pBlock->blk.pi) )
         {
            return &pBlock->blk.pi;
         }
         pBlock = pBlock->pPrevBlock;
      }
   }
   else
      debug0("EpgDb-SearchLastPi: DB not locked");

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
   const EPGDB_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      if (pPiBlock != NULL)
      {
         pBlock = (const EPGDB_BLOCK *)((ulong)pPiBlock - BLK_UNION_OFF);
         pBlock = pBlock->pNextBlock;

         while (pBlock != NULL)
         {
            if ( (fc == NULL) || EpgDbFilterMatches(dbc, fc, &pBlock->blk.pi) )
            {
               return &pBlock->blk.pi;
            }
            pBlock = pBlock->pNextBlock;
         }
      }
      else
         debug0("EpgDb-SearchNextPi: illegal NULL ptr param");
   }
   else
      debug0("EpgDb-SearchNextPi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for the next matching PI block before a given block
// - same as SearchNext(), just reverse
//
const PI_BLOCK * EpgDbSearchPrevPi( CPDBC dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock )
{
   const EPGDB_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      if (pPiBlock != NULL)
      {
         pBlock = (const EPGDB_BLOCK *)((ulong)pPiBlock - BLK_UNION_OFF);
         pBlock = pBlock->pPrevBlock;

         while (pBlock != NULL)
         {
            if ( (fc == NULL) || EpgDbFilterMatches(dbc, fc, &pBlock->blk.pi) )
            {
               return &pBlock->blk.pi;
            }
            pBlock = pBlock->pPrevBlock;
         }
      }
      else
         debug0("EpgDb-SearchPrevPi: illegal NULL ptr param");
   }
   else
      debug0("EpgDb-SearchPrevPi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Get the index of a PI in his netwop's chain
// - The index of a netwop's first PI is 0, or 1, if it's start time lies in
//   the future. This is required by the spec of the Nextview prog-no filter.
//
uint EpgDbGetProgIdx( CPDBC dbc, uint iBlockNo, uchar netwop )
{
   ulong blockNo, startNo, firstBlockNo;
   uint  nowIdx;
   uint  result = 0xffff;

   if ( EpgDbIsLocked(dbc) )
   {
      if (dbc->pAiBlock != NULL)
      {
         if (netwop < dbc->pAiBlock->blk.ai.netwopCount)
         {
            if (dbc->pFirstNetwopPi[netwop] != NULL)
            {
               if (dbc->pFirstNetwopPi[netwop]->blk.pi.start_time <= time(NULL))
               {  // first block of that network is currently running -> that block is #0
                  nowIdx = 0;
               }
               else
               {  // 1. there is no current programme on this netwop OR
                  // 2. there is a block missing in the database
                  // We could try to differentiate these two cases, but there's
                  // really no advantage because we couldn't do any better guess
                  // than giving the first present block #1, i.e. NEXT
                  nowIdx = 1;
               }

               startNo = AI_GET_NETWOP_N(&dbc->pAiBlock->blk.ai, netwop)->startNo;

               firstBlockNo = dbc->pFirstNetwopPi[netwop]->blk.pi.block_no;
               if (firstBlockNo < startNo)
                  firstBlockNo += 0x10000;

               blockNo = iBlockNo;
               if (blockNo < startNo)
                  blockNo += 0x10000;

               if (blockNo >= firstBlockNo)
               {
                  result = blockNo - firstBlockNo + nowIdx;
               }
               else
                  debug4("EpgDb-GetProgIdx: block #%d,net#%d should already have expired; start=%ld,first=%ld", iBlockNo, netwop, startNo, firstBlockNo);
            }
            else
               debug1("EpgDb-GetProgIdx: no blocks in db for netwop %d", netwop);
         }
         else
            debug2("EpgDb-GetProgIdx: invalid netwop #%d of %d", netwop, dbc->pAiBlock->blk.ai.netwopCount);
      }
      else
         debug0("EpgDb-GetProgIdx: no AI in db");
   }
   else
      debug0("EpgDb-GetProgIdx: DB not locked");

   return result;
}

// ---------------------------------------------------------------------------
// Returns the stream number of a PI block
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
bool EpgDbGetStat( CPDBC dbc, EPGDB_BLOCK_COUNT * pCount )
{
   const EPGDB_BLOCK * pBlock;
   const AI_NETWOP *pNetwops;
   uchar version1, version2;
   uchar netwop;
   bool result;

   memset(pCount, 0, 2 * sizeof(EPGDB_BLOCK_COUNT));

   if (dbc->pAiBlock != NULL)
   {
      version1 = dbc->pAiBlock->blk.ai.version;
      version2 = dbc->pAiBlock->blk.ai.version_swo;

      pBlock = dbc->pFirstPi;
      while (pBlock != NULL)
      {
         if (pBlock->stream == NXTV_STREAM_NO_1)
         {
            pCount[0].allVersions += 1;
            if (pBlock->version == version1)
               pCount[0].curVersion += 1;
         }
         else
         {
            pCount[1].allVersions += 1;
            if (pBlock->version == version2)
               pCount[1].curVersion += 1;
         }

         pBlock = pBlock->pNextBlock;
      }

      // count number of defect blocks; these are always current version
      pBlock = dbc->pObsoletePi;
      while (pBlock != NULL)
      {
         if (pBlock->stream == NXTV_STREAM_NO_1)
            pCount[0].obsolete += 1;
         else
            pCount[1].obsolete += 1;
         pBlock = pBlock->pNextBlock;
      }

      // determine total block sum from AI block start- and stop numbers per netwop
      pNetwops = AI_GET_NETWOPS(&dbc->pAiBlock->blk.ai);

      for (netwop=0; netwop < dbc->pAiBlock->blk.ai.netwopCount; netwop++)
      {
         pCount[0].ai += EpgDbGetPiBlockCount(pNetwops[netwop].startNo, pNetwops[netwop].stopNo);
         pCount[1].ai += EpgDbGetPiBlockCount(pNetwops[netwop].startNo, pNetwops[netwop].stopNoSwo);
      }
      pCount[1].ai -= pCount[0].ai;

      dprintf5("EpgDb-GetStat: count1=%ld count2=%ld cur1=%ld cur2=%ld total=%ld\n", *pStream1, *pStream2, *pCur1, *pCur2, *pTotal);
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
ulong EpgDbGetPiBlockCount( uint startNo, uint stopNo )
{
   ulong result;

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
ulong EpgDbGetPiBlockIndex( uint startNo, uint blockNo )
{
   uint result;

   if (blockNo >= startNo)
   {
      result = blockNo - startNo;
   }
   else
   {  // blockNo < startNo
      result = (0x10000 + blockNo) - startNo;
   }

   return result;
}

