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
 *    Merges serveral databases into one.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgdbmerge.c,v 1.8 2000/12/24 13:34:57 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgctl/epgmain.h"
#include "epgctl/epgctxctl.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/epgstream.h"
#include "epgui/pilistbox.h"
#include "epgui/statswin.h"


// internal shortcut
typedef EPGDB_CONTEXT *PDBC;


// ---------------------------------------------------------------------------
// Close all databases
//
static void EpgDbMergeCloseDatabases( EPGDB_MERGE_CONTEXT * pMergeContext )
{
   uint dbIdx;

   for (dbIdx=0; dbIdx < pMergeContext->dbCount; dbIdx++)
   {
      if (pMergeContext->pDbContext[dbIdx] != NULL)
      {
         EpgContextCtl_Close(pMergeContext->pDbContext[dbIdx]);
         pMergeContext->pDbContext[dbIdx] = NULL;
      }
   }

   pMergeContext->acqIdx = 0xff;
}

// ---------------------------------------------------------------------------
// Open all databases for complete merge or acquisition
//
static bool EpgDbMergeOpenDatabases( EPGDB_MERGE_CONTEXT * pMergeContext )
{
   uint dbIdx, cni;
   bool result = TRUE;

   for (dbIdx=0; dbIdx < pMergeContext->dbCount; dbIdx++)
   {
      if (pMergeContext->pDbContext[dbIdx] == NULL)
      {
         cni = pMergeContext->cnis[dbIdx];
         pMergeContext->pDbContext[dbIdx] = EpgContextCtl_Open(cni);

         if (EpgDbContextGetCni(pMergeContext->pDbContext[dbIdx]) != cni)
         {  // database could not be loaded -> abort merge
            result = FALSE;
            break;
         }
      }
   }

   if (result == FALSE)
   {  // free already opened databases
      EpgDbMergeCloseDatabases(pMergeContext);
   }
   return result;
}

// ---------------------------------------------------------------------------
// Destroy the sub-context of a merged database
//
void EpgDbMergeDestroyContext( void * pMergeContextPtr )
{
   EpgDbMergeCloseDatabases((EPGDB_MERGE_CONTEXT *) pMergeContextPtr);
   xfree(pMergeContextPtr);
}

// ---------------------------------------------------------------------------
// Returns list of provider CNIs in merged database
//
bool EpgDbMergeGetCnis( const EPGDB_CONTEXT * dbc, uint * pCniCount, uint *pCniTab )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   bool result = FALSE;

   if ((pCniCount != NULL) && (pCniTab != NULL))
   {
      if (dbc->pMergeContext != NULL)
      {
         dbmc = pUiDbContext->pMergeContext;

         *pCniCount = dbmc->dbCount;
         memcpy(pCniTab, dbmc->cnis, dbmc->dbCount * sizeof(uint));

         result = TRUE;
      }
      else
         debug0("EpgDbMerge-GetCnis: db has no merged context");
   }
   else
      debug0("EpgDbMerge-GetCnis: illegal NULL ptr param");

   return result;
}

