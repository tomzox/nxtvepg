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
 *  $Id: epgdbfil.c,v 1.42 2003/03/19 16:17:12 tom Exp tom $
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
   NETWOP_MATCH_OK,
   NETWOP_MATCH_FAIL,
   NETWOP_MATCH_FAIL_PRE
} NETWOP_MATCH;

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
   fc->enabledFilters = 0;

   return fc;
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

      // copy sub-string chain
      if (fc->pSubStrCtx != NULL)
      {
         EPGDB_FILT_SUBSTR * pSrcWalk, ** ppDestWalk;
         uint  size;

         pSrcWalk  = fc->pSubStrCtx;
         ppDestWalk = &newfc->pSubStrCtx;
         while (pSrcWalk != NULL)
         {
            size = sizeof(EPGDB_FILT_SUBSTR) + strlen(pSrcWalk->str);
            *ppDestWalk = xmalloc(size);
            memcpy(*ppDestWalk, pSrcWalk, size);

            pSrcWalk = pSrcWalk->pNext;
            ppDestWalk = &(*ppDestWalk)->pNext;
         }
      }
      else
         newfc->pSubStrCtx = NULL;
   }
   else
   {
      fatal0("EpgDbFilter-CopyContext: illegal NULL ptr param");
      newfc = NULL;
   }

   return newfc;
}

// ---------------------------------------------------------------------------
// Destroy a filter context: free memory
//
static void EpgDbFilterDestroySubstrContext( FILTER_CONTEXT * fc )
{
   EPGDB_FILT_SUBSTR * pWalk;
   EPGDB_FILT_SUBSTR * pNext;

   pWalk = fc->pSubStrCtx;
   while (pWalk != NULL)
   {
      pNext = pWalk->pNext;
      xfree(pWalk);
      pWalk = pNext;
   }
   fc->pSubStrCtx = NULL;
}

