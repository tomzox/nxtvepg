/*
 *  Nextview GUI: PI listbox
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
 *    Implements a listing of programmes in a Nextview EPG, using a
 *    Tk text widget. The user can perform all the usual scrolling
 *    operations. The widget is tightly connected to the database,
 *    i.e. expired programmes are immediately removed and newly
 *    acquired items are added to the display, if they match the
 *    current filter settings (which are managed by the PiFilter
 *    module)
 *
 *  Author: Tom Zoerner
 *
 *  $Id: pilistbox.c,v 1.61 2001/08/21 19:46:59 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmerge.h"
#include "epgctl/epgacqctl.h"
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgctl/epgctxctl.h"
#include "epgui/pifilter.h"
#include "epgui/pioutput.h"
#include "epgui/pilistbox.h"


// ----------------------------------------------------------------------------
// private data structures
// describing the current state of the widget

typedef struct
{
   uchar    netwop_no;
   ulong    start_time;
} PIBOX_ENTRY;

typedef enum 
{
   PIBOX_NOT_INIT,
   PIBOX_MESSAGE,
   PIBOX_LIST
} PIBOX_STATE;

#define PIBOX_DEFAULT_HEIGHT 25
#define PIBOX_INVALID_CURPOS -1
PIBOX_ENTRY * pibox_list = NULL; // list of all items in the window
int         pibox_height;        // number of available lines in the widget
int         pibox_count;         // number of items currently visible
int         pibox_curpos;        // relative cursor position in window or -1 if window empty
int         pibox_max;           // total number of matching items
int         pibox_off;           // number of currently invisible items above the window
bool        pibox_resync;        // if TRUE, need to evaluate off and max
PIBOX_STATE pibox_state = PIBOX_NOT_INIT;  // listbox state
EPGDB_STATE pibox_dbstate;       // database state

#define dbc pUiDbContext         // internal shortcut

// ----------------------------------------------------------------------------
// Check the consistancy of the listbox state with the database and text widget
// - if an inconsistancy is found, an assertion failure occurs, which either
//   raises a SEGV (developer release), adds a message to EPG.LOG (tester release)
//   or is ignored (production release). For more details, see debug.[ch]
//
#if DEBUG_SWITCH == ON
static bool PiListBox_ConsistancyCheck( void )
{
   const PI_BLOCK *pPiBlock;
   uint i;

   if (pibox_state == PIBOX_LIST)
   {
      dprintf4("Check: cnt=%d max=%d off=%d cur=%d\n", pibox_count, pibox_max, pibox_off, pibox_curpos);

      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
      if (pPiBlock != NULL)
      {
         assert((pibox_count > 0) && (pibox_count <= pibox_height));
         assert((pibox_max < pibox_height) || (pibox_count == pibox_height));
         assert((pibox_max >= pibox_height) || (pibox_off == 0));
         assert(pibox_off + pibox_count <= pibox_max);
         assert(pibox_curpos < pibox_count);

         if (pibox_resync == FALSE)
         {
            // count items before visible window
            for (i=0; (i < pibox_off) && (pPiBlock != NULL); i++)
               pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            assert(pPiBlock != NULL);

            // check items inside the visible window
            for (i=0; (i < pibox_count) && (pPiBlock != NULL); i++)
            {
               assert( (pibox_list[i].netwop_no  == pPiBlock->netwop_no) &&
                       (pibox_list[i].start_time == pPiBlock->start_time) );
               pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            }
            assert((pPiBlock != NULL) ^ (pibox_off + pibox_count == pibox_max));

            // count items after the visible window
            for (i=pibox_off + pibox_count; (i < pibox_max) && (pPiBlock != NULL); i++)
               pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            assert((i == pibox_max) && (pPiBlock == NULL));
         }
         else
         {  // current off and max counts are marked invalid -> only check visible items
            pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].start_time, pibox_list[0].netwop_no);
            for (i=0; (i < pibox_count) && (pPiBlock != NULL); i++)
            {
               assert( (pibox_list[i].netwop_no  == pPiBlock->netwop_no) &&
                       (pibox_list[i].start_time == pPiBlock->start_time) );
               pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            }
            assert(i >= pibox_count);
         }

         // check cursor position
         sprintf(comm, ".all.pi.list.text tag ranges sel\n");
         if (eval_check(interp, comm) == TCL_OK)
         {
            sprintf(comm, "%d.0 %d.0", pibox_curpos + 1, pibox_curpos + 2);
            assert(!strcmp(interp->result, comm));
         }

         // check number of lines in text widget
         sprintf(comm, ".all.pi.list.text index end\n");
         if (eval_check(interp, comm) == TCL_OK)
         {
            sprintf(comm, "%d.0", pibox_count + 2);
            assert(!strcmp(interp->result, comm));
         }
      }
      else
      {
         assert((pibox_count == 0) && (pibox_max == 0) && (pibox_off == 0));
         assert(pibox_curpos == PIBOX_INVALID_CURPOS);
      }
      EpgDbLockDatabase(dbc, FALSE);
   }

   return TRUE;  //dummy for assert()
}
#endif //DEBUG_SWITCH == ON

// ----------------------------------------------------------------------------
// Update the listbox state according to database and acq state
//
void PiListBox_UpdateState( EPGDB_STATE newDbState )
{
   assert(pUiDbContext != NULL);

   if (pibox_dbstate != newDbState)
   {
      pibox_dbstate = newDbState;

      switch (pibox_dbstate)
      {
         case EPGDB_WAIT_SCAN:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "Please wait until the provider scan has finished."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_PROV_SCAN:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "There are no providers for Nextview data known yet. "
                          "Please start a provider scan from the Configure menu "
                          "(you only have to do this once). "
                          "During the scan all TV channels are checked for Nextview "
                          "transmissions. Among the found ones you then can select your "
                          "favorite provider and start reading in its TV programme database."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_PROV_WAIT:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "There are no providers for Nextview data known yet. "
                          "Since you do not use the TV tuner as video input source, "
                          "you have to select a provider's channel at the external "
                          "video source by yourself. For a list of possible channels "
                          "see the README file or the nxtvepg Internet Homepage."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_PROV_SEL:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "Please select your favorite provider from the Configure menu."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_ACQ_NO_FREQ:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "The database of the currently selected provider is empty "
                          "and the provider's TV channel is unknown. Please do start "
                          "a provider scan from the Configure menu to find out the frequency "
                          "(this has to be done only this one time). Else, please make sure "
                          "you have tuned the TV channel of the selected Nextview provider "
                          "or select a different provider from the Configure menu."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_ACQ_NO_TUNER:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "The database of the currently selected provider is empty. "
                          "Since you have not selected the internal TV tuner as input source, "
                          "you have to make sure yourself that "
                          "you have tuned the TV channel of the this provider "
                          "or select a different provider from the Configure menu."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_ACQ_ACCESS_DEVICE:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "The database of the currently selected provider is empty. "
                          "The TV channel could not be changed because the video device is "
                          "kept busy by another application. Therefore you have to make sure "
                          "you have tuned the TV channel of the selected Nextview provider "
                          "or select a different provider from the Configure menu."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_ACQ_PASSIVE:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "The database of the currently selected provider is empty. "
                          "You have configured acquisition mode to passive, so data for "
                          "this provider can only be acquired if you use another application "
                          "to tune to it's TV channel."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_ACQ_WAIT:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "The database of the currently selected provider is empty. "
                          "Please wait a few seconds until Nextview data is received "
                          "or select a different provider from the Configure menu."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_ACQ_OTHER_PROV:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "The database of the currently selected provider is empty. "
                          "This provider is not in your selection for acquisition! "
                          "Please choose a different provider, or change the "
                          "acquisition mode from the Configure menu."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_EMPTY:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "The database of the currently selected provider is empty. "
                          "Start the acquisition from the Control menu or select a "
                          "different provider from the Configure menu."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_PREFILTERED_EMPTY:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "None of the programmes in this database match your network "
                          "preselection. Either add more networks for this provider or "
                          "select a different one in the Configure menus. Starting the "
                          "acquisition might also help."
                          "\n}\n");
            eval_check(interp, comm);
            break;

         case EPGDB_OK:
            // db is now ready -> display the data
            pibox_state = PIBOX_LIST;
            PiListBox_Reset();
            break;

         default:
            SHOULD_NOT_BE_REACHED;
            break;
      }

      if (pibox_dbstate != EPGDB_OK)
      {
         sprintf(comm, ".all.pi.list.sc set 0.0 1.0\n"
                       ".all.pi.info.text configure -state normal\n"
                       ".all.pi.info.text delete 1.0 end\n"
                       ".all.pi.info.text configure -state disabled\n");
         eval_check(interp, comm);
      }
   }
}

// ----------------------------------------------------------------------------
// Append one item to PI listing in the text widget
//
static void PiListBox_InsertItem( const PI_BLOCK *pPiBlock, int pos )
{
   uint idx, off;
   time_t now;
   int len;

   sprintf(comm, ".all.pi.list.text insert %d.0 {", pos + 1);

   idx = 0;
   off = strlen(comm);

   while ( (len = PiOutput_PrintColumnItem(pPiBlock, idx, comm + off)) >= 0 )
   {
      off += len;
      comm[off++] = '\t';
      comm[off] = 0;

      idx += 1;
   }
   // remove trailing tab character
   if (idx > 0)
      off -= 1;

   now = time(NULL);
   sprintf(comm + off, "\n} %s\n", /* ((pPiBlock->stop_time <= now) && ((pPiFilterContext->enabledFilters & FILTER_EXPIRE_TIME) == FALSE))  ? "past" : */
                                   ((pPiBlock->start_time <= now) ? "now" : "then") );
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Callback function for PiOutput-AppendShortAndLongInfoText
//
static void PiListBox_AppendInfoTextCb( void *fp, const char * pShortInfo, bool insertSeparator, const char * pLongInfo )
{
   assert(fp == NULL);

   if (pShortInfo != NULL)
   {
      if (pLongInfo != NULL)
         sprintf(comm, ".all.pi.info.text insert end {%s%s%s}\n", pShortInfo, (insertSeparator ? "\n" : ""), pLongInfo);
      else
         sprintf(comm, ".all.pi.info.text insert end {%s}\n", pShortInfo);
   }
   else
   {  // separator between info texts of different providers
      sprintf(comm, ".all.pi.info.text insert end {\n\n} title\n"
                    ".all.pi.info.text image create {end - 2 line} -image bitmap_line\n");
   }
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Display short and long info for currently selected item
//
static void PiListBox_UpdateInfoText( bool keepView )
{  
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK *pPiBlock;
   const uchar *pCfNetname;
   uchar date_str[20], start_str[20], stop_str[20], cni_str[7];
   uchar view_buf[20];
   int len;
   
   if (keepView)
   {  // remember the viewable fraction of the text
      sprintf(comm, "lindex [.all.pi.info.text yview] 0\n");
      eval_check(interp, comm);
      strncpy(view_buf, interp->result, 20-1);
      view_buf[20-1] = 0;
   }

   sprintf(comm, ".all.pi.info.text configure -state normal\n"
                 ".all.pi.info.text delete 1.0 end\n");
   eval_check(interp, comm);

   if (pibox_curpos >= 0)
   {
      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      pPiBlock = EpgDbSearchPi(dbc, NULL, pibox_list[pibox_curpos].start_time, pibox_list[pibox_curpos].netwop_no);

      if ((pAiBlock != NULL) && (pPiBlock != NULL) && (pPiBlock->netwop_no < pAiBlock->netwopCount))
      {
         sprintf(comm, ".all.pi.info.text insert end {%s\n} title\n", PI_GET_TITLE(pPiBlock));
         eval_check(interp, comm);

         // now add a feature summary to the bottom label field
         strcpy(comm, ".all.pi.info.text insert end {");

         // append theme list
         len = strlen(comm);
         PiOutput_AppendCompressedThemes(pPiBlock, comm + len, sizeof(comm) - (len + 1));

         // append features list
         strcat(comm, " (");
         PiOutput_AppendFeatureList(pPiBlock, comm + strlen(comm));
         // remove opening bracket if nothing follows
         len = strlen(comm);
         if ((comm[len - 2] == ' ') && (comm[len - 1] == '('))
            comm[len - 2] = 0;
         else
            strcat(comm, ")");

         // finalize theme & feature string
         strcat(comm, "\n} features\n");
         eval_check(interp, comm);

         // print network name, start- & stop-time, short info
         strftime(date_str, 19, " %a %d.%m", localtime(&pPiBlock->start_time));
         strftime(start_str, 19, "%H:%M", localtime(&pPiBlock->start_time));
         strftime(stop_str, 19, "%H:%M", localtime(&pPiBlock->stop_time));

         sprintf(cni_str, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
         pCfNetname = Tcl_GetVar2(interp, "cfnetnames", cni_str, TCL_GLOBAL_ONLY);
         if (pCfNetname == NULL)
            pCfNetname = AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no);

         sprintf(comm, ".all.pi.info.text insert end {%s, %s-%s%s: } bold\n",
                       pCfNetname,
                       start_str, stop_str,
                       (((pPiBlock->start_time - time(NULL)) > 12*60*60) ? (char *)date_str : "") );
         eval_check(interp, comm);

         PiOutput_AppendShortAndLongInfoText(pPiBlock, PiListBox_AppendInfoTextCb, NULL);
      }
      else
         debug2("PiListBox-UpdateInfoText: selected block start=%ld netwop=%d not found\n", pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);

      EpgDbLockDatabase(dbc, FALSE);
   }

   sprintf(comm, ".all.pi.info.text configure -state disabled\n");
   eval_check(interp, comm);

   if (keepView)
   {  // set the view back to its previous position
      sprintf(comm, ".all.pi.info.text yview moveto %s\n", view_buf);
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// inform the vertical scrollbar about the start offset and viewable fraction
//
static void PiListBox_AdjustScrollBar( void )
{
   if (pibox_max == 0)
      sprintf(comm, ".all.pi.list.sc set 0.0 1.0\n");
   else
      sprintf(comm, ".all.pi.list.sc set %f %f\n", (float)pibox_off / pibox_max, (float)(pibox_off + pibox_height) / pibox_max);
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// tag the selected line (by background color and border)
//
static void PiListBox_ShowCursor( void )
{
   time_t now = time(NULL);
   const char * pColor;

   //if (pibox_list[pibox_curpos].stop_time <= now)
   //   pColor = "cursor_bg_past";
   //else
   if (pibox_list[pibox_curpos].start_time <= now)
      pColor = "cursor_bg_now";
   else
      pColor = "cursor_bg";

   sprintf(comm, ".all.pi.list.text tag configure sel -background $%s\n"
                 ".all.pi.list.text tag add sel %d.0 %d.0\n",
                 pColor, pibox_curpos + 1, pibox_curpos + 2);
   eval_global(interp, comm);
}

// ----------------------------------------------------------------------------
// Fill the listbox starting with the first item in the database
//
void PiListBox_Reset( void )
{
   const PI_BLOCK *pPiBlock;

   if (pibox_state != PIBOX_LIST)
      return;

   sprintf(comm, ".all.pi.list.text delete 1.0 end\n"
                 ".all.pi.info.text configure -state normal\n"
                 ".all.pi.info.text delete 1.0 end\n"
                 ".all.pi.info.text configure -state disabled\n");
   eval_check(interp, comm);

   EpgDbLockDatabase(dbc, TRUE);
   pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
   pibox_resync = FALSE;
   pibox_count = 0;
   pibox_max = 0;
   pibox_off = 0;
   if (pPiBlock != NULL)
   {
      do
      {
	 if (pibox_count < pibox_height)
	 {
	    pibox_list[pibox_count].netwop_no  = pPiBlock->netwop_no;
	    pibox_list[pibox_count].start_time = pPiBlock->start_time;
	    pibox_count += 1;

	    PiListBox_InsertItem(pPiBlock, pibox_count);
	 }

         pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
	 pibox_max += 1;
      }
      while (pPiBlock != NULL);

      // set the cursor on the first item
      pibox_curpos = 0;
      pibox_off    = 0;
      PiListBox_ShowCursor();

      // display short and long info
      PiListBox_UpdateInfoText(FALSE);
   }
   else
      pibox_curpos = PIBOX_INVALID_CURPOS;

   EpgDbLockDatabase(dbc, FALSE);
   assert(PiListBox_ConsistancyCheck());

   // inform the vertical scrollbar about the viewable fraction
   PiListBox_AdjustScrollBar();
}

// ----------------------------------------------------------------------------
// Re-Sync the listbox with the database
//
void PiListBox_Refresh( void )
{
   const PI_BLOCK *pPiBlock, *pPrev, *pNext;
   uchar last_netwop;
   ulong min_time;
   int i;

   if (pibox_state != PIBOX_LIST)
   {
      // do nothing
   }
   else if ((pibox_off == 0) && (pibox_curpos <= 0))
   {  // listing starts with the very first item -> keep that unchanged
      PiListBox_Reset();
   }
   else
   {
      sprintf(comm, ".all.pi.list.text delete 1.0 end\n");
      eval_check(interp, comm);

      min_time    = pibox_list[pibox_curpos].start_time;
      last_netwop = pibox_list[pibox_curpos].netwop_no;
      pibox_resync = FALSE;
      pibox_off = 0;

      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
      if (pPiBlock != NULL)
      {
         while ( (pPiBlock->start_time < min_time) ||
                 ((pPiBlock->start_time == min_time) && (pPiBlock->netwop_no < last_netwop)) )
         {
            pNext = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            if ( (pNext == NULL) ||
                 (pNext->start_time > min_time) ||
                 ((pNext->start_time == min_time) && (pNext->netwop_no > last_netwop)) )
               break;

            pPiBlock = pNext;
            pibox_off += 1;
         }

         i = 0;
         while ( ((pPrev = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock)) != NULL) &&
                 (i < pibox_curpos) && (pibox_off > 0) )
         {
            pPiBlock = pPrev;
            pibox_off -= 1;
            i++;
         }
         pibox_curpos = i;

         pibox_count = 0;
         pibox_max = pibox_off;
         do
         {
            if (pibox_count < pibox_height)
            {
               pibox_list[pibox_count].netwop_no  = pPiBlock->netwop_no;
               pibox_list[pibox_count].start_time = pPiBlock->start_time;
               pibox_count += 1;

               PiListBox_InsertItem(pPiBlock, pibox_count);
            }

            pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            pibox_max += 1;
         }
         while (pPiBlock != NULL);

         if ((pibox_count < pibox_height) && (pibox_max > pibox_count))
         {  // not enough items found after the cursor -> shift window upwards
            pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].start_time, pibox_list[0].netwop_no);
            while ( (pibox_count < pibox_height) &&
                    ((pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock)) != NULL) )
            {
               dprintf4("shift up: count=%d, max=%d: insert block start=%ld net=%d\n", pibox_count, pibox_max, pPiBlock->start_time, pPiBlock->netwop_no);
               for (i=pibox_count-1; i>=0; i--)
                  pibox_list[i + 1] = pibox_list[i];
               pibox_list[0].netwop_no  = pPiBlock->netwop_no;
               pibox_list[0].start_time = pPiBlock->start_time;
               PiListBox_InsertItem(pPiBlock, 0);

               assert(pibox_off > 0);
               pibox_off -= 1;
               pibox_curpos += 1;
               pibox_count += 1;
            }
            assert((pibox_count >= pibox_height) || ((pibox_off == 0) && (pPiBlock == NULL)));
         }

         if (pibox_curpos >= pibox_count)
         {  // set the cursor on the last item
            pibox_curpos = pibox_count - 1;
         }
         PiListBox_ShowCursor();

         // display short and long info
         if ( (min_time    == pibox_list[pibox_curpos].start_time) &&
              (last_netwop == pibox_list[pibox_curpos].netwop_no) )
         {  // still the same PI under the cursor -> keep the short-info view unchanged
            PiListBox_UpdateInfoText(TRUE);
         }
         else
            PiListBox_UpdateInfoText(FALSE);
      }
      else
      {
         pibox_curpos = PIBOX_INVALID_CURPOS;
         pibox_count = 0;
         pibox_max = 0;
         // clear the short-info window
         sprintf(comm, ".all.pi.info.text configure -state normal\n"
                       ".all.pi.info.text delete 1.0 end\n"
                       ".all.pi.info.text configure -state disabled\n");
         eval_check(interp, comm);
      }

      EpgDbLockDatabase(dbc, FALSE);
      assert(PiListBox_ConsistancyCheck());

      // inform the vertical scrollbar about the viewable fraction
      PiListBox_AdjustScrollBar();
   }
}

