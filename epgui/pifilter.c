/*
 *  Nextview GUI: PI search filter control
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
 *    Provides callbacks to the widgets used for EPG filter settings.
 *    Allows to control the selection of items displayed in the PI
 *    listbox.
 */

#define __PIFILTER_C

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
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgmain.h"
#include "epgui/epgsetup.h"
#include "epgui/pdc_themes.h"
#include "epgui/pibox.h"
#include "epgui/pidescr.h"
#include "epgui/pifilter.h"
#include "epgui/uictrl.h"


// this is the filter context, which contains all filter settings
// for the PiBox modules
static FILTER_CONTEXT *pUiFilterContext = NULL;

// this is a cache for alternate contexts which are used in user-defined PI listbox columns
static FILTER_CONTEXT **pFilterCache = NULL;
static uint  filterCacheCount = 0;

// this points to one of the above contexts:
// to the one to which subsequent filter changes are to be applied
FILTER_CONTEXT *pPiFilterContext = NULL;

#define dbc pUiDbContext         // internal shortcut

// ----------------------------------------------------------------------------
// Update the themes filter setting
//
static int SelectThemes( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_SelectThemes <class 1..8> <themes-list>";
   Tcl_Obj ** pThemes;
   uchar usedClasses;
   int themeCount;
   int tclass, theme, idx;
   int result; 
   
   if ( (objc != 3)  || Tcl_GetIntFromObj(interp, objv[1], &tclass) ||
        (tclass == 0) || ((uint)tclass > THEME_CLASS_COUNT) )
   {  // illegal parameter count, format or value
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_ListObjGetElements(interp, objv[2], &themeCount, &pThemes);
      if (result == TCL_OK)
      {
         tclass = 1 << (tclass - 1);
         usedClasses = EpgDbFilterInitThemes(pPiFilterContext, tclass);

         for (idx=0; idx < themeCount; idx++)
         {
            result = Tcl_GetIntFromObj(interp, pThemes[idx], &theme);
            if (result == TCL_OK)
            {
               EpgDbFilterSetThemes(pPiFilterContext, theme, theme, tclass);
            }
            else
               break;
         }

         if ((themeCount > 0) || usedClasses)
            EpgDbFilterEnable(pPiFilterContext, FILTER_THEMES);
         else
            EpgDbFilterDisable(pPiFilterContext, FILTER_THEMES);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for netwop listbox: update the netwop filter setting
//
static int SelectNetwops( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_SelectNetwops <netwop-list>";
   const AI_BLOCK *pAiBlock;
   Tcl_Obj ** pNetwops;
   int netwopCount;
   int netwop, idx;
   int result; 
   
   if (objc != 2) 
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_ListObjGetElements(interp, objv[1], &netwopCount, &pNetwops);
      if (result == TCL_OK)
      {
         if (netwopCount > 0)
         {
            EpgDbLockDatabase(dbc, TRUE);
            pAiBlock = EpgDbGetAi(dbc);
            if (pAiBlock != NULL)
            {
               EpgDbFilterInitNetwop(pPiFilterContext, pAiBlock->netwopCount);
               for (idx=0; idx < netwopCount; idx++)
               {
                  result = Tcl_GetIntFromObj(interp, pNetwops[idx], &netwop);
                  if (result == TCL_OK)
                  {
                     EpgDbFilterSetNetwop(pPiFilterContext, netwop);
                  }
                  else
                     break;
               }
               EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP);
            }
            else
               debug0("C_SelectNetwops: no AI in db");

            EpgDbLockDatabase(dbc, FALSE);
         }
         else
         {  // no netwops selected -> disable filter
            EpgDbFilterDisable(pPiFilterContext, FILTER_NETWOP);
         }
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for features menu: update the feature filter setting
//
static int SelectFeatures( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_SelectFeatures (<mask> <value>){0..6}";
   Tcl_Obj ** pFeatures;
   int idx, featureCount;
   int mask, value;
   int result; 
   
   if (objc != 2)
   {  // illegal parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_ListObjGetElements(interp, objv[1], &featureCount, &pFeatures);
      if (result == TCL_OK)
      {
         for (idx=0; idx < featureCount; idx += 2)
         {
            if ( (Tcl_GetIntFromObj(interp, pFeatures[idx], &mask) == TCL_OK) &&
                 (Tcl_GetIntFromObj(interp, pFeatures[idx + 1], &value) == TCL_OK) )
            {
               EpgDbFilterSetFeatureFlags(pPiFilterContext, idx / 2, value, mask);
            }
            else
            {  // parse error -> abort
               result = TCL_ERROR;
               break;
            }
         }

         if (featureCount > 0)
         {
            EpgDbFilterSetNoFeatures(pPiFilterContext, featureCount / 2);
            EpgDbFilterEnable(pPiFilterContext, FILTER_FEATURES);
         }
         else
            EpgDbFilterDisable(pPiFilterContext, FILTER_FEATURES);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Update the parental rating filter setting
//
static int SelectParentalRating( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_SelectParentalRating <value>";
   int rating;
   int result; 
   
   if ( (objc != 2) || Tcl_GetIntFromObj(interp, objv[1], &rating) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      if (rating != 0xFF)
      {
         EpgDbFilterEnable(pPiFilterContext, FILTER_PAR_RAT);
         EpgDbFilterSetParentalRating(pPiFilterContext, rating);
      }
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_PAR_RAT);

      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Update the editorial rating filter setting
//
static int SelectEditorialRating( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_SelectEditorialRating <value>";
   int rating;
   int result; 
   
   if ( (objc != 2) || Tcl_GetIntFromObj(interp, objv[1], &rating) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      if (rating != 0xFF)
      {
         EpgDbFilterEnable(pPiFilterContext, FILTER_EDIT_RAT);
         EpgDbFilterSetEditorialRating(pPiFilterContext, rating);
      }
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_EDIT_RAT);

      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for entry widget: update the sub-string filter setting
//
static int SelectSubStr( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_SelectSubStr [param-list ...]\nwith params: "
                               "{<string> <bTitle=0/1> <bDescr=0/1> <bCase=0/1> <bFull=0/1> <reserved> <reserved>}]";
   Tcl_Obj ** pStrList;
   Tcl_Obj ** pParList;
   int scope_title, scope_descr, match_case, match_full;
   const char * subStr;
   sint  idx;
   bool  enable;
   int strCount;
   int parCount;
   int subStrLen;
   int result; 
   
   if (objc != 2) 
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_ListObjGetElements(interp, objv[1], &strCount, &pStrList);
      if (result == TCL_OK)
      {
         EpgDbFilterDisable(pPiFilterContext, FILTER_SUBSTR);
         enable = FALSE;

         for (idx=0; idx < strCount; idx++)
         {
            if ( (Tcl_ListObjGetElements(interp, pStrList[idx], &parCount, &pParList) != TCL_OK) ||
                 (parCount < 5) ||
                 (Tcl_GetBooleanFromObj(interp, pParList[1], &scope_title) != TCL_OK) ||
                 (Tcl_GetBooleanFromObj(interp, pParList[2], &scope_descr) != TCL_OK) ||
                 (Tcl_GetBooleanFromObj(interp, pParList[3], &match_case)  != TCL_OK) ||
                 (Tcl_GetBooleanFromObj(interp, pParList[4], &match_full)  != TCL_OK) )
            {  // one parameter is not a boolean
               result = TCL_ERROR; 
               break;
            }

            subStr = Tcl_GetStringFromObj(pParList[0], &subStrLen);

            if ((subStr != NULL) && (subStrLen > 0))
            {
               EpgDbFilterSetSubStr(pPiFilterContext, subStr, scope_title, scope_descr, match_case, match_full);
               enable = TRUE;
            }
            else
               debug1("Select-SubStr: empty substr pattern at param #%d", idx);
         }

         if (enable)
            EpgDbFilterEnable(pPiFilterContext, FILTER_SUBSTR);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Callback for start time filter dialog
//
static int SelectStartTime( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_SelectStartTime <start-rel:0/1> <stop-midnight:0/1> "
                               "<date-mode:0/1/2> <start time> <stop time> <rel.date>";
   int isRelStart, isAbsStop, dateMode;
   int startTime, stopTime, relDate;
   NI_FILTER_STATE nifs;
   static CONST84 char * pKeywords[] = {"rel", "ignore", "wday", "mday", (char *) NULL};
   enum date_mode_keys { DM_REL, DM_IGN, DM_WDAY, DM_MDAY };
   int result; 

   if (objc == 1) 
   {  // no parameters -> disable filter
      EpgDbFilterDisable(pPiFilterContext, FILTER_TIME_ALL);
      result = TCL_OK; 
   }
   else if (objc != 7) 
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if ( (Tcl_GetBooleanFromObj(interp, objv[1], &isRelStart) != TCL_OK) ||
             (Tcl_GetBooleanFromObj(interp, objv[2], &isAbsStop) != TCL_OK) ||
             (Tcl_GetIndexFromObj(interp, objv[3], pKeywords, "keyword", TCL_EXACT, &dateMode) != TCL_OK) ||
             (Tcl_GetIntFromObj(interp, objv[4], &startTime) != TCL_OK) || ((uint)startTime > 23*60+59) ||
             (Tcl_GetIntFromObj(interp, objv[5], &stopTime) != TCL_OK)  || ((uint)stopTime > 23*60+59) ||
             (Tcl_GetIntFromObj(interp, objv[6], &relDate) != TCL_OK) )
   {  // one parameter has an invalid format or value
      result = TCL_ERROR; 
   }
   else
   {
      EpgDbFilterDisable(pPiFilterContext, FILTER_TIME_ALL);

      nifs.flags = NI_DATE_START;

      if (isRelStart == FALSE)
         nifs.startMoD = startTime;
      else
         nifs.startMoD = 0xffff;
      if (isAbsStop == FALSE)
      {
         nifs.flags |= NI_DATE_STOP;
         nifs.stopMoD = stopTime;
      }
      switch (dateMode)
      {
         case DM_REL:
            nifs.flags  |= NI_DATE_RELDATE;
            nifs.reldate = relDate;
            break;
         case DM_WDAY:
            nifs.flags  |= NI_DATE_WEEKLY;
            nifs.reldate = relDate;
            break;
         case DM_MDAY:
            nifs.flags  |= NI_DATE_MONTHLY;
            nifs.reldate = relDate;
            break;
      }
      EpgDbFilterFinishNi(pPiFilterContext, &nifs);

      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Callback for duration selection popup
//
static int SelectMinMaxDuration( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_SelectMinMaxDuration <min> <max>";
   int dur_min, dur_max;
   int result; 
   
   if (objc != 1+2) 
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if ( (Tcl_GetIntFromObj(interp, objv[1], &dur_min) != TCL_OK) || ((uint)dur_min > 23*60+59) ||
             (Tcl_GetIntFromObj(interp, objv[2], &dur_max) != TCL_OK) || ((uint)dur_max > 23*60+59) )
   {  // one parameter has an invalid value
      result = TCL_ERROR; 
   }
   else
   {
      if (dur_max > 0)
      {
         EpgDbFilterSetMinMaxDuration(pPiFilterContext, dur_min * 60, dur_max * 60);
         EpgDbFilterEnable(pPiFilterContext, FILTER_DURATION);
      }
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_DURATION);

      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Callback for PI expire time delta popup
// - the parameter is read from the Tcl variable and need not be passed here
//
static int SelectExpiredPiDisplay( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const PI_BLOCK * pPiBlock;
   time_t lastStop;
   time_t newThresh;
   time_t oldThresh;

   oldThresh = EpgDbFilterGetExpireTime(pPiFilterContext);

   // apply the new filter setting
   PiFilter_Expire();

   newThresh = EpgDbFilterGetExpireTime(pPiFilterContext);

   // determine the max. stop time of all PI in the database
   EpgDbLockDatabase(dbc, TRUE);
   lastStop = 0;
   pPiBlock = EpgDbSearchLastPi(pUiDbContext, NULL);
   while (pPiBlock != NULL)
   {
      if (pPiBlock->stop_time > lastStop)
         lastStop = pPiBlock->stop_time;

      pPiBlock = EpgDbSearchPrevPi(pUiDbContext, NULL, pPiBlock);
   }
   EpgDbLockDatabase(dbc, FALSE);

   // check if threshold was moved across the max stop time
   if ( (lastStop != 0) &&
        ((lastStop <= oldThresh) != (lastStop <= newThresh)) )
   {
      UiControl_CheckDbState(NULL);
   }
   return TCL_OK; 
}

// ----------------------------------------------------------------------------
// Callback for VPS/PDC filter changes
//
static int EnableExpirePreFilter( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_EnableExpirePreFilter <0/1>";
   int enable;
   int result;

   if ((objc != 1+1)  || (Tcl_GetBooleanFromObj(interp, objv[1], &enable) != TCL_OK))
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (enable)
         EpgDbPreFilterEnable(pPiFilterContext, FILTER_EXPIRE_TIME);
      else
         EpgDbPreFilterDisable(pPiFilterContext, FILTER_EXPIRE_TIME);

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Callback for VPS/PDC filter changes
//
static int SelectVpsPdcFilter( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: SelectVpsPdcFilter <mode>";
   int mode;
   int result; 
   
   if (objc != 1+1) 
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if (Tcl_GetIntFromObj(interp, objv[1], &mode) != TCL_OK)
   {  // one parameter has an invalid value
      result = TCL_ERROR; 
   }
   else
   {
      if (mode != 0)
      {
         EpgDbFilterSetVpsPdcMode(pPiFilterContext, mode);
         EpgDbFilterEnable(pPiFilterContext, FILTER_VPS_PDC);
      }
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_VPS_PDC);

      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Callback for Program-Index radio buttons
//
static int SelectProgIdx( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_SelectProgIdx <first-index> <last-index>";
   int first, last;
   int result; 
   
   if ( (objc == 3)  &&
        (Tcl_GetIntFromObj(interp, objv[1], &first) == TCL_OK) &&
        (Tcl_GetIntFromObj(interp, objv[2], &last) == TCL_OK) )
   {  // set min and max index boundaries
      EpgDbFilterEnable(pPiFilterContext, FILTER_PROGIDX);
      EpgDbFilterSetProgIdx(pPiFilterContext, first, last);
      result = TCL_OK; 
   }
   else if (objc == 1) 
   {  // no parameters -> disable filter
      EpgDbFilterDisable(pPiFilterContext, FILTER_PROGIDX);
      result = TCL_OK; 
   }
   else
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  

   return result;
}

// ----------------------------------------------------------------------------
// Disable all given filter types
//
static int PiFilter_Reset( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ResetFilter <mask-list>";
   Tcl_Obj   ** pKeyList;
   int  idx, keywordCount;
   int  filtIndex;
   uint mask;
   int  result; 

   static CONST84 char * pKeywords[] = {"all", "netwops", "themes",
      "substr", "progidx", "timsel", "dursel", "parental", "editorial",
      "features", "vps_pdc", (char *) NULL};
   enum reset_keys { FE_ALL, FE_NETWOPS, FE_THEMES,
      FE_SUBSTR, FE_PROGIDX, FE_TIMSEL, FE_DURSEL, FE_PARENTAL, FE_EDITORIAL,
      FE_FEATURES, FE_VPS_PDC};

   if (objc != 2)
   {  // illegal parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_ListObjGetElements(interp, objv[1], &keywordCount, &pKeyList);
      if (result == TCL_OK)
      {
         mask = 0;
         for (idx=0; idx < keywordCount; idx++)
         {
            if (Tcl_GetIndexFromObj(interp, pKeyList[idx], pKeywords, "keyword", TCL_EXACT, &filtIndex) == TCL_OK)
            {
               switch (filtIndex)
               {
                  case FE_ALL:        mask |= FILTER_ALL & ~FILTER_PERM; break;
                  case FE_NETWOPS:    mask |= FILTER_NETWOP; break;
                  case FE_THEMES:     mask |= FILTER_THEMES; break;
                  case FE_SUBSTR:     mask |= FILTER_SUBSTR; break;
                  case FE_PROGIDX:    mask |= FILTER_PROGIDX; break;
                  case FE_TIMSEL:     mask |= FILTER_TIME_ALL; break;
                  case FE_DURSEL:     mask |= FILTER_DURATION; break;
                  case FE_PARENTAL:   mask |= FILTER_PAR_RAT; break;
                  case FE_EDITORIAL:  mask |= FILTER_EDIT_RAT; break;
                  case FE_FEATURES:   mask |= FILTER_FEATURES; break;
                  case FE_VPS_PDC:    mask |= FILTER_VPS_PDC; break;
                  default:
                     fatal1("PiFilter-Reset: index lookup failure: illegal index %d", filtIndex);
                     break;
               }
            }
            else
               debug2("PiFilter-Reset: unknown keyword at idx %d: '%s'", idx, Tcl_GetString(pKeyList[idx]));
         }

         EpgDbFilterDisable(pPiFilterContext, mask);
      }
   }

   return result; 
}

// ----------------------------------------------------------------------------
// Invert all given filters
// - note that "all" is NOT a logical OR of all possible filter types but instead
//   it's the "global invert"
//
static int PiFilter_Invert( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_InvertFilter <keyword-list>";
   const char * pThisKey;
   Tcl_Obj   ** pKeyList;
   uchar themeClass;
   uint  mask, tclass;
   bool  globalInvert;
   int   filtIndex;
   int   idx, keywordCount;
   int   result;

   static CONST84 char * pKeywords[] = {"all", "netwops", "themes",
      "substr", "progidx", "timsel", "dursel", "parental", "editorial",
      "features", "vps_pdc", "custom", (char *) NULL};
   enum reset_keys { FE_ALL, FE_NETWOPS, FE_THEMES,
      FE_SUBSTR, FE_PROGIDX, FE_TIMSEL, FE_DURSEL, FE_PARENTAL, FE_EDITORIAL,
      FE_FEATURES, FE_VPS_PDC, FE_CUSTOM};

   if (objc != 2)
   {  // illegal parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = Tcl_ListObjGetElements(interp, objv[1], &keywordCount, &pKeyList);
      if (result == TCL_OK)
      {
         themeClass = 0;
         mask = 0;
         globalInvert = FALSE;

         for (idx=0; idx < keywordCount; idx++)
         {
            pThisKey = Tcl_GetString(pKeyList[idx]);
            if (pThisKey != NULL)
            {
               if (sscanf(pThisKey, "theme_class%u", &tclass) == 1)
               {
                  ifdebug1((tclass == 0) || (tclass > 8), "PiFilter-Invert: illegal theme class %d", tclass);
                  mask          |= FILTER_THEMES;
                  themeClass    |= (1 << (tclass - 1));
               }
               else if (Tcl_GetIndexFromObj(interp, pKeyList[idx], pKeywords, "keyword", TCL_EXACT, &filtIndex) == TCL_OK)
               {
                  switch (filtIndex)
                  {
                     case FE_ALL:        globalInvert = TRUE; break;
                     case FE_NETWOPS:    mask |= FILTER_NETWOP; break;
                     case FE_SUBSTR:     mask |= FILTER_SUBSTR; break;
                     case FE_PROGIDX:    mask |= FILTER_PROGIDX; break;
                     case FE_TIMSEL:     mask |= FILTER_TIME_ALL; break;
                     case FE_DURSEL:     mask |= FILTER_DURATION; break;
                     case FE_PARENTAL:   mask |= FILTER_PAR_RAT; break;
                     case FE_EDITORIAL:  mask |= FILTER_EDIT_RAT; break;
                     case FE_FEATURES:   mask |= FILTER_FEATURES; break;
                     case FE_VPS_PDC:    mask |= FILTER_VPS_PDC; break;
                     case FE_CUSTOM:     mask |= FILTER_CUSTOM; break;

                     case FE_THEMES:
                        debug1("PiFilter-Invert: illegal key: %s", pThisKey);
                        break;
                     default:
                        fatal1("PiFilter-Invert: index lookup failure: illegal index %d", filtIndex);
                        break;
                  }
               }
               else
                  debug2("PiFilter-Reset: unknown keyword at idx %d: '%s'", idx, Tcl_GetString(pKeyList[idx]));
            }
         }

         EpgDbFilterInvert(pPiFilterContext, mask, themeClass);

         if (globalInvert)
            EpgDbFilterEnable(pPiFilterContext, FILTER_INVERT);
         else
            EpgDbFilterDisable(pPiFilterContext, FILTER_INVERT);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Control filter context forks
// - allows logical AND/OR between complex filter parameter sets
//   instead of the implicit filter combinations when shortcuts are "merged"
//
static int PiFilter_ForkContext( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiFilter_ForkContext <keyword> [<tag>]";
   int   ctrl;
   int   tag;
   int   result;

   static CONST84 char * pKeywords[] = {"or", "and", "close", "remove", "reset", (char *) NULL};
   enum fork_keys { FORK_OR, FORK_AND, FORK_CLOSE, FORK_REMOVE, FORK_RESET };

   if ( ((objc != 2) && (objc != 3)) ||
        ((objc == 3) && (Tcl_GetIntFromObj(interp, objv[2], &tag) != TCL_OK)) )
   {  // illegal parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (objc == 2)
         tag = -1;

      if (Tcl_GetIndexFromObj(interp, objv[1], pKeywords, "keyword", TCL_EXACT, &ctrl) == TCL_OK)
      {
         switch (ctrl)
         {
            case FORK_OR:
               EpgDbFilterFork(pPiFilterContext, FILTER_FORK_OR, tag);
               break;
            case FORK_AND:
               EpgDbFilterFork(pPiFilterContext, FILTER_FORK_AND, tag);
               break;
            case FORK_CLOSE:
               EpgDbFilterCloseFork(pPiFilterContext);
               break;
            case FORK_REMOVE:
               EpgDbFilterDestroyFork(pPiFilterContext, tag);
               break;
            case FORK_RESET:
               EpgDbFilterDestroyAllForks(pPiFilterContext);
               break;
            default:
               fatal1("PiFilter-ForkContext: invalid keyword index %d", ctrl);
               break;
         }
      }
      else
         debug1("PiFilter-ForkContext: invalid keyword '%s'", Tcl_GetString(objv[1]));

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Match the given PI against a filter context
//
bool PiFilter_ContextCacheMatch( const PI_BLOCK * pPiBlock, uint idx )
{
   uint result = FALSE;

   if ((idx < filterCacheCount) && (pFilterCache[idx] != NULL))
   {
      if (EpgDbFilterMatches(dbc, pFilterCache[idx], pPiBlock))
      {
         result = TRUE;
      }
   }
   else
      debug2("PiFilter-ContextCacheMatch: no filter ctx for user-def #%d (cache count %d)", idx, filterCacheCount);

   return result;
}

// ----------------------------------------------------------------------------
// Match the given PI against a filter context
//
const FILTER_CONTEXT * PiFilter_ContextCacheGet( uint idx )
{
   FILTER_CONTEXT * pCtx = NULL;

   if (idx < filterCacheCount)
   {
      pCtx = pFilterCache[idx];
   }
   else
      debug2("PiFilter-ContextCacheGet: invalid index %d (cache count %d)", idx, filterCacheCount);

   return pCtx;
}

// ----------------------------------------------------------------------------
// Clear the context cache (free resources)
//
static void PiFilter_ContextCacheClear( void )
{
   uint  idx;

   if (pFilterCache != NULL)
   {
      for (idx = 0; idx < filterCacheCount; idx++)
      {
         if (pFilterCache[idx] != NULL)
            EpgDbFilterDestroyContext(pFilterCache[idx]);
      }

      xfree(pFilterCache);
      pFilterCache = NULL;
      filterCacheCount = 0;
   }
}

// ----------------------------------------------------------------------------
// Shortcut filter context cache control
// - "start" control: free the previous cache, allocate new one for the given number of entries
// - "set" control  : select the n-th cache context for subsequent filter selections
//                    must be called for all contexts in the cache during initialization
// - "done" control : switch back to the UI filter context (after the cache is initialized)
//
static int PiFilter_ContextCacheCtl( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiFilter_ContextCacheCtl {start|set|fork|done} [idx]";
   const char * pKey;
   int   index;
   int   result = TCL_ERROR;

   if ((objc >= 2) && ((pKey = Tcl_GetString(objv[1])) != NULL))
   {
      if ((strcmp(pKey, "start") == 0) && (objc == 3))
      {
         if (Tcl_GetIntFromObj(interp, objv[2], &index) == TCL_OK)
         {
            // free the previous cache
            PiFilter_ContextCacheClear();

            // copy number of elements in the new cache into global variable
            filterCacheCount = index;

            if (index > 0)
            {
               // allocate a new cache: array which will hold references to filter contexts
               pFilterCache = (FILTER_CONTEXT**) xmalloc(filterCacheCount * sizeof(*pFilterCache));
               memset(pFilterCache, 0, filterCacheCount * sizeof(*pFilterCache));
            }
            result = TCL_OK;
         }
      }
      else if ((strcmp(pKey, "set") == 0) && (objc == 3))
      {
         if (Tcl_GetIntFromObj(interp, objv[2], &index) == TCL_OK)
         {
            if ((uint)index < filterCacheCount)
            {
               if (pFilterCache[index] == NULL)
                  pFilterCache[index] = EpgDbFilterCreateContext();
               else
                  debug1("PiFilter-ContextCacheCtl: filter context %d already set", index);
               pPiFilterContext = pFilterCache[index];
            }
            else
               debug2("PiFilter-ContextCacheCtl: invalid index %d >= count %d", index, filterCacheCount);

            result = TCL_OK;
         }
      }
      else if ((strcmp(pKey, "done") == 0) && (objc == 2))
      {
         EpgDbFilterCloseFork(pPiFilterContext);
         pPiFilterContext = pUiFilterContext;
         result = TCL_OK;
      }
      else
         debug2("PiFilter-ContextCacheCtl: invalid keyword '%s' or arg count %d", pKey, objc);
   }

   if ((result == TCL_ERROR) && (strlen(Tcl_GetStringResult(interp)) == 0))
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);

   return result;
}

// ----------------------------------------------------------------------------
// Return the name for a given PDC theme category index
//
static int GetPdcString( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_GetPdcString <index>";
   const char * pGeneralStr;
   const char * pThemeStr;
   Tcl_Obj * pTmpObj;
   int index;
   int result; 
   
   if ( (objc != 2) || (Tcl_GetIntFromObj(interp, objv[1], &index) != TCL_OK) )
   {  // wrong parameter count or no integer parameter
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      pThemeStr = PdcThemeGetWithGeneral(index, &pGeneralStr, FALSE);
      if (pThemeStr != NULL)
      {
         pTmpObj = Tcl_NewStringObj(pThemeStr, -1);
         Tcl_AppendStringsToObj(pTmpObj, pGeneralStr, NULL);

         Tcl_SetObjResult(interp, pTmpObj);
      }
      else
      {
         sprintf(comm, "#%d", index);
         Tcl_SetResult(interp, comm, TCL_VOLATILE);
      }
      result = TCL_OK; 
   }
   return result;
}

#if 0
// ----------------------------------------------------------------------------
// Return a list of CNIs of all networks that have recurring title strings
//
static int GetNetwopsWithSeries( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetNetwopsWithSeries";
   Tcl_HashTable piCache;
   Tcl_Obj * pNetwopList;
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK *pPiBlock;
   FILTER_CONTEXT *fc;
   char  strbuf[16+2+1];
   int isNewCacheEntry;
   int netwop;
   int result;

   if (objc != 1) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = TCL_OK; 

      EpgDbLockDatabase(dbc, TRUE);
      fc = EpgDbFilterCreateContext();
      EpgDbFilterEnable(fc, FILTER_NETWOP);

      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         // create an empty list object
         pNetwopList = Tcl_NewListObj(0, NULL);

         for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
         {
            // create a hash for filling with title strings
            Tcl_InitHashTable(&piCache, TCL_STRING_KEYS);

            EpgDbFilterInitNetwop(fc, pAiBlock->netwopCount);
            EpgDbFilterSetNetwop(fc, netwop);

            pPiBlock = EpgDbSearchFirstPi(dbc, fc);
            while (pPiBlock != NULL)
            {
               // add title to hash & check if already hashed previously
               Tcl_CreateHashEntry(&piCache, PI_GET_TITLE(pPiBlock), &isNewCacheEntry);
               if (isNewCacheEntry == FALSE)
               {
                  // found first recurring title -> append the CNI to results
                  sprintf(strbuf, "0x%04X", AI_GET_NET_CNI_N(pAiBlock, netwop));
                  Tcl_ListObjAppendElement(interp, pNetwopList,
                                           Tcl_NewStringObj(strbuf, -1));
                  break;
               }
               pPiBlock = EpgDbSearchNextPi(dbc, fc, pPiBlock);
            }
            Tcl_DeleteHashTable(&piCache);
         }

         Tcl_SetObjResult(interp, pNetwopList);
      }
      EpgDbFilterDestroyContext(fc);
      EpgDbLockDatabase(dbc, FALSE);
   }  

   return result;
}

// ----------------------------------------------------------------------------
// Get a list of recurring titles on a given network
// - returns a 'paired list': original title as in DB and "dictified" title
//
static int GetNetwopSeriesList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetNetwopSeriesList <cni>";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   const char     * pTitle;
   Tcl_Obj * pResultList;
   Tcl_Obj * pTmpObj;
   FILTER_CONTEXT *fc;
   uint netwop;
   uint cni;
   int result;

   if (objc != 2)
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if (Tcl_GetIntFromObj(interp, objv[1], (int*)&cni) != TCL_OK)
   {
      result = TCL_ERROR; 
   }
   else
   {
      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         // convert the CNI parameter to a netwop index
         for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
            if (cni == AI_GET_NET_CNI_N(pAiBlock, netwop))
               break;

         if (netwop < pAiBlock->netwopCount)
         {
            // create an empty list object to hold the result
            pResultList = Tcl_NewListObj(0, NULL);

            fc = EpgDbFilterCreateContext();
            EpgDbFilterEnable(fc, FILTER_NETWOP | FILTER_THEMES);
            EpgDbFilterInitNetwop(fc, pAiBlock->netwopCount);
            EpgDbFilterSetNetwop(fc, netwop);

            pPiBlock = EpgDbSearchFirstPi(dbc, fc);
            while (pPiBlock != NULL)
            {
               EpgDbFilterSetSubStr(fc, PI_GET_TITLE(pPiBlock), TRUE, FALSE, TRUE, TRUE);
               EpgDbFilterEnable(fc, FILTER_SUBSTR);

               // check if there's another block with the same title
               if (EpgDbSearchNextPi(dbc, fc, pPiBlock) != NULL)
               {
                  pTmpObj = Tcl_NewStringObj(PI_GET_TITLE(pPiBlock), -1);
                  Tcl_ListObjAppendElement(interp, pResultList, pTmpObj);

                  pTitle = PiDescription_DictifyTitle(PI_GET_TITLE(pPiBlock), pPiBlock->lang_title, comm, TCL_COMM_BUF_SIZE);
                  Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(pTitle, -1));
               }
               EpgDbFilterDisable(fc, FILTER_SUBSTR);

               pPiBlock = EpgDbSearchNextPi(dbc, fc, pPiBlock);
            }
            EpgDbFilterDestroyContext(fc);

            Tcl_SetObjResult(interp, pResultList);
         }
         else
            debug1("Get-NetwopSeriesList: CNI 0x%04X not found in AI", cni);
      }
      else
         debug0("Get-NetwopSeriesList: no AI in db");
      EpgDbLockDatabase(dbc, FALSE);
      result = TCL_OK; 
   }

   return result;
}
#endif

// ----------------------------------------------------------------------------
// Get a list of all titles starting with a given letter
// - keeps current network filter (i.e. only shows series on selected networks)
// - uses the letter which is in the first position after moving language dependent
//   attributes like 'Der', 'Die', 'Das' etc. to the end ('dictified' titles)
// - returns a 'tripled list': "dictified" title, search text, 0/1 (1=match-complete)
//
static int GetSeriesByLetter( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetSeriesByLetter <letter>";
   Tcl_HashTable piCache;
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK * pPiBlock;
   const char * pTitleDict;
   const char * pTitleSerial;
   const char * pLetter;
   Tcl_Obj * pResultList;
   Tcl_Obj * pTmpObj;
   FILTER_CONTEXT *fc;
   uchar letter, c;
   bool isShortened;
   int isNewCacheEntry;
   int result;

   if (objc != 2)
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         // create a hash for filling with title strings
         Tcl_InitHashTable(&piCache, TCL_STRING_KEYS);

         // create an empty list object to hold the result
         pResultList = Tcl_NewListObj(0, NULL);

         // copy the pre-filters from the current filter context
         // to exclude series from suppressed networks
         fc = EpgDbFilterCopyContext(pPiFilterContext);
         EpgDbFilterDisable(fc, FILTER_ALL & ~(FILTER_PERM | FILTER_NETWOP));

         // get the starting letter from the parameters; "other" means non-alpha chars, e.g. digits
         pLetter = Tcl_GetString(objv[1]);
         if ((pLetter == NULL) || (strcmp(pLetter, "other") == 0))
            letter = 0;
         else
            letter = tolower(pLetter[0]);

         pPiBlock = EpgDbSearchFirstPi(dbc, fc);
         while (pPiBlock != NULL)
         {
            // FIXME use second half of comm buffer here, and first half below
            pTitleDict = PiDescription_DictifyTitle(PI_GET_TITLE(pPiBlock), pPiBlock->lang_title, comm + TCL_COMM_BUF_SIZE/2, TCL_COMM_BUF_SIZE/2);
            // check if the starting letter matches
            c = tolower(pTitleDict[0]);
            if ( (c == letter) ||
                 ((letter == 0) && ((c < 'a') || (c > 'z'))) )
            {
               pTitleSerial = PiDescription_RemoveSeriesIndex(PI_GET_TITLE(pPiBlock), comm, TCL_COMM_BUF_SIZE / 2);
               isShortened = (pTitleSerial != PI_GET_TITLE(pPiBlock));

               Tcl_CreateHashEntry(&piCache, pTitleSerial, &isNewCacheEntry);
               if (isNewCacheEntry == FALSE)
               {
                  // found first recurring title -> append the CNI to results
                  pTmpObj = Tcl_NewStringObj(pTitleDict, -1);
                  Tcl_ListObjAppendElement(interp, pResultList, pTmpObj);

                  pTmpObj = Tcl_NewStringObj(pTitleSerial, -1);
                  Tcl_ListObjAppendElement(interp, pResultList, pTmpObj);

                  Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(!isShortened));
               }
            }
            pPiBlock = EpgDbSearchNextPi(dbc, fc, pPiBlock);
         }

         EpgDbFilterDestroyContext(fc);
         Tcl_DeleteHashTable(&piCache);

         Tcl_SetObjResult(interp, pResultList);
      }
      else
         debug0("Get-NetwopSeriesList: no AI in db");
      EpgDbLockDatabase(dbc, FALSE);
      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Retrieve netwop filter setting for shortcut addition
// - returns CNIs, to be independent of AI netwop table ordering
//
static int GetNetwopFilterList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetNetwopFilterList";
   const AI_BLOCK *pAiBlock;
   Tcl_Obj * pResultList;
   char strbuf[16+2+1];
   int result;

   if (objc != 1) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      if (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_NETWOP))
      {
         EpgDbLockDatabase(dbc, TRUE);
         pAiBlock = EpgDbGetAi(dbc);
         if (pAiBlock != NULL)
         {
            pResultList = Tcl_NewListObj(0, NULL);
            for (uint netwop = 0; netwop < pAiBlock->netwopCount; netwop++)
            {
               if (pPiFilterContext->act.pNetwopFilterField[netwop])
               {
                  sprintf(strbuf, "0x%04X", AI_GET_NET_CNI_N(pAiBlock, netwop));
                  Tcl_ListObjAppendElement(interp, pResultList,
                                           Tcl_NewStringObj(strbuf, -1));
               }
            }
            Tcl_SetObjResult(interp, pResultList);
         }
         EpgDbLockDatabase(dbc, FALSE);
      }
      result = TCL_OK; 
   }
   return result;
}

// ----------------------------------------------------------------------------
// Get stop time of the last PI in the current UI database
//
static int GetLastPiTime( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetLastPiTime";
   const PI_BLOCK * pPiBlock;
   time_t  lastTime;
   int     result;

   if (objc != 1) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      EpgDbLockDatabase(dbc, TRUE);
      // warning: this may not be the maximum stop time since PI are sorted by start time!
      pPiBlock = EpgDbSearchLastPi(pUiDbContext, pPiFilterContext);
      if (pPiBlock != NULL)
         lastTime = pPiBlock->stop_time;
      else
         lastTime = 0;
      EpgDbLockDatabase(dbc, FALSE);

      Tcl_SetObjResult(interp, Tcl_NewIntObj(lastTime));
      result = TCL_OK; 
   }
   return result;
}

// ----------------------------------------------------------------------------
// Update network prefilter for restricted air times
//
void PiFilter_UpdateAirTime( void )
{
   const AI_BLOCK  * pAiBlock;
   const AI_NETWOP * pNetwop;
   const char  * pTmpStr;
   char  strbuf[16+2+1];
   uint  startMoD, stopMoD;
   uint  netwop;

   EpgDbLockDatabase(dbc, TRUE);
   pAiBlock = EpgDbGetAi(dbc);
   if (pAiBlock != NULL)
   {
      EpgDbFilterInitAirTimesFilter(pPiFilterContext, pAiBlock->netwopCount);
      EpgDbPreFilterEnable(pPiFilterContext, FILTER_AIR_TIMES);

      pNetwop = AI_GET_NETWOPS(pAiBlock);
      for (netwop=0; netwop < pAiBlock->netwopCount; netwop++, pNetwop++)
      {
         sprintf(strbuf, "0x%04X", AI_GET_NET_CNI(pNetwop));
         pTmpStr = Tcl_GetVar2(interp, "cfnettimes", strbuf, TCL_GLOBAL_ONLY);
         if (pTmpStr != NULL)
         {
            if (sscanf(pTmpStr, "%u,%u", &startMoD, &stopMoD) == 2)
            {
               assert((startMoD < 24*60) && (stopMoD < 24*60));

               if (startMoD != stopMoD)
                  EpgDbFilterSetAirTimesFilter(pPiFilterContext, netwop, startMoD, stopMoD);
            }
            else
               debug2("PiFilter-UpdateAirTime: cannot parse time config for CNI 0x%04X: '%s'", AI_GET_NET_CNI(pNetwop), pTmpStr);
         }
      }
   }
   EpgDbLockDatabase(dbc, FALSE);
}

// ----------------------------------------------------------------------------
// Add air-times prefilter to the current filter context
// - required for contexts which are cached for reminders; not required for
//   browser or shortcut cache because air-times are always part of the GUI
//   filter context
//
static int PiFilter_SelectAirTimes( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiFilter_SelectAirTimes";
   int   result;

   if (objc != 1)
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      PiFilter_UpdateAirTime();

      result = TCL_OK; 
   }
   return result;
}

// ----------------------------------------------------------------------------
// Set netwop prefilters and update network listbox
// - called by GUI after change of user network selection (netsel popup)
//
static int UpdateNetwopList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   UiControl_AiStateChange(NULL);

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Append to the context menu: "Undo Filters" section
// - returns the number of added menu entries
//
static int PiFilter_ContextMenuUndoFilter( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiFilter_ContextMenuUndoFilter";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   const char * pThemeStr;
   const char * pCfNetname;
   Tcl_Obj * pResultList;
   bool  isFromAi;
   int   entryCount;
   int   result;

   if (objc != 1)
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);

      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         // query listbox for user-selected PI, if any
         pPiBlock = PiBox_GetSelectedPi();

         // get user-configured name for that network
         if (pPiBlock != NULL)
         {
            pCfNetname = EpgSetup_GetNetName(pAiBlock, pPiBlock->netwop_no, &isFromAi);
         }
         else
            pCfNetname = NULL;

         // undo substring filter
         if ( (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_SUBSTR)) &&
              (pPiFilterContext->act.pSubStrCtx != NULL) )
         {
            if (pPiFilterContext->act.pSubStrCtx->pNext == NULL)
               snprintf(comm, TCL_COMM_BUF_SIZE, "Undo text filter '%.80s'", pPiFilterContext->act.pSubStrCtx->str);
            else
               sprintf(comm, "Undo text filters");

            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("ResetSubstr; SubstrUpdateFilter 0", -1));
         }

         // undo netwop filter
         if (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_NETWOP))
         {
            // check if more than one network is currently selected
            uint count = 0;
            if (pPiBlock != NULL)
            {
               for (uint netwop = 0; netwop < pAiBlock->netwopCount; netwop++)
                  if ( (pPiFilterContext->act.pNetwopFilterField[netwop]) &&
                       (netwop != pPiBlock->netwop_no) )
                     count += 1;
            }
            if (count > 1)
            {  // more than one network -> offer to remove only the selected one
               assert(pPiBlock != NULL);  // count only > 0 if PI selected
               snprintf(comm, TCL_COMM_BUF_SIZE, "Remove network '%.80s'", pCfNetname);
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));

               sprintf(comm, "SelectNetwopByIdx %d 0", pPiBlock->netwop_no);
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));

               // offer to remove all network filters
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo all network filters", -1));
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("C_SelectNetwops {}; ResetNetwops; C_PiBox_Refresh; CheckShortcutDeselection", -1));

               // increase count only by one, since both entries have the same type
            }
            else
            {  // just one network selected -> offer to remove it
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo network filter", -1));
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("C_SelectNetwops {}; ResetNetwops; C_PiBox_Refresh; CheckShortcutDeselection", -1));
            }
         }

         // undo editorial rating
         if (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_EDIT_RAT))
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo editorial rating filter", -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("set editorial_rating 0; SelectEditorialRating", -1));
         }

         // undo parental rating
         if (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_PAR_RAT))
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo parental rating filter", -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("set parental_rating 0; SelectParentalRating", -1));
         }

         // undo feature filters
         if (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_FEATURES))
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo feature filters", -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("C_SelectFeatures {}; ResetFeatures; C_PiBox_Refresh; CheckShortcutDeselection", -1));
         }

         // undo progidx
         if (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_PROGIDX))
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo program index filter", -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("set filter_progidx 0; SelectProgIdx -1 -1", -1));
         }

         // undo start time filter
         if ( EpgDbFilterIsEnabled(pPiFilterContext, FILTER_TIME_ALL) )
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo start time filter", -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("set timsel_enabled 0; SelectTimeFilter", -1));
         }

         // undo duration filter
         if ( EpgDbFilterIsEnabled(pPiFilterContext, FILTER_DURATION) )
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo duration filter", -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("set dursel_min 0; set dursel_max 0; SelectDurationFilter", -1));
         }

         // undo VPS filter
         if ( EpgDbFilterIsEnabled(pPiFilterContext, FILTER_VPS_PDC) )
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo VPS/PDC filter", -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("set vpspdc_filt 0; SelectVpsPdcFilter", -1));
         }

         // undo themes filters
         if (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_THEMES))
         {
            uint theme = 0;
            uint count = 0;

            // check if more than one theme filter is set
            for (uint idx = 0; idx <= 0x80; idx++)
            {
               if (pPiFilterContext->act.themeFilterField[idx] != 0)
               {
                  theme = idx;
                  count += 1;
               }
            }

            if (count == 1)
            {
               comm[0] = 0;
               for (uint tclass = 0; tclass < THEME_CLASS_COUNT; tclass++)
               {
                  // only offer to undo this theme if it is in exactly one class
                  if (pPiFilterContext->act.themeFilterField[theme] & (1 << tclass))
                  {
                     sprintf(comm, "C_SelectThemes %d {};"
                                   "ResetThemes; C_PiBox_Refresh; CheckShortcutDeselection",
                                   tclass + 1);
                     break;
                  }
               }
               if (comm[0] != 0)
               {
                  Tcl_Obj * pCmdObj = Tcl_NewStringObj(comm, -1);

                  pThemeStr = PdcThemeGet(theme);
                  if (pThemeStr != NULL)
                     snprintf(comm, TCL_COMM_BUF_SIZE, "Undo themes filter '%.80s'", pThemeStr);
                  else
                     sprintf(comm, "Undo themes filter");
                  Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));

                  Tcl_ListObjAppendElement(interp, pResultList, pCmdObj);
               }
            }
            else if (count > 1)
            {
               for (uint idx = 0; idx <= 128; idx++)
               {
                  if ( ((pPiFilterContext->act.themeFilterField[idx] & pPiFilterContext->act.usedThemeClasses) != 0) &&
                       ((pThemeStr = PdcThemeGet(idx)) != NULL) )
                  {
                     comm[0] = 0;
                     for (uint tclass=0; tclass < THEME_CLASS_COUNT; tclass++)
                     {
                        // only offer to undo this theme if it is in exactly one class
                        if (pPiFilterContext->act.themeFilterField[idx] == (1 << tclass))
                        {
                           sprintf(comm + strlen(comm), "DeselectTheme %d %d;", tclass + 1, idx);
                           break;
                        }
                     }
                     if (comm[0] != 0)
                     {
                        Tcl_Obj * pCmdObj = Tcl_NewStringObj(comm, -1);

                        snprintf(comm, TCL_COMM_BUF_SIZE, "Undo themes filter '%.80s'", pThemeStr);
                        Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));

                        Tcl_ListObjAppendElement(interp, pResultList, pCmdObj);
                     }
                  }
               }
            }
         }

         // undo global invert
         if (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_INVERT))
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Undo global inversion", -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("ResetGlobalInvert; InvertFilter", -1));
         }

         Tcl_ListObjLength(interp, pResultList, &entryCount);
         if (entryCount > 1)
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Reset all filters", -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("ResetFilterState; C_PiBox_Refresh", -1));
         }
      }
      EpgDbLockDatabase(dbc, FALSE);

      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Append to the context menu: "Add Filters" section
