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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgdbfil.c,v 1.23 2001/02/06 19:00:07 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <string.h>
#include <ctype.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgctl/epgmain.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgdbacq.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"


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
   }
   else
   {
      debug0("EpgDbFilter-CopyContext: illegal NULL ptr param");
      newfc = NULL;
   }

   return newfc;
}

// ---------------------------------------------------------------------------
// Destroy a filter context: free memory
//
void EpgDbFilterDestroyContext( FILTER_CONTEXT * fc )
{
   if (fc != NULL)
   {
      xfree(fc);
   }
   else
      debug0("EpgDbFilter-DestroyContext: illegal NULL ptr param");
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
      debug2("EpgDbFilter-SetSeries: illegal parameters: net=%d, series=%d", netwop, series);
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
      debug1("EpgDbFilter-SetFeatureFlags: illegal index %d", index);
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
      debug1("EpgDbFilter-SetNoFeatures: illegal count %d", noFeatures);
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
                     if ( (pDesc[descIdx].lang[0][langIdx] == lg[0]) &&
                          (pDesc[descIdx].lang[1][langIdx] == lg[1]) &&
                          (pDesc[descIdx].lang[2][langIdx] == lg[2]) )
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
      debug0("EpgDbFilter-SetLangDescr: DB not locked");
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
      debug0("EpgDbFilter-SetSubtDescr: DB not locked");
}

// ---------------------------------------------------------------------------
// Reset network filter
//
void EpgDbFilterInitNetwop( FILTER_CONTEXT *fc )
{
   memset(fc->netwopFilterField, 0, sizeof(fc->netwopFilterField));
}

// ---------------------------------------------------------------------------
// Enable one network in the network filter array
//
void EpgDbFilterSetNetwop( FILTER_CONTEXT *fc, uchar netwopNo )
{
   if (netwopNo < MAX_NETWOP_COUNT)
   {
      fc->netwopFilterField[netwopNo] = TRUE;
   }
   else
      debug1("EpgDbFilter-SetNetwop: illegal netwop idx %d", netwopNo);
}

// ---------------------------------------------------------------------------
// Reset network pre-filter -> all networks enabled
// - The netwop pre-filter basically works exactly like the netwop filter.
//   However having a separate list that is automatically used when the
//   netwop filter is unised greatly simplifies the handling.
// - The netwop pre-filter is only activated when the normal one is unused
//   to allow netwops outside of the prefilter to be explicitly requested,
//   e.g. by a NI menu.
// - ATTENTION: The semantics of init and set are opposite to
//              the normal netwop filter!
//
void EpgDbFilterInitNetwopPreFilter( FILTER_CONTEXT *fc )
{
   memset(fc->netwopPreFilterField, TRUE, sizeof(fc->netwopPreFilterField));
}

