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
 *  $Id: pifilter.c,v 1.11 2000/06/15 17:13:33 tom Exp tom $
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
// callback for theme listbox: update the themes filter setting
//
static int SelectThemes( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char *pUsage = "Usage: SelectThemes <class 0..7> <themes-list>";
   char *p, *n;
   uchar usedClasses;
   int class, theme;
   int result; 
   
   if (argc != 3) 
   {  // illegal parameter count (the themes must be passed as list, not separate items)
      interp->result = (char *) pUsage;
      result = TCL_ERROR; 
   }  
   else if ( Tcl_GetInt(interp, argv[1], &class) || (class >= 8) )
   {
      interp->result = (char *) pUsage;
      result = TCL_ERROR; 
   }
   else
   {
      result = TCL_OK; 

      usedClasses = EpgDbFilterInitThemes(pPiFilterContext, class);
      EpgDbFilterInitSeries(pPiFilterContext);
      p = argv[2];
      while (1)
      {
         theme = strtol(p, &n, 0);
         if (p == n)
            break;

         if (theme < 0x100)
            EpgDbFilterSetThemes(pPiFilterContext, theme, theme, class);
         else
         {
            EpgDbFilterSetSeries(pPiFilterContext, (theme >> 8)-1, theme & 0xff, TRUE);
            EpgDbFilterEnable(pPiFilterContext, FILTER_SERIES);
         }
         p = n;
      }

      if ((p != argv[2]) || usedClasses)
         EpgDbFilterEnable(pPiFilterContext, FILTER_THEMES);
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_THEMES);

      PiListBox_Refresh();
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for series checkbuttons
//
static int SelectSeries( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char *pUsage = "Usage: SelectSeries [<series-code> <enable=0/1>]";
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
      PiListBox_Refresh();
      result = TCL_OK; 
   }
   else if ( Tcl_GetInt(interp, argv[1], &series) ||
             Tcl_GetInt(interp, argv[2], &enable) || ((uint)enable > 1) )
   {  // illegal parameter format
      interp->result = (char *) pUsage;
      result = TCL_ERROR; 
   }
   else
   {
      result = TCL_OK; 

      if ((pPiFilterContext->enabledFilters &FILTER_SERIES) == FALSE)
      {
         EpgDbFilterInitSeries(pPiFilterContext);
      }
      EpgDbFilterSetSeries(pPiFilterContext, series >> 8, series & 0xff, enable);
      EpgDbFilterEnable(pPiFilterContext, FILTER_SERIES);

      PiListBox_Refresh();
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for netwop listbox: update the netwop filter setting
//
static int SelectNetwops( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   int netwop;
   char *p, *n;
   int result; 
   
   if (argc != 2) 
   {  // wrong parameter count
      interp->result = "SelectNetwops substring";
      result = TCL_ERROR; 
   }  
   else
   {
      EpgDbFilterInitNetwop(pPiFilterContext);
      p = argv[1];
      while (1)
      {
         netwop = strtol(p, &n, 0) - 1;
         if (p == n)
            break;
         EpgDbFilterSetNetwop(pPiFilterContext, netwop);
         p = n;
      }

      if (p != argv[1])
         EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP);
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_NETWOP);

      PiListBox_Refresh();
      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for features menu: update the feature filter setting
//
static int SelectFeatures( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char *pUsage = "Usage: SelectFeatures ([<mask> <value>]{0..12})";
   int class, mask, value;
   char *p, *n;
   int result; 
   
   if (argc != 2)
   {  // illegal parameter count
      interp->result = (char *)pUsage;
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

      PiListBox_Refresh();
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for features menu:
// update the editorial and parental rating filter setting
//
static int SelectRating( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   int parental, editorial;
   int result; 
   
   if (argc != 3) 
   {  // wrong parameter count
      interp->result = "SelectRating parental editorial";
      result = TCL_ERROR; 
   }  
   else if ( Tcl_GetInt(interp, argv[1], &parental) ||
             Tcl_GetInt(interp, argv[2], &editorial) )
   {
      interp->result = "SelectRating: parental and editorial rating must be integers";
      result = TCL_ERROR; 
   }
   else
   {
      if (parental != 0)
      {
         EpgDbFilterEnable(pPiFilterContext, FILTER_PAR_RAT);
         EpgDbFilterSetParentalRating(pPiFilterContext, parental);
      }
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_PAR_RAT);

      if (editorial != 0)
      {
         EpgDbFilterEnable(pPiFilterContext, FILTER_EDIT_RAT);
         EpgDbFilterSetEditorialRating(pPiFilterContext, editorial);
      }
      else
         EpgDbFilterDisable(pPiFilterContext, FILTER_EDIT_RAT);

      PiListBox_Refresh();
      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// callback for entry widget: update the sub-string filter setting
//
static int SelectSubStr( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char *pUsage = "Usage: SelectSubStr <bTitle=0/1> <bDescr=0/1> <bCase=0/1> <substring>";
   int scope_title, scope_descr, case_ignore;
   int result; 
   
   if (argc != 5) 
   {  // wrong parameter count
      interp->result = (char *) pUsage;
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

      PiListBox_Refresh();
      result = TCL_OK; 
   }

   return result;
}

// ----------------------------------------------------------------------------
// Callback for Program-Index radio buttons
//
static int SelectProgIdx( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char *pUsage = "Usage: SelectProgIdx <first-index> <last-index>";
   int first, last;
   int result; 
   
   if ( (argc == 3)  &&
        !Tcl_GetInt(interp, argv[1], &first) &&
        !Tcl_GetInt(interp, argv[2], &last) )
   {  // set min and max index boundaries
      EpgDbFilterEnable(pPiFilterContext, FILTER_PROGIDX);
      EpgDbFilterSetProgIdx(pPiFilterContext, first, last);
      PiListBox_Refresh();
      result = TCL_OK; 
   }
   else if (argc == 1) 
   {  // no parameters -> disable filter
      EpgDbFilterDisable(pPiFilterContext, FILTER_PROGIDX);
      PiListBox_Refresh();
      result = TCL_OK; 
   }
   else
   {  // wrong parameter count
      interp->result = (char *) pUsage;
      result = TCL_ERROR; 
   }  

   return result;
}

// ----------------------------------------------------------------------------
// Reset all filters at once and return to 1st NOW item
//
static int PiFilter_Reset( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   EpgDbFilterDisable(pPiFilterContext, FILTER_ALL);

   // do a reset instead of refresh set cursor onto item #0
   PiListBox_Reset();

   return TCL_OK; 
}

// ----------------------------------------------------------------------------
// return the name according to a PDC theme index
//
static int GetPdcString( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   const char * name;
   int index;
   int result; 
   
   if (argc != 2) 
   {  // wrong parameter count
      interp->result = "GetPdcString: index arg expected";
      result = TCL_ERROR; 
   }  
   else if ( Tcl_GetInt(interp, argv[1], &index) )
   {
      interp->result = "pdc_themes: index must be integers";
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
   const char *pUsage = "Usage: C_CreateSeriesNetwopMenu <menu-path>";
   const AI_BLOCK *pAiBlock;
   FILTER_CONTEXT *fc;
   char  subname[100];
   uchar netwop;
   int result;

   if (argc != 2) 
   {  // wrong # of args for this TCL cmd -> display error msg
      interp->result = (char *) pUsage;
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
               sprintf(subname, "%s.netwop_%d", argv[1], netwop);
               sprintf(comm, "%s add cascade -label {%s} -menu %s\n"
                             "if {![info exist dynmenu_posted(%s)] || ($dynmenu_posted(%s) == 0)} {\n"
                             "   menu %s -postcommand {PostDynamicMenu %s C_CreateSeriesMenu}\n"
                             "}\n",
                             argv[1], AI_GET_NETWOP_NAME(pAiBlock, netwop), subname,
                             subname, subname,
                             subname, subname);
               eval_check(interp, comm);
            }
         }
      }
      EpgDbFilterDestroyContext(fc);
      EpgDbLockDatabase(dbc, FALSE);
   }  

   return result;
}

// ----------------------------------------------------------------------------
// Create the series sub-menu
//
static int CreateSeriesMenu( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char *pUsage = "Usage: C_CreateSeriesMenu <menu-path.netwop_#>";
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
      interp->result = (char *) pUsage;
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
            do
            {
               series = 0;
               for (index=0; index < pPiBlock->no_themes; index++)
               {
                  series = pPiBlock->themes[index];
                  if ((series > 0x80) && (usedSeries[series - 0x80] == FALSE))
                  {
                     usedSeries[series - 0x80] = TRUE;
                     sprintf(comm, "%s add checkbutton -label {%s} -variable series_sel(%d) -command {SelectSeries %d}\n",
                                   argv[1],
                                   PI_GET_TITLE(pPiBlock),
                                   netwop * 0x100 + series,
                                   netwop * 0x100 + series);
                     eval_check(interp, comm);
                     //printf("%s 0x%02x - %s\n", netname, series, PI_GET_TITLE(pPiBlock));
                     break;
                  }
               }
               pPiBlock = EpgDbSearchNextPi(dbc, fc, pPiBlock);
            }
            while (pPiBlock != NULL);
         }
      }
      EpgDbFilterDestroyContext(fc);
      EpgDbLockDatabase(dbc, FALSE);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Create navigation menu
//
static int CreateNi( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char *pUsage = "Usage: C_CreateNi <menu-path>";
   const NI_BLOCK *pNiBlock;
   const EVENT_ATTRIB *pEv;
   uchar *evName;
   char *p, subname[100];
   int blockno, i;
   int result;

   if (argc != 2) 
   {  // wrong # of args for this TCL cmd -> display error msg
      interp->result = (char *) pUsage;
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
                          "%s add command -label Reset -command {ResetFilterState; C_ResetFilter}\n",
                          argv[1], argv[1]);
            eval_check(interp, comm);
         }
      }
      else
      {
         debug1("Create-Ni: blockno=%d not found", blockno);
         if (blockno == 1)
         {  // no NI available -> at least show "Reset" command
            sprintf(comm, "%s add command -label Reset -command {ResetFilterState; C_ResetFilter}\n", argv[1]);
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
   const char *pUsage = "Usage: C_SelectNi <menu-path>";
   const NI_BLOCK *pNiBlock;
   const EVENT_ATTRIB *pEv;
   NI_FILTER_STATE niState;
   char *p, *n;
   int blockno, index, attrib;
   int result;

   if (argc != 2) 
   {  // wrong # of args for this TCL cmd -> display error msg
      interp->result = (char *) pUsage;
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
   uchar netwop;

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
         sprintf(comm, ".all.netwops.list delete 1 end; .all.netwops.list selection set 0");
         eval_check(interp, comm);

         for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
         {
            sprintf(comm, ".all.netwops.list insert end \"%s\"\n", AI_GET_NETWOP_NAME(pAiBlock, netwop));
            eval_check(interp, comm);
         }

         sprintf(comm, ".all.netwops.list configure -height %d\n", pAiBlock->netwopCount + 1);
         eval_check(interp, comm);

         EpgDbFilterDisable(pPiFilterContext, FILTER_NETWOP);
         PiListBox_Refresh();
      }
      EpgDbLockDatabase(dbc, FALSE);
   }
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
      Tcl_CreateCommand(interp, "C_SelectRating", SelectRating, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectSubStr", SelectSubStr, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectProgIdx", SelectProgIdx, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ResetFilter", PiFilter_Reset, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_GetPdcString", GetPdcString, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_CreateSeriesNetwopMenu", CreateSeriesNetwopMenu, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_CreateSeriesMenu", CreateSeriesMenu, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_CreateNi", CreateNi, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SelectNi", SelectNi, (ClientData) NULL, NULL);

      sprintf(comm, "GenerateFilterMenues %d %d\n"
                    "ResetFilterState\n"
                    "InitThemesListbox\n",
                    THEME_CLASS_COUNT, FEATURE_CLASS_COUNT);
      eval_check(interp, comm);
   }
   else
      debug0("PiFilter-Create: commands are already created");

   // create and initialize the filter context
   pPiFilterContext = EpgDbFilterCreateContext();

   PiFilter_UpdateNetwopList(NULL);
}