// ---------------------------------------------------------------------------
// Determine the index of the current acq db
//
static bool EpgDbMergeOpenAcqContext( PDBC dbc, uint cni )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   uint dbIdx;
   bool result = FALSE;

   if ( (pUiDbContext != NULL) && (pUiDbContext->pMergeContext != NULL) )
   {
      dbmc = pUiDbContext->pMergeContext;

      // get the index of the acq db in the merge context
      if ( (dbmc->acqIdx >= dbmc->dbCount) || (cni != dbmc->cnis[dbmc->acqIdx]) )
      {  // no index cached or wrong CNI -> search CNI in list
         dbmc->acqIdx = 0xff;
         for (dbIdx=0; dbIdx < dbmc->dbCount; dbIdx++)
         {
            if (dbmc->cnis[dbIdx] == cni)
            {
               dbmc->acqIdx = dbIdx;
               result = TRUE;
               break;
            }
         }
      }
      else
         result = TRUE;

      if (result)
      {  // open the db contexts of all dbs in the merge context
         if (dbmc->pDbContext[0] == NULL)
         {
            if (EpgDbMergeOpenDatabases(dbmc) == FALSE)
            {  // open failed -> cannot insert
               dbmc->acqIdx = 0xff;
               result = FALSE;
            }
         }
      }
      else
      {  // wrong cni -> close db contexts, if neccessary
         EpgDbMergeCloseDatabases(dbmc);
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Append a PI block to the database
//
static void EpgDbMergeAddPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK **pPrevNetwop )
{
   uchar netwop;
   
   netwop = pBlock->blk.pi.netwop_no;
   if (dbc->pFirstPi == NULL)
   {  // db empty -> insert ver first item
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
   {  // hinten anhaengen

      // Netwop-Verzeigerung
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

      // in Startzeit-Verzeigerung einhaengen
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
   uchar *pTitle;
   uint descrLen;
   uint blockSize, off;
   uint dbCount, dbIdx, actIdx;
   uint anyIdx, piCount;
   uint idx, idx2;
   uchar stream, version;
   bool haveSeries;

   dbmc = dbc->pMergeContext;
   dbCount = dbmc->dbCount;

   version = dbc->pAiBlock->version;
   stream = 1;
   anyIdx = 0xff;
   piCount = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      if (pFoundBlocks[dbIdx] != NULL)
      {
         anyIdx = dbIdx;
         piCount += 1;
         stream &= pFoundBlocks[dbIdx]->stream;
         if (pFoundBlocks[dbIdx]->version != ((pFoundBlocks[dbIdx]->stream == NXTV_STREAM_NO_1) ? dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai.version : dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai.version_swo))
            version = dbc->pAiBlock->version - 1;
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
   descrLen = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_DESCR][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL)
         {
            if (pFoundBlocks[actIdx]->blk.pi.short_info_length > 0)
            {
               descrLen += 2 + strlen(PI_GET_SHORT_INFO(&pFoundBlocks[actIdx]->blk.pi));
            }
            if (pFoundBlocks[actIdx]->blk.pi.long_info_length > 0)
            {
               descrLen += 2 + strlen(PI_GET_LONG_INFO(&pFoundBlocks[actIdx]->blk.pi));
            }
         }
      }
      else
         break;
   }

   blockSize = sizeof(PI_BLOCK) + strlen(pTitle) + 1 + descrLen;
   pMergedBlock = EpgBlockCreate(BLOCK_TYPE_PI, blockSize);
   pMergedBlock->stream     = stream;
   pMergedBlock->version    = version;
   pMergedBlock->mergeCount = piCount;
   pPi = (PI_BLOCK *) &pMergedBlock->blk.pi;  // cast to remove const from pointer

   pPi->block_no          = 0;
   pPi->netwop_no         = dbmc->netwopMap[anyIdx][pFoundBlocks[anyIdx]->blk.pi.netwop_no];
   pPi->start_time        = pFoundBlocks[anyIdx]->blk.pi.start_time;
   pPi->stop_time         = pFoundBlocks[anyIdx]->blk.pi.stop_time;

   pPi->background_ref    = 0;
   pPi->background_reuse  = 0;
   pPi->no_descriptors    = 0;
   pPi->off_descriptors   = 0;

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
            for (idx=0; idx < pOnePi->no_themes && pPi->no_themes < 7; idx++)
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

   pPi->title_length      = strlen(pTitle);
   pPi->short_info_length = ((descrLen > 0) ? (descrLen - 1) : 0);
   pPi->long_info_length  = 0;
   pPi->long_info_type    = EPG_STR_TYPE_TRANSP_LONG;

   off = sizeof(PI_BLOCK);
   pPi->off_title = off;
   strcpy(PI_GET_TITLE(pPi), pTitle);
   off += strlen(pTitle) + 1;

   if (descrLen > 0)
   {
      pPi->off_short_info = off;
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         actIdx = dbmc->max[MERGE_TYPE_DESCR][dbIdx];
         if (actIdx < dbCount)
         {
            if (pFoundBlocks[actIdx] != NULL)
            {
               if (pFoundBlocks[actIdx]->blk.pi.short_info_length > 0)
               {
                  strcpy(PI_GET_STR_BY_OFF(pPi, off), PI_GET_SHORT_INFO(&pFoundBlocks[actIdx]->blk.pi));
                  //strcat(PI_GET_STR_BY_OFF(pPi, off), "\n\n");
                  off += strlen(PI_GET_STR_BY_OFF(pPi, off));
               }
               if (pFoundBlocks[actIdx]->blk.pi.long_info_length > 0)
               {
                  strcpy(PI_GET_STR_BY_OFF(pPi, off), PI_GET_LONG_INFO(&pFoundBlocks[actIdx]->blk.pi));
                  //strcat(PI_GET_STR_BY_OFF(pPi, off), "\n\n");
                  off += strlen(PI_GET_STR_BY_OFF(pPi, off));
               }
            }
         }
         else
            break;
      }
   }
   else
      pPi->off_short_info = 0;
   pPi->off_long_info = 0;

   assert(off <= pMergedBlock->size);

   return pMergedBlock;
}