// ---------------------------------------------------------------------------
// Destroy a filter context: free memory
//
void EpgDbFilterDestroyContext( FILTER_CONTEXT * fc )
{
   if (fc != NULL)
   {
      EpgDbFilterDestroySubstrContext(fc);
      xfree(fc);
   }
   else
      fatal0("EpgDbFilter-DestroyContext: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Update netwop match cache
// - fill a table with a boolean for every network index which tells the match
//   function if a given network matches the current filter settings
// - network matches depend on the network pre-filter, network filter if
//   enabled and possibly network filter inversion if enabled; the cache must
//   be invalidated if any of those parameters change
// - note: the cache does not include the NETWOP_PRE2 filter because that one
//   is a separate layer which is not affected by inversion and is intended to
//   be used by the GUI to limit network matches to a sub-set of networks
//
static void EpgDbFilterUpdateNetwopCache( FILTER_CONTEXT *fc )
{
   NETWOP_MATCH  matchCode;
   bool  fail, invert;
   uchar netwop;

   invert = ((fc->invertedFilters & FILTER_NETWOP) != FALSE);

   for (netwop = 0; netwop < MAX_NETWOP_COUNT; netwop++)
   {
      if (fc->enabledFilters & FILTER_NETWOP)
      {
         fail = (fc->netwopFilterField[netwop] == FALSE);

         if (fail ^ invert)
         {
            // if the network is also excluded by pre-filter, it's excluded from global inversion
            if ( (fc->enabledFilters & FILTER_NETWOP_PRE) &&
                 (fc->netwopPreFilter1[netwop] == FALSE) )
               matchCode = NETWOP_MATCH_FAIL_PRE;
            else
               matchCode = NETWOP_MATCH_FAIL;
         }
         else
         {
            // do not include pre-filtered netwops with inverted network selection
            if ( invert && (fc->enabledFilters & FILTER_NETWOP_PRE) &&
                 (fc->netwopPreFilter1[netwop] == FALSE) )
               matchCode = NETWOP_MATCH_FAIL_PRE;
            else
               matchCode = NETWOP_MATCH_OK;
         }
      }
      else if (fc->enabledFilters & FILTER_NETWOP_PRE)
      {  // netwop pre-filter is only activated when netwop is unused
         // this way also netwops outside of the prefilter can be explicitly requested, e.g. by a NI menu
         if (fc->netwopPreFilter1[netwop] == FALSE)
            matchCode = NETWOP_MATCH_FAIL_PRE;
         else
            matchCode = NETWOP_MATCH_OK;
      }
      else
         matchCode = NETWOP_MATCH_OK;

      // code 0: match; 1: fail (0 and 1 may be inverted later); 2: pre-filtered
      fc->netwopCache[netwop] = matchCode;
   }

   fc->netwopCacheUpdated = TRUE;
}

// ---------------------------------------------------------------------------
// Query which netwops are filtered out after combining & inverting
// - intended to be used by the GUI to determine the NETWOP_PRE2 filter as a
//   sub-set of the full set of matching netwops
//
void EpgDbFilterGetNetwopFilter( FILTER_CONTEXT *fc, uchar * pNetFilter, uint count )
{
   uint  idx;

   if (fc->netwopCacheUpdated == FALSE)
   {
      EpgDbFilterUpdateNetwopCache(fc);
   }

   for (idx=0; idx < count; idx++)
   {
      pNetFilter[idx] =  (fc->netwopCache[idx] == NETWOP_MATCH_OK) ||
                        ((fc->netwopCache[idx] == NETWOP_MATCH_FAIL) && (fc->enabledFilters & FILTER_INVERT));
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
      memset(fc->themeFilterField, 0, sizeof(fc->themeFilterField));
      fc->usedThemeClasses = 0;
   }
   else
   {  // clear the setting of selected classes only
      for (index=0; index < 256; index++)
      {
         fc->themeFilterField[index] &= ~ themeClassBitField;
      }
      fc->usedThemeClasses &= ~ themeClassBitField;
   }

   return fc->usedThemeClasses;
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
      fc->themeFilterField[index] |= themeClassBitField;
   }
   fc->usedThemeClasses |= themeClassBitField;
}

// ---------------------------------------------------------------------------
// Reset the series filter state
//
void EpgDbFilterInitSeries( FILTER_CONTEXT *fc )
{
   memset(fc->seriesFilterMatrix, 0, sizeof(fc->seriesFilterMatrix));
}

// ---------------------------------------------------------------------------
// Enable one programme in the series filter matrix
//
void EpgDbFilterSetSeries( FILTER_CONTEXT *fc, uchar netwop, uchar series, bool enable )
{
   if ((netwop < MAX_NETWOP_COUNT) && (series > 0x80))
   {
      fc->seriesFilterMatrix[netwop][series - 0x80] = enable;
   }
   else
      fatal2("EpgDbFilter-SetSeries: illegal parameters: net=%d, series=%d", netwop, series);
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
      memset(fc->sortCritFilterField, 0, sizeof(fc->sortCritFilterField));
      fc->usedSortCritClasses = 0;
   }
   else
   {  // clear the setting of selected classes only
      for (index=0; index < 256; index++)
      {
         fc->sortCritFilterField[index] &= ~ sortCritClassBitField;
      }
      fc->usedSortCritClasses &= ~ sortCritClassBitField;
   }

   return fc->usedSortCritClasses;
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
      fc->sortCritFilterField[index] |= sortCritClassBitField;
   }
   fc->usedSortCritClasses |= sortCritClassBitField;
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

   fc->parentalRating = newParentalRating;
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

   fc->editorialRating = newEditorialRating;
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

      fc->featureFilterFlagField[index] = flags;
      fc->featureFilterMaskField[index] = mask;
   }
   else
      fatal1("EpgDbFilter-SetFeatureFlags: illegal index %d", index);
}

// ---------------------------------------------------------------------------
// Set the number of used feature-bits filters
//
void EpgDbFilterSetNoFeatures( FILTER_CONTEXT *fc, uchar noFeatures )
{
   if (fc->featureFilterCount < FEATURE_CLASS_COUNT)
   {
      fc->featureFilterCount = noFeatures;
   }
   else
      fatal1("EpgDbFilter-SetNoFeatures: illegal count %d", noFeatures);
}

// ---------------------------------------------------------------------------
// Get the number of used feature-bits filters
//
uchar EpgDbFilterGetNoFeatures( FILTER_CONTEXT *fc )
{
   assert(fc->featureFilterCount < FEATURE_CLASS_COUNT);
   return fc->featureFilterCount;
}

// ---------------------------------------------------------------------------
// Reset the language filter
//
void EpgDbFilterInitLangDescr( FILTER_CONTEXT *fc )
{
   memset(fc->langDescrTable, 0, sizeof(fc->langDescrTable));
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
                        fc->langDescrTable[netwop][pDesc[descIdx].id >> 8] |= 1 << (pDesc[descIdx].id & 7);
                        break;
                     }
                  }
               }
            }
         }
      }
   }
   else
      fatal0("EpgDbFilter-SetLangDescr: DB not locked");
}

