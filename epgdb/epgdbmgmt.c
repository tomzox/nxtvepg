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
 *    sorted by start time or block number respectively. This module
 *    contains only functions that modify the database; queries are
 *    implemented in the epgdbif.c module.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgdbmgmt.c,v 1.27 2001/02/03 21:02:51 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbacq.h"
#include "epgui/pilistbox.h"
#include "epgui/statswin.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/epgdbmgmt.h"


// internal shortcuts
typedef       EPGDB_CONTEXT *PDBC;
typedef const EPGDB_CONTEXT *CPDBC;

static void EpgDbCheckDefectPiBlocknos( PDBC dbc );
static void EpgDbRemoveAllDefectPi( PDBC dbc, uchar version );
static bool EpgDbAddDefectPi( EPGDB_CONTEXT * dbc, EPGDB_BLOCK *pBlock );

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
//
void EpgDbDestroy( PDBC dbc )
{
   EPGDB_BLOCK *pNext, *pWalk;
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

      // free all types of generic blocks
      for (type=0; type < BLOCK_TYPE_GENERIC_COUNT; type++)
      {
         pWalk = dbc->pFirstGenericBlock[type];
         while (pWalk != NULL)
         {
            pNext = pWalk->pNextBlock;
            xfree(pWalk);
            pWalk = pNext;
         }
         dbc->pFirstGenericBlock[type] = NULL;
      }

      // free AI
      if (dbc->pAiBlock != NULL)
      {
         xfree(dbc->pAiBlock);
         dbc->pAiBlock = NULL;
      }

      // free sub-context if it's a merged database
      if (dbc->pMergeContext != NULL)
      {
         EpgDbMergeDestroyContext(dbc->pMergeContext);
         dbc->pMergeContext = NULL;
      }

      // free the database context
      xfree(dbc);
   }
   else
      debug0("EpgDbDestroy: cannot destroy locked db");
}

