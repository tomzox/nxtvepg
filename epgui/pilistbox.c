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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: pilistbox.c,v 1.25 2000/10/15 18:55:41 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcl.h"
#include "tk.h"

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgmain.h"
#include "epgctl/epgacqctl.h"
#include "epgui/pdc_themes.h"
#include "epgui/pifilter.h"
#include "epgui/pilistbox.h"


// ----------------------------------------------------------------------------
// private data structures
// describing the current state of the widget

typedef struct
{
   uint     block_no;
   uchar    netwop_no;
   ulong    start_time;
} PIBOX_ENTRY;

typedef enum 
{
   PIBOX_NOT_INIT,
   PIBOX_MESSAGE,
   PIBOX_LIST
} PIBOX_STATE;

#define PIBOX_HEIGHT 27
#define PIBOX_INVALID_CURPOS -1
PIBOX_ENTRY pibox_list[PIBOX_HEIGHT];  // list of all items in the window
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
         assert((pibox_count > 0) && (pibox_count <= PIBOX_HEIGHT));
         assert((pibox_max < PIBOX_HEIGHT) || (pibox_count == PIBOX_HEIGHT));
         assert((pibox_max >= PIBOX_HEIGHT) || (pibox_off == 0));
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
               assert( (pibox_list[i].block_no   == pPiBlock->block_no) &&
                       (pibox_list[i].netwop_no  == pPiBlock->netwop_no) &&
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
            pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].block_no, pibox_list[0].netwop_no);
            for (i=0; (i < pibox_count) && (pPiBlock != NULL); i++)
            {
               assert( (pibox_list[i].block_no   == pPiBlock->block_no) &&
                       (pibox_list[i].netwop_no  == pPiBlock->netwop_no) &&
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
void PiListBox_UpdateState( void )
{
   EPGDB_STATE oldState;

   assert(pUiDbContext != NULL);

   oldState = pibox_dbstate;
   pibox_dbstate = EpgAcqCtl_GetDbState();

   if (pibox_dbstate != oldState)
   {
      switch (pibox_dbstate)
      {
         case EPGDB_NO_PROVIDERS:
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

         case EPGDB_NO_PROV_SEL:
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

         case EPGDB_ACQ_PASSIVE:
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

         case EPGDB_ACQ_WAIT:
            pibox_state = PIBOX_MESSAGE;
            sprintf(comm, "PiListBox_PrintHelpHeader {"
                          "The database of the currently selected provider is empty. "
                          "Please wait a few seconds until Nextview data is received "
                          "or select a different provider from the Configure menu."
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

         case EPGDB_OK:
            // db is now ready -> display the data
            pibox_state = PIBOX_LIST;
            PiListBox_Reset();
            break;

         default:
            SHOULD_NOT_BE_REACHED;
            break;
      }
      sprintf(comm, ".all.pi.list.sc set 0.0 1.0\n");
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// Append one item to PI listing in the text widget
//
static void PiListBox_InsertItem( const PI_BLOCK *pPiBlock, int pos )
{
   const AI_BLOCK *pAiBlock;
   uchar date_str[20], start_str[20], stop_str[20];
   uchar netname[8];

   strftime(date_str, 19, "%a %d.%m", localtime(&pPiBlock->start_time));
   strftime(start_str, 19, "%H:%M", localtime(&pPiBlock->start_time));
   strftime(stop_str, 19, "%H:%M", localtime(&pPiBlock->stop_time));

   pAiBlock = EpgDbGetAi(dbc);
   if ((pAiBlock != NULL) && (pPiBlock->netwop_no < pAiBlock->netwopCount))
   {
      strncpy(netname, AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no), 7);
      netname[7] = 0;
   }
   else
   {
      debug0("PiListBox_InsertItem: no AI block");
      strcpy(netname, "??");
   }

   sprintf(comm, ".all.pi.list.text insert %d.0 {%-7s\t%s-%s %s %c%c\t%s\n} %s\n",
           pos + 1,
           netname,
           start_str, stop_str, date_str,
           ((pPiBlock->short_info_length > 0) ? 's' : '-'),
           ((pPiBlock->long_info_length > 0) ? 'l' : '-'),
           PI_GET_TITLE(pPiBlock),
           (pPiBlock->start_time <= time(NULL)) ? "now" : "");

   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Display short and long info for currently selected item
//
static void PiListBox_UpdateInfoText( void )
{  
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK *pPiBlock;
   uchar date_str[20], start_str[20], stop_str[20];
   int len, idx, theme, themeCat;
   
   sprintf(comm, ".all.pi.info.text delete 1.0 end\n");
   eval_check(interp, comm);

   if (pibox_curpos >= 0)
   {
      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      pPiBlock = EpgDbSearchPi(dbc, NULL, pibox_list[pibox_curpos].block_no, pibox_list[pibox_curpos].netwop_no);

      if ((pAiBlock != NULL) && (pPiBlock != NULL) && (pPiBlock->netwop_no < pAiBlock->netwopCount))
      {
         sprintf(comm, ".all.pi.info.text insert end {%s\n} title\n", PI_GET_TITLE(pPiBlock));
         eval_check(interp, comm);

         // now add a feature summary to the bottom label field
         strcpy(comm, ".all.pi.info.text insert end {");

         for (idx=0; idx < pPiBlock->no_themes; idx++)
         {
            theme = pPiBlock->themes[idx];
            if (theme > 0x80)
               theme = 0x80;
            themeCat = PdcThemeGetCategory(theme);
            if ( (pdc_themes[theme] != NULL) &&
                 // current theme is general and next same category -> skip
                 ( (themeCat != theme) ||
                   (idx + 1 >= pPiBlock->no_themes) ||
                   (themeCat != PdcThemeGetCategory(pPiBlock->themes[idx + 1])) ) &&
                 // current theme is identical to next -> skip
                 ( (idx + 1 >= pPiBlock->no_themes) ||
                   (theme != pPiBlock->themes[idx + 1]) ))
            {
               if ( (strlen(pdc_themes[theme]) > 10) &&
                    (strcmp(pdc_themes[theme] + strlen(pdc_themes[theme]) - 10, " - general") == 0) )
               {  // copy theme name except of the trailing " - general"
                  comm[strlen(comm) + strlen(pdc_themes[theme]) - 10] = 0;
                  strncpy(comm + strlen(comm), pdc_themes[theme], strlen(pdc_themes[theme]) - 10);
               }
               else
                  strcat(comm + strlen(comm), pdc_themes[theme]);
               strcat(comm, ", ");
            }
         }
         // remove last comma if nothing follows
         len = strlen(comm);
         if ((comm[len - 2] == ',') && (comm[len - 1] == ' '))
            comm[len - 2] = 0;
         strcat(comm, " (");

         switch(pPiBlock->feature_flags & 0x03)
         {
            case  1: strcat(comm, "2-channel, "); break;
            case  2: strcat(comm, "stereo, "); break;
            case  3: strcat(comm, "surround, "); break;
         }

         if (pPiBlock->feature_flags & 0x04)
            strcat(comm, "wide, ");
         if (pPiBlock->feature_flags & 0x08)
            strcat(comm, "PAL+, ");
         if (pPiBlock->feature_flags & 0x10)
            strcat(comm, "digital, ");
         if (pPiBlock->feature_flags & 0x20)
            strcat(comm, "encrypted, ");
         if (pPiBlock->feature_flags & 0x40)
            strcat(comm, "live, ");
         if (pPiBlock->feature_flags & 0x80)
            strcat(comm, "repeat, ");
         if (pPiBlock->feature_flags & 0x100)
            strcat(comm, "subtitles, ");

         if (pPiBlock->editorial_rating > 0)
            sprintf(comm + strlen(comm), "rating: %d of 1..7, ", pPiBlock->editorial_rating);

         if (pPiBlock->parental_rating == 1)
            strcat(comm, "age: general, ");
         else if (pPiBlock->parental_rating > 0)
            sprintf(comm + strlen(comm), "age: %d and up, ", pPiBlock->parental_rating * 2);

         // remove opening bracket if nothing follows
         len = strlen(comm);
         if ((comm[len - 2] == ' ') && (comm[len - 1] == '('))
            comm[len - 2] = 0;
         else
         {
            // remove last comma if nothing follows
            if ((comm[len - 2] == ',') && (comm[len - 1] == ' '))
               comm[len - 2] = 0;
            strcat(comm, ")");
         }
         // finalize string
         strcat(comm, "\n} features\n");
         eval_check(interp, comm);

         // print network name, start- & stop-time, short info
         strftime(date_str, 19, " %a %d.%m", localtime(&pPiBlock->start_time));
         strftime(start_str, 19, "%H:%M", localtime(&pPiBlock->start_time));
         strftime(stop_str, 19, "%H:%M", localtime(&pPiBlock->stop_time));

         sprintf(comm, ".all.pi.info.text insert end {%s, %s-%s%s: } bold\n",
                       AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no),
                       start_str, stop_str,
                       (((pPiBlock->start_time - time(NULL)) > 12*60*60) ? (char *)date_str : "") );
         eval_check(interp, comm);

         if ( (pPiBlock->short_info_length > 0) &&
              (pPiBlock->long_info_length > 0) )
         {
            // remove identical substring from beginning of long info
            // (this feature has been added esp. for the German provider RTL-II)
            const uchar *ps = PI_GET_SHORT_INFO(pPiBlock);
            const uchar *pl = PI_GET_LONG_INFO(pPiBlock);
            uint len = strlen(ps);
            uchar c = *pl;

            // min length is 30, because else single words might match
            while (len-- > 30)
            {
               if (*(ps++) == c)
               {
                  if (!strncmp(ps, pl+1, len))
                  {
                     pl += len + 1;
                     break;
                  }
               }
            }

            sprintf(comm, ".all.pi.info.text insert end {%s%s%s}\n",
                          PI_GET_SHORT_INFO(pPiBlock),
                          (len > 30) ? "" : "\n",
                          pl);
            eval_check(interp, comm);
         }
         else if (pPiBlock->short_info_length > 0)
         {
            sprintf(comm, ".all.pi.info.text insert end {%s}\n", PI_GET_SHORT_INFO(pPiBlock));
            eval_check(interp, comm);
         }
         else if (pPiBlock->long_info_length > 0)
         {
            sprintf(comm, ".all.pi.info.text insert end {%s}\n", PI_GET_LONG_INFO(pPiBlock));
            eval_check(interp, comm);
         }
      }
      else
         debug2("PiListBox-UpdateInfoText: selected block=%d netwop=%d not found\n", pibox_list[pibox_count-1].block_no, pibox_list[pibox_count-1].netwop_no);

      EpgDbLockDatabase(dbc, FALSE);
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
      sprintf(comm, ".all.pi.list.sc set %f %f\n", (float)pibox_off / pibox_max, (float)(pibox_off + PIBOX_HEIGHT) / pibox_max);
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// tag the selected line (by background color and border)
//
static void PiListBox_ShowCursor( void )
{
   if (pibox_list[pibox_curpos].start_time <= time(NULL))
   {
      sprintf(comm, ".all.pi.list.text tag configure sel -background #b8b8df\n"
                    ".all.pi.list.text tag add sel %d.0 %d.0\n",
                    pibox_curpos + 1, pibox_curpos + 2);
   }
   else
   {
      sprintf(comm, ".all.pi.list.text tag configure sel -background #c3c3c3\n"
                    ".all.pi.list.text tag add sel %d.0 %d.0\n",
                    pibox_curpos + 1, pibox_curpos + 2);
   }
   eval_check(interp, comm);
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
                 ".all.pi.info.text delete 1.0 end\n");
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
	 if (pibox_count < PIBOX_HEIGHT)
	 {
	    pibox_list[pibox_count].block_no   = pPiBlock->block_no;
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
      PiListBox_UpdateInfoText();
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
      sprintf(comm, ".all.pi.list.text delete 1.0 end\n"
                    ".all.pi.info.text delete 1.0 end\n");
      eval_check(interp, comm);

      EpgDbLockDatabase(dbc, TRUE);
      // XXX TODO: better check first if exact match on netwop&block is possible
      min_time = pibox_list[pibox_curpos].start_time;
      pibox_resync = FALSE;
      pibox_off = 0;
      pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
      if (pPiBlock != NULL)
      {
         while ( (pPiBlock->start_time < min_time) &&
                 ((pNext = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock)) != NULL) )
         {
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
            if (pibox_count < PIBOX_HEIGHT)
            {
               pibox_list[pibox_count].block_no   = pPiBlock->block_no;
               pibox_list[pibox_count].netwop_no  = pPiBlock->netwop_no;
               pibox_list[pibox_count].start_time = pPiBlock->start_time;
               pibox_count += 1;

               PiListBox_InsertItem(pPiBlock, pibox_count);
            }

            pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            pibox_max += 1;
         }
         while (pPiBlock != NULL);

         if ((pibox_count < PIBOX_HEIGHT) && (pibox_max > pibox_count))
         {  // not enough items found after the cursor -> shift window upwards
            pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].block_no, pibox_list[0].netwop_no);
            while ( (pibox_count < PIBOX_HEIGHT) &&
                    ((pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock)) != NULL) )
            {
               dprintf4("shift up: count=%d, max=%d: insert block=%d net=%d\n", pibox_count, pibox_max, pPiBlock->block_no, pPiBlock->netwop_no);
               for (i=pibox_count-1; i>=0; i--)
                  pibox_list[i + 1] = pibox_list[i];
               pibox_list[0].block_no   = pPiBlock->block_no;
               pibox_list[0].netwop_no  = pPiBlock->netwop_no;
               pibox_list[0].start_time = pPiBlock->start_time;
               PiListBox_InsertItem(pPiBlock, 0);

               assert(pibox_off > 0);
               pibox_off -= 1;
               pibox_curpos += 1;
               pibox_count += 1;
            }
            assert((pibox_count >= PIBOX_HEIGHT) || ((pibox_off == 0) && (pPiBlock == NULL)));
         }

         if (pibox_curpos >= pibox_count)
         {  // set the cursor on the last item
            pibox_curpos = pibox_count - 1;
         }
         PiListBox_ShowCursor();

         // display short and long info
         PiListBox_UpdateInfoText();
      }
      else
      {
         pibox_curpos = PIBOX_INVALID_CURPOS;
         pibox_count = 0;
         pibox_max = 0;
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

      if (pibox_max > PIBOX_HEIGHT)
      {
         uint old_block, old_netwop;

	 delta = pibox_max * atof(argv[2]);
	 if (delta < 0)
	    delta = 0;
	 else if (delta > pibox_max - PIBOX_HEIGHT)
	    delta = pibox_max - PIBOX_HEIGHT;

	 sprintf(comm, ".all.pi.list.text delete 1.0 end\n"
		       ".all.pi.info.text delete 1.0 end\n");
	 eval_check(interp, comm);

         if (pibox_curpos >= 0)
         {
            old_block  = pibox_list[pibox_curpos].block_no;
            old_netwop = pibox_list[pibox_curpos].netwop_no;
         }
         else
            old_netwop = old_block = 0xffff;

	 pibox_count = 0;
	 pibox_max = 0;
	 EpgDbLockDatabase(dbc, TRUE);
	 pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
	 if (pPiBlock != NULL)
	 {
	    do
	    {
	       if ((pibox_max >= delta) && (pibox_count < PIBOX_HEIGHT))
	       {
		  pibox_list[pibox_count].block_no   = pPiBlock->block_no;
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
                    (old_block == pibox_list[i].block_no) )
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
	    PiListBox_UpdateInfoText();
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
	 const PI_BLOCK *pNewPiBlock[PIBOX_HEIGHT];
	 int new_count = 0;

	 if (delta > 0)
	 {
	    //  ----  Scroll one page down  ----

	    EpgDbLockDatabase(dbc, TRUE);
            if (pibox_count >= PIBOX_HEIGHT)
            {
               pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[pibox_count-1].block_no, pibox_list[pibox_count-1].netwop_no);
               if (pPiBlock != NULL)
               {
                  while ( (new_count < PIBOX_HEIGHT) &&
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
                  debug2("PiListBox-Scroll: last listed block=%d netwop=%d not found\n", pibox_list[pibox_count-1].block_no, pibox_list[pibox_count-1].netwop_no);
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

               for (i=new_count; i < PIBOX_HEIGHT; i++)
                  pibox_list[i - new_count] = pibox_list[i];
               for (i=PIBOX_HEIGHT-new_count, j=0; i < PIBOX_HEIGHT; i++, j++)
               {
                  pibox_list[i].block_no   = pNewPiBlock[j]->block_no;
                  pibox_list[i].netwop_no  = pNewPiBlock[j]->netwop_no;
                  pibox_list[i].start_time = pNewPiBlock[j]->start_time;
                  PiListBox_InsertItem(pNewPiBlock[j], i);
               }
               pibox_off += new_count;

               if (new_count > pibox_curpos)
               {  // cursor moves downwards by number of items in widget
                  // i.e. during a full page scroll it stays at the same position
                  pibox_curpos = pibox_curpos + PIBOX_HEIGHT - new_count;
               }
               else
               {  // end of listing reached -> set cursor onto the last item
                  pibox_curpos = PIBOX_HEIGHT - 1;
               }
               PiListBox_ShowCursor();
               PiListBox_UpdateInfoText();

               // inform the vertical scrollbar about the start offset and viewable fraction
               PiListBox_AdjustScrollBar();
            }
            else if (pibox_curpos < pibox_count - 1)
            {  // no items below -> set cursor to last item
               sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
               eval_check(interp, comm);
               pibox_curpos = pibox_count - 1;
               PiListBox_ShowCursor();
               PiListBox_UpdateInfoText();
            }

	    EpgDbLockDatabase(dbc, FALSE);
            assert(PiListBox_ConsistancyCheck());
	 }
	 else if (pibox_count > 0)
	 {
	    //  ----  Scroll one page up  ----

	    EpgDbLockDatabase(dbc, TRUE);
	    pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].block_no, pibox_list[0].netwop_no);
	    if (pPiBlock != NULL)
	    {
	       new_count = 0;
	       while ( (new_count < PIBOX_HEIGHT) &&
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
		  if ((pibox_curpos < PIBOX_HEIGHT - new_count) && (pibox_curpos >= 0))
		  {
		     sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
		     eval_check(interp, comm);
		  }
		  else
		     i=0; //breakpoint
		  // remove old text at the bottom of the widget
		  sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", PIBOX_HEIGHT - new_count + 1, PIBOX_HEIGHT + 1);
		  eval_check(interp, comm);

		  for (i=PIBOX_HEIGHT-1; i >= new_count; i--)
		     pibox_list[i] = pibox_list[i - new_count];
		  // insert new items at the top in reverse order
		  for (i=0, j=new_count-1; i < new_count; i++, j--)
		  {
		     pibox_list[j].block_no   = pNewPiBlock[i]->block_no;
		     pibox_list[j].netwop_no  = pNewPiBlock[i]->netwop_no;
		     pibox_list[j].start_time = pNewPiBlock[i]->start_time;
		     PiListBox_InsertItem(pNewPiBlock[i], 0);
		  }
		  pibox_off -= new_count;

		  if (new_count >= PIBOX_HEIGHT - pibox_curpos)
		  {  // cursor moves upwards by number of items in widget
		     // i.e. during a full page scroll it stays at the same position
		     pibox_curpos = new_count + pibox_curpos - PIBOX_HEIGHT;
		  }
		  else
		  {  // start of listing reached -> set cursor onto the first item
		     pibox_curpos = 0;
		  }
		  PiListBox_ShowCursor();
		  PiListBox_UpdateInfoText();

		  // inform the vertical scrollbar about the start offset and viewable fraction
		  PiListBox_AdjustScrollBar();
	       }
	       else if (pibox_curpos > 0)
	       {  // no items above -> set cursor to first item
		  sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
		  eval_check(interp, comm);
		  pibox_curpos = 0;
		  PiListBox_ShowCursor();
		  PiListBox_UpdateInfoText();
	       }
	    }
	    else
	       debug2("PiListBox-Scroll: first listed block=%d netwop=%d not found\n", pibox_list[pibox_count-1].block_no, pibox_list[pibox_count-1].netwop_no);
	    EpgDbLockDatabase(dbc, FALSE);
            assert(PiListBox_ConsistancyCheck());
	 }
      }
      else if (!strcmp(argv[3], "units"))
      {
	 if ((delta > 0) && (pibox_count >= PIBOX_HEIGHT))
	 {
	    //  ----  Scroll one item down  ----

	    EpgDbLockDatabase(dbc, TRUE);
	    pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[pibox_count-1].block_no, pibox_list[pibox_count-1].netwop_no);
	    if (pPiBlock != NULL)
	    {
	       pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
	       if (pPiBlock != NULL)
	       {
		  sprintf(comm, ".all.pi.list.text delete 1.0 2.0\n");
		  eval_check(interp, comm);
		  for (i=1; i < PIBOX_HEIGHT; i++)
		     pibox_list[i - 1] = pibox_list[i];
		  pibox_list[pibox_count-1].block_no   = pPiBlock->block_no;
		  pibox_list[pibox_count-1].netwop_no  = pPiBlock->netwop_no;
		  pibox_list[pibox_count-1].start_time = pPiBlock->start_time;
		  pibox_off += 1;

		  PiListBox_InsertItem(pPiBlock, PIBOX_HEIGHT);
		  if (pibox_curpos == 0)
		  {
		     PiListBox_ShowCursor();
		  }
		  else
		     pibox_curpos -= 1;
		  PiListBox_UpdateInfoText();

		  // adjust the vertical scrollbar
		  PiListBox_AdjustScrollBar();
	       }
	    }
	    else
	       debug2("PiListBox-Scroll: last listed block=%d netwop=%d not found\n", pibox_list[pibox_count-1].block_no, pibox_list[pibox_count-1].netwop_no);
	    EpgDbLockDatabase(dbc, FALSE);
            assert(PiListBox_ConsistancyCheck());
	 }
	 else if (pibox_count >= PIBOX_HEIGHT)
	 {
	    //  ----  Scroll one item up  ----

	    EpgDbLockDatabase(dbc, TRUE);
	    pPiBlock = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].block_no, pibox_list[0].netwop_no);
	    if (pPiBlock != NULL)
	    {
	       pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
	       if (pPiBlock != NULL)
	       {
		  sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", PIBOX_HEIGHT, PIBOX_HEIGHT+1);
		  eval_check(interp, comm);
		  for (i=PIBOX_HEIGHT-2; i >= 0; i--)
		     pibox_list[i + 1] = pibox_list[i];
		  pibox_list[0].block_no   = pPiBlock->block_no;
		  pibox_list[0].netwop_no  = pPiBlock->netwop_no;
		  pibox_list[0].start_time = pPiBlock->start_time;
		  pibox_off -= 1;

		  PiListBox_InsertItem(pPiBlock, 0);
		  if (pibox_curpos == PIBOX_HEIGHT - 1)
		  {
		     PiListBox_ShowCursor();
		  }
		  else
		     pibox_curpos += 1;
		  PiListBox_UpdateInfoText();

		  // inform the vertical scrollbar about the start offset and viewable fraction
		  PiListBox_AdjustScrollBar();
	       }
	    }
	    else
	       debug2("PiListBox-Scroll: first listed block=%d netwop=%d not found\n", pibox_list[0].block_no, pibox_list[0].netwop_no);
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
      if (pibox_curpos == PIBOX_HEIGHT-1)
      {  // last item is selected -> scroll down
	 const char *av[] = { "C_PiListBox_Scroll", "scroll", "1", "units" };

	 PiListBox_Scroll(ttp, interp, 4, (char **) av);

	 // set cursor to new last element
	 if (pibox_curpos == PIBOX_HEIGHT-2)
	 {
	    // set cursor one line down, onto last item
	    sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	    eval_check(interp, comm);
	    pibox_curpos += 1;
	    PiListBox_ShowCursor();
            PiListBox_UpdateInfoText();
	 }
      }
      else if (pibox_curpos < pibox_count-1)
      {  // move cursor one position down (text widget line count starts at 1)
	 sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	 eval_check(interp, comm);
	 pibox_curpos += 1;
	 PiListBox_ShowCursor();
	 PiListBox_UpdateInfoText();
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
            PiListBox_UpdateInfoText();
	 }
      }
      else
      {  // move cursor one position up (text widget line count starts at 1)
	 sprintf(comm, ".all.pi.list.text tag remove sel %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	 eval_check(interp, comm);
	 pibox_curpos -= 1;
	 PiListBox_ShowCursor();
	 PiListBox_UpdateInfoText();
      }
      assert(PiListBox_ConsistancyCheck());
   }

   return TCL_OK; 
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
	 PiListBox_UpdateInfoText();
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
      PiListBox_UpdateState();
      return;
   }

   EpgDbLockDatabase(dbc, TRUE);
   if ((pibox_state == PIBOX_LIST) && EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock))
   {
      if ((pibox_count >= PIBOX_HEIGHT) &&
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

	 if (pos < PIBOX_HEIGHT)
	 {
	    if ( (pos < pibox_count) &&
		 (pibox_list[pos].start_time == pPiBlock->start_time) &&
		 (pibox_list[pos].netwop_no == pPiBlock->netwop_no) &&
		 (pibox_list[pos].block_no == pPiBlock->block_no) )
	    {  // special case: item update
	       dprintf5("item update curpos=%d at pos=%d  off=%d,max=%d,c=%d\n", pibox_curpos, pos, pibox_off, pibox_max, pibox_count);
	       sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", pos + 1, pos + 2);
	       eval_check(interp, comm);
	       PiListBox_InsertItem(pPiBlock, pos);
	       if (pos == pibox_curpos)
	       {
		  PiListBox_ShowCursor();
	       }
	    }
	    else if ((pos == 0) && (pibox_off == 0) && (pibox_curpos <= 0))
	    {  // special case: empty or cursor on the very first item -> cursor stays on top
	       dprintf4("insert at top: curpos=%d,off=%d,max=%d,c=%d\n", pibox_curpos, pibox_off, pibox_max, pibox_count);
	       for (i=PIBOX_HEIGHT-2; i >= 0; i--)
		  pibox_list[i + 1] = pibox_list[i];
	       pibox_list[0].block_no   = pPiBlock->block_no;
	       pibox_list[0].netwop_no  = pPiBlock->netwop_no;
	       pibox_list[0].start_time = pPiBlock->start_time;
	       pibox_max += 1;

	       if (pibox_count >= PIBOX_HEIGHT)
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
	       PiListBox_UpdateInfoText();
	       PiListBox_AdjustScrollBar();
               assert(PiListBox_ConsistancyCheck());
	    }
	    else if ((pos > 0) || (pibox_count < PIBOX_HEIGHT))
	    {
	       // selected item must not be shifted out of viewable area
	       if ((pos > pibox_curpos) || (pibox_count < PIBOX_HEIGHT))
	       {  // insert below the cursor position (or window not full)
		  dprintf5("insert below curpos=%d at pos=%d  off=%d,max=%d,c=%d\n", pibox_curpos, pos, pibox_off, pibox_max, pibox_count);
		  if (pibox_count >= PIBOX_HEIGHT)
		  {  // box already full -> remove last line
		     sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", PIBOX_HEIGHT, PIBOX_HEIGHT + 1);
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

		  for (i=PIBOX_HEIGHT-2; i >= pos; i--)
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

	       pibox_list[pos].block_no   = pPiBlock->block_no;
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
   bool result;

   if ( (EpgDbFilterMatches(dbc, pPiFilterContext, pObsolete) != FALSE) &&
        (EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) == FALSE) )
   {
      dprintf2("item no longer matches the filter: netwop=%d block=%d\n", pPiBlock->netwop_no, pPiBlock->block_no);
      PiListBox_DbRemoved(usedDbc, pPiBlock);

      //assert(PiListBox_ConsistancyCheck());  //cannot check since item is not yet removed from db

      // return TRUE -> caller must not call Post-Update function
      result = TRUE;
   }
   else
      result = FALSE;

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
                 (pibox_list[pos].block_no == pPiBlock->block_no) )
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
                  PiListBox_UpdateInfoText();
               }
               assert(PiListBox_ConsistancyCheck());
            }
            else
            {  // item is listed, but does not match the filter anymore -> remove it
               dprintf3("PiListBox-DbUpdated: item %d no longer matches the filter: netwop=%d block=%d\n", pos, pPiBlock->netwop_no, pPiBlock->block_no);
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
                  dprintf2("invisible item now matches the filter: netwop=%d block=%d\n", pPiBlock->netwop_no, pPiBlock->block_no);
                  PiListBox_DbInserted(usedDbc, pPiBlock);
                  assert(PiListBox_ConsistancyCheck());
               }
            }
            else
            {  // remove was already handled in PreUpdate
               //if ( EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) == FALSE )
               //{  // the new item does not match -> remove it
               //   dprintf2("invisible item no longer matches the filter: netwop=%d block=%d\n", pPiBlock->netwop_no, pPiBlock->block_no);
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
         // note: need to compare the start time too, to avoid deleting a newly
         // inserted block which kept block_no + newop, but changed only time
         // (this is not handled as an update by the database management)
	 if ( (pibox_list[pos].netwop_no == pPiBlock->netwop_no) &&
	      (pibox_list[pos].block_no == pPiBlock->block_no) &&
	      (pibox_list[pos].start_time == pPiBlock->start_time) )
	    break;
      }

      if (pos < pibox_count)
      {  // deleted item is currently visible -> remove it
	 dprintf8("remove item pos=%d (net=%d,block=%d,start=%ld) cur=%d,off=%d,max=%d,c=%d\n", pos, pibox_list[pos].netwop_no, pibox_list[pos].block_no, pibox_list[pos].start_time, pibox_curpos, pibox_off, pibox_max, pibox_count);

	 sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", pos + 1, pos + 2);
	 eval_check(interp, comm);

	 EpgDbLockDatabase(dbc, TRUE);
	 pPrev = pNext = NULL;

         // need to search by start time too, else we might catch the newly-inserted block (at a different position in the list)
	 pTemp = EpgDbSearchPiExact(dbc, pibox_list[0].block_no, pibox_list[0].netwop_no, pibox_list[0].start_time);
	 if (pTemp != NULL)
         {
	    pPrev = EpgDbSearchPrevPi(dbc, pPiFilterContext, pTemp);
            assert(pPrev != pPiBlock);
         }
	 else
	    debug2("PiListBox-DbRemoved: first listed block=%d netwop=%d not found", pibox_list[0].block_no, pibox_list[0].netwop_no);

	 pTemp = EpgDbSearchPiExact(dbc, pibox_list[pibox_count - 1].block_no, pibox_list[pibox_count - 1].netwop_no, pibox_list[pibox_count - 1].start_time);
	 if (pTemp != NULL)
         {
	    pNext = EpgDbSearchNextPi(dbc, pPiFilterContext, pTemp);
            assert(pNext != pPiBlock);
         }
	 else
	    debug2("PiListBox-DbRemoved: last listed block=%d netwop=%d not found", pibox_list[pibox_count - 1].block_no, pibox_list[pibox_count - 1].netwop_no);

	 if ((pos < pibox_curpos) && (pPrev != NULL))
	 {  // item is above cursor -> insert item at top of window
	    assert(pibox_off > 0);
	    pibox_off -= 1;
	    pibox_max -= 1;
	    for (i=pos-1; i >= 0; i--)
	       pibox_list[i + 1] = pibox_list[i];
	    pibox_list[0].block_no   = pPrev->block_no;
	    pibox_list[0].netwop_no  = pPrev->netwop_no;
	    pibox_list[0].start_time = pPrev->start_time;
	    PiListBox_InsertItem(pPrev, 0);
            dprintf2("prepend item netwop=%d block=%d\n", pPrev->netwop_no, pPrev->block_no);
	 }
	 else
	 {  // item is below or at the cursor -> insert item at bottom of window
            for (i=pos; i <= pibox_count - 2; i++)
               pibox_list[i] = pibox_list[i + 1];

	    if (pNext != NULL)
	    {
	       pibox_list[pibox_count - 1].block_no   = pNext->block_no;
	       pibox_list[pibox_count - 1].netwop_no  = pNext->netwop_no;
	       pibox_list[pibox_count - 1].start_time = pNext->start_time;
	       PiListBox_InsertItem(pNext, pibox_count - 1);
	       dprintf2("append item netwop=%d block=%d\n", pNext->netwop_no, pNext->block_no);
	    }
	    else
	       pibox_count -= 1;
	    pibox_max -= 1;

	    if (pos == pibox_curpos)
	    {
	       PiListBox_ShowCursor();
	       PiListBox_UpdateInfoText();
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
         pPrev = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[0].block_no, pibox_list[0].netwop_no);
         pNext = EpgDbSearchPi(dbc, pPiFilterContext, pibox_list[pibox_count - 1].block_no, pibox_list[pibox_count - 1].netwop_no);
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
// check if any items need to be markes as NOW, i.e. currently running
//
void PiListBox_UpdateNowItems( const EPGDB_CONTEXT *usedDbc )
{
   ulong now;
   uint  pos;

   if ((pibox_state == PIBOX_LIST) && (usedDbc == dbc))
   {
      if (pPiFilterContext->enabledFilters & FILTER_PROGIDX)
      {  // prog-no filter -> set flag so that listbox is resync'ed with db
         pibox_resync = TRUE;
      }

      now = time(NULL);

      for (pos=0; pos < pibox_count; pos++)
      {
	 if (pibox_list[pos].start_time > now)
	    break;
      }

      if (pos > 0)
      {
	 sprintf(comm, ".all.pi.list.text tag ranges now\n");
	 if (eval_check(interp, comm) == TCL_OK)
	 {
	    sprintf(comm, "1.0 %d.0", pos + 1);
	    if(strcmp(interp->result, comm) != 0)
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
      }
   }
}

// ----------------------------------------------------------------------------
// show info about the currently selected item in pop-up window
//
static int PiListBox_PopupPi( ClientData ttp, Tcl_Interp *i, int argc, char *argv[] )
{
   const AI_BLOCK * pAiBlock;
   const AI_NETWOP *pNetwop;
   const PI_BLOCK * pPiBlock;
   const DESCRIPTOR *pDesc;
   uchar *p;
   uchar start_str[50], stop_str[50];
   uchar ident[50];
   int index;
   int result;

   if (argc != 3) 
   {  // Anzahl Parameter falsch -> usage ausgeben lassen
      i->result = "Usage: PopupPi xcoo ycoo";
      result = TCL_ERROR; 
   }  
   else
   {
      result = TCL_OK; 

      if (pibox_curpos >= 0)
      {
	 EpgDbLockDatabase(dbc, TRUE);
	 pAiBlock = EpgDbGetAi(dbc);
	 pPiBlock = EpgDbSearchPi(dbc, NULL, pibox_list[pibox_curpos].block_no, pibox_list[pibox_curpos].netwop_no);
	 if ((pAiBlock != NULL) && (pPiBlock != NULL) && (pPiBlock->netwop_no < pAiBlock->netwopCount))
	 {
	    pNetwop = AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no);

	    sprintf(ident, ".poppi_%d_%d", pPiBlock->netwop_no, pPiBlock->block_no);

	    sprintf(comm, "Create_PopupPi %s %s %s\n", ident, argv[1], argv[2]);
	    eval_check(interp, comm);

	    sprintf(comm, "%s.text insert end {%s\n} title\n", ident, PI_GET_TITLE(pPiBlock));
	    eval_check(interp, comm);

	    sprintf(comm, "%s.text insert end {Network: \t#%d %s\n} body\n", ident, pPiBlock->netwop_no, AI_GET_NETWOP_NAME(pAiBlock,pPiBlock->netwop_no));
	    eval_check(interp, comm);

	    sprintf(comm, "%s.text insert end {BlockNo:\t0x%04X in %04X-%04X-%04X\n} body\n", ident, pPiBlock->block_no, pNetwop->startNo, pNetwop->stopNo, pNetwop->stopNoSwo);
	    eval_check(interp, comm);

	    strftime(start_str, 49, "%a %d.%m %H:%M", localtime(&pPiBlock->start_time));
	    strftime(stop_str, 49, "%a %d.%m %H:%M", localtime(&pPiBlock->stop_time));
	    sprintf(comm, "%s.text insert end {Start:\t%s\nStop:\t%s\n} body\n", ident, start_str, stop_str);
	    eval_check(interp, comm);

	    if (pPiBlock->pil != 0x7FFF)
	    {
	       sprintf(comm, "%s.text insert end {PIL:\t%02d.%02d. %02d:%02d\n} body\n", ident,
		       (pPiBlock->pil >> 15) & 0x1F,
		       (pPiBlock->pil >> 11) & 0x0F,
		       (pPiBlock->pil >>  6) & 0x1F,
		       (pPiBlock->pil      ) & 0x3F );
	    }
	    else
	       sprintf(comm, "%s.text insert end {PIL:\tnone\n} body\n", ident);
	    eval_check(interp, comm);

	    switch(pPiBlock->feature_flags & 0x03)
	    {
	      case  0: p = "mono"; break;
	      case  1: p = "2chan"; break;
	      case  2: p = "stereo"; break;
	      case  3: p = "surround"; break;
	      default: p = ""; break;
	    }
	    sprintf(comm, "%s.text insert end {Sound:\t%s\n} body\n", ident, p);
	    eval_check(interp, comm);
	    if (pPiBlock->feature_flags & ~0x03)
	    sprintf(comm, "%s.text insert end {Features:\t%s%s%s%s%s%s%s\n} body\n", ident,
			   ((pPiBlock->feature_flags & 0x04) ? " wide" : ""),
			   ((pPiBlock->feature_flags & 0x08) ? " PAL+" : ""),
			   ((pPiBlock->feature_flags & 0x10) ? " digital" : ""),
			   ((pPiBlock->feature_flags & 0x20) ? " encrypted" : ""),
			   ((pPiBlock->feature_flags & 0x40) ? " live" : ""),
			   ((pPiBlock->feature_flags & 0x80) ? " repeat" : ""),
			   ((pPiBlock->feature_flags & 0x100) ? " subtitles" : "")
			   );
	    else
	       sprintf(comm, "%s.text insert end {Features:\tnone\n} body\n", ident);
	    eval_check(interp, comm);

	    if (pPiBlock->parental_rating == 0)
	       sprintf(comm, "%s.text insert end {Parental rating:\tnone\n} body\n", ident);
	    else if (pPiBlock->parental_rating == 1)
	       sprintf(comm, "%s.text insert end {Parental rating:\tgeneral\n} body\n", ident);
	    else
	       sprintf(comm, "%s.text insert end {Parental rating:\t%d years and up\n} body\n", ident, pPiBlock->parental_rating * 2);
	    eval_check(interp, comm);

	    if (pPiBlock->editorial_rating == 0)
	       sprintf(comm, "%s.text insert end {Editorial rating:\tnone\n} body\n", ident);
	    else
	       sprintf(comm, "%s.text insert end {Editorial rating:\t%d of 1..7\n} body\n", ident, pPiBlock->editorial_rating);
	    eval_check(interp, comm);

	    for (index=0; index < pPiBlock->no_themes; index++)
	    {
	       if (pPiBlock->themes[index] > 0x80)
		  p = "series";
	       else if ((p = (char *) pdc_themes[pPiBlock->themes[index]]) == 0)
		  p = (char *) pdc_undefined_theme;
	       sprintf(comm, "%s.text insert end {Theme:\t0x%02X %s\n} body\n", ident, pPiBlock->themes[index], p);
	       eval_check(interp, comm);
	    }

	    for (index=0; index < pPiBlock->no_sortcrit; index++)
	    {
	       sprintf(comm, "%s.text insert end {Sorting Criterion:\t0x%02X\n} body\n", ident, pPiBlock->sortcrits[index]);
	       eval_check(interp, comm);
	    }

	    pDesc = PI_GET_DESCRIPTORS(pPiBlock);
	    for (index=0; index < pPiBlock->no_descriptors; index++)
	    {
	       switch (pDesc[index].type)
	       {
		  case 7:  sprintf(comm, "%s.text insert end {Descriptor:\tlanguage ID %d\n} body\n", ident, pDesc[index].id); break;
		  case 8:  sprintf(comm, "%s.text insert end {Descriptor:\tsubtitle ID %d\n} body\n", ident, pDesc[index].id); break;
		  default: sprintf(comm, "%s.text insert end {Descriptor:\tunknown type=%d, ID=%d\n} body\n", ident, pDesc[index].type, pDesc[index].id); break;
	       }
	       eval_check(interp, comm);
	    }

	    sprintf(comm, "global poppedup_pi\n"
	                  "$poppedup_pi.text configure -state disabled\n"
	                  "$poppedup_pi.text configure -height [expr 1 + [$poppedup_pi.text index end]]\n"
			  "pack $poppedup_pi.text\n");
	    eval_check(interp, comm);
	 }
	 else
	    debug2("PiListBox-PopupPi: selected block=%d netwop=%d not found\n", pibox_list[pibox_count-1].block_no, pibox_list[pibox_count-1].netwop_no);
	 EpgDbLockDatabase(dbc, FALSE);
      }
   }

   return result;
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

   if ((pibox_curpos >= 0) && (pibox_curpos < pibox_count))
      pPiBlock = EpgDbSearchPi(dbc, NULL, pibox_list[pibox_curpos].block_no, pibox_list[pibox_curpos].netwop_no);
   else
      pPiBlock = NULL;

   return pPiBlock;
}

// ----------------------------------------------------------------------------
// create the listbox and its commands
// - this should be called only once during start-up
//
void PiListBox_Create( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_PiListBox_Scroll", &cmdInfo) == 0)
   {
      Tcl_CreateCommand(interp, "C_PiListBox_Scroll", PiListBox_Scroll, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_PiListBox_CursorDown", PiListBox_CursorDown, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_PiListBox_CursorUp", PiListBox_CursorUp, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_PiListBox_SelectItem", PiListBox_SelectItem, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_PiListBox_PopupPi", PiListBox_PopupPi, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_RefreshPiListbox", PiListBox_RefreshCmd, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ResetPiListbox", PiListBox_ResetCmd, (ClientData) NULL, NULL);
   }
   else
      debug0("PiListBox-Create: commands were already created");

   pibox_state = PIBOX_NOT_INIT;
   pibox_dbstate = EPGDB_NOT_INIT;
   PiListBox_UpdateState();
}