// ----------------------------------------------------------------------------
// move the visible window - interface to the scrollbar
//
static int PiListBox_Scroll( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   int delta, i, j;
   const PI_BLOCK *pPiBlock;
   
   if ((argc == 2+1) && !strcmp(argv[1], "moveto"))
   {
      //  ----  Move to absolute position  ----

      if (pibox_max > pibox_height)
      {
         uint old_netwop;
         ulong old_start;

	 delta = (int)(pibox_max * atof(argv[2]));
	 if (delta < 0)
	    delta = 0;
	 else if (delta > pibox_max - pibox_height)
	    delta = pibox_max - pibox_height;

	 sprintf(comm, ".all.pi.list.text delete 1.0 end\n"
                       ".all.pi.info.text configure -state normal\n"
                       ".all.pi.info.text delete 1.0 end\n"
                       ".all.pi.info.text configure -state disabled\n");
	 eval_check(interp, comm);

         if (pibox_curpos >= 0)
         {
            old_start  = pibox_list[pibox_curpos].start_time;
            old_netwop = pibox_list[pibox_curpos].netwop_no;
         }
         else
         {
            old_start  = 0;
            old_netwop = 0xff;
         }

	 pibox_count = 0;
	 pibox_max = 0;
	 EpgDbLockDatabase(dbc, TRUE);
	 pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
	 if (pPiBlock != NULL)
	 {
	    do
	    {
	       if ((pibox_max >= delta) && (pibox_count < pibox_height))
	       {
		  pibox_list[pibox_count].netwop_no  = pPiBlock->netwop_no;
		  pibox_list[pibox_count].start_time = pPiBlock->start_time;
		  pibox_count += 1;

		  PiListBox_InsertItem(pPiBlock, pibox_count);
	       }

	       pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
	       pibox_max += 1;
	    }
	    while (pPiBlock != NULL);

            // keep the cursor on the same item as long as its visable
            // then keep the cursor at the top or bottom item
            for (i=0; i < pibox_count; i++)
               if ( (old_netwop == pibox_list[i].netwop_no) &&
                    (old_start == pibox_list[i].start_time) )
                  break;
            if (i < pibox_count)
               pibox_curpos = i;
            else if (delta < pibox_off)
               pibox_curpos = pibox_count - 1;
            else
               pibox_curpos = 0;
            pibox_off = delta;
	    PiListBox_ShowCursor();

	    // display short and long info
	    PiListBox_UpdateInfoText(FALSE);
	 }
	 else
	 {
	    pibox_off = 0;
	    pibox_curpos = PIBOX_INVALID_CURPOS;
	 }
	 EpgDbLockDatabase(dbc, FALSE);

	 // adjust the vertical scrollbar
	 PiListBox_AdjustScrollBar();

         assert(PiListBox_ConsistancyCheck());
      }
   }
   else if ((argc == 3+1) && !strcmp(argv[1], "scroll"))
   {
      delta = atoi(argv[2]);
      if (!strcmp(argv[3], "pages"))
      {
	 const PI_BLOCK ** pNewPiBlock = xmalloc(pibox_height * sizeof(PI_BLOCK *));
	 int new_count = 0;

	 if (delta > 0)
	 {
	    //  ----  Scroll one page down  ----

	    EpgDbLockDatabase(dbc, TRUE);
            if (pibox_count >= pibox_height)
            {
               pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
               if (pPiBlock != NULL)
               {
                  while ( (new_count < pibox_height) &&
                          ((pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock)) != NULL) )
                  {
                     pNewPiBlock[new_count] = pPiBlock;
                     new_count += 1;
                  }

                  if (pPiBlock == NULL)
                  {  // correct max-value, if neccessary
                     assert(pibox_max == pibox_off + pibox_count + new_count);
                     pibox_max = pibox_off + pibox_count + new_count;
                  }
               }
               else
                  debug2("PiListBox-Scroll: last listed block start=%ld netwop=%d not found\n", pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
            }
            else
               new_count = 0;

            if (new_count > 0)
            {
               // remove cursor, if on one of the remaining items
               if (pibox_curpos >= new_count)
               {
                  sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
                  eval_check(interp, comm);
               }
               // remove old text
               sprintf(comm, ".all.pi.list.text delete 1.0 %d.0\n", new_count + 1);
               eval_check(interp, comm);

               for (i=new_count; i < pibox_height; i++)
                  pibox_list[i - new_count] = pibox_list[i];
               for (i=pibox_height-new_count, j=0; i < pibox_height; i++, j++)
               {
                  pibox_list[i].netwop_no  = pNewPiBlock[j]->netwop_no;
                  pibox_list[i].start_time = pNewPiBlock[j]->start_time;
                  PiListBox_InsertItem(pNewPiBlock[j], i);
               }
               pibox_off += new_count;

               if (new_count > pibox_curpos)
               {  // cursor moves downwards by number of items in widget
                  // i.e. during a full page scroll it stays at the same position
                  pibox_curpos = pibox_curpos + pibox_height - new_count;
               }
               else
               {  // end of listing reached -> set cursor onto the last item
                  pibox_curpos = pibox_height - 1;
               }
               PiListBox_ShowCursor();
               PiListBox_UpdateInfoText(FALSE);

               // inform the vertical scrollbar about the start offset and viewable fraction
               PiListBox_AdjustScrollBar();
            }
            else if (pibox_curpos < pibox_count - 1)
            {  // no items below -> set cursor to last item
               sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
               eval_check(interp, comm);
               pibox_curpos = pibox_count - 1;
               PiListBox_ShowCursor();
               PiListBox_UpdateInfoText(FALSE);
            }

	    EpgDbLockDatabase(dbc, FALSE);
            assert(PiListBox_ConsistancyCheck());
	 }
	 else if (pibox_count > 0)
	 {
	    //  ----  Scroll one page up  ----

	    EpgDbLockDatabase(dbc, TRUE);
	    pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].start_time, pibox_list[0].netwop_no);
	    if (pPiBlock != NULL)
	    {
	       new_count = 0;
	       while ( (new_count < pibox_height) &&
	               ((pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock)) != NULL) )
	       {
		  pNewPiBlock[new_count] = pPiBlock;
		  new_count += 1;
	       }

	       if (pPiBlock == NULL)
	       {  // correct offset-value, if neccessary
	          assert(pibox_off == new_count);
		  pibox_off = new_count;
	       }

	       if (new_count > 0)
	       {
		  // remove cursor, if on one of the remaining items
		  if ((pibox_curpos < pibox_height - new_count) && (pibox_curpos >= 0))
		  {
		     sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
		     eval_check(interp, comm);
		  }
		  else
		     i=0; //breakpoint
		  // remove old text at the bottom of the widget
		  sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", pibox_height - new_count + 1, pibox_height + 1);
		  eval_check(interp, comm);

		  for (i=pibox_height-1; i >= new_count; i--)
		     pibox_list[i] = pibox_list[i - new_count];
		  // insert new items at the top in reverse order
		  for (i=0, j=new_count-1; i < new_count; i++, j--)
		  {
		     pibox_list[j].netwop_no  = pNewPiBlock[i]->netwop_no;
		     pibox_list[j].start_time = pNewPiBlock[i]->start_time;
		     PiListBox_InsertItem(pNewPiBlock[i], 0);
		  }
		  pibox_off -= new_count;

		  if (new_count >= pibox_height - pibox_curpos)
		  {  // cursor moves upwards by number of items in widget
		     // i.e. during a full page scroll it stays at the same position
		     pibox_curpos = new_count + pibox_curpos - pibox_height;
		  }
		  else
		  {  // start of listing reached -> set cursor onto the first item
		     pibox_curpos = 0;
		  }
		  PiListBox_ShowCursor();
		  PiListBox_UpdateInfoText(FALSE);

		  // inform the vertical scrollbar about the start offset and viewable fraction
		  PiListBox_AdjustScrollBar();
	       }
	       else if (pibox_curpos > 0)
	       {  // no items above -> set cursor to first item
		  sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
		  eval_check(interp, comm);
		  pibox_curpos = 0;
		  PiListBox_ShowCursor();
		  PiListBox_UpdateInfoText(FALSE);
	       }
	    }
	    else
	       debug2("PiListBox-Scroll: first listed block start=%ld netwop=%d not found\n", pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
	    EpgDbLockDatabase(dbc, FALSE);
            assert(PiListBox_ConsistancyCheck());
	 }
	 xfree((void*)pNewPiBlock);
      }
      else if (!strcmp(argv[3], "units"))
      {
	 if ((delta > 0) && (pibox_count >= pibox_height))
	 {
	    //  ----  Scroll one item down  ----

	    EpgDbLockDatabase(dbc, TRUE);
	    pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
	    if (pPiBlock != NULL)
	    {
	       pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
	       if (pPiBlock != NULL)
	       {
		  sprintf(comm, ".all.pi.list.text delete 1.0 2.0\n");
		  eval_check(interp, comm);
		  for (i=1; i < pibox_height; i++)
		     pibox_list[i - 1] = pibox_list[i];
		  pibox_list[pibox_count-1].netwop_no  = pPiBlock->netwop_no;
		  pibox_list[pibox_count-1].start_time = pPiBlock->start_time;
		  pibox_off += 1;

		  PiListBox_InsertItem(pPiBlock, pibox_height);
		  if (pibox_curpos == 0)
		  {
		     PiListBox_ShowCursor();
		  }
		  else
		     pibox_curpos -= 1;
		  PiListBox_UpdateInfoText(FALSE);

		  // adjust the vertical scrollbar
		  PiListBox_AdjustScrollBar();
	       }
	    }
	    else
	       debug2("PiListBox-Scroll: last listed block start=%ld netwop=%d not found\n", pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
	    EpgDbLockDatabase(dbc, FALSE);
            assert(PiListBox_ConsistancyCheck());
	 }
	 else if (pibox_count >= pibox_height)
	 {
	    //  ----  Scroll one item up  ----

	    EpgDbLockDatabase(dbc, TRUE);
	    pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].start_time, pibox_list[0].netwop_no);
	    if (pPiBlock != NULL)
	    {
	       pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
	       if (pPiBlock != NULL)
	       {
		  sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", pibox_height, pibox_height+1);
		  eval_check(interp, comm);
		  for (i=pibox_height-2; i >= 0; i--)
		     pibox_list[i + 1] = pibox_list[i];
		  pibox_list[0].netwop_no  = pPiBlock->netwop_no;
		  pibox_list[0].start_time = pPiBlock->start_time;
		  pibox_off -= 1;

		  PiListBox_InsertItem(pPiBlock, 0);
		  if (pibox_curpos == pibox_height - 1)
		  {
		     PiListBox_ShowCursor();
		  }
		  else
		     pibox_curpos += 1;
		  PiListBox_UpdateInfoText(FALSE);

		  // inform the vertical scrollbar about the start offset and viewable fraction
		  PiListBox_AdjustScrollBar();
	       }
	    }
	    else
	       debug2("PiListBox-Scroll: first listed block start=%ld netwop=%d not found\n", pibox_list[0].start_time, pibox_list[0].netwop_no);
	    EpgDbLockDatabase(dbc, FALSE);
            assert(PiListBox_ConsistancyCheck());
	 }
      }
   }

   return TCL_OK;
}