// ---------------------------------------------------------------------------
// Disable one network in the network pre-filter array
//
void EpgDbFilterSetNetwopPreFilter( FILTER_CONTEXT *fc, uchar netwopNo )
{
   if (netwopNo < MAX_NETWOP_COUNT)
   {
      fc->netwopPreFilterField[netwopNo] = FALSE;
   }
   else
      debug1("EpgDbFilter-SetNetwopPreFilter: illegal netwop idx %d", netwopNo);
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
// Set string for sub-string search in title, short- and long info
//
                                     //"ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞß"
const uchar latin1LowerCaseTable[32] = "àáâãäåæçèéêëìíîïðñòóôõö×øùúûüýþß";

void EpgDbFilterSetSubStr( FILTER_CONTEXT *fc, const uchar *pStr, bool ignoreCase )
{
   uchar *p, c;

   if (pStr != NULL)
   {
      strncpy(fc->subStrFilter, pStr, SUBSTR_FILTER_MAXLEN);
      fc->subStrFilter[SUBSTR_FILTER_MAXLEN] = 0;
      fc->ignoreCase = ignoreCase;

      if (ignoreCase)
      {  // convert search string to all lowercase
         p = fc->subStrFilter;
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
   }
   else
      debug0("EpgDbFilter-SetSubStr: illegal NULL param");
}

// ---------------------------------------------------------------------------
// Case-insensitive sub-string search
// - make a lower-cas copy of the haystack and then use the strstr library func.
//   the needle already has to be lower-case
//
static const char * strstri( const char *haystack, const char *needle )
{
   register const uchar *src;
   register uchar c, *dst;
   register int  len;
   uchar copy[1030];

   len = 1024;
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
// Enables one or more filters in the given context
//
void EpgDbFilterEnable( FILTER_CONTEXT *fc, uint searchFilter )
{
   assert((searchFilter & ~FILTER_ALL) == 0);
   fc->enabledFilters |= searchFilter;
}

// ---------------------------------------------------------------------------
// Disables one or more filters in the given context
//
void EpgDbFilterDisable( FILTER_CONTEXT *fc, uint searchFilter )
{
   assert((searchFilter & ~FILTER_ALL) == 0);

   if ((searchFilter & FILTER_THEMES) != 0)
   {
      fc->usedThemeClasses = 0;
   }
   if ((searchFilter & FILTER_SORTCRIT) != 0)
   {
      fc->usedSortCritClasses = 0;
   }
   if ((searchFilter & FILTER_FEATURES) != 0)
   {
      fc->featureFilterCount = 0;
   }

   fc->enabledFilters &= ~ searchFilter;
}

// ----------------------------------------------------------------------------
// Initialize a filter context and time slot for NI stack processing
//
void EpgDbFilterInitNi( FILTER_CONTEXT *fc, NI_FILTER_STATE *pNiState )
{
   pNiState->flags = NI_DATE_NONE;
   fc->enabledFilters = 0;
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
   ulong now;
   uint nowMoD;

   now = time(NULL);
   // number of minutes that have elapsed since last midnight (Minutes-of-Day)
   nowMoD = ((now + lto) % (60*60*24)) / 60;

   if (pNiState->flags != NI_DATE_NONE)
   {  // at least one time related filter is in the NI stack

      if ((pNiState->flags & NI_DATE_RELDATE) == 0)
      {  // no date given -> assume today
         pNiState->reldate = 0;
      }

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
      fc->timeBegin = now + pNiState->startMoD * 60L + pNiState->reldate * 60L*60*24;
      fc->timeEnd   = now + pNiState->stopMoD  * 60L + pNiState->reldate * 60L*60*24;

      fc->enabledFilters |= FILTER_TIME_BEG | FILTER_TIME_END;
   }
}

// ---------------------------------------------------------------------------
// Checks if the given PI block matches the settings in the given context
// - using goto to end of procedure upon first failed match for performance reasons
//
bool EpgDbFilterMatches( const EPGDB_CONTEXT *dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPi )
{
   uchar class;
   uint  index;
   bool  skipThemes = FALSE;
   
   if ((fc != NULL) && (pPi != NULL))
   {
      if (fc->enabledFilters & FILTER_NETWOP)
      {
         if (fc->netwopFilterField[pPi->netwop_no] == FALSE)
            goto failed;
      }
      else if (fc->enabledFilters & FILTER_NETWOP_PRE)
      {  // netwop pre-filter is only activated when netwop is unused
         // this way also netwops outside of the prefilter can be explicitly requested, e.g. by a NI menu
         if (fc->netwopPreFilterField[pPi->netwop_no] == FALSE)
            goto failed;
      }

      if (fc->enabledFilters & FILTER_TIME_BEG)
      {
         if (pPi->start_time < fc->timeBegin)
            goto failed;
      }

      if (fc->enabledFilters & FILTER_TIME_END)
      {
         if (pPi->start_time >= fc->timeEnd)
            goto failed;
      }

      if (fc->enabledFilters & FILTER_PAR_RAT)
      {
         if ((pPi->parental_rating == 0) || (pPi->parental_rating > fc->parentalRating))
            goto failed;
      }

      if (fc->enabledFilters & FILTER_EDIT_RAT)
      {
         if (pPi->editorial_rating < fc->editorialRating)
            goto failed;
      }

      if (fc->enabledFilters & FILTER_PROGIDX)
      {
         uint progNo = EpgDbGetProgIdx(dbc, pPi->block_no, pPi->netwop_no);
         if ((progNo < fc->firstProgIdx) || (progNo > fc->lastProgIdx))
            goto failed;
      }

      if (fc->enabledFilters & FILTER_FEATURES)
      {
         for (index=0; index < fc->featureFilterCount; index++)
         {
            if ((pPi->feature_flags & fc->featureFilterMaskField[index]) == fc->featureFilterFlagField[index])
               break;
         }
         if (index >= fc->featureFilterCount)
            goto failed;
      }

      if (fc->enabledFilters & FILTER_SERIES)
      {
         for (index=0; index < pPi->no_themes; index++)
         {
            register uchar theme = pPi->themes[index];
            if ((theme > 0x80) &&
                (fc->seriesFilterMatrix[pPi->netwop_no][theme - 0x80]))
               break;
         }
         // logical OR between series and themes filter (same filter type)
         if (index < pPi->no_themes)
         {  // series matched -> skip themes filter
            skipThemes = TRUE;
         }
         else if ((fc->enabledFilters & FILTER_THEMES) == FALSE)
         {  // series did not match -> failed only, if themes disabled
            goto failed;
         }
      }

      if ((fc->enabledFilters & FILTER_THEMES) && (skipThemes == FALSE))
      {
         for (class=1; class != 0; class <<= 1)
         {  // AND across all classes
            if (fc->usedThemeClasses & class)
            {
               for (index=0; index < pPi->no_themes; index++)
               {  // OR across all themes in a class
                  if (fc->themeFilterField[pPi->themes[index]] & class)
                  {  // ignore theme series codes if series filter is used
                     // (or a "series - general" selection couldn't be reduced by selecting particular series)
                     if ((pPi->themes[index] < 0x80) || ((fc->enabledFilters & FILTER_SERIES) == FALSE))
                        break;
                  }
               }
               if (index >= pPi->no_themes)
               {  // no match for this class -> abort (because of AND)
                  goto failed;
               }
            }
         }
      }

      if (fc->enabledFilters & FILTER_SORTCRIT)
      {
         for (class=1; class != 0; class <<= 1)
         {  // AND across all classes
            if (fc->usedSortCritClasses & class)
            {
               for (index=0; index < pPi->no_sortcrit; index++)
               {  // OR across all sorting criteria in a class
                  if (fc->sortCritFilterField[pPi->sortcrits[index]] & class)
                     break;
               }
               if (index >= pPi->no_sortcrit)
               {  // no match for this class -> abort (because of AND)
                  goto failed;
               }
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
         if (class >= pPi->no_descriptors)
         {  // none of the searched for IDs is contained in this PI
            goto failed;
         }
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
         if (class >= pPi->no_descriptors)
         {  // none of the searched for IDs is contained in this PI
            goto failed;
         }
      }

      if ((fc->enabledFilters & (FILTER_SUBSTR_TITLE|FILTER_SUBSTR_DESCR)) == (FILTER_SUBSTR_TITLE|FILTER_SUBSTR_DESCR))
      {
         if (fc->ignoreCase == FALSE)
         {
            if ( (!strstr(PI_GET_TITLE(pPi), fc->subStrFilter)) &&
                 (!PI_HAS_SHORT_INFO(pPi) || !strstr(PI_GET_SHORT_INFO(pPi), fc->subStrFilter)) &&
                 (!PI_HAS_LONG_INFO(pPi) || !strstr(PI_GET_LONG_INFO(pPi), fc->subStrFilter)) )
               goto failed;
         }
         else
         {
            if ( (!strstri(PI_GET_TITLE(pPi), fc->subStrFilter)) &&
                 (!PI_HAS_SHORT_INFO(pPi) || !strstri(PI_GET_SHORT_INFO(pPi), fc->subStrFilter)) &&
                 (!PI_HAS_SHORT_INFO(pPi) || !strstri(PI_GET_LONG_INFO(pPi), fc->subStrFilter)) )
               goto failed;
         }
      }
      else if (fc->enabledFilters & FILTER_SUBSTR_TITLE)
      {
         if (fc->ignoreCase == FALSE)
         {
            if (!strstr(PI_GET_TITLE(pPi), fc->subStrFilter))
               goto failed;
         }
         else
         {
            if (!strstri(PI_GET_TITLE(pPi), fc->subStrFilter))
               goto failed;
         }
      }
      else if (fc->enabledFilters & FILTER_SUBSTR_DESCR)
      {
         if (fc->ignoreCase == FALSE)
         {
            if ( (!PI_HAS_SHORT_INFO(pPi) || !strstr(PI_GET_SHORT_INFO(pPi), fc->subStrFilter)) &&
                 (!PI_HAS_SHORT_INFO(pPi) || !strstr(PI_GET_LONG_INFO(pPi), fc->subStrFilter)) )
               goto failed;
         }
         else
         {
            if ( (!PI_HAS_SHORT_INFO(pPi) || !strstri(PI_GET_SHORT_INFO(pPi), fc->subStrFilter)) &&
                 (!PI_HAS_SHORT_INFO(pPi) || !strstri(PI_GET_LONG_INFO(pPi), fc->subStrFilter)) )
               goto failed;
         }
      }

      // all tests have passed -> match ok
      return TRUE;
   }
   else
      debug0("EpgDbFilter-Matches: illegal NULL ptr param");

failed:
   return FALSE;
}

