/*
 *  Nextview EPG database merging
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
 *    This module implements the 'lower' half of the merge functionality
 *    where the actual merging is done. It offers three services to the
 *    upper half in the epgctl directory: 1 - merge AI blocks of the source
 *    databases; 2 - merge all PI of all source databases; 3 - insert one
 *    PI from one of the source databases into an existing merged database.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgdbmerge.c,v 1.37 2020/06/21 07:33:18 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgui/epgmain.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"


// internal shortcut
typedef EPGDB_CONTEXT *PDBC;

#undef MIN
#undef MAX
#define MIN(A,B) (((A)<=(B)) ? (A) : (B))
#define MAX(A,B) (((A)>=(B)) ? (A) : (B))

// limit length of merged provider name (i.e. concatenation)
#define MAX_SERVICE_NAME_LEN  300

// ---------------------------------------------------------------------------
// Helper function for debug output
//
#ifndef DPRINTF_OFF
static const char * EpgDbMergePrintTime( const PI_BLOCK * pPi )
{
   static char buf[32];
   struct tm * pTm;
   size_t off;

   pTm = localtime(&pPi->start_time);
   off = strftime(buf, sizeof(buf), "%d.%m %H:%M", pTm);

   pTm = localtime(&pPi->stop_time);
   strftime(buf + off, sizeof(buf) - off, " - %H:%M", pTm);

   return buf;
}
#endif // DPRINTF_OFF

// ---------------------------------------------------------------------------
// Append a PI block to the database
//
static void EpgDbMergeAddPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK *pPrevBlock )
{
   uchar netwop;
   
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
static void EpgDbMergeLinkNetworkPi( PDBC dbc, EPGDB_BLOCK ** pFirstNetwopBlock )
{
   EPGDB_BLOCK *pPrevNetwopBlock[MAX_NETWOP_COUNT];
   EPGDB_BLOCK *pBlock;
   time_t minStartTime;
   uint minNetwop;
   uint netwop, netCount;

   // reset start links in DB context
   dbc->pFirstPi = NULL;
   dbc->pLastPi = NULL;
   memset(dbc->pFirstNetwopPi, 0, sizeof(dbc->pFirstNetwopPi));

   memset(pPrevNetwopBlock, 0, sizeof(pPrevNetwopBlock));
   netCount = dbc->pAiBlock->blk.ai.netwopCount;

   // combine blocks of separately merged networks into one database
   while (1)
   {
      minStartTime = 0;
      minNetwop = 0xff;

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

      if (minStartTime == 0)
         break;

      // pop block off temporary per-network list
      pBlock = pFirstNetwopBlock[minNetwop];
      pFirstNetwopBlock[minNetwop] = pBlock->pNextNetwopBlock;
      pBlock->pNextNetwopBlock = NULL;

      EpgDbMergeAddPiBlock(dbc, pBlock, pPrevNetwopBlock[minNetwop]);
      pPrevNetwopBlock[minNetwop] = pBlock;
   }
}

// ---------------------------------------------------------------------------
// Merge equivalent PI blocks from different databases
//
static EPGDB_BLOCK * EpgDbMergePiBlocks( PDBC dbc, EPGDB_BLOCK **pFoundBlocks )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_BLOCK * pMergedBlock;
   const PI_BLOCK * pOnePi;
   PI_BLOCK       * pPi;
   EPGDB_MERGE_SRC  piDesc[MAX_MERGED_DB_COUNT];
   const char *pTitle;
   uint descTextLen;
   uint blockSize, off;
   uint dbCount, dbIdx, actIdx;
   uint firstIdx, piCount;
   uint idx, idx2;
   uchar version;
   time_t mtime;

   dbmc = dbc->pMergeContext;
   dbCount = dbmc->dbCount;

   version = 1;
   firstIdx = 0xff;
   piCount = 0;
   mtime = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      if (pFoundBlocks[dbIdx] != NULL)
      {
         if (firstIdx == 0xff)
            firstIdx = dbIdx;
         piDesc[piCount] = dbIdx;
         piCount += 1;
         if (pFoundBlocks[dbIdx]->version != dbmc->prov[dbIdx].pDbContext->pAiBlock->blk.ai.version)
            version = 0;
         if (mtime < pFoundBlocks[dbIdx]->acqTimestamp)
            mtime = pFoundBlocks[dbIdx]->acqTimestamp;
      }
   }
   // abort if no PI are in the list
   if (firstIdx == 0xff)
      return NULL;

   // get the first title
   pTitle = NULL;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_TITLE][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL)
         {
            pTitle = PI_GET_TITLE(&pFoundBlocks[actIdx]->blk.pi);
            break;
         }
      }
      else
         break;
   }
   if (pTitle == NULL)
      pTitle = "";

   // calculate length of concatenated description texts
   descTextLen = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_DESCR][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL)
         {
            if (PI_HAS_DESC_TEXT(&pFoundBlocks[actIdx]->blk.pi))
            {
               descTextLen += 3 + strlen(PI_GET_DESC_TEXT(&pFoundBlocks[actIdx]->blk.pi));
            }
         }
      }
      else
         break;
   }

   blockSize = sizeof(PI_BLOCK) + strlen(pTitle) + 1 + descTextLen + (piCount * sizeof(EPGDB_MERGE_SRC));
   pMergedBlock = EpgBlockCreate(BLOCK_TYPE_PI, blockSize, mtime);
   pMergedBlock->version    = version;
   pPi = (PI_BLOCK *) &pMergedBlock->blk.pi;  // cast to remove const from pointer
   off = sizeof(PI_BLOCK);

   pPi->netwop_no         = dbmc->prov[firstIdx].netwopMap[pFoundBlocks[firstIdx]->blk.pi.netwop_no];
   pPi->start_time        = pFoundBlocks[firstIdx]->blk.pi.start_time;
   pPi->stop_time         = pFoundBlocks[firstIdx]->blk.pi.stop_time;

   pPi->feature_flags = 0;
   // feature sound
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_SOUND][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL) 
         {
            const uint16_t mask = PI_FEATURE_SOUND_MASK;
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & mask;
            break;
         }
      }
      else
         break;
   }
   // feature picture format
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_FORMAT][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL) 
         {
            const uint16_t mask = PI_FEATURE_PAL_PLUS | PI_FEATURE_FMT_WIDE |
                                  PI_FEATURE_VIDEO_HD | PI_FEATURE_VIDEO_BW;
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & mask;
            break;
         }
      }
      else
         break;
   }
   // feature repeat
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_REPEAT][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL) 
         {
            const uint16_t mask = PI_FEATURE_REPEAT;
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & mask;
            break;
         }
      }
      else
         break;
   }
   // feature subtitles
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_SUBT][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL) 
         {
            const uint16_t mask = PI_FEATURE_SUBTITLES;
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & mask;
            break;
         }
      }
      else
         break;
   }
   // feature others
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_OTHERFEAT][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL) 
         {
            const uint16_t mask = 0xFFFFu & ~(PI_FEATURE_SOUND_MASK |
                                              PI_FEATURE_PAL_PLUS | PI_FEATURE_FMT_WIDE |
                                              PI_FEATURE_VIDEO_HD | PI_FEATURE_VIDEO_BW |
                                              PI_FEATURE_REPEAT | PI_FEATURE_SUBTITLES);
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & mask;
            break;
         }
      }
      else
         break;
   }

   // parental rating
   pPi->parental_rating = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_PARENTAL][dbIdx];
      if (actIdx < dbCount)
      {
         if ((pFoundBlocks[actIdx] != NULL) && (pFoundBlocks[actIdx]->blk.pi.parental_rating != 0))
         {
            pPi->parental_rating = pFoundBlocks[actIdx]->blk.pi.parental_rating;
            break;
         }
      }
      else
         break;
   }

   // editorial rating
   pPi->editorial_rating = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_EDITORIAL][dbIdx];
      if (actIdx < dbCount)
      {
         if ((pFoundBlocks[actIdx] != NULL) && (pFoundBlocks[actIdx]->blk.pi.editorial_rating != 0))
         {
            pPi->editorial_rating = pFoundBlocks[actIdx]->blk.pi.editorial_rating;
            break;
         }
      }
      else
         break;
   }

   // themes codes
   pPi->no_themes = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_THEMES][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL)
         {
            pOnePi = &pFoundBlocks[actIdx]->blk.pi;
            for (idx=0; (idx < pOnePi->no_themes) && (pPi->no_themes < PI_MAX_THEME_COUNT); idx++)
            {
               for (idx2=0; idx2 < pPi->no_themes; idx2++)
                  if (pPi->themes[idx2] == pOnePi->themes[idx])
                     break;
               if (idx2 >= pPi->no_themes)
               {  // theme is not in the list yet
                  pPi->themes[pPi->no_themes++] = pOnePi->themes[idx];
               }
            }
         }
      }
      else
         break;
   }

   // VPS PIL
   pPi->pil = 0x07FFF; // = VPS_PIL_CODE_SYSTEM
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_VPS][dbIdx];
      if (actIdx < dbCount)
      {
         if ((pFoundBlocks[actIdx] != NULL) && (pFoundBlocks[actIdx]->blk.pi.pil != 0x07FFF))
         {
            pPi->pil = pFoundBlocks[actIdx]->blk.pi.pil;
            break;
         }
      }
      else
         break;
   }

   // append merge descriptor array
   pPi->no_descriptors    = piCount;
   pPi->off_descriptors   = off;
   memcpy((void*)PI_GET_DESCRIPTORS(pPi), piDesc, piCount * sizeof(EPGDB_MERGE_SRC));
   off += piCount * sizeof(EPGDB_MERGE_SRC);

   // append title
   pPi->off_title = off;
   strcpy((char*)PI_GET_TITLE(pPi), pTitle);   // cast to remove "const"
   off += strlen(pTitle) + 1;

   // append concatenated description texts, separated by ASCII #12 = form-feed
   if (descTextLen > 0)
   {
      pPi->off_desc_text = off;
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         actIdx = dbmc->max[MERGE_TYPE_DESCR][dbIdx];
         if (actIdx < dbCount)
         {
            if (pFoundBlocks[actIdx] != NULL)
            {
               if (PI_HAS_DESC_TEXT(&pFoundBlocks[actIdx]->blk.pi))
               {
                  // cast here and below to remove const from macro results
                  strcpy((char*)PI_GET_STR_BY_OFF(pPi, off), PI_GET_DESC_TEXT(&pFoundBlocks[actIdx]->blk.pi));
                  off += strlen(PI_GET_STR_BY_OFF(pPi, off));

                  ((char *) PI_GET_STR_BY_OFF(pPi, off))[0] = EPG_DB_MERGE_DESC_TEXT_SEP;
                  ((char *) PI_GET_STR_BY_OFF(pPi, off))[1] = 0;
                  off += 1;
               }
            }
         }
         else
            break;
      }
   }
   else
      pPi->off_desc_text = 0;

   assert(off <= pMergedBlock->size);

   return pMergedBlock;
}

// ---------------------------------------------------------------------------
// Determine if two PI from different databases can be merged
// - Example for a critical case, i.e. adjacent programmes with the same title:
//   DB#0 'Cheers' 21.11 04:00 - 04:20
//   DB#0 'Cheers' 21.11 04:20 - 04:45
//   DB#2 'Cheers' 21.11 03:59 - 04:22 
//   DB#2 'Cheers' 21.11 04:22 - 04:45 
//
static bool EpgDbMerge_PiMatch( const PI_BLOCK * pRefPi, const PI_BLOCK * pNewPi )
{
   const char * p1, * p2, *pe, *ps;
   sint ovl, rtmin, rtmax;
   uint rt1, rt2;
   bool result = FALSE;

   if ( (pNewPi->start_time == pRefPi->start_time) &&
        (pNewPi->stop_time == pRefPi->stop_time) )
   {
      result = TRUE;
   }
   else
   {
      // calculate overlap (note: can get negative, if they don't actually overlap)
      ovl = MIN(pNewPi->stop_time, pRefPi->stop_time) -
            MAX(pNewPi->start_time, pRefPi->start_time);

      rt1 = (pNewPi->stop_time - pNewPi->start_time);
      rt2 = (pRefPi->stop_time - pRefPi->start_time);
      rtmin = MIN(rt1, rt2);
      rtmax = MAX(rt1, rt2);

      // must overlap at least by 50% of the larger one of both runtimes
      // AND running times must not differ by more than factor 1.5
      // OR special case: run-time 1 second, used on case of missing stop times in XMLTV

      if ( ( (ovl > rtmax / 2) &&
             (rtmin + rtmin/2 >= rtmax) ) ||
           ( (rtmin == 1) &&
             (labs(pNewPi->start_time - pRefPi->start_time) < 20*60)) )
      {
         // compare the titles
         p1 = PI_GET_TITLE(pRefPi);
         p2 = PI_GET_TITLE(pNewPi);
         while (*p1 && *p2)
         {
            // ignore case
            if ( (*p1 != *p2) &&
                 (tolower(*p1) != tolower(*p2)) )
            {
               // TODO: ignore parity errors (i.e. spaces)
               break;
            }
            else
            {
               p1++;
               p2++;
            }
         }
         // if one title is shorter: next must be non-alpha in longer one
         // OR both start and stop time must be very close
         pe = (*p1 != 0) ? p1 : p2;
         ps = pe;
         if (*ps != 0)
         {
            while (isspace(*ps))
               ps++;
         }
         if ( (*pe == 0) ||
              !isalnum(*ps) ||
              (isspace(*pe) && ((p1 - PI_GET_TITLE(pRefPi)) >= 20)) )
         {
            result = TRUE;
         }
         else if (((*p1 == 0) || (*p2 == 0)) &&
                  (labs(pNewPi->start_time - pRefPi->start_time) < 5*60) &&
                  (labs(pNewPi->stop_time - pRefPi->stop_time) < 5*60))
         {
            result = TRUE;
         }
      }
      if (result == FALSE)
         dprintf6("        CMP FAIL   '%s' %s -- %d,%d,%d,%d\n", PI_GET_TITLE(pNewPi), EpgDbMergePrintTime(pNewPi), ovl, rt1, rt2, abs(pNewPi->start_time - pRefPi->start_time));
   }
   return result;
}

// ---------------------------------------------------------------------------
// Merge all PI blocks of a single network
//
static void EpgDbMergeNetworkPi( PDBC dbc, uint netwop, EPGDB_BLOCK **ppFirstNetwopBlock )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_BLOCK *pNextBlock[MAX_MERGED_DB_COUNT];
   EPGDB_BLOCK *pFoundBlocks[MAX_MERGED_DB_COUNT];
   EPGDB_BLOCK *pPrevMerged;
   EPGDB_BLOCK *pBlock;
   time_t firstStartTime, firstStopTime;
   uint dbCount, dbIdx, firstIdx;
   bool conflict;

   dbmc = dbc->pMergeContext;
   dbCount = dbmc->dbCount;
   memset(pFoundBlocks, 0, sizeof(pFoundBlocks));

   // get first block of this network from each DB
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      uint dbNet = dbmc->prov[dbIdx].revNetwopMap[netwop];
      if (dbNet != 0xff)
         pNextBlock[dbIdx] = dbmc->prov[dbIdx].pDbContext->pFirstNetwopPi[dbNet];
      else
         pNextBlock[dbIdx] = NULL;
   }
   pPrevMerged = NULL;

   // process all blocks (of all source DBs) of this target network
   while (1)
   {
      firstStartTime = 0;
      firstIdx = MAX_MERGED_DB_COUNT; //dummy

      // determine the oldest unprocessed block of all DBs
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         if (pNextBlock[dbIdx] != NULL)
         {
            pBlock = pNextBlock[dbIdx];
            if ( (firstStartTime == 0) ||
                 (pBlock->blk.pi.start_time < firstStartTime) )
            {
               firstStartTime = pBlock->blk.pi.start_time;
               firstIdx = dbIdx;
            }
         }
      }

      // exit loop when all blocks are processed
      if (firstStartTime == 0)
         break;

      // identify which blocks can be merged
      dprintf4("MERGE: DB#%d net#%d '%s' %s\n", firstIdx, netwop, PI_GET_TITLE(&pNextBlock[firstIdx]->blk.pi), EpgDbMergePrintTime(&pNextBlock[firstIdx]->blk.pi));
      pFoundBlocks[firstIdx] = pNextBlock[firstIdx];
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         pBlock = pNextBlock[dbIdx];
         if ( (pBlock != NULL) &&
              (pFoundBlocks[dbIdx] == NULL) &&
              EpgDbMerge_PiMatch(&pFoundBlocks[firstIdx]->blk.pi, &pBlock->blk.pi) )
         {
            dprintf4("       DB#%d net#%d '%s' %s\n", dbIdx, netwop, PI_GET_TITLE(&pBlock->blk.pi), EpgDbMergePrintTime(&pBlock->blk.pi));
            pFoundBlocks[dbIdx] = pBlock;

            if (firstIdx > dbIdx)
               firstIdx = dbIdx;
         }
      }

      // check for overlapping with previous merged block of same network
      // (note: doing this after PI matching because start time may change)
      if ( (pPrevMerged != NULL) &&
           (pFoundBlocks[firstIdx]->blk.pi.start_time < pPrevMerged->blk.pi.stop_time) )
      {
         dprintf1("-----  OVERLAP prev. stop %s\n", EpgDbMergePrintTime(&pPrevMerged->blk.pi));
      }
      else
      {
         // check all databases of higher prio for conflicts with following blocks
         firstStopTime  = pFoundBlocks[firstIdx]->blk.pi.stop_time;
         conflict = FALSE;
         for (dbIdx=0; dbIdx < firstIdx; dbIdx++)
         {
            if (pNextBlock[dbIdx] != NULL)
            {
               if (pNextBlock[dbIdx]->blk.pi.start_time < firstStopTime)
               {
                  dprintf1("-----  OVERLAP next: DB#%d\n", dbIdx);
                  conflict = TRUE;
                  break;
               }
            }
         }

         if (conflict == FALSE)
         {
            // merge all equivalent blocks
            pBlock = EpgDbMergePiBlocks(dbc, pFoundBlocks);

            // append the merged block to the linked list
            pBlock->pNextNetwopBlock = NULL;
            if (*ppFirstNetwopBlock == NULL)
               *ppFirstNetwopBlock = pBlock;
            else
               pPrevMerged->pNextNetwopBlock = pBlock;
            pPrevMerged = pBlock;
         }
      }

      // finally skip over processed blocks
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         if (pFoundBlocks[dbIdx] != NULL)
         {
            pNextBlock[dbIdx] = pNextBlock[dbIdx]->pNextNetwopBlock;
            pFoundBlocks[dbIdx] = NULL;
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Merge all PI blocks
//
void EpgDbMergeAllPiBlocks( PDBC dbc )
{
   EPGDB_BLOCK *pFirstNetwopBlock[MAX_NETWOP_COUNT];
   uint netwop, netCount;

   netCount = dbc->pAiBlock->blk.ai.netwopCount;
   memset(pFirstNetwopBlock, 0, sizeof(pFirstNetwopBlock));

   // loop across target networks: merge networks separately
   for (netwop = 0; netwop < netCount; netwop++)
   {
      EpgDbMergeNetworkPi(dbc, netwop, &pFirstNetwopBlock[netwop]);
   }

   // combine networks into a single database
   EpgDbMergeLinkNetworkPi(dbc, pFirstNetwopBlock);

   assert(EpgDbCheckChains(dbc));
}

// ---------------------------------------------------------------------------
// Replace all blocks of one network
//
void EpgDbMergeUpdateNetwork( EPGDB_CONTEXT * pDbContext, uint srcNetwop, EPGDB_BLOCK * pNewBlock )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_BLOCK *pFirstNetwopBlock[MAX_NETWOP_COUNT];
   EPGDB_BLOCK *pMergedPi;
   EPGDB_BLOCK *pBlock, *pNext;
   uint netwop;

   assert(pDbContext->merged);

   dbmc = pDbContext->pMergeContext;
   netwop = dbmc->prov[dbmc->acqIdx].netwopMap[pNewBlock->blk.pi.netwop_no];
   if (netwop != 0xff)
   {
      pMergedPi = NULL;

      EpgDbMergeNetworkPi(pDbContext, netwop, &pMergedPi);

      // free old blocks of the newly merged network
      pBlock = pDbContext->pFirstNetwopPi[netwop];
      while (pBlock != NULL)
      {
         pNext = pBlock->pNextNetwopBlock;
         xfree(pBlock);
         pBlock = pNext;
      }

      // copy network block lists and insert newly merged blocks
      memcpy(pFirstNetwopBlock, pDbContext->pFirstNetwopPi, sizeof(pFirstNetwopBlock));
      pFirstNetwopBlock[netwop] = pMergedPi;

      EpgDbMergeLinkNetworkPi(pDbContext, pFirstNetwopBlock);

      assert(EpgDbCheckChains(pDbContext));

      // update GUI
      if (pDbContext->pPiAcqCb != NULL)
         pDbContext->pPiAcqCb(pDbContext, EPGDB_PI_RESYNC, NULL, NULL);
   }
}

// ---------------------------------------------------------------------------
// Reset "version ok" bit for all PI merged from a given db index
// - versions are handled in the opposite way than for normal dbs: instead of
//   incrementing the version in the AI, the version of PI is reset when
//   the version of one of the contributing databases has changed.
// - the advantage is that not all PI in the db are marked out of version,
//   which would be wrong because the given db probably has not contributed
//   to ever PI in the merged db.
//
void EpgDbMerge_ResetPiVersion( PDBC dbc, uint dbIdx )
{
   EPGDB_BLOCK * pBlock;
   EPGDB_MERGE_SRC * pDesc;
   uint idx;

   pBlock = dbc->pFirstPi;
   while (pBlock != NULL)
   {
      pDesc = (EPGDB_MERGE_SRC *) PI_GET_DESCRIPTORS(&pBlock->blk.pi);  // cast to remove const
      for (idx = pBlock->blk.pi.no_descriptors; idx > 0; idx--, pDesc++)
      {
         if (*pDesc == dbIdx)
         {
            dprintf2("EpgDbMerge-ResetPiVersion: net=%d start=%ld\n", pBlock->blk.pi.netwop_no, pBlock->blk.pi.start_time);
            pBlock->version = 0;
            break;
         }
      }
      pBlock = pBlock->pNextBlock;
   }
}

// ---------------------------------------------------------------------------
// Create service name for merged database
//
static char * EpgDbMergeAiServiceNames( EPGDB_MERGE_CONTEXT * dbmc )
{
   static const char * const mergedServiceName = "Merged EPG providers (";
   AI_BLOCK  * pAi;
   uint dbIdx, len;
   const char *name;
   char *mergeName;
   uint concatCnt;

   // sum up netwop name lens
   len = strlen(mergedServiceName) + 5 + 5;
   for (dbIdx=0; dbIdx < dbmc->dbCount; dbIdx++)
   {
      pAi = (AI_BLOCK *) &dbmc->prov[dbIdx].pDbContext->pAiBlock->blk.ai;
      name = AI_GET_SERVICENAME(pAi);
      // limit name length
      size_t tlen = strlen(name);
      if ((len + tlen >= MAX_SERVICE_NAME_LEN) && (dbIdx != 0))
         break;
      len += tlen + 2;
   }
   concatCnt = dbIdx;

   // allocate the memory
   mergeName = xmalloc(len);
   strcpy(mergeName, mergedServiceName);

   // concatenate the names
   for (dbIdx=0; dbIdx < concatCnt; dbIdx++)
   {
      pAi = (AI_BLOCK *) &dbmc->prov[dbIdx].pDbContext->pAiBlock->blk.ai;
      name = AI_GET_SERVICENAME(pAi);
      strcat(mergeName, name);
      if (dbIdx + 1 < concatCnt)
         strcat(mergeName, ", ");
   }
   if (concatCnt < dbmc->dbCount)
      strcat(mergeName, ", ...)");
   else
      strcat(mergeName, ")");

   return mergeName;
}

// ---------------------------------------------------------------------------
// Merge AI blocks & netwops
//
void EpgDbMergeAiBlocks( PDBC dbc, uint netwopCount, const uint * pNetwopList )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   const AI_BLOCK  * pAi;
   const AI_NETWOP * pNetwops;
   AI_BLOCK  * pTargetAi;
   AI_NETWOP * pTargetNetwops;
   time_t mtimeProv, mtimeMerge;
   uint netwop, dbIdx, idx;
   uchar netOrigIdx[MAX_NETWOP_COUNT];
   uchar dayCount[MAX_NETWOP_COUNT];
   char * pServiceName;           // temporarily holds merged service name
   uint nameLen;                  // sum of netwop name lengths
   uint blockLen;                 // length of composed AI block
   uint dbCount;

   // determine number of netwops in merged db
   dbmc = dbc->pMergeContext;
   dbCount = dbmc->dbCount;
   nameLen = 0;
   if (netwopCount > MAX_NETWOP_COUNT)
      netwopCount = MAX_NETWOP_COUNT;
   memset(netOrigIdx, 0xff, sizeof(netOrigIdx));

   for (idx = 0; idx < netwopCount; idx++)
   {
      dayCount[idx] = 0;
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         pAi = &dbmc->prov[dbIdx].pDbContext->pAiBlock->blk.ai;
         netwop = dbmc->prov[dbIdx].revNetwopMap[idx];
         if (netwop != 0xFF)
         {
            if (netOrigIdx[idx] == 0xff)
            {
               netOrigIdx[idx] = dbIdx;
               nameLen += strlen(AI_GET_NETWOP_NAME(pAi, netwop)) + 1;
            }

            if (AI_GET_NETWOP_N(pAi, netwop)->dayCount > dayCount[idx])
               dayCount[idx] = AI_GET_NETWOP_N(pAi, netwop)->dayCount;
         }
      }
   }

   mtimeMerge = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      mtimeProv = dbmc->prov[dbIdx].pDbContext->pAiBlock->acqTimestamp;
      if (mtimeMerge < mtimeProv)
         mtimeMerge = mtimeProv;
   }

   // merge service names
   pServiceName = EpgDbMergeAiServiceNames(dbmc);

   // allocate memory for new AI block
   blockLen = sizeof(AI_BLOCK) +
              netwopCount * sizeof(AI_NETWOP) +
              strlen(pServiceName) + 1 +
              nameLen;
   dbc->pAiBlock = EpgBlockCreate(BLOCK_TYPE_AI, blockLen, mtimeMerge);
   pTargetAi = (AI_BLOCK *) &dbc->pAiBlock->blk.ai;

   // init base struct
   memcpy(pTargetAi, &dbmc->prov[0].pDbContext->pAiBlock->blk.ai, sizeof(AI_BLOCK));
   pTargetAi->off_netwops = sizeof(AI_BLOCK);
   pTargetAi->netwopCount = netwopCount;
   pTargetAi->version     = 1;
   blockLen = sizeof(AI_BLOCK) + netwopCount * sizeof(AI_NETWOP);

   // append service name
   pTargetAi->off_serviceNameStr = blockLen;
   strcpy((char *)AI_GET_STR_BY_OFF(pTargetAi, blockLen), pServiceName);
   blockLen += strlen(pServiceName) + 1;

   // append netwop structs and netwop names
   pTargetNetwops = (AI_NETWOP *) AI_GET_NETWOPS(pTargetAi);
   memset(pTargetNetwops, 0, netwopCount * sizeof(AI_NETWOP));

   for (netwop=0; netwop < netwopCount; netwop++)
   {
      dbIdx = netOrigIdx[netwop];
      if (dbIdx != 0xff)
      {
         pAi = (AI_BLOCK *) &dbmc->prov[dbIdx].pDbContext->pAiBlock->blk.ai;
         idx = dbmc->prov[dbIdx].revNetwopMap[netwop];
         if (idx < pAi->netwopCount)
         {
            pNetwops = AI_GET_NETWOP_N(pAi, idx);
            // note: following is not an error due to use of CniConvertUnknownToPdc()
            //ifdebug3(pNetwops->cni != pNetwopList[netwop], "EpgDb-MergeAiBlocks: mismatch of CNIs in netwop #%d: 0x%04X!=0x%04X", idx, pNetwops->cni, pNetwopList[netwop]);

            pTargetNetwops[netwop].netCni   = AI_GET_NET_CNI(pNetwops) &  XMLTV_NET_CNI_MASK;
            pTargetNetwops[netwop].netCniMSB = AI_GET_NET_CNI(pNetwops) >> XMLTV_NET_CNI_MSBS;
            pTargetNetwops[netwop].language = pNetwops->language;
            pTargetNetwops[netwop].dayCount = dayCount[netwop];
            pTargetNetwops[netwop].off_name = blockLen;
            strcpy((char *) AI_GET_STR_BY_OFF(pTargetAi, blockLen), AI_GET_STR_BY_OFF(pAi, pNetwops->off_name));
            blockLen += strlen(AI_GET_STR_BY_OFF(pAi, pNetwops->off_name)) + 1;
         }
         else
            fatal4("EpgDb-MergeAiBlocks: netwop %d (CNI 0x%04X) mapped to illegal netwop %d in db #%d", netwop, pNetwopList[netwop], idx, dbIdx);
      }
      else
         fatal2("EpgDb-MergeAiBlocks: netwop %d (CNI 0x%04X) not mapped", netwop, pNetwopList[netwop]);
   }
   xfree(pServiceName);
}