static int PiListBox_CursorDown( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   if (argc == 1)
   {
      if (pibox_curpos == pibox_height-1)
      {  // last item is selected -> scroll down
	 const char *av[] = { "C_PiListBox_Scroll", "scroll", "1", "units" };

	 PiListBox_Scroll(ttp, interp, 4, (char **) av);

	 // set cursor to new last element
	 if (pibox_curpos == pibox_height-2)
	 {
	    // set cursor one line down, onto last item
	    sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	    eval_check(interp, comm);
	    pibox_curpos += 1;
	    PiListBox_ShowCursor();
            PiListBox_UpdateInfoText(FALSE);
	 }
      }
      else if (pibox_curpos < pibox_count-1)
      {  // move cursor one position down (text widget line count starts at 1)
	 sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	 eval_check(interp, comm);
	 pibox_curpos += 1;
	 PiListBox_ShowCursor();
	 PiListBox_UpdateInfoText(FALSE);
      }
      assert(PiListBox_ConsistancyCheck());
   }

   return TCL_OK;
}

static int PiListBox_CursorUp( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   if (argc == 1)
   {
      if (pibox_curpos == 0)
      {  // first item is selected -> scroll up
	 const char *av[] = { "C_PiListBox_Scroll", "scroll", "-1", "units" };
	 PiListBox_Scroll(ttp, interp, 4, (char **) av);

	 if (pibox_curpos == 1)
	 {
	    // set cursor one line down, onto last item
	    sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	    eval_check(interp, comm);
	    pibox_curpos = 0;
	    PiListBox_ShowCursor();
            PiListBox_UpdateInfoText(FALSE);
	 }
      }
      else
      {  // move cursor one position up (text widget line count starts at 1)
	 sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	 eval_check(interp, comm);
	 pibox_curpos -= 1;
	 PiListBox_ShowCursor();
	 PiListBox_UpdateInfoText(FALSE);
      }
      assert(PiListBox_ConsistancyCheck());
   }

   return TCL_OK; 
}

