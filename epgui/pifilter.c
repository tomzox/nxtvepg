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
 *  $Id: pifilter.c,v 1.17 2000/07/08 11:07:58 tom Exp tom $
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
uchar preFilterNetwops[MAX_NETWOP_COUNT];

#define dbc pUiDbContext         // internal shortcut

// ----------------------------------------------------------------------------
// Update the themes filter setting
// - special value 0x80 is extended to cover all series codes 0x80...0xff
//
static int SelectThemes( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * const pUsage = "Usage: SelectThemes <class 0..7> <themes-list>";
   char *p, *n;
   uchar usedClasses;
   int class, theme;
   int result; 
   
   if ( (argc != 3)  || Tcl_GetInt(interp, argv[1], &class) ||
        (class == 0) || ((uint)class > THEME_CLASS_COUNT) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = TCL_OK; 

      class = 1 << (class - 1);
      usedClasses = EpgDbFilterInitThemes(pPiFilterContext, class);
      p = argv[2];
      while (1)
      {
         theme = strtol(p, &n, 0);
         if (p == n)
            break;
         p = n;

         if (theme == 0x80)
            EpgDbFilterSetThemes(pPiFilterContext, 0x80, 0xff, class);
         else
            EpgDbFilterSetThemes(pPiFilterContext, theme, theme, class);
      }

      if ((p != argv[2]) || usedClasses)
         EpgDbFilterEnable(pPiFilterContext, FILTER_THEMES);
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_THEMES);
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
                  EpgDbFilterSetNetwop(pPiFilterContext, netwop - 1);
               }
               else
                  break;
            }
         }
         else
         {  // no netwop filtering -> allow all netwops in prefilter list
            memcpy(pPiFilterContext->netwopFilterField, preFilterNetwops, sizeof(pPiFilterContext->netwopFilterField));
         }

         // netwop filter always stays enabled for prefiltering
         EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP);

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
   const char * const pUsage = "Usage: SelectFeatures ([<mask> <value>]{0..6})";
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

              if (!strncmp(p, "all", 3))       mask |= FILTER_ALL;
         else if (!strncmp(p, "netwops", 7))   mask |= FILTER_NETWOP;
         else if (!strncmp(p, "themes", 6))    mask |= FILTER_THEMES;
         else if (!strncmp(p, "sortcrits", 9)) mask |= FILTER_SORTCRIT;
         else if (!strncmp(p, "series", 6))    mask |= FILTER_SERIES;
         else if (!strncmp(p, "substr", 6))    mask |= FILTER_SUBSTR_TITLE | FILTER_SUBSTR_DESCR;
         else if (!strncmp(p, "progidx", 7))   mask |= FILTER_PROGIDX;
         else if (!strncmp(p, "starttime", 9)) mask |= FILTER_TIME_BEG | FILTER_TIME_END;
         else if (!strncmp(p, "parental", 8))  mask |= FILTER_PAR_RAT;
         else if (!strncmp(p, "editorial", 9)) mask |= FILTER_EDIT_RAT;
         else if (!strncmp(p, "features", 8))  mask |= FILTER_FEATURES;
         else if (!strncmp(p, "languages", 9)) mask |= FILTER_LANGUAGES;
         else if (!strncmp(p, "subtitles", 9)) mask |= FILTER_SUBTITLES;
         else debug1("PiFilter-Reset: unknown keyword at %s", p);

         p = strchr(p, ' ');
      }
      EpgDbFilterDisable(pPiFilterContext, mask);

      // netwop filter is never disabled, just reset to prefilter state
      if (mask & FILTER_NETWOP)
      {
         EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP);
         memcpy(pPiFilterContext->netwopFilterField, preFilterNetwops, sizeof(pPiFilterContext->netwopFilterField));
      }
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
// Update filter menu state
//
static void UpdateFilterContextMenuState( void )
{
   uchar *class_str;
   uchar netwop, series;
   uint index, class, cur_class;

   sprintf(comm, "ResetFilterState\n");
   eval_check(interp, comm);

   if (pPiFilterContext->enabledFilters & FILTER_NETWOP)
   {
      // XXX unsupported in filter menus
   }

   if (pPiFilterContext->enabledFilters & FILTER_TIME_BEG)
   {
      // XXX unsupported in filter menus
   }

   if (pPiFilterContext->enabledFilters & FILTER_TIME_END)
   {
      // XXX unsupported in filter menus
   }

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
      class_str = Tcl_GetVar(interp, "current_theme_class", TCL_GLOBAL_ONLY);
      if ((class_str == NULL) || (sscanf(class_str, "%d", &cur_class) != 1))
         cur_class = 1;
      cur_class = 1 << (cur_class - 1);
      for (index=0; index < 255; index++)
      {
         if (pPiFilterContext->themeFilterField[index])
         {
            for (class=1; class != 0; class <<= 1)
            {
               if (pPiFilterContext->themeFilterField[index] & class)
               {
                  if (class != cur_class)
                     sprintf(comm, "lappend theme_class_sel(%d) %d\n", class, index);
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
      // XXX unsupported in filter menus
   }

   if (pPiFilterContext->enabledFilters & FILTER_LANGUAGES)
   {
   }

   if (pPiFilterContext->enabledFilters & FILTER_SUBTITLES)
   {
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
                          "%s add command -label Reset -command {ResetFilterState; C_ResetFilter; C_ResetPiListbox}\n",
                          argv[1], argv[1]);
            eval_check(interp, comm);
         }
      }
      else
      {
         debug1("Create-Ni: blockno=%d not found", blockno);
         if (blockno == 1)
         {  // no NI available -> at least show "Reset" command
            sprintf(comm, "%s add command -label Reset -command {ResetFilterState; C_ResetFilter; C_ResetPiListbox}\n", argv[1]);
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
      EpgDbFilterFinishNi(pPiFilterContext, &niState);
      UpdateFilterContextMenuState();

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

         memset(preFilterNetwops, TRUE, sizeof(preFilterNetwops));

         // remove the old list; set cursor on first element
         sprintf(comm, "UpdateNetwopFilterBar 0x%04X\n", EpgDbContextGetCni(pUiDbContext));
         if ( (eval_check(interp, comm) == TCL_OK) &&
              (Tcl_SplitList(interp, interp->result, &argc, &argv) == TCL_OK) )
         {
            for (idx = 0; idx < argc; idx++) 
            {
               if ( (Tcl_GetInt(interp, argv[idx], &netwop) == TCL_OK) &&
                    (netwop < MAX_NETWOP_COUNT) )
               {
                  preFilterNetwops[netwop] = FALSE;
               }
            }
            Tcl_Free((char *) argv);
         }

         // reset netwop filter state to prefiltered netwops only
         EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP);
         memcpy(pPiFilterContext->netwopFilterField, preFilterNetwops, sizeof(pPiFilterContext->netwopFilterField));
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
            sprintf(comm, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);
            Tcl_AppendElement(interp, comm);
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
// initialize the filter menus
// - this should be called only once during start-up
//
void PiFilter_Create( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_SelectThemes", &cmdInfo) == 0)
   {
      Tcl_CreateCommand(interp, "C_SelectThemes", SelectThemes, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectSeries", SelectSeries, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectNetwops", SelectNetwops, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectFeatures", SelectFeatures, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectParentalRating", SelectParentalRating, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectEditorialRating", SelectEditorialRating, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectSubStr", SelectSubStr, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectProgIdx", SelectProgIdx, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ResetFilter", PiFilter_Reset, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_GetPdcString", GetPdcString, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_CreateSeriesNetwopMenu", CreateSeriesNetwopMenu, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_CreateSeriesMenu", CreateSeriesMenu, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_CreateNi", CreateNi, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectNi", SelectNi, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_UpdateNetwopList", UpdateNetwopList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetProvCni", GetProvCni, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetAiNetwopList", GetAiNetwopList, (ClientData) NULL, NULL);

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