// ---------------------------------------------------------------------------
// Remove expired PI blocks from the database
//  - should be called exery minute, or SearchFirst will not work,
//    i.e. return expired PI blocks
//
bool EpgDbExpire( PDBC dbc )
{
   EPGDB_BLOCK *pPrev, *pBlock, *pNext;
   time_t now;
   bool expired = FALSE;

   if ( dbc->lockLevel == 0 )
   {
      now = time(NULL);
      pBlock = dbc->pFirstPi;
      // Schleife ueber alle NOW Bloecke
      while ( (pBlock != NULL) && (pBlock->blk.pi.start_time < now) )
      {
         if (pBlock->blk.pi.stop_time <= now)
         {  // Sendung abgelaufen -> aus allen 3 Ketten aushaengen

            dprintf4("EXPIRED pi ptr=%lx: netwop=%d, blockno=%d start=%ld\n", (ulong)pBlock, pBlock->blk.pi.netwop_no, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);
            expired = TRUE;

            // GUI benachrichtigen
            PiListBox_DbRemoved(dbc, &pBlock->blk.pi);

            // Aus Verkettung nach Startzeit aushaengen
            pPrev = pBlock->pPrevBlock;
            pNext = pBlock->pNextBlock;
            if (pPrev != NULL)
               pPrev->pNextBlock = pNext;
            else
               dbc->pFirstPi = pNext;

            if (pNext != NULL)
               pNext->pPrevBlock = pPrev;
            else
               dbc->pLastPi = pPrev;

            // Aus Verkettung nach (Startzeit und) NetwopNo aushaengen
            // es kann keine Bloecke desselben Netwops vor dem zu loeschenden
            // geben, da nach Startzeit sortiert und Ueberlappungen verhindert,
            // fuer alle n: startzeit[n] >= stopzeit[n-1]
            assert(dbc->pFirstNetwopPi[pBlock->blk.pi.netwop_no] == pBlock);
            dbc->pFirstNetwopPi[pBlock->blk.pi.netwop_no] = pBlock->pNextNetwopBlock;
            if (pBlock->pNextNetwopBlock != NULL)
               pBlock->pNextNetwopBlock->pPrevNetwopBlock = NULL;

            // obsoleten Block in separate Liste obsoleter & defekter PI einfuegen
            if (EpgDbAddDefectPi(dbc, pBlock) == FALSE)
               xfree(pBlock);
            pBlock = pNext;
         }
         else
         {  // Sendung noch nicht abgelaufen -> naechste pruefen
            pBlock = pBlock->pNextBlock;
         }
      }

      // Pruefen ob Block-/SegmentVerkettung noch intakt
      assert(EpgDbCheckChains(dbc));
      PiListBox_UpdateNowItems(dbc);
      PiListBox_DbRecount(dbc);
   }
   else
      debug0("DB-SetDateTime: DB is locked");

   return expired;
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
   {  // keine PI in der DB -> alle Pointer muessen invalid sein
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
      assert(pWalk->version == ((pWalk->stream == 0) ? dbc->pAiBlock->blk.ai.version : dbc->pAiBlock->blk.ai.version_swo));
      pWalk = pWalk->pNextBlock;
   }

   // check AI
   if (dbc->pAiBlock != NULL)
   {
      assert((dbc->pAiBlock->pNextBlock == NULL) && (dbc->pAiBlock->pPrevBlock == NULL));
   }

   // Generic Bloecke pruefen
   for (type=0; type < BLOCK_TYPE_GENERIC_COUNT; type++)
   {
      pWalk = dbc->pFirstGenericBlock[type];
      while (pWalk != NULL)
      {
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
         debug3("PARITY refuse block type=%d: error counts new=%d old=%d", pNewBlock->type, pNewBlock->parityErrCnt, pOldBlock->parityErrCnt);
         result = FALSE;
      }
      else
      { // count the number of times this block was received
         pNewBlock->acqRepCount = pOldBlock->acqRepCount + 1;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Entfernt alle PI-Bloecke obsoleter netwops aus der DB
//  - soll ein bestimmtes Netwop geloescht werden (z.B. auf Benutzerwunsch),
//    muss im Filter am selben Index TRUE eingetragen werden
//  - netwop-count als Parameter und nicht aus AI Block in DB, damit die
//    Fkt. benutzt werden kann bevor der neue AI in DB aufgenommen
//    (z.B. Aufruf aus AI Callback)
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
         // GUI benachrichtigen
         PiListBox_DbRemoved(dbc, &pWalk->blk.pi);

         pPrev = pWalk->pPrevBlock;
         pNext = pWalk->pNextBlock;

         // aus Startzeit-Verzeigerung aushaengen
         if (pPrev != NULL)
            pPrev->pNextBlock = pNext;
         else
            dbc->pFirstPi = pNext;

         if (pNext != NULL)
            pNext->pPrevBlock = pPrev;
         else
            dbc->pLastPi = pPrev;

         // aus Netwop-Verzeigerung aushaengen
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

   assert(EpgDbCheckChains(dbc));

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
// Best. Max.anzahl an Bloecken eines Generic-Type laut AI
//  - daraus leitet sich direkt die gueltige range ab: [0 .. count[
//  - Ausnahme 1: NI-Block-Numerierung startet bei #1
//                (NI Block #0 wird aber auch akzeptiert)
//  - Ausnahme 2: TI,LI-Block #0x8000 erlaubt - muss vom Aufrufer beachet werden!
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
            // Blocknumerierung von NI-Bloecken startet bei #1
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
            // LI,TI: nur Bloecke 0x0000 und 0x8000 akzeptiert
            // Blocknummer 0x0000 wird durch netwop_no ersetzt
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
// Loescht Generic Bloecke, die laut AI obsolet sind
//  - sollte immer aufgerufen werden, wenn sich AI-Version aendert
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

         // ab diesem Block alle folgenden loeschen, da aufsteigend nach Blockno sortiert
         // Ausnahme: Block 0x8000 bei LI und TI
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
      assert(EpgDbCheckChains(dbc));
   }
   else
      debug0("EpgDb-RemoveObsoleteGenericBlocks: no AI block");
}

// ---------------------------------------------------------------------------
// AI-Block hinzufuegen
//
static bool EpgDbAddAiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   EPGDB_BLOCK *pOldAiBlock;
   const AI_NETWOP *pOldNets, *pNewNets;
   uchar netwop;

   pOldAiBlock = dbc->pAiBlock;

   dbc->pAiBlock = pBlock;

   if (pOldAiBlock != NULL)
   {
      if ((pOldAiBlock->blk.ai.version != dbc->pAiBlock->blk.ai.version) ||
          (pOldAiBlock->blk.ai.version_swo != dbc->pAiBlock->blk.ai.version_swo) )
      {
         EpgDbRemoveAllDefectPi(dbc, dbc->pAiBlock->blk.ai.version);
         EpgDbFilterIncompatiblePi(dbc, &pOldAiBlock->blk.ai, &dbc->pAiBlock->blk.ai);
         EpgDbRemoveObsoleteGenericBlocks(dbc);

         EpgDbMergeAiUpdate(dbc, pBlock);
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

   assert(EpgDbCheckChains(dbc));

   return TRUE;
}

// ---------------------------------------------------------------------------
// Pruefe ob Blocknummer im gueltigen Bereich liegt
//  - Achtung: Blocknummer von TI,LI Bloecken wird modifiziert!
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
            // Blocknumerierung von NI-Bloecken startet bei #1, deshalb <=
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
            // LI,TI: nur Bloecke 0x0000 und 0x8000 akzeptiert
            // Blocknummer 0x0000 wird durch netwop_no ersetzt
            if ( (block_no == 0x0000) && (pBlock->blk.all.netwop_no < MAX_NETWOP_COUNT) )
            {  // max. ein LI/TI-Block per Netwop, BlockNo immer #0
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
      ifdebug3(!accept, "REFUSE generic type=%d blockno=%d (netwop=%d)\n", type, block_no, pBlock->blk.all.netwop_no);
   }
   else
   {
      SHOULD_NOT_BE_REACHED;
      accept = FALSE;
   }

   return accept;
}

// ---------------------------------------------------------------------------
// Generic-Block in chain einfuegen
//  - Bloecke sind aufsteigend sortiert nach Blocknummer
//
static bool EpgDbAddGenericBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   EPGDB_BLOCK *pWalk, *pPrev;
   uint  block_no;
   bool  result;

   if ( EpgDbGenericBlockNoValid(dbc, pBlock, pBlock->type) )
   {
      block_no = pBlock->blk.all.block_no;

      if (dbc->pFirstGenericBlock[pBlock->type] == NULL)
      {  // allererster Block dieses Typs
         dprintf3("ADD first GENERIC type=%d ptr=%lx: blockno=%d\n", pBlock->type, (ulong)pBlock, block_no);
         pBlock->pNextBlock = NULL;
         dbc->pFirstGenericBlock[pBlock->type] = pBlock;
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
         {  // Block schon vorhanden -> ersetzen
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
            }
            else
               xfree(pBlock);
         }
         else if (pPrev == NULL)
         {  // Block ganz am Anfang einfuegen
            assert(pWalk != NULL);  // oben abgefangen (min. 1 Block in DB)
            dprintf3("ADD GENERIC type=%d ptr=%lx: blockno=%d\n", pBlock->type, (ulong)pBlock, block_no);
            pBlock->pNextBlock = dbc->pFirstGenericBlock[pBlock->type];
            dbc->pFirstGenericBlock[pBlock->type] = pBlock;
         }
         else
         {  // Block in Mitte oder am Ende einfuegen
            dprintf3("ADD GENERIC type=%d ptr=%lx: blockno=%d\n", pBlock->type, (ulong)pBlock, block_no);
            pBlock->pNextBlock = pPrev->pNextBlock;
            pPrev->pNextBlock = pBlock;
         }
      }
      result = TRUE;

      assert(EpgDbCheckChains(dbc));
   }
   else
      result = FALSE;

   return result;
}