// ----------------------------------------------------------------------------
// Jump to the first PI of the given start time
// - Scrolls the listing so that the first PI that starts at or after the given
//   time is in the first row; if there are not enough PI to fill the listbox,
//   the downmost page is displayed and the cursor set on the first matching PI.
//   If there's no PI at all, the cursor is set on the very last PI in the listing.
// - Unlike with filter function, the PI before the given time are not suppressed,
//   they are just scrolled out (unless there are not enough PI after the start)
// - This function is to be used by time and date navigation menus.
//
static int PiListBox_GotoTime( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const PI_BLOCK *pPiBlock;
   time_t startTime;
   int i, delta, param;
   
   if ((argc == 2) && (pibox_state == PIBOX_LIST))
   {
      // Retrieve the start time from the function parameters
      if (strcmp(argv[1], "now") == 0)
      {  // start with currently running -> suppress start time restriction
         startTime = 0;
      }
      else if (strcmp(argv[1], "next") == 0)
      {  // start with the first that's not yet running -> start the next second
         startTime = time(NULL) + 1;
      }
      else if (Tcl_GetInt(interp, argv[1], &param) == TCL_OK)
      {  // absolute time given: convert parameter from local time to UTC
         startTime = param - EpgLtoGet();
      }
      else  // internal error
         startTime = 0;

      // Clear the listbox and the description window
      sprintf(comm, ".all.pi.list.text delete 1.0 end\n"
                    ".all.pi.info.text configure -state normal\n"
                    ".all.pi.info.text delete 1.0 end\n"
                    ".all.pi.info.text configure -state disabled\n");
      eval_check(interp, comm);

      pibox_resync = FALSE;
      pibox_count = 0;
      pibox_max = 0;
      pibox_off = 0;

      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
      if (pPiBlock != NULL)
      {
         // skip all PI before the requested start time
         while ((pPiBlock != NULL) && (pPiBlock->start_time < startTime))
         {
            pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            pibox_max += 1;
         }
         pibox_off = pibox_max;

         // fill the listbox with the matching PI; skip & count when full
         if (pPiBlock != NULL)
         {
            do
            {
               if (pibox_count < pibox_height)
               {
                  pibox_list[pibox_count].netwop_no  = pPiBlock->netwop_no;
                  pibox_list[pibox_count].start_time = pPiBlock->start_time;
                  pibox_count += 1;

                  PiListBox_InsertItem(pPiBlock, pibox_count);
               }

               pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
               pibox_max += 1;
            }
            while (pPiBlock != NULL);
         }

         // if the listbox could not be filled, scroll backwards
         if ((pibox_count < pibox_height) && (pibox_off > 0))
         {
            delta = pibox_height - pibox_count;
            if (delta > pibox_off)
               delta = pibox_off;

            if (pibox_count == 0)
            {  // no PI found at all -> scan backwards from the last PI in the db matching the filter
               pPiBlock = EpgDbSearchLastPi(dbc, pPiFilterContext);
            }
            else
            {  // at least one PI was found -> scn backwards from there
               pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].start_time, pibox_list[0].netwop_no);
               pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
            }

            // shift down the existing PI in the listbox
            for (i = pibox_count - 1; i >= 0; i--)
               pibox_list[i + delta] = pibox_list[i];

            // set the cursor on the first item that matched the start time, or the last PI if none matched
            pibox_off   -= delta;
            pibox_count += delta;
            if (delta >= pibox_height)
               pibox_curpos = pibox_height - 1;
            else
               pibox_curpos = delta;

            // fill the listbox backwards with the missing PI
            for (delta -= 1; delta >= 0; delta--)
            {
               pibox_list[delta].netwop_no  = pPiBlock->netwop_no;
               pibox_list[delta].start_time = pPiBlock->start_time;

               PiListBox_InsertItem(pPiBlock, 0);

               pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
            }
         }
         else
         {  // set the cursor on the first item
            pibox_curpos = 0;
         }
         PiListBox_ShowCursor();

         // display short and long info
         PiListBox_UpdateInfoText(FALSE);
      }
      else
         pibox_curpos = PIBOX_INVALID_CURPOS;

      EpgDbLockDatabase(dbc, FALSE);
      assert(PiListBox_ConsistancyCheck());

      // inform the vertical scrollbar about the viewable fraction
      PiListBox_AdjustScrollBar();
   }
   return TCL_OK; 
}

