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
 *  $Id: epgdbmgmt.c,v 1.43 2002/02/16 11:35:00 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DEBUG_EPGDBMGMT_CONSISTANCY OFF
#define DPRINTF_OFF

#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgui/pilistbox.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbmgmt.h"
#include "epgctl/epgctxmerge.h"
#include "epgctl/epgacqsrv.h"


// internal shortcuts
typedef       EPGDB_CONTEXT *PDBC;
typedef const EPGDB_CONTEXT *CPDBC;

static void EpgDbCheckDefectPiBlocknos( PDBC dbc );
static void EpgDbRemoveAllDefectPi( PDBC dbc, uchar version );

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
   EPGDB_BLOCK *pOiBlock;
   uchar type;

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

      // save OI block #0
      if ( keepAiOi &&
           (dbc->pFirstGenericBlock[BLOCK_TYPE_OI] != NULL) &&
           (dbc->pFirstGenericBlock[BLOCK_TYPE_OI]->blk.oi.block_no == 0) )
         pOiBlock = dbc->pFirstGenericBlock[BLOCK_TYPE_OI];
      else
         pOiBlock = NULL;

      // free all types of generic blocks
      for (type=0; type < BLOCK_TYPE_GENERIC_COUNT; type++)
      {
         pWalk = dbc->pFirstGenericBlock[type];
         while (pWalk != NULL)
         {
            pNext = pWalk->pNextBlock;
            if (pWalk != pOiBlock)
               xfree(pWalk);
            pWalk = pNext;
         }
         dbc->pFirstGenericBlock[type] = NULL;
      }

      if (keepAiOi == FALSE)
      {
         // free AI
         if (dbc->pAiBlock != NULL)
         {
            xfree(dbc->pAiBlock);
            dbc->pAiBlock = NULL;
         }

         // free the database context
         xfree(dbc);
      }
      else
      {
         if (pOiBlock != NULL)
         {
            pOiBlock->pNextBlock = NULL;
            dbc->pFirstGenericBlock[BLOCK_TYPE_OI] = pOiBlock;
         }
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
   uint  blocks;
   uchar netwop, type;

   if (dbc->pFirstPi != NULL)
   {
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
            assert( dbc->merged ||
                    (pWalk->blk.pi.block_no == pPrev->blk.pi.block_no + 1) ||
                    EpgDbPiCmpBlockNoGt(dbc, pWalk->blk.pi.block_no, pPrev->blk.pi.block_no, netwop) );
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
      assert(pWalk->version == ((pWalk->stream == 0) ? dbc->pAiBlock->blk.ai.version : dbc->pAiBlock->blk.ai.version_swo));
      pWalk = pWalk->pNextBlock;
   }

   // check AI
   if (dbc->pAiBlock != NULL)
   {
      assert(dbc->pAiBlock->type == BLOCK_TYPE_AI);
      assert((dbc->pAiBlock->pNextBlock == NULL) && (dbc->pAiBlock->pPrevBlock == NULL));
   }

   // check generic block chains
   for (type=0; type < BLOCK_TYPE_GENERIC_COUNT; type++)
   {
      pWalk = dbc->pFirstGenericBlock[type];
      while (pWalk != NULL)
      {
         assert(pWalk->type == type);
         pPrev = pWalk;
         pWalk = pWalk->pNextBlock;
         if (pWalk != NULL)
         {
            assert(pPrev->blk.all.block_no < pWalk->blk.all.block_no);
         }
      }
   }

   return TRUE;
}
#endif // DEBUG_GLOBAL_SWITCH == ON

