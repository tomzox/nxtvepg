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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: pifilter.c,v 1.24 2000/10/09 18:05:26 tom Exp tom $
 */

#define __PIFILTER_C

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tcl.h"
#include "tk.h"

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgmain.h"
#include "epgui/pdc_themes.h"
#include "epgui/pilistbox.h"
#include "epgui/pifilter.h"


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
   const char * const pUsage = "Usage: SelectThemes <class 1..8> <themes-list>";
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
   const char * const pUsage = "Usage: SelectSortCrits <class 1..8> <list>";
   uchar usedClasses;
   int class, sortcrit, idx;
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
         usedClasses = EpgDbFilterInitSortCrit(pPiFilterContext, class);

         for (idx=0; idx < argc; idx++)
         {
            result = Tcl_GetInt(interp, argv[idx], &sortcrit);
            if (result == TCL_OK)
            {
               EpgDbFilterSetSortCrit(pPiFilterContext, sortcrit, sortcrit, class);
            }
            else
               break;
         }

         if ((argc > 0) || usedClasses)
            EpgDbFilterEnable(pPiFilterContext, FILTER_SORTCRIT);
         else
            EpgDbFilterDisable(pPiFilterContext, FILTER_SORTCRIT);

         Tcl_Free((char *) argv);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for series checkbuttons