// ---------------------------------------------------------------------------
// PI-Block aus Verkettung aushaengen
//  - Zeiger auf den vorhergehenden Block in der Netwop-Verkettung muss
//    als Parameter uebergeben werden, da innerhalb des Netwops keine
//    Rueckwaertsverkettung vorhanden ist, d.h. nach dem Block muesse
//    sonst ab pFirstNetwopPi gesucht werden.
//  - der Speicher wird nicht freigegeben, dies muss vom Aufrufer
//    erledigt werden
//
void EpgDbPiRemove( PDBC dbc, EPGDB_BLOCK * pObsolete )
{
   EPGDB_BLOCK *pPrev, *pNext;

   // GUI benachrichtigen
   PiListBox_DbRemoved(dbc, &pObsolete->blk.pi);

   // aus Netwop-Verkettung aushaengen
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

   // aus Startzeit-Verkettung aushaengen
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

   // GUI benachrichtigen
   uiUpdated = PiListBox_DbPreUpdate(dbc, &pObsolete->blk.pi, &pBlock->blk.pi);

   // in Netwop-Verzeigerung einhaengen
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

   // in Startzeit-Verzeigerung einhaengen
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

   // GUI benachrichtigen
   if (uiUpdated == FALSE)
      PiListBox_DbPostUpdate(dbc, &pObsolete->blk.pi, &pBlock->blk.pi);

   // alten Block freigeben
   xfree(pObsolete);
}