// ---------------------------------------------------------------------------
// Check equivalent blocks for parity errors
// - called from insert functions if a block with the same block number and
//   network already is in the database
// - since the content may change without db version change (i.e. in PI blocks
//   long info might be added) additionally the low-level block size and
//   check-sum are compared (the high-level block size may change, since chars
//   with parity errors are replaced with white space, and multiple whitespace
//   gets compressed)
// - Note: there's no need to count hamming errors, since blocks with hamming
//   errors are generally refused (control data must be intact)
// - returns FALSE if the new block has more parity errors
// - as a side-effect, the acquisition repetition counter is incremented
//
static bool EpgDbCompareParityErrorCount( EPGDB_BLOCK * pOldBlock, EPGDB_BLOCK * pNewBlock )
{
   bool result = TRUE;

   if ( (pOldBlock->version    == pNewBlock->version) &&
        (pOldBlock->stream     == pNewBlock->stream) &&
        (pOldBlock->origBlkLen == pNewBlock->origBlkLen) &&
        (pOldBlock->origChkSum == pNewBlock->origChkSum) )
   {
      if (pNewBlock->parityErrCnt > pOldBlock->parityErrCnt)
      {  // refuse block if it has more errors than the already available copy
         dprintf3("PARITY refuse block type=%d: error counts new=%d old=%d\n", pNewBlock->type, pNewBlock->parityErrCnt, pOldBlock->parityErrCnt);

         pOldBlock->acqTimestamp = pNewBlock->acqTimestamp;
         pOldBlock->acqRepCount += 1;
         result = FALSE;
      }
      else
      {  // new block has equal or less errors -> accept it
         // count the number of times this block was received
         pNewBlock->acqRepCount  = pOldBlock->acqRepCount + 1;

         if (pOldBlock->parityErrCnt == 0)
         {  // keep the update timestamp of the old block, since the content is unchanged
            pNewBlock->updTimestamp = pOldBlock->updTimestamp;
         }
      }
   }
   return result;
}

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

   pWalk = dbc->pFirstPi;
   while (pWalk != NULL)
   {
      netwop = pWalk->blk.pi.netwop_no;
      if ( (netwop >= netwopCount) ||
           filter[netwop] ||
           (EpgDbPiBlockNoValid(dbc, pWalk->blk.pi.block_no, netwop) == FALSE) )
      {
         dprintf3("free obsolete PI ptr=%lx, netwop=%d >= %d or filtered\n", (ulong)pWalk, pWalk->blk.pi.netwop_no, netwopCount);
         // notify the GUI
         PiListBox_DbRemoved(dbc, &pWalk->blk.pi);

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

   PiListBox_DbRecount(dbc);
}

// ---------------------------------------------------------------------------
// After new AI version, remove incompatible networks from database
// - also remove blocks that are outside of blockno range in AI tables
// - invoked by callback for AI database insertion
//
static void EpgDbFilterIncompatiblePi( PDBC dbc, const AI_BLOCK *pOldAi, const AI_BLOCK *pNewAi )
{
   uchar filter[MAX_NETWOP_COUNT];
   const AI_NETWOP *pOldNets, *pNewNets;
   uchar netwop;
   bool found;

   memset(filter, 0, sizeof(filter));
   found = FALSE;

   pOldNets = AI_GET_NETWOPS(pOldAi);
   pNewNets = AI_GET_NETWOPS(pNewAi);
   for (netwop=0; netwop < pOldAi->netwopCount; netwop++)
   {
      if ( (netwop >= pNewAi->netwopCount) ||
           (pOldNets[netwop].cni != pNewNets[netwop].cni) )
      {
         // that index is no longer used by the new AI or
         // at this index there is a different netwop now -> remove the data
         filter[netwop] = TRUE;
         found = TRUE;
      }
   }

   EpgDbRemoveObsoleteNetwops(dbc, pNewAi->netwopCount, filter);
}

// ---------------------------------------------------------------------------
// Evaluate the max.no. of blocks of a given generic block type according to AI
// - the maximum number is directly related to the valid range: [0 .. count[
// - Exception #1: NI blocks start with #1
//   (NI block 0 is accepted too however)
// - Exception #2: TI, LI also allow 0x8000 - have to be dealt with by the caller!
//
uint EpgDbGetGenericMaxCount( CPDBC dbc, BLOCK_TYPE type )
{
   uint count;

   if (dbc->pAiBlock != NULL)
   {
      switch ( type )
      {
         case BLOCK_TYPE_NI:
            count = dbc->pAiBlock->blk.ai.niCount + dbc->pAiBlock->blk.ai.niCountSwo;
            // NI block numbers start with 1
            count += 1;
            break;
         case BLOCK_TYPE_OI:
            count = dbc->pAiBlock->blk.ai.oiCount + dbc->pAiBlock->blk.ai.oiCountSwo;
            break;
         case BLOCK_TYPE_MI:
            count = dbc->pAiBlock->blk.ai.miCount + dbc->pAiBlock->blk.ai.miCountSwo;
            break;
         case BLOCK_TYPE_LI:
         case BLOCK_TYPE_TI:
            // LI, TI: only block numbers 0x0000 and 0x8000 are acceptable
            // block number 0x0000 is replaced by the network number as simplification
            count = dbc->pAiBlock->blk.ai.netwopCount;
            break;
         default:
            debug1("EpgDb-GetGenericMaxCount: illegal type=%d", type);
            count = 0;
            break;
      }
   }
   else
   {
      debug0("EpgDb-GetGenericMaxCount: no AI block");
      count = 0xffff;
   }

   return count;
}

// ---------------------------------------------------------------------------
// Removed generic blocks which are obsolete according to AI
// - should be called after every AI update
//
static void EpgDbRemoveObsoleteGenericBlocks( PDBC dbc )
{
   BLOCK_TYPE type;
   EPGDB_BLOCK *pWalk, *pPrev, *pObsolete;
   uint  count;

   if (dbc->pAiBlock != NULL)
   {
      for (type=0; type < BLOCK_TYPE_GENERIC_COUNT; type++)
      {
         count = EpgDbGetGenericMaxCount(dbc, type);

         pWalk = dbc->pFirstGenericBlock[type];
         pPrev = NULL;
         while (pWalk != NULL)
         {
            if (pWalk->blk.all.block_no < count)
            {
               pPrev = pWalk;
               pWalk = pWalk->pNextBlock;
            }
            else
               break;
         }

         // starting with this block delete all following, since they are sorted by block number
         // exception: block number 0x8000 for LI and TI
         while (pWalk != NULL)
         {
            if ( (pWalk->blk.all.block_no != 0x8000) ||
                 ((type != BLOCK_TYPE_LI) && (type != BLOCK_TYPE_TI)) )
            {
               dprintf3("free obsolete generic type=%d, block_no=%d >= %d\n", type, pWalk->blk.all.block_no, count);
               pObsolete = pWalk;
               pWalk = pWalk->pNextBlock;
               if (pPrev != NULL)
                  pPrev->pNextBlock = pObsolete->pNextBlock;
               else
                  dbc->pFirstGenericBlock[type] = pObsolete->pNextBlock;

               xfree(pObsolete);
            }
            else
               break;
         }
      }
      assert(EpgDbMgmtCheckChains(dbc));
   }
   else
      debug0("EpgDb-RemoveObsoleteGenericBlocks: no AI block");
}

// ---------------------------------------------------------------------------
// Add a newly acquired AI block into the database
//
static bool EpgDbAddAiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   EPGDB_BLOCK *pOldAiBlock;
   const AI_NETWOP *pOldNets, *pNewNets;
   uchar netwop;
   bool result;

   pOldAiBlock = dbc->pAiBlock;
   result = TRUE;

   if ( (pOldAiBlock != NULL) &&
        (pOldAiBlock->origBlkLen == pBlock->origBlkLen) &&
        (pOldAiBlock->origChkSum == pBlock->origChkSum) &&
        (memcmp(&pOldAiBlock->blk.ai, &pBlock->blk.ai, sizeof(AI_BLOCK)) == 0) &&
        (memcmp(AI_GET_NETWOPS(&pOldAiBlock->blk.ai), AI_GET_NETWOPS(&pBlock->blk.ai),
                pBlock->blk.ai.netwopCount * sizeof(AI_NETWOP)) == 0) )
   {
      if (pBlock->parityErrCnt > pOldAiBlock->parityErrCnt)
      {
         // refuse block if it has more errors than the already available copy
         dprintf2("PARITY refuse AI block: error counts new=%d old=%d\n", pBlock->parityErrCnt, pOldAiBlock->parityErrCnt);

         pOldAiBlock->acqTimestamp = pBlock->acqTimestamp;
         pOldAiBlock->acqRepCount += 1;
         result = FALSE;
      }
      else
      {  // new block has equal or less errors -> accept it
         // count the number of times this block was received
         pBlock->acqRepCount  = pOldAiBlock->acqRepCount + 1;

         if (pOldAiBlock->parityErrCnt == 0)
         {  // keep the update timestamp of the old block, since the content is unchanged
            pBlock->updTimestamp = pOldAiBlock->updTimestamp;
         }
      }
   }

   if (result != FALSE)
   {
      dbc->pAiBlock = pBlock;

      if (pOldAiBlock != NULL)
      {
         if ((pOldAiBlock->blk.ai.version != dbc->pAiBlock->blk.ai.version) ||
             (pOldAiBlock->blk.ai.version_swo != dbc->pAiBlock->blk.ai.version_swo) )
         {
            EpgDbRemoveAllDefectPi(dbc, dbc->pAiBlock->blk.ai.version);
            EpgDbFilterIncompatiblePi(dbc, &pOldAiBlock->blk.ai, &dbc->pAiBlock->blk.ai);
            EpgDbRemoveObsoleteGenericBlocks(dbc);

            EpgContextMergeAiUpdate(dbc);
         }
         else
         {  // same version, but block start numbers might still have changed
            // (this should happen max. once per cycle, i.e. every 10-20 minutes)
            pOldNets = AI_GET_NETWOPS(&pOldAiBlock->blk.ai);
            pNewNets = AI_GET_NETWOPS(&pBlock->blk.ai);

            for (netwop=0; netwop < pBlock->blk.ai.netwopCount; netwop++)
            {
               if ( (pOldNets[netwop].startNo != pNewNets[netwop].startNo) ||
                    (pOldNets[netwop].stopNoSwo != pNewNets[netwop].stopNoSwo) )
               {  // at least one start/stop number changed -> check all PI
                  debug5("EpgDb-AddAiBlock: PI block range changed: net=%d %d-%d -> %d-%d", netwop, pOldNets[netwop].startNo, pOldNets[netwop].stopNoSwo, pNewNets[netwop].startNo, pNewNets[netwop].stopNoSwo);
                  EpgDbFilterIncompatiblePi(dbc, &pOldAiBlock->blk.ai, &dbc->pAiBlock->blk.ai);
                  EpgDbCheckDefectPiBlocknos(dbc);
                  break;
               }
            }
         }

         // free previous block
         xfree(pOldAiBlock);
      }

      assert(EpgDbMgmtCheckChains(dbc));
   }

   return result;
}