// ---------------------------------------------------------------------------
// Merge all PI blocks
//
static void EpgDbMergeAllPiBlocks( PDBC dbc )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_BLOCK *pNextBlock[MAX_MERGED_DB_COUNT];
   EPGDB_BLOCK *pFoundBlocks[MAX_MERGED_DB_COUNT];
   EPGDB_BLOCK *pBlock;
   EPGDB_BLOCK *pPrevNetwopBlock[MAX_NETWOP_COUNT];
   time_t firstStartTime, firstStopTime, minStartTime;
   uchar  netwop, minMappedNetwop, firstNetwop;
   uint dbCount, dbIdx;

   dbmc = dbc->pMergeContext;
   dbCount = dbmc->dbCount;
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
         while( (pBlock != NULL) &&
                (pBlock->blk.pi.start_time < minStartTime) )
         {
            pBlock = pBlock->pNextBlock;
         }
         pNextBlock[dbIdx] = pBlock;

         while(pBlock != NULL)
         {
            netwop = dbmc->netwopMap[dbIdx][pBlock->blk.pi.netwop_no];
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

// ---------------------------------------------------------------------------
// Insert a PI block into the merged db
// - called after the block was inserted to its provider's database
// 
void EpgDbMergeInsertPi( PDBC pAcqContext, EPGDB_BLOCK * pNewBlock )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_BLOCK *pFoundBlocks[MAX_MERGED_DB_COUNT];
   EPGDB_BLOCK *pWalk, *pPrev, *pNext;

   if (EpgDbMergeOpenAcqContext(pAcqContext, AI_GET_CNI(&pAcqContext->pAiBlock->blk.ai)))
   {
      dbmc = pUiDbContext->pMergeContext;
      dprintf6("MERGE PI ptr=%lx: dbidx=%d, netwop=%d->%d, blockno=%d, start=%ld\n", (ulong)pNewBlock, dbmc->acqIdx, pNewBlock->blk.pi.netwop_no, dbmc->netwopMap[dbmc->acqIdx][pNewBlock->blk.pi.netwop_no], pNewBlock->blk.pi.block_no, pNewBlock->blk.pi.start_time);

      // find equivalent blocks in all other dbs and check for conflicts with higher-priorized PI
      if ( EpgDbMergeGetPiEquivs(dbmc, pNewBlock, pFoundBlocks) )
      {
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
         PiListBox_DbRecount(pUiDbContext);

         StatsWin_NewPi(pUiDbContext, &pNewBlock->blk.pi, pNewBlock->stream);
      }
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
   uchar *mergeName, *name;

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
// - build netwop mapping tables
//
static void EpgDbMergeAiBlocks( PDBC dbc )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   AI_BLOCK  * pAi, * pTargetAi;
   AI_NETWOP * pNetwops, * pTargetNetwops;
   uint netwop, dbIdx, idx;
   uchar * pServiceName;          // temporarily holds merged service name
   uint cni[MAX_NETWOP_COUNT];    // temporary array to simplify unification
   uint lastCount;                // helper variable to optimize search
   uint netwopCount;              // number of unique netwops in merged db
   uint nameLen;                  // sum of netwop name lengths
   uint blockLen;                 // length of composed AI block
   uint dbCount;

   // determine number of netwops in merged db
   dbmc = dbc->pMergeContext;
   dbCount = dbmc->dbCount;
   nameLen = 0;
   netwopCount = 0;
   lastCount = 0;

   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      pAi = (AI_BLOCK *) &dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai;
      pNetwops = AI_GET_NETWOPS(pAi);
      memset(dbmc->revNetwopMap[dbIdx], 0xff, sizeof(dbmc->revNetwopMap[0]));

      for (netwop=0; (netwop < pAi->netwopCount) && (netwopCount < MAX_NETWOP_COUNT); netwop++)
      {
         // search if CNI is already in list
         // (optimization: do not search in current AI block)
         for (idx=0; idx < lastCount; idx++)
            if (cni[idx] == pNetwops[netwop].cni)
               break;

         if (idx >= lastCount)
         {  // CNI is unique -> append to list
            dbmc->netwopMap[dbIdx][netwop] = netwopCount;
            dbmc->revNetwopMap[dbIdx][netwopCount] = netwop;
            nameLen += strlen(AI_GET_STR_BY_OFF(pAi, pNetwops[netwop].off_name)) + 1;
            cni[netwopCount] = pNetwops[netwop].cni;

            netwopCount += 1;
         }
         else
         {
            dbmc->netwopMap[dbIdx][netwop] = idx;
            dbmc->revNetwopMap[dbIdx][idx] = netwop;
         }
      }
      lastCount = netwopCount;
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

   // init base block
   memcpy(pTargetAi, &dbmc->pDbContext[0]->pAiBlock->blk.ai, sizeof(AI_BLOCK));
   pTargetAi->off_netwops = sizeof(AI_BLOCK);
   pTargetAi->netwopCount = netwopCount;
   pTargetAi->serviceNameLen = strlen(pServiceName);
   pTargetAi->version = 1;
   pTargetAi->version_swo = 1;
   blockLen = sizeof(AI_BLOCK) + netwopCount * sizeof(AI_NETWOP);

   // append service name
   pTargetAi->off_serviceNameStr = blockLen;
   strcpy(AI_GET_STR_BY_OFF(pTargetAi, blockLen), pServiceName);
   blockLen += strlen(pServiceName) + 1;

   // append netwop structs and netwop names
   lastCount = 0;
   pTargetNetwops = AI_GET_NETWOPS(pTargetAi);
   memset(pTargetNetwops, 0, netwopCount * sizeof(AI_NETWOP));

   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      pAi = (AI_BLOCK *) &dbmc->pDbContext[dbIdx]->pAiBlock->blk.ai;
      pNetwops = AI_GET_NETWOPS(pAi);

      for (netwop=0; netwop < pAi->netwopCount; netwop++)
      {
         // skip non-unique netwops
         if (dbmc->netwopMap[dbIdx][netwop] == lastCount)
         {
            pTargetNetwops[lastCount].cni      = pNetwops[netwop].cni;
            pTargetNetwops[lastCount].alphabet = pNetwops[netwop].alphabet;
            pTargetNetwops[lastCount].nameLen  = pNetwops[netwop].nameLen;
            pTargetNetwops[lastCount].off_name = blockLen;
            strcpy(AI_GET_STR_BY_OFF(pTargetAi, blockLen), AI_GET_STR_BY_OFF(pAi, pNetwops[netwop].off_name));
            blockLen += strlen(AI_GET_STR_BY_OFF(pAi, pNetwops[netwop].off_name)) + 1;

            lastCount += 1;
         }
      }
   }
   assert(lastCount == netwopCount);
   xfree(pServiceName);
}

