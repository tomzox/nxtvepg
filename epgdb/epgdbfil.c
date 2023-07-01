/*
 *  Nextview EPG block database search filters
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgvbi/ttxdecode.h"
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


#define DUP_ARRAY(PSRC, PDST, COUNT) do{\
      if (PSRC != NULL) \
      { \
         uint size = sizeof(PSRC[0]) * COUNT; \
         PDST = xmalloc(size); \
         memcpy(PDST, PSRC, size); \
      }} while(0)


// ---------------------------------------------------------------------------
// Create and initialize a filter context
// - when the context is no longer used, it has to be destroyed (i.e. freed)
//
FILTER_CONTEXT * EpgDbFilterCreateContext( void )
{
   FILTER_CONTEXT * fc;

   fc = (FILTER_CONTEXT *) (FILTER_CONTEXT*) xmalloc(sizeof(*fc));
   memset(fc, 0, sizeof(*fc));
   fc->pFocus = &fc->act;
   fc->act.forkCombMode = FILTER_FORK_AND;

   return fc;
}

// ---------------------------------------------------------------------------
// Copy a filter parameter chain: make a linked copy of all linked elements
//
static EPGDB_FILT_SUBSTR * EpgDbFilterCopySubContexts( EPGDB_FILT_SUBSTR * pCtx )
{
   EPGDB_FILT_SUBCTX_GENERIC   * pFirst;
   EPGDB_FILT_SUBCTX_GENERIC   * pSrcWalk;
   EPGDB_FILT_SUBCTX_GENERIC  ** ppDestWalk;

   pSrcWalk   = (EPGDB_FILT_SUBCTX_GENERIC*) pCtx;  //FIXME CC
   pFirst     = NULL;
   ppDestWalk = NULL;

   while (pSrcWalk != NULL)
   {
      assert(pSrcWalk->elem_size != 0);

      if (ppDestWalk == NULL)
      {  // first copied element
         pFirst = (EPGDB_FILT_SUBCTX_GENERIC*) xmalloc(pSrcWalk->elem_size);
         memcpy(pFirst, pSrcWalk, pSrcWalk->elem_size);
         ppDestWalk = &(pFirst->pNext);
      }
      else
      {
         *ppDestWalk = (EPGDB_FILT_SUBCTX_GENERIC*) xmalloc(pSrcWalk->elem_size);
         memcpy(*ppDestWalk, pSrcWalk, pSrcWalk->elem_size);
         ppDestWalk = &(*ppDestWalk)->pNext;
      }
      pSrcWalk = pSrcWalk->pNext;
   }
   return (EPGDB_FILT_SUBSTR*) pFirst;  //FIXME CC
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

      // copy dynamically allocated arrays
      DUP_ARRAY(fc->pNetwopPreFilter1, newfc->pNetwopPreFilter1, fc->netwopCount);
      DUP_ARRAY(fc->pNetwopPreFilter2, newfc->pNetwopPreFilter2, fc->netwopCount);
      DUP_ARRAY(fc->pNetwopAirTimeStart, newfc->pNetwopAirTimeStart, fc->netwopCount);
      DUP_ARRAY(fc->pNetwopAirTimeStop, newfc->pNetwopAirTimeStop, fc->netwopCount);

      // do not copy forked contexts
      newfc->act.pNext = NULL;
      newfc->pFocus    = &newfc->act;

      // do not copy custom filter parameters (XXX would need custom copy function)
      newfc->act.pCustomArg          = NULL;
      newfc->act.pCustomFilterFunc   = NULL;
      newfc->act.pCustomDestroyFunc  = NULL;

      // copy dynamically allocated arrays
      DUP_ARRAY(fc->act.pNetwopFilterField, newfc->act.pNetwopFilterField, fc->act.netwopCount);
      DUP_ARRAY(fc->act.themeFilterField, newfc->act.themeFilterField, fc->act.themeCount);

      // copy filter parameter chain
      newfc->act.pSubStrCtx = EpgDbFilterCopySubContexts(fc->act.pSubStrCtx);
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
static void EpgDbFilterDestroyParamChain( EPGDB_FILT_SUBSTR * pCtx )
{
   EPGDB_FILT_SUBCTX_GENERIC * pWalk;
   EPGDB_FILT_SUBCTX_GENERIC * pNext;

   pWalk = (EPGDB_FILT_SUBCTX_GENERIC*) pCtx;  //FIXME CC
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
      if ( (fc_act->pCustomArg != NULL) &&
           (fc_act->pCustomDestroyFunc != NULL) )
         fc_act->pCustomDestroyFunc(fc_act->pCustomArg);

      if (fc_act->pNetwopFilterField != NULL)
         xfree(fc_act->pNetwopFilterField);

      if (fc_act->themeFilterField != NULL)
         xfree(fc_act->themeFilterField);

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

      if (fc->pNetwopPreFilter1 != NULL)
         xfree(fc->pNetwopPreFilter1);
      if (fc->pNetwopPreFilter2 != NULL)
         xfree(fc->pNetwopPreFilter2);
      if (fc->pNetwopAirTimeStart != NULL)
         xfree(fc->pNetwopAirTimeStart);
      if (fc->pNetwopAirTimeStop != NULL)
         xfree(fc->pNetwopAirTimeStop);

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

   pNew = (FILTER_CTX_ACT*) xmalloc(sizeof(FILTER_CTX_ACT));
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
uchar * EpgDbFilterGetNetwopFilter( FILTER_CONTEXT *fc, uint count )
{
   FILTER_CTX_ACT  * fc_act;
   FILT_MATCH  matchCode;
   bool  fail, invert;

   uchar * pNetFilter = xmalloc(sizeof(*pNetFilter) * count);

   for (uint netwop = 0; netwop < count; netwop++)
   {
      // XXX only the first context is considered
      fc_act = &fc->act;

      invert = ((fc_act->invertedFilters & FILTER_NETWOP) != FALSE);

      if (fc_act->enabledFilters & FILTER_NETWOP)
      {
         assert(count <= fc_act->netwopCount);
         fail = (fc_act->pNetwopFilterField[netwop] == FALSE);

         if (fail ^ invert)
         {
            // if the network is also excluded by pre-filter, it's excluded from global inversion
            if ( (fc->enabledPreFilters & FILTER_NETWOP_PRE) &&
                 (fc->pNetwopPreFilter1[netwop] == FALSE) )
               matchCode = FILT_MATCH_FAIL_PRE;
            else
               matchCode = FILT_MATCH_FAIL;
         }
         else
         {
            // do not include pre-filtered netwops with inverted network selection
            if ( invert && (fc->enabledPreFilters & FILTER_NETWOP_PRE) &&
                 (fc->pNetwopPreFilter1[netwop] == FALSE) )
               matchCode = FILT_MATCH_FAIL_PRE;
            else
               matchCode = FILT_MATCH_OK;
         }
      }
      else if (fc->enabledPreFilters & FILTER_NETWOP_PRE)
      {  // netwop pre-filter is only activated when netwop is unused
         // this way also netwops outside of the prefilter can be explicitly requested, e.g. by a NI menu
         assert(count <= fc->netwopCount);

         if (fc->pNetwopPreFilter1[netwop] == FALSE)
            matchCode = FILT_MATCH_FAIL_PRE;
         else
            matchCode = FILT_MATCH_OK;
      }
      else
         matchCode = FILT_MATCH_OK;

      pNetFilter[netwop] =  (matchCode == FILT_MATCH_OK) ||
                           ((matchCode == FILT_MATCH_FAIL) && (fc->act.enabledFilters & FILTER_INVERT));
   }
   return pNetFilter;
}

// ---------------------------------------------------------------------------
// Reset the theme filter state
// - the theme filter is divided into 8 classes (see ETS 303 707, chp. 11.12.4)
//   during the match of a PI into the theme filters, a logical AND is
//   performed across all classes; inside a class a logical OR is performed
// - i.e. a PI has to match each enabled class; a class is matched if the
//   PI has at least one theme attribute of that class
//
uchar EpgDbFilterInitThemes( FILTER_CONTEXT *fc, uint themeCount, uchar themeClassBitField )
{
   FILTER_CTX_ACT * fc_act = fc->pFocus;

   if (fc_act->themeCount < themeCount)
   {
      uint size = sizeof(*fc_act->themeFilterField) * themeCount;
      fc_act->themeFilterField = xrealloc(fc_act->themeFilterField, size);
      memset(fc_act->themeFilterField + fc_act->themeCount, 0,
             (themeCount - fc_act->themeCount) * sizeof(*fc_act->themeFilterField));
      fc_act->themeCount = themeCount;
   }

   if (themeClassBitField == 0xff)
   {  // clear all classes
      memset(fc_act->themeFilterField, 0, sizeof(*fc_act->themeFilterField) * fc_act->themeCount);
      fc_act->usedThemeClasses = 0;
   }
   else
   {  // clear the setting of selected classes only
      for (uint index=0; index < fc_act->themeCount; index++)
      {
         fc_act->themeFilterField[index] &= ~ themeClassBitField;
      }
      fc_act->usedThemeClasses &= ~ themeClassBitField;
   }

   return fc_act->usedThemeClasses;
}

// ---------------------------------------------------------------------------
// Assign a range of theme indices to a class
//
void EpgDbFilterSetThemes( FILTER_CONTEXT *fc, uint firstTheme, uint lastTheme, uchar themeClassBitField )
{
   assert(themeClassBitField != 0);
   assert(firstTheme <= lastTheme);

   if (lastTheme < fc->pFocus->themeCount)
   {
      for (uint index = firstTheme; index <= lastTheme; index++)
      {
         fc->pFocus->themeFilterField[index] |= themeClassBitField;
      }
      fc->pFocus->usedThemeClasses |= themeClassBitField;
   }
   else
      debug3("EpgDbFilter-SetThemes: invalid theme indices:%d-%d >= count:%d", firstTheme, lastTheme, fc->pFocus->themeCount);
}

// ---------------------------------------------------------------------------
// Query if the given theme is selected by the filter
//
bool EpgDbFilterIsThemeFiltered( FILTER_CONTEXT *fc, uint index )
{
   if (index < fc->pFocus->themeCount)
   {
      return (fc->pFocus->themeFilterField[index] & fc->pFocus->usedThemeClasses) != 0;
   }
   debug2("EpgDbFilter-IsThemeFiltered: invalid theme index:%d >= count:%d", index, fc->pFocus->themeCount);
   return FALSE;
}

// ---------------------------------------------------------------------------
// Set the value for the parental rating filter
// - values N stands for "ok above N years"
// - value 0x80 stands for "any rated programme"
// - value 0xFF stands for "not rated" and should not be used
//
void EpgDbFilterSetParentalRating( FILTER_CONTEXT *fc, uchar newParentalRating )
{
   ifdebug0(newParentalRating == PI_PARENTAL_UNDEFINED, "EpgDbFilter-SetParentalRating: WARNING: setting illegal parental rating");

   fc->pFocus->parentalRating = newParentalRating;
}

// ---------------------------------------------------------------------------
// Set the value for the editorial rating filter
// - value 0xFF stands for "not rated" and should not be used
//
void EpgDbFilterSetEditorialRating( FILTER_CONTEXT *fc, uchar newEditorialRating )
{
   ifdebug0(newEditorialRating == PI_EDITORIAL_UNDEFINED, "EpgDbFilter-SetEditorialRating: WARNING: setting illegal editorial rating");

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
// Reset network filter
// - number of networks in database has to be provided by caller
//
void EpgDbFilterInitNetwop( FILTER_CONTEXT *fc, uint netwopCount )
{
   uint size = sizeof(*fc->pFocus->pNetwopFilterField) * netwopCount;

   FILTER_CTX_ACT * fc_act = &fc->act;
   while (fc_act != NULL)
   {
      if (fc_act->netwopCount < netwopCount)
      {
         fc_act->pNetwopFilterField = xrealloc(fc_act->pNetwopFilterField, size);
         fc_act->netwopCount = netwopCount;
      }
      fc_act = fc_act->pNext;
   }

   memset(fc->pFocus->pNetwopFilterField, FALSE, size);
}

// ---------------------------------------------------------------------------
// Enable one network in the network filter array
//
void EpgDbFilterSetNetwop( FILTER_CONTEXT *fc, uint netwopNo )
{
   if ((fc->pFocus->pNetwopFilterField != NULL) && (netwopNo < fc->pFocus->netwopCount))
   {
      fc->pFocus->pNetwopFilterField[netwopNo] = TRUE;
   }
   else
      fatal1("EpgDbFilter-SetNetwop: illegal netwop idx %d", netwopNo);
}

// ---------------------------------------------------------------------------
// Allocate dynamic arrays
// - arrays that reflect network list have to be allocated dynamically
// - number of networks in database has to be provided by caller
//
static void EpgDbFilterAllocNetwops( FILTER_CONTEXT *fc, uint netwopCount )
{
   if (fc->netwopCount < netwopCount)
   {
      // fields are allocated all at once, so all shall be zero or none
      assert((((fc->pNetwopPreFilter1 == NULL) ^ (fc->pNetwopPreFilter2 == NULL)) ^
              ((fc->pNetwopAirTimeStart == NULL) ^ (fc->pNetwopAirTimeStop == NULL))) == 0);

      fc->pNetwopPreFilter1   = xrealloc(fc->pNetwopPreFilter1,
                                         sizeof(*fc->pNetwopPreFilter1) * netwopCount);
      fc->pNetwopPreFilter2   = xrealloc(fc->pNetwopPreFilter2,
                                         sizeof(*fc->pNetwopPreFilter2) * netwopCount);
      fc->pNetwopAirTimeStart = xrealloc(fc->pNetwopAirTimeStart,
                                         sizeof(*fc->pNetwopAirTimeStart) * netwopCount);
      fc->pNetwopAirTimeStop  = xrealloc(fc->pNetwopAirTimeStop,
                                         sizeof(*fc->pNetwopAirTimeStop) * netwopCount);

      fc->netwopCount = netwopCount;
   }
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
void EpgDbFilterInitNetwopPreFilter( FILTER_CONTEXT *fc, uint netwopCount )
{
   EpgDbFilterAllocNetwops(fc, netwopCount);
   memset(fc->pNetwopPreFilter1, TRUE, sizeof(*fc->pNetwopPreFilter1) * netwopCount);
}

// ---------------------------------------------------------------------------
// Disable one network in the network pre-filter array
//
void EpgDbFilterSetNetwopPreFilter( FILTER_CONTEXT *fc, uint netwopNo )
{
   if ((fc->pNetwopPreFilter1 != NULL) && (netwopNo < fc->netwopCount))
   {
      fc->pNetwopPreFilter1[netwopNo] = FALSE;
   }
   else
      fatal1("EpgDbFilter-SetNetwopPreFilter: illegal netwop idx %d", netwopNo);
}

// ---------------------------------------------------------------------------
// Init higher-level netwop pre-filter (pre-filter #2)
// - intended to be used by the GUI to limit network matches to a sub-set of networks
// - note: this filter is NOT included with the network match cache
//
void EpgDbFilterInitNetwopPreFilter2( FILTER_CONTEXT *fc, uint netwopCount )
{
   EpgDbFilterAllocNetwops(fc, netwopCount);
   memset(fc->pNetwopPreFilter2, FALSE, sizeof(*fc->pNetwopPreFilter2) * netwopCount);
}

// ---------------------------------------------------------------------------
// Disable one network in the network pre-filter #2 array
// - see comments at the init function above
//
void EpgDbFilterSetNetwopPreFilter2( FILTER_CONTEXT *fc, uint netwopNo )
{
   if ((fc->pNetwopPreFilter2 != NULL) && (netwopNo < fc->netwopCount))
   {
      fc->pNetwopPreFilter2[netwopNo] = TRUE;
   }
   else
      fatal1("EpgDbFilter-SetNetwopPreFilter2: illegal netwop idx %d", netwopNo);
}

// ---------------------------------------------------------------------------
// Reset air times pre-filter -> no restrictions for any networks
//
void EpgDbFilterInitAirTimesFilter( FILTER_CONTEXT *fc, uint netwopCount )
{
   EpgDbFilterAllocNetwops(fc, netwopCount);

   memset(fc->pNetwopAirTimeStart, 0, sizeof(*fc->pNetwopAirTimeStart) * netwopCount);
   memset(fc->pNetwopAirTimeStop, 0, sizeof(*fc->pNetwopAirTimeStop) * netwopCount);
}

// ---------------------------------------------------------------------------
// Set air times for one network in the air times filter array
// - the filter is disabled by setting start and stop times to 0
//
void EpgDbFilterSetAirTimesFilter( FILTER_CONTEXT *fc, uint netwopNo, uint startMoD, uint stopMoD )
{
   if (   (fc->pNetwopAirTimeStart != NULL) && (fc->pNetwopAirTimeStop != NULL)
       && (netwopNo < fc->netwopCount))
   {
      assert((startMoD < 24*60) && (stopMoD < 24*60));

      fc->pNetwopAirTimeStart[netwopNo] = startMoD;
      fc->pNetwopAirTimeStop[netwopNo]  = stopMoD;
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
// Set string for sub-string search in title and description text
//

void EpgDbFilterSetSubStr( FILTER_CONTEXT *fc, const char *pStr,
                           bool scopeTitle, bool scopeDesc, bool matchCase, bool matchFull )
{
   EPGDB_FILT_SUBSTR * pSubStrCtx;
   uint   size;
   char * p;
   uchar  c;

   assert(scopeTitle || scopeDesc);

   if (pStr != NULL)
   {
      size = sizeof(EPGDB_FILT_SUBSTR) + strlen(pStr) + 1 - 1;
      pSubStrCtx = (EPGDB_FILT_SUBSTR*) xmalloc(size);

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
            *(p++) = c; //TODO tolower(c);
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
static void EpgDbFilter_SubstrToLower( const char * src, char * dst, uint maxLen )
{
   register uchar c;
   register int  len;

   len = maxLen - 1;
   while ( ((c = *(src++)) != 0) && (len > 0) )
   {
      *(dst++) = c; //TODO tolower(c)
      len--;
   }
   *(dst++) = 0;
}

// ---------------------------------------------------------------------------
// Compare text with search string according to search parameters
// - parameter #1: ignore case
// - parameter #2: exact => search string must match start and end of text
//
static bool xstrcmp( const EPGDB_FILT_SUBSTR *ssc, const char * str,
                     char * pCache, uint cacheSize,  bool * pIsLower )
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
// Compare text with search string according to search parameters
// - note: either title, or description search, or both must be enabled
// - parameter #1: ignore case
// - parameter #2: exact => search string must match start and end of text
//
static bool EpgDbFilter_MatchSubstr( const EPGDB_FILT_SUBSTR *ssc,
                                     const char * pTitleStr, const char * pDescStr )
{
   char desc_buf[3*(256+2048+4)]; // XXX FIXME merged db description can be much longer
   char title[255+4];
   bool title_lower;
   bool short_lower;

   title_lower = short_lower = FALSE;

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
         if ((pDescStr != NULL) && xstrcmp(ssc, pDescStr, desc_buf, sizeof(desc_buf), &short_lower))
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
   if ((mask & FILTER_FEATURES) != 0)
   {
      fc->pFocus->featureFilterCount = 0;
   }

   fc->pFocus->enabledFilters &= ~ mask;
}

// ----------------------------------------------------------------------------
// Invert the result of given filter types when matching against PI
// - note: for global inversion of the combined filtering result there is a
//   separate filter type which is enabled via the regular filter type enable func
//
void EpgDbFilterInvert( FILTER_CONTEXT *fc, uint mask, uchar themeClass )
{
   assert((mask & (FILTER_PERM | FILTER_INVERT)) == 0);  // inverting these is not supported

   fc->pFocus->invertedFilters         = mask;
   fc->pFocus->invertedThemeClasses    = themeClass;
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
   uchar fclass;
   uint  index;
   bool  fail_buf;
   bool  fail, invert;
   bool  skipThemes = FALSE;
   bool  skipSubstr = FALSE;

   // variable to temporarily keep track of failed matches, because matches
   // cannot be aborted with "goto failed" until all possible "failed_pre"
   // matches are through
   fail_buf = FALSE;

   if (fc_act->enabledFilters & FILTER_NETWOP)
   {
      assert(pPi->netwop_no < fc_act->netwopCount);

      fail   = (fc_act->pNetwopFilterField[pPi->netwop_no] == FALSE);

      invert = ((fc_act->invertedFilters & FILTER_NETWOP) != FALSE);
      if (fail ^ invert)
      {
         // if the network is also excluded by pre-filter, it's excluded from global inversion
         if ( (fc->enabledPreFilters & FILTER_NETWOP_PRE) &&
              (fc->pNetwopPreFilter1[pPi->netwop_no] == FALSE) )
            goto failed_pre;
         else
            fail_buf = TRUE;
      }

      // do not include pre-filtered netwops with inverted network selection
      if ( invert && (fc->enabledPreFilters & FILTER_NETWOP_PRE) &&
           (fc->pNetwopPreFilter1[pPi->netwop_no] == FALSE) )
         goto failed_pre;
   }
   else if (fc->enabledPreFilters & FILTER_NETWOP_PRE)
   {  // netwop pre-filter is only activated when netwop is unused
      // this way also netwops outside of the prefilter can be explicitly requested, e.g. by a NI menu
      if (fc->pNetwopPreFilter1[pPi->netwop_no] == FALSE)
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
      time_t start_time = pPi->start_time;
      struct tm * ptm = localtime(&start_time);
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
      time_t start_time = pPi->start_time;
      struct tm * ptm = localtime(&start_time);
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
      time_t start_time = pPi->start_time;
      struct tm * ptm = localtime(&start_time);
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
      if (pPi->parental_rating != PI_PARENTAL_UNDEFINED)
      {
         if (fc_act->parentalRating == 0x80U)
            fail   = FALSE;
         else
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
      if ((pPi->editorial_rating != PI_EDITORIAL_UNDEFINED) &&
          (pPi->editorial_max_val != 0))
      {
         if (fc_act->editorialRating == 0x80U)
            fail = FALSE;
         else
            fail = (pPi->editorial_rating * 10 / pPi->editorial_max_val < fc_act->editorialRating);

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

   if ((fc_act->enabledFilters & FILTER_THEMES) && (skipThemes == FALSE))
   {
      if (fc_act->usedThemeClasses == 0)
      {  // no theme selected in filter: disable all
         goto failed;
      }

      for (fclass=1; fclass != 0; fclass <<= 1)
      {  // AND across all classes
         if (fc_act->usedThemeClasses & fclass)
         {
            fail = TRUE;
            for (index=0; index < pPi->no_themes; index++)
            {  // OR across all themes in a class
               if ((pPi->themes[index] < fc->pFocus->themeCount) &&
                   (fc_act->themeFilterField[pPi->themes[index]] & fclass))
               {
                  fail = FALSE;
                  break;
               }
            }

            invert = ((fc_act->invertedThemeClasses & fclass) != FALSE);
            if (fail ^ invert)
               goto failed;
         }
      }
   }

   if ((fc_act->enabledFilters & FILTER_SUBSTR) && (skipSubstr == FALSE))
   {
      fail = ( EpgDbFilter_MatchSubstr(fc_act->pSubStrCtx,
                                       PI_GET_TITLE(pPi),
                                       PI_HAS_DESC_TEXT(pPi) ? PI_GET_DESC_TEXT(pPi) : NULL
                                      ) == FALSE );

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
         time_t start_time = pPi->start_time;
         struct tm * pTm = localtime(&start_time);
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
      assert((pPi->netwop_no < fc->netwopCount) ||
             ((fc->enabledPreFilters & (FILTER_NETWOP_PRE | FILTER_NETWOP_PRE2 | FILTER_AIR_TIMES)) == 0));

      if (fc->enabledPreFilters & FILTER_NETWOP_PRE2)
      {
         if (fc->pNetwopPreFilter2[pPi->netwop_no] == FALSE)
            goto failed_pre;
      }

      if (fc->enabledPreFilters & FILTER_AIR_TIMES)
      {
         if ( (fc->pNetwopAirTimeStart[pPi->netwop_no] != 0) ||
              (fc->pNetwopAirTimeStop[pPi->netwop_no] != 0) )
         {
            sint  lto = EpgLtoGet(pPi->start_time);
            sint  airStartMoD = fc->pNetwopAirTimeStart[pPi->netwop_no];
            sint  airStopMoD  = fc->pNetwopAirTimeStop[pPi->netwop_no];
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
