/*
 *  Nextview GUI: PI search filter control
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: pifilter.c,v 1.54 2002/05/12 16:53:20 tom Exp tom $
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
#include "epgui/pdc_themes.h"
#include "epgui/pilistbox.h"
#include "epgui/pioutput.h"
#include "epgui/pifilter.h"
#include "epgui/uictrl.h"


// this is the filter context, which contains all filter settings
// for the PiListbox module
FILTER_CONTEXT *pPiFilterContext = NULL;

#define dbc pUiDbContext         // internal shortcut

// ----------------------------------------------------------------------------
// Update the themes filter setting
// - special value 0x80 is extended to cover all series codes 0x80...0xff
//
static int SelectThemes( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectThemes <class 1..8> <themes-list>";
   uchar usedClasses;
   int class, theme, idx;
   int result; 
   
   if ( (argc != 3)  || Tcl_GetInt(interp, argv[1], &class) ||
        (class == 0) || ((uint)class > THEME_CLASS_COUNT) )
   {  // illegal parameter count, format or value
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_SplitList(interp, argv[2], &argc, &argv);
      if (result == TCL_OK)
      {
         class = 1 << (class - 1);
         usedClasses = EpgDbFilterInitThemes(pPiFilterContext, class);

         for (idx=0; idx < argc; idx++)
         {
            result = Tcl_GetInt(interp, argv[idx], &theme);
            if (result == TCL_OK)
            {
               if (theme == 0x80)
                  EpgDbFilterSetThemes(pPiFilterContext, 0x80, 0xff, class);
               else
                  EpgDbFilterSetThemes(pPiFilterContext, theme, theme, class);
            }
            else
               break;
         }

         if ((argc > 0) || usedClasses)
            EpgDbFilterEnable(pPiFilterContext, FILTER_THEMES);
         else
            EpgDbFilterDisable(pPiFilterContext, FILTER_THEMES);

         Tcl_Free((char *) argv);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Callback for sorting criteria
//
static int SelectSortCrits( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectSortCrits <class 1..8> <list>";
   char **pSortCritArgv;
   uchar usedClasses;
   int class, sortcrit, sortcritCount, idx;
   int result; 
   
   if ( (argc != 3)  || Tcl_GetInt(interp, argv[1], &class) ||
        (class == 0) || ((uint)class > THEME_CLASS_COUNT) )
   {  // illegal parameter count, format or value
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_SplitList(interp, argv[2], &sortcritCount, &pSortCritArgv);
      if (result == TCL_OK)
      {
         class = 1 << (class - 1);
         usedClasses = EpgDbFilterInitSortCrit(pPiFilterContext, class);

         for (idx=0; idx < sortcritCount; idx++)
         {
            result = Tcl_GetInt(interp, pSortCritArgv[idx], &sortcrit);
            if (result == TCL_OK)
            {
               EpgDbFilterSetSortCrit(pPiFilterContext, sortcrit, sortcrit, class);
            }
            else
               break;
         }

         if ((sortcritCount > 0) || usedClasses)
            EpgDbFilterEnable(pPiFilterContext, FILTER_SORTCRIT);
         else
            EpgDbFilterDisable(pPiFilterContext, FILTER_SORTCRIT);

         Tcl_Free((char *) pSortCritArgv);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for series checkbuttons
//
static int SelectSeries( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectSeries <series-code> <enable=0/1>";
   int series, enable;
   int result; 
   
   if (argc != 3)
   {  // illegal parameter count (the themes must be passed as list, not separate items)
      interp->result = (char *) pUsage;
      result = TCL_ERROR; 
   }  
   else if ( (Tcl_GetInt(interp, argv[1], &series) != TCL_OK) ||
             (Tcl_GetBoolean(interp, argv[2], &enable) != TCL_OK) )
   {  // illegal parameter format
      result = TCL_ERROR; 
   }
   else
   {
      result = TCL_OK; 

      if ((pPiFilterContext->enabledFilters & FILTER_SERIES) == FALSE)
      {
         EpgDbFilterInitSeries(pPiFilterContext);
         EpgDbFilterEnable(pPiFilterContext, FILTER_SERIES);
      }
      EpgDbFilterSetSeries(pPiFilterContext, series >> 8, series & 0xff, enable);
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for netwop listbox: update the netwop filter setting
//
static int SelectNetwops( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectNetwops <netwop-list>";
   int netwop, idx;
   int result; 
   
   if (argc != 2) 
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_SplitList(interp, argv[1], &argc, &argv);
      if (result == TCL_OK)
      {
         if (argc > 0)
         {
            EpgDbFilterInitNetwop(pPiFilterContext);
            for (idx=0; idx < argc; idx++)
            {
               result = Tcl_GetInt(interp, argv[idx], &netwop);
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
         {  // no netwops selected -> disable filter
            EpgDbFilterDisable(pPiFilterContext, FILTER_NETWOP);
         }

         Tcl_Free((char *) argv);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for features menu: update the feature filter setting
//
static int SelectFeatures( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectFeatures (<mask> <value>){0..6}";
   int class, mask, value;
   char *p, *n;
   int result; 
   
   if (argc != 2)
   {  // illegal parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = TCL_OK; 
      class = 0;
      p = argv[1];
      while(class < FEATURE_CLASS_COUNT)
      {
         mask = strtol(p, &n, 0);
         if (p == n)
            break;
         value = strtol(n, &p, 0);
         if (n == p)
            break;

         EpgDbFilterSetFeatureFlags(pPiFilterContext, class, value, mask);
         class += 1;
      }

      if (class > 0)
      {
         EpgDbFilterSetNoFeatures(pPiFilterContext, class);
         EpgDbFilterEnable(pPiFilterContext, FILTER_FEATURES);
      }
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_FEATURES);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Update the editorial rating filter setting
//
static int SelectParentalRating( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectParentalRating <value>";
   int rating;
   int result; 
   
   if ( (argc != 2)  || Tcl_GetInt(interp, argv[1], &rating) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      if (rating != 0)
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
// Update the parental rating filter setting
//
static int SelectEditorialRating( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectEditorialRating <value>";
   int rating;
   int result; 
   
   if ( (argc != 2)  || Tcl_GetInt(interp, argv[1], &rating) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      if (rating != 0)
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
static int SelectSubStr( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectSubStr <bTitle=0/1> <bDescr=0/1> <bCase=0/1> <bFull=0/1> <substring>";
   int scope_title, scope_descr, match_case, match_full;
   char *native;
   Tcl_DString ds;
   int result; 
   
   if (argc != 6) 
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if ( (Tcl_GetBoolean(interp, argv[1], &scope_title) != TCL_OK) ||
             (Tcl_GetBoolean(interp, argv[2], &scope_descr) != TCL_OK) ||
             (Tcl_GetBoolean(interp, argv[3], &match_case)  != TCL_OK) ||
             (Tcl_GetBoolean(interp, argv[4], &match_full)  != TCL_OK) )
   {  // one parameter is not a boolean
      interp->result = (char *) pUsage;
      result = TCL_ERROR; 
   }
   else
   {
      EpgDbFilterDisable(pPiFilterContext, FILTER_SUBSTR_TITLE|FILTER_SUBSTR_DESCR);

      if ((argv[5] != NULL) && (argv[5][0] != 0))
      {
         if (scope_title)
            EpgDbFilterEnable(pPiFilterContext, FILTER_SUBSTR_TITLE);
         if (scope_descr)
            EpgDbFilterEnable(pPiFilterContext, FILTER_SUBSTR_DESCR);

         // convert the String from Tcl internal format to Latin-1
         native = Tcl_UtfToExternalDString(NULL, argv[5], -1, &ds);

         EpgDbFilterSetSubStr(pPiFilterContext, native, match_case, match_full);
         Tcl_DStringFree(&ds);
      }

      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for time selection popup
//
static int SelectStartTime( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectStartTime <start-rel:0/1> <stop-midnight:0/1> "
                               "<start time> <stop time> <rel.date>";
   int isRelStart, isAbsStop;
   int startTime, stopTime, relDate;
   NI_FILTER_STATE nifs;
   int result; 
   
   if (argc == 1) 
   {  // no parameters -> disabled filter
      EpgDbFilterDisable(pPiFilterContext, FILTER_TIME_BEG | FILTER_TIME_END);
      result = TCL_OK; 
   }
   else if (argc != 6) 
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if ( Tcl_GetBoolean(interp, argv[1], &isRelStart) ||
             Tcl_GetBoolean(interp, argv[2], &isAbsStop) ||
             Tcl_GetInt(interp, argv[3], &startTime) || ((uint)startTime > 23*60+59) ||
             Tcl_GetInt(interp, argv[4], &stopTime)  || ((uint)stopTime > 23*60+59) ||
             Tcl_GetInt(interp, argv[5], &relDate) )
   {  // one parameter has an invalid value
      interp->result = (char *) pUsage;
      result = TCL_ERROR; 
   }
   else
   {
      nifs.flags = NI_DATE_RELDATE | NI_DATE_START;
      nifs.reldate = relDate;
      if (isRelStart == FALSE)
         nifs.startMoD = startTime;
      else
         nifs.startMoD = 0xffff;
      if (isAbsStop == FALSE)
      {
         nifs.flags |= NI_DATE_STOP;
         nifs.stopMoD = stopTime;
      }
      EpgDbFilterFinishNi(pPiFilterContext, &nifs);

      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Callback for Program-Index radio buttons
//
static int SelectProgIdx( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_SelectProgIdx <first-index> <last-index>";
   int first, last;
   int result; 
   
   if ( (argc == 3)  &&
        !Tcl_GetInt(interp, argv[1], &first) &&
        !Tcl_GetInt(interp, argv[2], &last) )
   {  // set min and max index boundaries
      EpgDbFilterEnable(pPiFilterContext, FILTER_PROGIDX);
      EpgDbFilterSetProgIdx(pPiFilterContext, first, last);
      result = TCL_OK; 
   }
   else if (argc == 1) 
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
static int PiFilter_Reset( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_ResetFilter {mask-list}";
   int  mask;
   char *p;
   int  result; 
   
   if (argc != 2)
   {  // illegal parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      p = argv[1];
      mask = 0;
      while (p != NULL)
      {
         while (*p == ' ')
            p++;
         if (*p == 0)
            break;

              if (!strncmp(p, "all", 3))       mask |= FILTER_ALL & ~FILTER_PERM;
         else if (!strncmp(p, "netwops", 7))   mask |= FILTER_NETWOP;
         else if (!strncmp(p, "themes", 6))    mask |= FILTER_THEMES;
         else if (!strncmp(p, "sortcrits", 9)) mask |= FILTER_SORTCRIT;
         else if (!strncmp(p, "series", 6))    mask |= FILTER_SERIES;
         else if (!strncmp(p, "substr", 6))    mask |= FILTER_SUBSTR_TITLE | FILTER_SUBSTR_DESCR;
         else if (!strncmp(p, "progidx", 7))   mask |= FILTER_PROGIDX;
         else if (!strncmp(p, "timsel", 6))    mask |= FILTER_TIME_BEG | FILTER_TIME_END;
         else if (!strncmp(p, "parental", 8))  mask |= FILTER_PAR_RAT;
         else if (!strncmp(p, "editorial", 9)) mask |= FILTER_EDIT_RAT;
         else if (!strncmp(p, "features", 8))  mask |= FILTER_FEATURES;
         else if (!strncmp(p, "languages", 9)) mask |= FILTER_LANGUAGES;
         else if (!strncmp(p, "subtitles", 9)) mask |= FILTER_SUBTITLES;
         else debug1("PiFilter-Reset: unknown keyword at %s", p);

         p = strchr(p, ' ');
      }
      EpgDbFilterDisable(pPiFilterContext, mask);

      result = TCL_OK; 
   }

   return result; 
}

// ----------------------------------------------------------------------------
// return the name according to a PDC theme index
//
static int GetPdcString( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: C_GetPdcString <index>";
   const char * name;
   int index;
   int result; 
   
   if ( (argc != 2) || Tcl_GetInt(interp, argv[1], &index) )
   {  // wrong parameter count or no integer parameter
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      if (index <= 0x80)
         name = pdc_themes[index];
      else
         name = pdc_series;

      Tcl_SetResult(interp, (char *) name, TCL_STATIC);
      result = TCL_OK; 
   }
   return result;
}

// ----------------------------------------------------------------------------
// Return a list of CNIs of all networks that have PI of theme "series"
//
static int GetNetwopsWithSeries( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GetNetwopsWithSeries";
   const AI_BLOCK *pAiBlock;
   FILTER_CONTEXT *fc;
   int netwop;
   int result;

   if (argc != 1) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = TCL_OK; 

      EpgDbLockDatabase(dbc, TRUE);
      fc = EpgDbFilterCreateContext();
      EpgDbFilterEnable(fc, FILTER_NETWOP | FILTER_THEMES);
      EpgDbFilterInitThemes(fc, 0xff);
      EpgDbFilterSetThemes(fc, 0x81, 0xff, 1);

      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
         {
            EpgDbFilterInitNetwop(fc);
            EpgDbFilterSetNetwop(fc, netwop);
            if (EpgDbSearchFirstPi(dbc, fc) != NULL)
            {
               // append the CNI in the format "0x0D94" to the TCL result list
               sprintf(comm, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);
               Tcl_AppendElement(interp, comm);
            }
         }
      }
      EpgDbFilterDestroyContext(fc);
      EpgDbLockDatabase(dbc, FALSE);
   }  

   return result;
}

// ----------------------------------------------------------------------------
// Get a list of all series codes and titles on a given network
// - returns a 'paired list': series code followed by title
//
static int GetNetwopSeriesList( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GetNetwopSeriesList <cni>";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   const char     * pTitle;
   bool usedSeries[0x80];
   FILTER_CONTEXT *fc;
   uchar netwop, series, lang;
   uint index, cni;
   int result;

   if (argc != 2)
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if (Tcl_GetInt(interp, argv[1], &cni) != TCL_OK)
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
            if (cni == AI_GET_NETWOP_N(pAiBlock, netwop)->cni)
               break;

         if (netwop < pAiBlock->netwopCount)
         {
            lang = AI_GET_NETWOP_N(pAiBlock, netwop)->alphabet;

            memset(usedSeries, FALSE, sizeof(usedSeries));

            fc = EpgDbFilterCreateContext();
            EpgDbFilterEnable(fc, FILTER_NETWOP | FILTER_THEMES);
            EpgDbFilterInitNetwop(fc);
            EpgDbFilterSetNetwop(fc, netwop);
            EpgDbFilterInitThemes(fc, 0xff);
            EpgDbFilterSetThemes(fc, 0x81, 0xff, 1);

            pPiBlock = EpgDbSearchFirstPi(dbc, fc);
            if (pPiBlock != NULL)
            {
               do
               {
                  series = 0;
                  for (index=0; index < pPiBlock->no_themes; index++)
                  {
                     series = pPiBlock->themes[index];
                     if ((series > 0x80) && (usedSeries[series - 0x80] == FALSE))
                     {
                        usedSeries[series - 0x80] = TRUE;

                        sprintf(comm, "%d", (netwop << 8) | series);
                        Tcl_AppendElement(interp, comm);

                        pTitle = PiOutput_DictifyTitle(PI_GET_TITLE(pPiBlock), lang, comm, sizeof(comm));
                        Tcl_AppendElement(interp, pTitle);
                        //printf("%s 0x%02x - %s\n", netname, series, PI_GET_TITLE(pPiBlock));
                        break;
                     }
                  }
                  pPiBlock = EpgDbSearchNextPi(dbc, fc, pPiBlock);
               }
               while (pPiBlock != NULL);
            }
            EpgDbFilterDestroyContext(fc);
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

// ----------------------------------------------------------------------------
// Get a list of all series titles starting with a given letter
// - uses the letter which is in the first position after moving language dependent
//   attributes like 'Der', 'Die', 'Das' etc. to the end ('dictified' titles)
// - appends the network name to the series title; this is helpful for the user
//   if the same series is broadcast by multiple networks
// - returns a 'paired list': network number followed by title
//
static int GetSeriesByLetter( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GetSeriesByLetter <letter>";
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK * pPiBlock;
   const char * pTitle;
   char numbuf[20];
   FILTER_CONTEXT *fc;
   uchar lang, series, letter, c;
   uint index;
   int result;

   if (argc != 2)
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
         // copy the pre-filters from the current filter context
         // to exclude series from suppressed networks
         fc = EpgDbFilterCopyContext(pPiFilterContext);
         EpgDbFilterDisable(fc, FILTER_ALL & ~FILTER_PERM);

         EpgDbFilterEnable(fc, FILTER_THEMES);
         EpgDbFilterInitThemes(fc, 0xff);
         EpgDbFilterSetThemes(fc, 0x81, 0xff, 1);
         // get the starting letter from the parameters; "other" means non-alpha chars, e.g. digits
         if (strcmp(argv[1], "other") == 0)
            letter = 0;
         else
            letter = tolower(argv[1][0]);

         pPiBlock = EpgDbSearchFirstPi(dbc, fc);
         if (pPiBlock != NULL)
         {
            do
            {
               lang = AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->alphabet;
               pTitle = PiOutput_DictifyTitle(PI_GET_TITLE(pPiBlock), lang, comm, sizeof(comm));
               // check if the starting letter matches
               c = tolower(pTitle[0]);
               if ( (c == letter) ||
                    ((letter == 0) && ((c < 'a') || (c > 'z'))) )
               {
                  // search for a series code among the PDC themes
                  for (index=0; index < pPiBlock->no_themes; index++)
                  {
                     series = pPiBlock->themes[index];
                     if (series > 0x80)
                     {
                        sprintf(numbuf, "%d", (pPiBlock->netwop_no << 8) | series);
                        Tcl_AppendElement(interp, numbuf);
                        // append network name to series title
                        if (pTitle != comm)
                           strcpy(comm, pTitle);
                        sprintf(comm + strlen(comm), " (%s)", AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no));
                        Tcl_AppendElement(interp, comm);
                        break;
                     }
                  }
               }

               pPiBlock = EpgDbSearchNextPi(dbc, fc, pPiBlock);
            }
            while (pPiBlock != NULL);
         }
         EpgDbFilterDestroyContext(fc);
      }
      else
         debug0("Get-NetwopSeriesList: no AI in db");
      EpgDbLockDatabase(dbc, FALSE);
      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Retrieve list of titles for a list of series/netwop codes
// - used by the shortcut config menu to pretty-print the filter settings
// - returns a 'paired list': title followed by netwop name
//
static int GetSeriesTitles( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GetSeriesTitles <series-list>";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   const char     * pTitle;
   FILTER_CONTEXT *fc;
   char ** seriesArgv;
   uchar lang;
   int seriesCount, series, idx;
   int result;

   if (argc != 2) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_SplitList(interp, argv[1], &seriesCount, &seriesArgv);
      if (result == TCL_OK)
      {
         fc = EpgDbFilterCreateContext();
         EpgDbFilterInitSeries(fc);
         EpgDbFilterEnable(fc, FILTER_SERIES);
         EpgDbLockDatabase(dbc, TRUE);
         pAiBlock = EpgDbGetAi(dbc);
         if (pAiBlock != NULL)
         {
            // loop across all series passed as arguments
            for (idx=0; (idx < seriesCount) && (result == TCL_OK); idx++)
            {
               result = Tcl_GetInt(interp, seriesArgv[idx], &series);
               if ((result == TCL_OK) && ((series >> 8) < pAiBlock->netwopCount))
               {
                  // determine series title by searching a PI in the database
                  EpgDbFilterSetSeries(fc, series >> 8, series & 0xff, TRUE);
                  pPiBlock = EpgDbSearchFirstPi(dbc, fc);
                  if (pPiBlock != NULL)
                  {
                     lang = AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->alphabet;
                     pTitle = PiOutput_DictifyTitle(PI_GET_TITLE(pPiBlock), lang, comm, sizeof(comm));
                     Tcl_AppendElement(interp, pTitle);
                  }
                  else
                  {  // no PI of that series in the db (this will happen quite often
                     // unfortunately since most databases do not cover full 7 days)
                     sprintf(comm, "[0x%02X - no instance in database]", series & 0xff);
                     Tcl_AppendElement(interp, comm);
                  }
                  // append network name from AI block
                  Tcl_AppendElement(interp, AI_GET_NETWOP_NAME(pAiBlock, series >> 8));
                  EpgDbFilterSetSeries(fc, series >> 8, series & 0xff, FALSE);
               }
            }
         }

         EpgDbLockDatabase(dbc, FALSE);
         EpgDbFilterDestroyContext(fc);
         Tcl_Free((char *)seriesArgv);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Retrieve netwop filter setting for shortcut addition
// - returns CNIs, to be independant of AI netwop table ordering
//
static int GetNetwopFilterList( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GetNetwopFilterList";
   const AI_BLOCK *pAiBlock;
   uchar netwop;
   int result;

   if (argc != 1) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      if (pPiFilterContext->enabledFilters & FILTER_NETWOP)
      {
         EpgDbLockDatabase(dbc, TRUE);
         pAiBlock = EpgDbGetAi(dbc);
         if (pAiBlock != NULL)
         {
            for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
            {
               if (pPiFilterContext->netwopFilterField[netwop])
               {
                  sprintf(comm, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);
                  Tcl_AppendElement(interp, comm);
               }
            }
         }
         EpgDbLockDatabase(dbc, FALSE);
      }
      result = TCL_OK; 
   }
   return result;
}

// ----------------------------------------------------------------------------
// Update filter menu state
//
static void UpdateFilterContextMenuState( const NI_FILTER_STATE * pNiState )
{
   uchar netwop, series;
   uint index, class, classIdx;

   sprintf(comm, "ResetFilterState\n");
   eval_check(interp, comm);

   if (pPiFilterContext->enabledFilters & FILTER_NETWOP)
   {
      eval_check(interp, "set all {}");
      for (netwop=0; netwop < MAX_NETWOP_COUNT; netwop++)
      {
         if (pPiFilterContext->netwopFilterField[netwop])
         {
            sprintf(comm, "lappend all %d", netwop);
            eval_check(interp, comm);
         }
      }
      eval_check(interp, "UpdateNetwopMenuState $all");
   }

   // handled through pNiState
   //if (pPiFilterContext->enabledFilters & FILTER_TIME_BEG)
   //if (pPiFilterContext->enabledFilters & FILTER_TIME_END)

   if (pPiFilterContext->enabledFilters & FILTER_PAR_RAT)
   {
      sprintf(comm, "set parental_rating %d\n", pPiFilterContext->parentalRating);
      eval_check(interp, comm);
   }

   if (pPiFilterContext->enabledFilters & FILTER_EDIT_RAT)
   {
      sprintf(comm, "set editorial_rating %d\n", pPiFilterContext->editorialRating);
      eval_check(interp, comm);
   }

   if (pPiFilterContext->enabledFilters & FILTER_PROGIDX)
   {
      sprintf(comm, "set progidx_first %d; set progidx_last %d;"
                    "UpdateProgIdxMenuState\n",
                    pPiFilterContext->firstProgIdx, pPiFilterContext->lastProgIdx);
      eval_check(interp, comm);
   }

   if (pPiFilterContext->enabledFilters & FILTER_FEATURES)
   {
      for (index=0; index < pPiFilterContext->featureFilterCount; index++)
      {
         sprintf(comm, "set feature_class_mask(%d) %d; set feature_class_value(%d) %d\n",
                       index, pPiFilterContext->featureFilterMaskField[index],
                       index, pPiFilterContext->featureFilterFlagField[index]);
         eval_check(interp, comm);
      }
   }

   if (pPiFilterContext->enabledFilters & FILTER_SERIES)
   {
      for (netwop=0; netwop < MAX_NETWOP_COUNT; netwop++)
      {
         for (series=0; series < 128; series++)
         {
            if (pPiFilterContext->seriesFilterMatrix[netwop][series])
            {
               sprintf(comm, "set series_sel(%d) 1\n", (netwop << 8) | series);
               eval_check(interp, comm);
            }
         }
      }
   }

   if (pPiFilterContext->enabledFilters & FILTER_THEMES)
   {
      for (index=0; index <= 255; index++)
      {
         if (pPiFilterContext->themeFilterField[index])
         {
            for (classIdx=1, class=1; classIdx <= 8; classIdx++, class <<= 1)
            {
               if (pPiFilterContext->themeFilterField[index] & class)
               {
                  if (classIdx != 1)
                     sprintf(comm, "lappend theme_class_sel(%d) %d\n", classIdx, index);
                  else
                     sprintf(comm, "set theme_sel(%d) 1\n", index);
                  eval_check(interp, comm);
               }
            }
         }
      }
   }

   if (pPiFilterContext->enabledFilters & FILTER_SORTCRIT)
   {
      for (index=0; index <= 255; index++)
      {
         if (pPiFilterContext->sortCritFilterField[index])
         {
            for (classIdx=1, class=1; classIdx <= 8; classIdx++, class <<= 1)
            {
               if (pPiFilterContext->sortCritFilterField[index] & class)
               {
                  sprintf(comm, "lappend sortcrit_class_sel(%d) %d\n", classIdx, index);
                  eval_check(interp, comm);
               }
            }
         }
      }
      eval_check(interp, "UpdateSortCritListbox");
   }

   // currently unused by providers -> unsupported by me too
   //if (pPiFilterContext->enabledFilters & FILTER_LANGUAGES)
   //if (pPiFilterContext->enabledFilters & FILTER_SUBTITLES)

   if (pNiState->flags != NI_DATE_NONE)
   {
      time_t now  = time(NULL);
      sint lto    = EpgLtoGet(now);
      uint nowMoD = ((now + lto) % (60*60*24)) / 60;

      if ((pNiState->flags & NI_DATE_RELDATE) == 0)
         sprintf(comm, "set timsel_date 0\n");
      else
         sprintf(comm, "set timsel_date %d\n", pNiState->reldate);
      eval_check(interp, comm);

      if ((pNiState->flags & NI_DATE_START) == 0)
         sprintf(comm, "set timsel_start %d\nset timsel_relative 0\n", nowMoD);
      else if (pNiState->startMoD == 0xffff)
         sprintf(comm, "set timsel_start %d\nset timsel_relative 1\n", nowMoD);
      else
         sprintf(comm, "set timsel_start %d\nset timsel_relative 0\n", pNiState->startMoD);
      eval_check(interp, comm);

      if ((pNiState->flags & NI_DATE_STOP) == 0)
         sprintf(comm, "set timsel_stop %d\nset timsel_absstop 1\n", 23*60+59);
      else
         sprintf(comm, "set timsel_stop %d\nset timsel_absstop 0\n", pNiState->stopMoD);
      eval_check(interp, comm);

      eval_check(interp, "set timsel_enabled 1\n"
                         "if {$timsel_popup} SelectTimeFilterRelStart\n");
   }
}

// ----------------------------------------------------------------------------
// Check if any NI are in the database
//
static int IsNavigateMenuEmpty( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_IsNavigateMenuEmpty";
   bool isEmpty;
   int  result;

   if (argc != 1)
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      EpgDbLockDatabase(dbc, TRUE);
      isEmpty = (EpgDbGetNi(dbc, 1) == NULL);
      EpgDbLockDatabase(dbc, FALSE);

      Tcl_SetResult(interp, isEmpty ? "1" : "0", TCL_STATIC);
      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Create navigation menu
//
static int CreateNi( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_CreateNi <menu-path>";
   const NI_BLOCK *pNiBlock;
   const EVENT_ATTRIB *pEv;
   const char *evName;
   uchar *p, subname[100];
   int blockno, i;
   int result;

   if (argc != 2) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      EpgDbLockDatabase(dbc, TRUE);
      p = argv[1] + strlen(argv[1]);
      while (((char *)p > argv[1]) && isdigit(*(p-1)))
         p--;
      blockno = strtol(p, NULL, 10);

      pNiBlock = EpgDbGetNi(dbc, blockno);
      if (pNiBlock != NULL)
      {
         pEv = NI_GET_EVENTS(pNiBlock);
         for (i=0; i < pNiBlock->no_events; i++)
         {
            evName = ((pEv[i].off_evstr != 0) ? NI_GET_EVENT_STR(pNiBlock, &pEv[i]) : (uchar *)"?");
            if (pEv[i].next_type == 1) 
            {  // link to NI -> cascade
               sprintf(subname, "%sx%d_%d", argv[1], i, pEv[i].next_id);
               sprintf(comm, "%s add cascade -label {%s} -menu {%s}\n"
                             "if {[string length [info commands %s]] > 0} {\n"
                             "   PostDynamicMenu %s C_CreateNi\n"
                             "} elseif {![info exist dynmenu_posted(%s)] || ($dynmenu_posted(%s) == 0)} {\n"
                             "   menu %s -postcommand {PostDynamicMenu %s C_CreateNi}\n"
                             "}\n",
                             argv[1], evName, subname,
                             subname, subname,
                             subname, subname,
                             subname, subname);
            }
            else
            {  // link to OI
               p = strrchr(argv[1], '.');
               if ((p != NULL) && !strncmp(p, ".ni_", 4))
               {
                  sprintf(comm, "%s add command -label {%s} -command {C_SelectNi %sx%d}\n",
                                argv[1], evName, p+4, i);
               }
               else
                  debug1("Create-Ni: invalid menu %s", argv[1]);
            }
            eval_check(interp, comm);
         }

         // append "Reset" command to the top-level menu
         if (blockno == 1)
         {
            sprintf(comm, "%s add separator\n"
                          "%s add command -label Reset -command {ResetFilterState; C_ResetFilter all; C_ResetPiListbox}\n",
                          argv[1], argv[1]);
            eval_check(interp, comm);
         }
      }
      else
      {
         debug1("Create-Ni: blockno=%d not found", blockno);
         if (blockno == 1)
         {  // no NI available -> at least show "Reset" command
            sprintf(comm, "%s add command -label Reset -command {ResetFilterState; C_ResetFilter all; C_ResetPiListbox}\n", argv[1]);
            eval_check(interp, comm);
         }
      }

      EpgDbLockDatabase(dbc, FALSE);
      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Apply navigation menu filter setting
//
static int SelectNi( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_SelectNi <menu-path>";
   const NI_BLOCK *pNiBlock;
   const EVENT_ATTRIB *pEv;
   NI_FILTER_STATE niState;
   char *p, *n;
   int blockno, index, attrib;
   int result;

   if (argc != 2) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      EpgDbLockDatabase(dbc, TRUE);
      EpgDbFilterInitNi(pPiFilterContext, &niState);
      dprintf1("Select-Ni: processing NI stack %s\n", argv[1]);
      p = argv[1];
      while (1)
      {
         blockno = strtol(p, &n, 10);
         if (p == n)
            break;

         assert(*n == 'x');
         p = n + 1;
         index = strtol(p, &n, 10);
         assert(p != n);

         pNiBlock = EpgDbGetNi(dbc, blockno);
         if ((pNiBlock != NULL) && (index < pNiBlock->no_events))
         {
            dprintf2("Select-Ni: apply NI %d #%d\n", blockno, index);
            pEv = NI_GET_EVENTS(pNiBlock) + index;

            // apply all filters of the selected item
            for (attrib=0; attrib < pEv->no_attribs; attrib++)
            {
               dprintf2("           filter kind=0x%x data=0x%lx\n", pEv->unit[attrib].kind, pEv->unit[attrib].data);
               EpgDbFilterApplyNi(dbc, pPiFilterContext, &niState, pEv->unit[attrib].kind, pEv->unit[attrib].data);
            }
         }
         else
            debug3("Select-Ni: invalid blockno=%d or index=%d/%d", blockno, index, ((pNiBlock != NULL) ? pNiBlock->no_events : 0));

         if (*n == 0)
            break;

         assert(*n == '_');
         p = n + 1;
      }
      // lift filter settings into GUI menu state
      // (called before applying time-slots since the state is altered in there)
      UpdateFilterContextMenuState(&niState);
      // apply time filter settings to filter context
      EpgDbFilterFinishNi(pPiFilterContext, &niState);
      // add network and expire pre-filters
      EpgDbFilterEnable(pPiFilterContext, FILTER_PERM);

      PiListBox_Refresh();

      EpgDbLockDatabase(dbc, FALSE);
      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Fill listbox widget with names of all netwops
//
void PiFilter_UpdateNetwopList( void )
{
   int idx, netwop;
   char **argv;
   int argc;

   if (pPiFilterContext != NULL)
   {
      // remove the old list; set cursor on first element
      sprintf(comm, "UpdateProvCniTable 0x%04X\n", EpgDbContextGetCni(pUiDbContext));
      if ( (eval_check(interp, comm) == TCL_OK) &&
           (Tcl_SplitList(interp, interp->result, &argc, &argv) == TCL_OK) )
      {
         EpgDbFilterInitNetwopPreFilter(pPiFilterContext);
         EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP_PRE);

         for (idx = 0; idx < argc; idx++) 
         {
            if (Tcl_GetInt(interp, argv[idx], &netwop) == TCL_OK)
            {
               EpgDbFilterSetNetwopPreFilter(pPiFilterContext, netwop);
            }
         }
         Tcl_Free((char *) argv);
      }

      // check if browser is empty due to network prefilters
      UiControl_CheckDbState();

      // removed pre-filtered networks from the browser, or add them back
      PiListBox_Refresh();
   }
}

// ----------------------------------------------------------------------------
// Set netwop prefilters and update network listbox
// - called by GUI after change of user network selection (netsel popup)
//
static int UpdateNetwopList( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   PiFilter_UpdateNetwopList();
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Create the context menu (after click with the right mouse button onto a PI)
//
static int CreateContextMenu( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_CreateContextMenu <menu-path>";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   int idx, entryCount;
   uchar theme, themeCat, netwop;
   int result;
   const char * pTmpStr;
   char ** userCmds;
   int userCmdCount;

   if (argc != 2) 
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
         entryCount = 0;

         // query listbox for user-selected PI, if any
         pPiBlock = PiListBox_GetSelectedPi();

         // undo substring filter
         if (pPiFilterContext->enabledFilters & (FILTER_SUBSTR_TITLE|FILTER_SUBSTR_DESCR))
         {
            sprintf(comm, "%s add command -label {Undo substring filter '%s'} "
                          "-command {set substr_pattern {}; SubstrUpdateFilter}\n",
                          argv[1], pPiFilterContext->subStrFilter);
            eval_check(interp, comm);
            entryCount += 1;
         }

         // undo netwop filter
         if (pPiFilterContext->enabledFilters & FILTER_NETWOP)
         {
            // check if more than one network is currently selected
            idx = 0;
            if (pPiBlock != NULL)
            {
               for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
                  if ( (pPiFilterContext->netwopFilterField[netwop]) &&
                       (netwop != pPiBlock->netwop_no) )
                     idx += 1;
            }
            if (idx > 1)
            {  // more than one network -> offer to remove only the selected one
               sprintf(comm, "%s add command -label {Remove network %s} -command {SelectNetwopByIdx %d 0}\n",
                             argv[1], AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no), pPiBlock->netwop_no);
               eval_check(interp, comm);

               // offer to remove all network filters
               sprintf(comm, "%s add command -label {Undo all network filters} -command {C_SelectNetwops {}; ResetNetwops; C_RefreshPiListbox; CheckShortcutDeselection}\n", argv[1]);
               eval_check(interp, comm);

               // increase count only by one, since both entries have the same type
               entryCount += 1;
            }
            else
            {  // just one network selected -> offer to remove it
               sprintf(comm, "%s add command -label {Undo network filter} -command {C_SelectNetwops {}; ResetNetwops; C_RefreshPiListbox; CheckShortcutDeselection}\n", argv[1]);
               eval_check(interp, comm);
               entryCount += 1;
            }
         }

         // undo editorial rating
         if (pPiFilterContext->enabledFilters & FILTER_EDIT_RAT)
         {
            sprintf(comm, "%s add command -label {Undo editorial rating filter} -command {set editorial_rating 0; SelectEditorialRating}", argv[1]);
            eval_check(interp, comm);
            entryCount += 1;
         }

         // undo parental rating
         if (pPiFilterContext->enabledFilters & FILTER_PAR_RAT)
         {
            sprintf(comm, "%s add command -label {Undo parental rating filter} -command {set parental_rating 0; SelectParentalRating}", argv[1]);
            eval_check(interp, comm);
            entryCount += 1;
         }

         // undo feature filters
         if (pPiFilterContext->enabledFilters & FILTER_FEATURES)
         {
            sprintf(comm, "%s add command -label {Undo feature filters} -command {C_SelectFeatures {}; ResetFeatures; C_RefreshPiListbox; CheckShortcutDeselection}", argv[1]);
            eval_check(interp, comm);
            entryCount += 1;
         }

         // undo progidx
         if (pPiFilterContext->enabledFilters & FILTER_PROGIDX)
         {
            sprintf(comm, "%s add command -label {Undo program index filter} -command {set filter_progidx 0; SelectProgIdx -1 -1}", argv[1]);
            eval_check(interp, comm);
            entryCount += 1;
         }

         // undo start time filter
         if (pPiFilterContext->enabledFilters & (FILTER_TIME_BEG | FILTER_TIME_END))
         {
            sprintf(comm, "%s add command -label {Undo start time filter} -command UndoTimeFilter", argv[1]);
            eval_check(interp, comm);
            entryCount += 1;
         }

         // undo series
         if (pPiFilterContext->enabledFilters & FILTER_SERIES)
         {
            int count, series, netwop;

            count = series = 0;
            if (pPiBlock != NULL)
            {
               // check if a title with series-code is selected
               for (idx=0; idx < pPiBlock->no_themes; idx++)
               {
                  theme = pPiBlock->themes[idx];
                  if ( (theme > 128) &&
                       pPiFilterContext->seriesFilterMatrix[pPiBlock->netwop_no][theme - 128] )
                  {
                     series = theme;
                     break;
                  }
               }
               // check if more than one series filter is set
               if (series != 0)
               {
                  for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
                  {
                     for (idx=0; idx < 128; idx++)
                     {
                        if (pPiFilterContext->seriesFilterMatrix[netwop][idx])
                        {
                           count++;
                           if (count > 1)
                              break;
                        }
                     }
                  }
               }

               if (count > 1)
               {
                  EpgDbFilterSetSeries(pPiFilterContext, pPiBlock->netwop_no, series, FALSE);
                  if (EpgDbSearchFirstPi(pUiDbContext, pPiFilterContext) != NULL)
                  {
                     series |= pPiBlock->netwop_no << 8;
                     sprintf(comm, "%s add command -label {Remove this series only} -command {set series_sel(%d) 0; SelectSeries %d; C_RefreshPiListbox}",
                                   argv[1], series, series);
                     eval_check(interp, comm);
                  }
                  EpgDbFilterSetSeries(pPiFilterContext, pPiBlock->netwop_no, series, TRUE);
               }
            }

            sprintf(comm, "%s add command -label {Undo series filter} -command {C_ResetFilter series; ResetSeries; C_RefreshPiListbox; CheckShortcutDeselection}", argv[1]);
            eval_check(interp, comm);
            entryCount += 1;
         }

         // undo themes filters
         if (pPiFilterContext->enabledFilters & FILTER_THEMES)
         {
            int class, theme, count;

            // check if more than one theme filter is set
            count = theme = 0;
            for (idx=0; idx <= 128; idx++)
            {
               if (pPiFilterContext->themeFilterField[idx] != 0)
               {
                  theme = idx;
                  count += 1;
                  if (count > 1)
                     break;
               }
            }

            if (count == 1)
            {
               sprintf(comm, "%s add command -label {Undo themes filter %s} -command {",
                             argv[1], (((theme > 0) && (pdc_themes[theme] == NULL)) ? "" : (char *)pdc_themes[theme]));
               for (class=0; class < THEME_CLASS_COUNT; class++)
               {
                  if (pPiFilterContext->themeFilterField[theme] & (1 << class))
                     sprintf(comm + strlen(comm), "C_SelectThemes %d {};", class + 1);
               }
               strcat(comm, "ResetThemes; C_RefreshPiListbox; CheckShortcutDeselection}\n");
               eval_check(interp, comm);
            }
            else if (count > 1)
            {
               for (idx=0; idx <= 128; idx++)
               {
                  if ( ((pPiFilterContext->themeFilterField[idx] & pPiFilterContext->usedThemeClasses) != 0) &&
                       (pdc_themes[idx] != NULL) )
                  {
                     sprintf(comm, "%s add command -label {Undo themes filter %s} -command {", argv[1], (char *)pdc_themes[idx]);
                     for (class=0; class < THEME_CLASS_COUNT; class++)
                     {
                        // only offer to undo this theme if it is in exactly one class
                        if (pPiFilterContext->themeFilterField[idx] == (1 << class))
                           sprintf(comm + strlen(comm), "DeselectTheme %d %d;", class + 1, idx);
                     }
                     strcat(comm, "}\n");
                     eval_check(interp, comm);
                     entryCount += 1;
                  }
               }
            }
            entryCount += 1;
         }

         // undo sorting criteria filters
         if (pPiFilterContext->enabledFilters & FILTER_SORTCRIT)
         {
            sprintf(comm, "%s add command -label {Undo sorting criteria filter} -command {for {set idx 1} {$idx <= $theme_class_count} {incr idx} {C_SelectSortCrits $idx {}}; ResetSortCrits; C_RefreshPiListbox; CheckShortcutDeselection}", argv[1]);
            eval_check(interp, comm);
            entryCount += 1;
         }

         if (entryCount > 1)
         {
            sprintf(comm, "%s add command -label {Reset all filters} -command {ResetFilterState; C_ResetFilter all; C_RefreshPiListbox}\n", argv[1]);
            eval_check(interp, comm);
         }

         if (entryCount >= 1)
         {
            sprintf(comm, "%s add separator\n", argv[1]);
            eval_check(interp, comm);
            entryCount = 0;
         }

         // ---------------------------------------------------------------------
         // Offer filter addition

         if (pPiBlock != NULL)
         {
            // substring filter
            if ( (pPiFilterContext->enabledFilters & (FILTER_SUBSTR_TITLE|FILTER_SUBSTR_DESCR)) == FALSE )
            {
               uchar subStr[SUBSTR_FILTER_MAXLEN+1];

               strncpy(subStr, PI_GET_TITLE(pPiBlock), SUBSTR_FILTER_MAXLEN);
               subStr[SUBSTR_FILTER_MAXLEN] = 0;
               idx = strlen(subStr) - 1;

               if ((idx >= 0) && (subStr[idx] == ')'))
               {
                  idx--;
                  while ((idx >= 0) && (subStr[idx] >= '0') && (subStr[idx] <= '9'))
                     idx--;
                  if ((idx > 0) && (subStr[idx] == '(') && (subStr[idx - 1] == ' '))
                     subStr[idx - 1] = 0;
               }

               if (subStr[0] != 0)
               {
                  EpgDbFilterEnable(pPiFilterContext, FILTER_SUBSTR_TITLE);
                  EpgDbFilterSetSubStr(pPiFilterContext, subStr, TRUE, TRUE);
                  if ( (EpgDbSearchPrevPi(pUiDbContext, pPiFilterContext, pPiBlock) != NULL) ||
                       (EpgDbSearchNextPi(pUiDbContext, pPiFilterContext, pPiBlock) != NULL) )
                  {
                     sprintf(comm, "%s add command -label {Filter title '%s'} -command {SubstrSetFilter 1 0 1 1 {%s}}\n",
                                   argv[1], subStr, subStr);
                     eval_check(interp, comm);
                     entryCount += 1;
                  }
                  EpgDbFilterDisable(pPiFilterContext, FILTER_SUBSTR_TITLE);
               }
            }

            // themes filter
            for (idx=0; idx < pPiBlock->no_themes; idx++)
            {
               theme = pPiBlock->themes[idx];
               if (theme <= 128)
               {
                  if ( ((pPiFilterContext->enabledFilters & FILTER_THEMES) == FALSE ) ||
                       (pPiFilterContext->themeFilterField[theme] == FALSE) )
                  {
                     themeCat = PdcThemeGetCategory(theme);
                     if ( (pPiFilterContext->enabledFilters & FILTER_THEMES) &&
                          (themeCat != theme) &&
                          (pPiFilterContext->themeFilterField[themeCat] != FALSE) )
                     {  // special case: undo general theme before sub-theme is enabled, else filter would have no effect (due to OR)
                        sprintf(comm, "%s add command -label {Filter theme %s} -command {set theme_sel(%d) 0; set theme_sel(%d) 1; SelectTheme %d}\n",
                                      argv[1], ((pdc_themes[theme] == NULL) ? "" : (char *)pdc_themes[theme]), themeCat, theme, theme);
                        eval_check(interp, comm);
                     }
                     else
                     {
                        sprintf(comm, "%s add command -label {Filter theme %s} -command {set theme_sel(%d) 1; SelectTheme %d}\n",
                                      argv[1], ((pdc_themes[theme] == NULL) ? "" : (char *)pdc_themes[theme]), theme, theme);
                        eval_check(interp, comm);
                     }
                     entryCount += 1;
                  }
               }
            }

            // series filter
            for (idx=0; idx < pPiBlock->no_themes; idx++)
            {
               theme = pPiBlock->themes[idx];
               if (theme > 128)
               {
                  if ( ((pPiFilterContext->enabledFilters & FILTER_SERIES) == FALSE ) ||
                       (pPiFilterContext->seriesFilterMatrix[pPiBlock->netwop_no][theme - 128] == FALSE) )
                  {
                     bool wasEnabled = ((pPiFilterContext->enabledFilters & FILTER_SERIES) != 0);
                     if (wasEnabled == FALSE)
                     {
                        EpgDbFilterEnable(pPiFilterContext, FILTER_SERIES);
                        EpgDbFilterInitSeries(pPiFilterContext);
                     }
                     EpgDbFilterSetSeries(pPiFilterContext, pPiBlock->netwop_no, theme, TRUE);
                     if ( (EpgDbSearchPrevPi(pUiDbContext, pPiFilterContext, pPiBlock) != NULL) ||
                          (EpgDbSearchNextPi(pUiDbContext, pPiFilterContext, pPiBlock) != NULL) )
                     {
                        sprintf(comm, "%s add command -label {Filter this series} -command {set series_sel(%d) 1; SelectSeries %d}\n",
                                      argv[1], theme | (pPiBlock->netwop_no << 8), theme | (pPiBlock->netwop_no << 8));
                        eval_check(interp, comm);
                        entryCount += 1;
                     }
                     if (wasEnabled == FALSE)
                        EpgDbFilterDisable(pPiFilterContext, FILTER_SERIES);
                     else
                        EpgDbFilterSetSeries(pPiFilterContext, pPiBlock->netwop_no, theme, FALSE);
                  }
               }
            }

            // feature/repeat filter - only offered in addition to series or title filters
            if ( pPiFilterContext->enabledFilters & (FILTER_SERIES | FILTER_SUBSTR_TITLE) )
            {
               if ( ((pPiFilterContext->enabledFilters & FILTER_FEATURES) == FALSE) ||
                    (pPiFilterContext->featureFilterCount == 1) )
               {
                  bool wasEnabled = ((pPiFilterContext->enabledFilters & FILTER_FEATURES) != 0);
                  uint oldMask    = pPiFilterContext->featureFilterMaskField[0];
                  uint oldFlags   = pPiFilterContext->featureFilterFlagField[0];

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
                           sprintf(comm, "%s add command -label {Filter for original transmissions} -command {SelectFeaturesAllClasses 0xc0 0 0}\n", argv[1]);
                           eval_check(interp, comm);
                           entryCount += 1;
                        }
                        if ( (wasEnabled == FALSE) ||
                             ((oldMask & 0x80) == 0) ||
                             ((oldFlags & 0x80) == 0) )
                        {
                           sprintf(comm, "%s add command -label {Filter for repeats} -command {SelectFeaturesAllClasses 0x80 0x80 0x40}\n", argv[1]);
                           eval_check(interp, comm);
                           entryCount += 1;
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
            if ( (pPiFilterContext->enabledFilters & FILTER_NETWOP) == FALSE )
            {  // no network filter yet -> offer to filter for the currently selected
               sprintf(comm, "%s add command -label {Filter network %s} -command {SelectNetwopByIdx %d 1}\n",
                             argv[1], AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no), pPiBlock->netwop_no);
               eval_check(interp, comm);
               entryCount += 1;
            }
            else
            {  // check if there's more than one network selected -> offer to reduce filtering to one
               for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
                  if ( (pPiFilterContext->netwopFilterField[netwop]) &&
                       (netwop != pPiBlock->netwop_no) )
                     break;
               if (netwop < pAiBlock->netwopCount)
               {
                  sprintf(comm, "%s add command -label {Filter only network %s} -command {ResetNetwops; SelectNetwopByIdx %d 1}\n",
                                argv[1], AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no), pPiBlock->netwop_no);
                  eval_check(interp, comm);
                  entryCount += 1;
               }
            }

            // ---------------------------------------------------------------------
            // Add user-defined entries

            pTmpStr = Tcl_GetVar(interp, "ctxmencf", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
            if ( (pTmpStr != NULL) &&
                 (Tcl_SplitList(interp, pTmpStr, &userCmdCount, &userCmds) == TCL_OK) &&
                 (userCmdCount > 0) )
            {
               if (entryCount >= 1)
               {
                  sprintf(comm, "%s add separator\n", argv[1]);
                  eval_check(interp, comm);
               }
               for (idx=0; idx < userCmdCount; idx += 2)
               {
                  sprintf(comm, "%s add command -label {%s} -command {C_ExecUserCmd %d}\n", argv[1], userCmds[idx], idx / 2);
                  eval_check(interp, comm);
               }
            }
         }
      }
      EpgDbLockDatabase(dbc, FALSE);

      result = TCL_OK; 
   }
   return result;
}

// ---------------------------------------------------------------------------
// Suppress expired PI blocks in the listbox
// - a PI is considered expired, when it's stop time is less or equal the
//   expire time, which is set here every minute. We must not use the current
//   time, because then PI do expire asynchronously with the GUI, so that
//   the tight sync between PI listbox and DB would break.
// - must be called exery minute, or SearchFirst will not work,
//   i.e. return expired PI blocks
// - returns TRUE if any blocks are newly expired
//
void PiFilter_Expire( void )
{
   EpgDbFilterSetExpireTime(pPiFilterContext, time(NULL));

   // refresh the listbox to remove expired PI
   PiListBox_UpdateNowItems();
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
      Tcl_CreateCommand(interp, "C_SelectThemes", SelectThemes, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectSortCrits", SelectSortCrits, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectSeries", SelectSeries, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectNetwops", SelectNetwops, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectFeatures", SelectFeatures, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectParentalRating", SelectParentalRating, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectEditorialRating", SelectEditorialRating, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectSubStr", SelectSubStr, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectProgIdx", SelectProgIdx, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectStartTime", SelectStartTime, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ResetFilter", PiFilter_Reset, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_GetPdcString", GetPdcString, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetNetwopsWithSeries", GetNetwopsWithSeries, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetNetwopSeriesList", GetNetwopSeriesList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_IsNavigateMenuEmpty", IsNavigateMenuEmpty, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_CreateNi", CreateNi, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectNi", SelectNi, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_UpdateNetwopList", UpdateNetwopList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetNetwopFilterList", GetNetwopFilterList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetSeriesByLetter", GetSeriesByLetter, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetSeriesTitles", GetSeriesTitles, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_CreateContextMenu", CreateContextMenu, (ClientData) NULL, NULL);

      sprintf(comm, "GenerateFilterMenues %d %d\n"
                    "ResetFilterState\n",
                    THEME_CLASS_COUNT, FEATURE_CLASS_COUNT);
      eval_check(interp, comm);
   }
   else
      debug0("PiFilter-Create: commands are already created");

   // create and initialize the filter context
   pPiFilterContext = EpgDbFilterCreateContext();
   // initialize the expire time filter
   EpgDbFilterSetExpireTime(pPiFilterContext, time(NULL));
   EpgDbFilterEnable(pPiFilterContext, FILTER_EXPIRE_TIME);
}

// ----------------------------------------------------------------------------
// destroy the filter menus
//
void PiFilter_Destroy( void )
{
   EpgDbFilterDestroyContext(pPiFilterContext);
}