// ---------------------------------------------------------------------------
// Update AI block when an AI in one of the dbs was updated
//
void EpgDbMergeAiUpdate( PDBC pAcqContext, EPGDB_BLOCK * pAiBlock )
{
   AI_BLOCK *pAi;
   uchar old_version, old_version_swo;

   if (EpgDbMergeOpenAcqContext(pAcqContext, AI_GET_CNI(&pAiBlock->blk.ai)))
   {
      pAi = (AI_BLOCK *) &pAiBlock->blk.ai;  //remove const
      old_version     = pAi->version;
      old_version_swo = pAi->version_swo;

      xfree(pUiDbContext->pAiBlock);
      pUiDbContext->pAiBlock = NULL;

      EpgDbMergeAiBlocks(pUiDbContext);

      pAi->version     = old_version;
      pAi->version_swo = old_version_swo;
   }
}

// ---------------------------------------------------------------------------
// Start complete merge
//
EPGDB_CONTEXT * EpgDbMerge( uint dbCount, const uint * pCni, MERGE_ATTRIB_VECTOR_PTR pMax )
{
   EPGDB_CONTEXT * pDbContext;
   EPGDB_MERGE_CONTEXT * pMergeContext;

   if (dbCount > MAX_MERGED_DB_COUNT)
   {
      debug2("EpgDb-Merge: too many dbs %d > %d", dbCount, MAX_MERGED_DB_COUNT);
      dbCount = MAX_MERGED_DB_COUNT;
   }

   // initialize context
   pMergeContext = xmalloc(sizeof(EPGDB_MERGE_CONTEXT));
   memset(pMergeContext, 0, sizeof(EPGDB_MERGE_CONTEXT));

   pMergeContext->dbCount = dbCount;
   pMergeContext->acqIdx  = 0xff;
   memcpy(pMergeContext->cnis, pCni, sizeof(uint) * dbCount);
   memcpy(pMergeContext->max, pMax, sizeof(MERGE_ATTRIB_MATRIX));

   if ( EpgDbMergeOpenDatabases(pMergeContext) )
   {
      // create target database
      pDbContext = EpgDbCreate();

      pDbContext->merged = TRUE;
      pDbContext->pMergeContext = pMergeContext;

      // create AI block
      EpgDbMergeAiBlocks(pDbContext);

      // merge all PI from all databases into the new one
      EpgDbMergeAllPiBlocks(pDbContext);

      // close the databases
      EpgDbMergeCloseDatabases(pMergeContext);
   }
   else
   {
      pDbContext = NULL;
      xfree(pMergeContext);
   }

   return pDbContext;
}