//
static int SelectSeries( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: SelectSeries [<series-code> <enable=0/1>]";
   int series, enable;
   int result; 
   
   if ((argc != 1) && (argc != 3))
   {  // illegal parameter count (the themes must be passed as list, not separate items)
      interp->result = (char *) pUsage;
      result = TCL_ERROR; 
   }  
   else if (argc == 1)
   {  // special case: undo series filter
      EpgDbFilterDisable(pPiFilterContext, FILTER_SERIES);
      result = TCL_OK; 
   }
   else if ( Tcl_GetInt(interp, argv[1], &series) ||
             Tcl_GetInt(interp, argv[2], &enable) || ((uint)enable > 1) )
   {  // illegal parameter format
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }
   else
   {
      result = TCL_OK; 

      if ((pPiFilterContext->enabledFilters & FILTER_SERIES) == FALSE)
      {
         EpgDbFilterInitSeries(pPiFilterContext);
      }
      EpgDbFilterSetSeries(pPiFilterContext, series >> 8, series & 0xff, enable);
      EpgDbFilterEnable(pPiFilterContext, FILTER_SERIES);
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for netwop listbox: update the netwop filter setting
//
static int SelectNetwops( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: SelectNetwops <netwop-list>";
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
   const char * const pUsage = "Usage: SelectFeatures (<mask> <value>){0..6}";
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
   const char * const pUsage = "Usage: SelectParentalRating <value>";
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
   const char * const pUsage = "Usage: SelectEditorialRating <value>";
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
   const char * const pUsage = "Usage: SelectSubStr <bTitle=0/1> <bDescr=0/1> <bCase=0/1> <substring>";
   int scope_title, scope_descr, case_ignore;
   int result; 
   
   if (argc != 5) 
   {  // wrong parameter count
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if ( Tcl_GetInt(interp, argv[1], &scope_title) || ((uint)scope_title > 1) ||
             Tcl_GetInt(interp, argv[2], &scope_descr) || ((uint)scope_descr > 1) ||
             Tcl_GetInt(interp, argv[3], &case_ignore) || ((uint)case_ignore > 1) )
   {  // one parameter is not a boolean
      interp->result = (char *) pUsage;
      result = TCL_ERROR; 
   }
   else
   {
      EpgDbFilterDisable(pPiFilterContext, FILTER_SUBSTR_TITLE|FILTER_SUBSTR_DESCR);

      if ((argv[4] != NULL) && (argv[4][0] != 0))
      {
         if (scope_title)
            EpgDbFilterEnable(pPiFilterContext, FILTER_SUBSTR_TITLE);
         if (scope_descr)
            EpgDbFilterEnable(pPiFilterContext, FILTER_SUBSTR_DESCR);

	 EpgDbFilterSetSubStr(pPiFilterContext, argv[4], case_ignore);
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
   const char * const pUsage = "Usage: SelectProgIdx <first-index> <last-index>";
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

              if (!strncmp(p, "all", 3))       mask |= FILTER_ALL & ~FILTER_NETWOP_PRE;
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
// Create the series netwop menu
// - consists of a list of all netwops with PI that have assigned PDC themes > 0x80
// - each entry is a sub-menu (cascade) that list all found series in that netwop
//
static int CreateSeriesNetwopMenu( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_CreateSeriesNetwopMenu <menu-path>";
   const AI_BLOCK *pAiBlock;
   FILTER_CONTEXT *fc;
   bool found;
   int netwop;
   int result;

   if (argc != 2) 
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
         Tcl_UnsetVar(interp, "tmp_list", 0);
         found = FALSE;

         for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
         {
            EpgDbFilterInitNetwop(fc);
            EpgDbFilterSetNetwop(fc, netwop);
            if (EpgDbSearchFirstPi(dbc, fc) != NULL)
            {
               sprintf(comm, "lappend tmp_list [list %d {%s}]", netwop, AI_GET_NETWOP_NAME(pAiBlock, netwop));
               eval_check(interp, comm);
               found = TRUE;
            }
         }

         if (found)
         {  // sort found netwop names by user-selection and add them to the menu
            sprintf(comm, "FillSeriesMenu %s 0x%04X $tmp_list\n", argv[1], AI_GET_CNI(pAiBlock));
            eval_check(interp, comm);

            Tcl_UnsetVar(interp, "tmp_list", 0);
         }
      }
      EpgDbFilterDestroyContext(fc);
      EpgDbLockDatabase(dbc, FALSE);
   }  

   return result;
}

// ----------------------------------------------------------------------------
// Create the series sub-menu for a selected network
// - with a list of all series on this network, sorted by title
//
static int CreateSeriesMenu( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_CreateSeriesMenu <menu-path.netwop_#>";
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK * pPiBlock;
   char *p;
   bool usedSeries[0x80];
   FILTER_CONTEXT *fc;
   uchar netwop, series;
   uint index;
   int result;

   if ( (argc != 2)  ||
        ((p = strstr(argv[1], "netwop_")) == NULL) )
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      netwop = strtol(p + 7, NULL, 10);
      result = TCL_OK; 

      EpgDbLockDatabase(dbc, TRUE);
      fc = EpgDbFilterCreateContext();
      pAiBlock = EpgDbGetAi(dbc);
      if ((pAiBlock != NULL) && (netwop < pAiBlock->netwopCount))
      {
         memset(usedSeries, FALSE, sizeof(usedSeries));

         EpgDbFilterEnable(fc, FILTER_NETWOP);
         EpgDbFilterInitNetwop(fc);
         EpgDbFilterSetNetwop(fc, netwop);

         pPiBlock = EpgDbSearchFirstPi(dbc, fc);
         if (pPiBlock != NULL)
         {
            Tcl_UnsetVar(interp, "tmp_list", TCL_GLOBAL_ONLY);
            do
            {
               series = 0;
               for (index=0; index < pPiBlock->no_themes; index++)
               {
                  series = pPiBlock->themes[index];
                  if ((series > 0x80) && (usedSeries[series - 0x80] == FALSE))
                  {
                     // append the command to create this menu entry to a list
                     usedSeries[series - 0x80] = TRUE;
                     //sprintf(comm, "lappend tmp_list {%s add checkbutton -label {%s} -variable series_sel(%d) -command {SelectSeries %d}}",
                     sprintf(comm, "lappend tmp_list {%s} %d",
                                   PI_GET_TITLE(pPiBlock),
                                   netwop * 0x100 + series);
                     eval_check(interp, comm);
                     //printf("%s 0x%02x - %s\n", netname, series, PI_GET_TITLE(pPiBlock));
                     break;
                  }
               }
               pPiBlock = EpgDbSearchNextPi(dbc, fc, pPiBlock);
            }
            while (pPiBlock != NULL);

            // sort the list of commands by series title and then create the entries in that order
            sprintf(comm, "CreateSeriesMenuEntries %s $tmp_list %d\n",
                          argv[1], AI_GET_NETWOP_N(pAiBlock, netwop)->alphabet);
            eval_check(interp, comm);
         }
      }
      EpgDbFilterDestroyContext(fc);
      EpgDbLockDatabase(dbc, FALSE);
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
               sprintf(comm, "set series_sel(%d) 1\n", netwop*128 + series);
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
      uint nowMoD = ((time(NULL) + lto) % (60*60*24)) / 60;

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

      eval_check(interp, "if {$timsel_popup} SelectTimeFilterRelStart\n");
   }
}

// ----------------------------------------------------------------------------
// Create navigation menu
//
static int CreateNi( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_CreateNi <menu-path>";
   const NI_BLOCK *pNiBlock;
   const EVENT_ATTRIB *pEv;
   uchar *evName;
   char *p, subname[100];
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
      while ((p > argv[1]) && isdigit(*(p-1)))
         p--;
      blockno = strtol(p, NULL, 10);

      pNiBlock = EpgDbGetNi(dbc, blockno);
      if (pNiBlock != NULL)
      {
         pEv = NI_GET_EVENTS(*pNiBlock);
         for (i=0; i < pNiBlock->no_events; i++)
         {
            evName = ((pEv[i].off_evstr != 0) ? NI_GET_EVENT_STR(*pNiBlock, pEv[i]) : (uchar *)"?");
            if (pEv[i].next_type == 1) 
            {  // link to NI -> cascade
               sprintf(subname, "%sx%d_%d", argv[1], i, pEv[i].next_id);
               sprintf(comm, "%s add cascade -label {%s} -menu {%s}\n"
                             "if {[string length [info command %s]] > 0} {destroy %s}\n"
                             "if {![info exist dynmenu_posted(%s)] || ($dynmenu_posted(%s) == 0)} {\n"
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
            pEv = NI_GET_EVENTS(*pNiBlock) + index;

            // apply all filters of the selected item
            for (attrib=0; attrib < pEv->no_attribs; attrib++)
            {
               dprintf2("           filter kind=0x%x data=0x%lx\n", pEv->unit[attrib].kind, pEv->unit[attrib].data);
               EpgDbFilterApplyNi(dbc, pPiFilterContext, &niState, pEv->unit[attrib].kind, pEv->unit[attrib].data);
            }
         }
         else
            debug3("Select-Ni: invalid blockno=%d or index=%d/%d", blockno, index, pNiBlock->no_events);

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
      // add network pre-filter
      EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP_PRE);

      PiListBox_Refresh();

      EpgDbLockDatabase(dbc, FALSE);
      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// fill listbox widget with names of all netwops
// - called as idle function after AI update or when first AI received
//
void PiFilter_UpdateNetwopList( ClientData clientData )
{
   const AI_BLOCK *pAiBlock;
   int idx, netwop;
   char **argv;
   int argc;

   if (pPiFilterContext != NULL)
   {
      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         // set the window title according to the new AI
         sprintf(comm, "wm title . {Nextview EPG: %s}\n", AI_GET_SERVICENAME(pAiBlock));
         eval_check(interp, comm);

         // remove the old list; set cursor on first element
         sprintf(comm, "UpdateNetwopFilterBar 0x%04X\n", EpgDbContextGetCni(pUiDbContext));
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

         PiListBox_Refresh();
      }
      EpgDbLockDatabase(dbc, FALSE);
   }
}

static int UpdateNetwopList( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   PiFilter_UpdateNetwopList(NULL);
   return TCL_OK;
}


// ----------------------------------------------------------------------------
// Get List of netwop CNIs and array of names from AI for netwop selection
//
static int GetAiNetwopList( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GetAiNetwopList";
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
      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
         {
            // append the CNI in the format "0x0D94" to the TCL result list
            sprintf(comm, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);
            Tcl_AppendElement(interp, comm);

            // as a side-effect the netwop names are stored into a TCL array
            Tcl_SetVar2(interp, "netsel_names", comm, AI_GET_NETWOP_NAME(pAiBlock, netwop), TCL_GLOBAL_ONLY);
         }
      }
      EpgDbLockDatabase(dbc, FALSE);
      result = TCL_OK; 
   }
   return result;
}

// ----------------------------------------------------------------------------
// Get CNI of currently selected provider for netwop selection popup
//
static int GetProvCni( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GetProvCni";
   int result;

   if (argc != 1) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      sprintf(comm, "0x%04X", EpgDbContextGetCni(pUiDbContext));
      Tcl_SetResult(interp, comm, TCL_VOLATILE);
      result = TCL_OK; 
   }
   return result;
}

// ----------------------------------------------------------------------------
// Create the context menu (after click with the right mouse button onto a PI)
//
static int CreateContextMenu( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_PiListBox_CreateContextMenu <menu-path>";
   const AI_BLOCK * pAiBlock;
   const AI_NETWOP *pNetwop;
   const PI_BLOCK * pPiBlock;
   int idx, entryCount;
   uchar theme, themeCat;
   int result;

   if (argc != 2) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      sprintf(comm, "bind %s <Leave> {%s unpost}\n", argv[1], argv[1]);
      eval_check(interp, comm);

      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         pNetwop = AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no);
         entryCount = 0;

         // query listbox for user-selected PI, if any
         pPiBlock = PiListBox_GetSelectedPi();

         // undo substring filter
         if (pPiFilterContext->enabledFilters & (FILTER_SUBSTR_TITLE|FILTER_SUBSTR_DESCR))
         {
            sprintf(comm, "%s add command -label {Undo substring filter '%s'} "
                          "-command {set substr_pattern {}; C_SelectSubStr 0 0 0 {}; C_RefreshPiListbox; CheckShortcutDeselection}\n",
                          argv[1], pPiFilterContext->subStrFilter);
            eval_check(interp, comm);
            entryCount += 1;
         }

         // undo netwop filter
         if (pPiFilterContext->enabledFilters & FILTER_NETWOP)
         {
            sprintf(comm, "%s add command -label {Undo network filter} -command {C_SelectNetwops {}; ResetNetwops; C_RefreshPiListbox; CheckShortcutDeselection}\n", argv[1]);
            eval_check(interp, comm);
            entryCount += 1;
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

            sprintf(comm, "%s add command -label {Undo series filter} -command {C_SelectSeries; ResetSeries; C_RefreshPiListbox; CheckShortcutDeselection}", argv[1]);
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
                  if (pPiFilterContext->themeFilterField[idx] != 0)
                  {
                     sprintf(comm, "%s add command -label {Undo themes filter %s} -command {",
                             argv[1], ((pdc_themes[idx] == NULL) ? "" : (char *)pdc_themes[idx]));
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
                  EpgDbFilterSetSubStr(pPiFilterContext, subStr, FALSE);
                  if ( (EpgDbSearchPrevPi(pUiDbContext, pPiFilterContext, pPiBlock) != NULL) ||
                       (EpgDbSearchNextPi(pUiDbContext, pPiFilterContext, pPiBlock) != NULL) )
                  {
                     sprintf(comm, "%s add command -label {Filter title '%s'} "
                                   "-command {set substr_grep_title 1; set substr_grep_descr 0; set substr_ignore_case 0; set substr_pattern {%s}; C_SelectSubStr $substr_grep_title $substr_grep_descr $substr_ignore_case $substr_pattern; C_RefreshPiListbox}\n",
                                   argv[1], subStr, subStr);
                     eval_check(interp, comm);
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
                        }
                        if ( (wasEnabled == FALSE) ||
                             ((oldMask & 0x80) == 0) ||
                             ((oldFlags & 0x80) == 0) )
                        {
                           sprintf(comm, "%s add command -label {Filter for repeats} -command {SelectFeaturesAllClasses 0x80 0x80 0x40}\n", argv[1]);
                           eval_check(interp, comm);
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
            {
               sprintf(comm, "%s add command -label {Filter network %s} -command {C_SelectNetwops %d; C_RefreshPiListbox}\n",
                             argv[1], AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no), pPiBlock->netwop_no);
               eval_check(interp, comm);
            }
         }
      }
      EpgDbLockDatabase(dbc, FALSE);

      result = TCL_OK; 
   }
   return result;
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
      Tcl_CreateCommand(interp, "C_CreateSeriesNetwopMenu", CreateSeriesNetwopMenu, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_CreateSeriesMenu", CreateSeriesMenu, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_CreateNi", CreateNi, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectNi", SelectNi, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_UpdateNetwopList", UpdateNetwopList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetProvCni", GetProvCni, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetAiNetwopList", GetAiNetwopList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetNetwopFilterList", GetNetwopFilterList, (ClientData) NULL, NULL);

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

   PiFilter_UpdateNetwopList(NULL);
}

// ----------------------------------------------------------------------------
// destroy the filter menus
//
void PiFilter_Destroy( void )
{
   EpgDbFilterDestroyContext(pPiFilterContext);
}