// ---------------------------------------------------------------------------
// Link a new PI block into the database
//
void EpgDbLinkPi( PDBC dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK * pPrev, EPGDB_BLOCK * pNext )
{
   EPGDB_BLOCK *pWalk;
   uchar netwop = pBlock->blk.pi.netwop_no;

   // Einfuegepunkt muss zw. dem letzten und akt. gef. des netwops liegen
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
   // exakten Einfuegepunkt bestimmt: vor pWalk

   // in Netwop-Verzeigerung einhaengen
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

   // in Startzeit-Verzeigerung einhaengen
   if (pWalk != NULL)
   {
      pPrev = pWalk->pPrevBlock;
      pNext = pWalk;
   }
   else
   {  // als allerletztes Element (aller netwops) anhaengen
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

   // GUI benachrichtigen
   PiListBox_DbInserted(dbc, &pBlock->blk.pi);
}

// ---------------------------------------------------------------------------
// Check if the given block number lies in the valid range
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
         {  // case 1: no block sequence overflow -> simple comparison
            result = ( (block_no >= pNetwop->startNo) &&
                       (block_no <= pNetwop->stopNoSwo) );
         }
         else if (pNetwop->startNo == pNetwop->stopNoSwo + 1)
         {  // case 2: empty range
            result = FALSE;
         }
         else
         {  // case 3: block sequence overflow -> range of valid block numbers consists of
            //         two separate ranges at the top and bottom of the 16-bit number space
            result = ( (block_no >= pNetwop->startNo) ||
                       (block_no <= pNetwop->stopNoSwo) );
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
//    0xFF00-0xFFFF und 0x0000-0x100
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
// Grundprinzipien der Konfliktbereinigung:
// - akt. AI-Version hat Prioritaet
// - Blocknummer hat Prioritaet ueber Startzeit
// - geringere Blocknummern haben Prioritaet
// - fruehere Startzeit hat Prioritaet, d.h. bei Ueberlappung werden
//   die folgenden Bloecke geloescht
//
// Die Pruefungen fallen in zwei Klassen:
// - Nicht Einfuegen falls Ueberlappung mit Bloecken hoeherer Prioritaet
// - Sonst ueberlappende Bloecke vor und nach der Einfuegestelle loeschen
//   Loeschung erfolgt vor dem Einfuegen, damit DB immer konsistent bleibt
//
// Fehlermoeglichkeiten bzgl. Blockno (impliziert Startzeit):
// - Blockno dahinter ist kleiner -> Block nicht einfuegen
// - Blockno davor ist groesser -> Bloecke davor loeschen
// Fehlermoeglichkeiten bzgl. Stoppzeit:
// - Ueberlappung mit Block davor und BlockNo davor kleiner -> nicht einfuegen
// - Ueberlappung mit Block dahinter und blockno dahinter groesser) -> Block dahinter loeschen
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Pruefe ob PI-Block in Netwop-Sequenz eingefuegt werden kann
//   Fehlermoeglichkeiten bzgl. Blockno (impliziert Startzeit):
//   - Blockno dahinter ist kleiner -> Block nicht einfuegen
//   Fehlermoeglichkeiten bzgl. Stoppzeit:
//   - Ueberlappung mit Block davor und BlockNo davor kleiner -> nicht einfuegen
//
static bool EpgDbPiCheckBlockSequence( PDBC dbc, EPGDB_BLOCK *pPrev, EPGDB_BLOCK *pBlock, EPGDB_BLOCK *pNext )
{
   bool  result;

   // PI muessen in DB aufsteigend sortiert verkettet sein
   assert((pPrev == NULL) || (pPrev->blk.pi.start_time < pBlock->blk.pi.start_time));
   assert((pNext == NULL) || (pBlock->blk.pi.start_time <= pNext->blk.pi.start_time));

   if ( (pNext != NULL) &&
        (pBlock->version == pNext->version) &&
        EpgDbPiCmpBlockNoGt(dbc, pBlock->blk.pi.block_no, pNext->blk.pi.block_no, pBlock->blk.pi.netwop_no) )
   {  // BlockNo ist groesser als die des folgenden Blocks -> nicht einfuegen
      // (Grund: Startzeit ist kleiner oder gleich, deshalb Einfuegepunkt davor)
      dprintf2("+++++++ REFUSE: next blockno lower: %d > %d\n", pBlock->blk.pi.block_no, pNext->blk.pi.block_no);
      result = FALSE;
   }
   else
   if ( (pPrev != NULL) &&
        (pPrev->blk.pi.stop_time > pBlock->blk.pi.start_time) &&
        (pBlock->version == pPrev->version) &&
        EpgDbPiCmpBlockNoGt(dbc, pBlock->blk.pi.block_no, pPrev->blk.pi.block_no, pBlock->blk.pi.netwop_no) )
   {  // Startzeit ueberlappt mit vorherigem Block -> nicht einfuegen
      dprintf0("+++++++ REFUSE: invalid starttime: prev overlap\n");
      result = FALSE;
   }
   else
      result = TRUE;

   return result;
}

// ---------------------------------------------------------------------------
// Loesche PI mit denen der neue Block in Nr. oder Laufzeit ueberlappen wuerde
//   Fehlermoeglichkeiten bzgl. Blockno (impliziert Startzeit):
//   - Blockno danach ist gleich -> Block danach loeschen
//   - Blockno davor ist groesser oder gleich -> Bloecke davor loeschen
//   - Blockno dahinter ist kleiner -> Block nicht einfuegen
//   Fehlermoeglichkeiten bzgl. Stoppzeit:
//   - Ueberlappung mit Block dahinter (und blockno dahinter groesser) -> Block dahinter loeschen
//   - Ueberlappung mit Block davor und BlockNo davor kleiner -> nicht einfuegen
//
static void EpgDbPiResolveConflicts( PDBC dbc, EPGDB_BLOCK *pBlock, EPGDB_BLOCK **pPrev, EPGDB_BLOCK **pNext )
{
   EPGDB_BLOCK *pWalk;
   uchar netwop = pBlock->blk.pi.netwop_no;

   pWalk = *pPrev;
   while ( (pWalk != NULL) &&
           ( (pWalk->blk.pi.stop_time > pBlock->blk.pi.start_time) ||
             !EpgDbPiCmpBlockNoGt(dbc, pBlock->blk.pi.block_no, pWalk->blk.pi.block_no, netwop) ))
   {  // Blocknummer ist kleiner oder gleich dem vorherigem Block -> vorherigen loeschen
      dprintf6("+++++++ DELETE: ptr=%lx prev=%lx blockno=%d >= %d or start=%ld > %ld\n", (ulong)pBlock, (ulong)pWalk, pWalk->blk.pi.block_no, pBlock->blk.pi.block_no, pWalk->blk.pi.start_time, pBlock->blk.pi.start_time);
      *pPrev = pWalk->pPrevNetwopBlock;
      EpgDbPiRemove(dbc, pWalk);
      // Block in defekt-Liste umhaengen oder freigeben
      if (EpgDbAddDefectPi(dbc, pWalk) == FALSE)
         xfree(pWalk);
      pWalk = *pPrev;
   }

   // Stoppzeit ueberlappt mit nachfolgendem Block od. BlockNo groesser -> folgende Bloecke loeschen
   // Schleife ab pBlock: alle mit start < stopp loeschen
   pWalk = *pNext;
   while( (pWalk != NULL) &&
          ( (pBlock->blk.pi.stop_time > pWalk->blk.pi.start_time) ||
            !EpgDbPiCmpBlockNoGt(dbc, pWalk->blk.pi.block_no, pBlock->blk.pi.block_no, netwop) ))
   {
      dprintf6("+++++++ DELETE: ptr=%lx next=%lx overlapped: blockno=%d<=%d, start=%ld < stop %ld\n", (ulong)pBlock, (ulong)pWalk, pWalk->blk.pi.block_no, pBlock->blk.pi.block_no, pWalk->blk.pi.start_time, pBlock->blk.pi.stop_time);
      *pNext = pWalk->pNextNetwopBlock;
      EpgDbPiRemove(dbc, pWalk);
      // Block in defekt-Liste umhaengen oder freigeben
      if (EpgDbAddDefectPi(dbc, pWalk) == FALSE)
         xfree(pWalk);
      pWalk = *pNext;
   }
}

// ----------------------------------------------------------------------------
// PI-Block in PI-Chain einfuegen
//  - 4-fach Verkettung:
//    +  vor- und rueckwaerts nach Startzeit ueber alle PI Bloecke
//    +  vor- und rueckwaerts nach Startzeit per Network
//
static bool EpgDbInsertPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   EPGDB_BLOCK *pWalk, *pPrev;
   uchar netwop;
   bool result;

   netwop = pBlock->blk.pi.netwop_no;
   dprintf4("ADD PI ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pBlock, netwop, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);

   // Suche nach Einfuegestelle in sortierter Liste des entspr. Netwops
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
   {  // derselbe PI-Block (alle Ordnungskriterien unveraendert) -> ersetzen

      if ( EpgDbCompareParityErrorCount(pWalk, pBlock) )
      {
         EpgDbReplacePi(dbc, pWalk, pBlock);
         // offer the block to the merged db
         EpgDbMergeInsertPi(dbc, pBlock);
      }
      else
         xfree(pBlock);

      result = TRUE;
   }
   else if (EpgDbPiCheckBlockSequence(dbc, pPrev, pBlock, pWalk))
   {  // BlockNo und Startzeit OK

      // Ueberlappungen beseitigen
      EpgDbPiResolveConflicts(dbc, pBlock, &pPrev, &pWalk);
      assert((pPrev == NULL) || (pPrev->pNextNetwopBlock == pWalk));
      assert((pWalk == NULL) || (pWalk->pPrevNetwopBlock == pPrev));

      EpgDbLinkPi(dbc, pBlock, pPrev, pWalk);

      // offer the block to the merged db
      EpgDbMergeInsertPi(dbc, pBlock);

      result = TRUE;
   }
   else
   {  // Block nicht einfuegen
      result = FALSE;
   }

   // falls Bloecke entfernt, scrollbar-pos im GUI neu bestimmen
   PiListBox_DbRecount(dbc);

   return result;
}

// ---------------------------------------------------------------------------
// Add a PI block to the database
// - if the block is in the valid netwop and blockno ranges, but cannot be
//   added because it's expired or overlapping, add it to a separate list
//   of obsolete PI blocks (so that we know when we have captured 100%)
// - since there are two separate lists with PI, after each addition it
//   must be checked that the block is not in the other list too (older
//   version, or after a transmission error)
//   Doing this is actually a disadvantage for the user, because we are
//   removing correct information upon reception of a defect block, however
//   it's needed to maintain consistancy.
// - XXX TODO try to repair PI with start=stop time: set duration to
//   one minute if start time of following block allows it
//
static bool EpgDbAddPiBlock( PDBC dbc, EPGDB_BLOCK *pBlock )
{
   bool result;

   if ( EpgDbPiBlockNoValid(dbc, pBlock->blk.pi.block_no, pBlock->blk.pi.netwop_no) )
   {
      if ( (pBlock->blk.pi.start_time != pBlock->blk.pi.stop_time) &&
           (pBlock->blk.pi.stop_time > time(NULL)) )
      {  // running time has not yet expired
         result = EpgDbInsertPiBlock(dbc, pBlock);
      }
      else
      {  // expired or invalid duration
         dprintf5("EXPIRED pi ptr=%lx: netwop=%d, blockno=%d start=%ld stop=%ld\n", (ulong)pBlock, pBlock->blk.pi.netwop_no, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time, pBlock->blk.pi.stop_time);
         result = FALSE;
      }

      // ensure that the same netwop/block is not in the other list
      if (result == FALSE)
      {  // block could not be added to the PI database -> add to list of obsolete PI
         result = EpgDbAddDefectPi(dbc, pBlock);
         if (result)
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
      result = FALSE;
   }

   assert(EpgDbCheckChains(dbc));

   return result;
}

// ---------------------------------------------------------------------------
// Add or replace a block in the database
//
bool EpgDbAddBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
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
               SHOULD_NOT_BE_REACHED;
               result = FALSE;
               break;
            case BLOCK_TYPE_AI:
               pBlock->version = pBlock->blk.ai.version;
               result = EpgDbAddAiBlock(dbc, pBlock);
               if (result)
               {
                  dbc->lastAiUpdate = time(NULL);
                  StatsWin_NewAi(dbc);
               }
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
               if (result)
                  StatsWin_NewPi(dbc, &pBlock->blk.pi, pBlock->stream);
               break;
            default:
               SHOULD_NOT_BE_REACHED;
               break;
         }

         if (result)
            dbc->modified = TRUE;
      }
      else
         debug0("EpgDb-AddBlock: db locked");
   }
   else
      debug0("EpgDb-AddBlock: illegal NULL ptr param");

   return result;
}