// ----------------------------------------------------------------------------
// Jump to the given program title
// - if the item is already visible, just place the cursor on it
// - if the item is not in the visible area, the window is scrolled
//   however scroll as little as possible (in contrary to the above GotoTime)
// - XXX currently no expired PI can be displayed (e.g. for VPS)
// - if netwop filtering is enabled, it's switched to the new PI's network
//
void PiListBox_GotoPi( const PI_BLOCK * pPiBlock )
{
   FILTER_CONTEXT *fc;
   time_t now;
   int idx;
   bool doRefresh;
   
   if (pibox_state == PIBOX_LIST)
   {
      now = time(NULL);
      doRefresh = FALSE;

      if ((pPiFilterContext->enabledFilters & ~FILTER_PERM) == FILTER_NETWOP)
      {  // network filter is enabled -> switch to the new network
         // but check before if the item will be visible with the new filter setting
         fc = EpgDbFilterCopyContext(pPiFilterContext);
         EpgDbFilterDisable(fc, FILTER_NETWOP | FILTER_NETWOP_PRE);
         if (EpgDbFilterMatches(dbc, fc, pPiBlock))
         {
            sprintf(comm, "ResetNetwops; SelectNetwopByIdx %d 1\n", pPiBlock->netwop_no);
            eval_check(interp, comm);
            // must refresh to display PI according to the new filter setting
            doRefresh = TRUE;
         }
         EpgDbFilterDestroyContext(fc);
      }

      // check if the given PI matches the current filter criteria
      if ( (pibox_count > 0) &&
           EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) )
      {
         if ( (pPiBlock->start_time < pibox_list[0].start_time) ||
              ((pPiBlock->start_time == pibox_list[0].start_time) && (pPiBlock->netwop_no < pibox_list[0].netwop_no)) )
         {  // PI lies before the visible area
            // ugly hack: overwrite the first item with where we want to jump to
            //            then set the cursor on if and force a refresh
            //            the refresh will fill the window around the cursor with the correct items
            pibox_list[0].start_time = pPiBlock->start_time;
            pibox_list[0].netwop_no  = pPiBlock->netwop_no;
            pibox_curpos = 0;
            PiListBox_Refresh();
            doRefresh = FALSE;
         }
         else
         if ( (pPiBlock->start_time > pibox_list[pibox_count - 1].start_time) ||
              ((pPiBlock->start_time == pibox_list[pibox_count - 1].start_time) && (pPiBlock->netwop_no > pibox_list[pibox_count - 1].netwop_no)) )
         {  // PI lies behind the visible area
            // ugly hack: overwrite the last item with where we want to jump to
            pibox_list[pibox_count - 1].start_time = pPiBlock->start_time;
            pibox_list[pibox_count - 1].netwop_no  = pPiBlock->netwop_no;
            pibox_curpos = pibox_count - 1;
            PiListBox_Refresh();
            doRefresh = FALSE;
         }
         else
         {  // PI falls into the visible area
            // search the exact position
            for (idx=0; idx < pibox_count; idx++)
            {
               if ( (pibox_list[idx].netwop_no == pPiBlock->netwop_no) &&
                    (pibox_list[idx].start_time == pPiBlock->start_time) )
               {
                  break;
               }
            }
            if (idx < pibox_count)
            {  // found NOW item in the current listing -> just set the cursor there
               sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
               eval_check(interp, comm);

               pibox_curpos = idx;

               // skip GUI redraw if complete refresh follows
               if (doRefresh == FALSE)
               {
                  PiListBox_ShowCursor();
                  PiListBox_UpdateInfoText(FALSE);
               }
            }
            else
               assert(doRefresh);  // not listed although it matches filters and is in the visible area!?
         }
      }

      // rebuild PI list around new cursor position now if filter was changed
      if (doRefresh)
         PiListBox_Refresh();

      assert(PiListBox_ConsistancyCheck());
   }
}