// - returns the number of added menu entries
//
static int PiFilter_ContextMenuAddFilter( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiFilter_ContextMenuAddFilter";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   const char * pThemeStr;
   const char * pCfNetname;
   bool  isFromAi;
   uint  idx;
   uchar theme, themeCat;
   uint netwop;
   Tcl_Obj * pResultList;
   int   result;

   if (objc != 1)
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);

      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         // query listbox for user-selected PI, if any
         pPiBlock = PiBox_GetSelectedPi();

         if (pPiBlock != NULL)
         {
            // get user-configured name for that network
            pCfNetname = EpgSetup_GetNetName(pAiBlock, pPiBlock->netwop_no, &isFromAi);

            // substring filter
            if (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_SUBSTR) == FALSE)
            {
               Tcl_DString dstr;
               const char * pTitle;
               char subStr[256];

               pTitle = PiDescription_RemoveSeriesIndex(PI_GET_TITLE(pPiBlock), subStr, sizeof(subStr));
               if (pTitle[0] != 0)
               {
                  bool isShortened = (pTitle != PI_GET_TITLE(pPiBlock));

                  EpgDbFilterSetSubStr(pPiFilterContext, pTitle, TRUE, FALSE, TRUE, !isShortened);
                  EpgDbFilterEnable(pPiFilterContext, FILTER_SUBSTR);
                  if ( (EpgDbSearchPrevPi(pUiDbContext, pPiFilterContext, pPiBlock) != NULL) ||
                       (EpgDbSearchNextPi(pUiDbContext, pPiFilterContext, pPiBlock) != NULL) )
                  {
                     snprintf(comm, TCL_COMM_BUF_SIZE, "Filter title '%.80s'", pTitle);
                     Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));

                     Tcl_DStringInit(&dstr);
                     Tcl_DStringAppend(&dstr, "SubstrSetFilter", -1);
                     Tcl_DStringStartSublist(&dstr);
                     Tcl_DStringAppendElement(&dstr, pTitle); // escapes '{' characters

                     sprintf(comm, " 1 0 1 %d", !isShortened);
                     Tcl_DStringAppend(&dstr, comm, -1);
                     Tcl_DStringEndSublist(&dstr);

                     Tcl_ListObjAppendElement(interp, pResultList,
                        Tcl_NewStringObj(Tcl_DStringValue(&dstr), Tcl_DStringLength(&dstr)));
                  }
                  EpgDbFilterDisable(pPiFilterContext, FILTER_SUBSTR);
               }
            }

            // themes filter
            for (idx=0; idx < pPiBlock->no_themes; idx++)
            {
               theme = pPiBlock->themes[idx];
               if (theme <= 128)
               {
                  pThemeStr = PdcThemeGet(theme);
                  if ( (pThemeStr != NULL) &&
                       ( (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_THEMES) == FALSE) ||
                         (pPiFilterContext->act.themeFilterField[theme] == FALSE) ) )
                  {
                     themeCat  = PdcThemeGetCategory(theme);
                     if ( EpgDbFilterIsEnabled(pPiFilterContext, FILTER_THEMES) &&
                          (themeCat != theme) &&
                          (pPiFilterContext->act.themeFilterField[themeCat] != FALSE) )
                     {  // special case: undo general theme before sub-theme is enabled, else filter would have no effect (due to OR)
                        snprintf(comm, TCL_COMM_BUF_SIZE, "Filter theme '%.80s'", pThemeStr);
                        Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));

                        sprintf(comm, "set theme_sel(%d) 0; set theme_sel(%d) 1; SelectTheme %d",
                                      themeCat, theme, theme);
                        Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));
                     }
                     else
                     {
                        snprintf(comm, TCL_COMM_BUF_SIZE, "Filter theme '%.80s'", pThemeStr);
                        Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));

                        sprintf(comm, "set theme_sel(%d) 1; SelectTheme %d", theme, theme);
                        Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));
                     }
                  }
               }
            }

            // feature/repeat filter - only offered in addition to title filter
            if ( EpgDbFilterIsEnabled(pPiFilterContext, FILTER_SUBSTR) )
            {
               if ( (EpgDbFilterIsEnabled(pPiFilterContext, FILTER_FEATURES) == FALSE) ||
                    (pPiFilterContext->act.featureFilterCount == 1) )
               {
                  bool wasEnabled = EpgDbFilterIsEnabled(pPiFilterContext, FILTER_FEATURES);
                  uint oldMask    = pPiFilterContext->act.featureFilterMaskField[0];
                  uint oldFlags   = pPiFilterContext->act.featureFilterFlagField[0];

                  if (wasEnabled == FALSE)
                  {
                     EpgDbFilterEnable(pPiFilterContext, FILTER_FEATURES);
                     EpgDbFilterSetNoFeatures(pPiFilterContext, 1);
                     oldMask = oldFlags = 0;
                  }
                  EpgDbFilterSetFeatureFlags(pPiFilterContext, 0, oldFlags & ~0x80, oldMask | 0x80);
                  if (EpgDbSearchFirstPi(pUiDbContext, pPiFilterContext) != NULL)
                  {
                     EpgDbFilterSetFeatureFlags(pPiFilterContext, 0, oldFlags | 0x80, oldMask | 0x80);
                     if (EpgDbSearchFirstPi(pUiDbContext, pPiFilterContext) != NULL)
                     {
                        if ( (wasEnabled == FALSE) ||
                             ((oldMask & 0x80) == 0) ||
                             ((oldFlags & 0x80) == 0x80) )
                        {
                           Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Filter for original transmissions", -1));
                           Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("SelectFeaturesAllClasses 0xc0 0 0", -1));
                        }
                        if ( (wasEnabled == FALSE) ||
                             ((oldMask & 0x80) == 0) ||
                             ((oldFlags & 0x80) == 0) )
                        {
                           Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("Filter for repeats", -1));
                           Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("SelectFeaturesAllClasses 0x80 0x80 0x40", -1));
                        }
                     }
                  }
                  if (wasEnabled == FALSE)
                     EpgDbFilterDisable(pPiFilterContext, FILTER_FEATURES);
                  else
                     EpgDbFilterSetFeatureFlags(pPiFilterContext, 0, oldFlags, oldMask);
               }
            }

            // netwop filter
            if ( EpgDbFilterIsEnabled(pPiFilterContext, FILTER_NETWOP) == FALSE )
            {  // no network filter yet -> offer to filter for the currently selected
               snprintf(comm, TCL_COMM_BUF_SIZE, "Filter network '%.80s'", pCfNetname);
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));

               sprintf(comm, "SelectNetwopByIdx %d 1", pPiBlock->netwop_no);
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));
            }
            else
            {  // check if there's more than one network selected -> offer to reduce filtering to one
               for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
                  if ( (pPiFilterContext->act.pNetwopFilterField[netwop]) &&
                       (netwop != pPiBlock->netwop_no) )
                     break;
               if (netwop < pAiBlock->netwopCount)
               {
                  snprintf(comm, TCL_COMM_BUF_SIZE, "Filter only network '%.80s'", pCfNetname);
                  Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));

                  sprintf(comm, "ResetNetwops; SelectNetwopByIdx %d 1", pPiBlock->netwop_no);
                  Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(comm, -1));
               }
            }
         }
      }
      EpgDbLockDatabase(dbc, FALSE);

      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Suppress expired PI blocks in the listbox
