/*
 *  Nextview EPG block database search filters
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
 *    Provides both filter control and match functions for selected
 *    access to the EPG database. The filter status is kept in a
 *    structure called FILTER_CONTEXT. If a pointer to such a struct
 *    is given to any of the PI search functions in the database
 *    interface module, the match function will be used to return
 *    only PI blocks that match the filter settings.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgdbfil.c,v 1.47 2003/09/23 19:36:32 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <string.h>
#include <ctype.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgvbi/ttxdecode.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"


// type definition for netwop match cache
typedef enum
{
   FILT_MATCH_OK,
   FILT_MATCH_FAIL,
   FILT_MATCH_FAIL_PRE
} FILT_MATCH;

//internal shortcuts
typedef const EPGDB_CONTEXT * CPDBC;
typedef       EPGDB_CONTEXT * PDBC;


// ---------------------------------------------------------------------------
// Create and initialize a filter context
// - when the context is no longer used, it has to be destroyed (i.e. freed)
//
FILTER_CONTEXT * EpgDbFilterCreateContext( void )
{
   FILTER_CONTEXT * fc;

   fc = (FILTER_CONTEXT *) xmalloc(sizeof(*fc));
   memset(fc, 0, sizeof(*fc));
   fc->pFocus = &fc->act;
   fc->act.forkCombMode = FILTER_FORK_AND;

   return fc;
}

// ---------------------------------------------------------------------------
// Copy a filter parameter chain: make a linked copy of all linked elements
//
static void * EpgDbFilterCopySubContexts( void * pCtx )
{
   EPGDB_FILT_SUBCTX_GENERIC   * pFirst;
   EPGDB_FILT_SUBCTX_GENERIC   * pSrcWalk;
   EPGDB_FILT_SUBCTX_GENERIC  ** ppDestWalk;

   pSrcWalk   = pCtx;
   pFirst     = NULL;
   ppDestWalk = NULL;

   while (pSrcWalk != NULL)
   {
      assert(pSrcWalk->elem_size != 0);

      if (ppDestWalk == NULL)
      {  // first copied element
         pFirst = xmalloc(pSrcWalk->elem_size);
         memcpy(pFirst, pSrcWalk, pSrcWalk->elem_size);
         ppDestWalk = &(pFirst->pNext);
      }
      else
      {
         *ppDestWalk = xmalloc(pSrcWalk->elem_size);
         memcpy(*ppDestWalk, pSrcWalk, pSrcWalk->elem_size);
         ppDestWalk = &(*ppDestWalk)->pNext;
      }
      pSrcWalk = pSrcWalk->pNext;
   }
   return pFirst;
}

// ---------------------------------------------------------------------------
// Copy a filter context
// - intended for checks that require modifications of the filter mask
// - when the context is no longer used, it has to be destroyed (i.e. freed)
//
FILTER_CONTEXT * EpgDbFilterCopyContext( const FILTER_CONTEXT * fc )
{
   FILTER_CONTEXT * newfc;

   if (fc != NULL)
   {
      newfc = (FILTER_CONTEXT *) xmalloc(sizeof(*fc));
      memcpy(newfc, fc, sizeof(*fc));

      // do not copy forked contexts
      newfc->act.pNext = NULL;
      newfc->pFocus    = &newfc->act;

      // do not copy custom filter parameters (XXX would need custom copy function)
      newfc->act.pCustomArg          = NULL;
      newfc->act.pCustomFilterFunc   = NULL;
      newfc->act.pCustomDestroyFunc  = NULL;

      // copy filter parameter chain
      newfc->act.pSubStrCtx = EpgDbFilterCopySubContexts(fc->act.pSubStrCtx);

      if (fc->act.pSeriesFilterMatrix != NULL)
      {
         newfc->act.pSeriesFilterMatrix = xmalloc(sizeof(*newfc->act.pSeriesFilterMatrix));
         memcpy(newfc->act.pSeriesFilterMatrix, fc->act.pSeriesFilterMatrix, sizeof(*newfc->act.pSeriesFilterMatrix));
      }
      if (fc->act.pLangDescrTable != NULL)
      {
         newfc->act.pLangDescrTable = xmalloc(sizeof(*newfc->act.pLangDescrTable));
         memcpy(newfc->act.pLangDescrTable, fc->act.pLangDescrTable, sizeof(*newfc->act.pLangDescrTable));
      }
      if (fc->act.pSubtDescrTable != NULL)
      {
         newfc->act.pSubtDescrTable = xmalloc(sizeof(*newfc->act.pSubtDescrTable));
         memcpy(newfc->act.pSubtDescrTable, fc->act.pSubtDescrTable, sizeof(*newfc->act.pSubtDescrTable));
      }
   }
   else
   {
      fatal0("EpgDbFilter-CopyContext: illegal NULL ptr param");
      newfc = NULL;
   }

   return newfc;
}

// ---------------------------------------------------------------------------
// Destroy all elements in a filter parameter chain
//
static void EpgDbFilterDestroyParamChain( void * pCtx )
{
   EPGDB_FILT_SUBCTX_GENERIC * pWalk;
   EPGDB_FILT_SUBCTX_GENERIC * pNext;

   pWalk = pCtx;
   while (pWalk != NULL)
   {
      pNext = pWalk->pNext;
      xfree(pWalk);
      pWalk = pNext;
   }
}

// ---------------------------------------------------------------------------
// Destroy a filter context chain
//
static void EpgDbFilterDestroyAct( FILTER_CONTEXT * fc, FILTER_CTX_ACT * fc_act )
{
   FILTER_CTX_ACT  * fc_next;

   do
   {
      EpgDbFilterDestroyParamChain(fc_act->pSubStrCtx);

      // free dynamically allocated filter arrays
      if (fc_act->pSeriesFilterMatrix != NULL)
         xfree(fc_act->pSeriesFilterMatrix);
      if (fc_act->pSubtDescrTable != NULL)
         xfree(fc_act->pSubtDescrTable);
      if (fc_act->pLangDescrTable != NULL)
         xfree(fc_act->pLangDescrTable);
      if ( (fc_act->pCustomArg != NULL) &&
           (fc_act->pCustomDestroyFunc != NULL) )
         fc_act->pCustomDestroyFunc(fc_act->pCustomArg);

      fc_next = fc_act->pNext;
      if (fc_act != &fc->act)
         xfree(fc_act);
      fc_act = fc_next;
   }
   while (fc_act != NULL);
}

// ---------------------------------------------------------------------------
// Destroy a filter context: free memory
//
void EpgDbFilterDestroyContext( FILTER_CONTEXT * fc )
{
   if (fc != NULL)
   {
      // destroy first and all forked parameter sets
      EpgDbFilterDestroyAct(fc, &fc->act);

      // destroy pre-filter parameter set
      xfree(fc);
   }
   else
      fatal0("EpgDbFilter-DestroyContext: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Forks a new filter sub-context to implement an "OR" of filters
// - subsequent "set" calls will modify the forked context until a new fork
//   of the fork is closed
// - tag can be used to allow removing this specific sub-context at a later time
//
void EpgDbFilterFork( FILTER_CONTEXT * fc, uint combMode, sint tag )
{
   FILTER_CTX_ACT  * pNew;
   FILTER_CTX_ACT  * fc_act;

   assert((combMode == FILTER_FORK_OR) || (combMode == FILTER_FORK_AND));

   if (tag != -1)
   {  // search context chain for the given tag
      fc_act = fc->act.pNext;
      while (fc_act != NULL)
      {
         if (fc_act->forkTag == tag)
         {  // sub-context with given same tag is already defined
            // -> destroy it (comb mode might have changed, and hence chain location)
            debug1("EpgDbFilter-Fork: tag %d already in use - destroy", tag);
            EpgDbFilterDestroyFork(fc, tag);
            break;
         }
         fc_act = fc_act->pNext;
      }
   }

   pNew = xmalloc(sizeof(FILTER_CTX_ACT));
   memset(pNew, 0, sizeof(*pNew));
   pNew->elem_size    = sizeof(FILTER_CTX_ACT);
   pNew->forkCombMode = combMode;
   pNew->forkTag      = tag;

   fc_act = fc->act.pNext;
   if ((fc_act != NULL) && (combMode == FILTER_FORK_OR))
   {
      // OR contexts are chained behind all AND contexts -> search first OR
      while ( (fc_act->forkCombMode != FILTER_FORK_OR) &&
              (fc_act->pNext != NULL) )
      {
         fc_act = fc_act->pNext;
      }
      pNew->pNext = fc_act->pNext;
      fc_act->pNext = pNew;
   }
   else
   {  // AND or first sub-context in chain -> link directly to primary context
      pNew->pNext = fc->act.pNext;
      fc->act.pNext = pNew;
   }

   // apply subsequent filter modifications to the new sub-context
   fc->pFocus = pNew;
}

// ---------------------------------------------------------------------------
// Switch target of filter "set" calls back to the main context
//
void EpgDbFilterCloseFork( FILTER_CONTEXT * fc )
{
   fc->pFocus = &fc->act;
}

// ---------------------------------------------------------------------------
// Free memory for "forked" parameter sets
// - not necessary to call this before normal destroy, only for a filter reset
//
void EpgDbFilterDestroyAllForks( FILTER_CONTEXT * fc )
{
   if (fc->act.pNext != NULL)
   {
      EpgDbFilterDestroyAct(fc, fc->act.pNext);
      fc->act.pNext = NULL;
   }
}

// ---------------------------------------------------------------------------
// Destroy the "forked" sub-context with the given tag
//
void EpgDbFilterDestroyFork( FILTER_CONTEXT * fc, sint tag )
{
   FILTER_CTX_ACT  * fc_act;
   FILTER_CTX_ACT  * pLast;

   if (tag != -1)
   {
      pLast  = &fc->act;
      fc_act = fc->act.pNext;

      while (fc_act != NULL)
      {
         if (fc_act->forkTag == tag)
         {  // found context with the given tag
            // unlink it from the chain
            pLast->pNext = fc_act->pNext;
            // free memory (including param chains)
            fc_act->pNext = NULL;
            EpgDbFilterDestroyAct(fc, fc_act);
            break;
         }
         else
         {
            pLast = fc_act;
            fc_act = fc_act->pNext;
            ifdebug1(fc_act == NULL, "EpgDbFilter-DestroyFork: tag %d not found", tag);
         }
      }
   }
   else
      debug0("EpgDbFilter-DestroyFork: invalid use of wildcard tag -1");
}

// ---------------------------------------------------------------------------
// Query which netwops are filtered out after combining & inverting
// - intended to be used by the GUI to determine the NETWOP_PRE2 filter as a
//   sub-set of the full set of matching netwops
// - fill a table with a boolean for every network index which tells the match
//   function if a given network matches the current filter settings
// - network matches depend on the network pre-filter, network filter if
//   enabled and possibly network filter inversion if enabled; the cache must
//   be invalidated if any of those parameters change
// - note: the cache does not include the NETWOP_PRE2 filter because that one
//   is a separate layer which is not affected by inversion and is intended to
//   be used by the GUI to limit network matches to a sub-set of networks
//
void EpgDbFilterGetNetwopFilter( FILTER_CONTEXT *fc, uchar * pNetFilter, uint count )
{
   FILTER_CTX_ACT  * fc_act;
   FILT_MATCH  matchCode;
   bool  fail, invert;
   uchar netwop;

   for (netwop = 0; (netwop < MAX_NETWOP_COUNT) && (netwop < count); netwop++)
   {
      // XXX only the first context is considered
      fc_act = &fc->act;

      invert = ((fc_act->invertedFilters & FILTER_NETWOP) != FALSE);

      if (fc_act->enabledFilters & FILTER_NETWOP)
      {
         fail = (fc_act->netwopFilterField[netwop] == FALSE);

         if (fail ^ invert)
         {
            // if the network is also excluded by pre-filter, it's excluded from global inversion
            if ( (fc->enabledPreFilters & FILTER_NETWOP_PRE) &&
                 (fc->netwopPreFilter1[netwop] == FALSE) )
               matchCode = FILT_MATCH_FAIL_PRE;
            else
               matchCode = FILT_MATCH_FAIL;
         }
         else
         {
            // do not include pre-filtered netwops with inverted network selection
            if ( invert && (fc->enabledPreFilters & FILTER_NETWOP_PRE) &&
                 (fc->netwopPreFilter1[netwop] == FALSE) )
               matchCode = FILT_MATCH_FAIL_PRE;
            else
               matchCode = FILT_MATCH_OK;
         }
      }
      else if (fc->enabledPreFilters & FILTER_NETWOP_PRE)
      {  // netwop pre-filter is only activated when netwop is unused
         // this way also netwops outside of the prefilter can be explicitly requested, e.g. by a NI menu
         if (fc->netwopPreFilter1[netwop] == FALSE)
            matchCode = FILT_MATCH_FAIL_PRE;
         else
            matchCode = FILT_MATCH_OK;
      }
      else
         matchCode = FILT_MATCH_OK;

      pNetFilter[netwop] =  (matchCode == FILT_MATCH_OK) ||
                           ((matchCode == FILT_MATCH_FAIL) && (fc->act.enabledFilters & FILTER_INVERT));
   }
}

// ---------------------------------------------------------------------------
// Reset the theme filter state
// - the theme filter is divided into 8 classes (see ETS 303 707, chp. 11.12.4)
//   during the match of a PI into the theme filters, a logical AND is
//   performed across all classes; inside a class a logical OR is performed
// - i.e. a PI has to match each enabled class; a class is matched if the
//   PI has at least one theme attribute of that class
//
uchar EpgDbFilterInitThemes( FILTER_CONTEXT *fc, uchar themeClassBitField )
{
   uint index;

   if (themeClassBitField == 0xff)
   {  // clear all classes
      memset(fc->pFocus->themeFilterField, 0, sizeof(fc->pFocus->themeFilterField));
      fc->pFocus->usedThemeClasses = 0;
   }
   else
   {  // clear the setting of selected classes only
      for (index=0; index < 256; index++)
      {
         fc->pFocus->themeFilterField[index] &= ~ themeClassBitField;
      }
      fc->pFocus->usedThemeClasses &= ~ themeClassBitField;
   }

   return fc->pFocus->usedThemeClasses;
}

// ---------------------------------------------------------------------------
// Assign a range of theme indices to a class
//
void EpgDbFilterSetThemes( FILTER_CONTEXT *fc, uchar firstTheme, uchar lastTheme, uchar themeClassBitField )
{
   uint index;
   
   assert(themeClassBitField != 0);
   assert(firstTheme <= lastTheme);

   for (index = firstTheme; index <= lastTheme; index++)
   {
      fc->pFocus->themeFilterField[index] |= themeClassBitField;
   }
   fc->pFocus->usedThemeClasses |= themeClassBitField;
}

// ---------------------------------------------------------------------------
// Reset the series filter state
//
void EpgDbFilterInitSeries( FILTER_CONTEXT *fc )
{
   if (fc->pFocus->pSeriesFilterMatrix == NULL)
      fc->pFocus->pSeriesFilterMatrix = xmalloc(sizeof(*fc->pFocus->pSeriesFilterMatrix));

   memset(fc->pFocus->pSeriesFilterMatrix, 0, sizeof(*fc->pFocus->pSeriesFilterMatrix));
}

// ---------------------------------------------------------------------------
// Enable one programme in the series filter matrix
//
void EpgDbFilterSetSeries( FILTER_CONTEXT *fc, uchar netwop, uchar series, bool enable )
{
   if (fc->pFocus->pSeriesFilterMatrix != NULL)
   {
      if ((netwop < MAX_NETWOP_COUNT) && (series > 0x80))
      {
         (*fc->pFocus->pSeriesFilterMatrix)[netwop][series - 0x80] = enable;
      }
      else
         fatal2("EpgDbFilter-SetSeries: illegal parameters: net=%d, series=%d", netwop, series);
   }
   else
      fatal0("EpgDbFilter-SetSeries: series matrix not initialized");
}

// ---------------------------------------------------------------------------
// Reset the sorting criteria filter state
// - the meaning of sorting criteria is not fixed by the ETSI spec
// - it's implicitly defined e.g. by use in NI menus
//
uchar EpgDbFilterInitSortCrit( FILTER_CONTEXT *fc, uchar sortCritClassBitField )
{
   uint index;

   if (sortCritClassBitField == 0xff)
   {  // clear all classes
      memset(fc->pFocus->sortCritFilterField, 0, sizeof(fc->pFocus->sortCritFilterField));
      fc->pFocus->usedSortCritClasses = 0;
   }
   else
   {  // clear the setting of selected classes only
      for (index=0; index < 256; index++)
      {
         fc->pFocus->sortCritFilterField[index] &= ~ sortCritClassBitField;
      }
      fc->pFocus->usedSortCritClasses &= ~ sortCritClassBitField;
   }

   return fc->pFocus->usedSortCritClasses;
}

// ---------------------------------------------------------------------------
// Assign a range of sorting criteria to a class
//
void EpgDbFilterSetSortCrit( FILTER_CONTEXT *fc, uchar firstSortCrit, uchar lastSortCrit, uchar sortCritClassBitField )
{
   uint index;
   
   assert(sortCritClassBitField != 0);
   assert(firstSortCrit <= lastSortCrit);

   for (index = firstSortCrit; index <= lastSortCrit; index++)
   {
      fc->pFocus->sortCritFilterField[index] |= sortCritClassBitField;
   }
   fc->pFocus->usedSortCritClasses |= sortCritClassBitField;
}

// ---------------------------------------------------------------------------
// Set the value for the parental rating filter (see ETS 300 707, Annex F.1)
// - the value 0 stands for "not rated" and should not be used
// - the value 1 stands for "any rated programme"
// - the values 2...8 stand for "ok above n*2 years"
// - the values 9...15 are reserved; 16 and up are invalid b/c PI rating has only 4 bit
//
void EpgDbFilterSetParentalRating( FILTER_CONTEXT *fc, uchar newParentalRating )
{
   ifdebug0(newParentalRating == 0, "EpgDbFilter-SetParentalRating: WARNING: setting parental rating 0");
   ifdebug1(newParentalRating > 8, "EpgDbFilter-SetParentalRating: WARNING: setting illegal parental rating %d", newParentalRating);

   fc->pFocus->parentalRating = newParentalRating;
}

// ---------------------------------------------------------------------------
// Set the value for the editorial rating filter
// - the value 0 stands for "not rated" and should not be used
// - values 8 and up are invalid b/c PI editorial rating uses only 3 bit
//
void EpgDbFilterSetEditorialRating( FILTER_CONTEXT *fc, uchar newEditorialRating )
{
   ifdebug0(newEditorialRating == 0, "EpgDbFilter-SetEditorialRating: WARNING: setting editorial rating 0");
   ifdebug1(newEditorialRating >= 8, "EpgDbFilter-SetEditorialRating: WARNING: setting illegal editorial rating %d", newEditorialRating);

   fc->pFocus->editorialRating = newEditorialRating;
}

// ---------------------------------------------------------------------------
// Add a feature-bits filter (definition see ETS 300 707, chapter 11.3.2)
// - the filter consists of a flags/mask pair: after applying the mask to the
//   feature bits of a PI, the value has to be equal to the given flags,
//   i.e. all selected feature-bits have to match (AND operation)
// - if several feature-bits filters are defined, they are ORed
//
void EpgDbFilterSetFeatureFlags( FILTER_CONTEXT *fc, uchar index, uint flags, uint mask )
{
   if (index < FEATURE_CLASS_COUNT)
   {
      ifdebug2((flags & ~mask) != 0, "EpgDbFilter-SetFeatureFlags: flags=%x outside of mask=%x", flags, mask);
      ifdebug2((flags | mask) & ~FEATURES_ALL, "EpgDbFilter-SetFeatureFlags: flags=%x or mask=%x have invalid bits", flags, mask);

      fc->pFocus->featureFilterFlagField[index] = flags;
      fc->pFocus->featureFilterMaskField[index] = mask;
   }
   else
      fatal1("EpgDbFilter-SetFeatureFlags: illegal index %d", index);
}

// ---------------------------------------------------------------------------
// Set the number of used feature-bits filters
//
void EpgDbFilterSetNoFeatures( FILTER_CONTEXT *fc, uchar noFeatures )
{
   if (fc->pFocus->featureFilterCount < FEATURE_CLASS_COUNT)
   {
      fc->pFocus->featureFilterCount = noFeatures;
   }
   else
      fatal1("EpgDbFilter-SetNoFeatures: illegal count %d", noFeatures);
}

// ---------------------------------------------------------------------------
// Get the number of used feature-bits filters
//
uchar EpgDbFilterGetNoFeatures( FILTER_CONTEXT *fc )
{
   assert(fc->pFocus->featureFilterCount < FEATURE_CLASS_COUNT);
   return fc->pFocus->featureFilterCount;
}

// ---------------------------------------------------------------------------
// Reset the language filter
//
void EpgDbFilterInitLangDescr( FILTER_CONTEXT *fc )
{
   if (fc->pFocus->pLangDescrTable == NULL)
      fc->pFocus->pLangDescrTable = xmalloc(sizeof(*fc->pFocus->pLangDescrTable));

   memset(fc->pFocus->pLangDescrTable, 0, sizeof(*fc->pFocus->pLangDescrTable));
}

// ---------------------------------------------------------------------------
// Enable one language in the language search filter
// - languages are defined by the language descriptor array in the LI block
//   so what actually is being searched for is the descriptor index [0..63]
// - each enabled index is represented by a 1 in the bit field
// - LI blocks are network-specific, so we need a bit field for each network
//
void EpgDbFilterSetLangDescr( CPDBC dbc, FILTER_CONTEXT *fc, const uchar *lg )
{
   const AI_BLOCK *pAiBlock;
   const LI_BLOCK *pLiBlock;
   const LI_DESC  *pDesc;
   uint  descIdx, langIdx;
   uchar netwop;

   if ( EpgDbIsLocked(dbc) )
   {
      if (fc->pFocus->pLangDescrTable != NULL)
      {
         pAiBlock = EpgDbGetAi(dbc);
         if (pAiBlock != NULL)
         {
            for (netwop=0; netwop <= pAiBlock->netwopCount; netwop++)
            {
               if (netwop < pAiBlock->netwopCount)
                  pLiBlock = EpgDbGetLi(dbc, 0, netwop);
               else
                  pLiBlock = EpgDbGetLi(dbc, 0x8000, pAiBlock->thisNetwop);

               if (pLiBlock != NULL)
               {
                  pDesc = LI_GET_DESC(pLiBlock);

                  for (descIdx=0; descIdx < pLiBlock->desc_no; descIdx++)
                  {
                     for (langIdx=0; langIdx < pDesc[descIdx].lang_count; langIdx++)
                     {
                        if ( (pDesc[descIdx].lang[langIdx][0] == lg[0]) &&
                             (pDesc[descIdx].lang[langIdx][1] == lg[1]) &&
                             (pDesc[descIdx].lang[langIdx][2] == lg[2]) )
                        {
                           (*fc->pFocus->pLangDescrTable)[netwop][pDesc[descIdx].id >> 8] |= 1 << (pDesc[descIdx].id & 7);
                           break;
                        }
                     }
                  }
               }
            }
         }
      }
      else
         fatal0("EpgDbFilter-SetLangDescr: LI array not allocated");
   }
   else
      fatal0("EpgDbFilter-SetLangDescr: DB not locked");
}

// ---------------------------------------------------------------------------
// Reset subtitle filter
//
void EpgDbFilterInitSubtDescr( FILTER_CONTEXT *fc )
{
   if (fc->pFocus->pSubtDescrTable == NULL)
      fc->pFocus->pSubtDescrTable = xmalloc(sizeof(*fc->pFocus->pSubtDescrTable));

   memset(fc->pFocus->pSubtDescrTable, 0, sizeof(*fc->pFocus->pSubtDescrTable));
}

// ---------------------------------------------------------------------------
// Enable one subtitle language in the subtitle search filter
// - to search for PI that have subtitles at all use the feature bits
// - see also description of language filter
//
void EpgDbFilterSetSubtDescr( CPDBC dbc, FILTER_CONTEXT *fc, const uchar *lg )
{
   const AI_BLOCK *pAiBlock;
   const TI_BLOCK *pTiBlock;
   const TI_DESC  *pDesc;
   uint  descIdx, subtIdx;
   uchar netwop;

   if ( EpgDbIsLocked(dbc) )
   {
      if (fc->pFocus->pSubtDescrTable != NULL)
      {
         pAiBlock = EpgDbGetAi(dbc);
         if (pAiBlock != NULL)
         {
            for (netwop=0; netwop <= pAiBlock->netwopCount; netwop++)
            {
               if (netwop < pAiBlock->netwopCount)
                  pTiBlock = EpgDbGetTi(dbc, 0, netwop);
               else
                  pTiBlock = EpgDbGetTi(dbc, 0x8000, pAiBlock->thisNetwop);

               if (pTiBlock != NULL)
               {
                  pDesc = TI_GET_DESC(pTiBlock);

                  for (descIdx=0; descIdx < pTiBlock->desc_no; descIdx++)
                  {
                     for (subtIdx=0; subtIdx < pDesc[descIdx].subt_count; subtIdx++)
                     {
                        if ( (pDesc[descIdx].subt[subtIdx].lang[0] == lg[0]) &&
                             (pDesc[descIdx].subt[subtIdx].lang[1] == lg[1]) &&
                             (pDesc[descIdx].subt[subtIdx].lang[2] == lg[2]) )
                        {
                           (*fc->pFocus->pSubtDescrTable)[netwop][pDesc[descIdx].id >> 8] |= 1 << (pDesc[descIdx].id & 7);
                           break;
                        }
                     }
                  }
               }
            }
         }
      }
      else
         fatal0("EpgDbFilter-SetSubtDescr: TI array not allocated");
   }
   else
      fatal0("EpgDbFilter-SetSubtDescr: DB not locked");
}

// ---------------------------------------------------------------------------
// Reset network filter
//
void EpgDbFilterInitNetwop( FILTER_CONTEXT *fc )
{
   memset(fc->pFocus->netwopFilterField, FALSE, sizeof(fc->pFocus->netwopFilterField));
}

// ---------------------------------------------------------------------------
// Enable one network in the network filter array
//
void EpgDbFilterSetNetwop( FILTER_CONTEXT *fc, uchar netwopNo )
{
   if (netwopNo < MAX_NETWOP_COUNT)
   {
      fc->pFocus->netwopFilterField[netwopNo] = TRUE;
   }
   else
      fatal1("EpgDbFilter-SetNetwop: illegal netwop idx %d", netwopNo);
}

// ---------------------------------------------------------------------------
// Reset network pre-filter -> all networks enabled
// - The netwop pre-filter basically works exactly like the netwop filter.
//   However having a separate list that is automatically used when the
//   netwop filter is unused greatly simplifies the handling.
// - The netwop pre-filter is only activated when the normal one is unused
//   to allow netwops outside of the prefilter to be explicitly requested,
//   e.g. by a NI menu.
// - ATTENTION: The semantics of init and set are opposite to
//              the normal netwop filter!
//
void EpgDbFilterInitNetwopPreFilter( FILTER_CONTEXT *fc )
{
   memset(fc->netwopPreFilter1, TRUE, sizeof(fc->netwopPreFilter1));
}

// ---------------------------------------------------------------------------
// Disable one network in the network pre-filter array
//
void EpgDbFilterSetNetwopPreFilter( FILTER_CONTEXT *fc, uchar netwopNo )
{
   if (netwopNo < MAX_NETWOP_COUNT)
   {
      fc->netwopPreFilter1[netwopNo] = FALSE;
   }
   else
      fatal1("EpgDbFilter-SetNetwopPreFilter: illegal netwop idx %d", netwopNo);
}

// ---------------------------------------------------------------------------
// Init higher-level netwop pre-filter (pre-filter #2)
// - intended to be used by the GUI to limit network matches to a sub-set of networks
// - note: this filter is NOT included with the network match cache
//
void EpgDbFilterInitNetwopPreFilter2( FILTER_CONTEXT *fc )
{
   memset(fc->netwopPreFilter2, FALSE, sizeof(fc->netwopPreFilter2));
}

// ---------------------------------------------------------------------------
// Disable one network in the network pre-filter #2 array
// - see comments at the init function above
//
void EpgDbFilterSetNetwopPreFilter2( FILTER_CONTEXT *fc, uchar netwopNo )
{
   if (netwopNo < MAX_NETWOP_COUNT)
   {
      fc->netwopPreFilter2[netwopNo] = TRUE;
   }
   else
      fatal1("EpgDbFilter-SetNetwopPreFilter2: illegal netwop idx %d", netwopNo);
}

// ---------------------------------------------------------------------------
// Reset air times pre-filter -> no restrictions for any networks
//
void EpgDbFilterInitAirTimesFilter( FILTER_CONTEXT *fc )
{
   memset(fc->netwopAirTimeStart, 0, sizeof(fc->netwopAirTimeStart));
   memset(fc->netwopAirTimeStop, 0, sizeof(fc->netwopAirTimeStop));
}

// ---------------------------------------------------------------------------
// Set air times for one network in the air times filter array
// - the filter is disabled by setting start and stop times to 0
//
void EpgDbFilterSetAirTimesFilter( FILTER_CONTEXT *fc, uchar netwopNo, uint startMoD, uint stopMoD )
{
   if (netwopNo < MAX_NETWOP_COUNT)
   {
      assert((startMoD < 24*60) && (stopMoD < 24*60));

      fc->netwopAirTimeStart[netwopNo] = startMoD;
      fc->netwopAirTimeStop[netwopNo]  = stopMoD;
   }
   else
      fatal1("EpgDbFilter-SetAirTimesFilter: illegal netwop idx %d", netwopNo);
}

// ---------------------------------------------------------------------------
// Set expire time border, i.e. min value for stop time
// - usually this will be set to the current time every full minute
//   so that expired programmes automatically disappear from the PI listing
//
void EpgDbFilterSetExpireTime( FILTER_CONTEXT *fc, ulong newExpireTime )
{
   fc->expireTime = newExpireTime;
}

// ---------------------------------------------------------------------------
// Set min value for start time filter
//
void EpgDbFilterSetDateTimeBegin( FILTER_CONTEXT *fc, ulong newTimeBegin )
{
   fc->pFocus->timeBegin = newTimeBegin;
}

// ---------------------------------------------------------------------------
// Set max value for start time filter
//
void EpgDbFilterSetDateTimeEnd( FILTER_CONTEXT *fc, ulong newTimeEnd )
{
   fc->pFocus->timeEnd = newTimeEnd;
}

// ---------------------------------------------------------------------------
// Set minimum and maximum programme duration values
//
void EpgDbFilterSetMinMaxDuration( FILTER_CONTEXT *fc, uint dur_min, uint dur_max )
{
   assert(dur_max >= dur_min);
   assert(dur_max < 24*60*60);

   fc->pFocus->duration_min = dur_min;
   fc->pFocus->duration_max = dur_max;
}

// ---------------------------------------------------------------------------
// Set value for prog-no filter
// - i.e the range of indices relative to the start block no in AI
// - 0 refers to the currently running programme
//
void EpgDbFilterSetProgIdx( FILTER_CONTEXT *fc, uchar newFirstProgIdx, uchar newLastProgIdx )
{
   assert(newFirstProgIdx <= newLastProgIdx);

   fc->pFocus->firstProgIdx = newFirstProgIdx;
   fc->pFocus->lastProgIdx  = newLastProgIdx;
}

// ---------------------------------------------------------------------------
// Set minimum and maximum programme duration values
//
void EpgDbFilterSetVpsPdcMode( FILTER_CONTEXT *fc, uint mode )
{
   assert(mode <= 2);

   fc->pFocus->vps_pdc_mode = mode;
}

// ---------------------------------------------------------------------------
// Enable a filter with a match function provided by the user
// - the match function is invoked for every matched PI with the given argument
// - when the filter context is destroyed the free function is invoked
//
void EpgDbFilterSetCustom( FILTER_CONTEXT *fc, CUSTOM_FILTER_MATCH * pMatchCb,
                           CUSTOM_FILTER_FREE * pFreeCb, void * pArg )
{
   if (pMatchCb != NULL)
   {
      if ( (fc->pFocus->pCustomArg != NULL) &&
           (fc->pFocus->pCustomDestroyFunc != NULL) )
      {
         fc->pFocus->pCustomDestroyFunc(fc->pFocus->pCustomArg);
      }

      fc->pFocus->pCustomArg          = pArg;
      fc->pFocus->pCustomFilterFunc   = pMatchCb;
      fc->pFocus->pCustomDestroyFunc  = pFreeCb;
   }
   else
      fatal0("EpgDbFilter-SetCustom: illegal NULL param");
}

// ---------------------------------------------------------------------------
// Set string for sub-string search in title, short- and long info
//
                                     //"ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞß"
const uchar latin1LowerCaseTable[32] = "àáâãäåæçèéêëìíîïðñòóôõö×øùúûüýþß";

void EpgDbFilterSetSubStr( FILTER_CONTEXT *fc, const uchar *pStr,
                           bool scopeTitle, bool scopeDesc, bool matchCase, bool matchFull )
{
   EPGDB_FILT_SUBSTR * pSubStrCtx;
   uint   size;
   uchar *p, c;

   assert(scopeTitle || scopeDesc);

   if (pStr != NULL)
   {
      size = sizeof(EPGDB_FILT_SUBSTR) + strlen(pStr) + 1 - 1;
      pSubStrCtx = xmalloc(size);

      pSubStrCtx->elem_size    = size;
      pSubStrCtx->scopeTitle   = scopeTitle;
      pSubStrCtx->scopeDesc    = scopeDesc;
      pSubStrCtx->strMatchCase = matchCase;
      pSubStrCtx->strMatchFull = matchFull;
      strcpy(pSubStrCtx->str, pStr);

      if (matchCase == FALSE)
      {  // convert search string to all lowercase
         p = pSubStrCtx->str;
         while ((c = *p) != 0)
         {
            if ((c <= 'Z') && (c >= 'A'))
               *(p++) = c + ('a' - 'A');
            else if ((c >= 0xA0 + 32)  && (c < 0xA0 + 2*32))
               *(p++) = latin1LowerCaseTable[c - (0xA0 + 32)];
            else
               *(p++) = c;
         }
      }

      // link the new context at the head of the substring parameter chain
      pSubStrCtx->pNext = fc->pFocus->pSubStrCtx;
      fc->pFocus->pSubStrCtx = pSubStrCtx;
   }
   else
      fatal0("EpgDbFilter-SetSubStr: illegal NULL param");
}

// ---------------------------------------------------------------------------
// Make a lower-case copy of a string for case-insensitive comparisons
//
static void EpgDbFilter_SubstrToLower( const uchar * src, uchar * dst, uint maxLen )
{
   register uchar c;
   register int  len;

   len = maxLen - 1;
   while ( ((c = *(src++)) != 0) && (len > 0) )
   {
      if ((c <= 'Z') && (c >= 'A'))
         *(dst++) = c + ('a' - 'A');
      else if ((c >= 0xA0 + 32)  && (c < 0xA0 + 2*32))
         *(dst++) = latin1LowerCaseTable[c - (0xA0 + 32)];
      else
         *(dst++) = c;
      len--;
   }
   *(dst++) = 0;
}

// ---------------------------------------------------------------------------
// Compare text with search string accorgind to search parameters
// - parameter #1: ignore case
// - parameter #2: exact => search string must match start and end of text
//
static bool xstrcmp( const EPGDB_FILT_SUBSTR *ssc, const char * str,
                     uchar * pCache, uint cacheSize,  bool * pIsLower )
{
   bool match;

   if (ssc->strMatchCase == FALSE)
   {
      if (ssc->strMatchFull == FALSE)
      {
         // make a lower-case copy of the haystack and then use the strstr library func.
         // the needle already has to be lower-case
         if (*pIsLower == FALSE)
         {
            EpgDbFilter_SubstrToLower(str, pCache, cacheSize);
            *pIsLower = TRUE;
         }

         match = (strstr(pCache, ssc->str) != NULL);
      }
      else
      {
         match = (strcasecmp(str, ssc->str) == 0);
      }
   }
   else
   {
      if (ssc->strMatchFull == FALSE)
      {
         match = (strstr(str, ssc->str) != NULL);
      }
      else
      {
         match = (strcmp(str, ssc->str) == 0);
      }
   }
   return match;
}

// ---------------------------------------------------------------------------
// Compare text with search string accorgind to search parameters
// - note: either title or description search or both must be enabled
// - parameter #1: ignore case
// - parameter #2: exact => search string must match start and end of text
//
static bool EpgDbFilter_MatchSubstr( const EPGDB_FILT_SUBSTR *ssc, const uchar * pTitleStr, 
                                     const uchar * pShortStr,      const uchar * pLongStr )
{
   uchar long_info[2048+20];
   uchar short_info[255+4];
   uchar title[255+4];
   bool title_lower;
   bool short_lower;
   bool long_lower;

   title_lower = short_lower = long_lower = FALSE;

   // OR across all substring text matches
   while (ssc != NULL)
   {
      if (ssc->scopeTitle)
      {
         if (xstrcmp(ssc, pTitleStr, title, sizeof(title), &title_lower))
         {
            break;
         }
      }
      if (ssc->scopeDesc)
      {
         if ( ((pShortStr != NULL) && xstrcmp(ssc, pShortStr, short_info, sizeof(short_info), &short_lower)) ||
              ((pLongStr != NULL) && xstrcmp(ssc, pLongStr, long_info, sizeof(long_info), &long_lower)) )
         {
            break;
         }
      }
      ssc = ssc->pNext;
   }
   return (ssc != NULL);
}

// ---------------------------------------------------------------------------
// Enables one or more pre-filters in the given context
//
void EpgDbPreFilterEnable( FILTER_CONTEXT *fc, uint mask )
{
   assert((mask & ~FILTER_PERM) == 0);

   fc->enabledPreFilters |= mask;
}

// ---------------------------------------------------------------------------
// Disables one or more pre-filters in the given context
//
void EpgDbPreFilterDisable( FILTER_CONTEXT *fc, uint mask )
{
   assert((mask & ~FILTER_PERM) == 0);

   fc->enabledPreFilters &= ~ mask;

}
// ---------------------------------------------------------------------------
// Enables one or more filters in the given context
//
void EpgDbFilterEnable( FILTER_CONTEXT *fc, uint mask )
{
   assert((mask & ~FILTER_NONPERM) == 0);

   fc->pFocus->enabledFilters |= mask;
}

// ---------------------------------------------------------------------------
// Disables one or more filters in the given context
//
void EpgDbFilterDisable( FILTER_CONTEXT *fc, uint mask )
{
   assert((mask & ~FILTER_NONPERM) == 0);

   if ((mask & FILTER_SUBSTR) != 0)
   {
      EpgDbFilterDestroyParamChain(fc->pFocus->pSubStrCtx);
      fc->pFocus->pSubStrCtx = NULL;
   }
   if ((mask & FILTER_SORTCRIT) != 0)
   {
      fc->pFocus->usedSortCritClasses = 0;
   }
   if ((mask & FILTER_FEATURES) != 0)
   {
      fc->pFocus->featureFilterCount = 0;
   }

   // free dynamically allocated filter arrays
   if (((mask & FILTER_SERIES) != 0) && (fc->pFocus->pSeriesFilterMatrix != NULL))
   {
      xfree(fc->pFocus->pSeriesFilterMatrix);
      fc->pFocus->pSeriesFilterMatrix = NULL;
   }
   if (((mask & FILTER_SUBTITLES) != 0) && (fc->pFocus->pSubtDescrTable != NULL))
   {
      xfree(fc->pFocus->pSubtDescrTable);
      fc->pFocus->pSubtDescrTable = NULL;
   }
   if (((mask & FILTER_LANGUAGES) != 0) && (fc->pFocus->pLangDescrTable != NULL))
   {
      xfree(fc->pFocus->pLangDescrTable);
      fc->pFocus->pLangDescrTable = NULL;
   }

   fc->pFocus->enabledFilters &= ~ mask;
}

// ----------------------------------------------------------------------------
// Invert the result of given filter types when matching against PI
// - note: for global inversion of the combined filtering result there is a
//   separate filter type which is enabled via the regular filter type enable func
//
void EpgDbFilterInvert( FILTER_CONTEXT *fc, uint mask, uchar themeClass, uchar sortCritClass )
{
   assert((mask & (FILTER_PERM | FILTER_INVERT)) == 0);  // inverting these is not supported

   fc->pFocus->invertedFilters         = mask;
   fc->pFocus->invertedThemeClasses    = themeClass;
   fc->pFocus->invertedSortCritClasses = sortCritClass;
}

// ----------------------------------------------------------------------------
// Initialize a filter context and time slot for NI stack processing
//
void EpgDbFilterInitNi( FILTER_CONTEXT *fc, NI_FILTER_STATE *pNiState )
{
   pNiState->flags = NI_DATE_NONE;

   // reset all filter settings (except pre-filters)
   fc->pFocus->enabledFilters = 0;
}

// ----------------------------------------------------------------------------
// Apply one filter from a NI stack to filter context and time slot
// - time filters cannot be applied immediately to the filter context,
//   because they are interdependant; so we have to collect all time
//   related filters from the NI stack in the time slot state and
//   process them at the end
//
void EpgDbFilterApplyNi( CPDBC dbc, FILTER_CONTEXT *fc, NI_FILTER_STATE *pNiState, uchar kind, ulong data )
{
   uchar lg[3];
   uint  class;

   switch (kind)
   {
      case EV_ATTRIB_KIND_PROGNO_START:
         if ((fc->pFocus->enabledFilters & FILTER_PROGIDX) == FALSE)
            fc->pFocus->lastProgIdx = (uchar)(data & 0xff);
         fc->pFocus->firstProgIdx = (uchar)(data & 0xff);
         fc->pFocus->enabledFilters |= FILTER_PROGIDX;
         break;

      case EV_ATTRIB_KIND_PROGNO_STOP:
         if ((fc->pFocus->enabledFilters & FILTER_PROGIDX) == FALSE)
            fc->pFocus->firstProgIdx = 0;
         fc->pFocus->lastProgIdx = (uchar)(data & 0xff);
         fc->pFocus->enabledFilters |= FILTER_PROGIDX;
         break;

      case EV_ATTRIB_KIND_NETWOP:
         if ((fc->pFocus->enabledFilters & FILTER_NETWOP) == FALSE)
            EpgDbFilterInitNetwop(fc);
         fc->pFocus->netwopFilterField[data & 0xff] = TRUE;
         fc->pFocus->enabledFilters |= FILTER_NETWOP;
         break;

      case EV_ATTRIB_KIND_THEME:
      case EV_ATTRIB_KIND_THEME + 1:
      case EV_ATTRIB_KIND_THEME + 2:
      case EV_ATTRIB_KIND_THEME + 3:
      case EV_ATTRIB_KIND_THEME + 4:
      case EV_ATTRIB_KIND_THEME + 5:
      case EV_ATTRIB_KIND_THEME + 6:
      case EV_ATTRIB_KIND_THEME + 7:
         if ((fc->pFocus->enabledFilters & FILTER_THEMES) == FALSE)
            EpgDbFilterInitThemes(fc, 0xff);
         class = 1 << (kind - EV_ATTRIB_KIND_THEME);
         fc->pFocus->themeFilterField[data & 0xff] |= class;
         fc->pFocus->usedThemeClasses |= class;
         fc->pFocus->enabledFilters |= FILTER_THEMES;
         break;

      case EV_ATTRIB_KIND_SORTCRIT:
      case EV_ATTRIB_KIND_SORTCRIT + 1:
      case EV_ATTRIB_KIND_SORTCRIT + 2:
      case EV_ATTRIB_KIND_SORTCRIT + 3:
      case EV_ATTRIB_KIND_SORTCRIT + 4:
      case EV_ATTRIB_KIND_SORTCRIT + 5:
      case EV_ATTRIB_KIND_SORTCRIT + 6:
      case EV_ATTRIB_KIND_SORTCRIT + 7:
         if ((fc->pFocus->enabledFilters & FILTER_SORTCRIT) == FALSE)
            EpgDbFilterInitSortCrit(fc, 0xff);
         class = 1 << (kind - EV_ATTRIB_KIND_SORTCRIT);
         fc->pFocus->sortCritFilterField[data & 0xff] |= class;
         fc->pFocus->usedSortCritClasses |= class;
         fc->pFocus->enabledFilters |= FILTER_SORTCRIT;
         break;

      case EV_ATTRIB_KIND_EDITORIAL:
         fc->pFocus->editorialRating = (uchar)(data & 0xff);
         fc->pFocus->enabledFilters |= FILTER_EDIT_RAT;
         break;

      case EV_ATTRIB_KIND_PARENTAL:
         fc->pFocus->parentalRating = (uchar)(data & 0xff);
         fc->pFocus->enabledFilters |= FILTER_PAR_RAT;
         break;

      case EV_ATTRIB_KIND_FEATURES:
         if ((fc->pFocus->enabledFilters & FILTER_FEATURES) == FALSE)
            fc->pFocus->featureFilterCount = 0;
         if (fc->pFocus->featureFilterCount < FEATURE_CLASS_COUNT - 1)
         {
            fc->pFocus->featureFilterFlagField[fc->pFocus->featureFilterCount] = data & 0xfff;
            fc->pFocus->featureFilterMaskField[fc->pFocus->featureFilterCount] = data >> 12;
            fc->pFocus->featureFilterCount += 1;
            fc->pFocus->enabledFilters |= FILTER_FEATURES;
         }
         else
            debug0("EpgDbFilter-ApplyNi: feature filter count exceeded");
         break;

      case EV_ATTRIB_KIND_REL_DATE:
         pNiState->reldate = (uchar)(data & 0xff);
         pNiState->flags |= NI_DATE_RELDATE;
         break;

      case EV_ATTRIB_KIND_START_TIME:
         pNiState->startMoD = EpgBlockBcdToMoD(data);
         pNiState->flags |= NI_DATE_START;
         break;

      case EV_ATTRIB_KIND_STOP_TIME:
         pNiState->stopMoD = EpgBlockBcdToMoD(data);
         pNiState->flags |= NI_DATE_STOP;
         break;

      case EV_ATTRIB_KIND_LANGUAGE:
         if ((fc->pFocus->enabledFilters & FILTER_LANGUAGES) == FALSE)
            EpgDbFilterInitLangDescr(fc);
         lg[0] = (uchar)(data & 0xff);
         lg[1] = (uchar)((data >> 8) & 0xff);
         lg[2] = (uchar)((data >> 16) & 0xff);
         EpgDbFilterSetLangDescr(dbc, fc, lg);
         fc->pFocus->enabledFilters |= FILTER_LANGUAGES;
         break;

      case EV_ATTRIB_KIND_SUBT_LANG:
         if ((fc->pFocus->enabledFilters & FILTER_SUBTITLES) == FALSE)
            EpgDbFilterInitSubtDescr(fc);
         lg[0] = (uchar)(data & 0xff);
         lg[1] = (uchar)((data >> 8) & 0xff);
         lg[2] = (uchar)((data >> 16) & 0xff);
         EpgDbFilterSetSubtDescr(dbc, fc, lg);
         fc->pFocus->enabledFilters |= FILTER_SUBTITLES;
         break;

      default:
         debug1("EpgDbFilterApplyNi: unknown attrib kind %d", kind);
         break;
   }
}

// ----------------------------------------------------------------------------
// Process any collected time filters from the NI stack
// - conforms to ETS 300 707, chapter 11.12.4.1, "Attribute Descriptions",
//   first paragraph: "Start Time, Stop Time"
// - all dates from the filter data are in local time
//
void EpgDbFilterFinishNi( FILTER_CONTEXT *fc, NI_FILTER_STATE *pNiState )
{
   time_t  now;
   uint    nowMoD;
   sint    lto;

   now = time(NULL);
   lto = EpgLtoGet(now);
   // number of minutes that have elapsed since last midnight (Minutes-of-Day)
   nowMoD = ((now + lto) % (60*60*24)) / 60;

   if (pNiState->flags != NI_DATE_NONE)
   {  // at least one time related filter is in the NI stack

      if ((pNiState->flags & NI_DATE_START) == 0)
      {  // no start time given -> take current time
         pNiState->startMoD = nowMoD;
      }
      else if (pNiState->startMoD == 0xffff)
      {  // special value for "current time"
         pNiState->startMoD = nowMoD;
         // in this case stop time is defined as an offset to the current time
         if (pNiState->flags & NI_DATE_STOP)
         {
            pNiState->stopMoD += nowMoD;
         }
      }

      if ((pNiState->flags & NI_DATE_STOP) == 0)
      {  // no stop time given -> use end of the day
         pNiState->stopMoD = 23*60 + 59;
      }

      fc->pFocus->enabledFilters &= ~ FILTER_TIME_ALL;

      if ((pNiState->flags & NI_DATE_RELDATE) != 0)
      {
         if (pNiState->startMoD > pNiState->stopMoD)
         {  // time slot crosses date border
            pNiState->stopMoD += 24*60;
         }
         else if ((pNiState->stopMoD <= nowMoD) && (pNiState->reldate == 0))
         {  // time slot has completely elapsed today -> use tomorrow
            pNiState->reldate += 1;
         }

         // time base is today, midnight
         // substract LTO because the added filters are in local time
         now = now - (now % (60*60*24)) - lto;
         fc->pFocus->timeBegin = now + pNiState->startMoD * 60 + pNiState->reldate * 60*60*24;
         fc->pFocus->timeEnd   = now + pNiState->stopMoD  * 60 + pNiState->reldate * 60*60*24;
         fc->pFocus->timeDayOffset = 0;

         fc->pFocus->enabledFilters |= FILTER_TIME_ONCE;
      }
      else
      {  // no date given -> allow given time window on any day
         fc->pFocus->timeBegin       = pNiState->startMoD;
         fc->pFocus->timeEnd         = pNiState->stopMoD;

         if ((pNiState->flags & NI_DATE_WEEKLY) != 0)
         {
            fc->pFocus->timeDayOffset   = pNiState->reldate;
            fc->pFocus->enabledFilters |= FILTER_TIME_WEEKLY;
         }
         else if ((pNiState->flags & NI_DATE_MONTHLY) != 0)
         {
            fc->pFocus->timeDayOffset   = pNiState->reldate;
            fc->pFocus->enabledFilters |= FILTER_TIME_MONTHLY;
         }
         else
         {
            fc->pFocus->timeDayOffset   = 0;
            fc->pFocus->enabledFilters |= FILTER_TIME_DAILY;
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Checks if the given PI block matches the settings in the given context
// - using goto to end of procedure upon first failed match for performance reasons
//
static bool EpgDbFilterMatchAct( const EPGDB_CONTEXT *dbc, const FILTER_CONTEXT *fc,
                                 const FILTER_CTX_ACT *fc_act, const PI_BLOCK * pPi )
{
   uchar class;
   uint  index;
   bool  fail_buf;
   bool  fail, invert;
   bool  skipThemes = FALSE;
   bool  skipSubstr = FALSE;
   bool  orSeriesThemes = FALSE;
   bool  orSeriesSubstr = FALSE;

   // variable to temporarily keep track of failed matches, because matches
   // cannot be aborted with "goto failed" until all possible "failed_pre"
   // matches are through
   fail_buf = FALSE;

   if (fc_act->enabledFilters & FILTER_NETWOP)
   {
      fail   = (fc_act->netwopFilterField[pPi->netwop_no] == FALSE);

      invert = ((fc_act->invertedFilters & FILTER_NETWOP) != FALSE);
      if (fail ^ invert)
      {
         // if the network is also excluded by pre-filter, it's excluded from global inversion
         if ( (fc->enabledPreFilters & FILTER_NETWOP_PRE) &&
              (fc->netwopPreFilter1[pPi->netwop_no] == FALSE) )
            goto failed_pre;
         else
            fail_buf = TRUE;
      }

      // do not include pre-filtered netwops with inverted network selection
      if ( invert && (fc->enabledPreFilters & FILTER_NETWOP_PRE) &&
           (fc->netwopPreFilter1[pPi->netwop_no] == FALSE) )
         goto failed_pre;
   }
   else if (fc->enabledPreFilters & FILTER_NETWOP_PRE)
   {  // netwop pre-filter is only activated when netwop is unused
      // this way also netwops outside of the prefilter can be explicitly requested, e.g. by a NI menu
      if (fc->netwopPreFilter1[pPi->netwop_no] == FALSE)
         goto failed_pre;
   }

   if (fc_act->enabledFilters & FILTER_TIME_ONCE)
   {
      fail   = ((pPi->start_time < fc_act->timeBegin) || (pPi->start_time >= fc_act->timeEnd));

      invert = ((fc_act->invertedFilters & FILTER_TIME_ONCE) != FALSE);
      if (fail ^ invert)
         goto failed_pre;
   }
   else if (fc_act->enabledFilters & FILTER_TIME_DAILY)
   {
      struct tm * ptm = localtime(&pPi->start_time);
      int timeOfDay = ptm->tm_min + (ptm->tm_hour * 60);

      if (fc_act->timeBegin < fc_act->timeEnd)
         fail   = ((timeOfDay < fc_act->timeBegin) || (timeOfDay >= fc_act->timeEnd));
      else
         fail   = ((timeOfDay < fc_act->timeBegin) && (timeOfDay >= fc_act->timeEnd));

      invert = ((fc_act->invertedFilters & FILTER_TIME_DAILY) != FALSE);
      if (fail ^ invert)
         goto failed_pre;
   }
   else if (fc_act->enabledFilters & FILTER_TIME_WEEKLY)
   {
      struct tm * ptm = localtime(&pPi->start_time);
      int timeOfDay = ptm->tm_min + (ptm->tm_hour * 60);

      fail = ((((uint)ptm->tm_wday + 1) % 7) != fc_act->timeDayOffset);
      if (fail == FALSE)
      {
         if (fc_act->timeBegin < fc_act->timeEnd)
            fail   = ((timeOfDay < fc_act->timeBegin) || (timeOfDay >= fc_act->timeEnd));
         else
            fail   = ((timeOfDay < fc_act->timeBegin) && (timeOfDay >= fc_act->timeEnd));
      }

      invert = ((fc_act->invertedFilters & FILTER_TIME_WEEKLY) != FALSE);
      if (fail ^ invert)
         goto failed_pre;
   }
   else if (fc_act->enabledFilters & FILTER_TIME_MONTHLY)
   {
      struct tm * ptm = localtime(&pPi->start_time);
      int timeOfDay = ptm->tm_min + (ptm->tm_hour * 60);

      fail = ((uint)ptm->tm_mday != fc_act->timeDayOffset);
      if (fail == FALSE)
      {
         if (fc_act->timeBegin < fc_act->timeEnd)
            fail   = ((timeOfDay < fc_act->timeBegin) || (timeOfDay >= fc_act->timeEnd));
         else
            fail   = ((timeOfDay < fc_act->timeBegin) && (timeOfDay >= fc_act->timeEnd));
      }

      invert = ((fc_act->invertedFilters & FILTER_TIME_MONTHLY) != FALSE);
      if (fail ^ invert)
         goto failed_pre;
   }

   if (fc_act->enabledFilters & FILTER_DURATION)
   {
      uint duration = pPi->stop_time - pPi->start_time;

      fail   = ((duration < fc_act->duration_min) || (duration > fc_act->duration_max));

      invert = ((fc_act->invertedFilters & FILTER_DURATION) != FALSE);
      if (fail ^ invert)
         fail_buf = TRUE;
   }

   if (fc_act->enabledFilters & FILTER_PAR_RAT)
   {
      if (pPi->parental_rating != 0)
      {
         fail   = (pPi->parental_rating > fc_act->parentalRating);

         invert = ((fc_act->invertedFilters & FILTER_PAR_RAT) != FALSE);
         if (fail ^ invert)
            fail_buf = TRUE;
      }
      else
      {  // do not include unrated programmes via global inversion
         goto failed_pre;
      }
   }

   if (fc_act->enabledFilters & FILTER_EDIT_RAT)
   {
      if (pPi->editorial_rating != 0)
      {
         fail   = (pPi->editorial_rating < fc_act->editorialRating);

         invert = ((fc_act->invertedFilters & FILTER_EDIT_RAT) != FALSE);
         if (fail ^ invert)
            fail_buf = TRUE;
      }
      else
      {  // do not include unrated programmes via global inversion
         goto failed_pre;
      }
   }

   // after the last "failed_pre" we can abort if one of the above matches failed
   if (fail_buf)
      goto failed;

   if (fc_act->enabledFilters & FILTER_PROGIDX)
   {
      uint progNo = EpgDbGetProgIdx(dbc, pPi);
      fail   = ((progNo < fc_act->firstProgIdx) || (progNo > fc_act->lastProgIdx));

      invert = ((fc_act->invertedFilters & FILTER_PROGIDX) != FALSE);
      if (fail ^ invert)
         goto failed;
   }

   if (fc_act->enabledFilters & FILTER_FEATURES)
   {
      for (index=0; index < fc_act->featureFilterCount; index++)
      {
         if ((pPi->feature_flags & fc_act->featureFilterMaskField[index]) == fc_act->featureFilterFlagField[index])
            break;
      }
      fail   = (index >= fc_act->featureFilterCount);

      invert = ((fc_act->invertedFilters & FILTER_FEATURES) != FALSE);
      if (fail ^ invert)
         goto failed;
   }

   if ( ((fc_act->enabledFilters & (FILTER_SERIES | FILTER_THEMES)) == (FILTER_SERIES | FILTER_THEMES)) &&
        (((fc_act->invertedFilters & (FILTER_SERIES | FILTER_THEMES)) == 0) ||
         ((fc_act->invertedFilters & (FILTER_SERIES | FILTER_THEMES)) == (FILTER_SERIES | FILTER_THEMES)) ) )
   {  // both themes and series filters are used -> logical OR (unless only one of theme is inverted)
      orSeriesThemes = TRUE;
   }

   if ( ((fc_act->enabledFilters & (FILTER_SERIES | FILTER_SUBSTR)) == (FILTER_SERIES | FILTER_SUBSTR)) &&
        (((fc_act->invertedFilters & (FILTER_SERIES | FILTER_SUBSTR)) == 0) ||
         ((fc_act->invertedFilters & (FILTER_SERIES | FILTER_SUBSTR)) == (FILTER_SERIES | FILTER_SUBSTR)) ) )
   {  // both themes and series filters are used -> logical OR (unless only one of theme is inverted)
      orSeriesSubstr = TRUE;
   }

   if ( (fc_act->enabledFilters & FILTER_SERIES) &&
        (fc_act->pSeriesFilterMatrix != NULL) )
   {
      fail = TRUE;
      for (index=0; index < pPi->no_themes; index++)
      {
         register uchar theme = pPi->themes[index];
         if ((theme > 0x80) &&
             ((*fc_act->pSeriesFilterMatrix)[pPi->netwop_no][theme - 0x80]))
         {
            fail = FALSE;
            break;
         }
      }
      invert = ((fc_act->invertedFilters & FILTER_SERIES) != FALSE);
      fail  ^= invert;

      // logical OR between series and themes filter (same filter type)
      if (fail == FALSE)
      {  // series matched -> skip themes filter (unless asymetrical inversion)
         skipThemes = orSeriesThemes;
         skipSubstr = orSeriesSubstr;
      }
      else if ((orSeriesThemes == FALSE) && (orSeriesSubstr == FALSE))
      {  // series did not match -> failed only, if logical AND between themes and series
         goto failed;
      }
   }

   if ((fc_act->enabledFilters & FILTER_THEMES) && (skipThemes == FALSE))
   {
      for (class=1; class != 0; class <<= 1)
      {  // AND across all classes
         if (fc_act->usedThemeClasses & class)
         {
            fail = TRUE;
            for (index=0; index < pPi->no_themes; index++)
            {  // OR across all themes in a class
               if (fc_act->themeFilterField[pPi->themes[index]] & class)
               {  // ignore theme series codes if series filter is used
                  // (or a "series - general" selection couldn't be reduced by selecting particular series)
                  if ( (pPi->themes[index] < 0x80) ||
                       ((fc_act->enabledFilters & FILTER_SERIES) == FALSE) ||
                       ((fc_act->invertedFilters & FILTER_SERIES) != FALSE) /* ||
                       ((fc_act->invertedFilters & (FILTER_THEMES | FILTER_SERIES)) != FALSE)*/ )
                  {
                     fail = FALSE;
                     break;
                  }
               }
            }

            invert = ((fc_act->invertedThemeClasses & class) != FALSE);
            if (fail ^ invert)
               goto failed;
         }
      }
   }

   if (fc_act->enabledFilters & FILTER_SORTCRIT)
   {
      for (class=1; class != 0; class <<= 1)
      {  // AND across all classes
         if (fc_act->usedSortCritClasses & class)
         {
            fail = TRUE;
            for (index=0; index < pPi->no_sortcrit; index++)
            {  // OR across all sorting criteria in a class
               if (fc_act->sortCritFilterField[pPi->sortcrits[index]] & class)
               {
                  fail = FALSE;
                  break;
               }
            }

            invert = ((fc_act->invertedSortCritClasses & class) != FALSE);
            if (fail ^ invert)
               goto failed;
         }
      }
   }

   if ( (fc_act->enabledFilters & FILTER_LANGUAGES) &&
        (fc_act->pLangDescrTable != NULL) )
   {
      const DESCRIPTOR *pDesc = PI_GET_DESCRIPTORS(pPi);
      for (class=0; class < pPi->no_descriptors; pPi++)
      {
         if ( (pDesc->type == LI_DESCR_TYPE) &&
              ((*fc_act->pLangDescrTable)[pPi->netwop_no][pDesc->id / 8] & (1 << (pDesc->id % 8))) )
         {  // bit for this ID is set in the bit field -> match
            break;
         }
         pDesc += 1;
      }
      // failed if none of the searched for IDs is contained in this PI
      fail   = (class >= pPi->no_descriptors);

      invert = ((fc_act->invertedFilters & FILTER_LANGUAGES) != FALSE);
      if (fail ^ invert)
         goto failed;
   }

   if (fc_act->enabledFilters & FILTER_SUBTITLES)
   {
      const DESCRIPTOR *pDesc = PI_GET_DESCRIPTORS(pPi);
      for (class=0; class < pPi->no_descriptors; pPi++)
      {
         if ( (pDesc->type == TI_DESCR_TYPE) &&
              ((*fc_act->pSubtDescrTable)[pPi->netwop_no][pDesc->id / 8] & (1 << (pDesc->id % 8))) )
         {  // bit for this ID is set in the bit field -> match
            break;
         }
      }
      // failed if none of the searched for IDs is contained in this PI
      fail   = (class >= pPi->no_descriptors);

      invert = ((fc_act->invertedFilters & FILTER_SUBTITLES) != FALSE);
      if (fail ^ invert)
         goto failed;
   }

   if ((fc_act->enabledFilters & FILTER_SUBSTR) && (skipSubstr == FALSE))
   {
      fail = ( EpgDbFilter_MatchSubstr(fc_act->pSubStrCtx,
                                       PI_GET_TITLE(pPi),
                                       PI_HAS_SHORT_INFO(pPi) ? PI_GET_SHORT_INFO(pPi) : NULL,
                                       PI_HAS_LONG_INFO(pPi) ? PI_GET_LONG_INFO(pPi) : NULL) == FALSE);

      invert = ((fc_act->invertedFilters & FILTER_SUBSTR) != FALSE);
      if (fail ^ invert)
         goto failed;
   }

   if (fc_act->enabledFilters & FILTER_CUSTOM)
   {
      // custom search filter function is implemented by caller
      fail = (fc_act->pCustomFilterFunc == NULL) ||
             (fc_act->pCustomFilterFunc(dbc, pPi, fc_act->pCustomArg) == FALSE);

      invert = ((fc_act->invertedFilters & FILTER_CUSTOM) != FALSE);
      if (fail ^ invert)
         goto failed;
   }

   if (fc_act->enabledFilters & FILTER_VPS_PDC)
   {
      if (fc_act->vps_pdc_mode == 1)
      {
         fail = (pPi->pil == INVALID_VPS_PIL);
      }
      else if (fc_act->vps_pdc_mode == 2)
      {
         struct tm * pTm = localtime(&pPi->start_time);
         if (pPi->pil == INVALID_VPS_PIL)
            fail = TRUE;
         else if ( (pTm->tm_min == (sint)(pPi->pil & 0x3f)) &&
                   (pTm->tm_hour == (sint)((pPi->pil >>  6) & 0x1f)) &&
                   (pTm->tm_mday == (sint)((pPi->pil >> 15) & 0x1f)) &&
                   (pTm->tm_mon + 1 == (sint)((pPi->pil >> 11) & 0x0f)) )
            fail = TRUE;
         else
            fail = FALSE;
      }
      else  // illegal mode
         fail = TRUE;

      invert = ((fc_act->invertedFilters & FILTER_VPS_PDC) != FALSE);
      if (fail ^ invert)
         goto failed;
   }

   // all tests have passed -> match ok
   if (fc_act->enabledFilters & FILTER_INVERT)
      return FALSE;
   else
      return TRUE;