// ---------------------------------------------------------------------------
// Check if a given block number is in the valid range
// - Note: block numbers of TI and LI blocks are modified in the db!
//
static bool EpgDbGenericBlockNoValid( PDBC dbc, EPGDB_BLOCK * pBlock, BLOCK_TYPE type )
{
   uint  block_no;
   bool  accept;

   if (dbc->pAiBlock != NULL)
   {
      block_no = pBlock->blk.all.block_no;
      switch ( type )
      {
         case BLOCK_TYPE_NI:
            // block numbers of NI blocks start with 1, hence <=
            accept = (block_no <= (dbc->pAiBlock->blk.ai.niCount + dbc->pAiBlock->blk.ai.niCountSwo));
            break;

         case BLOCK_TYPE_OI:
            accept = (block_no < (dbc->pAiBlock->blk.ai.oiCount + dbc->pAiBlock->blk.ai.oiCountSwo));
            break;

         case BLOCK_TYPE_MI:
            accept = (block_no < (dbc->pAiBlock->blk.ai.miCount + dbc->pAiBlock->blk.ai.miCountSwo));
            break;

         case BLOCK_TYPE_LI:
         case BLOCK_TYPE_TI:
            // LI,TI: only block numbers 0x0000 and 0x8000 are acceptable
            // block number 0x0000 is replaced by network number
            if ( (block_no == 0x0000) && (pBlock->blk.all.netwop_no < MAX_NETWOP_COUNT) )
            {  // at max one LI/TI block per network, with block number 0
               GENERIC_BLK *pBlk = (GENERIC_BLK *) &pBlock->blk.all;  // remove const from pointer
               pBlk->block_no = block_no = pBlock->blk.all.netwop_no;
               accept = TRUE;
            }
            else if ( (block_no == 0x8000) && (pBlock->blk.all.netwop_no == dbc->pAiBlock->blk.ai.thisNetwop) )
            {  // This-Channel TI/LI-Block
               accept = TRUE;
            }
            else
            {  // unused blockno according to ETSI 707 (or invalid netwop)
               accept = FALSE;
            }
            break;

         default:
            accept = FALSE;;
            break;
      }
      ifdebug3(!accept, "REFUSE generic type=%d blockno=%d (netwop=%d)", type, block_no, pBlock->blk.all.netwop_no);
   }
   else
   {
      SHOULD_NOT_BE_REACHED;
      accept = FALSE;
   }

   return accept;
}

// ---------------------------------------------------------------------------
// Add a newly acquired block of one of the generic types into the database
// - blocks will be sorted by increasing block number
//
static bool EpgDbAddGenericBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   EPGDB_BLOCK *pWalk, *pPrev;
   uint  block_no;
   bool  added = FALSE;

   if ( EpgDbGenericBlockNoValid(dbc, pBlock, pBlock->type) )
   {
      block_no = pBlock->blk.all.block_no;

      if (dbc->pFirstGenericBlock[pBlock->type] == NULL)
      {  // very first block of this type in the db
         dprintf3("ADD first GENERIC type=%d ptr=%lx: blockno=%d\n", pBlock->type, (ulong)pBlock, block_no);
         pBlock->pNextBlock = NULL;
         dbc->pFirstGenericBlock[pBlock->type] = pBlock;
         added = TRUE;
      }
      else
      {
         pWalk = dbc->pFirstGenericBlock[pBlock->type];
         pPrev = NULL;

         while ( (pWalk != NULL) && (pWalk->blk.all.block_no < block_no) )
         {
            pPrev = pWalk;
            pWalk = pWalk->pNextBlock;
         }

         if ( (pWalk != NULL) && (pWalk->blk.all.block_no == block_no) )
         {  // block already in the db -> replace
            dprintf3("REPLACE GENERIC type=%d ptr=%lx: blockno=%d\n", pBlock->type, (ulong)pBlock, block_no);

            if ( EpgDbCompareParityErrorCount(pWalk, pBlock) )
            {
               pBlock->pNextBlock = pWalk->pNextBlock;
               if (pPrev != NULL)
                  pPrev->pNextBlock = pBlock;
               else
                  dbc->pFirstGenericBlock[pBlock->type] = pBlock;

               // free replaced block
               xfree(pWalk);
               added = TRUE;
            }
         }
         else if (pPrev == NULL)
         {  // insert the block at the start
            assert(pWalk != NULL);  // there must be at least one other block
            dprintf3("ADD GENERIC type=%d ptr=%lx: blockno=%d\n", pBlock->type, (ulong)pBlock, block_no);
            pBlock->pNextBlock = dbc->pFirstGenericBlock[pBlock->type];
            dbc->pFirstGenericBlock[pBlock->type] = pBlock;
            added = TRUE;
         }
         else
         {  // insert or append
            dprintf3("ADD GENERIC type=%d ptr=%lx: blockno=%d\n", pBlock->type, (ulong)pBlock, block_no);
            pBlock->pNextBlock = pPrev->pNextBlock;
            pPrev->pNextBlock = pBlock;
            added = TRUE;
         }
      }

      assert(EpgDbMgmtCheckChains(dbc));
   }

   return added;
}

