/*
 *  Nextview GUI: Reminder interface to database
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
 *    This module is a "glue layer" between the reminder core functionality
 *    on Tcl level and the programmes database.  It implements services to
 *    (i) search for single reminders by use of a "custom" database filter,
 *    (ii) maintain the single PI reminder list, i.e. all modifications of
 *    this list must be made here (as a consequence of point (i), as a cache
 *    is required to speed up searches), (iii) search for pending reminder
 *    events.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: piremind.c,v 1.16 2004/09/26 16:12:07 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/ttxdecode.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgmain.h"
#include "epgui/shellcmd.h"
#include "epgui/pibox.h"
#include "epgui/pifilter.h"
#include "epgui/piremind.h"
#include "epgtcl/dlg_remind.h"


#define SUPRESS_REM_GRPTAG     0
#define SUPRESS_REM_GRPTAG_STR "0"
#define SUPRESS_REM_GROUP     31          // group 31 = suppress group
#define SUPRESS_REM_MASK      (1 << SUPRESS_REM_GROUP)
#define MATCH_ALL_REM_GROUPS  0x7fffffff  // groups 0..30 (indices into group order list)

#define RPI_CTRL_CHECK_THRESHOLD(CTRL,TIME)  \
                       ( ( (((CTRL) & EPGTCL_RPI_CTRL_MASK) != EPGTCL_RPI_CTRL_NEW) && \
                           (((CTRL) & EPGTCL_RPI_CTRL_MASK) != EPGTCL_RPI_CTRL_REPEAT) ) || \
                         ((TIME) > ((CTRL) & ~EPGTCL_RPI_CTRL_MASK)) )


#define dbc pUiDbContext         // internal shortcut


// ----------------------------------------------------------------------------
// Variables and macros to handle cache for "single PI" reminders
// - cache is a hash array (implemented in Tcl library)
// - keys are derived from PI: using network and start time as keys, since this
//   pair uniquely identifies any PI in the database
// - Coding of keys: 7 bit network index; 1 bit VPS yes/no; 24 bit VPS or
//   start time minus a base time (else 24 bit would not suffice)
// - values assigend to keys are 5 bit group index (0..31) and 27 bit reminder index
//
static bool          isRemPiCacheInitialized = FALSE;
static time_t        RemPiCacheBaseTime;
static Tcl_HashTable RemPiCache;

// macros to derive the hash key from PI network and start time/VPS
#define REMPI_CACHE_GET_VPS_KEY(PIL,NETWOP)     ((const char *)(((PIL) << 8) | 0x80 | (NETWOP)))
#define REMPI_CACHE_GET_TIME_KEY(START,NETWOP)  ((const char *)((((START) - RemPiCacheBaseTime) << 8) | \
                                                                (NETWOP)))

// ----------------------------------------------------------------------------
// Recursively duplicate shared objects in reminder list elements
// - Tcl list operators don't allow modifying shared objects, hence they need
//   to be duplicated before modification; duplication requires substitution
//   of the object in the parent list, hence duplication may have to be done
//   recursively up to the root object in case the parents are shared too.
//
static int PiReminder_UnshareList( Tcl_Obj ** ppRemListObj,
                                   Tcl_Obj *** ppRemArgv, int remIdx,
                                   Tcl_Obj *** ppElemArgv, int elemIdx )
{
   Tcl_Obj  * pTmpObj;
   int   remCount, elemCount;
   int   result = TCL_OK;

   if ( (ppElemArgv == NULL) ||
        Tcl_IsShared((*ppElemArgv)[elemIdx]) )
   {
      if ( (ppRemArgv == NULL) ||
           Tcl_IsShared((*ppRemArgv)[remIdx]) )
      {
         if (Tcl_IsShared(*ppRemListObj))
         {
            dprintf0("duplicate remlist\n");

            *ppRemListObj = Tcl_DuplicateObj(*ppRemListObj);
            pTmpObj = Tcl_SetVar2Ex(interp, "reminders", NULL, *ppRemListObj, TCL_GLOBAL_ONLY);
            if (pTmpObj == NULL)
            {
               debug0("PiReminder-UnshareList: failed to duplicate reminders list object");
               result = TCL_ERROR;
            }
            else
               *ppRemListObj = pTmpObj;
         }

         if ( (ppRemArgv != NULL) && (result == TCL_OK) )
         {
            dprintf1("duplicate reminder #%d\n", remIdx);

            pTmpObj = Tcl_DuplicateObj((*ppRemArgv)[remIdx]);
            if ( (Tcl_ListObjReplace(interp, *ppRemListObj, remIdx, 1, 1, &pTmpObj) != TCL_OK) ||
                 (Tcl_ListObjGetElements(interp, *ppRemListObj, &remCount, ppRemArgv) != TCL_OK) )
            {
               debug1("PiReminder-UnshareList: failed to duplicate reminders list elem #%d", remIdx);
               result = TCL_ERROR;
            }
         }
      }
      if ( (ppElemArgv != NULL) && (result == TCL_OK) )
      {
         dprintf2("duplicate elem #%d in reminder #%d\n", elemIdx, remIdx);

         pTmpObj = Tcl_DuplicateObj((*ppElemArgv)[elemIdx]);
         if ( (Tcl_ListObjReplace(interp, (*ppRemArgv)[remIdx], elemIdx, 1, 1, &pTmpObj) != TCL_OK) ||
              (Tcl_ListObjGetElements(interp, (*ppRemArgv)[remIdx], &elemCount, ppElemArgv) != TCL_OK) )
         {
            debug2("PiReminder-UnshareList: failed to duplicate reminder #%d, elem #%d", remIdx, elemIdx);
            result = TCL_ERROR;
         }
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Read control index value from the reminder with the given index
//
static int PiRemind_GetPiControlValue( sint remIdx )
{
   Tcl_Obj  * pRemListObj;
   Tcl_Obj  * pRemPiObj;
   Tcl_Obj ** pElemArgv;
   int        elemCount;
   int        ctrl;

   ctrl = EPGTCL_RPI_CTRL_DEFAULT;
   pRemListObj = Tcl_GetVar2Ex(interp, "reminders", NULL, TCL_GLOBAL_ONLY);
   if ( (pRemListObj != NULL) &&
        (Tcl_ListObjIndex(interp, pRemListObj, remIdx, &pRemPiObj) == TCL_OK) &&
        (pRemPiObj != NULL) )
   {
      if ( (Tcl_ListObjGetElements(interp, pRemPiObj, &elemCount, &pElemArgv) == TCL_OK) &&
           (elemCount == EPGTCL_RPI_IDX_COUNT) )
      {
         if (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_CTRL_IDX], &ctrl) != TCL_OK)
         {
            ctrl = EPGTCL_RPI_CTRL_DEFAULT;
         }
      }
   }
   return ctrl;
}

// ----------------------------------------------------------------------------
// Get first group tag, i.e. default group for additions
//
static Tcl_Obj * PiRemind_GetDefaultGroupTag( int * pGroupTag )
{
   Tcl_Obj  * pGrpTagList;
   Tcl_Obj  * pGrpTagObj;
   int  groupTag;

   pGrpTagList = Tcl_GetVar2Ex(interp, "remgroup_order", NULL, TCL_GLOBAL_ONLY);
   if (pGrpTagList != NULL)
   {
      if ( (Tcl_ListObjIndex(NULL, pGrpTagList, 0, &pGrpTagObj) == TCL_OK) &&
           (pGrpTagObj != NULL) )
      {
         if (Tcl_GetIntFromObj(NULL, pGrpTagObj, &groupTag) == TCL_OK)
         {
            if (pGroupTag != NULL)
               *pGroupTag = groupTag;

            return pGrpTagObj;
         }
         else
            debug2("PiRemind-GetDefaultGroupTag: remgroup_order tag #0 is not a number: '%s': %s", Tcl_GetString(pGrpTagObj), Tcl_GetStringResult(interp));
      }
      else
         debug2("PiRemind-GetDefaultGroupTag: filed to get remgroup_order tag #0: '%s': %s", Tcl_GetString(pGrpTagList), Tcl_GetStringResult(interp));
   }
   else
      debug0("PiRemind-GetDefaultGroupTag: remgroup_order variable not found");

   return NULL;
}

// ----------------------------------------------------------------------------
// Add a new "single PI" reminder to the cache
//
static void PiRemind_PiCacheAdd( sint remIdx, sint groupTag,
                                 uint netwop, time_t start_time, uint pil )
{
   Tcl_HashEntry * pEntry;
   Tcl_Obj  * pGrpTagList;
   Tcl_Obj ** pGrpTagArgv;
   int   groupCount;
   int   cmpTag, tagIdx;
   int   isNewCacheEntry;

   if (remIdx >= 0)
   {
      pGrpTagList = Tcl_GetVar2Ex(interp, "remgroup_order", NULL, TCL_GLOBAL_ONLY);
      if ( (pGrpTagList != NULL) &&
           (Tcl_ListObjGetElements(NULL, pGrpTagList, &groupCount, &pGrpTagArgv) == TCL_OK) )
      {
         if (groupTag != SUPRESS_REM_GRPTAG)
         {
            // search given group tag in group tag list to convert it to a group index
            for (tagIdx = 0; tagIdx < groupCount; tagIdx++)
            {
               if (Tcl_GetIntFromObj(NULL, pGrpTagArgv[tagIdx], &cmpTag) == TCL_OK)
               {
                  if (cmpTag == groupTag)
                     break;
               }
            }
         }
         else  // special group tag: suppres group
            tagIdx = SUPRESS_REM_GROUP;

         if ((tagIdx == SUPRESS_REM_GROUP) || ((tagIdx < groupCount) && (tagIdx <= 31)))
         {
            if (pil != INVALID_VPS_PIL)
            {
               pEntry = Tcl_CreateHashEntry(&RemPiCache, REMPI_CACHE_GET_VPS_KEY(pil, netwop), &isNewCacheEntry);
               Tcl_SetHashValue(pEntry, (remIdx << 5) | tagIdx);

               if (isNewCacheEntry == 0)
                  debug3("PiRemind-PiCacheAdd: duplicate key 0x%X: netwop %d, PIL 0x%X", (int)REMPI_CACHE_GET_VPS_KEY(pil, netwop), netwop, pil);
            }
            pEntry = Tcl_CreateHashEntry(&RemPiCache, REMPI_CACHE_GET_TIME_KEY(start_time, netwop), &isNewCacheEntry);
            Tcl_SetHashValue(pEntry, (remIdx << 5) | tagIdx);

            if (isNewCacheEntry == 0)
               debug3("PiRemind-PiCacheAdd: duplicate key 0x%X: netwop %d, start %d", (int)REMPI_CACHE_GET_TIME_KEY(start_time, netwop), netwop, (int)start_time);
         }
         else
            debug1("PiRemind-PiCacheAdd: invalid group tag %d", groupTag);
      }
      else
         debug0("PiRemind-PiCacheAdd: remgroup_order variable not found");
   }
   else
      debug1("PiRemind-PiCacheAdd: invalid reminder index %d", remIdx);
}

// ----------------------------------------------------------------------------
// Remove a "single PI" reminder from the cache
//
static void PiRemind_PiCacheRemove( Tcl_Obj * pRemListObj, int remIdx )
{
   Tcl_HashEntry * pEntry;
   Tcl_Obj  * pRemPiObj;
   Tcl_Obj ** pElemArgv;
   int        elemCount;
   int        netwop, pil, start_time;

   if ( (pRemListObj != NULL) &&
        (Tcl_ListObjIndex(interp, pRemListObj, remIdx, &pRemPiObj) == TCL_OK) &&
        (pRemPiObj != NULL) )
   {
      if ( (Tcl_ListObjGetElements(interp, pRemPiObj, &elemCount, &pElemArgv) == TCL_OK) &&
           (elemCount == EPGTCL_RPI_IDX_COUNT) )
      {
         if ( (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_NETWOP_IDX], &netwop) == TCL_OK) &&
              (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_PIL_IDX], &pil) == TCL_OK) &&
              (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_START_IDX], &start_time) == TCL_OK) )
         {
            if (pil != INVALID_VPS_PIL)
            {
               pEntry = Tcl_FindHashEntry(&RemPiCache, REMPI_CACHE_GET_VPS_KEY(pil, netwop));
               if (pEntry != NULL)
                  Tcl_DeleteHashEntry(pEntry);
            }

            pEntry = Tcl_FindHashEntry(&RemPiCache, REMPI_CACHE_GET_TIME_KEY(start_time, netwop));
            if (pEntry != NULL)
               Tcl_DeleteHashEntry(pEntry);
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Search a reminder group which matches the given PI
//
static int PiRemind_SearchRemGroupMatch( const EPGDB_CONTEXT * dbc, const PI_BLOCK * pPi )
{
   Tcl_Obj  * pGrpTagList;
   Tcl_Obj ** pGrpTagArgv;
   Tcl_Obj  * pGroupObj;
   Tcl_Obj  * CacheIdxObj;
   int        groupCount;
   int        tagIdx;
   int        cacheIdx;
   bool       groupMatch = FALSE;

   pGrpTagList = Tcl_GetVar2Ex(interp, "remgroup_order", NULL, TCL_GLOBAL_ONLY);
   if ( (pGrpTagList != NULL) &&
        (Tcl_ListObjGetElements(NULL, pGrpTagList, &groupCount, &pGrpTagArgv) == TCL_OK) )
   {
      for (tagIdx = 0; tagIdx < groupCount; tagIdx++)
      {
         pGroupObj = Tcl_GetVar2Ex(interp, "remgroups", Tcl_GetString(pGrpTagArgv[tagIdx]), TCL_GLOBAL_ONLY);
         if (pGroupObj != NULL)
         {
            if ( (Tcl_ListObjIndex(NULL, pGroupObj, EPGTCL_RGP_CTXCACHE_IDX, &CacheIdxObj) == TCL_OK) &&
                 (CacheIdxObj != NULL) &&
                 (Tcl_GetIntFromObj(NULL, CacheIdxObj, &cacheIdx) == TCL_OK) )
            {
               if (PiFilter_ContextCacheMatch(pPi, cacheIdx))
               {
                  groupMatch = TRUE;
                  break;
               }
            }
            else
               debug3("PiRemind-SearchRemGroupMatch: failed to get cache idx for group #%d (tag %s): '%s'", tagIdx, Tcl_GetString(pGrpTagArgv[tagIdx]), Tcl_GetString(pGroupObj));
         }
         else
            debug2("PiRemind-SearchRemGroupMatch: invalid group tag #%d: %s", tagIdx, Tcl_GetString(pGrpTagArgv[tagIdx]));
      }
   }
   else
      debug0("PiRemind-SearchRemGroupMatch: remgroup_order variable not found");

   if (groupMatch)
      return groupCount;
   else
      return -1;
}

// ----------------------------------------------------------------------------
// Searches the PI reminder list for an entry which matches the given PI
// - returns the reminder index or -1 if no match is found
//
static sint PiRemind_SearchRemPiMatch ( const EPGDB_CONTEXT * dbc, const PI_BLOCK * pPi, uint mask )
{
   Tcl_HashEntry * pEntry = NULL;
   sint  remIdx;
   uint  group;
   uint  value;

   if (pPi->pil != INVALID_VPS_PIL)
      pEntry = Tcl_FindHashEntry(&RemPiCache, REMPI_CACHE_GET_VPS_KEY(pPi->pil, pPi->netwop_no));
   if (pEntry == NULL)
      pEntry = Tcl_FindHashEntry(&RemPiCache, REMPI_CACHE_GET_TIME_KEY(pPi->start_time, pPi->netwop_no));

   if (pEntry != NULL)
   {
      value  = PVOID2UINT(Tcl_GetHashValue(pEntry));
      group  = value & 0x3f;

      if ((mask & (1 << group)) == 0)
         remIdx = -1;
      else
         remIdx = (value >> 5);
   }
   else
      remIdx = -1;

   return remIdx;
}

// ----------------------------------------------------------------------------
// Check if a given PI is matched by am entry in the "single PI" reminder list
// - called by database filter function
//
static bool PiRemind_MatchPiReminder( const EPGDB_CONTEXT * dbc, const PI_BLOCK * pPi, void * pArg )
{
   return ( PiRemind_SearchRemPiMatch(dbc, pPi, PVOID2UINT(pArg)) != -1 );
}

// ----------------------------------------------------------------------------
// Enable reminder filter
//
static int SelectReminderFilter( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_SelectReminderFilter <mask>";
   int mask;
   int result;

   if ( (objc != 2) ||
        (Tcl_GetIntFromObj(interp, objv[1], &mask) != TCL_OK) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (mask != 0)
      {
         EpgDbFilterSetCustom(pPiFilterContext, PiRemind_MatchPiReminder, NULL, INT2PVOID(mask));
         EpgDbFilterEnable(pPiFilterContext, FILTER_CUSTOM);
      }
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_CUSTOM);

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Search first match by reminder group after applying start time offsets
// - used to set a timer which expires when the next event is due, to pop up a message
//   returns the time when the next event occurs, or 0 if none is found (in the given interval)
// - must be called separately for each reminder group with the respective offsets
//
static int PiRemind_MatchFirstReminder( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiRemind_MatchFirstReminder "
                                          "<cache_idx> <msg-min-time> <cmd-min-time> <max-time> "
                                          "<sorted-msg-offsets> <sorted-cmd-offsets>";
   const FILTER_CONTEXT * pFc;
   const PI_BLOCK   * pPiBlock;
   Tcl_Obj         ** MsgToffObjv;
   Tcl_Obj         ** CmdToffObjv;
   int    cache_idx;
   int    msgMinStart, cmdMinStart, maxStart;
   int    cmdToffCount, msgToffCount;
   int    toffIdx, toffVal;
   int    delta, minDelta;
   time_t minAbsTime, eventAbsTime;
   int    remCtrl, remIdx;
   bool   done;
   int    result;

   if ( (objc != 1+6) ||
        (Tcl_GetIntFromObj(interp, objv[1], &cache_idx) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[2], &msgMinStart) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[3], &cmdMinStart) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[4], &maxStart) != TCL_OK) ||
        (Tcl_ListObjGetElements(interp, objv[5], &msgToffCount, &MsgToffObjv) != TCL_OK) ||
        (Tcl_ListObjGetElements(interp, objv[6], &cmdToffCount, &CmdToffObjv) != TCL_OK) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      minDelta = -1;
      minAbsTime = 0;

      // retrieve db filter for the given group (single-PI OR shortcuts)
      pFc = PiFilter_ContextCacheGet(cache_idx);
      if (pFc != NULL)
      {
         // add expire time filter to skip PI before the match interval:
         // performance optimization, since possibly complex shortcut filter matches are omitted
         // note: use cast to remove const (hack)
         EpgDbFilterSetExpireTime((FILTER_CONTEXT *) pFc, msgMinStart);
         EpgDbPreFilterEnable((FILTER_CONTEXT *) pFc, FILTER_EXPIRE_TIME);

         // loop across PI in database
         EpgDbLockDatabase(dbc, TRUE);
         pPiBlock = EpgDbSearchFirstPi(dbc, pFc);
         while (pPiBlock != NULL)
         {
            remIdx = PiRemind_SearchRemPiMatch(dbc, pPiBlock, 0x7fffffff);
            if (remIdx != -1)
               remCtrl = PiRemind_GetPiControlValue(remIdx);
            else
               remCtrl = EPGTCL_RPI_CTRL_DEFAULT;

            if ((remCtrl & EPGTCL_RPI_CTRL_MASK) == EPGTCL_RPI_CTRL_SUPPRESS)
               goto skip_pi;

            done = TRUE;
            for (toffIdx = 0; toffIdx < msgToffCount; toffIdx++)
            {
               if (Tcl_GetIntFromObj(interp, MsgToffObjv[toffIdx], &toffVal) == TCL_OK)
               {
                  // negate because positive offsets mean advance warning
                  eventAbsTime = pPiBlock->start_time - (toffVal * 60);
                  delta = eventAbsTime - msgMinStart;
                  if ( (delta >= 0) &&
                       RPI_CTRL_CHECK_THRESHOLD(remCtrl, eventAbsTime) )
                  {
                     if ( (eventAbsTime < maxStart) &&
                          ((delta < minDelta) || (minDelta < 0)) )
                     {
                        minDelta   = delta;
                        minAbsTime = eventAbsTime;
                     }
                  }
                  else
                     done = FALSE;
               }
            }
            for (toffIdx = 0; toffIdx < cmdToffCount; toffIdx++)
            {
               if (Tcl_GetIntFromObj(interp, CmdToffObjv[toffIdx], &toffVal) == TCL_OK)
               {
                  // negate because positive offsets mean advance warning
                  eventAbsTime = pPiBlock->start_time - (toffVal * 60);
                  delta = eventAbsTime - cmdMinStart;
                  if ( (delta >= 0) &&
                       RPI_CTRL_CHECK_THRESHOLD(remCtrl, eventAbsTime) )
                  {
                     if ( (eventAbsTime < maxStart) &&
                          ((delta < minDelta) || (minDelta < 0)) )
                     {
                        minDelta   = delta;
                        minAbsTime = eventAbsTime;
                     }
                  }
                  else
                     done = FALSE;
               }
            }
            if (done)
               break;
skip_pi:
            pPiBlock = EpgDbSearchNextPi(dbc, pFc, pPiBlock);
         }
         EpgDbLockDatabase(dbc, FALSE);
         EpgDbPreFilterDisable((FILTER_CONTEXT *) pFc, FILTER_EXPIRE_TIME);
      }
      Tcl_SetObjResult(interp, Tcl_NewIntObj(minAbsTime));
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Report all events which occurred in the given timer interval
// - called when the reminder timer (calculated above) expires, to collect pending events
//   for command events the matching PI are used to substitute variables in the script
// - returns a Tcl list with descriptions of each pending event
// - separate intervals are used for messages and commands (see handling on Tcl level)
// - must be called separately for each reminder group with the respective offsets
//
static int PiRemind_GetReminderEvents( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiRemind_GetReminderEvents <cache_idx> "
                                          "<cmd-min-time> <msg-min-time> <msg-base-time> <max-time> "
                                          "<sorted-msg-offsets> <sorted-cmd-offsets> <cmd-vector>";
   const FILTER_CONTEXT * pFc;
   const PI_BLOCK   * pPiBlock;
   Tcl_Obj         ** MsgToffObjv;
   Tcl_Obj         ** CmdToffObjv;
   Tcl_Obj          * pResultList;
   Tcl_Obj          * pTmpList;
   time_t eventAbsTime;
   int    msgMinStart, msgMinStop, cmdMinStart, maxStart;
   int    cmdToffCount, msgToffCount;
   int    toffIdx, toffVal;
   int    cache_idx;
   int    remCtrl, remIdx;
   bool   done;
   int    result;

   if ( (objc != 1+8) ||
        (Tcl_GetIntFromObj(interp, objv[1], &cache_idx) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[2], &cmdMinStart) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[3], &msgMinStart) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[4], &msgMinStop) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[5], &maxStart) != TCL_OK) ||
        (Tcl_ListObjGetElements(interp, objv[6], &msgToffCount, &MsgToffObjv) != TCL_OK) ||
        (Tcl_ListObjGetElements(interp, objv[7], &cmdToffCount, &CmdToffObjv) != TCL_OK) ||
        (Tcl_GetStringFromObj(objv[8], NULL) == NULL) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);

      // retrieve db filter for the given group (single-PI OR shortcuts)
      pFc = PiFilter_ContextCacheGet(cache_idx);
      if (pFc != NULL)
      {
         // add expire time filter to skip PI before the match interval:
         // performance optimization, since possibly complex shortcut filter matches are omitted
         // note: use cast to remove const (hack)
         if (msgMinStop <= maxStart)
            EpgDbFilterSetExpireTime((FILTER_CONTEXT *) pFc, msgMinStop);
         else
            EpgDbFilterSetExpireTime((FILTER_CONTEXT *) pFc, maxStart);
         EpgDbPreFilterEnable((FILTER_CONTEXT *) pFc, FILTER_EXPIRE_TIME);

         // loop across PI in database; stop when a matching PI is whose starting time
         // minus the largest advance offset is in the future: since PI are sorted by
         // start time all following PI are in the future too
         EpgDbLockDatabase(dbc, TRUE);
         pPiBlock = EpgDbSearchFirstPi(dbc, pFc);
         while (pPiBlock != NULL)
         {
            remIdx = PiRemind_SearchRemPiMatch(dbc, pPiBlock, 0x7fffffff);
            if (remIdx != -1)
               remCtrl = PiRemind_GetPiControlValue(remIdx);
            else
               remCtrl = EPGTCL_RPI_CTRL_DEFAULT;

            if ((remCtrl & EPGTCL_RPI_CTRL_MASK) == EPGTCL_RPI_CTRL_SUPPRESS)
               goto skip_pi;

            done = TRUE;
            for (toffIdx = 0; toffIdx < msgToffCount; toffIdx++)
            {
               if (Tcl_GetIntFromObj(interp, MsgToffObjv[toffIdx], &toffVal) == TCL_OK)
               {
                  // negate because positive offsets mean advance warning
                  eventAbsTime = pPiBlock->start_time - (toffVal * 60);

                  // check if event falls inside the requested interval (or lies past the threshold)
                  if ( (eventAbsTime >= msgMinStart) && (eventAbsTime < maxStart) )
                  {
                     done = FALSE;

                     // skip already expired events when creating a new reminder
                     // skip events from expired programmes (esp. at program start; unless already displayed)
                     if ( RPI_CTRL_CHECK_THRESHOLD(remCtrl, eventAbsTime) &&
                          (pPiBlock->stop_time > msgMinStop) )
                     {
                        pTmpList = Tcl_NewListObj(0, NULL);
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewStringObj("msg", -1));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewIntObj((toffIdx == 0) ? 1 : 0));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewIntObj(pPiBlock->netwop_no));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewIntObj(pPiBlock->start_time));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewIntObj(pPiBlock->stop_time));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewStringObj(PI_GET_TITLE(pPiBlock), -1));
                        Tcl_ListObjAppendElement(interp, pResultList, pTmpList);
                        // add only a message for the most distant event (offsets are sorted)
                        break;
                     }
                  }
                  else if (eventAbsTime < maxStart)
                  {
                     done = FALSE;
                  }
               }
               else
                  debugTclErr(interp, "PiRemind-GetReminderEvents: msg offsets");
            }
            for (toffIdx = 0; toffIdx < cmdToffCount; toffIdx++)
            {
               if (Tcl_GetIntFromObj(interp, CmdToffObjv[toffIdx], &toffVal) == TCL_OK)
               {
                  // negate because positive offsets mean advance warning
                  eventAbsTime = pPiBlock->start_time - (toffVal * 60);

                  // check if event falls is after the minimum start time (or past the threshold)
                  if ( (eventAbsTime >= cmdMinStart) && (eventAbsTime < maxStart) )
                  {
                     if ( RPI_CTRL_CHECK_THRESHOLD(remCtrl, eventAbsTime) &&
                          (pPiBlock->stop_time > maxStart) )
                     {
                        pTmpList = Tcl_NewListObj(0, NULL);
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewStringObj("cmd", -1));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewIntObj(toffVal));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewIntObj(pPiBlock->netwop_no));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewIntObj(pPiBlock->start_time));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewIntObj(pPiBlock->stop_time));
                        Tcl_ListObjAppendElement(interp, pTmpList, Tcl_NewStringObj(PI_GET_TITLE(pPiBlock), -1));
                        // one additional parameter is returned for script events (compared to msg):
                        // parsed & substituted (e.g. ${title} with PI's title) command line
                        Tcl_ListObjAppendElement(interp, pTmpList,
                                                 PiOutput_ParseScript(interp, objv[8], pPiBlock));
                        Tcl_ListObjAppendElement(interp, pResultList, pTmpList);
                     }
                     done = FALSE;
                  }
                  else if (eventAbsTime < maxStart)
                  {
                     done = FALSE;
                  }
               }
            }
            if (done)
               break;
skip_pi:
            pPiBlock = EpgDbSearchNextPi(dbc, pFc, pPiBlock);
         }
         EpgDbLockDatabase(dbc, FALSE);
         EpgDbPreFilterDisable((FILTER_CONTEXT *) pFc, FILTER_EXPIRE_TIME);
      }
      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Append a new "single PI" reminder to the list
// - returns the index of the new reminder in the list or < 0 for error
//
static int PiRemind_AddReminder( const PI_BLOCK * pPiBlock, int groupTag, int control )
{
   const AI_BLOCK * pAiBlock;
   Tcl_Obj  * pRemListObj;
   Tcl_Obj  * pNewRem;
   uchar strbuf[10];
   int   newIdx;

   newIdx = -1;
   pRemListObj = Tcl_GetVar2Ex(interp, "reminders", NULL, TCL_GLOBAL_ONLY);
   if (pRemListObj != NULL)
   {
      if (PiReminder_UnshareList(&pRemListObj, NULL, 0, NULL, 0) == TCL_OK)
      {
         pAiBlock = EpgDbGetAi(dbc);
         if (pAiBlock != NULL)
         {
            sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);

            // build a new reminder element
            pNewRem = Tcl_NewListObj(0, NULL);
            Tcl_ListObjAppendElement(interp, pNewRem, Tcl_NewIntObj(groupTag));
            Tcl_ListObjAppendElement(interp, pNewRem, Tcl_NewIntObj(control));
            Tcl_ListObjAppendElement(interp, pNewRem, Tcl_NewStringObj(strbuf, -1));
            Tcl_ListObjAppendElement(interp, pNewRem, Tcl_NewIntObj(pPiBlock->netwop_no));
            Tcl_ListObjAppendElement(interp, pNewRem, Tcl_NewIntObj(pPiBlock->pil));
            Tcl_ListObjAppendElement(interp, pNewRem, Tcl_NewIntObj(pPiBlock->start_time));
            Tcl_ListObjAppendElement(interp, pNewRem, Tcl_NewIntObj(pPiBlock->stop_time));
            Tcl_ListObjAppendElement(interp, pNewRem, Tcl_NewStringObj(PI_GET_TITLE(pPiBlock), -1));

            // append the new element to the list
            if (Tcl_ListObjAppendElement(interp, pRemListObj, pNewRem) == TCL_OK)
            {
               if (Tcl_ListObjLength(interp, pRemListObj, &newIdx) == TCL_OK)
               {
                  newIdx -= 1;

                  PiRemind_PiCacheAdd(newIdx, groupTag, pPiBlock->netwop_no,
                                      pPiBlock->start_time, pPiBlock->pil);
               }
               else
                  debugTclErr(interp, "PiRemind-AddReminder: failed to query new list length");
            }
            else
               debugTclErr(interp, "PiRemind-AddReminder: failed to append to reminder list");
         }
         else
            debug0("PiRemind-AddReminder: no AI block in database");
      }
   }
   else
      debug0("PiRemind-AddReminder: reminder list not found");

   return newIdx;
}

// ----------------------------------------------------------------------------
// Add "single-PI" reminder for PI currently selected in PI listbox
// - called from pibox context menu and reminder main menu
// - if group tag parameter is omitted the default group is used
//
static int PiRemind_AddPi( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiRemind_AddPi [<group>]";
   const PI_BLOCK * pPiBlock;
   Tcl_Obj        * pTagObj;
   int   newIdx;
   int   groupTag;
   int   control;
   int   result;

   if ( ((objc != 1+0) && (objc != 1+1)) ||
        ((objc == 1+1) && (Tcl_GetIntFromObj(interp, objv[1], &groupTag) != TCL_OK)) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (objc == 1)
         pTagObj = PiRemind_GetDefaultGroupTag(&groupTag);
      else
         pTagObj = objv[1];

      if (pTagObj != NULL)
      {
         // check if a group with the given tag exists
         if ( (groupTag == SUPRESS_REM_GRPTAG) ||
              (Tcl_GetVar2Ex(interp, "remgroups", Tcl_GetString(pTagObj), TCL_GLOBAL_ONLY) != NULL) )
         {
            newIdx = -1;
            EpgDbLockDatabase(dbc, TRUE);
            pPiBlock = PiBox_GetSelectedPi();
            if (pPiBlock != NULL)
            {
               if (PiRemind_SearchRemPiMatch(dbc, pPiBlock, MATCH_ALL_REM_GROUPS) == -1)
               {
                  assert((EpgGetUiMinuteTime() & EPGTCL_RPI_CTRL_MASK) == 0);  // %60==0 ==> %4==0

                  control = EPGTCL_RPI_CTRL_NEW | EpgGetUiMinuteTime();

                  newIdx = PiRemind_AddReminder(pPiBlock, groupTag, control);
               }
               else
                  debug0("PiRemind-AddPi: selected PI already has a reminder");
            }
            else
               debug0("PiRemind-AddPi: no PI selected in listbox");

            EpgDbLockDatabase(dbc, FALSE);

            if (newIdx >= 0)
            {  // reminder list was changed -> update PI listbox and reminder list (if open)
               sprintf(comm, "Reminder_ExternalChange %d", newIdx);
               eval_check(interp, comm);
            }
         }
         else
            debug2("PiRemind-AddPi: invalid group tag %d ('%s')", groupTag, ((objc == 1+1) ? Tcl_GetString(objv[1]) : ""));
      }

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Remove "single-PI" reminder for PI currently selected in PI listbox
// - if no reminder index is given, it's derived from the currently selected PI
//   this is used both for the context menu and reminder menu (main window menubar);
//   mode with given remidx is used by "edit reminders" dialog
// - used both to remove regular and "supress" reminders
// - requires to update the cache for PI filters
//
static int PiRemind_RemovePi( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiRemind_RemovePi [rem-idx]";
   const PI_BLOCK * pPiBlock;
   Tcl_Obj  * pRemListObj;
   sint  remIdx;
   bool  changed;
   int   result;

   if ( (objc < 1) || (objc > 2) ||
        ((objc == 2) && (Tcl_GetIntFromObj(interp, objv[1], &remIdx) != TCL_OK)) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      changed = FALSE;
      pRemListObj = Tcl_GetVar2Ex(interp, "reminders", NULL, TCL_GLOBAL_ONLY);
      if (pRemListObj != NULL)
      {
         if (objc == 1)
         {
            EpgDbLockDatabase(dbc, TRUE);
            pPiBlock = PiBox_GetSelectedPi();
            if (pPiBlock != NULL)
            {
               remIdx = PiRemind_SearchRemPiMatch(dbc, pPiBlock, MATCH_ALL_REM_GROUPS | SUPRESS_REM_MASK);
               if (remIdx == -1)
                  debug0("PiRemind-RemovePi: no reminder found for the selected PI");
            }
            else
               debug0("PiRemind-RemovePi: no PI selected in listbox");

            EpgDbLockDatabase(dbc, FALSE);
         }

         if (remIdx >= 0)
         {
            PiRemind_PiCacheRemove(pRemListObj, remIdx);

            if (PiReminder_UnshareList(&pRemListObj, NULL, 0, NULL, 0) == TCL_OK)
            {
               if (Tcl_ListObjReplace(interp, pRemListObj, remIdx, 1, 0, NULL) != TCL_OK)
                  debugTclErr(interp, "PiRemind-RemovePi: failed to remove element from reminder list");

               changed = TRUE;
            }
         }
      }

      if ((objc == 1) && changed)
      {  // reminder list was changed -> update PI listbox and reminder list (if open)
         sprintf(comm, "Reminder_ExternalChange -1");
         eval_check(interp, comm);
      }
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Callback for reminder list: change "single PI" reminder's group index
// - requires to update the cache for PI filters
// - replaces the following Tcl code (not counting cache management):
//
//       set elem [lindex $reminders $rem_idx]
//       set elem [lreplace $elem $::rpi_group_idx $::rpi_group_idx $remlist_cfpigrp]
//       set reminders [lreplace $reminders $rem_idx $rem_idx $elem]
//
static int PiRemind_PiSetGroup( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiRemind_PiSetGroup <group> [<rem-idx>]";
   const PI_BLOCK * pPiBlock;
   Tcl_Obj  * pRemListObj;
   Tcl_Obj ** pRemArgv;
   Tcl_Obj ** pElemArgv;
   int   elemCount;
   int   remIdx, remCount;
   int   group;
   int   control;
   int   netwop, pil, start_time;
   bool  changed;
   int   result;

   if ( (objc < 1+1) || (objc > 1+2) ||
        (Tcl_GetIntFromObj(interp, objv[1], &group) != TCL_OK) ||
        ((objc == 1+2) && (Tcl_GetIntFromObj(interp, objv[2], &remIdx) != TCL_OK)) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      changed = FALSE;
      pRemListObj = Tcl_GetVar2Ex(interp, "reminders", NULL, TCL_GLOBAL_ONLY);
      if ( (pRemListObj != NULL) &&
           (Tcl_ListObjGetElements(interp, pRemListObj, &remCount, &pRemArgv) == TCL_OK) )
      {
         if (objc == 1+1)
         {  // no reminder index given as parameter -> get currently selected PI
            EpgDbLockDatabase(dbc, TRUE);
            remIdx   = -1;
            pPiBlock = PiBox_GetSelectedPi();
            if (pPiBlock != NULL)
            {  // check which "single-PI" reminder matches the selected PI
               remIdx = PiRemind_SearchRemPiMatch(dbc, pPiBlock, MATCH_ALL_REM_GROUPS | SUPRESS_REM_MASK);
               if (remIdx == -1)
                  debug0("PiRemind-RemPi_SetGroup: no reminder found for the selected PI");
            }
            else
               debug0("PiRemind-RemPi_SetGroup: no PI selected in listbox");

            EpgDbLockDatabase(dbc, FALSE);
         }

         if ( (remIdx >= 0) && (remIdx < remCount) &&
              (Tcl_ListObjGetElements(interp, pRemArgv[remIdx], &elemCount, &pElemArgv) == TCL_OK) &&
              (elemCount == EPGTCL_RPI_IDX_COUNT) &&
              (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_NETWOP_IDX], &netwop) == TCL_OK) &&
              (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_PIL_IDX], &pil) == TCL_OK) &&
              (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_START_IDX], &start_time) == TCL_OK) )
         {
            if ( (PiReminder_UnshareList(&pRemListObj, &pRemArgv, remIdx, &pElemArgv, EPGTCL_RPI_GRPTAG_IDX) == TCL_OK) &&
                 (PiReminder_UnshareList(&pRemListObj, &pRemArgv, remIdx, &pElemArgv, EPGTCL_RPI_CTRL_IDX) == TCL_OK) )
            {
               PiRemind_PiCacheRemove(pRemListObj, remIdx);

               control = EPGTCL_RPI_CTRL_NEW | EpgGetUiMinuteTime();

               Tcl_SetIntObj(pElemArgv[EPGTCL_RPI_GRPTAG_IDX], group);
               Tcl_SetIntObj(pElemArgv[EPGTCL_RPI_CTRL_IDX], control);
               Tcl_InvalidateStringRep(pRemArgv[remIdx]);

               PiRemind_PiCacheAdd(remIdx, group, netwop, start_time, pil);
               changed = TRUE;
            }
         }
      }

      if ((objc == 1+1) && changed)
      {  // reminder list was changed -> update PI listbox and reminder list (if open)
         sprintf(comm, "Reminder_ExternalChange -1");
         eval_check(interp, comm);
      }
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Callback for reminder popup
//
static int PiRemind_SetControl( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiRemind_SetControl <control> <toffset> "
                                                            "<group> <netwop> <start-time>";
   const PI_BLOCK * pPiBlock;
   Tcl_Obj  * pRemListObj;
   Tcl_Obj ** pRemArgv;
   Tcl_Obj ** pElemArgv;
   int   elemCount;
   int   remIdx, remCount;
   int   control, toffset, group, start_time, netwop;
   bool  changed;
   int   result;

   if ( (objc != 1+5) ||
        (Tcl_GetIntFromObj(interp, objv[1], &control) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[2], &toffset) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[3], &group) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[4], &netwop) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[5], &start_time) != TCL_OK) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (control < EPGTCL_RPI_CTRL_COUNT)
      {
         if (control == EPGTCL_RPI_CTRL_REPEAT)
         {
            toffset  = (toffset * 60) + 30 + time(NULL);
            toffset -= (toffset % 60);
            control += toffset;
         }
         changed = FALSE;

         EpgDbLockDatabase(dbc, TRUE);
         pPiBlock = EpgDbSearchPi(dbc, start_time, netwop);
         if (pPiBlock != NULL)
         {
            remIdx = PiRemind_SearchRemPiMatch(dbc, pPiBlock, 0x7fffffff);
            if (remIdx >= 0)
            {  // found "single PI" reminder for this PI
               pRemListObj = Tcl_GetVar2Ex(interp, "reminders", NULL, TCL_GLOBAL_ONLY);
               if ( (pRemListObj != NULL) &&
                    (Tcl_ListObjGetElements(interp, pRemListObj, &remCount, &pRemArgv) == TCL_OK) )
               {
                  if (remIdx < remCount)
                  {
                     if ( (Tcl_ListObjGetElements(interp, pRemArgv[remIdx], &elemCount, &pElemArgv) == TCL_OK) &&
                          (elemCount == EPGTCL_RPI_IDX_COUNT) )
                     {
                        if (PiReminder_UnshareList(&pRemListObj, &pRemArgv, remIdx, &pElemArgv, EPGTCL_RPI_CTRL_IDX) == TCL_OK)
                        {
                           Tcl_SetIntObj(pElemArgv[EPGTCL_RPI_CTRL_IDX], control);
                           Tcl_InvalidateStringRep(pRemArgv[remIdx]);
                           changed = TRUE;
                        }
                     }
                     else
                        debug2("PiRemind-SetControl: corrupt reminder list element at #%d: '%s'", remIdx, Tcl_GetString(pRemArgv[remIdx]));
                  }
                  else
                     debug2("PiRemind-SetControl: invalid reminder index %d (>= %d) returned by match", remIdx, remCount);
               }
            }
            else
            {  // no match by single PI (shortcut match) -> create new reminder to store control
               // exception: "default" control (used when message popup is closed with "OK")
               if (control != EPGTCL_RPI_CTRL_DEFAULT)
               {
                  PiRemind_AddReminder(pPiBlock, group, control);
                  changed = TRUE;
               }
            }
         }
         else
            debug2("PiRemind-SetControl: PI net=%d start=%d not found", netwop, start_time);

         EpgDbLockDatabase(dbc, FALSE);

         if (changed)
         {  // reminder list was changed -> update PI listbox and reminder list (if open)
            sprintf(comm, "Reminder_ExternalChange -1");
            eval_check(interp, comm);
         }
      }
      else
         debug1("PiRemind-SetControl: invalid control value %d", control);

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Add reminder control commands to the PI listbox context menu
// - note: two Tcl commands implemented by the same function: distinguished by ttp
// - returns the number of added menu entries
//
static int PiRemind_ContextMenu( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiRemind_ContextMenu{Short|Extended} <menu>";
   const char * pMenu;
   const PI_BLOCK * pPiBlock;
   uint  entryCount = 0;
   bool  isExtended = PVOID2INT(ttp);
   sint  remIdx;
   int   result;

   if (objc != 2)
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = PiBox_GetSelectedPi();
      if (pPiBlock != NULL)
      {
         pMenu = Tcl_GetString(objv[1]);

         remIdx = PiRemind_SearchRemPiMatch(dbc, pPiBlock, MATCH_ALL_REM_GROUPS);
         if (remIdx != -1)
         {  // there's already a "single-PI" reminder for this PI -> offer to remove it
            sprintf(comm, "%s add command -label {Remove reminder} -command {C_PiRemind_RemovePi}\n", pMenu);
            eval_check(interp, comm);
            entryCount += 1;
            if (isExtended)
            {
               sprintf(comm, "%s add cascade -label {Change reminder group} -menu %s.reminder_group\n"
                             "if {[string length [info commands %s.reminder_group]] == 0} {\n"
                             "   menu %s.reminder_group -tearoff 0 -postcommand {PostDynamicMenu %s.reminder_group Reminder_PostGroupMenu %d}\n"
                             "}\n",
                             pMenu, pMenu, pMenu, pMenu, pMenu, remIdx);
               eval_check(interp, comm);
               entryCount += 1;
            }
         }
         else if (PiRemind_SearchRemPiMatch(dbc, pPiBlock, SUPRESS_REM_MASK) != -1)
         {  // there's a "suppress single PI" reminder for this PI -> offer to remove it
            sprintf(comm, "%s add command -label {Un-suppress reminder} -command {C_PiRemind_RemovePi}\n", pMenu);
            eval_check(interp, comm);
            entryCount += 1;
         }
         else
         {  // no match on any single reminder: check for groups
            if (PiRemind_SearchRemGroupMatch(dbc, pPiBlock) != -1)
            {  // group match -> offer to suppress it
               sprintf(comm, "%s add command -label {Suppress reminder} -command "
                                             "{C_PiRemind_AddPi " SUPRESS_REM_GRPTAG_STR "}\n", pMenu);
               eval_check(interp, comm);
               entryCount += 1;
            }
            // offer single reminder (may be set in addition to group match)
            sprintf(comm, "%s add command -label {Add reminder} -command {C_PiRemind_AddPi}\n", pMenu);
            eval_check(interp, comm);
            entryCount += 1;
            if (isExtended)
            {
               sprintf(comm, "%s add cascade -label {Add reminder into group} -menu %s.reminder_group\n"
                             "if {[string length [info commands %s.reminder_group]] == 0} {\n"
                             "  menu %s.reminder_group -tearoff 0 -postcommand {PostDynamicMenu %s.reminder_group Reminder_PostGroupMenu -1}\n"
                             "}\n",
                             pMenu, pMenu, pMenu, pMenu, pMenu);
               eval_check(interp, comm);
               entryCount += 1;
            }
         }
      }
      EpgDbLockDatabase(dbc, FALSE);

      Tcl_SetObjResult(interp, Tcl_NewIntObj(entryCount));
      result = TCL_OK;
   }
   return result;
}

// ---------------------------------------------------------------------------
// En-/Disable reminder menu entries
//
static int PiRemind_SetReminderMenuStates( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_SetReminderMenuStates";
   const PI_BLOCK * pPiBlock;
   const uchar * pConf1;
   const uchar * pConf2;
   const uchar * pConf3;
   sint  remIdx;
   int   result;

   if (objc != 1)
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = PiBox_GetSelectedPi();
      if (pPiBlock != NULL)
      {
         remIdx = PiRemind_SearchRemPiMatch(dbc, pPiBlock, MATCH_ALL_REM_GROUPS);
         if (remIdx != -1)
         {  // there's already a "single-PI" reminder for this PI -> offer to remove it
            pConf1 = "-label {Add reminder for selected title} -state disabled";
            pConf2 = "-label {Change reminder group for selected} -state normal";
            pConf3 = "-label {Remove reminder for selected title} -state normal -command C_PiRemind_RemovePi";

            sprintf(comm, ".menubar.reminder.group configure "
                             "-postcommand [list PostDynamicMenu .menubar.reminder.group Reminder_PostGroupMenu %d]\n", remIdx);
            eval_check(interp, comm);
         }
         else if (PiRemind_SearchRemPiMatch(dbc, pPiBlock, SUPRESS_REM_MASK) != -1)
         {  // there's a "suppress single PI" reminder for this PI -> offer to remove it
            pConf1 = "-label {Add reminder for selected title} -state disabled";
            pConf2 = "-label {Add reminder into group} -state disabled";
            pConf3 = "-label {Un-suppress selected title} -state normal -command C_PiRemind_RemovePi";
         }
         else
         {  // no match on any single reminder: check for groups
            pConf1 = "-label {Add reminder for selected title} -state normal -command {C_PiRemind_AddPi}";
            pConf2 = "-label {Add reminder into group} -state normal";

            sprintf(comm, ".menubar.reminder.group configure "
                             "-postcommand [list PostDynamicMenu .menubar.reminder.group Reminder_PostGroupMenu -1]\n");
            eval_check(interp, comm);

            if (PiRemind_SearchRemGroupMatch(dbc, pPiBlock) != -1)
            {  // group match -> offer to suppress it
               pConf3 = "-label {Suppress match on selected title} -state normal -command {C_PiRemind_AddPi " SUPRESS_REM_GRPTAG_STR "}";
            }
            else
               pConf3 = "-label {Remove reminder for selected title} -state disabled";
         }
      }
      else
      {
         pConf1 = "-label {Add reminder for selected title} -state disabled";
         pConf2 = "-label {Add reminder into group} -state disabled";
         pConf3 = "-label {Remove reminder for selected title} -state disabled";
      }

      sprintf(comm, ".menubar.reminder entryconfigure 0 %s\n"
                    ".menubar.reminder entryconfigure 1 %s\n"
                    ".menubar.reminder entryconfigure 2 %s\n", pConf1, pConf2, pConf3);
      eval_check(interp, comm);

      EpgDbLockDatabase(dbc, FALSE);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Preprocess reminder list after database load
// - update netwop indices
// - remove reminders referring to expired programmes
//
void PiRemind_CheckDb( void )
{
   const AI_BLOCK * pAiBlock;
   Tcl_Obj  * pRemListObj;
   Tcl_Obj ** pRemArgv;
   Tcl_Obj ** pElemArgv;
   Tcl_Obj  * pTmpObj;
   time_t     expireThreshold;
   bool       elem_ok;
   int        remIdx, remCount, elemCount;
   int        group, cni, netwop, pil, start_time, stop_time;

   if (isRemPiCacheInitialized)
      Tcl_DeleteHashTable(&RemPiCache);
   Tcl_InitHashTable(&RemPiCache, TCL_ONE_WORD_KEYS);
   RemPiCacheBaseTime      = time(NULL);
   isRemPiCacheInitialized = TRUE;

   EpgDbLockDatabase(dbc, TRUE);
   pAiBlock = EpgDbGetAi(dbc);
   if (pAiBlock != NULL)
   {
      pRemListObj = Tcl_GetVar2Ex(interp, "reminders", NULL, TCL_GLOBAL_ONLY);
      if ( (pRemListObj != NULL) &&
           (Tcl_ListObjGetElements(interp, pRemListObj, &remCount, &pRemArgv) == TCL_OK) )
      {
         expireThreshold = time(NULL) - dbc->expireDelayPi;

         // loop across reminder list: note list length may shrink inside
         remIdx = 0;
         while (remIdx < remCount)
         {
            elem_ok = FALSE;

            // each reminder list element is again a list of parameters
            if ( (Tcl_ListObjGetElements(interp, pRemArgv[remIdx], &elemCount, &pElemArgv) == TCL_OK) &&
                 (elemCount == EPGTCL_RPI_IDX_COUNT) )
            {
               if ( (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_GRPTAG_IDX], &group) == TCL_OK) &&
                    (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_NETWOP_IDX], &netwop) == TCL_OK) &&
                    (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_CNI_IDX], &cni) == TCL_OK) &&
                    (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_PIL_IDX], &pil) == TCL_OK) &&
                    (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_START_IDX], &start_time) == TCL_OK) &&
                    (Tcl_GetIntFromObj(interp, pElemArgv[EPGTCL_RPI_STOP_IDX], &stop_time) == TCL_OK) )
               {
                  // check if reminder is expired: use same threshold as for PI in database
                  if (stop_time >= expireThreshold)
                  {
                     if ( (group != SUPRESS_REM_GRPTAG) &&
                          (Tcl_GetVar2Ex(interp, "remgroups", Tcl_GetString(pElemArgv[EPGTCL_RPI_GRPTAG_IDX]), TCL_GLOBAL_ONLY) == NULL) )
                     {  // invalid group index found -> repair by changing to default group
                        debug3("PiRemind-CheckDb: invalid group tag '%s' found in reminder #%d (%s): moving to default group", Tcl_GetString(pElemArgv[EPGTCL_RPI_GRPTAG_IDX]), remIdx, Tcl_GetString(pElemArgv[EPGTCL_RPI_TITLE_IDX]));
                        if (PiReminder_UnshareList(&pRemListObj, &pRemArgv, remIdx, &pElemArgv, EPGTCL_RPI_GRPTAG_IDX) != TCL_OK)
                           goto tcl_error;

                        pTmpObj = PiRemind_GetDefaultGroupTag(&group);
                        if ( (pTmpObj != NULL) &&
                             (Tcl_ListObjReplace(interp, pRemArgv[remIdx], EPGTCL_RPI_GRPTAG_IDX, 1, 1, &pTmpObj) != TCL_OK) )
                           debug2("PiRemind-CheckDb: failed to replace group tag in reminder %d ('%s')", remIdx, Tcl_GetString(pRemArgv[remIdx]));
                     }
                     // check if netwop index is still correct, i.e. matches the CNI
                     if ( (netwop >= pAiBlock->netwopCount) || (netwop < 0) ||
                          (AI_GET_NETWOP_N(pAiBlock, netwop)->cni != cni) )
                     {
                        // mismatch -> search CNI in AI's list to determine netwop index
                        for (netwop = 0; netwop < pAiBlock->netwopCount; netwop++)
                           if (AI_GET_NETWOP_N(pAiBlock, netwop)->cni == cni)
                              break;
                        if (PiReminder_UnshareList(&pRemListObj, &pRemArgv, remIdx, &pElemArgv, EPGTCL_RPI_NETWOP_IDX) != TCL_OK)
                           goto tcl_error;

                        Tcl_SetIntObj(pElemArgv[EPGTCL_RPI_NETWOP_IDX], netwop);
                        Tcl_InvalidateStringRep(pRemArgv[remIdx]);
                     }

                     PiRemind_PiCacheAdd(remIdx, group, netwop, start_time, pil);

                     elem_ok = TRUE;
                  }
                  else
                     dprintf6("PiRemind-CheckDb: dropping expired reminder #%d: %d-%d (expired by %d min), CNI 0x%04X, '%s'\n", remIdx, start_time, stop_time, (int)expireThreshold - stop_time, cni, Tcl_GetString(pElemArgv[EPGTCL_RPI_TITLE_IDX]));
               }
               else
                  debugTclErr(interp, "PiRemind-CheckDb: reminder list element corrupt");
            }
            else
               debug2("PiRemind-CheckDb: reminder list elem #%d invalid: '%s'", remIdx, Tcl_GetString(pRemArgv[remIdx]));

            if (elem_ok == FALSE)
            {  // reminder list element is defective or expired -> remove it
               if (PiReminder_UnshareList(&pRemListObj, NULL, 0, NULL, 0) != TCL_OK)
                  goto tcl_error;
               if ( (Tcl_ListObjReplace(interp, pRemListObj, remIdx, 1, 0, NULL) != TCL_OK) ||
                    (Tcl_ListObjGetElements(interp, pRemListObj, &remCount, &pRemArgv) != TCL_OK) )
               {
                  debugTclErr(interp, "PiRemind-CheckDb: reminder list removal failed");
                  goto tcl_error;
               }
            }
            else
               remIdx += 1;
         }
      }
      else
         debug1("PiRemind-CheckDb: reminders variable undefined (%d) or not a list", (int)(pRemListObj==NULL));
   }

tcl_error:
   EpgDbLockDatabase(dbc, FALSE);
}

// ----------------------------------------------------------------------------
// Reload single PI cache after change to reminder list by GUI
// - note there are special functions for reminder deletion and group change
//
static int PiRemind_PiReload( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiRemind_PiReload";
   int   result;

   if (objc != 1)
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      PiRemind_CheckDb();
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Free module resources
//
void PiRemind_Destroy( void )
{
   if (isRemPiCacheInitialized)
   {
      Tcl_DeleteHashTable(&RemPiCache);
      isRemPiCacheInitialized = FALSE;
   }
}

// ----------------------------------------------------------------------------
// Initialize the module
//
void PiRemind_Create( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_SetReminderMenuStates", &cmdInfo) == 0)
   {
      Tcl_CreateObjCommand(interp, "C_SelectReminderFilter", SelectReminderFilter, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiRemind_MatchFirstReminder", PiRemind_MatchFirstReminder, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiRemind_GetReminderEvents", PiRemind_GetReminderEvents, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SetReminderMenuStates", PiRemind_SetReminderMenuStates, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiRemind_ContextMenuShort", PiRemind_ContextMenu, (ClientData) INT2PVOID(FALSE), NULL);
      Tcl_CreateObjCommand(interp, "C_PiRemind_ContextMenuExtended", PiRemind_ContextMenu, (ClientData) INT2PVOID(TRUE), NULL);

      Tcl_CreateObjCommand(interp, "C_PiRemind_AddPi", PiRemind_AddPi, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiRemind_RemovePi", PiRemind_RemovePi, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiRemind_PiSetGroup", PiRemind_PiSetGroup, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiRemind_PiReload", PiRemind_PiReload, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiRemind_SetControl", PiRemind_SetControl, (ClientData) NULL, NULL);

      // note: called after reminder groups have been checked and after the initial
      // database has been loaded -> prepare reminder list now
      PiRemind_CheckDb();
   }
}

