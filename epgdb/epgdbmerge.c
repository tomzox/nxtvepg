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
 *  $Id: epgdbmerge.c,v 1.28 2003/10/05 19:13:36 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgui/epgmain.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgdbmerge.h"


// internal shortcut
typedef EPGDB_CONTEXT *PDBC;


// ---------------------------------------------------------------------------
// Append a PI block to the database
//
static void EpgDbMergeAddPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK **pPrevNetwop )
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
      pPrevNetwop[netwop] = pBlock;
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
         pPrevNetwop[netwop]->pNextNetwopBlock = pBlock;
         pBlock->pPrevNetwopBlock = pPrevNetwop[netwop];
      }
      pBlock->pNextNetwopBlock = NULL;
      pPrevNetwop[netwop] = pBlock;

      // set pointers of start time chain
      pBlock->pPrevBlock = dbc->pLastPi;
      pBlock->pNextBlock = NULL;
      dbc->pLastPi->pNextBlock = pBlock;
      dbc->pLastPi = pBlock;
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
   DESCRIPTOR       piDesc[MAX_MERGED_DB_COUNT];
   const uchar *pTitle;
   uint slInfoLen;
   uint blockSize, off;
   uint dbCount, dbIdx, actIdx;
   uint anyIdx, piCount;
   uint idx, idx2;
   uchar stream, version;
   bool haveSeries;

   dbmc = dbc->pMergeContext;
   dbCount = dbmc->dbCount;

   version = 1;
   stream = 1;
   anyIdx = 0xff;
   piCount = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      if (pFoundBlocks[dbIdx] != NULL)
      {
         anyIdx = dbIdx;
         piDesc[piCount].type = MERGE_DESCR_TYPE;
         piDesc[piCount].id   = dbIdx;
         piCount += 1;
         stream &= pFoundBlocks[dbIdx]->stream;
         if (pFoundBlocks[dbIdx]->version != ((pFoundBlocks[dbIdx]->stream == NXTV_STREAM_NO_1) ?
                                              dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai.version :
                                              dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai.version_swo))
            version = 0;
      }
   }
   // abort if no PI are in the list
   if (anyIdx == 0xff)
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

   // calculate length of concatenated short and long infos
   slInfoLen = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_DESCR][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL)
         {
            if (PI_HAS_SHORT_INFO(&pFoundBlocks[actIdx]->blk.pi))
            {
               slInfoLen += 3 + strlen(PI_GET_SHORT_INFO(&pFoundBlocks[actIdx]->blk.pi));
            }
            if (PI_HAS_LONG_INFO(&pFoundBlocks[actIdx]->blk.pi))
            {
               slInfoLen += 3 + strlen(PI_GET_LONG_INFO(&pFoundBlocks[actIdx]->blk.pi));
            }
         }
      }
      else
         break;
   }

   blockSize = sizeof(PI_BLOCK) + strlen(pTitle) + 1 + slInfoLen + (piCount * sizeof(DESCRIPTOR));
   pMergedBlock = EpgBlockCreate(BLOCK_TYPE_PI, blockSize);
   pMergedBlock->stream     = stream;
   pMergedBlock->version    = version;
   pPi = (PI_BLOCK *) &pMergedBlock->blk.pi;  // cast to remove const from pointer
   off = sizeof(PI_BLOCK);

   pPi->block_no          = 0;
   pPi->netwop_no         = dbmc->netwopMap[anyIdx][pFoundBlocks[anyIdx]->blk.pi.netwop_no];
   pPi->start_time        = pFoundBlocks[anyIdx]->blk.pi.start_time;
   pPi->stop_time         = pFoundBlocks[anyIdx]->blk.pi.stop_time;

   pPi->background_ref    = 0;
   pPi->background_reuse  = 0;
   pPi->off_long_info     = 0;

   pPi->feature_flags = 0;
   // feature sound
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_SOUND][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL) 
         {
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & 0x03;
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
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & 0x0c;
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
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & 0x80;
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
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & 0x100;
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
            pPi->feature_flags |= pFoundBlocks[actIdx]->blk.pi.feature_flags & 0xfe70;
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

   pPi->no_themes = 0;
   // series codes - first provider only
   haveSeries = FALSE;
   actIdx = dbmc->max[MERGE_TYPE_SERIES][0];
   if ( (actIdx < dbCount) && (pFoundBlocks[actIdx] != NULL) )
   {
      pOnePi = &pFoundBlocks[actIdx]->blk.pi;
      for (idx=0; idx < pOnePi->no_themes; idx++)
      {
         if (pOnePi->themes[idx] >= 0x80)
         {
            pPi->themes[pPi->no_themes++] = pOnePi->themes[idx];
            haveSeries = TRUE;
         }
      }
   }

   // themes codes
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
               if (pOnePi->themes[idx] < 0x80)
               {
                  for (idx2=0; idx2 < pPi->no_themes; idx2++)
                     if (pPi->themes[idx2] == pOnePi->themes[idx])
                        break;
                  if (idx2 >= pPi->no_themes)
                  {  // theme is not in the list yet
                     pPi->themes[pPi->no_themes++] = pOnePi->themes[idx];
                  }
               }
               else if (haveSeries == FALSE)
               {
                  pPi->themes[pPi->no_themes++] = 0x80;
                  haveSeries = TRUE;
               }
            }
         }
      }
      else
         break;
   }

   // sorting criteria - first provider only
   pPi->no_sortcrit = 0;
   actIdx = dbmc->max[MERGE_TYPE_SORTCRIT][0];
   if ( (actIdx < dbCount) && (pFoundBlocks[actIdx] != NULL) )
   {
      pOnePi = &pFoundBlocks[actIdx]->blk.pi;
      for (idx=0; idx < pOnePi->no_sortcrit; idx++)
      {
         pPi->sortcrits[idx] = pOnePi->sortcrits[idx];
      }
      pPi->no_sortcrit = pOnePi->no_sortcrit;
   }

   // VPS PIL
   pPi->pil = 0x07FFF;
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
   memcpy((char*)PI_GET_DESCRIPTORS(pPi), piDesc, piCount * sizeof(DESCRIPTOR));
   off += piCount * sizeof(DESCRIPTOR);

   // append title
   pPi->off_title = off;
   strcpy((char*)PI_GET_TITLE(pPi), pTitle);
   off += strlen(pTitle) + 1;

   // append concatenated short infos, separated by ASCII #12 = form-feed
   if (slInfoLen > 0)
   {
      pPi->off_short_info = off;
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         actIdx = dbmc->max[MERGE_TYPE_DESCR][dbIdx];
         if (actIdx < dbCount)
         {
            if (pFoundBlocks[actIdx] != NULL)
            {
               bool hasShort = PI_HAS_SHORT_INFO(&pFoundBlocks[actIdx]->blk.pi);
               bool hasLong  = PI_HAS_LONG_INFO(&pFoundBlocks[actIdx]->blk.pi);

               if (hasShort)
               {
                  strcpy((char*)PI_GET_STR_BY_OFF(pPi, off), PI_GET_SHORT_INFO(&pFoundBlocks[actIdx]->blk.pi));
                  off += strlen(PI_GET_STR_BY_OFF(pPi, off));

                  ((char *) PI_GET_STR_BY_OFF(pPi, off))[0] = (hasLong ? '\n' : 12);  // cast to remove const from macro result
                  ((char *) PI_GET_STR_BY_OFF(pPi, off))[1] = 0;
                  off += 1;
               }
               if (hasLong)
               {
                  if (hasShort == FALSE)
                  {
                     ((char *) PI_GET_STR_BY_OFF(pPi, off))[0] = '\n';
                     off += 1;
                  }

                  strcpy((char*)PI_GET_STR_BY_OFF(pPi, off), PI_GET_LONG_INFO(&pFoundBlocks[actIdx]->blk.pi));
                  off += strlen(PI_GET_STR_BY_OFF(pPi, off));

                  ((char *) PI_GET_STR_BY_OFF(pPi, off))[0] = 12;
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
      pPi->off_short_info = 0;

   assert(off <= pMergedBlock->size);

   return pMergedBlock;
}

// ---------------------------------------------------------------------------
// Merge all PI blocks
//
void EpgDbMergeAllPiBlocks( PDBC dbc )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_BLOCK *pNextBlock[MAX_MERGED_DB_COUNT];
   EPGDB_BLOCK *pFoundBlocks[MAX_MERGED_DB_COUNT];
   EPGDB_BLOCK *pBlock;
   EPGDB_BLOCK *pPrevNetwopBlock[MAX_NETWOP_COUNT];
   time_t firstStartTime, firstStopTime, minStartTime;
   time_t expireTime;
   uchar  netwop, minMappedNetwop, firstNetwop;
   uint dbCount, dbIdx;

   dbmc = dbc->pMergeContext;
   dbCount = dbmc->dbCount;
   expireTime = time(NULL) - dbc->expireDelayPi;
   minStartTime = 0;
   minMappedNetwop = 0;
   memset(pPrevNetwopBlock, 0, sizeof(pPrevNetwopBlock));
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      pNextBlock[dbIdx] = dbmc->pDbContext[dbIdx]->pFirstPi;
   }

   // loop until all PI blocks are processed
   while (1)
   {
      // find earliest unprocessed block
      firstStartTime = 0;
      firstStopTime = 0;  //compiler-dummy
      firstNetwop = 0xff;
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         // skip PI before the minimum start time
         pBlock = pNextBlock[dbIdx];
         while ( (pBlock != NULL) &&
                 ( (pBlock->blk.pi.start_time < minStartTime) ||
                   (pBlock->blk.pi.stop_time < expireTime) ) )
         {
            pBlock = pBlock->pNextBlock;
         }
         pNextBlock[dbIdx] = pBlock;

         while(pBlock != NULL)
         {
            netwop = dbmc->netwopMap[dbIdx][pBlock->blk.pi.netwop_no];
            if (netwop != 0xff)
            {
               assert(pBlock->blk.pi.start_time >= minStartTime);  //enforced by while above
               if ( (pBlock->blk.pi.start_time > minStartTime) || (netwop > minMappedNetwop) )
               {
                  if ( (firstStartTime == 0) ||
                       (pBlock->blk.pi.start_time < firstStartTime) ||
                       ((pBlock->blk.pi.start_time == firstStartTime) && (netwop < firstNetwop)) )
                  {  // new minimum
                     memset(pFoundBlocks, 0, sizeof(pFoundBlocks));
                     pFoundBlocks[dbIdx] = pBlock;
                     firstNetwop = netwop;
                     firstStartTime = pBlock->blk.pi.start_time;
                     firstStopTime  = pBlock->blk.pi.stop_time;
                  }
                  else if ( (pBlock->blk.pi.start_time == firstStartTime) &&
                            (pBlock->blk.pi.stop_time == firstStopTime) &&
                            (netwop == firstNetwop))
                  {  // second occasion of minimum
                     pFoundBlocks[dbIdx] = pBlock;
                  }
                  else if (pBlock->blk.pi.start_time > firstStartTime)
                  {
                     break;
                  }
               }
            }
            pBlock = pBlock->pNextBlock;
         }
      }

      if (firstNetwop == 0xff)
         break;

      assert((minStartTime != firstStartTime) || (minMappedNetwop != firstNetwop));
      minStartTime = firstStartTime;
      minMappedNetwop = firstNetwop;

      // check for overlapping
      if ( (pPrevNetwopBlock[firstNetwop] == NULL) ||
           (firstStartTime >= pPrevNetwopBlock[firstNetwop]->blk.pi.stop_time) )
      {
         bool conflict = FALSE;

         for (dbIdx=0; (dbIdx < dbCount) && (conflict == FALSE); dbIdx++)
         {
            if (pFoundBlocks[dbIdx] == NULL)
            {
               pBlock = pNextBlock[dbIdx];
               while ((pBlock != NULL) && (pBlock->blk.pi.start_time <= firstStopTime))
               {
                  if ((dbmc->netwopMap[dbIdx][pBlock->blk.pi.netwop_no] == firstNetwop) &&
                      (pBlock->blk.pi.start_time < firstStopTime))
                  {
                     conflict = TRUE;
                     break;
                  }
                  pBlock = pBlock->pNextBlock;
               }
            }
            else
            {  // all checked, no overlapping
               // merge all equivalent blocks
               pBlock = EpgDbMergePiBlocks(dbc, pFoundBlocks);
               // append the merged block to the database
               EpgDbMergeAddPiBlock(dbc, pBlock, pPrevNetwopBlock);
               break;
            }
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Search equivalent PI in all dbs for insertion PI and check for conflicts
//
static bool EpgDbMergeGetPiEquivs( EPGDB_MERGE_CONTEXT * dbmc, EPGDB_BLOCK * pNewBlock, EPGDB_BLOCK ** pFoundBlocks )
{
   EPGDB_BLOCK *pWalk, *pPrev;
   uchar mappedNetwop, netwop;
   uint dbIdx;

   memset(pFoundBlocks, 0, sizeof(EPGDB_BLOCK *) * dbmc->dbCount);
   mappedNetwop = dbmc->netwopMap[dbmc->acqIdx][pNewBlock->blk.pi.netwop_no];

   if (mappedNetwop != 0xff)
   {
      // search the first db that covers the time range of the new PI := master PI
      for (dbIdx=0; dbIdx < dbmc->acqIdx; dbIdx++)
      {
         netwop = dbmc->revNetwopMap[dbIdx][mappedNetwop];
         if (netwop < dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai.netwopCount)
         {
            pWalk = dbmc->pDbContext[dbIdx]->pFirstNetwopPi[netwop];
            pPrev = NULL;
            while ( (pWalk != NULL) &&
                    (pWalk->blk.pi.start_time < pNewBlock->blk.pi.start_time) )
            {
               pPrev = pWalk;
               pWalk = pWalk->pNextNetwopBlock;
            }
            // test for conflicting run-time
            if ( (pPrev != NULL) && (pPrev->blk.pi.stop_time > pNewBlock->blk.pi.start_time) )
            {  // previous block overlaps -> refuse the new block
               pNewBlock = NULL;
               break;
            }
            if (pWalk != NULL)
            {
               if ( (pWalk->blk.pi.start_time == pNewBlock->blk.pi.start_time) &&
                    (pWalk->blk.pi.stop_time == pNewBlock->blk.pi.stop_time) )
               {  // equivalent block -> found master PI
                  pFoundBlocks[dbIdx] = pWalk;
                  break;
               }
               else if (pWalk->blk.pi.start_time < pNewBlock->blk.pi.stop_time)
               {
                  // new block overlaps -> refuse
                  pNewBlock = NULL;
                  break;
               }
            }
         }
      }

      if (pNewBlock != NULL)
      {  // no conflicts

         // find equivalent PIs in the following dbs
         for ( ; dbIdx < dbmc->dbCount; dbIdx++)
         {
            if (dbIdx != dbmc->acqIdx)
            {
               netwop = dbmc->revNetwopMap[dbIdx][mappedNetwop];
               if (netwop < dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai.netwopCount)
               {
                  pWalk = dbmc->pDbContext[dbIdx]->pFirstNetwopPi[netwop];
                  while ( (pWalk != NULL) &&
                          (pWalk->blk.pi.start_time < pNewBlock->blk.pi.start_time) )
                  {
                     pWalk = pWalk->pNextNetwopBlock;
                  }

                  if ( (pWalk != NULL) &&
                       (pWalk->blk.pi.start_time == pNewBlock->blk.pi.start_time) &&
                       (pWalk->blk.pi.stop_time == pNewBlock->blk.pi.stop_time) )
                  {  // equivalent block -> add to merge set
                     pFoundBlocks[dbIdx] = pWalk;
                  }
               }
            }
            else
            {  // no need to search for the block to be inserted
               pFoundBlocks[dbIdx] = pNewBlock;
            }
         }
         return TRUE;
      }
      else
         return FALSE;
   }
   else
      return FALSE;
}

// ---------------------------------------------------------------------------
// Insert a PI block into the merged db
// - called after the block was inserted to its provider's database
// 
void EpgDbMergeInsertPi( EPGDB_MERGE_CONTEXT * dbmc, EPGDB_BLOCK * pNewBlock )
{
   EPGDB_BLOCK *pFoundBlocks[MAX_MERGED_DB_COUNT];
   EPGDB_BLOCK *pWalk, *pPrev, *pNext;

   // find equivalent blocks in all other dbs and check for conflicts with higher-priorized PI
   if ( EpgDbMergeGetPiEquivs(dbmc, pNewBlock, pFoundBlocks) )
   {
      if (pUiDbContext->pPiAcqCb != NULL)
         pUiDbContext->pPiAcqCb(pUiDbContext, EPGDB_PI_PROC_START, NULL, NULL);

      // merge the found blocks
      pNewBlock = EpgDbMergePiBlocks(pUiDbContext, pFoundBlocks);

      // find the insertion position in the merged db
      pWalk = pUiDbContext->pFirstNetwopPi[pNewBlock->blk.pi.netwop_no];
      pPrev = NULL;
      while ( (pWalk != NULL) &&
              (pWalk->blk.pi.start_time < pNewBlock->blk.pi.start_time) )
      {
         pPrev = pWalk;
         pWalk = pWalk->pNextNetwopBlock;
      }

      if ( (pWalk != NULL) &&
           (pWalk->blk.pi.start_time == pNewBlock->blk.pi.start_time) &&
           (pWalk->blk.pi.stop_time  == pNewBlock->blk.pi.stop_time) )
      {  // special case: replacing a block with identical ordering keys
         // (this case is handled special for performance reasons only:
         // conflict handling and search of exact insert position is not needed here)

         EpgDbReplacePi(pUiDbContext, pWalk, pNewBlock);
      }
      else
      {
         pNext = pWalk;
         // delete conflicting blocks in the merged db
         // (this also covers a replacement of an equivalent block)
         pWalk = pPrev;
         while ( (pWalk != NULL) && (pWalk->blk.pi.stop_time > pNewBlock->blk.pi.start_time) )
         {  // previous blocks overlaps the new one -> remove it
            dprintf4("+++++++ DELETE: ptr=%lx prev=%lx start=%ld > %ld\n", (ulong)pNewBlock, (ulong)pWalk, pWalk->blk.pi.start_time, pNewBlock->blk.pi.start_time);
            pPrev = pWalk->pPrevNetwopBlock;
            EpgDbPiRemove(pUiDbContext, pWalk);
            xfree(pWalk);
            pWalk = pPrev;
         }

         pWalk = pNext;
         while( (pWalk != NULL) && (pNewBlock->blk.pi.stop_time > pWalk->blk.pi.start_time) )
         {
            dprintf4("+++++++ DELETE: ptr=%lx next=%lx overlapped: start=%ld < stop %ld\n", (ulong)pNewBlock, (ulong)pWalk, pWalk->blk.pi.start_time, pNewBlock->blk.pi.stop_time);
            pNext = pWalk->pNextNetwopBlock;
            EpgDbPiRemove(pUiDbContext, pWalk);
            xfree(pWalk);
            pWalk = pNext;
         }

         // find the exact insertion position and link the new PI inbetween
         EpgDbLinkPi(pUiDbContext, pNewBlock, pPrev, pNext);
      }

      assert(EpgDbCheckChains(pUiDbContext));
      // if blocks were removed, re-evaluate scrollbar position
      if (pUiDbContext->pPiAcqCb != NULL)
         pUiDbContext->pPiAcqCb(pUiDbContext, EPGDB_PI_PROC_DONE, NULL, NULL);

      // append the block's covered time range to the PI timescale queue
      if (dbmc->tscEnable)
         EpgTscQueue_AddPi(&dbmc->tscQueue, pUiDbContext, &pNewBlock->blk.pi, pNewBlock->stream);
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
   DESCRIPTOR  * pDesc;
   uint idx;

   pBlock = dbc->pFirstPi;
   while (pBlock != NULL)
   {
      pDesc = (DESCRIPTOR *) PI_GET_DESCRIPTORS(&pBlock->blk.pi);
      for (idx = pBlock->blk.pi.no_descriptors; idx > 0; idx--, pDesc++)
      {
         if ((pDesc->type == MERGE_DESCR_TYPE) && (pDesc->id == dbIdx))
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
static uchar * EpgDbMergeAiServiceNames( EPGDB_MERGE_CONTEXT * dbmc )
{
   static const uchar * const mergedServiceName = "Merged database (";
   AI_BLOCK  * pAi;
   uint dbIdx, len;
   const uchar *name;
   uchar *mergeName;

   // sum up netwop name lens
   len = strlen(mergedServiceName) + 5;
   for (dbIdx=0; dbIdx < dbmc->dbCount; dbIdx++)
   {
      pAi = (AI_BLOCK *) &dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai;
      name = AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop);
      len += strlen(name) + 2;
   }

   // allocate the memory
   mergeName = xmalloc(len);
   strcpy(mergeName, mergedServiceName);

   // concatenate the names
   for (dbIdx=0; dbIdx < dbmc->dbCount; dbIdx++)
   {
      pAi = (AI_BLOCK *) &dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai;
      name = AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop);
      strcat(mergeName, name);
      if (dbIdx + 1 < dbmc->dbCount)
         strcat(mergeName, ", ");
   }
   strcat(mergeName, ")");

   return mergeName;
}

// ---------------------------------------------------------------------------
// Merge AI blocks & netwops
// - build netwop mapping and reverse tables for each database
// - during the very first merge the network list is always empty, because the
//   user hasn't had a chance to invoke the network selection -> must include
//   all CNIs of all providers
// - XXX must not use passed netwop list to build new netwop list - might be too short!
//
void EpgDbMergeAiBlocks( PDBC dbc, uint cfNetwopCount, uint * pNetwopList )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   const AI_BLOCK  * pAi;
   const AI_NETWOP * pNetwops;
   AI_BLOCK  * pTargetAi;
   AI_NETWOP * pTargetNetwops;
   uint netwop, dbIdx, idx;
   bool  found;
   uchar netOrigIdx[MAX_NETWOP_COUNT];
   uchar dayCount[MAX_NETWOP_COUNT];
   uchar * pServiceName;          // temporarily holds merged service name
   uint nameLen;                  // sum of netwop name lengths
   uint blockLen;                 // length of composed AI block
   uint netwopCount;              // number of CNIs in merged netwop table
   uint dbCount;
   uint cni;

   // determine number of netwops in merged db
   dbmc = dbc->pMergeContext;
   dbCount = dbmc->dbCount;
   nameLen = 0;
   if (cfNetwopCount > MAX_NETWOP_COUNT)
      cfNetwopCount = MAX_NETWOP_COUNT;
   memset(netOrigIdx, 0xff, sizeof(netOrigIdx));

   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      memset(dbmc->revNetwopMap[dbIdx], 0xff, sizeof(dbmc->revNetwopMap[0]));
      memset(dbmc->netwopMap[dbIdx], 0xff, sizeof(dbmc->netwopMap[0]));
   }

   if (cfNetwopCount == 0)
   {  // user-configured netwop table is empty -> build one of all CNIs in all AIs
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         pAi = (AI_BLOCK *) &dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai;
         pNetwops = AI_GET_NETWOPS(pAi);

         for (netwop=0; netwop < pAi->netwopCount; netwop++, pNetwops++)
         {
            // check if the CNI already is in the generated table
            for (idx=0; idx < cfNetwopCount; idx++)
               if (pNetwops->cni == pNetwopList[idx])
                  break;

            if ((idx == cfNetwopCount) && (cfNetwopCount < MAX_NETWOP_COUNT))
            {
               pNetwopList[cfNetwopCount] = pNetwops->cni;
               cfNetwopCount += 1;
            }
         }
      }
   }

   netwopCount = 0;
   for (idx=0; idx < cfNetwopCount; idx++)
   {
      found = FALSE;
      cni = pNetwopList[idx];
      // check if the same CNI already was in the list
      for (netwop=0; netwop < netwopCount; netwop++)
         if (pNetwopList[netwop] == cni)
            break;
      // check if it's a valid CNI
      if ((netwop >= netwopCount) && (cni != 0) && (cni != 0x00ff) &&
          (netwopCount < MAX_NETWOP_COUNT))
      {
         dayCount[netwopCount] = 0;
         for (dbIdx=0; dbIdx < dbCount; dbIdx++)
         {
            pAi = (AI_BLOCK *) &dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai;
            pNetwops = AI_GET_NETWOPS(pAi);
            for (netwop=0; netwop < pAi->netwopCount; netwop++, pNetwops++)
               if (pNetwops->cni == cni)
                  break;
            if (netwop < pAi->netwopCount)
            {  // found
               if (found == FALSE)
               {  // first db with that CNI -> copy params from here
                  assert(netOrigIdx[netwopCount] == 0xff);
                  netOrigIdx[netwopCount] = dbIdx;
                  nameLen += strlen(AI_GET_STR_BY_OFF(pAi, pNetwops->off_name)) + 1;

                  pNetwopList[netwopCount] = cni;
                  found = TRUE;
               }
               if (pNetwops->dayCount > dayCount[netwopCount])
                  dayCount[netwopCount] = pNetwops->dayCount;
               dbmc->netwopMap[dbIdx][netwop] = netwopCount;
               dbmc->revNetwopMap[dbIdx][netwopCount] = netwop;
            }
         }
         if (found)
            netwopCount += 1;
      }
      // if the CNI is in none of the databases, it's skipped
      ifdebug1(found == FALSE, "EpgDb-MergeAiBlocks: skipping CNI 0x%04X", cni);
   }

   // merge service names
   pServiceName = EpgDbMergeAiServiceNames(dbmc);

   // allocate memory for new AI block
   blockLen = sizeof(AI_BLOCK) +
              netwopCount * sizeof(AI_NETWOP) +
              strlen(pServiceName) + 1 +
              nameLen;
   dbc->pAiBlock = EpgBlockCreate(BLOCK_TYPE_AI, blockLen);
   pTargetAi = (AI_BLOCK *) &dbc->pAiBlock->blk.ai;

   // init base struct
   memcpy(pTargetAi, &dbmc->pDbContext[0]->pAiBlock->blk.ai, sizeof(AI_BLOCK));
   pTargetAi->off_netwops = sizeof(AI_BLOCK);
   pTargetAi->netwopCount = netwopCount;
   pTargetAi->version     = 1;
   pTargetAi->version_swo = 1;
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
         pAi = (AI_BLOCK *) &dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai;
         idx = dbmc->revNetwopMap[dbIdx][netwop];
         if (idx < pAi->netwopCount)
         {
            pNetwops = AI_GET_NETWOP_N(pAi, idx);
            assert(pNetwops->cni == pNetwopList[netwop]);

            pTargetNetwops[netwop].cni      = pNetwops->cni;
            pTargetNetwops[netwop].alphabet = pNetwops->alphabet;
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