// ----------------------------------------------------------------------------
// set cursor onto specified item (after single mouse click)
//
static int PiListBox_SelectItem( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{  
   int newLine;

   if ((argc == 2) && (Tcl_GetInt(interp, argv[1], &newLine) == TCL_OK))
   {
      if ((newLine < pibox_count) && (newLine != pibox_curpos))
      {  
         // set cursor to new line (text widget line count starts at 1)
	 sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	 eval_check(interp, comm);
	 pibox_curpos = newLine;
	 PiListBox_ShowCursor();

	 // display short and long info
	 PiListBox_UpdateInfoText(FALSE);
      }
   }

   return TCL_OK; 
}

// ----------------------------------------------------------------------------
// Insert a newly acquired item into the listbox
//
void PiListBox_DbInserted( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pPiBlock )
{
   int pos;
   int i;

   if (usedDbc != dbc)
   {  // acquisition is using a different database from the UI
      return;
   }

   if (pPiFilterContext->enabledFilters & FILTER_PROGIDX)
   {  // when progidx filter is used insertion is too complicated, hence do complete refresh
      pibox_resync = TRUE;
      return;
   }

   if (pibox_state != PIBOX_LIST)
   {  // listbox was in an error state -> switch to normal mode
      UiControl_CheckDbState();
      return;
   }

   EpgDbLockDatabase(dbc, TRUE);
   if ( (pibox_state == PIBOX_LIST) &&
        EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) )
   {
      if ((pibox_count >= pibox_height) &&
	  ((pibox_off > 0) || (pibox_curpos > 0)) &&
	  ( (pibox_list[0].start_time > pPiBlock->start_time) ||
	    ((pibox_list[0].start_time == pPiBlock->start_time) &&
	     (pibox_list[0].netwop_no > pPiBlock->netwop_no)) ))
      {  // new block lies before first item (and listbox already full)
	 dprintf2("before visible area off=%d,max=%d\n", pibox_off, pibox_max);
         pibox_max += 1;
	 pibox_off += 1;
	 PiListBox_AdjustScrollBar();
         assert(PiListBox_ConsistancyCheck());
      }
      else
      {
	 for (pos=0; pos < pibox_count; pos++)
	 {
	    if ( (pibox_list[pos].start_time > pPiBlock->start_time) ||
		 ( (pibox_list[pos].start_time == pPiBlock->start_time) &&
		   (pibox_list[pos].netwop_no >= pPiBlock->netwop_no) ))
	       break;
	 }

	 if (pos < pibox_height)
	 {
            if ((pos == 0) && (pibox_off == 0) && (pibox_curpos <= 0))
	    {  // special case: empty or cursor on the very first item -> cursor stays on top
	       dprintf4("insert at top: curpos=%d,off=%d,max=%d,c=%d\n", pibox_curpos, pibox_off, pibox_max, pibox_count);
	       for (i=pibox_height-2; i >= 0; i--)
		  pibox_list[i + 1] = pibox_list[i];
	       pibox_list[0].netwop_no  = pPiBlock->netwop_no;
	       pibox_list[0].start_time = pPiBlock->start_time;
	       pibox_max += 1;

	       if (pibox_count >= pibox_height)
	       {
		  sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", pibox_count, pibox_count + 1);
		  eval_check(interp, comm);
	       }
	       else
		  pibox_count += 1;
	       PiListBox_InsertItem(pPiBlock, 0);
	       pibox_curpos = 0;  // may have been INVALID
	       sprintf(comm, ".all.pi.list.text tag remove sel 2.0 3.0\n");
	       eval_check(interp, comm);
	       PiListBox_ShowCursor();
	       PiListBox_UpdateInfoText(FALSE);
	       PiListBox_AdjustScrollBar();
               assert(PiListBox_ConsistancyCheck());
	    }
	    else if ((pos > 0) || (pibox_count < pibox_height))
	    {
	       // selected item must not be shifted out of viewable area
	       if ((pos > pibox_curpos) || (pibox_count < pibox_height))
	       {  // insert below the cursor position (or window not full)
		  dprintf5("insert below curpos=%d at pos=%d  off=%d,max=%d,c=%d\n", pibox_curpos, pos, pibox_off, pibox_max, pibox_count);
		  if (pibox_count >= pibox_height)
		  {  // box already full -> remove last line
		     sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", pibox_height, pibox_height + 1);
		     eval_check(interp, comm);
		  }
		  else
		  {
		     pibox_count += 1;
		     if (pos <= pibox_curpos)
			pibox_curpos += 1;
		     else if (pibox_curpos == PIBOX_INVALID_CURPOS)
			pibox_curpos = 0;
		  }

		  for (i=pibox_height-2; i >= pos; i--)
		     pibox_list[i + 1] = pibox_list[i];
		  PiListBox_InsertItem(pPiBlock, pos);
	       }
	       else
	       {  // insert above the cursor position
		  dprintf5("insert above curpos=%d at pos=%d  off=%d,max=%d,c=%d\n", pibox_curpos, pos, pibox_off, pibox_max, pibox_count);
		  assert(pos > 0);
		  pos -= 1;
		  // box already full -> remove first line
		  sprintf(comm, ".all.pi.list.text delete 1.0 2.0\n");
		  eval_check(interp, comm);
		  pibox_off += 1;
		  PiListBox_InsertItem(pPiBlock, pos);

		  for (i=0; i < pos; i++)
		     pibox_list[i] = pibox_list[i + 1];
	       }

	       pibox_list[pos].netwop_no  = pPiBlock->netwop_no;
	       pibox_list[pos].start_time = pPiBlock->start_time;

	       // update scrollbar
	       pibox_max += 1;
	       PiListBox_AdjustScrollBar();
               assert(PiListBox_ConsistancyCheck());
	    }
	 }
	 else
	 {  // new block lies behind viewable area
	    dprintf2("behind visible area off=%d,max=%d\n", pibox_off, pibox_max);
	    pibox_max += 1;
	    PiListBox_AdjustScrollBar();
            assert(PiListBox_ConsistancyCheck());
	 }
      }
   }
   EpgDbLockDatabase(dbc, FALSE);
}