// - a PI is considered expired, when it's stop time is less or equal the
//   expire time, which is set here every minute. We must not tie this to the
//   current time, because then PI do expire asynchronously with the GUI,
//   so that the tight sync between PI listbox and DB would break.
// - must be called every minute, or SearchFirst will not work,
//   i.e. return expired PI blocks
//
void PiFilter_Expire( void )
{
   Tcl_Obj * disableVar;
   Tcl_Obj * cutOffVar;
   int    expireNever;
   int    cutOffTime;
   time_t expireTime;

   disableVar = Tcl_GetVar2Ex(interp, "piexpire_never", NULL, TCL_GLOBAL_ONLY);
   if ( (disableVar == NULL) ||
        (Tcl_GetBooleanFromObj(NULL, disableVar, &expireNever) != TCL_OK) )
   {
      expireNever = 0;
   }

   cutOffVar = Tcl_GetVar2Ex(interp, "piexpire_display", NULL, TCL_GLOBAL_ONLY);
   if ( (cutOffVar == NULL) ||
        (Tcl_GetIntFromObj(NULL, cutOffVar, &cutOffTime) != TCL_OK) )
   {
      cutOffTime = 0;
   }

   expireTime = EpgGetUiMinuteTime() - (cutOffTime * 60);

   EpgDbFilterSetExpireTime(pPiFilterContext, expireTime);

   if (expireNever == 0)
      EpgDbPreFilterEnable(pPiFilterContext, FILTER_EXPIRE_TIME);
   else
      EpgDbPreFilterDisable(pPiFilterContext, FILTER_EXPIRE_TIME);

   // Remove error overlay in case there are PI to display now
   UiControl_CheckDbState(NULL);
}

