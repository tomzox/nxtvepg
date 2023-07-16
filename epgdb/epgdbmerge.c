/*
 *  Nextview EPG database merging
 *
 *  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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
 *    databases; 2 - merge all PI of all source databases; 3 - replace all
 *    PI blocks of networks merged from the given providers.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <stdlib.h>
#include <string.h>
#include <time.h>

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
// Merge equivalent PI blocks from different databases
//
static EPGDB_PI_BLOCK * EpgDbMergePiBlocks( PDBC dbc, EPGDB_PI_BLOCK **pFoundBlocks )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_PI_BLOCK * pMergedBlock;
   const PI_BLOCK * pOnePi;
   PI_BLOCK       * pPi;
   EPGDB_MERGE_SRC  piDesc[MAX_MERGED_DB_COUNT];
   const char *pTitle;
   uint descTextLen;
   uint descTextCount;
   uint blockSize, off;
   uint dbCount, dbIdx, actIdx;
   uint firstIdx, piCount;
   uint idx, idx2;
   uchar version;
   time_t mtime;

   dbmc = (EPGDB_MERGE_CONTEXT*) dbc->pMergeContext;
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
         if (pFoundBlocks[dbIdx]->version != dbmc->prov[dbIdx].pDbContext->pAiBlock->ai.version)
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
            pTitle = PI_GET_TITLE(&pFoundBlocks[actIdx]->pi);
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
   descTextCount = 0;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_DESCR][dbIdx];
      if (actIdx < dbCount)
      {
         if (pFoundBlocks[actIdx] != NULL)
         {
            if (PI_HAS_DESC_TEXT(&pFoundBlocks[actIdx]->pi))
            {
               descTextLen += 3 + strlen(PI_GET_DESC_TEXT(&pFoundBlocks[actIdx]->pi));
               descTextCount += 1;
            }
         }
      }
      else
         break;
   }

   blockSize = sizeof(PI_BLOCK) + strlen(pTitle) + 1 + descTextLen + (piCount * sizeof(EPGDB_MERGE_SRC));
   pMergedBlock = EpgBlockCreatePi(blockSize, mtime);
   pMergedBlock->version    = version;
   pPi = (PI_BLOCK *) &pMergedBlock->pi;  // cast to remove const from pointer
   off = sizeof(PI_BLOCK);

   pPi->netwop_no         = dbmc->prov[firstIdx].netwopMap[pFoundBlocks[firstIdx]->pi.netwop_no];
   pPi->start_time        = pFoundBlocks[firstIdx]->pi.start_time;
   pPi->stop_time         = pFoundBlocks[firstIdx]->pi.stop_time;

   pPi->feature_flags = 0;
   // feature sound
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_SOUND][dbIdx];
      if (actIdx < dbCount)
      {
         const uint16_t mask = PI_FEATURE_SOUND_MASK;

         if ((pFoundBlocks[actIdx] != NULL) &&
             ((pFoundBlocks[actIdx]->pi.feature_flags & mask) != PI_FEATURE_SOUND_UNKNOWN))
         {
            pPi->feature_flags |= pFoundBlocks[actIdx]->pi.feature_flags & mask;
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
            const uint16_t mask = PI_FEATURE_VIDEO_NONE | PI_FEATURE_FMT_WIDE |
                                  PI_FEATURE_VIDEO_HD | PI_FEATURE_VIDEO_BW;
            pPi->feature_flags |= pFoundBlocks[actIdx]->pi.feature_flags & mask;
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
            const uint16_t mask = PI_FEATURE_REPEAT | PI_FEATURE_LAST_REP |
                                  PI_FEATURE_PREMIERE | PI_FEATURE_NEW;

            pPi->feature_flags |= pFoundBlocks[actIdx]->pi.feature_flags & mask;
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
         const uint16_t mask = PI_FEATURE_SUBTITLE_MASK;

         if ((pFoundBlocks[actIdx] != NULL)  &&
             ((pFoundBlocks[actIdx]->pi.feature_flags & mask) != PI_FEATURE_SUBTITLE_NONE))
         {
            pPi->feature_flags |= pFoundBlocks[actIdx]->pi.feature_flags & mask;
            break;
         }
      }
      else
         break;
   }

   // parental rating
   pPi->parental_rating = PI_PARENTAL_UNDEFINED;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_PARENTAL][dbIdx];
      if (actIdx < dbCount)
      {
         if ((pFoundBlocks[actIdx] != NULL) &&
             (pFoundBlocks[actIdx]->pi.parental_rating != PI_PARENTAL_UNDEFINED))
         {
            pPi->parental_rating = pFoundBlocks[actIdx]->pi.parental_rating;
            break;
         }
      }
      else
         break;
   }

   // editorial rating
   pPi->editorial_rating = PI_EDITORIAL_UNDEFINED;
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      actIdx = dbmc->max[MERGE_TYPE_EDITORIAL][dbIdx];
      if (actIdx < dbCount)
      {
         if ((pFoundBlocks[actIdx] != NULL) &&
             (pFoundBlocks[actIdx]->pi.editorial_rating != PI_EDITORIAL_UNDEFINED))
         {
            pPi->editorial_rating = pFoundBlocks[actIdx]->pi.editorial_rating;
            pPi->editorial_max_val = pFoundBlocks[actIdx]->pi.editorial_max_val;
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
            pOnePi = &pFoundBlocks[actIdx]->pi;
            for (idx=0; (idx < pOnePi->no_themes) && (pPi->no_themes < PI_MAX_THEME_COUNT); idx++)
            {
               uint mappedTheme = dbmc->prov[dbIdx].themeIdMap[pOnePi->themes[idx]];

               for (idx2=0; idx2 < pPi->no_themes; idx2++)
                  if (pPi->themes[idx2] == mappedTheme)
                     break;
               if (idx2 >= pPi->no_themes)
               {  // theme is not in the list yet
                  pPi->themes[pPi->no_themes++] = mappedTheme;
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
         if ((pFoundBlocks[actIdx] != NULL) && (pFoundBlocks[actIdx]->pi.pil != 0x07FFF))
         {
            pPi->pil = pFoundBlocks[actIdx]->pi.pil;
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
               if (PI_HAS_DESC_TEXT(&pFoundBlocks[actIdx]->pi))
               {
                  // cast here and below to remove const from macro results
                  strcpy((char*)PI_GET_STR_BY_OFF(pPi, off), PI_GET_DESC_TEXT(&pFoundBlocks[actIdx]->pi));
                  off += strlen(PI_GET_STR_BY_OFF(pPi, off));

                  descTextCount -= 1;
                  if (descTextCount > 0)
                  {  // at least one more description text following: append separator
                     ((char *) PI_GET_STR_BY_OFF(pPi, off))[0] = EPG_DB_MERGE_DESC_TEXT_SEP;
                     ((char *) PI_GET_STR_BY_OFF(pPi, off))[1] = 0;
                     off += 1;
                  }
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
#if 0 // TODO adapt to UTF
         const char * p1, * p2, *pe, *ps;

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
              ispunct(*ps) || isspace(*ps) ||
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
#endif
      }
      if (result == FALSE)
         dprintf6("        CMP FAIL   '%s' %s -- %d,%d,%d,%ld\n", PI_GET_TITLE(pNewPi), EpgDbMergePrintTime(pNewPi), ovl, rt1, rt2, labs(pNewPi->start_time - pRefPi->start_time));
   }
   return result;
}

// ---------------------------------------------------------------------------
// Merge all PI blocks of a single network
//
static void EpgDbMergeNetworkPi( PDBC dbc, uint netwop, EPGDB_PI_BLOCK **ppFirstNetwopBlock )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_PI_BLOCK *pNextBlock[MAX_MERGED_DB_COUNT];
   EPGDB_PI_BLOCK *pFoundBlocks[MAX_MERGED_DB_COUNT];
   EPGDB_PI_BLOCK *pPrevMerged;
   EPGDB_PI_BLOCK *pBlock;
   time_t firstStartTime, firstStopTime;
   uint dbCount, dbIdx, firstIdx;
   bool conflict;

   dbmc = (EPGDB_MERGE_CONTEXT*) dbc->pMergeContext;
   dbCount = dbmc->dbCount;
   memset(pFoundBlocks, 0, sizeof(pFoundBlocks));

   // get first block of this network from each DB
   for (dbIdx=0; dbIdx < dbCount; dbIdx++)
   {
      uint dbNet = dbmc->prov[dbIdx].revNetwopMap[netwop];
      if (dbNet != INVALID_NETWOP_IDX)
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
                 (pBlock->pi.start_time < firstStartTime) )
            {
               firstStartTime = pBlock->pi.start_time;
               firstIdx = dbIdx;
            }
         }
      }

      // exit loop when all blocks are processed
      if (firstStartTime == 0)
         break;

      // identify which blocks can be merged
      dprintf4("MERGE: DB#%d net#%d '%s' %s\n", firstIdx, netwop, PI_GET_TITLE(&pNextBlock[firstIdx]->pi), EpgDbMergePrintTime(&pNextBlock[firstIdx]->pi));
      pFoundBlocks[firstIdx] = pNextBlock[firstIdx];
      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         pBlock = pNextBlock[dbIdx];
         if ( (pBlock != NULL) &&
              (pFoundBlocks[dbIdx] == NULL) &&
              EpgDbMerge_PiMatch(&pFoundBlocks[firstIdx]->pi, &pBlock->pi) )
         {
            dprintf4("       DB#%d net#%d '%s' %s\n", dbIdx, netwop, PI_GET_TITLE(&pBlock->pi), EpgDbMergePrintTime(&pBlock->pi));
            pFoundBlocks[dbIdx] = pBlock;

            if (firstIdx > dbIdx)
               firstIdx = dbIdx;
         }
      }

      // check for overlapping with previous merged block of same network
      // (note: doing this after PI matching because start time may change)
      if ( (pPrevMerged != NULL) &&
           (pFoundBlocks[firstIdx]->pi.start_time < pPrevMerged->pi.stop_time) )
      {
         dprintf1("-----  OVERLAP prev. stop %s\n", EpgDbMergePrintTime(&pPrevMerged->pi));
      }
      else
      {
         // check all databases of higher prio for conflicts with following blocks
         firstStopTime  = pFoundBlocks[firstIdx]->pi.stop_time;
         conflict = FALSE;
         for (dbIdx=0; dbIdx < firstIdx; dbIdx++)
         {
            if (pNextBlock[dbIdx] != NULL)
            {
               if (pNextBlock[dbIdx]->pi.start_time < firstStopTime)
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
   EPGDB_PI_BLOCK **pFirstNetwopBlock;
   uint netwop, netCount;

   netCount = dbc->pAiBlock->ai.netwopCount;
   pFirstNetwopBlock = (EPGDB_PI_BLOCK**) xmalloc(sizeof(pFirstNetwopBlock[0]) * netCount);
   memset(pFirstNetwopBlock, 0, sizeof(pFirstNetwopBlock[0]) * netCount);

   // loop across target networks: merge networks separately
   for (netwop = 0; netwop < netCount; netwop++)
   {
      EpgDbMergeNetworkPi(dbc, netwop, &pFirstNetwopBlock[netwop]);
   }

   // combine networks into a single database
   EpgDbMergeLinkNetworkPi(dbc, pFirstNetwopBlock);

   xfree(pFirstNetwopBlock);
   assert(EpgDbCheckChains(dbc));
}

// ---------------------------------------------------------------------------
// Replace all PI blocks of networks merged from the given providers
//
void EpgDbMergeUpdateNetworks( EPGDB_CONTEXT * pDbContext, uint provCount, const uint * pProvCni )
{
   EPGDB_MERGE_CONTEXT * dbmc;
   EPGDB_PI_BLOCK **pFirstNetwopBlock;
   EPGDB_PI_BLOCK *pMergedPi;
   EPGDB_PI_BLOCK *pBlock, *pNext;
   const AI_BLOCK * pAi;
   bool * needMerge;
   uint netCount;
   uint provIdx;
   uint dbIdx;
   uint netIdx;
   uint netwop;

   assert(pDbContext->merged);
   dbmc = (EPGDB_MERGE_CONTEXT*) pDbContext->pMergeContext;

   needMerge = (bool*) xmalloc(sizeof(needMerge[0]) * dbmc->netwopCount);
   memset(needMerge, 0, sizeof(needMerge[0]));

   for (provIdx = 0; provIdx < provCount; ++provIdx)
   {
      // get the index of the db in the merge context
      for (dbIdx = 0; dbIdx < dbmc->dbCount; dbIdx++)
         if (dbmc->prov[dbIdx].provCni == pProvCni[provIdx])
            break;

      if (dbIdx < dbmc->dbCount)
      {
         pAi = (AI_BLOCK *) &dbmc->prov[dbIdx].pDbContext->pAiBlock->ai;
         for (netIdx = 0; netIdx < pAi->netwopCount; ++netIdx)
         {
            netwop = dbmc->prov[dbIdx].netwopMap[netIdx];
            if (netwop < dbmc->netwopCount)
            {
               dprintf3("EpgDbMerge-UpdateNetworks: prov:%X net#:%d update merged net#:%d\n", pProvCni[provIdx], netIdx, netwop);
               needMerge[netwop] = TRUE;
            }
         }
      }
   }

   netCount = pDbContext->pAiBlock->ai.netwopCount;
   pFirstNetwopBlock = (EPGDB_PI_BLOCK**) xmalloc(sizeof(pFirstNetwopBlock[0]) * netCount);
   memset(pFirstNetwopBlock, 0, sizeof(pFirstNetwopBlock[0]) * netCount);

   for (netwop = 0; netwop < netCount; ++netwop)
   {
      if (needMerge[netwop])
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

         // replace with chained list of newly merged blocks
         pFirstNetwopBlock[netwop] = pMergedPi;
      }
      else  // keep PI of this network unchanged
         pFirstNetwopBlock[netwop] = pDbContext->pFirstNetwopPi[netwop];
   }

   // rebuild link chains between all blocks
   EpgDbMergeLinkNetworkPi(pDbContext, pFirstNetwopBlock);

   xfree(pFirstNetwopBlock);
   assert(EpgDbCheckChains(pDbContext));

   // update GUI
   if (pDbContext->pPiAcqCb != NULL)
      pDbContext->pPiAcqCb(pDbContext, EPGDB_PI_RESYNC, NULL, NULL);
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
   EPGDB_PI_BLOCK * pBlock;
   EPGDB_MERGE_SRC * pDesc;
   uint idx;

   pBlock = dbc->pFirstPi;
   while (pBlock != NULL)
   {
      pDesc = (EPGDB_MERGE_SRC *) PI_GET_DESCRIPTORS(&pBlock->pi);  // cast to remove const
      for (idx = pBlock->pi.no_descriptors; idx > 0; idx--, pDesc++)
      {
         if (*pDesc == dbIdx)
         {
            dprintf2("EpgDbMerge-ResetPiVersion: net=%d start=%ld\n", pBlock->pi.netwop_no, pBlock->pi.start_time);
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
      pAi = (AI_BLOCK *) &dbmc->prov[dbIdx].pDbContext->pAiBlock->ai;
      name = AI_GET_SERVICENAME(pAi);
      // limit name length
      size_t tlen = strlen(name);
      if ((len + tlen >= MAX_SERVICE_NAME_LEN) && (dbIdx != 0))
         break;
      len += tlen + 2;
   }
   concatCnt = dbIdx;

   // allocate the memory
   mergeName = (char*) xmalloc(len);
   strcpy(mergeName, mergedServiceName);

   // concatenate the names
   for (dbIdx=0; dbIdx < concatCnt; dbIdx++)
   {
      pAi = (AI_BLOCK *) &dbmc->prov[dbIdx].pDbContext->pAiBlock->ai;
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
   uchar * netOrigIdx;
   char * pServiceName;           // temporarily holds merged service name
   uint serviceNameLen;
   uint nameLen;                  // sum of netwop name lengths
   uint blockLen;                 // length of composed AI block
   uint dbCount;

   // determine number of netwops in merged db
   dbmc = (EPGDB_MERGE_CONTEXT*) dbc->pMergeContext;
   dbCount = dbmc->dbCount;
   nameLen = 0;

   netOrigIdx = (uchar*) xmalloc(sizeof(netOrigIdx[0]) * netwopCount);

   for (idx = 0; idx < netwopCount; idx++)
   {
      netOrigIdx[idx] = 0xff;

      for (dbIdx=0; dbIdx < dbCount; dbIdx++)
      {
         pAi = &dbmc->prov[dbIdx].pDbContext->pAiBlock->ai;
         netwop = dbmc->prov[dbIdx].revNetwopMap[idx];
         if (netwop != INVALID_NETWOP_IDX)
         {
            if (netOrigIdx[idx] == 0xff)
            {
               netOrigIdx[idx] = dbIdx;
               nameLen += strlen(AI_GET_NETWOP_NAME(pAi, netwop)) + 1;
            }
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
   serviceNameLen = strlen(pServiceName) + 1;

   // allocate memory for new AI block
   blockLen = sizeof(AI_BLOCK) +
              netwopCount * sizeof(AI_NETWOP) +
              serviceNameLen +
              nameLen;
   dbc->pAiBlock = EpgBlockCreateAi(blockLen, mtimeMerge);
   pTargetAi = (AI_BLOCK *) &dbc->pAiBlock->ai;

   // init base struct
   memcpy(pTargetAi, &dbmc->prov[0].pDbContext->pAiBlock->ai, sizeof(AI_BLOCK));
   pTargetAi->off_netwops = sizeof(AI_BLOCK);
   pTargetAi->netwopCount = netwopCount;
   pTargetAi->version     = 1;
   blockLen = sizeof(AI_BLOCK) + netwopCount * sizeof(AI_NETWOP);

   // append service name
   pTargetAi->off_serviceNameStr = blockLen;
   memcpy((char*)pTargetAi + blockLen, pServiceName, serviceNameLen);
   blockLen += serviceNameLen;

   // append netwop structs and netwop names
   pTargetNetwops = (AI_NETWOP *) AI_GET_NETWOPS(pTargetAi);
   memset(pTargetNetwops, 0, netwopCount * sizeof(AI_NETWOP));

   for (netwop=0; netwop < netwopCount; netwop++)
   {
      dbIdx = netOrigIdx[netwop];
      if (dbIdx != 0xff)
      {
         pAi = (AI_BLOCK *) &dbmc->prov[dbIdx].pDbContext->pAiBlock->ai;
         idx = dbmc->prov[dbIdx].revNetwopMap[netwop];
         if (idx < pAi->netwopCount)
         {
            pNetwops = AI_GET_NETWOP_N(pAi, idx);
            ifdebug4(AI_GET_NET_CNI(pNetwops) != pNetwopList[netwop], "EpgDb-MergeAiBlocks: mismatch of CNIs in netwop #%d: 0x%04X!=0x%04X (%s)", idx, AI_GET_NET_CNI(pNetwops), pNetwopList[netwop], AI_GET_STR_BY_OFF(pAi, pNetwops->off_name));

            pTargetNetwops[netwop].netCni   = AI_GET_NET_CNI(pNetwops);
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
   xfree(netOrigIdx);
}