// ----------------------------------------------------------------------------
// Update an item in the listbox, after it was newly acquired
// - This function is called *before* the item is replaced in the db;
//   if this function returns FALSE, the post-update function has to be called
//   after the replacement (in case an insert is required)
// - It's attributes may have changed, so that it no longer matches the filters
//   and has to be removed from the listing or max count
//
bool PiListBox_DbPreUpdate( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pObsolete, const PI_BLOCK *pPiBlock )
{
   bool result = FALSE;

   if ((pibox_state == PIBOX_LIST) && (usedDbc == dbc))
   {
      if ( (EpgDbFilterMatches(dbc, pPiFilterContext, pObsolete) != FALSE) &&
           (EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) == FALSE) )
      {
         dprintf2("item no longer matches the filter: start=%ld, netwop=%d\n", pPiBlock->start_time, pPiBlock->netwop_no);
         PiListBox_DbRemoved(dbc, pPiBlock);

         //assert(PiListBox_ConsistancyCheck());  //cannot check since item is not yet removed from db

         // return TRUE -> caller must not call Post-Update function
         result = TRUE;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Update an item in the listbox, after it was newly acquired
// - This function must be called *after* the new version was inserted into the db
// - it's attributes may have changed, so that it now matches the filters
//   and has to be inserted to the text widget
// - else, if it's visible it's simply newls displayed
//
void PiListBox_DbPostUpdate( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pObsolete, const PI_BLOCK *pPiBlock )
{
   int pos;

   if ((pibox_state == PIBOX_LIST) && (usedDbc == dbc))
   {
      if (pPiFilterContext->enabledFilters & FILTER_PROGIDX)
      {  // when progidx filter is used check is too complicated, hence do complete refresh
         pibox_resync = TRUE;
      }
      else
      {
         // check if the updated item is currently visible in the listbox
         for (pos=0; pos < pibox_count; pos++)
         {
            if ( (pibox_list[pos].netwop_no == pPiBlock->netwop_no) &&
                 (pibox_list[pos].start_time == pPiBlock->start_time) )
               break;
         }

         if (pos < pibox_count)
         {
            if ( EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) )
            {
               dprintf5("item replaced pos=%d  curpos=%d,off=%d,max=%d,c=%d\n", pos, pibox_curpos, pibox_off, pibox_max, pibox_count);

               sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", pos + 1, pos + 2);
               eval_check(interp, comm);
               EpgDbLockDatabase(dbc, TRUE);
               PiListBox_InsertItem(pPiBlock, pos);
               EpgDbLockDatabase(dbc, FALSE);

               if (pos == pibox_curpos)
               {
                  PiListBox_ShowCursor();
                  // update the short-info (but keep the scrollbar position)
                  PiListBox_UpdateInfoText(TRUE);
               }
               assert(PiListBox_ConsistancyCheck());
            }
            else
            {  // item is listed, but does not match the filter anymore -> remove it
               dprintf3("PiListBox-DbUpdated: item %d no longer matches the filter: start=%ld netwop=%d\n", pos, pPiBlock->start_time, pPiBlock->netwop_no);
               assert(EpgDbFilterMatches(dbc, pPiFilterContext, pObsolete));
               PiListBox_DbRemoved(usedDbc, pPiBlock);
               assert(PiListBox_ConsistancyCheck());
            }
         }
         else
         {  // item is not in the listing

            if ( EpgDbFilterMatches(dbc, pPiFilterContext, pObsolete) == FALSE )
            {
               if ( EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) != FALSE )
               {  // the new item matches -> need to insert it or at least add it to the max count
                  dprintf2("invisible item now matches the filter: start=%ld netwop=%d\n", pPiBlock->start_time, pPiBlock->netwop_no);
                  PiListBox_DbInserted(usedDbc, pPiBlock);
                  assert(PiListBox_ConsistancyCheck());
               }
            }
            else
            {  // remove was already handled in PreUpdate
               //if ( EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) == FALSE )
               //{  // the new item does not match -> remove it
               //   dprintf2("invisible item no longer matches the filter: start=%ld netwop=%d\n", pPiBlock->start_time, pPiBlock->netwop_no);
               //   PiListBox_DbRemoved(usedDbc, pPiBlock);
               //   assert(PiListBox_ConsistancyCheck());
               //}
            }
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Delete an item, that's about to be removed from the database, from the listbox
//
void PiListBox_DbRemoved( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pPiBlock )
{
   const PI_BLOCK *pTemp, *pPrev, *pNext;
   int i, pos;

   if ((pibox_state == PIBOX_LIST) && (usedDbc == dbc) && (pibox_count > 0))
   {
      for (pos=0; pos < pibox_count; pos++)
      {
	 if ( (pibox_list[pos].netwop_no == pPiBlock->netwop_no) &&
	      (pibox_list[pos].start_time == pPiBlock->start_time) )
	    break;
      }

      if (pos < pibox_count)
      {  // deleted item is currently visible -> remove it
	 dprintf7("remove item pos=%d (netwop=%d,start=%ld) cur=%d,off=%d,max=%d,c=%d\n", pos, pibox_list[pos].netwop_no, pibox_list[pos].start_time, pibox_curpos, pibox_off, pibox_max, pibox_count);

	 sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", pos + 1, pos + 2);
	 eval_check(interp, comm);

	 EpgDbLockDatabase(dbc, TRUE);
	 pPrev = pNext = NULL;

         // need to search by start time too, else we might catch the newly-inserted block (at a different position in the list)
	 pTemp = EpgDbSearchPi(dbc, NULL, pibox_list[0].start_time, pibox_list[0].netwop_no);
	 if (pTemp != NULL)
         {
	    pPrev = EpgDbSearchPrevPi(dbc, pPiFilterContext, pTemp);
            assert(pPrev != pPiBlock);
         }
	 else
	    debug2("PiListBox-DbRemoved: first listed block start=%ld netwop=%d not found", pibox_list[0].start_time, pibox_list[0].netwop_no);

	 pTemp = EpgDbSearchPi(dbc, NULL, pibox_list[pibox_count - 1].start_time, pibox_list[pibox_count - 1].netwop_no);
	 if (pTemp != NULL)
         {
	    pNext = EpgDbSearchNextPi(dbc, pPiFilterContext, pTemp);
            assert(pNext != pPiBlock);
         }
	 else
	    debug2("PiListBox-DbRemoved: last listed block start=%ld netwop=%d not found", pibox_list[pibox_count - 1].start_time, pibox_list[pibox_count - 1].netwop_no);

	 if ((pos < pibox_curpos) && (pPrev != NULL))
	 {  // item is above cursor -> insert item at top of window
	    assert(pibox_off > 0);
	    pibox_off -= 1;
	    pibox_max -= 1;
	    for (i=pos-1; i >= 0; i--)
	       pibox_list[i + 1] = pibox_list[i];
	    pibox_list[0].netwop_no  = pPrev->netwop_no;
	    pibox_list[0].start_time = pPrev->start_time;
	    PiListBox_InsertItem(pPrev, 0);
            dprintf2("prepend item netwop=%d start=%ld\n", pPrev->netwop_no, pPrev->start_time);
	 }
	 else
	 {  // item is below or at the cursor -> insert item at bottom of window
            for (i=pos; i <= pibox_count - 2; i++)
               pibox_list[i] = pibox_list[i + 1];

	    if (pNext != NULL)
	    {
	       pibox_list[pibox_count - 1].netwop_no  = pNext->netwop_no;
	       pibox_list[pibox_count - 1].start_time = pNext->start_time;
	       PiListBox_InsertItem(pNext, pibox_count - 1);
	       dprintf2("append item netwop=%d start=%ld\n", pNext->netwop_no, pNext->start_time);
	    }
	    else
	       pibox_count -= 1;
	    pibox_max -= 1;

	    if (pos == pibox_curpos)
	    {
	       PiListBox_ShowCursor();
	       PiListBox_UpdateInfoText(FALSE);
	    }
	    else if (pos < pibox_curpos)
	    {
	       pibox_curpos -= 1;
	    }
	 }
	 EpgDbLockDatabase(dbc, FALSE);

         // can not check here since db operation might not be completed
         //assert(PiListBox_ConsistancyCheck());
      }
      else
      {
	 dprintf2("remove invisible item off=%d,max=%d\n", pibox_off, pibox_max);
	 pibox_resync = TRUE;
      }
   }
}

// ----------------------------------------------------------------------------
// Recount number of items before and after the visible area
//
void PiListBox_DbRecount( const EPGDB_CONTEXT *usedDbc )
{
   const PI_BLOCK *pPrev, *pNext;

   if ((pibox_state == PIBOX_LIST) && (usedDbc == dbc) && pibox_resync)
   {
      pibox_resync = FALSE;

      if (pPiFilterContext->enabledFilters & FILTER_PROGIDX)
      {
         PiListBox_Refresh();
      }
      else
      if (pibox_count > 0)
      {
         dprintf2("recount items: old values: off=%d,max=%d\n", pibox_off, pibox_max);
         EpgDbLockDatabase(dbc, TRUE);
         pPrev = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].start_time, pibox_list[0].netwop_no);
         pNext = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[pibox_count - 1].start_time, pibox_list[pibox_count - 1].netwop_no);
         if ((pPrev != NULL) && (pNext != NULL))
         {
            pibox_off = 0;
            pPrev = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPrev);
            while (pPrev != NULL)
            {
               pibox_off++;
               pPrev = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPrev);
            }
            pibox_max = pibox_off + pibox_count;
            pNext = EpgDbSearchNextPi(dbc, pPiFilterContext, pNext);
            while (pNext != NULL)
            {
               pibox_max++;
               pNext = EpgDbSearchNextPi(dbc, pPiFilterContext, pNext);
            }
            EpgDbLockDatabase(dbc, FALSE);
            dprintf2("recount items: new values: off=%d,max=%d\n", pibox_off, pibox_max);

            // inform the vertical scrollbar about the viewable fraction
            PiListBox_AdjustScrollBar();

            assert(PiListBox_ConsistancyCheck());
         }
         else
         {
            debug0("PiListBox-DbRecount: first or last viewable item not found");
            PiListBox_Reset();
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Check if any items are expired or started running
// - currently simply does a refresh (resync with the db to remove expired PI)
// - could be improved by checking if any PI have expired. If not, only new
//   NOW PI would have to be marked
//
void PiListBox_UpdateNowItems( void )
{
   #if 0
   ulong now;
   uint  pos;
   bool refresh;
   #endif

   if (pibox_state == PIBOX_LIST)
   {
      #if 0
      if (pPiFilterContext->enabledFilters & FILTER_PROGIDX)
      {  // prog-no filter -> set flag so that listbox is resync'ed with db
         pibox_resync = TRUE;
      }

      refresh = TRUE; //XXX TODO
      now = time(NULL);

      for (pos=0; pos < pibox_count; pos++)
      {
         //XXX TODO: stop_time is not saved, so we have to search the DB
	 //if (pibox_list[pos].stop_time <= now)
            //refresh = TRUE;
	 if (pibox_list[pos].start_time > now)
	    break;
      }

      if (refresh)
      {
         #endif
         PiListBox_Refresh();
         #if 0
      }
      else if (pos > 0)
      {
	 sprintf(comm, ".all.pi.list.text tag ranges now\n");
	 if (eval_check(interp, comm) == TCL_OK)
	 {
	    sprintf(comm, "1.0 %d.0", pos + 1);
	    if (strcmp(interp->result, comm) != 0)
	    {
	       dprintf2("updating now items: was (%s), now (%s)\n", interp->result, comm);
	       sprintf(comm, ".all.pi.list.text tag remove now 1.0 end\n"
	                     ".all.pi.list.text tag add now 1.0 %d.0\n", pos + 1);
	       eval_check(interp, comm);
	    }
	    if (pibox_curpos < pos)
	    {  // change cursor color to NOW coloring
	       PiListBox_ShowCursor();
	    }
	 }
         PiListBox_DbRecount(dbc);
      }
      #endif
   }
}

// ----------------------------------------------------------------------------
// Refresh the listbox after filter change
//
static int PiListBox_RefreshCmd( ClientData ttp, Tcl_Interp *i, int argc, char *argv[] )
{
   PiListBox_Refresh();
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Refresh the listbox after filter change
//
static int PiListBox_ResetCmd( ClientData ttp, Tcl_Interp *i, int argc, char *argv[] )
{
   PiListBox_Reset();
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Get PI-Block for currently selected item
//
const PI_BLOCK * PiListBox_GetSelectedPi( void )
{
   const PI_BLOCK * pPiBlock;

   if ( (pibox_state == PIBOX_LIST) &&
        (pibox_curpos >= 0) && (pibox_curpos < pibox_count) )
   {
      pPiBlock = EpgDbSearchPi(dbc, NULL, pibox_list[pibox_curpos].start_time, pibox_list[pibox_curpos].netwop_no);
   }
   else
      pPiBlock = NULL;

   return pPiBlock;
}

// ----------------------------------------------------------------------------
// Returns the CNI and name of the network of the currently selected PI
//
static int PiListBox_GetSelectedNetwop( ClientData ttp, Tcl_Interp *i, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_PiListBox_GetSelectedNetwop";
   const AI_BLOCK *pAiBlock;
   uchar netwop;
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if ( (pibox_state == PIBOX_LIST) &&
           (pibox_curpos >= 0) && (pibox_curpos < pibox_count))
      {
         netwop = pibox_list[pibox_curpos].netwop_no;

         EpgDbLockDatabase(dbc, TRUE);
         pAiBlock = EpgDbGetAi(dbc);
         if ((pAiBlock != NULL) && (netwop < pAiBlock->netwopCount))
         {
            sprintf(comm, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);
            Tcl_AppendElement(interp, comm);
            Tcl_AppendElement(interp, AI_GET_NETWOP_NAME(pAiBlock, netwop));
         }
         EpgDbLockDatabase(dbc, FALSE);
      }
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Configure the number of PI in the listbox
//
static int PiListBox_Resize( ClientData ttp, Tcl_Interp *i, int argc, char *argv[] )
{
   PIBOX_ENTRY * old_list;
   char * pTmpStr;
   int height, off;

   pTmpStr = Tcl_GetVar(interp, "pibox_height", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if ((pTmpStr == NULL) || (Tcl_GetInt(interp, pTmpStr, &height) != TCL_OK))
   {  // no height configured or invalid number -> use default
      debug1("PiListBox-Resize: invalid pibox_height \"%s\"", (pTmpStr == NULL) ? "" : pTmpStr);
      height = PIBOX_DEFAULT_HEIGHT;
   }
   // minimum of one line avoids dealing with special cases
   if (height < 1)
      height = 1;

   if ((height != pibox_height) || (pibox_list == NULL))
   {
      old_list = pibox_list;
      pibox_list = xmalloc(height * sizeof(PIBOX_ENTRY));

      if ((old_list != NULL) && (pibox_count > 0))
      {
         // copy the old into the new array, to keep the cursor on the same PI
         // if the height was reduced, shift the listbox upwards to keep the cursor visible
         if (pibox_curpos >= height)
            off = pibox_curpos + 1 - height;
         else
            off = 0;
         memcpy(pibox_list, old_list + off, height * sizeof(PIBOX_ENTRY));

         pibox_height = height;
         // keep listbox params consistent
         pibox_curpos -= off;
         if (pibox_count > pibox_height)
            pibox_count = pibox_height;

         PiListBox_Refresh();
      }
      else
      {
         pibox_height = height;
         PiListBox_Reset();
      }
      if (old_list != NULL)
         xfree(old_list);
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Free resources allocated by the listbox
//
void PiListBox_Destroy( void )
{
   if (pibox_list != NULL)
      xfree(pibox_list);
}

// ----------------------------------------------------------------------------
// create the listbox and its commands
// - this should be called only once during start-up
//
void PiListBox_Create( void )
{
   Tcl_CmdInfo cmdInfo;

   pibox_state = PIBOX_NOT_INIT;
   pibox_dbstate = EPGDB_NOT_INIT;

   if (Tcl_GetCommandInfo(interp, "C_PiListBox_Scroll", &cmdInfo) == 0)
   {
      Tcl_CreateCommand(interp, "C_PiListBox_Scroll", PiListBox_Scroll, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_PiListBox_CursorDown", PiListBox_CursorDown, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_PiListBox_CursorUp", PiListBox_CursorUp, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_PiListBox_GotoTime", PiListBox_GotoTime, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_PiListBox_SelectItem", PiListBox_SelectItem, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_RefreshPiListbox", PiListBox_RefreshCmd, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ResetPiListbox", PiListBox_ResetCmd, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ResizePiListbox", PiListBox_Resize, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_PiListBox_GetSelectedNetwop", PiListBox_GetSelectedNetwop, (ClientData) NULL, NULL);
      // set the initial listbox height
      PiListBox_Resize(NULL, interp, 0, NULL);
   }
   else
      debug0("PiListBox-Create: commands were already created");
}