// ----------------------------------------------------------------------------
// initialize the filter menus
// - this should be called only once during start-up
//
void PiFilter_Create( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_SelectThemes", &cmdInfo) == 0)
   {
      Tcl_CreateObjCommand(interp, "C_SelectThemes", SelectThemes, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectNetwops", SelectNetwops, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectFeatures", SelectFeatures, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectParentalRating", SelectParentalRating, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectEditorialRating", SelectEditorialRating, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectSubStr", SelectSubStr, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectProgIdx", SelectProgIdx, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectStartTime", SelectStartTime, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectMinMaxDuration", SelectMinMaxDuration, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectExpiredPiDisplay", SelectExpiredPiDisplay, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SelectVpsPdcFilter", SelectVpsPdcFilter, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_ResetFilter", PiFilter_Reset, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_InvertFilter", PiFilter_Invert, (ClientData) NULL, NULL);

      Tcl_CreateObjCommand(interp, "C_PiFilter_ForkContext", PiFilter_ForkContext, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiFilter_ContextCacheCtl", PiFilter_ContextCacheCtl, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiFilter_SelectAirTimes", PiFilter_SelectAirTimes, (ClientData) NULL, NULL);

      Tcl_CreateObjCommand(interp, "C_GetPdcString", GetPdcString, (ClientData) NULL, NULL);
      //Tcl_CreateObjCommand(interp, "C_GetNetwopsWithSeries", GetNetwopsWithSeries, (ClientData) NULL, NULL);
      //Tcl_CreateObjCommand(interp, "C_GetNetwopSeriesList", GetNetwopSeriesList, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetSeriesByLetter", GetSeriesByLetter, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetLastPiTime", GetLastPiTime, (ClientData) NULL, NULL);

      Tcl_CreateObjCommand(interp, "C_UpdateNetwopList", UpdateNetwopList, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetNetwopFilterList", GetNetwopFilterList, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_EnableExpirePreFilter", EnableExpirePreFilter, (ClientData) NULL, NULL);

      Tcl_CreateObjCommand(interp, "C_PiFilter_ContextMenuUndoFilter", PiFilter_ContextMenuUndoFilter, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_PiFilter_ContextMenuAddFilter", PiFilter_ContextMenuAddFilter, (ClientData) NULL, NULL);

      sprintf(comm, "GenerateFilterMenues %d %d\n", THEME_CLASS_COUNT, FEATURE_CLASS_COUNT);
      eval_check(interp, comm);
   }
   else
      debug0("PiFilter-Create: commands are already created");

   // create and initialize the filter context
   pUiFilterContext = EpgDbFilterCreateContext();
   pPiFilterContext = pUiFilterContext;
   // initialize the expire time filter
   PiFilter_Expire();

   sprintf(comm, "ResetFilterState\n");
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// destroy the filter menus
//
void PiFilter_Destroy( void )
{
   PiFilter_ContextCacheClear();
   EpgDbFilterDestroyContext(pUiFilterContext);
}

