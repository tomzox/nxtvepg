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
 *  $Id: pilistbox.c,v 1.92 2003/10/05 19:34:37 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DEBUG_PILISTBOX_CONSISTANCY OFF
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
#include "epgctl/epgacqctl.h"
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgctl/epgctxctl.h"
#include "epgui/pifilter.h"
#include "epgui/pioutput.h"
#include "epgui/pidescr.h"
#include "epgui/pilistbox.h"


// ----------------------------------------------------------------------------
// private data structures
// describing the current state of the widget

typedef struct
{
   uchar    netwop_no;
   time_t   start_time;
} PIBOX_ENTRY;

typedef enum 
{
   PIBOX_NOT_INIT,
   PIBOX_MESSAGE,
   PIBOX_LIST
} PIBOX_STATE;

#define PIBOX_DEFAULT_HEIGHT 25
#define PIBOX_MAX_ACQ_CALLBACKS  100
#define PIBOX_INVALID_CURPOS -1
PIBOX_ENTRY * pibox_list = NULL; // list of all items in the window
int         pibox_height;        // number of available lines in the widget
int         pibox_count;         // number of items currently visible
int         pibox_curpos;        // relative cursor position in window or -1 if window empty
int         pibox_max;           // total number of matching items
int         pibox_off;           // number of currently invisible items above the window
bool        pibox_resync;        // if TRUE, need to evaluate off and max
bool        pibox_lock;          // if TRUE, refuse additions from acq
PIBOX_STATE pibox_state = PIBOX_NOT_INIT;  // listbox state

#define dbc pUiDbContext         // internal shortcut

// forward declarations
static void PiListBox_DbRemoved( const PI_BLOCK *pPiBlock );
static bool PiListBox_HandleAcqEvent( const EPGDB_CONTEXT * usedDbc, EPGDB_PI_ACQ_EVENT event,
                                      const PI_BLOCK * pPiBlock, const PI_BLOCK * pObsolete );

// ----------------------------------------------------------------------------
// Check the consistancy of the listbox state with the database and text widget
// - if an inconsistancy is found, an assertion failure occurs, which either
//   raises a SEGV (developer release), adds a message to EPG.LOG (tester release)
//   or is ignored (production release). For more details, see debug.[ch]
//
#if DEBUG_PILISTBOX_CONSISTANCY == ON
static bool PiListBox_ConsistancyCheck( void )
{
   const PI_BLOCK *pPiBlock;
   int i;

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
            pPiBlock = EpgDbSearchPi(dbc, pibox_list[0].start_time, pibox_list[0].netwop_no);
            assert(EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock));
            for (i=0; (i < pibox_count) && (pPiBlock != NULL); i++)
            {
               assert( (pibox_list[i].netwop_no  == pPiBlock->netwop_no) &&
                       (pibox_list[i].start_time == pPiBlock->start_time) );
               pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            }
            assert(i >= pibox_count);
         }

         // check cursor position
         sprintf(comm, ".all.pi.list.text tag ranges cur\n");
         if (Tcl_Eval(interp, comm) == TCL_OK)
         {
            sprintf(comm, "%d.0 %d.0", pibox_curpos + 1, pibox_curpos + 2);
            assert(!strcmp(interp->result, comm));
         }

         // check number of lines in text widget
         sprintf(comm, ".all.pi.list.text index end\n");
         if (Tcl_Eval(interp, comm) == TCL_OK)
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
#else // DEBUG_PILISTBOX_CONSISTANCY == OFF
#define PiListBox_ConsistancyCheck() TRUE
#endif

// ----------------------------------------------------------------------------
// Update the listbox state according to database and acq state
//
void PiListBox_ErrorMessage( const uchar * pMessage )
{
   assert(dbc != NULL);

   if (pMessage != NULL)
   {
      pibox_state = PIBOX_MESSAGE;

      sprintf(comm, "PiBox_DisplayErrorMessage {%s\n}\n", pMessage);
      eval_check(interp, comm);

      // reset the listbox scrollbar and clear the short-info text field
      sprintf(comm, ".all.pi.list.sc set 0.0 1.0\n");
      eval_check(interp, comm);

      PiDescription_ClearText();
   }
   else if (pibox_state != PIBOX_LIST)
   {
      pibox_state = PIBOX_LIST;

      sprintf(comm, "PiBox_DisplayErrorMessage {}\n");
      eval_check(interp, comm);

      PiListBox_Reset();
   }
}

// ----------------------------------------------------------------------------
// Display short and long info for currently selected item
//
static void PiListBox_UpdateInfoText( bool keepView )
{
   const PI_BLOCK * pPiBlock;

   if (pibox_curpos >= 0)
   {
      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = EpgDbSearchPi(dbc, pibox_list[pibox_curpos].start_time, pibox_list[pibox_curpos].netwop_no);
      if (pPiBlock != NULL)
      {
         PiDescription_UpdateText(pPiBlock, keepView);
      }
      else
         debug2("PiListBox-UpdateInfoText: selected block start=%ld netwop=%d not found", pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);

      EpgDbLockDatabase(dbc, FALSE);
   }
}