failed:
   if (fc_act->enabledFilters & FILTER_INVERT)
      return TRUE;
failed_pre:
      return FALSE;
}

// ---------------------------------------------------------------------------
// Checks if the given PI block matches the settings in the given context
// - using goto to end of procedure upon first failed match for performance reasons
//
bool EpgDbFilterMatches( const EPGDB_CONTEXT   * dbc,
                         const FILTER_CONTEXT  * fc,
                         const PI_BLOCK        * pPi )
{
   FILTER_CTX_ACT  * fc_act;
   bool  match;
   
   if ((fc != NULL) && (pPi != NULL))
   {
      if (fc->enabledPreFilters & FILTER_NETWOP_PRE2)
      {
         if (fc->netwopPreFilter2[pPi->netwop_no] == FALSE)
            goto failed_pre;
      }

      if (fc->enabledPreFilters & FILTER_AIR_TIMES)
      {
         if ( (fc->netwopAirTimeStart[pPi->netwop_no] != 0) ||
              (fc->netwopAirTimeStop[pPi->netwop_no] != 0) )
         {
            sint  lto = EpgLtoGet(pPi->start_time);
            sint  airStartMoD = fc->netwopAirTimeStart[pPi->netwop_no];
            sint  airStopMoD  = fc->netwopAirTimeStop[pPi->netwop_no];
            sint  piStartMoD  = ((pPi->start_time + lto) % (60*60*24)) / 60;
            sint  piStopMoD   = ((pPi->stop_time + lto) % (60*60*24)) / 60;

            if (airStopMoD >= airStartMoD)
            {  // time range e.g. 07:00 - 19:00
               if (piStartMoD < airStartMoD)
               {
                  if ((piStopMoD <= airStartMoD) && (piStopMoD > piStartMoD))
                     goto failed_pre;
               }
               else if (piStartMoD >= airStopMoD)
               {
                  if ((piStopMoD > piStartMoD) || (piStopMoD <= airStartMoD))
                     goto failed_pre;
               }

               if (fc->enabledPreFilters & FILTER_EXPIRE_TIME)
               {
                  if (piStopMoD > airStopMoD)
                  {
                     if (pPi->stop_time - (piStopMoD - airStopMoD)*60 <= fc->expireTime)
                        goto failed_pre;
                  }
                  else if (piStopMoD <= airStartMoD)
                  {
                     if (pPi->stop_time - (piStopMoD + 24*60 - airStopMoD)*60 <= fc->expireTime)
                        goto failed_pre;
                  }
               }
            }
            else
            {  // time range e.g. 19:00 - 07:00
               if ((piStartMoD < airStartMoD) && (piStartMoD >= airStopMoD))
               {
                  if ((piStopMoD > piStartMoD) && (piStopMoD <= airStartMoD))
                     goto failed_pre;
               }

               if (fc->enabledPreFilters & FILTER_EXPIRE_TIME)
               {
                  if ((piStopMoD <= airStartMoD) && (piStopMoD > airStopMoD))
                  {
                     if (pPi->stop_time - (piStopMoD - airStopMoD)*60 <= fc->expireTime)
                        goto failed_pre;
                  }
               }
            }
         }
      }

      if (fc->enabledPreFilters & FILTER_EXPIRE_TIME)
      {
         if (pPi->stop_time <= fc->expireTime)
            goto failed_pre;
      }

      // AND match against the primary parameter set
      match = EpgDbFilterMatchAct(dbc, fc, &fc->act, pPi);

      // match against additional ANDed parameter sets
      // (note: contexts are sorted by mode: AND contexts precede OR)
      fc_act = fc->act.pNext;
      while ((fc_act != NULL) && (fc_act->forkCombMode == FILTER_FORK_AND) && match)
      {
         match = EpgDbFilterMatchAct(dbc, fc, fc_act, pPi);
         fc_act = fc_act->pNext;
      }

      // match against ORed parameter sets: at least one must match if any are present
      if (match && (fc_act != NULL))
      {
         match = FALSE;
         while ((fc_act != NULL) && (match == FALSE))
         {
            assert (fc_act->forkCombMode == FILTER_FORK_OR);

            match = EpgDbFilterMatchAct(dbc, fc, fc_act, pPi);
            fc_act = fc_act->pNext;
         }
      }

      return match;
   }
   else
      fatal0("EpgDbFilter-Matches: illegal NULL ptr param");

failed_pre:
   return FALSE;
}