// ---------------------------------------------------------------------------
// Reset subtitle filter
//
void EpgDbFilterInitSubtDescr( FILTER_CONTEXT *fc )
{
   memset(fc->subtDescrTable, 0, sizeof(fc->subtDescrTable));
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
                        fc->subtDescrTable[netwop][pDesc[descIdx].id >> 8] |= 1 << (pDesc[descIdx].id & 7);
                        break;
                     }
                  }
               }
            }
         }
      }
   }
   else
      fatal0("EpgDbFilter-SetSubtDescr: DB not locked");
}

// ---------------------------------------------------------------------------
// Reset network filter
//
void EpgDbFilterInitNetwop( FILTER_CONTEXT *fc )
{
   memset(fc->netwopFilterField, FALSE, sizeof(fc->netwopFilterField));

   fc->netwopCacheUpdated = FALSE;
}

// ---------------------------------------------------------------------------
// Enable one network in the network filter array
//
void EpgDbFilterSetNetwop( FILTER_CONTEXT *fc, uchar netwopNo )
{
   if (netwopNo < MAX_NETWOP_COUNT)
   {
      fc->netwopFilterField[netwopNo] = TRUE;

      fc->netwopCacheUpdated = FALSE;
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

   fc->netwopCacheUpdated = FALSE;
}

// ---------------------------------------------------------------------------
// Disable one network in the network pre-filter array
//
void EpgDbFilterSetNetwopPreFilter( FILTER_CONTEXT *fc, uchar netwopNo )
{
   if (netwopNo < MAX_NETWOP_COUNT)
   {
      fc->netwopPreFilter1[netwopNo] = FALSE;

      fc->netwopCacheUpdated = FALSE;
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
   fc->timeBegin = newTimeBegin;
}

// ---------------------------------------------------------------------------
// Set max value for start time filter
//
void EpgDbFilterSetDateTimeEnd( FILTER_CONTEXT *fc, ulong newTimeEnd )
{
   fc->timeEnd = newTimeEnd;
}

// ---------------------------------------------------------------------------
// Set minimum and maximum programme duration values
//
void EpgDbFilterSetMinMaxDuration( FILTER_CONTEXT *fc, uint dur_min, uint dur_max )
{
   assert(dur_max >= dur_min);
   assert(dur_max < 24*60*60);

   fc->duration_min = dur_min;
   fc->duration_max = dur_max;
}

// ---------------------------------------------------------------------------
// Set value for prog-no filter
// - i.e the range of indices relative to the start block no in AI
// - 0 refers to the currently running programme
//
void EpgDbFilterSetProgIdx( FILTER_CONTEXT *fc, uchar newFirstProgIdx, uchar newLastProgIdx )
{
   assert(newFirstProgIdx <= newLastProgIdx);

   fc->firstProgIdx = newFirstProgIdx;
   fc->lastProgIdx  = newLastProgIdx;
}

// ---------------------------------------------------------------------------
// Set minimum and maximum programme duration values
//
void EpgDbFilterSetVpsPdcMode( FILTER_CONTEXT *fc, uint mode )
{
   assert(mode <= 2);

   fc->vps_pdc_mode = mode;
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
   uchar *p, c;

   assert(scopeTitle || scopeDesc);

   if (pStr != NULL)
   {
      pSubStrCtx = xmalloc(sizeof(EPGDB_FILT_SUBSTR) + strlen(pStr) + 100);

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

      // link the new context at the head of the substring context chain
      pSubStrCtx->pNext = fc->pSubStrCtx;
      fc->pSubStrCtx = pSubStrCtx;
   }
   else
      fatal0("EpgDbFilter-SetSubStr: illegal NULL param");
}

// ---------------------------------------------------------------------------
// Case-insensitive sub-string search
// - make a lower-cas copy of the haystack and then use the strstr library func.
//   the needle already has to be lower-case
//
static char * strstri( const char *haystack, const char *needle )
{
   register const uchar *src;
   register uchar c, *dst;
   register int  len;
   uchar copy[2048+4];

   len = 2048;
   dst = copy;
   src = haystack;
   while ( ((c = *(src++)) != 0) && len )
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

   return strstr(copy, needle);
}

// ---------------------------------------------------------------------------
// Compare text with search string accorgind to search parameters
// - parameter #1: ignore case
// - parameter #2: exact => search string must match start and end of text
//
static bool xstrcmp( const EPGDB_FILT_SUBSTR *ssc, const char * str )
{
   if (ssc->strMatchFull == FALSE)
   {
      return ((ssc->strMatchCase ? strstr : strstri)(str, ssc->str) != NULL);
   }
   else
   {
      return ((ssc->strMatchCase ? strcmp : strcasecmp)(str, ssc->str) == 0);
   }
}

// ---------------------------------------------------------------------------
// Enables one or more filters in the given context
//
void EpgDbFilterEnable( FILTER_CONTEXT *fc, uint searchFilter )
{
   assert((searchFilter & ~FILTER_ALL) == 0);
   assert(((searchFilter & FILTER_SUBSTR) == 0) || (fc->pSubStrCtx != NULL));

   if ( (fc->enabledFilters ^ (fc->enabledFilters | searchFilter)) & ((FILTER_NETWOP_PRE | FILTER_NETWOP)) )
   {
      fc->netwopCacheUpdated = FALSE;
   }

   fc->enabledFilters |= searchFilter;
}

// ---------------------------------------------------------------------------
// Disables one or more filters in the given context
//
void EpgDbFilterDisable( FILTER_CONTEXT *fc, uint searchFilter )
{
   assert((searchFilter & ~FILTER_ALL) == 0);

   if ((searchFilter & FILTER_SUBSTR) != 0)
   {
      EpgDbFilterDestroySubstrContext(fc);
   }
   if ((searchFilter & FILTER_SORTCRIT) != 0)
   {
      fc->usedSortCritClasses = 0;
   }
   if ((searchFilter & FILTER_FEATURES) != 0)
   {
      fc->featureFilterCount = 0;
   }
   if ( (fc->enabledFilters ^ (fc->enabledFilters & searchFilter)) & ((FILTER_NETWOP_PRE | FILTER_NETWOP)) )
   {
      fc->netwopCacheUpdated = FALSE;
   }

   fc->enabledFilters &= ~ searchFilter;

}

// ----------------------------------------------------------------------------
// Invert the result of given filter types when matching against PI
// - note: for global inversion of the combined filtering result there is a
//   separate filter type which is enabled via the regular filter type enable func
//
void EpgDbFilterInvert( FILTER_CONTEXT *fc, uint mask, uchar themeClass, uchar sortCritClass )
{
   assert((mask & (FILTER_PERM | FILTER_INVERT)) == 0);  // inverting these is not supported

   if (((fc->invertedFilters ^ mask) & FILTER_NETWOP) != 0)
   {
      fc->netwopCacheUpdated = FALSE;
   }

   fc->invertedFilters         = mask;
   fc->invertedThemeClasses    = themeClass;
   fc->invertedSortCritClasses = sortCritClass;
}

// ----------------------------------------------------------------------------
// Initialize a filter context and time slot for NI stack processing
//
void EpgDbFilterInitNi( FILTER_CONTEXT *fc, NI_FILTER_STATE *pNiState )
{
   pNiState->flags = NI_DATE_NONE;

   // reset all filter settings except pre-filters
   fc->enabledFilters &= FILTER_PERM;

   fc->netwopCacheUpdated = FALSE;
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
         if ((fc->enabledFilters & FILTER_PROGIDX) == FALSE)
            fc->lastProgIdx = (uchar)(data & 0xff);
         fc->firstProgIdx = (uchar)(data & 0xff);
         fc->enabledFilters |= FILTER_PROGIDX;
         break;

      case EV_ATTRIB_KIND_PROGNO_STOP:
         if ((fc->enabledFilters & FILTER_PROGIDX) == FALSE)
            fc->firstProgIdx = 0;
         fc->lastProgIdx = (uchar)(data & 0xff);
         fc->enabledFilters |= FILTER_PROGIDX;
         break;

      case EV_ATTRIB_KIND_NETWOP:
         if ((fc->enabledFilters & FILTER_NETWOP) == FALSE)
            EpgDbFilterInitNetwop(fc);
         fc->netwopFilterField[data & 0xff] = TRUE;
         fc->netwopCacheUpdated = FALSE;
         fc->enabledFilters |= FILTER_NETWOP;
         break;

      case EV_ATTRIB_KIND_THEME:
      case EV_ATTRIB_KIND_THEME + 1:
      case EV_ATTRIB_KIND_THEME + 2:
      case EV_ATTRIB_KIND_THEME + 3:
      case EV_ATTRIB_KIND_THEME + 4:
      case EV_ATTRIB_KIND_THEME + 5:
      case EV_ATTRIB_KIND_THEME + 6:
      case EV_ATTRIB_KIND_THEME + 7:
         if ((fc->enabledFilters & FILTER_THEMES) == FALSE)
            EpgDbFilterInitThemes(fc, 0xff);
         class = 1 << (kind - EV_ATTRIB_KIND_THEME);
         fc->themeFilterField[data & 0xff] |= class;
         fc->usedThemeClasses |= class;
         fc->enabledFilters |= FILTER_THEMES;
         break;

      case EV_ATTRIB_KIND_SORTCRIT:
      case EV_ATTRIB_KIND_SORTCRIT + 1:
      case EV_ATTRIB_KIND_SORTCRIT + 2:
      case EV_ATTRIB_KIND_SORTCRIT + 3:
      case EV_ATTRIB_KIND_SORTCRIT + 4:
      case EV_ATTRIB_KIND_SORTCRIT + 5:
      case EV_ATTRIB_KIND_SORTCRIT + 6:
      case EV_ATTRIB_KIND_SORTCRIT + 7:
         if ((fc->enabledFilters & FILTER_SORTCRIT) == FALSE)
            EpgDbFilterInitSortCrit(fc, 0xff);
         class = 1 << (kind - EV_ATTRIB_KIND_SORTCRIT);
         fc->sortCritFilterField[data & 0xff] |= class;
         fc->usedSortCritClasses |= class;
         fc->enabledFilters |= FILTER_SORTCRIT;
         break;

      case EV_ATTRIB_KIND_EDITORIAL:
         fc->editorialRating = (uchar)(data & 0xff);
         fc->enabledFilters |= FILTER_EDIT_RAT;
         break;

      case EV_ATTRIB_KIND_PARENTAL:
         fc->parentalRating = (uchar)(data & 0xff);
         fc->enabledFilters |= FILTER_PAR_RAT;
         break;

      case EV_ATTRIB_KIND_FEATURES:
         if ((fc->enabledFilters & FILTER_FEATURES) == FALSE)
            fc->featureFilterCount = 0;
         if (fc->featureFilterCount < FEATURE_CLASS_COUNT - 1)
         {
            fc->featureFilterFlagField[fc->featureFilterCount] = data & 0xfff;
            fc->featureFilterMaskField[fc->featureFilterCount] = data >> 12;
            fc->featureFilterCount += 1;
            fc->enabledFilters |= FILTER_FEATURES;
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
         if ((fc->enabledFilters & FILTER_LANGUAGES) == FALSE)
            EpgDbFilterInitLangDescr(fc);
         lg[0] = (uchar)(data & 0xff);
         lg[1] = (uchar)((data >> 8) & 0xff);
         lg[2] = (uchar)((data >> 16) & 0xff);
         EpgDbFilterSetLangDescr(dbc, fc, lg);
         fc->enabledFilters |= FILTER_LANGUAGES;
         break;

      case EV_ATTRIB_KIND_SUBT_LANG:
         if ((fc->enabledFilters & FILTER_SUBTITLES) == FALSE)
            EpgDbFilterInitSubtDescr(fc);
         lg[0] = (uchar)(data & 0xff);
         lg[1] = (uchar)((data >> 8) & 0xff);
         lg[2] = (uchar)((data >> 16) & 0xff);
         EpgDbFilterSetSubtDescr(dbc, fc, lg);
         fc->enabledFilters |= FILTER_SUBTITLES;
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
         fc->timeBegin = now + pNiState->startMoD * 60 + pNiState->reldate * 60*60*24;
         fc->timeEnd   = now + pNiState->stopMoD  * 60 + pNiState->reldate * 60*60*24;

         fc->enabledFilters |= FILTER_TIME_ONCE;
      }
      else
      {  // no date given -> allow given time window on any day
         fc->timeBegin       = pNiState->startMoD * 60;
         fc->timeEnd         = pNiState->stopMoD  * 60;
         fc->enabledFilters |= FILTER_TIME_DAILY;
      }
   }
}

// ---------------------------------------------------------------------------
// Checks if the given PI block matches the settings in the given context
// - using goto to end of procedure upon first failed match for performance reasons
//
bool EpgDbFilterMatches( const EPGDB_CONTEXT *dbc, FILTER_CONTEXT *fc, const PI_BLOCK * pPi )
{
   uchar class;
   uint  index;
   bool  fail, invert;
   bool  fail_buf;
   bool  skipThemes = FALSE;
   bool  skipSubstr = FALSE;
   bool  orSeriesThemes = FALSE;
   bool  orSeriesSubstr = FALSE;
   
   if ((fc != NULL) && (pPi != NULL))
   {
      // variable to temporarily keep track of failed matches, because matches
      // cannot be aborted with "goto failed" until all possible "failed_pre"
      // matches are through
      fail_buf = FALSE;

      if (fc->enabledFilters & FILTER_NETWOP_PRE2)
      {
         if (fc->netwopPreFilter2[pPi->netwop_no] == FALSE)
            goto failed_pre;
      }

      if (fc->enabledFilters & (FILTER_NETWOP | FILTER_NETWOP_PRE))
      {
         if (fc->netwopCacheUpdated == FALSE)
         {  // network match cache needs to be initialized (e.g. after parameter change)
            EpgDbFilterUpdateNetwopCache(fc);
         }

         // query the cache for the match result
         if (fc->netwopCache[pPi->netwop_no] != NETWOP_MATCH_OK)
         {
            if (fc->netwopCache[pPi->netwop_no] == NETWOP_MATCH_FAIL)
               fail_buf = TRUE;
            else
               goto failed_pre;
         }
      }

      if (fc->enabledFilters & FILTER_AIR_TIMES)
      {
         if ( (fc->netwopAirTimeStart[pPi->netwop_no] != 0) ||
              (fc->netwopAirTimeStop[pPi->netwop_no] != 0) )
         {
            sint  lto = EpgLtoGet(pPi->start_time);
            uint  airStartMoD = fc->netwopAirTimeStart[pPi->netwop_no];
            uint  airStopMoD  = fc->netwopAirTimeStop[pPi->netwop_no];
            uint  piStartMoD  = ((pPi->start_time + lto) % (60*60*24)) / 60;
            uint  piStopMoD   = ((pPi->stop_time + lto) % (60*60*24)) / 60;

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

               if (fc->enabledFilters & FILTER_EXPIRE_TIME)
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

               if (fc->enabledFilters & FILTER_EXPIRE_TIME)
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

      if (fc->enabledFilters & FILTER_EXPIRE_TIME)
      {
         if (pPi->stop_time <= fc->expireTime)
            goto failed_pre;
      }

      if (fc->enabledFilters & FILTER_TIME_ONCE)
      {
         fail   = ((pPi->start_time < fc->timeBegin) || (pPi->start_time >= fc->timeEnd));

         invert = ((fc->invertedFilters & FILTER_TIME_ONCE) != FALSE);
         if (fail ^ invert)
            goto failed_pre;
      }
      else if (fc->enabledFilters & FILTER_TIME_DAILY)
      {
         sint    lto       = EpgLtoGet(pPi->start_time);
         time_t  timeOfDay = (pPi->start_time + lto) % (24*60*60);

         if (fc->timeBegin < fc->timeEnd)
            fail   = ((timeOfDay < fc->timeBegin) || (timeOfDay >= fc->timeEnd));
         else
            fail   = ((timeOfDay < fc->timeBegin) && (timeOfDay >= fc->timeEnd));

         invert = ((fc->invertedFilters & FILTER_TIME_DAILY) != FALSE);
         if (fail ^ invert)
            goto failed_pre;
      }

      if (fc->enabledFilters & FILTER_DURATION)
      {
         uint duration = pPi->stop_time - pPi->start_time;

         fail   = ((duration < fc->duration_min) || (duration > fc->duration_max));

         invert = ((fc->invertedFilters & FILTER_DURATION) != FALSE);
         if (fail ^ invert)
            fail_buf = TRUE;
      }

      if (fc->enabledFilters & FILTER_PAR_RAT)
      {
         if (pPi->parental_rating != 0)
         {
            fail   = (pPi->parental_rating > fc->parentalRating);

            invert = ((fc->invertedFilters & FILTER_PAR_RAT) != FALSE);
            if (fail ^ invert)
               fail_buf = TRUE;
         }
         else
         {  // do not include unrated programmes via global inversion
            goto failed_pre;
         }
      }

      if (fc->enabledFilters & FILTER_EDIT_RAT)
      {
         if (pPi->editorial_rating != 0)
         {
            fail   = (pPi->editorial_rating < fc->editorialRating);

            invert = ((fc->invertedFilters & FILTER_EDIT_RAT) != FALSE);
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

      if (fc->enabledFilters & FILTER_PROGIDX)
      {
         uint progNo = EpgDbGetProgIdx(dbc, pPi);
         fail   = ((progNo < fc->firstProgIdx) || (progNo > fc->lastProgIdx));

         invert = ((fc->invertedFilters & FILTER_PROGIDX) != FALSE);
         if (fail ^ invert)
            goto failed;
      }

      if (fc->enabledFilters & FILTER_FEATURES)
      {
         for (index=0; index < fc->featureFilterCount; index++)
         {
            if ((pPi->feature_flags & fc->featureFilterMaskField[index]) == fc->featureFilterFlagField[index])
               break;
         }
         fail   = (index >= fc->featureFilterCount);

         invert = ((fc->invertedFilters & FILTER_FEATURES) != FALSE);
         if (fail ^ invert)
            goto failed;
      }

      if ( ((fc->enabledFilters & (FILTER_SERIES | FILTER_THEMES)) == (FILTER_SERIES | FILTER_THEMES)) &&
           (((fc->invertedFilters & (FILTER_SERIES | FILTER_THEMES)) == 0) ||
            ((fc->invertedFilters & (FILTER_SERIES | FILTER_THEMES)) == (FILTER_SERIES | FILTER_THEMES)) ) )
      {  // both themes and series filters are used -> logical OR (unless only one of theme is inverted)
         orSeriesThemes = TRUE;
      }

      if ( ((fc->enabledFilters & (FILTER_SERIES | FILTER_SUBSTR)) == (FILTER_SERIES | FILTER_SUBSTR)) &&
           (((fc->invertedFilters & (FILTER_SERIES | FILTER_SUBSTR)) == 0) ||
            ((fc->invertedFilters & (FILTER_SERIES | FILTER_SUBSTR)) == (FILTER_SERIES | FILTER_SUBSTR)) ) )
      {  // both themes and series filters are used -> logical OR (unless only one of theme is inverted)
         orSeriesSubstr = TRUE;
      }

      if (fc->enabledFilters & FILTER_SERIES)
      {
         fail = TRUE;
         for (index=0; index < pPi->no_themes; index++)
         {
            register uchar theme = pPi->themes[index];
            if ((theme > 0x80) &&
                (fc->seriesFilterMatrix[pPi->netwop_no][theme - 0x80]))
            {
               fail = FALSE;
               break;
            }
         }
         invert = ((fc->invertedFilters & FILTER_SERIES) != FALSE);
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

      if ((fc->enabledFilters & FILTER_THEMES) && (skipThemes == FALSE))
      {
         for (class=1; class != 0; class <<= 1)
         {  // AND across all classes
            if (fc->usedThemeClasses & class)
            {
               fail = TRUE;
               for (index=0; index < pPi->no_themes; index++)
               {  // OR across all themes in a class
                  if (fc->themeFilterField[pPi->themes[index]] & class)
                  {  // ignore theme series codes if series filter is used
                     // (or a "series - general" selection couldn't be reduced by selecting particular series)
                     if ( (pPi->themes[index] < 0x80) ||
                          ((fc->enabledFilters & FILTER_SERIES) == FALSE) ||
                          ((fc->invertedFilters & FILTER_SERIES) != FALSE) /* ||
                          ((fc->invertedFilters & (FILTER_THEMES | FILTER_SERIES)) != FALSE)*/ )
                     {
                        fail = FALSE;
                        break;
                     }
                  }
               }

               invert = ((fc->invertedThemeClasses & class) != FALSE);
               if (fail ^ invert)
                  goto failed;
            }
         }
      }

      if (fc->enabledFilters & FILTER_SORTCRIT)
      {
         for (class=1; class != 0; class <<= 1)
         {  // AND across all classes
            if (fc->usedSortCritClasses & class)
            {
               fail = TRUE;
               for (index=0; index < pPi->no_sortcrit; index++)
               {  // OR across all sorting criteria in a class
                  if (fc->sortCritFilterField[pPi->sortcrits[index]] & class)
                  {
                     fail = FALSE;
                     break;
                  }
               }

               invert = ((fc->invertedSortCritClasses & class) != FALSE);
               if (fail ^ invert)
                  goto failed;
            }
         }
      }

      if (fc->enabledFilters & FILTER_LANGUAGES)
      {
         const DESCRIPTOR *pDesc = PI_GET_DESCRIPTORS(pPi);
         for (class=0; class < pPi->no_descriptors; pPi++)
         {
            if ( (pDesc->type == LI_DESCR_TYPE) &&
                 (fc->langDescrTable[pPi->netwop_no][pDesc->id / 8] & (1 << (pDesc->id % 8))) )
            {  // bit for this ID is set in the bit field -> match
               break;
            }
            pDesc += 1;
         }
         // failed if none of the searched for IDs is contained in this PI
         fail   = (class >= pPi->no_descriptors);

         invert = ((fc->invertedFilters & FILTER_LANGUAGES) != FALSE);
         if (fail ^ invert)
            goto failed;
      }

      if (fc->enabledFilters & FILTER_SUBTITLES)
      {
         const DESCRIPTOR *pDesc = PI_GET_DESCRIPTORS(pPi);
         for (class=0; class < pPi->no_descriptors; pPi++)
         {
            if ( (pDesc->type == TI_DESCR_TYPE) &&
                 (fc->subtDescrTable[pPi->netwop_no][pDesc->id / 8] & (1 << (pDesc->id % 8))) )
            {  // bit for this ID is set in the bit field -> match
               break;
            }
         }
         // failed if none of the searched for IDs is contained in this PI
         fail   = (class >= pPi->no_descriptors);

         invert = ((fc->invertedFilters & FILTER_SUBTITLES) != FALSE);
         if (fail ^ invert)
            goto failed;
      }

      if ((fc->enabledFilters & FILTER_SUBSTR) && (skipSubstr == FALSE))
      {
         EPGDB_FILT_SUBSTR * pSubStrCtx;

         pSubStrCtx = fc->pSubStrCtx;
         fail = TRUE;
         // OR across all substring text matches
         while (pSubStrCtx != NULL)
         {
            if (pSubStrCtx->scopeTitle)
            {
               if (xstrcmp(pSubStrCtx, PI_GET_TITLE(pPi)))
               {
                  fail = FALSE;
                  break;
               }
            }
            if (pSubStrCtx->scopeDesc)
            {
               if ( (PI_HAS_SHORT_INFO(pPi) && xstrcmp(pSubStrCtx, PI_GET_SHORT_INFO(pPi))) ||
                    (PI_HAS_LONG_INFO(pPi) && xstrcmp(pSubStrCtx, PI_GET_LONG_INFO(pPi))) )
               {
                  fail = FALSE;
                  break;
               }
            }
            pSubStrCtx = pSubStrCtx->pNext;
         }

         invert = ((fc->invertedFilters & FILTER_SUBSTR) != FALSE);
         if (fail ^ invert)
            goto failed;
      }

      if (fc->enabledFilters & FILTER_VPS_PDC)
      {
         if (fc->vps_pdc_mode == 1)
         {
            fail = (pPi->pil == INVALID_VPS_PIL);
         }
         else if (fc->vps_pdc_mode == 2)
         {
            struct tm * pTm = localtime(&pPi->start_time);
            if (pPi->pil == INVALID_VPS_PIL)
               fail = TRUE;
            else if ( (pTm->tm_min == (pPi->pil & 0x3f)) &&
                      (pTm->tm_hour == ((pPi->pil >>  6) & 0x1f)) &&
                      (pTm->tm_mday == ((pPi->pil >> 15) & 0x1f)) &&
                      (pTm->tm_mon + 1 == ((pPi->pil >> 11) & 0x0f)) )
               fail = TRUE;
            else
               fail = FALSE;
         }
         else  // illegal mode
            fail = TRUE;

         invert = ((fc->invertedFilters & FILTER_VPS_PDC) != FALSE);
         if (fail ^ invert)
            goto failed;
      }

      // all tests have passed -> match ok
      if (fc->enabledFilters & FILTER_INVERT)
         return FALSE;
      else
         return TRUE;
   }
   else
      fatal0("EpgDbFilter-Matches: illegal NULL ptr param");

failed:
   if (fc->enabledFilters & FILTER_INVERT)
      return TRUE;
failed_pre:
      return FALSE;
}