// ----------------------------------------------------------------------------
// Draw a date scale to represent the range from first to last matching PI
// - called after refresh or changed by acquisition, i.e. whenever pibox_max,
//   changes (scrolling etc. only moves the slider)
//
static void PiListBox_DrawDateScale( void )
{
   const PI_BLOCK *pPiBlock;
   time_t  t_first;
   time_t  t_last;
   sint    lto;

   t_first = 0;
   t_last  = 0;

   EpgDbLockDatabase(dbc, TRUE);
   pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
   if (pPiBlock != NULL)
   {
      t_first = pPiBlock->start_time;

      pPiBlock = EpgDbSearchLastPi(dbc, pPiFilterContext);
      if (pPiBlock != NULL)
         t_last = pPiBlock->start_time;
   }
   EpgDbLockDatabase(dbc, FALSE);

   if (t_first != 0)
      lto = EpgLtoGet(t_first);
   else
      lto = EpgLtoGet(time(NULL));

   sprintf(comm, "PiDateScale_Redraw %d %d %d\n", (int)t_first, (int)t_last, lto);
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Calculate and set slider position in scrollbar and weekday scales
// - called whenever the list is scrolled or content changes (refresh or acq)
//
static void PiListBox_AdjustScrollBar( void )
{
   // scrollbar gets normalized start and end of viewable fraction
   if (pibox_max == 0)
      sprintf(comm, ".all.pi.list.sc set 0.0 1.0\n");
   else
      sprintf(comm, ".all.pi.list.sc set %f %f\n", (float)pibox_off / pibox_max,
                                                   (float)(pibox_off + pibox_height) / pibox_max);
   eval_check(interp, comm);

   if (pibox_count > 0)
   {
      // pass start times of first and last visible PI to weekday scale
      sprintf(comm, "PiDateScale_SetSlider %d %d %d\n",
                    (int)pibox_list[0].start_time,
                    (int)pibox_list[pibox_curpos].start_time,
                    (int)pibox_list[pibox_count - 1].start_time);
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// tag the selected line (by background color and border)
//
static void PiListBox_ShowCursor( void )
{
   const PI_BLOCK *pPiBlock;
   const char * pColor;
   time_t       curTime;

   curTime = EpgGetUiMinuteTime();

   if (pibox_list[pibox_curpos].start_time <= curTime)
   {
      // check if expired PI are currently visible
      if (curTime != EpgDbFilterGetExpireTime(pPiFilterContext))
      {  // stop time is not cached, so it must be looked up again here
         EpgDbLockDatabase(dbc, TRUE);
         pPiBlock = EpgDbSearchPi(dbc, pibox_list[pibox_curpos].start_time, pibox_list[pibox_curpos].netwop_no);

         if ((pPiBlock != NULL) && (pPiBlock->stop_time <= curTime))
            pColor = "pi_cursor_bg_past";
         else
            pColor = "pi_cursor_bg_now";
         EpgDbLockDatabase(dbc, FALSE);
      }
      else
         pColor = "pi_cursor_bg_now";
   }
   else
      pColor = "pi_cursor_bg";

   sprintf(comm, ".all.pi.list.text tag configure cur -background $%s\n"
                 ".all.pi.list.text tag add cur %d.0 %d.0\n",
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

   sprintf(comm, ".all.pi.list.text delete 1.0 end\n");
   eval_check(interp, comm);
   PiDescription_ClearText();

   EpgDbLockDatabase(dbc, TRUE);
   pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
   pibox_resync = FALSE;
   pibox_count = 0;
   pibox_max = 0;
   pibox_off = 0;
   if (pPiBlock != NULL)
   {
      while ((pPiBlock != NULL) && (pibox_count < pibox_height))
      {
         pibox_list[pibox_count].netwop_no  = pPiBlock->netwop_no;
         pibox_list[pibox_count].start_time = pPiBlock->start_time;
         PiOutput_PiListboxInsert(pPiBlock, pibox_count);
         pibox_count += 1;
	 pibox_max += 1;

         pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
      }

      if (pPiBlock != NULL)
         pibox_max += EpgDbCountPi(dbc, pPiFilterContext, pPiBlock);

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
   PiListBox_DrawDateScale();
   PiListBox_AdjustScrollBar();
}

// ----------------------------------------------------------------------------
// Re-Sync the listbox with the database
//
void PiListBox_Refresh( void )
{
   const PI_BLOCK *pPiBlock, *pPrev, *pNext;
   uchar last_netwop;
   time_t min_time;
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

         while ((pPiBlock != NULL) && (pibox_count < pibox_height))
         {
            pibox_list[pibox_count].netwop_no  = pPiBlock->netwop_no;
            pibox_list[pibox_count].start_time = pPiBlock->start_time;
            PiOutput_PiListboxInsert(pPiBlock, pibox_count);
            pibox_count += 1;
            pibox_max += 1;

            pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
         }

         if (pPiBlock != NULL)
            pibox_max += EpgDbCountPi(dbc, pPiFilterContext, pPiBlock);

         if ((pibox_count < pibox_height) && (pibox_max > pibox_count))
         {  // not enough items found after the cursor -> shift window upwards
            pPiBlock = EpgDbSearchPi(dbc, pibox_list[0].start_time, pibox_list[0].netwop_no);
            if (pPiBlock != NULL)
            {
               while ( (pibox_count < pibox_height) &&
                       ((pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock)) != NULL) )
               {
                  dprintf4("shift up: count=%d, max=%d: insert block start=%ld net=%d\n", pibox_count, pibox_max, pPiBlock->start_time, pPiBlock->netwop_no);
                  for (i=pibox_count-1; i>=0; i--)
                     pibox_list[i + 1] = pibox_list[i];
                  pibox_list[0].netwop_no  = pPiBlock->netwop_no;
                  pibox_list[0].start_time = pPiBlock->start_time;
                  PiOutput_PiListboxInsert(pPiBlock, 0);

                  assert(pibox_off > 0);
                  pibox_off -= 1;
                  pibox_curpos += 1;
                  pibox_count += 1;
               }
               assert((pibox_count >= pibox_height) || ((pibox_off == 0) && (pPiBlock == NULL)));
            }
            else
               debug2("PiListBox-Refresh: first listed block start=%ld netwop=%d not found", pibox_list[0].start_time, pibox_list[0].netwop_no);
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
         PiDescription_ClearText();
      }

      EpgDbLockDatabase(dbc, FALSE);
      assert(PiListBox_ConsistancyCheck());

      // inform the vertical scrollbar about the viewable fraction
      PiListBox_DrawDateScale();
      PiListBox_AdjustScrollBar();
   }
}

// ----------------------------------------------------------------------------
// Possible change in network list -> refresh
//
void PiListBox_AiStateChange( void )
{
   PiListBox_Refresh();

   EpgDbSetPiAcqCallback(dbc, PiListBox_HandleAcqEvent);
}

// ----------------------------------------------------------------------------
// Move the visible window to an absolute position
//
static void PiListBox_ScrollMoveto( int newpos )
{  
   const PI_BLOCK *pPiBlock;
   uint   old_netwop;
   time_t old_start;
   int    search_off;
   int    direction;
   int    i;

   if (pibox_max > pibox_height)
   {
      if (newpos < 0)
         newpos = 0;
      else if (newpos > pibox_max - pibox_height)
         newpos = pibox_max - pibox_height;

      if (newpos != pibox_max)
      {
         sprintf(comm, ".all.pi.list.text delete 1.0 end\n");
         eval_check(interp, comm);
         PiDescription_ClearText();

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

         EpgDbLockDatabase(dbc, TRUE);
         // as an optimization we don't search the requested PI starting at the first
         // PI in the database because that will get slow towards the end of the list
         // with complex filters; in most cases delta to the old position will be slow;
         // (usually it's most efficient to search beginning from the visible PIs)
         if (newpos < pibox_off)
         {
            if (newpos < pibox_off / 2)
            {
               dprintf1("search forward from start: %d\n", newpos);
               pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
               search_off = 0;
               direction = 1;
            }
            else
            {
               dprintf1("search backwards from pi box: %d\n", pibox_off - newpos);
               pPiBlock = EpgDbSearchPi(dbc, pibox_list[0].start_time, pibox_list[0].netwop_no);
               search_off = pibox_off;
               direction = -1;
            }
         }
         else
         {
            if (newpos < pibox_off + pibox_count)
            {
               dprintf1("no search - new PI is visible (#%d)\n", newpos - (pibox_off + pibox_count));
               pPiBlock = EpgDbSearchPi(dbc, pibox_list[newpos - pibox_off].start_time, pibox_list[newpos - pibox_off].netwop_no);
               search_off = newpos;
               direction = 0;
            }
            else if (newpos - (pibox_off + pibox_count) < (pibox_max - (pibox_off + pibox_count)) / 2)
            {
               dprintf1("search forward from pi box: %d\n", newpos - (pibox_off + pibox_count));
               pPiBlock = EpgDbSearchPi(dbc, pibox_list[pibox_count - 1].start_time, pibox_list[pibox_count - 1].netwop_no);
               search_off = pibox_off + pibox_count - 1;
               direction = 1;
            }
            else
            {
               dprintf1("search backwards from end: %d\n", pibox_max - newpos);
               pPiBlock = EpgDbSearchLastPi(dbc, pPiFilterContext);
               search_off = pibox_max - 1;
               direction = -1;
            }
         }

         if (pPiBlock != NULL)
         {
            if (direction < 0)
            {
               while ((pPiBlock != NULL) && (search_off > newpos))
               {
                  search_off -= 1;
                  pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
               }
            }
            else if (direction > 0)
            {
               while ((pPiBlock != NULL) && (search_off < newpos))
               {
                  search_off += 1;
                  pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
               }
            }

            pibox_count = 0;
            while ((pPiBlock != NULL) && (pibox_count < pibox_height))
            {
               pibox_list[pibox_count].netwop_no  = pPiBlock->netwop_no;
               pibox_list[pibox_count].start_time = pPiBlock->start_time;
               PiOutput_PiListboxInsert(pPiBlock, pibox_count);
               pibox_count += 1;

               pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
            }
            assert(pibox_count >= pibox_height);

            // keep the cursor on the same item as long as its visable
            // then keep the cursor at the top or bottom item
            for (i=0; i < pibox_count; i++)
               if ( (old_netwop == pibox_list[i].netwop_no) &&
                    (old_start == pibox_list[i].start_time) )
                  break;
            if (i < pibox_count)
               pibox_curpos = i;
            else if (newpos < pibox_off)
               pibox_curpos = pibox_count - 1;
            else
               pibox_curpos = 0;
            pibox_off = newpos;
            PiListBox_ShowCursor();

            // display short and long info
            PiListBox_UpdateInfoText(FALSE);
         }
         else
         {
            pibox_count = 0;
            pibox_off = 0;
            pibox_curpos = PIBOX_INVALID_CURPOS;
         }
         EpgDbLockDatabase(dbc, FALSE);

         // adjust the vertical scrollbar
         PiListBox_AdjustScrollBar();

         assert(PiListBox_ConsistancyCheck());
      }
   }
}

// ----------------------------------------------------------------------------
// Scroll one page down
//
static void PiListBox_ScrollPageDown( int delta )
{
   const PI_BLOCK ** pNewPiBlock;
   const PI_BLOCK  * pPiBlock;
   int   new_count = 0;
   int   i, j;

   pNewPiBlock = xmalloc(pibox_height * sizeof(PI_BLOCK *));
   EpgDbLockDatabase(dbc, TRUE);
   if (pibox_count >= pibox_height)
   {
      pPiBlock = EpgDbSearchPi(dbc, pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
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
         debug2("PiListBox-ScrollPageDown: last listed block start=%ld netwop=%d not found", pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
   }
   else
      new_count = 0;

   if (new_count > 0)
   {
      // remove old text
      sprintf(comm, ".all.pi.list.text delete 1.0 %d.0\n", new_count + 1);
      eval_check(interp, comm);

      for (i=new_count; i < pibox_height; i++)
         pibox_list[i - new_count] = pibox_list[i];
      for (i=pibox_height-new_count, j=0; i < pibox_height; i++, j++)
      {
         pibox_list[i].netwop_no  = pNewPiBlock[j]->netwop_no;
         pibox_list[i].start_time = pNewPiBlock[j]->start_time;
         PiOutput_PiListboxInsert(pNewPiBlock[j], i);
      }
      pibox_off += new_count;

      if (new_count > pibox_curpos)
      {  // selected item scrolled away (i.e. enough items to scroll one page)
         // -> don't change cursor position
         PiListBox_ShowCursor();
      }
      else
      {  // selected item still visible -> keep cursor on it
         pibox_curpos -= new_count;
      }
      PiListBox_UpdateInfoText(FALSE);

      // inform the vertical scrollbar about the start offset and viewable fraction
      PiListBox_AdjustScrollBar();
   }
   else if (pibox_curpos < pibox_count - 1)
   {  // no items below -> set cursor to last item
      sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
      eval_check(interp, comm);
      pibox_curpos = pibox_count - 1;
      PiListBox_ShowCursor();
      PiListBox_UpdateInfoText(FALSE);
   }

   EpgDbLockDatabase(dbc, FALSE);
   xfree((void*)pNewPiBlock);
   assert(PiListBox_ConsistancyCheck());
}

// ----------------------------------------------------------------------------
// Scroll one page up
//
static void PiListBox_ScrollPageUp( int delta )
{
   const PI_BLOCK ** pNewPiBlock;
   const PI_BLOCK  * pPiBlock;
   int   new_count;
   int   i, j;

   pNewPiBlock = xmalloc(pibox_height * sizeof(PI_BLOCK *));
   if (pibox_count > 0)
   {
      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = EpgDbSearchPi(dbc, pibox_list[0].start_time, pibox_list[0].netwop_no);
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
               PiOutput_PiListboxInsert(pNewPiBlock[i], 0);
            }
            pibox_off -= new_count;

            if (pibox_curpos + new_count >= pibox_height)
            {  // selected item scrolled away (i.e. enough items to scroll one page)
               // -> don't change cursor position
               PiListBox_ShowCursor();
            }
            else
            {  // selected item still visible -> keep cursor on it
               pibox_curpos += new_count;
            }
            PiListBox_UpdateInfoText(FALSE);

            // inform the vertical scrollbar about the start offset and viewable fraction
            PiListBox_AdjustScrollBar();
         }
         else if (pibox_curpos > 0)
         {  // no items above -> set cursor to first item
            sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
            eval_check(interp, comm);
            pibox_curpos = 0;
            PiListBox_ShowCursor();
            PiListBox_UpdateInfoText(FALSE);
         }
      }
      else
         debug2("PiListBox-ScrollPageUp: first listed block start=%ld netwop=%d not found", pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
      EpgDbLockDatabase(dbc, FALSE);
      assert(PiListBox_ConsistancyCheck());
   }
   xfree((void*)pNewPiBlock);
}

// ----------------------------------------------------------------------------
// Scroll one or more lines down
//
static void PiListBox_ScrollLineDown( int delta )
{
   const PI_BLOCK *pPiBlock;
   int   i;

   if (pibox_count >= pibox_height)
   {
      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = EpgDbSearchPi(dbc, pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
      ifdebug2((pPiBlock==NULL), "PiListBox-ScrollLineDown: last listed block start=%ld netwop=%d not found", pibox_list[pibox_count-1].start_time, pibox_list[pibox_count-1].netwop_no);
      while ((pPiBlock != NULL) && (delta > 0))
      {
         pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
         if (pPiBlock != NULL)
         {
            delta -= 1;

            sprintf(comm, ".all.pi.list.text delete 1.0 2.0\n");
            eval_check(interp, comm);
            for (i=1; i < pibox_height; i++)
               pibox_list[i - 1] = pibox_list[i];
            pibox_list[pibox_count-1].netwop_no  = pPiBlock->netwop_no;
            pibox_list[pibox_count-1].start_time = pPiBlock->start_time;
            PiOutput_PiListboxInsert(pPiBlock, pibox_height - 1);
            pibox_off += 1;

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

      if ((delta > 0) && (pibox_curpos < pibox_height - 1))
      {  // no items below -> move cursor X lines forward
         sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
         eval_check(interp, comm);
         pibox_curpos += delta;
         if (pibox_curpos >= pibox_count)
            pibox_curpos = pibox_count - 1;
         PiListBox_ShowCursor();
         PiListBox_UpdateInfoText(FALSE);
      }
      EpgDbLockDatabase(dbc, FALSE);
      assert(PiListBox_ConsistancyCheck());
   }
}

// ----------------------------------------------------------------------------
// Scroll one or more lines up
// - the last visible item is removed and one inserted at top
// - the cursor remains on the same item, i.e. moves one line down,
//   unless already in the last line
//
static void PiListBox_ScrollLineUp( int delta )
{
   const PI_BLOCK *pPiBlock;
   int   i;

   if (pibox_count >= pibox_height)
   {
      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = EpgDbSearchPi(dbc, pibox_list[0].start_time, pibox_list[0].netwop_no);
      ifdebug2((pPiBlock==NULL), "PiListBox-ScrollLineUp: first listed block start=%ld netwop=%d not found", pibox_list[0].start_time, pibox_list[0].netwop_no);
      while ((pPiBlock != NULL) && (delta < 0))
      {
         pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
         if (pPiBlock != NULL)
         {
            delta += 1;
            sprintf(comm, ".all.pi.list.text delete %d.0 %d.0\n", pibox_height, pibox_height+1);
            eval_check(interp, comm);
            for (i=pibox_height-2; i >= 0; i--)
               pibox_list[i + 1] = pibox_list[i];
            pibox_list[0].netwop_no  = pPiBlock->netwop_no;
            pibox_list[0].start_time = pPiBlock->start_time;
            pibox_off -= 1;

            PiOutput_PiListboxInsert(pPiBlock, 0);
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

      if ((delta < 0) && (pibox_curpos > 0))
      {  // no items above -> move cursor X lines up
         sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
         eval_check(interp, comm);
         if (pibox_curpos > (- delta))
            pibox_curpos += delta;
         else
            pibox_curpos = 0;
         PiListBox_ShowCursor();
         PiListBox_UpdateInfoText(FALSE);
      }
      EpgDbLockDatabase(dbc, FALSE);
      assert(PiListBox_ConsistancyCheck());
   }
}

// ----------------------------------------------------------------------------
// Move the visible window - interface to the scrollbar
//
static int PiListBox_Scroll( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   const char * const pUsage = "Usage: C_PiBox_Scroll {moveto <fract>|scroll <delta> {pages|unit}}";
   uchar * pOption;
   double  fract;
   int     delta;
   int     result;

   pOption = Tcl_GetString(objv[1]);
   if ((objc == 2+1) && (strcmp(pOption, "moveto") == 0))
   {
      result = Tcl_GetDoubleFromObj(interp, objv[2], &fract);
      if (result == TCL_OK)
      {
         PiListBox_ScrollMoveto((int)(0.5 + (double)pibox_max * fract));
      }
   }
   else if ((objc == 3+1) && (strcmp(pOption, "scroll") == 0))
   {
      result = Tcl_GetIntFromObj(interp, objv[2], &delta);
      if (result == TCL_OK)
      {
         pOption = Tcl_GetString(objv[3]);

         if (strcmp(pOption, "pages") == 0)
         {
            if (delta > 0)
               PiListBox_ScrollPageDown(delta);
            else if (delta < 0)
               PiListBox_ScrollPageUp(delta);
         }
         else if (strncmp(pOption, "unit", 4) == 0)
         {
            if (delta > 0)
               PiListBox_ScrollLineDown(delta);
            else if (delta < 0)
               PiListBox_ScrollLineUp(delta);
         }
         else
         {
            Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
            result = TCL_ERROR;
         }
      }
   }
   else
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Move the cursor one line up, scroll if necessary
//
static int PiListBox_CursorDown( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   if (objc == 1)
   {
      if (pibox_curpos == pibox_height-1)
      {  // last item is selected -> scroll down
         PiListBox_ScrollLineDown(1);

	 // set cursor to new last element
	 if (pibox_curpos == pibox_height-2)
	 {
	    // set cursor one line down, onto last item
	    sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	    eval_check(interp, comm);
	    pibox_curpos += 1;
	    PiListBox_ShowCursor();
            PiListBox_UpdateInfoText(FALSE);
	 }
      }
      else if (pibox_curpos < pibox_count-1)
      {  // move cursor one position down (text widget line count starts at 1)
	 sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	 eval_check(interp, comm);
	 pibox_curpos += 1;
	 PiListBox_ShowCursor();
	 PiListBox_UpdateInfoText(FALSE);
      }
      assert(PiListBox_ConsistancyCheck());
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Move the cursor one line down, scroll if necessary
//
static int PiListBox_CursorUp( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   if (objc == 1)
   {
      if (pibox_curpos == 0)
      {  // first item is selected -> scroll up
         PiListBox_ScrollLineUp(-1);

	 if (pibox_curpos == 1)
	 {
	    // set cursor one line down, onto last item
	    sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	    eval_check(interp, comm);
	    pibox_curpos = 0;
	    PiListBox_ShowCursor();
            PiListBox_UpdateInfoText(FALSE);
	 }
      }
      else
      {  // move cursor one position up (text widget line count starts at 1)
	 sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
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
// Function for callbacks which are not supported by this layout
//
static int PiListBox_Dummy( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Jump to the first PI running at the given start time
// - Scrolls the listing so that the first PI that's running at or after the given
//   time is in the first row; if there are not enough PI to fill the listbox,
//   the downmost page is displayed and the cursor set on the first matching PI.
//   If there's no PI at all, the cursor is set on the very last PI in the listing.
// - Unlike with filter function, the PI before the given time are not suppressed,
//   they are just scrolled out (unless there are not enough PI after the start)
// - This function is to be used by time and date navigation menus.
//
static int PiListBox_GotoTime( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const PI_BLOCK *pPiBlock;
   const uchar * pArg;
   EPGDB_TIME_SEARCH_MODE timeMode;
   time_t startTime;
   int min_start;
   int i, delta, param;
   
   if ((objc == 3) && (pibox_state == PIBOX_LIST))
   {
      // determine mode: start or stop time as limiter
      if ( (Tcl_GetBooleanFromObj(interp, objv[1], &min_start) != TCL_OK) ||
           (min_start != FALSE) )
         timeMode = STARTING_AT;
      else
         timeMode = RUNNING_AT;

      pArg = Tcl_GetString(objv[2]);
      // Retrieve the start time from the function parameters
      if (strcmp(pArg, "now") == 0)
      {  // start with currently running
         timeMode = RUNNING_AT;
         startTime = EpgGetUiMinuteTime();
      }
      else if (strcmp(pArg, "next") == 0)
      {  // start with the first that's not yet running -> start the next second
         timeMode = STARTING_AT;
         startTime = 1 + EpgGetUiMinuteTime();
      }
      else if (Tcl_GetIntFromObj(interp, objv[2], &param) == TCL_OK)
      {  // absolute time given (UTC)
         startTime = param;
      }
      else  // internal error
         startTime = 0;

      EpgDbLockDatabase(dbc, TRUE);

      // search the first PI to be displayed
      pPiBlock = EpgDbSearchFirstPiAfter(dbc, startTime, timeMode, pPiFilterContext);
      if (pPiBlock == NULL)
         pPiBlock = EpgDbSearchFirstPiBefore(dbc, startTime, timeMode, pPiFilterContext);

      if (pPiBlock != NULL)
      {
         // Clear the listbox and the description window
         sprintf(comm, ".all.pi.list.text delete 1.0 end\n");
         eval_check(interp, comm);
         PiDescription_ClearText();

         pibox_off = EpgDbCountPrevPi(dbc, pPiFilterContext, pPiBlock);
         pibox_count = 0;
         do
         {
            pibox_list[pibox_count].netwop_no  = pPiBlock->netwop_no;
            pibox_list[pibox_count].start_time = pPiBlock->start_time;
            PiOutput_PiListboxInsert(pPiBlock, pibox_count);
            pibox_count += 1;

            pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
         }
         while ((pibox_count < pibox_height) && (pPiBlock != NULL));

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
            {  // at least one PI was found -> scan backwards from there
               pPiBlock = EpgDbSearchPi(dbc, pibox_list[0].start_time, pibox_list[0].netwop_no);
               if (pPiBlock != NULL)
                  pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
               else
                  debug2("PiListBox-GotoTime: first listed block start=%ld netwop=%d not found", pibox_list[0].start_time, pibox_list[0].netwop_no);

               // shift down the existing PI in the listbox
               for (i = pibox_count - 1; i >= 0; i--)
                  pibox_list[i + delta] = pibox_list[i];
            }

            // set the cursor on the first item that matched the start time, or the last PI if none matched
            pibox_off   -= delta;
            pibox_count += delta;
            if (delta >= pibox_height)
               pibox_curpos = pibox_height - 1;
            else if (delta < pibox_count)
               pibox_curpos = delta;
            else
               pibox_curpos = pibox_count - 1;

            // fill the listbox backwards with the missing PI
            for (delta -= 1; (delta >= 0) && (pPiBlock != NULL); delta--)
            {
               pibox_list[delta].netwop_no  = pPiBlock->netwop_no;
               pibox_list[delta].start_time = pPiBlock->start_time;

               PiOutput_PiListboxInsert(pPiBlock, 0);

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
      PiListBox_DrawDateScale();
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
   int idx;
   bool doRefresh;
   
   if (pibox_state == PIBOX_LIST)
   {
      doRefresh = FALSE;

      if ( EpgDbFilterIsEnabled(pPiFilterContext, FILTER_NETWOP) &&
           !EpgDbFilterIsEnabled(pPiFilterContext, FILTER_NONPERM & ~FILTER_NETWOP) )
      {  // only network filter is enabled -> switch to the new network
         // but check before if the item will be visible with the new filter setting
         fc = EpgDbFilterCopyContext(pPiFilterContext);
         EpgDbFilterDisable(fc, FILTER_NETWOP);
         EpgDbPreFilterDisable(fc, FILTER_NETWOP_PRE);
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
               sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
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
// Set cursor onto the given row (after single mouse click)
// - note: arg #2 is not used by this layout (specifies column)
//
static int PiListBox_SelectItem( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{  
   int newRow;
   int result;

   if ( (objc == 3) &&
        (Tcl_GetIntFromObj(interp, objv[1], &newRow) == TCL_OK) )
   {
      if ( (pibox_state == PIBOX_LIST) &&
           (newRow < pibox_count) && (newRow != pibox_curpos))
      {  
         // set cursor to new line (text widget line count starts at 1)
	 sprintf(comm, ".all.pi.list.text tag remove cur %d.0 %d.0\n", pibox_curpos + 1, pibox_curpos + 2);
	 eval_check(interp, comm);

	 pibox_curpos = newRow;
	 PiListBox_ShowCursor();

	 // display short and long info
	 PiListBox_UpdateInfoText(FALSE);
      }
      result = TCL_OK; 
   }
   else
      result = TCL_ERROR; 

   return result; 
}

// ----------------------------------------------------------------------------
// Insert a newly acquired item into the listbox
//
static void PiListBox_DbInserted( const PI_BLOCK *pPiBlock )
{
   int pos;
   int i;

   if ( EpgDbFilterIsEnabled(pPiFilterContext, FILTER_PROGIDX) || pibox_resync )
   {  // when progidx filter is used insertion is too complicated, hence do complete refresh
      pibox_resync = TRUE;
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
         PiListBox_DrawDateScale();
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
	       PiOutput_PiListboxInsert(pPiBlock, 0);
	       pibox_curpos = 0;  // may have been INVALID
	       sprintf(comm, ".all.pi.list.text tag remove cur 2.0 3.0\n");
	       eval_check(interp, comm);
	       PiListBox_ShowCursor();
	       PiListBox_UpdateInfoText(FALSE);
               PiListBox_DrawDateScale();
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
		  PiOutput_PiListboxInsert(pPiBlock, pos);
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
		  PiOutput_PiListboxInsert(pPiBlock, pos);

		  for (i=0; i < pos; i++)
		     pibox_list[i] = pibox_list[i + 1];
	       }

	       pibox_list[pos].netwop_no  = pPiBlock->netwop_no;
	       pibox_list[pos].start_time = pPiBlock->start_time;

	       // update scrollbar
	       pibox_max += 1;
               PiListBox_DrawDateScale();
	       PiListBox_AdjustScrollBar();
               assert(PiListBox_ConsistancyCheck());
	    }
	 }
	 else
	 {  // new block lies behind viewable area
	    dprintf2("behind visible area off=%d,max=%d\n", pibox_off, pibox_max);
	    pibox_max += 1;
            PiListBox_DrawDateScale();
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
static bool PiListBox_DbPreUpdate( const PI_BLOCK *pObsolete, const PI_BLOCK *pPiBlock )
{
   bool result = FALSE;

   if (pibox_resync == FALSE)
   {
      if ( (EpgDbFilterMatches(dbc, pPiFilterContext, pObsolete) != FALSE) &&
           (EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) == FALSE) )
      {
         dprintf2("item no longer matches the filter: start=%ld, netwop=%d\n", pPiBlock->start_time, pPiBlock->netwop_no);
         PiListBox_DbRemoved(pPiBlock);

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
static void PiListBox_DbPostUpdate( const PI_BLOCK *pObsolete, const PI_BLOCK *pPiBlock )
{
   int pos;

   if (pibox_resync == FALSE)
   {
      if ( EpgDbFilterIsEnabled(pPiFilterContext, FILTER_PROGIDX) )
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
               PiOutput_PiListboxInsert(pPiBlock, pos);
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
               PiListBox_DbRemoved(pPiBlock);
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
                  PiListBox_DbInserted(pPiBlock);
                  assert(PiListBox_ConsistancyCheck());
               }
            }
            else
            {  // remove was already handled in PreUpdate
               //if ( EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) == FALSE )
               //{  // the new item does not match -> remove it
               //   dprintf2("invisible item no longer matches the filter: start=%ld netwop=%d\n", pPiBlock->start_time, pPiBlock->netwop_no);
               //   PiListBox_DbRemoved(pPiBlock);
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
static void PiListBox_DbRemoved( const PI_BLOCK *pPiBlock )
{
   const PI_BLOCK *pTemp, *pPrev, *pNext;
   int i, pos;

   if ( (pibox_resync == FALSE) && (pibox_count > 0) )
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
	 pTemp = EpgDbSearchPi(dbc, pibox_list[0].start_time, pibox_list[0].netwop_no);
	 if (pTemp != NULL)
         {
	    pPrev = EpgDbSearchPrevPi(dbc, pPiFilterContext, pTemp);
            assert(pPrev != pPiBlock);
         }
	 else
	    debug2("PiListBox-DbRemoved: first listed block start=%ld netwop=%d not found", pibox_list[0].start_time, pibox_list[0].netwop_no);

	 pTemp = EpgDbSearchPi(dbc, pibox_list[pibox_count - 1].start_time, pibox_list[pibox_count - 1].netwop_no);
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
	    PiOutput_PiListboxInsert(pPrev, 0);
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
	       PiOutput_PiListboxInsert(pNext, pibox_count - 1);
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

         PiListBox_DrawDateScale();
         PiListBox_AdjustScrollBar();

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
// Process changes in the PI database for the PI listbox
// - invoked by the mgmt module as a callback (function pointer is set during init)
// - database changes are braced by PROC_START and PROC_DONE events;
//   number of changes inside those braces are limited to avoid locking up the GUI
// - resync flag is set to switch from synchronous to asynchronous change mode
//   triggers a refresh upon the recount event
//
static bool PiListBox_HandleAcqEvent( const EPGDB_CONTEXT * usedDbc, EPGDB_PI_ACQ_EVENT event,
                                      const PI_BLOCK * pPiBlock, const PI_BLOCK * pObsolete )
{
   static int cbCallCount = 0;
   bool result = FALSE;

   if ( (pibox_state == PIBOX_LIST) && (usedDbc == dbc) && (pibox_lock == FALSE) )
   {
      cbCallCount += 1;
      if (cbCallCount > PIBOX_MAX_ACQ_CALLBACKS)
      {  // pull the plug on synchronous insertions and removals
         pibox_resync = TRUE;
      }

      switch (event)
      {
         case EPGDB_PI_PROC_START:
            assert(pibox_resync == FALSE);  // missing recount call after last queue processing
            cbCallCount = 0;
            break;
         case EPGDB_PI_INSERTED:
            PiListBox_DbInserted(pPiBlock);
            break;
         case EPGDB_PI_PRE_UPDATE:
            result = PiListBox_DbPreUpdate(pObsolete, pPiBlock);
            break;
         case EPGDB_PI_POST_UPDATE:
            PiListBox_DbPostUpdate(pObsolete, pPiBlock);
            break;
         case EPGDB_PI_REMOVED:
            PiListBox_DbRemoved(pPiBlock);
            break;
         case EPGDB_PI_PROC_DONE:
            if (pibox_resync)
            {
               PiListBox_Refresh();
            }
            cbCallCount = 0;
            break;
         default:
            fatal1("PiListBox-HandleAcqEvent: unknown event %d received", event);
            break;
      }
   }
   else if ( (pibox_state != PIBOX_LIST) && (usedDbc == dbc) && (event == EPGDB_PI_INSERTED) )
   {  // listbox was in an error state -> switch to normal mode
      UiControl_CheckDbState();
      return TRUE;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Lock/Unlock the direct link between listbox and acquisition
//
#if 0
void PiListBox_Lock( bool lock )
{
   if (lock)
   {
      pibox_lock = TRUE;
   }
   else
   {
      pibox_lock = FALSE;
      PiListBox_Refresh();
   }
}
#endif

// ----------------------------------------------------------------------------
// Refresh the listbox after filter change
//
static int PiListBox_RefreshCmd( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   PiListBox_Refresh();
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Refresh the listbox after filter change
//
static int PiListBox_ResetCmd( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
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
      pPiBlock = EpgDbSearchPi(dbc, pibox_list[pibox_curpos].start_time, pibox_list[pibox_curpos].netwop_no);
   }
   else
      pPiBlock = NULL;

   return pPiBlock;
}

// ----------------------------------------------------------------------------
// Returns the CNI and name of the network of the currently selected PI
//
static int PiListBox_GetSelectedNetwop( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiBox_GetSelectedNetwop";
   const AI_BLOCK *pAiBlock;
   Tcl_Obj * pResultList;
   uchar strbuf[10];
   uchar netwop;
   int result;

   if (objc != 1)
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
            pResultList = Tcl_NewListObj(0, NULL);

            sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(strbuf, -1));
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(AI_GET_NETWOP_NAME(pAiBlock, netwop), -1));

            Tcl_SetObjResult(interp, pResultList);
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
static int PiListBox_Resize( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   PIBOX_ENTRY * old_list;
   Tcl_Obj     * pTmpObj;
   int height, off;

   pTmpObj = Tcl_GetVar2Ex(interp, "pibox_height", NULL, TCL_GLOBAL_ONLY);
   if ((pTmpObj == NULL) || (Tcl_GetIntFromObj(interp, pTmpObj, &height) != TCL_OK))
   {  // no height configured or invalid number -> use default
      debug1("PiListBox-Resize: invalid pibox_height \"%s\"", (pTmpObj == NULL) ? "NULL" : Tcl_GetString(pTmpObj));
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
   pibox_state = PIBOX_NOT_INIT;

   if (pibox_list != NULL)
      xfree(pibox_list);
   pibox_list = NULL;

   if (dbc != NULL)
      EpgDbSetPiAcqCallback(dbc, NULL);
}

// ----------------------------------------------------------------------------
// create the listbox and its commands
// - this should be called only once during start-up
//
void PiListBox_Create( void )
{
   pibox_state = PIBOX_NOT_INIT;

   // clear possible error message overlay
   sprintf(comm, "PiBox_DisplayErrorMessage {}");
   eval_check(interp, comm);

   Tcl_CreateObjCommand(interp, "C_PiBox_Scroll", PiListBox_Scroll, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_ScrollHorizontal", PiListBox_Dummy, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_CursorDown", PiListBox_CursorDown, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_CursorUp", PiListBox_CursorUp, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_CursorLeft", PiListBox_Dummy, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_CursorRight", PiListBox_Dummy, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_GotoTime", PiListBox_GotoTime, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_SelectItem", PiListBox_SelectItem, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_Refresh", PiListBox_RefreshCmd, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_Reset", PiListBox_ResetCmd, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_Resize", PiListBox_Resize, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_GetSelectedNetwop", PiListBox_GetSelectedNetwop, (ClientData) NULL, NULL);

   eval_check(interp, "UpdatePiListboxColumns");

   // set the initial listbox height
   PiListBox_Resize(NULL, interp, 0, NULL);

   EpgDbPreFilterDisable(pPiFilterContext, FILTER_NETWOP_PRE2);
   EpgDbSetPiAcqCallback(dbc, PiListBox_HandleAcqEvent);
}