// ---------------------------------------------------------------------------
// Remove a PI block from its start time and network pointer chains
// - the block's memory is not freed - this has to be done by the caller!
//
void EpgDbPiRemove( PDBC dbc, EPGDB_BLOCK * pObsolete )
{
   EPGDB_BLOCK *pPrev, *pNext;

   // notify the GUI
   PiListBox_DbRemoved(dbc, &pObsolete->blk.pi);

   // remove from network pointer chain
   pNext = pObsolete->pNextNetwopBlock;
   pPrev = pObsolete->pPrevNetwopBlock;
   if (pPrev != NULL)
   {
      assert(pPrev->pNextNetwopBlock == pObsolete);
      pPrev->pNextNetwopBlock = pNext;
   }
   else
      dbc->pFirstNetwopPi[pObsolete->blk.pi.netwop_no] = pNext;

   if (pNext != NULL)
   {
      assert(pNext->pPrevNetwopBlock == pObsolete);
      pNext->pPrevNetwopBlock = pPrev;
   }

   // remove from start time pointer chain
   pPrev = pObsolete->pPrevBlock;
   pNext = pObsolete->pNextBlock;
   if (pPrev != NULL)
   {
      assert(pPrev->pNextBlock == pObsolete);
      pPrev->pNextBlock = pNext;
   }
   else
      dbc->pFirstPi = pNext;

   if (pNext != NULL)
   {
      assert(pNext->pPrevBlock == pObsolete);
      pNext->pPrevBlock = pPrev;
   }
   else
      dbc->pLastPi = pPrev;
}

// ---------------------------------------------------------------------------
// Replace a PI block in the db with another one
//
void EpgDbReplacePi( PDBC dbc, EPGDB_BLOCK * pObsolete, EPGDB_BLOCK * pBlock )
{
   EPGDB_BLOCK *pPrev, *pNext;
   bool uiUpdated;

   // notify the GUI
   uiUpdated = PiListBox_DbPreUpdate(dbc, &pObsolete->blk.pi, &pBlock->blk.pi);

   // insert into the network pointer chain
   pPrev = pObsolete->pPrevNetwopBlock;
   pNext = pObsolete->pNextNetwopBlock;
   pBlock->pNextNetwopBlock = pNext;
   pBlock->pPrevNetwopBlock = pPrev;
   if (pPrev != NULL)
      pPrev->pNextNetwopBlock = pBlock;
   else
      dbc->pFirstNetwopPi[pBlock->blk.pi.netwop_no] = pBlock;
   if (pNext != NULL)
      pNext->pPrevNetwopBlock = pBlock;

   // insert into the start time pointer chain
   pPrev = pObsolete->pPrevBlock;
   pNext = pObsolete->pNextBlock;
   dprintf4("replace pi        ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pObsolete, pObsolete->blk.pi.netwop_no, pObsolete->blk.pi.block_no, pObsolete->blk.pi.start_time);
   if (pPrev != NULL) dprintf4("replace pi after  ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pPrev, pPrev->blk.pi.netwop_no, pPrev->blk.pi.block_no, pPrev->blk.pi.start_time);
   if (pNext != NULL) dprintf4("replace pi before ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pNext, pNext->blk.pi.netwop_no, pNext->blk.pi.block_no, pNext->blk.pi.start_time);
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
   if (uiUpdated == FALSE)
      PiListBox_DbPostUpdate(dbc, &pObsolete->blk.pi, &pBlock->blk.pi);

   // remove the obsolete block
   xfree(pObsolete);
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
   if (pPrev != NULL) dprintf4("add pi after  ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pPrev, pPrev->blk.pi.netwop_no, pPrev->blk.pi.block_no, pPrev->blk.pi.start_time);
   if (pNext != NULL) dprintf4("add pi before ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pNext, pNext->blk.pi.netwop_no, pNext->blk.pi.block_no, pNext->blk.pi.start_time);
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
   PiListBox_DbInserted(dbc, &pBlock->blk.pi);
}

// ---------------------------------------------------------------------------
// Check if the given netwop and block number fall into the valid ranges
// - special case block numer start = stop + 1 defines an empty range,
//   i.e. all block numbers are invalid for that network
//
bool EpgDbPiBlockNoValid( CPDBC dbc, uint block_no, uchar netwop )
{
   const AI_NETWOP *pNetwop;
   bool result;

   if (dbc->pAiBlock != NULL)
   {
      if (netwop < dbc->pAiBlock->blk.ai.netwopCount)
      {
         pNetwop = AI_GET_NETWOP_N(&dbc->pAiBlock->blk.ai, netwop);

         if (pNetwop->startNo <= pNetwop->stopNoSwo)
         {
            if ((pNetwop->startNo == 0x0000) && (pNetwop->stopNoSwo == 0xffff))
            {  // case 1: empty range (startNo == stopNoSwo + 1 modulo 2^16)
               result = FALSE;
            }
            else
            {  // case 2: no block sequence overflow -> simple comparison
               result = ( (block_no >= pNetwop->startNo) &&
                          (block_no <= pNetwop->stopNoSwo) );
            }
         }
         else
         {
            if (pNetwop->startNo == pNetwop->stopNoSwo + 1)
            {  // case 3: empty range (as defined in ETS 300 707)
               result = FALSE;
            }
            else
            {  // case 4: block sequence overflow -> range of valid block numbers consists of
               //         two separate ranges at the top and bottom of the 16-bit number space
               result = ( (block_no >= pNetwop->startNo) ||
                          (block_no <= pNetwop->stopNoSwo) );
            }
         }
         DBGONLY( if (result == FALSE) )
            dprintf4("EpgDb-PiBlockNoValid: netwop=%d block_no not in range: %d <= %d <= %d\n", netwop, pNetwop->startNo, block_no, pNetwop->stopNoSwo);
      }
      else
      {
         debug2("EpgDb-PiBlockNoValid: invalid netwop=%d >= count %d", netwop, dbc->pAiBlock->blk.ai.netwopCount);
         result = FALSE;
      }
   }
   else
   {
      SHOULD_NOT_BE_REACHED;
      result = FALSE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Check if BlockNo no1 > no2, including possible overflow inbetween
//  - block numbers are 16-bit unsigned int, modulo arithmetic
//  - the valid range is defined in the AI block for every netwop
//  - this function assumes that both given numbers are in the allowed range
//    see EpgDb-PiBlockNoValid()
//  - have to handle two cases, e.g. 0x0600-0x0700 or 0xFF00-0x0100
//    in the second case we have to deal with two separate ranges:
//    0xFF00-0xFFFF and 0x0000-0x100
//
bool EpgDbPiCmpBlockNoGt( CPDBC dbc, uint no1, uint no2, uchar netwop )
{
   const AI_NETWOP *pNetwop = AI_GET_NETWOP_N(&dbc->pAiBlock->blk.ai, netwop);
   bool result;

   if (pNetwop->startNo <= pNetwop->stopNoSwo)
   {
      result = (no1 > no2);
   }
   else
   {
      if (no1 >= pNetwop->startNo)
      {
         if (no2 >= pNetwop->startNo)
            result = (no1 > no2);
         else
            result = FALSE;
      }
      else
      {
         if (no2 >= pNetwop->startNo)
            result = TRUE;
         else
            result = (no1 > no2);
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Remove all obsolete defect PI blocks after AI version change
// - only PI blocks of the current version AI are kept in the list
//   -> after a version change the complete list is cleared
//
static void EpgDbRemoveAllDefectPi( PDBC dbc, uchar version )
{
   EPGDB_BLOCK *pWalk, *pNext;
   DBGONLY(int count = 0);

   pWalk = dbc->pObsoletePi;
   while (pWalk != NULL)
   {
      pNext = pWalk->pNextBlock;
      xfree(pWalk);
      pWalk = pNext;

      DBGONLY(count++);
   }
   dbc->pObsoletePi = NULL;

   dprintf1("EpgDb-RemoveAllDefectPi: freed %d defect PI blocks\n", count);
}

// ---------------------------------------------------------------------------
// Check validity of block numbers of all defective PI blocks after AI update
// - even if there is no version change of the AI block, the block start
//   numbers can change; we have to remove all blocks with block numbers
//   that are no longer in the valid range for its netwop
//
static void EpgDbCheckDefectPiBlocknos( PDBC dbc )
{
   EPGDB_BLOCK *pWalk, *pPrev, *pNext;

   pPrev = NULL;
   pWalk = dbc->pObsoletePi;
   while (pWalk != NULL)
   {
      if ( EpgDbPiBlockNoValid(dbc, pWalk->blk.pi.block_no, pWalk->blk.pi.netwop_no) == FALSE )
      {
         dprintf4("REMOVE OBSOLETE defect PI ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pWalk, pWalk->blk.pi.netwop_no, pWalk->blk.pi.block_no, pWalk->blk.pi.start_time);
         pNext = pWalk->pNextBlock;
         if (pPrev != NULL)
            pPrev->pNextBlock = pNext;
         else
            dbc->pObsoletePi = pNext;

         xfree(pWalk);
         pWalk = pNext;
      }
      else
      {
         pPrev = pWalk;
         pWalk = pWalk->pNextBlock;
      }
   }
}

// ---------------------------------------------------------------------------
// Remove the named block from list of obsolete PI, if present
// - called whenever a block is added to the list of non-obsoletes
//
static void EpgDbRemoveDefectPi( PDBC dbc, uint block_no, uchar netwop_no )
{
   EPGDB_BLOCK *pWalk, *pPrev, *pNext;

   pPrev = NULL;
   pWalk = dbc->pObsoletePi;
   while (pWalk != NULL)
   {
      if ( (pWalk->blk.pi.block_no == block_no) && (pWalk->blk.pi.netwop_no == netwop_no) )
      {
         dprintf6("REDUNDANT obsolete pi ptr=%lx: netwop=%d, blockno=%d start=%ld stop=%ld version=%d\n", (ulong)pWalk, pWalk->blk.pi.netwop_no, pWalk->blk.pi.block_no, pWalk->blk.pi.start_time, pWalk->blk.pi.stop_time, pWalk->version);
         pNext = pWalk->pNextBlock;
         if (pPrev != NULL)
            pPrev->pNextBlock = pNext;
         else
            dbc->pObsoletePi = pNext;

         xfree(pWalk);
         pWalk = pNext;
      }
      else
      {
         pPrev = pWalk;
         pWalk = pWalk->pNextBlock;
      }
   }
}

// ---------------------------------------------------------------------------
// Add a defective (e.g. overlapping) PI block to a separate list
// - called by acquisition when insert fails or when blocks expire
// - this list is kept mainly to determine when all PI blocks are acquired
// - the blocks are single-chained and NOT sorted
// - returns FALSE if block was not added and has to be free()'d by caller
//
static bool EpgDbAddDefectPi( PDBC dbc, EPGDB_BLOCK *pBlock )
{
   EPGDB_BLOCK *pWalk, *pLast;
   uchar netwop;
   uint block;
   bool result = FALSE;

   if (dbc->pAiBlock != NULL)
   {
      netwop = pBlock->blk.pi.netwop_no;
      block = pBlock->blk.pi.block_no;

      if ( (pBlock->version == dbc->pAiBlock->blk.ai.version) &&
           EpgDbPiBlockNoValid(dbc, pBlock->blk.pi.block_no, netwop) )
      {
         // search for the block anywhere in the list
         pLast = NULL;
         pWalk = dbc->pObsoletePi;
         while ( (pWalk != NULL) &&
                 ((pWalk->blk.pi.netwop_no != netwop) || (pWalk->blk.pi.block_no != block)) )
         {
            pLast = pWalk;
            pWalk = pWalk->pNextBlock;
         }

         if (pWalk == NULL)
         {  // not found -> insert at beginning
            dprintf4("ADD OBSOLETE PI ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pBlock, netwop, block, pBlock->blk.pi.start_time);
            pBlock->pNextBlock = dbc->pObsoletePi;
            dbc->pObsoletePi = pBlock;
         }
         else
         {  // found in list -> replace
            dprintf4("REPLACE OBSOLETE PI ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pBlock, netwop, block, pBlock->blk.pi.start_time);
            pBlock->pNextBlock = pWalk->pNextBlock;
            if (pLast != NULL)
               pLast->pNextBlock = pBlock;
            else
               dbc->pObsoletePi = pBlock;
            xfree(pWalk);
         }
         // reset unused pointers
         pBlock->pPrevBlock = NULL;
         pBlock->pNextNetwopBlock = NULL;
         pBlock->pPrevNetwopBlock = NULL;

         result = TRUE;
      }
      else
         dprintf4("refused OBSOLETE PI version ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pBlock, netwop, block, pBlock->blk.pi.start_time);
   }
   else
      debug0("EpgDb-AddDefectPi: AI block missing");

   return result;
}

// ----------------------------------------------------------------------------
// Principles of PI block conflict handling:
// - conflicts can occur because PI are sorted by two unrelated criteria
// - order of priorities:
//   1. blocks of previous AI versions are removed upon conflicts
//   2. block number order is more important than start times
//   3. blocks with earlier start time have priority over later ones
// - checks are performed in two steps:
//   1. don't insert blocks if they overlap existing ones
//   2. after insertion, remove overlapping blocks before and after the
//      point of insertion to ensure that the db remains consistant.
// - possible conflics in block numbering (inplies start time) after the
//   point of insertion has been searched for by starting time:
//   1. following block's number is smaller -> don't insert block
//   2. previous block's number is greater -> delete previous block(s)
// - possible conflicts in stop time:
//   1. overlapping running time with previous block and previous block no
//      is smaller -> don't insert
//   2. overlapping running time with following block and next blockno
//      is larger -> remove following block(s)
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Check if the given PI Block can be inserted into the network sequence here
// - possible conflicts respective to block numbers (imply start time)
//   1. latter block's number is smaller -> don't insert block
// - possible conflicts in stop time:
//   1. overlapping running time with previous block and previous block no
//      is smaller -> don't insert
//
static bool EpgDbPiCheckBlockSequence( PDBC dbc, EPGDB_BLOCK *pPrev, EPGDB_BLOCK *pBlock, EPGDB_BLOCK *pNext )
{
   bool  result;

   // PI are assumed to be sorted by increasing start time
   assert((pPrev == NULL) || (pPrev->blk.pi.start_time < pBlock->blk.pi.start_time));
   assert((pNext == NULL) || (pBlock->blk.pi.start_time <= pNext->blk.pi.start_time));

   if ( (pNext != NULL) &&
        (pBlock->version == pNext->version) &&
        EpgDbPiCmpBlockNoGt(dbc, pBlock->blk.pi.block_no, pNext->blk.pi.block_no, pBlock->blk.pi.netwop_no) )
   {  // block number is larger than that of the previous block -> don't insert
      // (reason: start time is smaller or equal, hence insertion point lies ahead of them)
      dprintf2("+++++++ REFUSE: next blockno lower: %d > %d\n", pBlock->blk.pi.block_no, pNext->blk.pi.block_no);
      result = FALSE;
   }
   else
   if ( (pPrev != NULL) &&
        (pPrev->blk.pi.stop_time > pBlock->blk.pi.start_time) &&
        (pBlock->version == pPrev->version) &&
        EpgDbPiCmpBlockNoGt(dbc, pBlock->blk.pi.block_no, pPrev->blk.pi.block_no, pBlock->blk.pi.netwop_no) )
   {  // start time overlaps with running time of previous block -> don't insert
      dprintf0("+++++++ REFUSE: invalid starttime: prev overlap\n");
      result = FALSE;
   }
   else
      result = TRUE;

   return result;
}

// ---------------------------------------------------------------------------
// Delete blocks which overlap a newly inserted PI in block number or time
// - possible conflicts respective to block numbers (imply start time)
//   1. following block's number is equal -> delete following block
//   2. previous block's number is greater or equal -> delete previous block(s)
// - possible conflicts in stop time:
//   1. overlapping running time with following block (and next blockno
//      is larger) -> remove following block(s)
//
static void EpgDbPiResolveConflicts( PDBC dbc, EPGDB_BLOCK *pBlock, EPGDB_BLOCK **pPrev, EPGDB_BLOCK **pNext )
{
   EPGDB_BLOCK *pWalk;
   uchar netwop = pBlock->blk.pi.netwop_no;

   pWalk = *pPrev;
   while ( (pWalk != NULL) &&
           ( (pWalk->blk.pi.stop_time > pBlock->blk.pi.start_time) ||
             !EpgDbPiCmpBlockNoGt(dbc, pBlock->blk.pi.block_no, pWalk->blk.pi.block_no, netwop) ))
   {  // block number is smaller or equal to the previous -> delete previous block
      dprintf6("+++++++ DELETE: ptr=%lx prev=%lx blockno=%d >= %d or start=%ld > %ld\n", (ulong)pBlock, (ulong)pWalk, pWalk->blk.pi.block_no, pBlock->blk.pi.block_no, pWalk->blk.pi.start_time, pBlock->blk.pi.start_time);
      *pPrev = pWalk->pPrevNetwopBlock;
      EpgDbPiRemove(dbc, pWalk);
      // add the block to the defective list or free it
      if (EpgDbAddDefectPi(dbc, pWalk) == FALSE)
         xfree(pWalk);
      pWalk = *pPrev;
   }

   // stop time falls into the running time of the following block
   // -> remove the following block(s)
   // -> loop starting with pBlock: while start time < stop time of inserted block
   pWalk = *pNext;
   while( (pWalk != NULL) &&
          ( (pBlock->blk.pi.stop_time > pWalk->blk.pi.start_time) ||
            !EpgDbPiCmpBlockNoGt(dbc, pWalk->blk.pi.block_no, pBlock->blk.pi.block_no, netwop) ))
   {
      dprintf6("+++++++ DELETE: ptr=%lx next=%lx overlapped: blockno=%d<=%d, start=%ld < stop %ld\n", (ulong)pBlock, (ulong)pWalk, pWalk->blk.pi.block_no, pBlock->blk.pi.block_no, pWalk->blk.pi.start_time, pBlock->blk.pi.stop_time);
      *pNext = pWalk->pNextNetwopBlock;
      EpgDbPiRemove(dbc, pWalk);
      // add the block to the defective list or free it
      if (EpgDbAddDefectPi(dbc, pWalk) == FALSE)
         xfree(pWalk);
      pWalk = *pNext;
   }
}

// ----------------------------------------------------------------------------
// - Each PI is linked with its neighbours by 4 pointers:
//   + to previous and next block in order of start time
//     note: if start time is equal, network index is compared
//   + for each network: to previous and next block of the same network
//     in or order of start time (there can be only one block with the same
//     start time for each network - this is enforced during insertion)
//

// ---------------------------------------------------------------------------
// Add a PI block to the database
// - returns FALSE if not added -> the caller must free the block then
// - if the block is in the valid netwop and blockno ranges, but cannot be
//   added because it's overlapping, add it to a separate list of defective
//   PI blocks (so that we know when we have captured 100%)
// - since there are two separate lists with PI, after each addition it
//   must be checked that the block is not in the other list too (older
//   version, or after a transmission error)
//   Doing this is actually a disadvantage for the user, because we are
//   removing correct information upon reception of a defect block, however
//   it's required to maintain consistancy.
// - XXX TODO try to repair PI with start=stop time: set duration to
//   one minute if start time of following block allows it
//
static bool EpgDbAddPiBlock( PDBC dbc, EPGDB_BLOCK *pBlock )
{
   EPGDB_BLOCK *pWalk, *pPrev;
   uchar netwop;
   bool added, defective;

   if ( EpgDbPiBlockNoValid(dbc, pBlock->blk.pi.block_no, pBlock->blk.pi.netwop_no) )
   {
      if (pBlock->blk.pi.start_time != pBlock->blk.pi.stop_time)
      {
         defective = FALSE;
         netwop = pBlock->blk.pi.netwop_no;
         dprintf4("ADD PI ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pBlock, netwop, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);

         // search inside network chain for insertion point
         pWalk = dbc->pFirstNetwopPi[netwop];
         pPrev = NULL;

         while ( (pWalk != NULL) &&
                 (pWalk->blk.pi.start_time < pBlock->blk.pi.start_time) )
         {
            pPrev = pWalk;
            pWalk = pWalk->pNextNetwopBlock;
         }

         if ( (pWalk != NULL) &&
              (pWalk->blk.pi.block_no == pBlock->blk.pi.block_no) &&
              (pWalk->blk.pi.start_time == pBlock->blk.pi.start_time) &&
              (pWalk->blk.pi.stop_time == pBlock->blk.pi.stop_time) )
         {  // found equivalent block (all sorting criteria identical)
            // -> simply replace, i.e. no checks for overlap etc. required

            if ( EpgDbCompareParityErrorCount(pWalk, pBlock) )
            {
               EpgDbReplacePi(dbc, pWalk, pBlock);
               // offer the block to the merged db
               EpgContextMergeInsertPi(dbc, pBlock);
               added = TRUE;
            }
            else
            {  // block is NOT defective, but still it's not added to the db
               added = FALSE;
            }
         }
         else if (EpgDbPiCheckBlockSequence(dbc, pPrev, pBlock, pWalk))
         {  // block number and start time don't conflict with blocks of higher priority

            // remove conflicting (i.e. overlapping) blocks of lesser priority
            EpgDbPiResolveConflicts(dbc, pBlock, &pPrev, &pWalk);
            assert((pPrev == NULL) || (pPrev->pNextNetwopBlock == pWalk));
            assert((pWalk == NULL) || (pWalk->pPrevNetwopBlock == pPrev));

            EpgDbLinkPi(dbc, pBlock, pPrev, pWalk);

            // offer the block to the merged db
            EpgContextMergeInsertPi(dbc, pBlock);

            added = TRUE;
         }
         else
         {  // do not insert the block
            defective = TRUE;
            added = FALSE;
         }

         // notify the GUI that insertion and implied removals are done
         // (the GUI may have to count PI to calculate scrollbar positions)
         PiListBox_DbRecount(dbc);
      }
      else
      {  // invalid duration
         dprintf5("EXPIRED pi ptr=%lx: netwop=%d, blockno=%d start=%ld stop=%ld\n", (ulong)pBlock, pBlock->blk.pi.netwop_no, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time, pBlock->blk.pi.stop_time);
         defective = TRUE;
         added = FALSE;
      }

      if (defective)
      {  // block conflicts with PI database -> add to list of defective PI
         assert(added == FALSE);

         added = EpgDbAddDefectPi(dbc, pBlock);
         if (added)
         {  // block is now in defect list -> remove from PI list, if present
            EPGDB_BLOCK *pWalk;
            uint block_no = pBlock->blk.pi.block_no;

            pWalk = dbc->pFirstNetwopPi[pBlock->blk.pi.netwop_no];
            while (pWalk != NULL)
            {
               if (pWalk->blk.pi.block_no == block_no)
               {
                  dprintf6("REDUNDANT non-obsolete pi ptr=%lx: netwop=%d, blockno=%d start=%ld stop=%ld version=%d\n", (ulong)pWalk, pWalk->blk.pi.netwop_no, pWalk->blk.pi.block_no, pWalk->blk.pi.start_time, pWalk->blk.pi.stop_time, pWalk->version);
                  EpgDbPiRemove(dbc, pWalk);
                  xfree(pWalk);
                  break;
               }
               pWalk = pWalk->pNextNetwopBlock;
            }
         }
      }
      else
      {  // added to normal db -> remove block from list of obsolete PI
         EpgDbRemoveDefectPi(dbc, pBlock->blk.pi.block_no, pBlock->blk.pi.netwop_no);
      }
   }
   else
   {  // invalid netwop or not in valid block number range -> refuse block
      dprintf5("INVALID pi ptr=%lx: netwop=%d, blockno=%d start=%ld stop=%ld\n", (ulong)pBlock, pBlock->blk.pi.netwop_no, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time, pBlock->blk.pi.stop_time);
      added = FALSE;
   }

   assert(EpgDbMgmtCheckChains(dbc));

   return added;
}

// ---------------------------------------------------------------------------
// Add or replace a block in the database
//
static bool EpgDbAddBlock( PDBC dbc, EPGDB_PI_TSC * tsc, EPGDB_BLOCK * pBlock )
{
   bool result = FALSE;

   if (pBlock != NULL)
   {
      if (dbc->lockLevel == 0)
      {
         // insert current AI version number into the block
         // (needed to resolve conflicts and to monitor acq progress)
         if (dbc->pAiBlock != NULL)
            pBlock->version = ((pBlock->stream == 0) ? dbc->pAiBlock->blk.ai.version : dbc->pAiBlock->blk.ai.version_swo);

         switch (pBlock->type)
         {
            case BLOCK_TYPE_BI:
               fatal0("EpgDb-AddBlock: refusing BI block");
               result = FALSE;
               break;

            case BLOCK_TYPE_AI:
               pBlock->version = pBlock->blk.ai.version;
               result = EpgDbAddAiBlock(dbc, pBlock);
               // further processing is independent of result
               dbc->modified = TRUE;
               break;

            case BLOCK_TYPE_NI:
            case BLOCK_TYPE_OI:
            case BLOCK_TYPE_MI:
            case BLOCK_TYPE_LI:
            case BLOCK_TYPE_TI:
               result = EpgDbAddGenericBlock(dbc, pBlock);
               break;

            case BLOCK_TYPE_PI:
               result = EpgDbAddPiBlock(dbc, pBlock);
               if (result && (tsc != NULL))
                  EpgTscQueue_AddPi(tsc, dbc, &pBlock->blk.pi, pBlock->stream);
               break;

            default:
               fatal1("EpgDb-AddBlock: illegal block type %d", pBlock->type);
               break;
         }

         if (result)
         {
             dbc->modified = TRUE;
             #ifdef USE_DAEMON
             EpgAcqServer_AddBlock(dbc, pBlock);
             #endif
         }
      }
      else
         fatal0("EpgDb-AddBlock: db locked");
   }
   else
      fatal0("EpgDb-AddBlock: illegal NULL ptr param");

   return result;
}

// ---------------------------------------------------------------------------
// Process the first block in the queue of the given type
// - this is used during acquisition startup while waiting for the first AI block
// - also used to pick OI block #0 during EPG scan
// - passing pointer to database context pointer, b/c the context pointer
//   may change inside the AI callback (e.g. change of provider)
//
void EpgDbProcessQueueByType( EPGDB_CONTEXT * const * pdbc, EPGDB_QUEUE * pQueue, BLOCK_TYPE type, const EPGDB_ADD_CB * pCb )
{
   EPGDB_BLOCK * pBlock;

   pBlock = EpgDbQueue_GetByType(pQueue, type);
   if (pBlock != NULL)
   {
      if (type == BLOCK_TYPE_BI)
      {
         dprintf1("EpgDbQueue-ProcessBlocks: Offer BI block to acq ctl (0x%lx)\n", (long)pBlock);
         pCb->pBiCallback(&pBlock->blk.bi);
         xfree(pBlock);
      }
      else if (type == BLOCK_TYPE_AI)
      {
         dprintf2("EpgDbQueue-ProcessBlocks: Offer AI block 0x%04X to acq ctl (0x%lx)\n", AI_GET_CNI(&pBlock->blk.ai), (long)pBlock);
         if ( (pCb->pAiCallback(&pBlock->blk.ai) == FALSE) || 
              (EpgDbAddBlock(*pdbc, NULL, pBlock) == FALSE) )
         {  // block was not accepted
            xfree(pBlock);
         }
      }
      else
      {
         if ( EpgDbAddBlock(*pdbc, NULL, pBlock) == FALSE )
         {  // block was not accepted
            xfree(pBlock);
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Process blocks in the queue
// - passing pointer to database context pointer, b/c the context pointer
//   may change inside the AI callback (e.g. change of provider)
//
void EpgDbProcessQueue( EPGDB_CONTEXT * const * pdbc, EPGDB_QUEUE * pQueue,
                        EPGDB_PI_TSC * tsc, const EPGDB_ADD_CB * pCb )
{
   EPGDB_BLOCK * pBlock;

   // note: the acq might get switched off by the callbacks inside the loop,
   // but we don't need to check this since then the buffer is cleared
   while ((pBlock = EpgDbQueue_Get(pQueue)) != NULL)
   {
      if (pBlock->type == BLOCK_TYPE_BI)
      {  // the Bi block never is added to the db, only evaluated by acq ctl
         pCb->pBiCallback(&pBlock->blk.bi);
         xfree(pBlock);
      }
      else if (pBlock->type == BLOCK_TYPE_AI)
      {
         if ( (pCb->pAiCallback(&pBlock->blk.ai) == FALSE) || 
              (EpgDbAddBlock(*pdbc, tsc, pBlock) == FALSE) )
         {  // block was not accepted
            xfree(pBlock);
         }
      }
      else
      {
         if ( EpgDbAddBlock(*pdbc, tsc, pBlock) == FALSE )
         {  // block was not accepted
            xfree(pBlock);
         }
      }
   }
}

