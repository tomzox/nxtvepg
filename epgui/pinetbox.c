/*
 *  Nextview GUI: PI listbox in "grid" layout
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
 *    Implements a listing of programmes, similar to pilistbox but with
 *    separate columns for each network.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: pinetbox.c,v 1.36 2003/04/06 19:42:24 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DEBUG_PINETBOX_CONSISTANCY OFF
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
#include "epgctl/epgctxctl.h"
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgui/pifilter.h"
#include "epgui/pioutput.h"
#include "epgui/pidescr.h"
#include "epgui/pinetbox.h"

//#undef assert
//#define assert(X) do{if(!(X)){sprintf(debugStr,"assertion (" #X ") failed in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)


// ----------------------------------------------------------------------------
// private data structures
// describing the current state of the listbox

#define NETBOX_ELEM_GAP              1
#define NETBOX_ELEM_MIN_HEIGHT      (2 + NETBOX_ELEM_GAP)
#define NETBOX_DEFAULT_COL_COUNT     4
#define PIBOX_DEFAULT_HEIGHT        25  // should be same as in pilistbox.c
#define INVALID_CUR_IDX          32767

typedef enum
{
   CURTYPE_INVISIBLE,
   CURTYPE_NORMAL,
   CURTYPE_OVERLAY,
   CURTYPE_PREV_NEXT,
} NETBOX_CURTYPE;

typedef struct
{
   sshort       text_row;
   sshort       text_height;
   ushort       netwop;
   time_t       start_time;
   time_t       stop_time;
   time_t       upd_time;
} NETBOX_ELEM;

typedef struct
{
   ushort       entries;
   sshort       start_off;
   sshort       text_rows;
   NETBOX_ELEM  * list;
} NETBOX_COL;

typedef struct
{
   NETBOX_COL * cols;
   uint         col_count;
   sint         height;
   uint         max_elems;

   sint         cur_col;
   sint         cur_req_row;
   time_t       cur_req_time;
   uint         cur_idx;
   NETBOX_CURTYPE cur_type;
   sshort       cur_pseudo_row;
   sshort       cur_pseudo_height;

   bool         pi_resync;           // TRUE when refresh required due to acq.
   uint         pi_count;            // total number of matching PI
   uint         pi_off;              // number of currently invisible PI above the window

   uint         net_off;
   uint         net_count;           // number of netwops in sel2ai (may be less than AI netwop count)
   uchar        net_ai2sel[MAX_NETWOP_COUNT];
} NETBOX_STATE;


static NETBOX_STATE  netbox;

typedef enum 
{
   PIBOX_NOT_INIT,
   PIBOX_MESSAGE,
   PIBOX_LIST
} NETBOX_INIT_STATE;

static NETBOX_INIT_STATE netbox_init_state = PIBOX_NOT_INIT;

// ----------------------------------------------------------------------------
// Misc. types and structures used in sub-functions
//

// parameter type for picking an column item to place the cursor on
// after a re-fill of the listbox
typedef enum
{
   NO_PARTIAL,                          // never select partially displayed element for cursor
   ABOVE_PARTIAL,                       // only select partial if req. row is directly above
   ALLOW_PARTIAL,                       // no discrimination of partials
} PARTIAL_HANDLING;

// parameter type for determining an "anchor" if cursor is in an empty column
typedef enum
{
   CUR_REQ_UP,
   CUR_REQ_DOWN,
   CUR_REQ_GOTO,
} CUR_REQ_HANDLING;

// temporary array elements: one allocated for each column in the listbox
// during downwards und upwards refresh
typedef struct
{
   sint         walkIdx;                // index of the next to-be-refreshed item
   sint         delta;                  // number of rows by which items were shifted up/down
} REFRESH_VECT;


static void PiNetBox_UpdateNetwopMap( void );
static void PiNetBox_AdjustHorizontalScrollBar( void );

#define dbc pUiDbContext         // internal shortcut

// ----------------------------------------------------------------------------
// Check consistancy of state struct with text widget
//
#if DEBUG_PINETBOX_CONSISTANCY == ON
#include <stdarg.h>
static bool PiNetBox_ConsistancyError( uint colIdx, const char * pFmt, ... )
{
   va_list argl;
   char  str_buf[100];

   va_start(argl, pFmt);
   vprintf(pFmt, argl);
   va_end(argl);

   sprintf(str_buf, ".all.pi.list.nets.n_%d dump -text 1.0 end", colIdx);
   if (Tcl_EvalEx(interp, str_buf, -1, 0) == TCL_OK)
   {
      printf("%s\n", Tcl_GetStringResult(interp));
      //fflush(stdout);
      //SHOULD_NOT_BE_REACHED;
   }
   else
      fatal1("PiNetBox-ConsistancyError: failed to dump content of text col %d", colIdx);

   return TRUE;
}

static bool PiNetBox_ConsistancyCheck( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   sint  max_row;
   sint  text_row;
   sint  start_off;
   uint  colIdx;
   uint  elemIdx;

   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      assert(pCol->entries <= netbox.max_elems);
      if (pCol->entries > 0)
      {
         pElem = pCol->list;
         if (pElem->text_row < 0)
            start_off = 0 - pElem->text_row;
         else
            start_off = 0;
         assert(pCol->start_off == start_off);

         pElem = pCol->list + (pCol->entries - 1);
         if (pElem->text_row + pElem->text_height > netbox.height)
            assert(pCol->text_rows == pElem->text_row + pElem->text_height);
         else
            assert(pCol->text_rows == netbox.height);

         pElem = pCol->list;
         for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
         {
            // the first element may start above the visible area, but all others not
            //assert((elemIdx == 0) ? (pElem->text_row + pElem->text_height >= 0) : (pElem->text_row > 0));
            assert(pElem->text_row <= netbox.height);
            assert(pElem->text_height >= NETBOX_ELEM_MIN_HEIGHT);
            assert((elemIdx == 0) || (pElem->text_row >= (pElem - 1)->text_row + (pElem - 1)->text_height));

            // check network, i.e. if the element is in the correct column
            assert(netbox.net_ai2sel[pElem->netwop] == netbox.net_off + colIdx);

            // check if text rows between elements are empty
            if ((elemIdx + 1 < pCol->entries) && (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP <= netbox.height))
               max_row = pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP;
            else
               max_row = netbox.height - 1;
            for (text_row = pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP; text_row <= max_row; text_row++)
            {
               if ( (netbox.cur_type != CURTYPE_PREV_NEXT) ||
                    (text_row < netbox.cur_pseudo_row) ||
                    (text_row > netbox.cur_pseudo_row + 1) )
               {
                  sprintf(comm, ".all.pi.list.nets.n_%d index %d.end\n", colIdx, text_row + start_off + 1);
                  assert(Tcl_Eval(interp, comm) == TCL_OK);
                  sprintf(comm, "%d.0", text_row + start_off + 1);
                  if (strcmp(interp->result, comm) != 0)
                  {
                     PiNetBox_ConsistancyError(colIdx, "ERROR col %d, gap after elem #%d: rows %d-%d: row %d not empty: %s\n", colIdx, elemIdx, pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP, max_row, text_row, interp->result);
                     break;
                  }
               }
            }
         }

         // check cursor index and position
         sprintf(comm, ".all.pi.list.nets.n_%d tag nextrange cur 1.0\n", colIdx);
         assert(Tcl_Eval(interp, comm) == TCL_OK);
         if (netbox.cur_col == colIdx)
         {
            if (netbox.cur_idx < pCol->entries)
            {
               assert(netbox.cur_type == CURTYPE_NORMAL);
               pElem = pCol->list + netbox.cur_idx;
               sprintf(comm, "%d.0 %d.0", pElem->text_row + pCol->start_off + 1, pElem->text_row + pElem->text_height + pCol->start_off - NETBOX_ELEM_GAP + 1);
               if (strcmp(interp->result, comm) != 0)
                  PiNetBox_ConsistancyError(colIdx, "ERROR cursor pos wrong: '%s' instead of '%d.0 %d.0'", Tcl_GetStringResult(interp), pElem->text_row + pCol->start_off + 1, pElem->text_row + pElem->text_height + pCol->start_off - NETBOX_ELEM_GAP + 1);

               // check that there's not another tagged area below
               sprintf(comm, ".all.pi.list.nets.n_%d tag nextrange cur %d.0\n", colIdx, pElem->text_row + pElem->text_height + pCol->start_off - NETBOX_ELEM_GAP + 1);
               assert(Tcl_Eval(interp, comm) == TCL_OK);
               if (strlen(interp->result) != 0)
                  PiNetBox_ConsistancyError(colIdx, "ERROR 2nd cursor at '%s'; cursor '%d.0 %d.0'", Tcl_GetStringResult(interp), pElem->text_row + pCol->start_off + 1, pElem->text_row + pElem->text_height + pCol->start_off - NETBOX_ELEM_GAP + 1);
            }
            else if (netbox.cur_idx == INVALID_CUR_IDX)
            {  // no element selected -> there must be a pseudo-cursor
               assert(netbox.cur_type != CURTYPE_NORMAL);
               // check if there's really no suitable element in the column
               pElem = pCol->list;
               for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
                  if ((pElem->text_row >= 0) &&
                      (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP <= netbox.height))
                     fatal5("ERROR col %d: no cursor although non-partial elem #%d available at rows %d-%d (<= %d)", colIdx, elemIdx, pElem->text_row, pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP, netbox.height);
            }
            else
               fatal3("illegal cursor index %d (>= idx count %d) in col %d", netbox.cur_idx, pCol->entries, netbox.cur_col);
         }
         else
         {  // cursor is not in this column -> no range in this widget should be tagged
            if (strlen(interp->result) != 0)
               PiNetBox_ConsistancyError(colIdx, "ERROR column w/o cursor has cur tag: %s", interp->result);
         }
      }
      else
      {
         start_off = 0;
         assert(pCol->start_off == 0);
         assert(pCol->text_rows == netbox.height);
         assert((netbox.cur_col != colIdx) || (netbox.cur_type != CURTYPE_NORMAL));
      }

      // check number of lines in text widget
      max_row = netbox.height;
      if (pCol->entries > 0)
      {
         pElem = netbox.cols[colIdx].list + pCol->entries - 1;
         if (pElem->text_row + pElem->text_height > max_row)
            max_row = pElem->text_row + pElem->text_height;
      }
      sprintf(comm, ".all.pi.list.nets.n_%d index end\n", colIdx);
      assert(Tcl_Eval(interp, comm) == TCL_OK);
      sprintf(comm, "%d.0", start_off + max_row + 2);
      if (strcmp(interp->result, comm) != 0)
         PiNetBox_ConsistancyError(colIdx, "ERROR col %d, has not height %d+%d+2: %s\n", colIdx, start_off, max_row, interp->result);
   }
   assert(netbox.cur_col < netbox.col_count);
   assert(netbox.cur_req_row < netbox.height);
   assert((netbox.net_off + netbox.col_count <= netbox.net_count) || (netbox.col_count > netbox.net_count));
   assert((netbox.net_off + netbox.cur_col < netbox.net_count) || ((netbox.net_count <= netbox.cur_col) && (netbox.net_off == 0)));

   #if 1
   EpgDbLockDatabase(dbc, TRUE);
   pCol = netbox.cols;
   pElem = NULL;
   // search the oldest PI in the listbox
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      if (pCol->entries > 0)
      {
         if ( (pElem == NULL) ||
              (pElem->start_time > pCol->list[0].start_time) ||
              ((pElem->start_time == pCol->list[0].start_time) && (pElem->netwop > pCol->list[0].netwop)) )
         {
            pElem = pCol->list;
         }
      }
   }
   if (pElem != NULL)
   {
      const PI_BLOCK * pPiBlock;
      uint col_with_max;
      sint max_row;
      sint last_row;
      uint elcnt[MAX_NETWOP_COUNT];

      pPiBlock = EpgDbSearchPi(dbc, pElem->start_time, pElem->netwop);
      if (pPiBlock != NULL)
      {
         // check PI count and offset
         assert(netbox.pi_off   == EpgDbCountPrevPi(dbc, pPiFilterContext, pPiBlock));
         assert(netbox.pi_count == netbox.pi_off + EpgDbCountPi(dbc, pPiFilterContext, pPiBlock));

         memset(elcnt, 0, sizeof(elcnt));
         last_row = -32768;
         col_with_max = 0;
         max_row = 0;
         do
         {
            colIdx = netbox.net_ai2sel[pPiBlock->netwop_no];
            assert((colIdx >= netbox.net_off) && (colIdx < netbox.net_off + netbox.col_count));

            if ((colIdx >= netbox.net_off) && (colIdx < netbox.net_off + netbox.col_count))
            {
               colIdx -= netbox.net_off;
               pCol    = netbox.cols + colIdx;

               if (elcnt[colIdx] < pCol->entries)
               {
                  pElem = pCol->list + elcnt[colIdx];
                  assert(pElem->netwop == pPiBlock->netwop_no);
                  assert(pElem->start_time == pPiBlock->start_time);
                  assert(pElem->stop_time == pPiBlock->stop_time);
                  assert(pElem->upd_time  == EpgDbGetPiUpdateTime(pPiBlock));

                  assert(pElem->text_row >= last_row);
                  last_row = pElem->text_row;

                  if (pElem->text_row > max_row)
                  {
                     PiNetBox_ConsistancyError(colIdx, "ERROR col %d: too much space before idx %d, row %d(%+d) > %d\n", colIdx, elcnt[colIdx], pElem->text_row, pCol->start_off, max_row);
                     PiNetBox_ConsistancyError(colIdx, "ERROR col %d: too much space after idx %d, (start_off %d)\n", col_with_max, elcnt[col_with_max] - 1, netbox.cols[col_with_max].start_off);
                  }
                  if (pElem->text_row + pElem->text_height > max_row)
                  {
                     max_row = pElem->text_row + pElem->text_height;
                     col_with_max = colIdx;
                  }

                  elcnt[colIdx] += 1;
               }
               else
               {  // this PI is not in the db: since there are no gaps in the PI sequence
                  // (some may be invisible though) no later PI can be in the listbox
                  break;
               }
            }
            //else: PI outside netwop range matched filter; should never happen

            pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);

         } while (pPiBlock != NULL);

         // check if all elements in the listbox were found in the db
         pCol = netbox.cols;
         for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
         {
            assert(elcnt[colIdx] == pCol->entries);
         }
      }
      else
         fatal4("ERROR: first elem in col %d-%d not found in db: net %d, start %d", netbox.net_ai2sel[pPiBlock->netwop_no], netbox.net_off, pElem->netwop, (int)pElem->start_time);
   }
   else
   {  // listbox is empty -> check if there's really no matching PI in the db
      assert(EpgDbSearchFirstPi(dbc, pPiFilterContext) == NULL);
   }
   EpgDbLockDatabase(dbc, FALSE);
   #else
   netwop = 0;
   #endif
   return TRUE;
}
#else  // DEBUG_PINETBOX_CONSISTANCY == OFF
#define PiNetBox_ConsistancyCheck() TRUE
#endif

// ----------------------------------------------------------------------------
// Refresh display after asynchronous changes in the database (e.g. PI insert by acq)
// - also called directly by all interactive callbacks if a resync is pending
//   before the requested action is performed
//
static void PiNetBox_RefreshEvent( ClientData clientData )
{
   // remove idle event (if exists) if not called by the event handler
   if (PVOID2UINT(clientData) != FALSE)
   {
      RemoveMainIdleEvent(PiNetBox_RefreshEvent, (ClientData) FALSE, FALSE);
   }

   netbox.pi_resync = FALSE;

   PiNetBox_Refresh();
}

// ----------------------------------------------------------------------------
// Update the listbox state according to database and acq state
//
void PiNetBox_ErrorMessage( const uchar * pMessage )
{
   assert(dbc != NULL);

   if (pMessage != NULL)
   {
      netbox_init_state = PIBOX_MESSAGE;

      sprintf(comm, "PiBox_DisplayErrorMessage {%s\n}\n", pMessage);
      eval_check(interp, comm);

      // reset the scrollbars and clear the short-info text field
      sprintf(comm, ".all.pi.list.hsc set 0.0 1.0\n"
                    ".all.pi.list.sc set 0.0 1.0\n");
      eval_check(interp, comm);

      PiDescription_ClearText();
   }
   else if (netbox_init_state != PIBOX_LIST)
   {
      netbox_init_state = PIBOX_LIST;

      sprintf(comm, "PiBox_DisplayErrorMessage {}\n");
      eval_check(interp, comm);

      PiNetBox_Reset();
      PiNetBox_AdjustHorizontalScrollBar();
   }
}

// ----------------------------------------------------------------------------
// Display short and long info for currently selected item
//
static void PiNetBox_UpdateInfoText( bool keepView )
{
   NETBOX_COL     * pCol;
   NETBOX_ELEM    * pElem;
   const PI_BLOCK * pPiBlock;

   if (netbox.cur_col < netbox.col_count)
   {
      pCol  = netbox.cols + netbox.cur_col;

      if (netbox.cur_idx < pCol->entries)
      {
         pElem = pCol->list + netbox.cur_idx;

         EpgDbLockDatabase(dbc, TRUE);
         pPiBlock = EpgDbSearchPi(dbc, pElem->start_time, pElem->netwop);
         if (pPiBlock != NULL)
         {
            PiDescription_UpdateText(pPiBlock, keepView);
         }
         else
            debug2("PiNetBox-UpdateInfoText: selected block start=%ld netwop=%d not found", pElem->start_time, pElem->netwop);

         EpgDbLockDatabase(dbc, FALSE);
      }
      else if (netbox.cur_idx == INVALID_CUR_IDX)
      {
         PiDescription_ClearText();
      }
   }
}

// ----------------------------------------------------------------------------
// Update the network names in the column headers
//
static void PiNetBox_UpdateNetwopNames( void )
{
   const AI_BLOCK * pAiBlock;
   const uchar  * pCfNetname;
   NETBOX_COL   * pCol;
   uchar cni_str[10];
   uint  colIdx;
   uint  netwop;

   EpgDbFilterInitNetwopPreFilter2(pPiFilterContext);
   pCol   = netbox.cols;
   colIdx = 0;

   EpgDbLockDatabase(dbc, TRUE);
   pAiBlock = EpgDbGetAi(dbc);
   if (pAiBlock != NULL)
   {
      for ( ; (colIdx < netbox.col_count) && (colIdx < netbox.net_count); colIdx++, pCol++)
      {
         sprintf(comm, ".all.pi.list.nets.h_%d.b configure -text {", colIdx);

         for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
         {
            if (netbox.net_ai2sel[netwop] == netbox.net_off + colIdx)
            {
               EpgDbFilterSetNetwopPreFilter2(pPiFilterContext, netwop);

               sprintf(cni_str, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);
               pCfNetname = Tcl_GetVar2(interp, "cfnetnames", cni_str, TCL_GLOBAL_ONLY);
               if (pCfNetname == NULL)
                  pCfNetname = AI_GET_NETWOP_NAME(pAiBlock, netwop);

               if (comm[strlen(comm) - 1] != '{')
                  strcat(comm, " / ");
               strcat(comm, pCfNetname);
            }
         }

         //debug1("PiNetBox-UpdateNetwopNames: col idx %d not mapped any netwop", colIdx);

         strcat(comm, "}");
         eval_check(interp, comm);
      }
   }
   EpgDbLockDatabase(dbc, FALSE);

   // set pre-filter to suppress all networks outside of the visible range in db queries
   EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP_PRE2);

   // empty the headers of the remaining columns
   for ( ; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      sprintf(comm, ".all.pi.list.nets.h_%d.b configure -text {}", colIdx);
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// Find previous and next PI in an empty column
//
static void PiNetBox_SearchPrevNext( uint colIdx, time_t cur_time,
                                     const PI_BLOCK ** pPrevPi, const PI_BLOCK ** pNextPi )
{
   FILTER_CONTEXT * pFc;
   const AI_BLOCK * pAiBlock;
   uint  netwop;

   assert(EpgDbIsLocked(dbc));

   if ((pPrevPi != NULL) && (pNextPi != NULL))
   {
      // create a temporary filter context which holds only the networks
      // which are mapped onto the given column and not filtered out
      pFc = EpgDbFilterCopyContext(pPiFilterContext);
      EpgDbFilterInitNetwopPreFilter2(pFc);

      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         // find all netwops which are mapped into this column
         for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
         {
            if (netbox.net_ai2sel[netwop] == netbox.net_off + colIdx)
            {
               EpgDbFilterSetNetwopPreFilter2(pFc, netwop);
            }
         }
      }
      EpgDbFilterEnable(pFc, FILTER_NETWOP_PRE2);

      // note: don't use RUNNING-AT, else the same programme might show up
      // both as prev and next (even if the item is invisible above)
      *pPrevPi = EpgDbSearchFirstPiBefore(dbc, cur_time, STARTING_AT, pFc);
      *pNextPi = EpgDbSearchFirstPiAfter(dbc, cur_time, STARTING_AT, pFc);

      EpgDbFilterDestroyContext(pFc);
   }
   else
      fatal0("PiNetBox-SearchPrevNext: illegal NULL ptr params");
}

// ----------------------------------------------------------------------------
// inform the vertical scrollbar about the start offset and viewable fraction
//
static void PiNetBox_AdjustVerticalScrollBar( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uint  colIdx;
   uint  elemIdx;
   uint  total;
   uint  offset;
   uint  visible;

   if (netbox.pi_count > 0)
   {
      total   = netbox.pi_count;
      offset  = netbox.pi_off;
      visible = 0;

      // count the number of PI displayed on-screen
      pCol = netbox.cols;
      for (colIdx = 0; colIdx < netbox.col_count; colIdx++, pCol++)
      {
         if (pCol->entries > 0)
         {
            pElem = pCol->list + 0;
            for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
            {
               if (pElem->text_row < 0)
               {  // partially cut off at the top
                  // -> increment offset so that scrollbar can be used to scroll upwards
                  offset += 1;
                  // unless completely invisible increment total
                  if (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP > 0)
                  {
                     visible += 1;
                     total   += 1;
                  }
               }
               else
               {
                  visible += pCol->entries - elemIdx;
                  break;
               }
            }
            pElem = pCol->list + (pCol->entries - 1);
            if (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP > netbox.height)
            {  // partially cut off at the bottom -> increase total to allow scrolling downwards
               total += 1;
            }
         }
      }

      sprintf(comm, ".all.pi.list.sc set %f %f\n", (double)offset / total, (double)(offset + visible) / total);
   }
   else
      sprintf(comm, ".all.pi.list.sc set 0.0 1.0\n");
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Inform the horizontal scrollbar about the start offset and viewable fraction
//
static void PiNetBox_AdjustHorizontalScrollBar( void )
{
   if (netbox.net_count > 0)
   {
      sprintf(comm, ".all.pi.list.hsc set %f %f\n", (double)netbox.net_off / netbox.net_count, (double)(netbox.net_off + netbox.col_count) / netbox.net_count);
   }
   else
      sprintf(comm, ".all.pi.list.hsc set 0.0 1.0\n");
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Remove the cursor from the display
//
static void PiNetBox_WithdrawCursor( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uint  row;

   if (netbox.cur_col < netbox.col_count)
   {
      pCol = netbox.cols + netbox.cur_col;

      switch (netbox.cur_type)
      {
         case CURTYPE_NORMAL:
            if (netbox.cur_idx < pCol->entries)
            {
               pElem = pCol->list + netbox.cur_idx;

               sprintf(comm, ".all.pi.list.nets.n_%d tag remove cur %d.0 %d.0\n",
                             netbox.cur_col,
                             pElem->text_row + pCol->start_off + 1,
                             pElem->text_row + pElem-> text_height - NETBOX_ELEM_GAP + pCol->start_off + 1);
               eval_global(interp, comm);
            }
            else
               ifdebug2(netbox.cur_idx != INVALID_CUR_IDX, "PiNetBox-WithdrawCursor: illegal elem idx %d (>= %d)", netbox.cur_idx, pCol->entries);
            break;

         case CURTYPE_PREV_NEXT:
            for (row = 0; row < netbox.cur_pseudo_height; row++)
            {
               sprintf(comm, ".all.pi.list.nets.n_%d delete %d.0 %d.end\n",
                             netbox.cur_col,
                             netbox.cur_pseudo_row + row + pCol->start_off + 1,
                             netbox.cur_pseudo_row + row + pCol->start_off + 1);
               eval_global(interp, comm);
            }
            sprintf(comm, ".all.pi.list.nets.n_%d tag remove cur_pseudo 1.0 end\n", netbox.cur_col);
            eval_global(interp, comm);
            break;

         case CURTYPE_OVERLAY:
            sprintf(comm, ".all.pi.list.nets.n_%d tag remove cur_pseudo_overlay 1.0 end\n", netbox.cur_col);
            eval_global(interp, comm);
            break;


         case CURTYPE_INVISIBLE:
            break;

         default:
            fatal1("PiNetBox-WithdrawCursor: illegal cursor type %d", netbox.cur_type);
            break;
      }

      netbox.cur_type = CURTYPE_INVISIBLE;
   }
}

// ----------------------------------------------------------------------------
// Get the minimum start time of all currently visible elements
// - partially visible elements are only considered if no fully visible ones
//   are available
//
static time_t PiNetBox_GetMinVisibleStartTime( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uint  colIdx;
   sint  elemIdx;
   time_t minFullStartTime;
   time_t minPartStartTime;
   time_t  expireTime;

   minFullStartTime = minPartStartTime = 0;

   pCol = netbox.cols;
   for (colIdx = 0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      // search bottom->top for min. start time
      pElem = pCol->list + pCol->entries - 1;
      for (elemIdx = pCol->entries; elemIdx > 0; elemIdx--, pElem--)
      {
         if ( (pElem->text_row >= 0) &&
              ((pElem->start_time < minFullStartTime) || (minFullStartTime == 0)) )
            minFullStartTime = pElem->start_time;
         if ( (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP >= 0) &&
              ((pElem->start_time < minPartStartTime) || (minPartStartTime == 0)) )
            minPartStartTime = pElem->start_time;
      }
   }
   if (minFullStartTime == 0)
      minFullStartTime = minPartStartTime;

   // replace start time of currently running PI with zero
   expireTime = EpgDbFilterGetExpireTime(pPiFilterContext);
   if (minFullStartTime < expireTime)
      minFullStartTime = 0;

   return minFullStartTime;
}

// ----------------------------------------------------------------------------
// Get the maximum start time of all currently visible elements
//
static time_t PiNetBox_GetMaxVisibleStartTime( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uint  colIdx;
   sint  elemIdx;
   time_t maxFullStartTime;
   time_t maxPartStartTime;

   maxFullStartTime = maxPartStartTime = 0;

   pCol = netbox.cols;
   for (colIdx = 0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      // search top->bottom for max. start time
      pElem = pCol->list + 0;
      for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
      {
         if ( (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP <= netbox.height) &&
              (pElem->start_time > maxFullStartTime) )
            maxFullStartTime = pElem->start_time;
         if (pElem->start_time > maxPartStartTime)
            maxPartStartTime = pElem->start_time;
      }
   }
   if (maxFullStartTime == 0)
      maxFullStartTime = maxPartStartTime;

   return maxFullStartTime;
}

// ----------------------------------------------------------------------------
// Remember user-selected vertical position of the cursor (for column movements)
// - position and start time are taken from selected programme item
// - if none is selected (i.e. the selected column is empty or only has partially
//   visible elements) a suitable PI in other columns is searched for; search
//   direction is determined by the argument
//
static void PiNetBox_UpdateCursorReq( CUR_REQ_HANDLING direction )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;

   if (netbox.cur_col < netbox.col_count)
   {
      pCol = netbox.cols + netbox.cur_col;

      if (netbox.cur_idx < pCol->entries)
      {
         pElem = pCol->list + netbox.cur_idx;

         // copy time and position from selected programme item
         netbox.cur_req_row  = pElem->text_row;
         netbox.cur_req_time = pElem->start_time;

         dprintf3("UpdateCursorReq: ELEM idx %02d row %02d start %s", netbox.cur_idx, netbox.cur_req_row, ctime(&netbox.cur_req_time));
      }
      else
      {  // no item is currently selected (selected column is empty)
         // -> derive "requested" time from other visible items
         if ( (direction == CUR_REQ_UP) ||
              (direction == CUR_REQ_GOTO) )
         {
            netbox.cur_req_time = PiNetBox_GetMinVisibleStartTime();
            netbox.cur_req_row  = 0;

            dprintf2("UpdateCursorReq: UP            row %02d start %s", netbox.cur_req_row, ctime(&netbox.cur_req_time));
         }
         else if (direction == CUR_REQ_DOWN)
         {
            netbox.cur_req_time = PiNetBox_GetMaxVisibleStartTime();
            netbox.cur_req_row  = netbox.height - NETBOX_ELEM_MIN_HEIGHT;

            dprintf2("UpdateCursorReq: DOWN          row %02d start %s", netbox.cur_req_row, ctime(&netbox.cur_req_time));
         }
         else
            fatal1("PiNetBox-UpdateCursorReq: invalid direction %d", direction);
      }
   }
}

// ----------------------------------------------------------------------------
// Tag the selected programme (by background color and border)
//
static void PiNetBox_ShowCursor( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   const char   * pColor;
   time_t  expireTime;

   if (netbox.cur_col < netbox.col_count)
   {
      pCol = netbox.cols + netbox.cur_col;

      if (netbox.cur_idx < pCol->entries)
      {
         netbox.cur_type = CURTYPE_NORMAL;

         pElem = pCol->list + netbox.cur_idx;
         expireTime = EpgDbFilterGetExpireTime(pPiFilterContext);

         //if (pElem->stop_time <= expireTime)
         //   pColor = "pi_cursor_bg_past";
         //else
         if (pElem->start_time <= expireTime)
            pColor = "pi_cursor_bg_now";
         else
            pColor = "pi_cursor_bg";

         sprintf(comm, ".all.pi.list.nets.n_%d tag configure cur -background $%s\n"
                       ".all.pi.list.nets.n_%d tag add cur %d.0 %d.0\n",
                       netbox.cur_col, pColor,
                       netbox.cur_col,
                       pElem->text_row + pCol->start_off + 1,
                       pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP + pCol->start_off + 1);
         eval_global(interp, comm);

         ifdebug2(pElem->text_row < 0, "PiNetBox-ShowCursor: cursor only partially visible on col %d, elem #%d", netbox.cur_col, netbox.cur_idx);
         ifdebug2(pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP > netbox.height, "PiNetBox-ShowCursor: cursor only partially visible on col %d, elem #%d", netbox.cur_col, netbox.cur_idx);
      }
      else
      {  // this column is empty -> display pseudo cursor
         const PI_BLOCK * pPrevPi;
         const PI_BLOCK * pNextPi;
         NETBOX_COL     * pCol;
         NETBOX_ELEM    * pElem;
         time_t cur_time;
         sint   elemIdx;
         uchar  start_str[40];
         uchar  stop_str[40];

         pCol = netbox.cols + netbox.cur_col;
         netbox.cur_pseudo_row = ((netbox.height > 1) ? (netbox.height / 2 - 1) : 0);
         netbox.cur_pseudo_height = 2;

         // search nearest PI elements at cursor position
         cur_time = 0;
         pElem = pCol->list;
         for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
         {
            if ( (pElem->text_row >= netbox.cur_pseudo_row) && (cur_time == 0) )
               cur_time = pElem->start_time;

            if ( (netbox.cur_pseudo_row < pElem->text_row) ?
                    (netbox.cur_pseudo_row + 2+1 > pElem->text_row) :
                    (netbox.cur_pseudo_row < 1 + pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP) )
               break;
         }

         if (elemIdx >= pCol->entries)
         {  // no overlapping or adjacent PI element -> display prev/next cursor

            if (cur_time == 0)
               cur_time = netbox.cur_req_time;

            EpgDbLockDatabase(dbc, TRUE);
            PiNetBox_SearchPrevNext(netbox.cur_col, cur_time, &pPrevPi, &pNextPi);
            EpgDbLockDatabase(dbc, FALSE);

            if ((pPrevPi != NULL) || (pNextPi != NULL))
            {
               if (pPrevPi != NULL)
                  strftime(start_str, sizeof(start_str), "Prev: %a %H:%M", localtime(&pPrevPi->start_time));
               else
                  sprintf(start_str, "Prev: none");

               if (pNextPi != NULL)
                  strftime(stop_str, sizeof(stop_str), "Next: %a %H:%M", localtime(&pNextPi->start_time));
               else
                  sprintf(stop_str, "Next: none");

               sprintf(comm, ".all.pi.list.nets.n_%d insert %d.0 {%s} {}\n"
                             ".all.pi.list.nets.n_%d insert %d.0 {%s} {}\n",
                             netbox.cur_col, netbox.cur_pseudo_row + pCol->start_off + 1,
                             start_str,
                             netbox.cur_col, netbox.cur_pseudo_row + 1 + pCol->start_off + 1,
                             stop_str);
               eval_global(interp, comm);
            }
            else
            {  // overlapping or adjacent PI element -> display empty cursor
               sprintf(comm, ".all.pi.list.nets.n_%d insert %d.0 {No match} {}\n",
                             netbox.cur_col,
                             netbox.cur_pseudo_row + pCol->start_off + 1);
               eval_global(interp, comm);

               netbox.cur_pseudo_height = 1;
            }

            netbox.cur_type = CURTYPE_PREV_NEXT;
         }
         else
         {  // partially visible PI -> just insert a tiny cursor
            netbox.cur_type = CURTYPE_OVERLAY;
         }


         sprintf(comm, ".all.pi.list.nets.n_%d tag add %s %d.0 %d.0\n",
                       netbox.cur_col,
                       ((netbox.cur_type == CURTYPE_OVERLAY) ? "cur_pseudo_overlay" : "cur_pseudo"),
                       netbox.cur_pseudo_row + pCol->start_off + 1,
                       netbox.cur_pseudo_row + netbox.cur_pseudo_height + pCol->start_off + 1);
         eval_global(interp, comm);
      }
   }
   else
      debug2("PiNetBox-ShowCursor: invalid cursor row %d (>= col count %d)", netbox.cur_col, netbox.col_count);
}

// ----------------------------------------------------------------------------
// Clear all programmes in all network cols
//
static void PiNetBox_ClearRows( void )
{
   NETBOX_COL     *pCol;
   uint  colIdx;

   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++)
   {
      sprintf(comm, ".all.pi.list.nets.n_%d delete 1.0 end\n", colIdx);
      eval_check(interp, comm);

      pCol->entries = 0;
      pCol->text_rows = 0;
      pCol->start_off = 0;
      pCol += 1;
   }

   PiDescription_ClearText();
}

// ----------------------------------------------------------------------------
// Set visible area of the text
//
static void PiNetBox_SetVisible( void )
{
   NETBOX_COL     *pCol;
   uint  colIdx;

   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      sprintf(comm, ".all.pi.list.nets.n_%d yview %d.0\n", colIdx, pCol->start_off + 1);
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// Insert empty lines into the given column
//
static void PiNetBox_InsertSpace( uint colIdx, sint textRow, sint rowCount )
{
   NETBOX_COL  * pCol;

   if (colIdx < netbox.col_count)
   {
      pCol = netbox.cols + colIdx;

      assert(rowCount > 0);
      assert(textRow + pCol->start_off >= 0);

      sprintf(comm, ".all.pi.list.nets.n_%d insert %d.0 {",
                    colIdx, textRow + pCol->start_off + 1);

      while (rowCount-- > 0)
      {
         strcat(comm, "\n");
      }
      strcat(comm, "} {}");
      eval_check(interp, comm);
   }
   else
      fatal2("PiNetBox-InsertSpace: illegal col idx %d (>= %d)", colIdx, netbox.col_count);
}

// ----------------------------------------------------------------------------
// Remove the given rows from the text widget in the given column
//
static void PiNetBox_RemoveSpace( uint colIdx, sint textRow, sint rowCount )
{
   NETBOX_COL  * pCol;

   if (colIdx < netbox.col_count)
   {
      pCol = netbox.cols + colIdx;

      assert(rowCount > 0);
      assert(textRow + pCol->start_off >= 0);

      sprintf(comm, ".all.pi.list.nets.n_%d delete %d.0 %d.0\n", colIdx,
                    textRow + pCol->start_off + 1,
                    textRow + pCol->start_off + 1 + rowCount);

      eval_check(interp, comm);
   }
   else
      fatal2("PiNetBox-RemoveSpace: illegal col idx %d (>= %d)", colIdx, netbox.col_count);
}

// ----------------------------------------------------------------------------
// Insert a programme into a network column starting at the given row
//
static uint PiNetBox_InsertPi( const PI_BLOCK * pPiBlock, uint colIdx, sint textRow )
{
   textRow += netbox.cols[colIdx].start_off;

   return PiOutput_PiNetBoxInsert(pPiBlock, colIdx, textRow);
}

// ----------------------------------------------------------------------------
// Fill empty space at the bottom of all columns with blank lines
//
static void PiNetBox_AppendSpace( void )
{
   NETBOX_COL     *pCol;
   uint  colIdx;

   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      if (pCol->text_rows < netbox.height)
      {
         PiNetBox_InsertSpace(colIdx, pCol->text_rows, netbox.height - pCol->text_rows);
         pCol->text_rows = netbox.height;
      }
   }
}

// ----------------------------------------------------------------------------
// Count the number of empty rows at top of the listbox
// - empty rows may result from upwards refresh
//
static sint PiNetBox_CountEmptyRowsAtTop( void )
{
   NETBOX_COL   * pCol;
   uint  colIdx;
   sint  minRow;

   // determine topmost used row among all columns 
   minRow = netbox.height;
   pCol      = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      if ( (pCol->entries > 0) &&
           (minRow > pCol->list[0].text_row) )
      {
         minRow = pCol->list[0].text_row;
      }
   }

   return minRow;
}

// ----------------------------------------------------------------------------
// Count the number of empty rows at bottom of the listbox
// - the gap row is included, except if the very last element is in
//
static sint PiNetBox_CountEmptyRowsAtBottom( void )
{
   const PI_BLOCK * pPiBlock;
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   NETBOX_ELEM  * pLastElem;
   uint  colIdx;
   sint  maxRow;

   pLastElem   = NULL;
   maxRow      = 0;
   pCol        = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      if (pCol->entries > 0)
      {
         pElem = pCol->list + (pCol->entries - 1);
         // determine max. stop row (i.e. gap row + 1)
         if (pElem->text_row + pElem->text_height > maxRow)
            maxRow = pElem->text_row + pElem->text_height;
         // determine max. PI
         if ( (pLastElem == NULL) ||
              (pLastElem->start_time < pElem->start_time) ||
              ((pLastElem->start_time == pElem->start_time) && (pLastElem->netwop < pElem->netwop)) )
         {
            pLastElem   = pElem;
         }
      }
   }

   if (pLastElem != NULL)
   {
      EpgDbLockDatabase(dbc, TRUE);
      pPiBlock = EpgDbSearchLastPi(dbc, pPiFilterContext);
      if ( (pPiBlock != NULL) &&
           (pLastElem->start_time == pPiBlock->start_time) &&
           (pLastElem->netwop == pPiBlock->netwop_no) )
      {  // the very last PI is in the listbox -> do not count the gap row after it
         maxRow -= NETBOX_ELEM_GAP;
      }
      EpgDbLockDatabase(dbc, FALSE);
   }

   if (maxRow < netbox.height)
      return netbox.height - maxRow;
   else
      return 0;
}

// ----------------------------------------------------------------------------
// Align all NOW items to start in the same text row
// - elements are moved up by inserting empty rows below them
// - if that would result in a gap (of more than the default between items)
//   the top row is lowered, i.e. some NOW items are shifted downwards
// - if there's more than one NOW item in a column with multiple networks
//   the alignment is skipped (only makes things worse)
//
static sint PiNetBox_AlignNowItems( sint top_row, bool isRefresh )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uint  colIdx;
   uint  elemIdx;
   sint  maxNowRow, minNonNowRow;
   sint  delRowCount;
   sint  addRowCount;
   time_t expireTime;
   bool  multipleNows;

   expireTime = EpgDbFilterGetExpireTime(pPiFilterContext);
   multipleNows = FALSE;

   // determine the lowest row used by all NOW items (including default gap row)
   maxNowRow    = 0 - netbox.height * 2;
   // also determine highest row used by non-NOW items
   minNonNowRow = netbox.height * 2;

   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      pElem = pCol->list;
      for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
      {
         if (pElem->start_time <= expireTime)
         {
            sint stopRow = pElem->text_row + pElem->text_height;
            if ((elemIdx == 0) && (pElem->text_row > top_row))
               stopRow -= pElem->text_row - top_row;
            if (stopRow > maxNowRow)
               maxNowRow = stopRow;
            if (elemIdx > 0)
               multipleNows = TRUE;
         }
         else if (pElem->start_time > expireTime)
         {
            if (pElem->text_row < minNonNowRow)
               minNonNowRow = pElem->text_row;
            break;
         }

         if (isRefresh)
         {
            sprintf(comm, ".all.pi.list.nets.n_%d tag add now %d.0 %d.0\n", colIdx,
                          pElem->text_row + pCol->start_off + 1,
                          pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP + pCol->start_off + 1);
            eval_global(interp, comm);
         }
      }
   }

   // any NOW items found & any non-NOW items?
   if ( (maxNowRow > (0 - netbox.height * 2)) &&
        (minNonNowRow < netbox.height * 2) &&
        (multipleNows == FALSE) )
   {
      // shift top row down if there would be a gap
      if (minNonNowRow > maxNowRow)
      {
         dprintf2("PiNetBox-AlignNowItems: shift all NOW items down by %d rows below top row %d to avoid gap\n", minNonNowRow - maxNowRow, top_row);
         top_row += minNonNowRow - maxNowRow;
      }

      pCol = netbox.cols;
      for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
      {
         if (pCol->entries > 0)
         {
            pElem = pCol->list;
            if (pElem->start_time <= expireTime)
            {
               if (pElem->text_row > top_row)
               {  // item is too low -> shift it up by inserting space below
                  if (pElem->text_row > 0)
                  {  // item's top row is visible -> must remove any empty rows above
                     if (top_row <= 0)
                        delRowCount = pElem->text_row;
                     else
                        delRowCount = pElem->text_row - top_row;

                     PiNetBox_RemoveSpace(colIdx, 0, delRowCount);
                     pCol->start_off -= delRowCount;
                  }

                  addRowCount = pElem->text_row - top_row;
                  dprintf3("PiNetBox-AlignNowItems: shift NOW item col %d by %d rows up to top row %d\n", colIdx, addRowCount, top_row);

                  PiNetBox_InsertSpace(colIdx, pElem->text_row + pElem->text_height, addRowCount);
                  pElem->text_row -= addRowCount;
                  pCol->start_off += addRowCount;
               }
               else if (pElem->text_row < top_row)
               {
                  delRowCount = top_row - pElem->text_row;
                  PiNetBox_RemoveSpace(colIdx, pElem->text_row + pElem->text_height, delRowCount);
                  pElem->text_row += delRowCount;
                  pCol->start_off -= delRowCount;

                  if (pCol->start_off < 0)
                  {
                     PiNetBox_InsertSpace(colIdx, 0 - pCol->start_off, 0 - pCol->start_off);
                     pCol->start_off = 0;
                  }
               }
            }
         }
      }
   }
   // return the possibly modified top row
   return top_row;
}

// ----------------------------------------------------------------------------
// Scroll the listbox down, i.e. move content towards the top
//
static void PiNetBox_MoveTextUp( sint lineCount )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   NETBOX_ELEM  * pMaxInvisible;
   uint  colIdx;
   uint  elemIdx;
   uint  removed;
   sint  delRows;
   sint  topRow;

   // search for the topmost element in each column which is NOT deleted, because
   // - one invisible element must be kept so that the next refresh doesn't bring it into view
   // - invisible elements must be kept if older elements are visible due to a larger element height
   pMaxInvisible = NULL;
   topRow = lineCount;
   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      pElem = pCol->list;
      for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
      {
         if (pElem->text_row + pElem->text_height > lineCount)
         {  // element is visible
            if (topRow > pElem->text_row)
               topRow = pElem->text_row;
            break;
         }
         else
         {  // element is invisible
            if ( (pMaxInvisible == NULL) ||
                 (pMaxInvisible->start_time < pElem->start_time) ||
                 ((pMaxInvisible->start_time == pElem->start_time) && (pMaxInvisible->netwop < pElem->netwop)) )
            {
               pMaxInvisible = pElem;
            }
         }
      }
   }

   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      removed = 0;
      delRows = lineCount;
      pElem   = pCol->list;

      for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
      {
         if ( (pElem->text_row + pElem->text_height <= lineCount) &&
              (pElem->text_row < topRow) )
         {
            if (pElem != pMaxInvisible)
            {  // element becomes invisible -> remove
               if ((removed == 0) && (pElem->text_row < 0))
                  delRows -= pElem->text_row;  // note: "-" increases since value is negative
               removed += 1;
            }
            else
               dprintf5("KEEP col %d, elem %d, PI: %d-%d: %s", colIdx, elemIdx, (uint)pElem->start_time, (uint)pElem->stop_time, ctime(&pElem->start_time));
         }
         // move element x lines up (may become invisible, start index max be negative)
         pElem->text_row -= lineCount;
      }

      netbox.pi_off += removed;
      pCol->entries -= removed;
      if ((removed > 0) && (pCol->entries > 0))
      {
         memmove(pCol->list, pCol->list + removed, pCol->entries * sizeof(NETBOX_ELEM));

         // if element under cursor is removed, move cursor to following element
         if ((netbox.cur_col == colIdx) && (netbox.cur_idx != INVALID_CUR_IDX))
         {
            if (netbox.cur_idx >= removed)
               netbox.cur_idx -= removed;
            else
               netbox.cur_idx = 0;
         }
      }

      if ((pCol->entries > 0) && (pCol->list[0].text_row < 0))
         delRows += pCol->list[0].text_row;
      if (delRows > 0)
      {
         sprintf(comm, ".all.pi.list.nets.n_%d delete 1.0 %d.0\n", colIdx, delRows + 1);
         eval_check(interp, comm);
      }
      else
         delRows = 0;
      pCol->start_off += lineCount - delRows;

      if (pCol->text_rows > lineCount)
         pCol->text_rows -= lineCount;
      else
         pCol->text_rows  = 0;
   }

   if (netbox.cur_req_row > lineCount)
      netbox.cur_req_row -= lineCount;
   else
      netbox.cur_req_row  = 0;
}

// ----------------------------------------------------------------------------
// Append the given number of text lines
//
static void PiNetBox_FillDownwards( sint delta, bool completeElem )
{
   const PI_BLOCK * pPiBlock;
   NETBOX_ELEM    * pElem;
   NETBOX_ELEM    * pLastElem;
   NETBOX_COL     * pCol;
   sint  last_row;
   sint  max_row;
   sint  stop_row;
   uint  colIdx;
   uint  tmpColIdx;
   time_t expireTime;

   expireTime  = EpgDbFilterGetExpireTime(pPiFilterContext);
   pLastElem   = NULL;
   last_row    = 0;
   max_row     = 0;

   PiNetBox_WithdrawCursor();

   // search the last element displayed (in order of search time and AI network index)
   // and determine max. start and stop row of all elements
   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      if (pCol->entries > 0)
      {
         pElem = pCol->list + pCol->entries - 1;
         // keep maximum start row of all elements
         if (pElem->text_row > last_row)
            last_row = pElem->text_row;
         // keep maximum stop row of all elements
         stop_row = pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP;
         if (stop_row > max_row)
            max_row = stop_row;
         if ( (pLastElem == NULL) ||
              (pElem->start_time > pLastElem->start_time) ||
              ((pElem->start_time == pLastElem->start_time) && (pElem->netwop > pLastElem->netwop)) )
         {
            pLastElem   = pElem;
         }
      }
   }

   EpgDbLockDatabase(dbc, TRUE);
   // get the PI after the last displayed one from the db
   if (pLastElem != NULL)
   {
      pPiBlock = EpgDbSearchPi(dbc, pLastElem->start_time, pLastElem->netwop);
      if (pPiBlock != NULL)
      {
         dprintf4("---- fill downwards: start after row %d netwop %d time %d %s", pLastElem->text_row, pLastElem->netwop, (int)pPiBlock->start_time, ctime(&pPiBlock->start_time));
         pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
      }
      else
         debug3("PiNetBox-FillDownwards: last element not found in db: row %d, netwop %d, time %d", pLastElem->text_row, pLastElem->netwop, (int)pPiBlock->start_time);
   }
   else
      pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);

   if (pPiBlock != NULL)
   {
      do
      {
         colIdx = netbox.net_ai2sel[pPiBlock->netwop_no];
         assert(last_row >= 0);

         if ( (colIdx >= netbox.net_off) &&
              (colIdx < netbox.net_off + netbox.col_count) )
         {
            colIdx -= netbox.net_off;
            pCol    = netbox.cols + colIdx;

            if (pCol->entries == netbox.max_elems)
            {  // table overflow
               debug2("PiNetBox-FillDownwards: table overflow col %d (%d elems)", colIdx, pCol->entries);
               break;
            }

            // the element must start below the last one in the same column
            if (last_row < pCol->text_rows)
               last_row = pCol->text_rows;

            // make sure the element is placed lower than any other which *ended* earlier
            for (tmpColIdx=0; tmpColIdx < netbox.col_count; tmpColIdx++)
            {
               if (netbox.cols[tmpColIdx].entries > 0)
               {
                  pElem = netbox.cols[tmpColIdx].list + netbox.cols[tmpColIdx].entries - 1;
                  if ( (pElem->stop_time <= pPiBlock->start_time) &&
                       (last_row < pElem->text_row + pElem->text_height) )
                     last_row = pElem->text_row + pElem->text_height;
               }
            }

            // make sure the element starts not at top if it's not running NOW but there are NOW elements above
            if ( (pLastElem != NULL) &&
                 (pLastElem->start_time <= expireTime) && (pPiBlock->start_time > expireTime) &&
                 (last_row < NETBOX_ELEM_MIN_HEIGHT))
            {
               last_row = NETBOX_ELEM_MIN_HEIGHT;
            }

            if (last_row < netbox.height + delta)
            {
               dprintf8("FILL-DOWN col %d idx %d row %d(%+d) netwop %d time %d-%d %s", colIdx, pCol->entries, last_row, pCol->start_off, (int)pPiBlock->start_time, pPiBlock->netwop_no, (int)pPiBlock->stop_time, ctime(&pPiBlock->start_time));

               pLastElem = pCol->list + pCol->entries;
               pCol->entries += 1;

               pLastElem->netwop      = pPiBlock->netwop_no;
               pLastElem->start_time  = pPiBlock->start_time;
               pLastElem->stop_time   = pPiBlock->stop_time;
               pLastElem->upd_time    = EpgDbGetPiUpdateTime(pPiBlock);
               pLastElem->text_row    = last_row;

               //printf("col %d, last %d, prev height %d: ADD %d-%d\n", colIdx, last_row, pCol->text_rows, (uint)pPiBlock->start_time, (uint)pPiBlock->stop_time);
               if (pCol->text_rows < last_row)
                  PiNetBox_InsertSpace(colIdx, pCol->text_rows, last_row - pCol->text_rows);

               pLastElem->text_height = PiNetBox_InsertPi(pPiBlock, colIdx, last_row);
               pCol->text_rows        = last_row + pLastElem->text_height;

               // keep maximum stop row of all elements
               stop_row = pLastElem->text_row + pLastElem->text_height - NETBOX_ELEM_GAP;
               if (stop_row > max_row)
                  max_row = stop_row;

               // optionally make sure at least one complete element is added to the selected column
               if ( completeElem && (colIdx == netbox.cur_col) )
               {
                  netbox.cur_idx      = pCol->entries - 1;
                  netbox.cur_req_row  = last_row;
                  netbox.cur_req_time = pLastElem->start_time;

                  if (stop_row > netbox.height + delta)
                  {
                     delta = stop_row - netbox.height;
                  }
                  completeElem = FALSE;
               }
            }
            else
            {
               //printf("col %d, last %d, prev %d, height %d + %d, PI: %d-%d\n", colIdx, last_row, pCol->text_rows, netbox.height, delta, (uint)pPiBlock->start_time, (uint)pPiBlock->stop_time);
               break;
            }
         }
         else
            debug4("PiNetBox-FillDownwards: got unmapped network %d (abs. col idx %d, visible: %d-%d)", pPiBlock->netwop_no, colIdx, netbox.net_off, netbox.col_count);

         pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
      }
      while (pPiBlock != NULL);
   }

   if (pPiBlock == NULL)
   {  // bottom reached
      if (max_row <= netbox.height)
      {  // all items already visible -> do not scroll
         delta = 0;
      }
      else if (max_row < netbox.height + delta)
      {  // some elements not or partially visible -> scroll just enough to show them
         delta = max_row - netbox.height;
      }
   }
   // remove #delta lines of text at top and adjust row indices
   if (delta > 0)
   {
      PiNetBox_MoveTextUp(delta);
   }
   EpgDbLockDatabase(dbc, FALSE);

   // fill empty space at the bottom of all columns with blank lines
   PiNetBox_AppendSpace();
   // set visible range of all text widgets
   PiNetBox_SetVisible();
}

// ----------------------------------------------------------------------------
// Scroll the listbox up, i.e. move content towards the bottom
//
static void PiNetBox_MoveTextDown( sint lineCount )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   NETBOX_ELEM  * pMinInvisible;
   uint  colIdx;
   uint  elemIdx;
   sint  delStart;

   // search for the lowest element in each column which is NOT deleted, because
   // - one invisible element must be kept so that the next refresh doesn't bring it into view
   // - note: in contrary to moving upwards, the elements'  hiehght is not relevant,
   //   hence no invisible elements must be kept due to earlier elements
   pMinInvisible = NULL;
   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      pElem = pCol->list;
      for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
      {
         if (pElem->text_row + lineCount < netbox.height)
         {  // element becomes invisible
            if ( (pMinInvisible == NULL) ||
                 (pMinInvisible->start_time > pElem->start_time) ||
                 ((pMinInvisible->start_time == pElem->start_time) && (pMinInvisible->netwop > pElem->netwop)) )
            {
               pMinInvisible = pElem;
            }
         }
      }
   }

   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      pElem = pCol->list;
      for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
      {
         if (pElem->text_row + lineCount >= netbox.height)
         {
            if (pElem != pMinInvisible)
            {  // element becomes invisible
               // remove this and all following elements; exit loop
               pCol->entries = elemIdx;
               break;
            }
            else
               dprintf5("KEEP col %d, elem %d, PI: %d-%d: %s", colIdx, elemIdx, (uint)pElem->start_time, (uint)pElem->stop_time, ctime(&pElem->start_time));
         }
         // adjust row index
         pElem->text_row += lineCount;
      }

      if (pCol->entries > 0)
      {
         // insert space at top if necessary
         if (pCol->start_off < lineCount)
         {
            uint tmp = lineCount - pCol->start_off;
            pCol->start_off = 0;
            PiNetBox_InsertSpace(colIdx, 0, tmp);
         }
         else
            pCol->start_off -= lineCount;

         pElem = pCol->list + pCol->entries - 1;
         if (pElem->text_row + pElem->text_height > netbox.height)
         {  // last element is partially invisible -> start removing only after
            delStart = pElem->text_row + pElem->text_height;
            pCol->text_rows = pElem->text_row + pElem->text_height;
         }
         else
         {
            delStart = netbox.height;
            pCol->text_rows = netbox.height;
         }
         //printf("delStart %d, text_rows = %d off=%d\n", delStart, pCol->text_rows, pCol->start_off);

         // remove invisible text at the bottom
         sprintf(comm, ".all.pi.list.nets.n_%d delete %d.0 end\n",
                       colIdx, pCol->start_off + delStart + 2);
         eval_check(interp, comm);

         // adjust cursor position
         if (netbox.cur_col == colIdx)
         {
            if (netbox.cur_idx >= pCol->entries)
               netbox.cur_idx = INVALID_CUR_IDX;
         }
      }
      else
      {
         sprintf(comm, ".all.pi.list.nets.n_%d delete 1.0 end\n", colIdx);
         eval_check(interp, comm);

         pCol->text_rows = netbox.height;
         pCol->start_off = 0;

         PiNetBox_InsertSpace(colIdx, 0, netbox.height);
      }
   }

   netbox.cur_req_row += lineCount;
   if (netbox.cur_req_row >= netbox.height)
      netbox.cur_req_row = netbox.height - 1;
}

// ----------------------------------------------------------------------------
// Fill the given number of text lines at the top
//
static void PiNetBox_FillUpwards( sint delta, bool completeElem )
{
   const PI_BLOCK * pPiBlock;
   NETBOX_ELEM    * pElem;
   NETBOX_ELEM    * pLastElem;
   NETBOX_COL     * pCol;
   sint   top_row;
   sint   max_top_row;
   uint   colIdx;
   uint   tmpColIdx;
   time_t expireTime;

   expireTime  = EpgDbFilterGetExpireTime(pPiFilterContext);
   pLastElem   = NULL;
   top_row     = 0;

   PiNetBox_WithdrawCursor();

   // search the first element displayed (in order of search time and AI network index)
   pCol = netbox.cols;
   for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
   {
      if (pCol->entries > 0)
      {
         pElem = pCol->list;
         // keep minimum start row of all elements
         if (pElem->text_row < top_row)
            top_row = pElem->text_row;
         if ( (pLastElem == NULL) ||
              (pElem->start_time < pLastElem->start_time) ||
              ((pElem->start_time == pLastElem->start_time) && (pElem->netwop < pLastElem->netwop)) )
         {
            pLastElem   = pElem;
         }
      }
   }

   EpgDbLockDatabase(dbc, TRUE);
   // get the PI before the first displayed one from the db
   if (pLastElem != NULL)
   {
      pPiBlock = EpgDbSearchPi(dbc, pLastElem->start_time, pLastElem->netwop);
      if (pPiBlock != NULL)
      {
         pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
      }
   }
   else
      pPiBlock = EpgDbSearchLastPi(dbc, pPiFilterContext);

   if (pPiBlock != NULL)
   {
      do
      {
         // map network onto column index
         colIdx = netbox.net_ai2sel[pPiBlock->netwop_no];

         if ( (colIdx >= netbox.net_off) &&
              (colIdx < netbox.net_off + netbox.col_count) )
         {
            colIdx -= netbox.net_off;
            pCol    = netbox.cols + colIdx;

            if (pCol->entries == netbox.max_elems)
            {  // table is full (should never happen, since it's large enough to hold 3 times the max. elem count)
               debug2("PiNetBox-FillUpwards: table overflow col %d (%d elems)", colIdx, pCol->entries);
               break;
            }

            assert(pCol->start_off == (((pCol->entries > 0) && (pCol->list[0].text_row < 0)) ? (0 - pCol->list[0].text_row) : 0));

            // the element must start above the previous one in the same column
            if (0 - pCol->start_off < top_row)
               top_row = 0 - pCol->start_off;

            // make sure NOW elements are placed higher than any non-NOW elements
            if ( (pLastElem != NULL) &&
                 (pLastElem->start_time > expireTime) && (pPiBlock->start_time <= expireTime) &&
                 (top_row > pLastElem->text_row - NETBOX_ELEM_MIN_HEIGHT))
            {
               top_row = pLastElem->text_row - NETBOX_ELEM_MIN_HEIGHT;
            }

            // make sure the element is placed higher than any other which starts later than it ends
            max_top_row = netbox.height;
            for (tmpColIdx=0; tmpColIdx < netbox.col_count; tmpColIdx++)
            {
               if (netbox.cols[tmpColIdx].entries > 0)
               {
                  pElem = netbox.cols[tmpColIdx].list;
                  if ( (pElem->start_time >= pPiBlock->stop_time) &&
                       (max_top_row > pElem->text_row) )
                  {
                     max_top_row = pElem->text_row;
                  }
               }
            }

            if ( ((top_row > 0 - (delta + NETBOX_ELEM_MIN_HEIGHT)) && (max_top_row >= 0 - delta)) ||
                 (pPiBlock->start_time <= expireTime) )
            {
               dprintf8("FILL-UP col %d idx #0 (of %d) row %d(%+d) netwop %d time %d-%d %s", colIdx, pCol->entries, top_row, pCol->start_off, pPiBlock->netwop_no, (int)pPiBlock->start_time, (int)pPiBlock->stop_time, ctime(&pPiBlock->start_time));

               if (netbox.pi_off > 0)
                  netbox.pi_off -= 1;

               if (pCol->entries > 0)
                  memmove(pCol->list + 1, pCol->list, pCol->entries * sizeof(*pCol->list));
               if ((colIdx == netbox.cur_col) && (netbox.cur_idx < pCol->entries))
                  netbox.cur_idx += 1;
               pLastElem = pCol->list;
               pCol->entries += 1;

               pLastElem->netwop      = pPiBlock->netwop_no;
               pLastElem->start_time  = pPiBlock->start_time;
               pLastElem->stop_time   = pPiBlock->stop_time;
               pLastElem->upd_time    = EpgDbGetPiUpdateTime(pPiBlock);

               //printf("col %d, top %d, prev start-off %d: ADD %d-%d\n", colIdx, top_row, pCol->start_off, (uint)pPiBlock->start_time, (uint)pPiBlock->stop_time);

               pLastElem->text_height = PiNetBox_InsertPi(pPiBlock, colIdx, 0 - pCol->start_off);
               pLastElem->text_row    = (0 - pCol->start_off) - pLastElem->text_height;
               pCol->start_off        = 0 - pLastElem->text_row;

               if (top_row + pLastElem->text_height > max_top_row)
               {
                  top_row = max_top_row - pLastElem->text_height;
               }

               if (pLastElem->text_row > top_row)
               {
                  PiNetBox_InsertSpace(colIdx, pLastElem->text_row + pLastElem->text_height,
                                               pLastElem->text_row - top_row);
                  pLastElem->text_row  = top_row;
                  pCol->start_off      = 0 - pLastElem->text_row;
               }
               else if ( (pLastElem->text_row < top_row) &&
                         ((pCol->entries <= 1) || (pCol->list[1].text_row > 0)) )
               {
                  sint delCount = top_row - pLastElem->text_row;
                  if ((pCol->entries > 1) && (pCol->list[1].text_row < delCount))
                     delCount = pCol->list[1].text_row;
                  dprintf3("PiNetBox-FillUpwards: deleting %d blank lines at top of col %d to insert PI with height %d\n", delCount, colIdx, pLastElem->text_height);

                  // remove blank lines after the new item
                  PiNetBox_RemoveSpace(colIdx, pLastElem->text_height - pCol->start_off, delCount);

                  pLastElem->text_row += delCount;
                  pCol->start_off      = 0 - pLastElem->text_row;
               }
               top_row = pLastElem->text_row;

               // optionally make sure at least one complete element is added to the selected column
               if ( completeElem && (colIdx == netbox.cur_col) )
               {
                  netbox.cur_idx = 0;
                  netbox.cur_req_row  = top_row;
                  netbox.cur_req_time = pLastElem->start_time;

                  if (delta < 0 - top_row)
                     delta = 0 - top_row;
                  completeElem = FALSE;
               }
            }
            else
            {
               //printf("col %d, last %d, prev %d, height %d + %d, PI: %d-%d\n", colIdx, top_row, pCol->text_rows, netbox.height, delta, (uint)pPiBlock->start_time, (uint)pPiBlock->stop_time);
               break;
            }
         }
         else
            debug4("PiNetBox-FillUpwards: got unmapped network %d (abs. col idx %d, visible: %d-%d)", pPiBlock->netwop_no, colIdx, netbox.net_off, netbox.col_count);

         pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
      }
      while (pPiBlock != NULL);

      // if the top was reached, start re-aligning top-down
      // (e.g. make sure NOW items have no space above)
      if (pPiBlock == NULL)
      {
         top_row = PiNetBox_AlignNowItems(top_row, FALSE);
      }
   }

   if ((pPiBlock == NULL) && (delta > 0 - top_row))
   {  // top reached -> do not scroll beyond first PI
      delta = (0 - top_row);
   }
   EpgDbLockDatabase(dbc, FALSE);

   // remove #delta lines of text at top and adjust row indices
   if (delta > 0)
   {
      PiNetBox_MoveTextDown(delta);
   }

   PiNetBox_SetVisible();
}

// ----------------------------------------------------------------------------
// Fill listbox so that a given PI is inserted at the given position
//
static void PiNetBox_FillAroundPi( const PI_BLOCK * pPiBlock, uint refColIdx, sint refTextRow )
{
   const PI_BLOCK * pPrevPi;
   NETBOX_COL     * pCol;
   NETBOX_ELEM    * pElem;
   uint  colIdx;
   uint  newReqRow;
   sint  max_row;
   time_t expireTime;

   PiNetBox_WithdrawCursor();

   PiNetBox_ClearRows();

   if (pPiBlock != NULL)
   {
      expireTime = EpgDbFilterGetExpireTime(pPiFilterContext);
      if (pPiBlock->start_time <= expireTime)
      {  // currently running programme -> use first matching PI as start point instead
         pPrevPi = EpgDbSearchFirstPi(dbc, pPiFilterContext);
         if ( (pPrevPi != NULL) &&
              (netbox.net_ai2sel[pPrevPi->netwop_no] >= netbox.net_off) &&
              (netbox.net_ai2sel[pPrevPi->netwop_no] < netbox.net_off + netbox.col_count) )
         {
            refColIdx = netbox.net_ai2sel[pPrevPi->netwop_no] - netbox.net_off;
            pPiBlock = pPrevPi;
         }
      }
      else
      {
         while (1)
         {
            pPrevPi = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
            if ((pPrevPi != NULL) &&
                (pPrevPi->start_time == pPiBlock->start_time) &&
                (netbox.net_ai2sel[pPrevPi->netwop_no] >= netbox.net_off) &&
                (netbox.net_ai2sel[pPrevPi->netwop_no] < netbox.net_off + netbox.col_count) )
            {
               refColIdx = netbox.net_ai2sel[pPrevPi->netwop_no] - netbox.net_off;
               pPiBlock = pPrevPi;
            }
            else
               break;
         }
      }

      netbox.pi_resync = FALSE;
      netbox.pi_off    = EpgDbCountPrevPi(dbc, pPiFilterContext, pPiBlock);
      netbox.pi_count  = netbox.pi_off + EpgDbCountPi(dbc, pPiFilterContext, pPiBlock);

      // insert the given PI at the top of the given column
      if (refColIdx < netbox.col_count)
      {
         pCol = netbox.cols + refColIdx;
         pCol->entries = 1;
         pElem = pCol->list;
         pElem->netwop      = pPiBlock->netwop_no;
         pElem->start_time  = pPiBlock->start_time;
         pElem->stop_time   = pPiBlock->stop_time;
         pElem->upd_time    = EpgDbGetPiUpdateTime(pPiBlock);
         pElem->text_row    = 0;
         pElem->text_height = PiNetBox_InsertPi(pPiBlock, refColIdx, 0);
         pCol->text_rows    = pElem->text_height;
         netbox.cur_idx     = INVALID_CUR_IDX;

         dprintf4("---- fill around: insert ref'PI col %d row 0 netwop %d time %d %s", refColIdx, pElem->netwop, (int)pPiBlock->start_time, ctime(&pPiBlock->start_time));
      }
      else
         debug2("PiNetBox-FillAroundPi: illegal col idx %d (>= %d)", refColIdx, netbox.col_count);

      newReqRow = netbox.cur_req_row - refTextRow;

      PiNetBox_FillDownwards(0, FALSE);

      // determine last used row (i.e. if listbox is full)
      max_row = 0;
      pCol = netbox.cols;
      for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
      {
         if (pCol->entries > 0)
         {
            pElem = pCol->list + pCol->entries - 1;
            if (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP > max_row)
               max_row = pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP;
         }
      }

      // check if listbox not full yet
      if (max_row + refTextRow < netbox.height)
         refTextRow = netbox.height - max_row;
      netbox.cur_req_row = 0;

      // always fill upwards too, since some elements may become partially visible
      PiNetBox_FillUpwards(refTextRow, FALSE);

      // last user-requested cursor row is auto-adapted in fill funcs, must be reverted
      netbox.cur_req_row += newReqRow;
      if (netbox.cur_req_row >= netbox.height)
         netbox.cur_req_row = netbox.height - 1;
   }
   else
   {  // no PI found in any column
      assert(EpgDbSearchFirstPi(dbc, pPiFilterContext) == NULL);

      netbox.pi_off   = 0;
      netbox.pi_count = 0;

      // fill columns at least with space
      PiNetBox_FillDownwards(0, FALSE);
   }
}

// ----------------------------------------------------------------------------
// Select an element which is closest to the given text row in a given column
//
static uint PiNetBox_PickCursorRow( uint colIdx, PARTIAL_HANDLING partial )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   sint  delta;
   sint  stop_row;
   uint  minIdx;
   uint  maxIdx;
   uint  elemIdx = INVALID_CUR_IDX;

   if (colIdx < netbox.col_count)
   {
      pCol = netbox.cols + colIdx;
      if (pCol->entries > 0)
      {
         minIdx = 0;
         maxIdx = pCol->entries;
         pElem = pCol->list;
         for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
         {
            stop_row = pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP;

            if (stop_row <= 0)
            {  // element is completely invisible -> ignore
               minIdx = elemIdx + 1;
            }
            else if (pElem->text_row < 0)
            {  // partially cut off at the top
               if ( (partial == NO_PARTIAL) ||
                    ((partial == ABOVE_PARTIAL) && (netbox.cur_req_row >= stop_row)) )
               {  // ignore
                  minIdx = elemIdx + 1;
               }
            }

            if (pElem->text_row >= netbox.height)
            {  // completely invisible -> ignore
               maxIdx = elemIdx;
               break;
            }
            if (stop_row > netbox.height)
            {  // partially cut off at the end
               if ( (partial == NO_PARTIAL) ||
                    ((partial == ABOVE_PARTIAL) && (netbox.cur_req_row < pElem->text_row)) )
               {  // ignore
                  maxIdx = elemIdx;
                  break;
               }
            }
            if ((netbox.cur_req_row < stop_row) && (elemIdx >= minIdx))
            {
               break;
            }
         }

         // if the last element was unsuitable, check if there was a suitable one before
         if ((elemIdx >= maxIdx) && (maxIdx > 0))
         {
            elemIdx = maxIdx - 1;
            pElem   = pCol->list + elemIdx;
         }

         // if the cursor was not directly above an element select the one which is nearer
         if ( (elemIdx > minIdx) && (elemIdx < maxIdx) &&
              (netbox.cur_req_row < pElem->text_row) )
         {
            // calculate number of empty lines between the elements (including the default space)
            delta = pElem->text_row -
                    ((pElem - 1)->text_row + (pElem - 1)->text_height - NETBOX_ELEM_GAP);

            if (netbox.cur_req_row < pElem->text_row - ((delta + 1) / 2))
            {
               elemIdx -= 1;
            }
         }

         if ((elemIdx < minIdx) || (elemIdx >= maxIdx))
         {
            elemIdx = INVALID_CUR_IDX;
         }
      }
   }
   else
      debug2("PiNetBox-PickCursorRow: illegal column index %d (>= %d)", colIdx, netbox.col_count);

   return elemIdx;
}

// ----------------------------------------------------------------------------
// Scroll vertically if the element under the cursor is only partially visible
// - must be called when cursor pos is picked in ABOVE_ or ALLOW_PARTIAL mode
//
static void PiNetBox_ScrollIfPartial( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;

   if (netbox.cur_col < netbox.col_count)
   {
      pCol  = netbox.cols + netbox.cur_col;
      if (netbox.cur_idx < pCol->entries)
      {
         pElem = pCol->list + netbox.cur_idx;

         if (pElem->text_row < 0)
         {
            // search for the first partial, i.e. skip invisibles
            while ( (netbox.cur_idx + 1 < pCol->entries) && ((pElem + 1)->text_row < 0) )
            {
               netbox.cur_idx += 1;
               pElem          += 1;
            }
            PiNetBox_FillUpwards(0 - pElem->text_row, FALSE);
            PiNetBox_AdjustVerticalScrollBar();
         }
         else if (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP > netbox.height)
         {
            // search for the first partial, i.e. skip invisibles
            while ( (netbox.cur_idx > 0) &&
                    ((pElem - 1)->text_row + (pElem - 1)->text_height - NETBOX_ELEM_GAP > netbox.height) )
            {
               netbox.cur_idx -= 1;
               pElem          -= 1;
            }
            PiNetBox_FillDownwards(pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP - netbox.height, FALSE);
            PiNetBox_AdjustVerticalScrollBar();
         }
      }
      else
         ifdebug3((netbox.cur_idx != INVALID_CUR_IDX), "PiNetBox-ScrollIfPartial: illegal cursor index %d (>= %d) in col %d", netbox.cur_idx, pCol->entries, netbox.cur_col);
   }
   else
      debug2("PiNetBox-ScrollIfPartial: illegal cursor column %d (>= %d)", netbox.cur_col, netbox.col_count);
}

// ----------------------------------------------------------------------------
// Checks if there's an invisible or partially visible PI at top of the listbox
//
static bool PiNetBox_IsPartialElemAtTop( uint * pNewIdx )
{
   NETBOX_COL   * pCol;
   bool  result;

   if (netbox.cur_col < netbox.col_count)
   {
      pCol  = netbox.cols + netbox.cur_col;

      if ((pCol->entries > 0) && (pCol->list[0].text_row < 0))
      {
         if (pNewIdx != NULL)
         {  // search for the first partial, i.e. skip invisibles
            *pNewIdx = 0;
            while ((*pNewIdx + 1 < pCol->entries) && (pCol->list[*pNewIdx + 1].text_row < 0))
            {
               *pNewIdx += 1;
            }
         }
         result = TRUE;
      }
      else
         result = FALSE;
   }
   else
      result = FALSE;

   return result;
}

// ----------------------------------------------------------------------------
// Checks if there's an invisible or partially visible PI at the bottom of the listbox
//
static bool PiNetBox_IsPartialElemAtBottom( uint * pNewIdx )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   bool  result;

   if (netbox.cur_col < netbox.col_count)
   {
      pCol  = netbox.cols + netbox.cur_col;
      if (pCol->entries > 0)
      {
         pElem = pCol->list + pCol->entries - 1;
         
         if ((pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP > netbox.height))
         {
            if (pNewIdx != NULL)
            {  // search for the first partial, i.e. skip invisibles
               *pNewIdx = pCol->entries - 1;
               while ( (*pNewIdx > 0) &&
                       ((pElem - 1)->text_row + (pElem - 1)->text_height - NETBOX_ELEM_GAP > netbox.height) )
               {
                  pElem    -= 1;
                  *pNewIdx -= 1;
               }
            }
            result = TRUE;
         }
         else
            result = FALSE;
      }
      else
         result = FALSE;
   }
   else
      result = FALSE;

   return result;
}

// ----------------------------------------------------------------------------
// Check if the currently selected item remains visible after hor. scrolling
//
static const PI_BLOCK * PiNetBox_PickCurPi( uint startCol, uint colCount,
                                            uint * pColIdx, sint * pTextRow, uint * pElemIdx )
{
   const PI_BLOCK * pPiBlock;
   NETBOX_ELEM    * pElem;

   pPiBlock   = NULL;

   if ( (netbox.cur_col < netbox.col_count) &&
        (netbox.cur_idx < netbox.cols[netbox.cur_col].entries) )
   {
      if ((netbox.cur_col >= startCol) && (netbox.cur_col < startCol + colCount))
      {
         // a visible item is selected -> use that as reference
         pElem     = netbox.cols[netbox.cur_col].list + netbox.cur_idx;
         pPiBlock  = EpgDbSearchPi(dbc, pElem->start_time, pElem->netwop);

         *pColIdx  = netbox.cur_col;
         *pElemIdx = netbox.cur_idx;
         *pTextRow = pElem->text_row;

         if (pPiBlock != NULL)
            dprintf7("PickCurPi: col %d idx %d row %d networp %d time %d-%d %s", netbox.cur_col, netbox.cur_idx, pElem->text_row, pElem->netwop, (int)pPiBlock->start_time, (int)pPiBlock->stop_time, ctime(&pElem->start_time));
      }
   }
   return pPiBlock;
}

// ----------------------------------------------------------------------------
// Pick "reference" PI among currently visible elements
// - used to refill the listbox around the selected PI
// - the column with the cursor is preferred; is no match is found there, all
//   other columns are searched except those which are scrolled out
// - a PI is suitable when it covers the start time of the last selected programme
//
static const PI_BLOCK * PiNetBox_PickVisiblePi( uint startCol, uint colCount,
                                                uint * pColIdx, sint * pTextRow, uint * pElemIdx )
{
   NETBOX_COL     * pCol;
   NETBOX_ELEM    * pElem;
   const PI_BLOCK * pPiBlock;
   time_t expireTime;
   uint  colIdx;
   uint  elemIdx;

   pPiBlock   = NULL;

   if ((pColIdx != NULL) && (pTextRow != NULL) && (pElemIdx != NULL))
   {
      expireTime = EpgDbFilterGetExpireTime(pPiFilterContext);

      // first check search the column selected by the cursor
      if ((netbox.cur_col >= startCol) && (netbox.cur_col < netbox.col_count))
      {
         pCol = netbox.cols + netbox.cur_col;
         if (netbox.cur_idx < pCol->entries)
         {
            pElem = pCol->list + netbox.cur_idx;

            if ( ( (netbox.cur_req_time < expireTime) && (pElem->start_time < expireTime) ) ||
                 ( (pElem->start_time >= netbox.cur_req_time) &&
                   (pElem->stop_time < netbox.cur_req_time) &&
                   (pElem->text_row >= 0) &&
                   (abs(pElem->text_row - netbox.cur_req_row) < NETBOX_ELEM_MIN_HEIGHT) ))
            {
               pPiBlock = EpgDbSearchPi(dbc, pElem->start_time, pElem->netwop);

               if ((pPiBlock != NULL) && EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock))
               {
                  *pColIdx  = netbox.cur_col;
                  *pElemIdx = netbox.cur_idx;
                  *pTextRow = pElem->text_row;

                  dprintf7("PickVisiblePi: col %d idx %d row %d networp %d time %d-%d %s", netbox.cur_col, netbox.cur_idx, pElem->text_row, pElem->netwop, (int)pPiBlock->start_time, (int)pPiBlock->stop_time, ctime(&pElem->start_time));
               }
               else
                  pPiBlock = NULL;
            }
         }
      }

      if (pPiBlock == NULL)
      {
         // no PI found yet -> check all other network's columns in the given range
         pCol = netbox.cols + startCol;
         for (colIdx = startCol; (colIdx < startCol + colCount) && (colIdx < netbox.col_count); colIdx++, pCol++)
         {
            if (colIdx != netbox.cur_col)
            {
               pElem = pCol->list;
               for (elemIdx=0; elemIdx < pCol->entries; elemIdx++)
               {
                  if ( ( (netbox.cur_req_time < expireTime) && (pElem->start_time < expireTime) ) ||
                       ( (pElem->start_time <= netbox.cur_req_time) &&
                         (netbox.cur_req_time < pElem->stop_time) &&
                         (pElem->text_row >= 0) &&
                         (abs(pElem->text_row - netbox.cur_req_row) < NETBOX_ELEM_MIN_HEIGHT) ))
                  {
                     pPiBlock = EpgDbSearchPi(dbc, pElem->start_time, pElem->netwop);
                     if ((pPiBlock != NULL) && EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock))
                     {
                        *pColIdx  = colIdx;
                        *pTextRow = pElem->text_row;
                        *pElemIdx = elemIdx;

                        dprintf7("PickVisiblePi: col %d idx %d row %d networp %d time %d-%d %s", netbox.cur_col, netbox.cur_idx, pElem->text_row, pElem->netwop, (int)pPiBlock->start_time, (int)pPiBlock->stop_time, ctime(&pElem->start_time));
                        // done -> break inner and outer loop
                        goto found_pi;
                     }
                     else
                        pPiBlock = NULL;
                  }
               }
            }
         }
      }
   }
   else
      fatal3("PiNetBox-PickCurPi: illegal NULL ptr param pColIdx=%ld pTextRow=%ld pElemIdx=%ld", (long)pColIdx, (long)pTextRow, (long)pElemIdx);

found_pi:
   return pPiBlock;
}

// ----------------------------------------------------------------------------
// Pick a "reference" PI around which the listbox is refilled
//
static const PI_BLOCK * PiNetBox_PickNewPi( uint * pColIdx, sint * pTextRow, uint * pElemIdx )
{
   const PI_BLOCK * pPiBlock = NULL;
   NETBOX_COL     * pCol;
   NETBOX_ELEM    * pElem;
   uint  colIdx;
   uint  elemIdx;

   if ((pColIdx != NULL) && (pTextRow != NULL) && (pElemIdx != NULL))
   {
      pPiBlock = EpgDbSearchFirstPiAfter(dbc, netbox.cur_req_time, RUNNING_AT, pPiFilterContext);
      if (pPiBlock == NULL)
         pPiBlock = EpgDbSearchFirstPiBefore(dbc, netbox.cur_req_time, RUNNING_AT, pPiFilterContext);

      if (pPiBlock != NULL)
      {
         colIdx = netbox.net_ai2sel[pPiBlock->netwop_no];
         if ((colIdx >= netbox.net_off) && (colIdx < netbox.net_off + netbox.col_count))
         {
            colIdx -= netbox.net_off;

            // search the PI in the listbox
            pCol    = netbox.cols + colIdx;
            pElem   = pCol->list;
            for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
               if ( (pElem->netwop == pPiBlock->netwop_no) &&
                    (pElem->start_time == pPiBlock->start_time) &&
                    (pElem->text_row >= 0) &&
                    (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP <= netbox.height) )
                  break;

            if (elemIdx < pCol->entries)
            {  // element is currently fully visible -> keep it's position
               *pTextRow = pElem->text_row;
               *pElemIdx = elemIdx;
            }
            else
            {  // element is not or only partially visible -> place it at cursor row
               *pTextRow = netbox.cur_req_row;
               *pElemIdx = INVALID_CUR_IDX;
            }
            *pColIdx  = colIdx;

            dprintf7("PickNewPi: col %d idx %d row %d networp %d time %d-%d %s", netbox.cur_col, netbox.cur_idx, pElem->text_row, pElem->netwop, (int)pPiBlock->start_time, (int)pPiBlock->stop_time, ctime(&pElem->start_time));
         }
         else
            pPiBlock = NULL;
      }
   }
   else
      fatal3("PiNetBox-PickNewPi: illegal NULL ptr param %ld,%ld,%ld", (long)pColIdx, (long)pTextRow, (long)pElemIdx);

   return pPiBlock;
}

// ----------------------------------------------------------------------------
// Check if the network filter setting has changes
//
static bool PiNetBox_CheckNetwopFilterChange( void )
{
   const AI_BLOCK * pAiBlock;
   uchar netFilter[MAX_NETWOP_COUNT];
   uint  netwop;
   bool  result = FALSE;

   EpgDbLockDatabase(dbc, TRUE);
   pAiBlock = EpgDbGetAi(dbc);
   if (pAiBlock != NULL)
   {
      EpgDbFilterGetNetwopFilter(pPiFilterContext, netFilter, pAiBlock->netwopCount);

      for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
      {
         if ((netFilter[netwop] == FALSE) ^ (netbox.net_ai2sel[netwop] == 0xff))
         {  // network is not filtered out but not part of the selection
            // OR filtered but part of the selection -> mismatch
            result = TRUE;
            break;
         }
      }
   }
   EpgDbLockDatabase(dbc, FALSE);

   return result;
}

// ----------------------------------------------------------------------------
// Fill the listbox starting with the first item in the database
//
void PiNetBox_Reset( void )
{
   const PI_BLOCK * pPiBlock;
   NETBOX_ELEM    * pElem;
   bool  isMapChange;

   PiNetBox_WithdrawCursor();

   PiNetBox_ClearRows();

   isMapChange = PiNetBox_CheckNetwopFilterChange();
   if (isMapChange)
   {
      PiNetBox_UpdateNetwopMap();
   }

   if ((netbox.net_off != 0) || isMapChange)
   {
      netbox.net_off = 0;
      PiNetBox_UpdateNetwopNames();
      PiNetBox_AdjustHorizontalScrollBar();
   }

   netbox.cur_col      = 0;
   netbox.cur_idx      = INVALID_CUR_IDX;
   netbox.cur_req_row  = 0;
   netbox.cur_req_time = 0;
   netbox.pi_resync    = FALSE;
   netbox.pi_count     = 0;
   netbox.pi_off       = 0;

   // the first matching PI in the database is displayed (among the visible networks)
   EpgDbLockDatabase(dbc, TRUE);
   pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
   if (pPiBlock != NULL)
   {
      netbox.pi_count = EpgDbCountPi(dbc, pPiFilterContext, pPiBlock);
   }

   PiNetBox_FillDownwards(0, FALSE);

   // place the cursor on the first element, if completely visible
   if (netbox.cols[0].entries > 0)
   {
      pElem = netbox.cols[0].list;
      if (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP <= netbox.height)
      {
         netbox.cur_idx = 0;
      }
   }

   PiNetBox_ShowCursor();
   PiNetBox_AdjustVerticalScrollBar();
   PiNetBox_UpdateInfoText(FALSE);

   assert(PiNetBox_ConsistancyCheck());

   EpgDbLockDatabase(dbc, FALSE);
}

// ----------------------------------------------------------------------------
// Find new max delta vector across all columns
//
static sint PiNetBox_GetMaxDeltaVector( REFRESH_VECT * pWalk )
{
   uint  colIdx;
   sint  newDelta;

   newDelta = 0;
   for (colIdx = 0; colIdx < netbox.col_count; colIdx++)
   {
      if (newDelta < pWalk[colIdx].delta)
         newDelta = pWalk[colIdx].delta;
   }

   dprintf1("DELTA %d\n", newDelta);
   return newDelta;
}

// ----------------------------------------------------------------------------
// Refresh the listbox content following a given "reference" PI block
// - the given block must be currently displayed (note: if there's no suitable
//   block in the listbox it's refilled instead of refreshed)
// - items are only redrawn if they have changed (according to update timestamp)
// - main goal is to induce as little changes in item placement as possible
// - if an item is inserted or it's size increases, following items are shifted
//   down as neccessary (same "last_row" algorithm as in FillDownwards) by
//   inserting empty rows above
// - if an items is removed or becomes shorter, following items in all columns may
//   be shifted upwards by up to the number of deleted lines; when an item is found later
//
static void PiNetBox_RefreshDownwards( const PI_BLOCK * pPiBlock, uint colIdx )
{
   NETBOX_COL     * pCol;
   NETBOX_COL     * pTmpCol;
   NETBOX_ELEM    * pElem;
   NETBOX_ELEM    * pLastElem;
   NETBOX_ELEM    * pTmpElem;
   REFRESH_VECT   * pWalk;
   uint  elemIdx;
   uint  tmpColIdx;
   sint  last_row;
   sint  maxDelta;
   sint  old_stop_row;
   sint  last_row_in_col;
   time_t expireTime;

   assert(EpgDbIsLocked(dbc));

   pWalk = xmalloc(sizeof(*pWalk) * netbox.col_count);
   memset(pWalk, 0, sizeof(*pWalk) * netbox.col_count);
   pElem = NULL;

   if ((pPiBlock != NULL) && (colIdx < netbox.col_count))
   {
      // search the start element's entry in the listbox
      pCol  = netbox.cols + colIdx;
      pElem = pCol->list;
      for (elemIdx=0; elemIdx < netbox.cols[colIdx].entries; elemIdx++, pElem++)
         if ( (pElem->netwop == pPiBlock->netwop_no) &&
              (pElem->start_time == pPiBlock->start_time) )
            break;

      if (elemIdx >= netbox.cols[colIdx].entries)
      {
         fatal3("PiNetBox-RefreshDownwards: reference PI (net %d, start %d) not found in listbox col %d", pPiBlock->netwop_no, (int)pPiBlock->start_time, colIdx);
         pElem = NULL;
      }
   }
   else
      fatal3("PiNetBox-RefreshDownwards: call with NULL ptr %ld or invalid col %d >= %d", (long)pPiBlock, colIdx, netbox.col_count);

   if ((pPiBlock != NULL) && (pElem != NULL))
   {
      dprintf5("---- refresh downwards: start with col %d row %d(%+d) time %d %s", colIdx, pElem->text_row, netbox.cols[colIdx].start_off, (int)pPiBlock->start_time, ctime(&pPiBlock->start_time));
      last_row    = pElem->text_row;
      expireTime  = EpgDbFilterGetExpireTime(pPiFilterContext);
      pLastElem   = NULL;
      maxDelta    = 0;

      // search the starting point in all other columns
      pCol = netbox.cols;
      for (colIdx = 0; colIdx < netbox.col_count; colIdx++, pCol++)
      {
         pElem = pCol->list;
         for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
            if ( (pElem->start_time > pPiBlock->start_time) ||
                 ((pElem->start_time == pPiBlock->start_time) &&
                  (pElem->netwop >= pPiBlock->netwop_no)) )
               break;

         pWalk[colIdx].walkIdx = elemIdx;
      }

      do
      {
         colIdx = netbox.net_ai2sel[pPiBlock->netwop_no];
         if ( (colIdx >= netbox.net_off) &&
              (colIdx < netbox.net_off + netbox.col_count) )
         {
            colIdx -= netbox.net_off;
            pCol    = netbox.cols + colIdx;
            pElem   = pCol->list + pWalk[colIdx].walkIdx;

            // the element must start below the preceding one in the same column
            if ( (pWalk[colIdx].walkIdx > 0) &&
                 (last_row < (pElem - 1)->text_row + (pElem - 1)->text_height) )
               last_row = (pElem - 1)->text_row + (pElem - 1)->text_height;

            // make sure the element is placed lower than any other which *ended* earlier
            for (tmpColIdx=0; tmpColIdx < netbox.col_count; tmpColIdx++)
            {
               if (pWalk[tmpColIdx].walkIdx > 0)
               {
                  pTmpElem = netbox.cols[tmpColIdx].list + pWalk[tmpColIdx].walkIdx - 1;
                  if ( (pTmpElem->stop_time <= pPiBlock->start_time) &&
                       (last_row < pTmpElem->text_row + pTmpElem->text_height) )
                     last_row = pTmpElem->text_row + pTmpElem->text_height;
               }
            }

            // make sure the element starts not at top if it's not running NOW but there are NOW elements above
            // XXX doesn't work for reference element b/c last elem ptr is NULL
            if ( (pLastElem != NULL) &&
                 (pLastElem->start_time <= expireTime) && (pPiBlock->start_time > expireTime) &&
                 // XXX add start off // also in _Fill functions
                 (last_row < NETBOX_ELEM_MIN_HEIGHT))
            {
               last_row = NETBOX_ELEM_MIN_HEIGHT;
            }

            if (last_row < netbox.height)
            {
               // check for obsolete PI in all columns
               old_stop_row = 0;
               pTmpCol = netbox.cols;
               for (tmpColIdx=0; tmpColIdx < netbox.col_count; tmpColIdx++, pTmpCol++)
               {
                  pTmpElem = pTmpCol->list + pWalk[tmpColIdx].walkIdx;
                  // - note: if there's an element with smaller start time in the listbox, it must have
                  //         been deleted from the db -> remove it; same if starts later but overlaps
                  for (elemIdx = pWalk[tmpColIdx].walkIdx; elemIdx < pTmpCol->entries; elemIdx++, pTmpElem++)
                     if ( (pTmpElem->start_time > pPiBlock->start_time) ||
                          ( (pTmpElem->start_time == pPiBlock->start_time) &&
                            ( (pTmpElem->netwop > pPiBlock->netwop_no) ||
                              ( (pTmpElem->netwop == pPiBlock->netwop_no) &&
                                (pTmpElem->upd_time == EpgDbGetPiUpdateTime(pPiBlock)) ))))
                        break;

                  // note: elemIdx ist the index of the first not to-be-deleted element
                  if (elemIdx > pWalk[tmpColIdx].walkIdx)
                  {  // remove the obsolete elements
                     sint delRows;
                     pTmpElem = pTmpCol->list + pWalk[tmpColIdx].walkIdx;

                     delRows = pTmpCol->list[elemIdx-1].text_row + pTmpCol->list[elemIdx-1].text_height - pTmpElem->text_row;
                     dprintf9("REMOVE col %d idx %d-%d row %d-%d(%+d) time %d-%d %s", tmpColIdx, pWalk[tmpColIdx].walkIdx, elemIdx-1, pTmpElem->text_row, pTmpElem->text_row + delRows, netbox.cols[tmpColIdx].start_off, (int)pTmpElem->start_time, (int)pTmpCol->list[elemIdx-1].stop_time, ctime(&pTmpElem->start_time));

                     PiNetBox_RemoveSpace(tmpColIdx, pTmpElem->text_row, delRows);
                     if (elemIdx +1 -1 < pTmpCol->entries)
                     {  // there are elements behind the removed one -> move them forward; update row index
                        memmove(pTmpElem, pTmpCol->list + elemIdx, (pTmpCol->entries - elemIdx + 1) * sizeof(*pElem));
                     }
                     pTmpCol->entries -= elemIdx - pWalk[tmpColIdx].walkIdx;
                     for (elemIdx = pWalk[tmpColIdx].walkIdx; elemIdx < pTmpCol->entries; elemIdx++)
                        pTmpCol->list[elemIdx].text_row -= delRows;
                     pTmpCol->text_rows -= delRows;

                     pWalk[tmpColIdx].delta += delRows;
                     maxDelta = PiNetBox_GetMaxDeltaVector(pWalk);
                  }
               }

               if ( (pWalk[colIdx].walkIdx < pCol->entries) &&
                    (pPiBlock->start_time == pElem->start_time) &&
                    (pPiBlock->netwop_no == pElem->netwop) &&
                    (pElem->upd_time == EpgDbGetPiUpdateTime(pPiBlock) ) )
               {  // block is unchanged -> skip
                  dprintf8("KEEP col %d idx %d row %d->%d(%+d) time %d-%d %s", colIdx, pWalk[colIdx].walkIdx, pElem->text_row, last_row, pCol->start_off, (int)pPiBlock->start_time, (int)pPiBlock->stop_time, ctime(&pPiBlock->start_time));

                  if (pElem->text_row < last_row)
                  {  // element is too high -> insert space
                     sint addRowCount = last_row - pElem->text_row;
                     dprintf4("INSERT SPACE col %d row %d(%+d) count %d\n", colIdx, pElem->text_row, pCol->start_off, addRowCount);
                     PiNetBox_InsertSpace(colIdx, pElem->text_row, addRowCount);
                     for (elemIdx = pWalk[colIdx].walkIdx; elemIdx < pCol->entries; elemIdx++)
                        pCol->list[elemIdx].text_row += addRowCount;
                     pCol->text_rows += addRowCount;

                     pWalk[colIdx].delta -= addRowCount;
                     maxDelta = PiNetBox_GetMaxDeltaVector(pWalk);
                  }
                  else if (pElem->text_row > last_row)
                  {  // elem could be placed higher -> check if there's space and if delta permits to move it
                     sint tmpRows;
                     // calulate how many lines of space are above which can be removed
                     if (pWalk[colIdx].walkIdx > 0)
                        tmpRows = pElem->text_row - ((pElem - 1)->text_row + (pElem - 1)->text_height);
                     else if (pElem->text_row > 0)
                        tmpRows = pElem->text_row;
                     else
                        tmpRows = 0;

                     // check how much of the space is not mandatory
                     if (tmpRows > pElem->text_row - last_row)
                        tmpRows = pElem->text_row - last_row;

                     // check how much movement the max. delta allows
                     if (tmpRows > maxDelta - pWalk[colIdx].delta)
                     {
                        dprintf3("DELTA %d reduced from %d: not enough space in col %d\n", maxDelta - pWalk[colIdx].delta, tmpRows, colIdx);
                        assert(maxDelta - pWalk[colIdx].delta >= 0);  // maxDelta is supposed to be the max of all delta vectors

                        tmpRows = maxDelta - pWalk[colIdx].delta;
                     }

                     if (tmpRows > 0)
                     {  // move element up by removing space above
                        dprintf5("REMOVE SPACE col %d row %d(%+d) count %d (curdelta %d)\n", colIdx, pElem->text_row - tmpRows, pCol->start_off, tmpRows, maxDelta);
                        PiNetBox_RemoveSpace(colIdx, pElem->text_row - tmpRows, tmpRows);
                        for (elemIdx = pWalk[colIdx].walkIdx; elemIdx < pCol->entries; elemIdx++)
                           pCol->list[elemIdx].text_row -= tmpRows;
                        pCol->text_rows -= tmpRows;

                        pWalk[colIdx].delta += tmpRows;
                        maxDelta = PiNetBox_GetMaxDeltaVector(pWalk);
                     }
                  }
                  last_row = pElem->text_row;
                  pLastElem = pElem;

                  pWalk[colIdx].walkIdx += 1;
               }
               else
               {
                  // insert element as new
                  sint extraSpace;
                  sint addRowCount;
                  dprintf7("INSERT col %d idx %d row %d(%+d) time %d-%d %s", colIdx, pWalk[colIdx].walkIdx, last_row, pCol->start_off, (int)pPiBlock->start_time, (int)pPiBlock->stop_time, ctime(&pPiBlock->start_time));

                  if (pCol->entries == netbox.max_elems)
                  {  // table is full (should never happen, since it's large enough to hold 3 times the max. elem count)
                     debug2("PiNetBox-RefreshDownwards: table overflow col %d (%d elems)", colIdx, pCol->entries);
                     break;
                  }

                  // insert the PI into the listbox array
                  pLastElem = pElem;
                  if (pWalk[colIdx].walkIdx < pCol->entries)
                  {
                     memmove( pCol->list + pWalk[colIdx].walkIdx + 1,
                              pCol->list + pWalk[colIdx].walkIdx,
                              (pCol->entries - pWalk[colIdx].walkIdx) * sizeof(*pCol->list) );
                  }
                  pCol->entries += 1;
                  pLastElem = pElem;

                  pLastElem->netwop      = pPiBlock->netwop_no;
                  pLastElem->start_time  = pPiBlock->start_time;
                  pLastElem->stop_time   = pPiBlock->stop_time;
                  pLastElem->upd_time    = EpgDbGetPiUpdateTime(pPiBlock);
                  pLastElem->text_row    = last_row;

                  // calculate the insertion point: after previous element, plus available space
                  extraSpace = 0;
                  if (pWalk[colIdx].walkIdx > 0)
                     last_row_in_col = (pElem - 1)->text_row + (pElem - 1)->text_height;
                  else
                     last_row_in_col = 0;
                  if (pWalk[colIdx].walkIdx + 1 < pCol->entries)
                  {
                     if ((pElem + 1)->text_row > last_row)
                     {
                        last_row_in_col = last_row;
                        extraSpace = (pElem + 1)->text_row - last_row;
                     }
                     else if ((pElem + 1)->text_row > last_row_in_col)
                        last_row_in_col = (pElem + 1)->text_row;
                  }

                  //dprintf5("col %d, last %d, prev height %d: ADD %d-%d\n", colIdx, last_row, last_row_in_col, (uint)pPiBlock->start_time, (uint)pPiBlock->stop_time);
                  addRowCount = 0;
                  if (last_row_in_col < last_row)
                  {
                     dprintf4("INSERT SPACE col %d row %d(%+d) count %d\n", colIdx, last_row_in_col, pCol->start_off, last_row - last_row_in_col);
                     PiNetBox_InsertSpace(colIdx, last_row_in_col, last_row - last_row_in_col);
                     addRowCount += last_row - last_row_in_col;
                  }

                  pLastElem->text_height = PiNetBox_InsertPi(pPiBlock, colIdx, last_row);

                  // compensate insertion by "eating up" space behind the PI
                  // XXX TODO only for "true" insertions, not for updates with same start time
                  addRowCount += pLastElem->text_height;
                  if (extraSpace > pLastElem->text_height)
                     extraSpace = pLastElem->text_height;
                  if (extraSpace > 0)
                  {
                     dprintf4("REMOVE SPACE col %d row %d(%+d) count %d\n", colIdx, pElem->text_row + pElem->text_height, pCol->start_off, extraSpace);
                     PiNetBox_RemoveSpace(colIdx, pElem->text_row + pElem->text_height, extraSpace);
                     addRowCount -= extraSpace;
                  }

                  for (elemIdx = pWalk[colIdx].walkIdx + 1; elemIdx < pCol->entries; elemIdx++)
                     pCol->list[elemIdx].text_row += addRowCount;
                  pCol->text_rows += addRowCount;

                  pWalk[colIdx].delta -= addRowCount;
                  maxDelta = PiNetBox_GetMaxDeltaVector(pWalk);
                  #if 0
                  if (old_stop_row == 0)
                  {  // nothing was removed above
                     if (delta != 0) dprintf1("DELTA 0 (insert in col %d)\n", colIdx);
                     delta = 0;
                  }
                  else if (old_stop_row - pLastElem->text_row + pLastElem->text_height > delta)
                  {
                     delta = old_stop_row - pLastElem->text_row + pLastElem->text_height;
                     dprintf2("DELTA %d (replacement in cur PI col %d)\n", delta, colIdx);
                  }
                  #endif

                  pWalk[colIdx].walkIdx += 1;
               }

            }
            else
            {  // listbox is full -> stop insertion / refresh
               dprintf6("STOP BEFORE col %d, last %d, prev %d, height %d, PI: %d-%d\n", colIdx, last_row, pCol->text_rows, netbox.height, (uint)pPiBlock->start_time, (uint)pPiBlock->stop_time);
               break;
            }
         }
         else
            debug4("PiNetBox-RefreshDownwards: got unmapped network %d (abs. col idx %d, visible: %d-%d)", pPiBlock->netwop_no, colIdx, netbox.net_off, netbox.col_count);

         pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
      }
      while (pPiBlock != NULL);

      // clean up
      pCol = netbox.cols;
      for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
      {
         // remove elements that have become invisible (by insertions above)
         if (pCol->entries > pWalk[colIdx].walkIdx)
         {
            sint delRows;
            pElem   = pCol->list + pWalk[colIdx].walkIdx;
            delRows = pCol->text_rows - pElem->text_row;
            dprintf9("REMOVE-BOTTOM col %d idx %d-%d row %d-%d(%+d) time %d-%d %s", colIdx, pWalk[colIdx].walkIdx, pCol->entries-1, pElem->text_row, pCol->text_rows, pCol->start_off, (int)pElem->start_time, (int)pElem->stop_time, ctime(&pElem->start_time));

            PiNetBox_RemoveSpace(colIdx, pElem->text_row, delRows);
            pCol->text_rows -= delRows;
            pCol->entries = pWalk[colIdx].walkIdx;
         }
         // fill empty space at the bottom of all columns with blank lines
         if (pCol->text_rows < netbox.height)
         {
            dprintf4("INSERT SPACE-BOTTOM col %d row %d(%+d) count %d\n", colIdx, pCol->text_rows, pCol->start_off, netbox.height - pCol->text_rows);
            PiNetBox_InsertSpace(colIdx, pCol->text_rows, netbox.height - pCol->text_rows);
            pCol->text_rows = netbox.height;
         }
         else
         {  // remove obsolete space after the last element
            sint delStart;
            pElem = pCol->list + pCol->entries - 1;
            if ( (pCol->entries > 0) &&
                 (pElem->text_row + pElem->text_height > netbox.height) )
               delStart = pElem->text_row + pElem->text_height;
            else
               delStart = netbox.height;

            if (delStart < pCol->text_rows)
            { // remove invisible text at the bottom
               sprintf(comm, ".all.pi.list.nets.n_%d delete %d.0 end\n",
                             colIdx, pCol->start_off + delStart + 2);
               eval_check(interp, comm);
               pCol->text_rows = delStart;
            }
         }
      }
   }
   xfree(pWalk);
}

// ----------------------------------------------------------------------------
// Refresh the listbox content preceding a given PI block
// - algorithms used are the same as for downwards direction
// - note that when text rows are deleted, no space is inserted at listbox top
//   until the end of the loop; the start offset may be negative during the loop
//
static void PiNetBox_RefreshUpwards( const PI_BLOCK * pPiBlock, uint refColIdx, sint scrollRows )
{
   NETBOX_COL     * pCol;
   NETBOX_COL     * pTmpCol;
   NETBOX_ELEM    * pElem;
   NETBOX_ELEM    * pLastElem;
   NETBOX_ELEM    * pTmpElem;
   REFRESH_VECT   * pWalk;
   uint  colIdx;
   uint  tmpColIdx;
   sint  elemIdx;
   sint  top_row;
   sint  max_top_row;
   sint  maxDelta;
   sint  old_top_row;
   sint  top_row_in_col;
   time_t expireTime;

   assert(EpgDbIsLocked(dbc));

   pWalk = xmalloc(sizeof(*pWalk) * netbox.col_count);
   memset(pWalk, 0, sizeof(*pWalk) * netbox.col_count);
   pLastElem = NULL;

   if ((pPiBlock != NULL) && (refColIdx < netbox.col_count))
   {
      // search the start element's entry in the listbox
      pCol      = netbox.cols + refColIdx;
      pLastElem = pCol->list;
      for (elemIdx=0; elemIdx < netbox.cols[refColIdx].entries; elemIdx++, pLastElem++)
         if ( (pLastElem->netwop == pPiBlock->netwop_no) &&
              (pLastElem->start_time == pPiBlock->start_time) )
            break;

      pWalk[refColIdx].walkIdx = elemIdx - 1;
      if (elemIdx >= netbox.cols[refColIdx].entries)
      {
         fatal3("PiNetBox-RefreshUpwards: reference PI (net %d, start %d) not found in listbox col %d", pPiBlock->netwop_no, (int)pPiBlock->start_time, refColIdx);
         pLastElem = NULL;
      }
      dprintf5("---- refresh upwards: start with col %d row %d(%+d) time %d %s", refColIdx, pLastElem->text_row, netbox.cols[refColIdx].start_off, (int)pPiBlock->start_time, ctime(&pPiBlock->start_time));

      // search the starting point in all other columns: PI preceding the reference PI
      pCol = netbox.cols;
      for (colIdx = 0; colIdx < netbox.col_count; colIdx++, pCol++)
      {
         if (colIdx != refColIdx)
         {
            pElem = pCol->list;
            for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
               if ( (pElem->start_time > pPiBlock->start_time) ||
                    ((pElem->start_time == pPiBlock->start_time) &&
                     (pElem->netwop > pPiBlock->netwop_no)) )
                  break;

            // note: index may become -1 if there's no preceding PI (or column empty)
            pWalk[colIdx].walkIdx = elemIdx - 1;
         }
      }
   }
   else
      fatal3("PiNetBox-RefreshUpwards: call with NULL ptr %ld or invalid col %d >= %d", (long)pPiBlock, refColIdx, netbox.col_count);

   if ((pLastElem != NULL) && (pPiBlock != NULL))
   {
      top_row     = pLastElem->text_row;
      expireTime  = EpgDbFilterGetExpireTime(pPiFilterContext);
      maxDelta    = 0;

      pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);

      while (pPiBlock != NULL)
      {
         // map network onto column index
         colIdx = netbox.net_ai2sel[pPiBlock->netwop_no];

         if ( (colIdx >= netbox.net_off) &&
              (colIdx < netbox.net_off + netbox.col_count) )
         {
            colIdx -= netbox.net_off;
            pCol    = netbox.cols + colIdx;

            // the element must start above the previous one in the same column
            pTmpElem = pCol->list + pWalk[colIdx].walkIdx + 1;
            if ( (pWalk[colIdx].walkIdx + 1 < pCol->entries) &&
                 (pTmpElem->text_row < top_row) )
               top_row = pTmpElem->text_row;

            // make sure NOW elements are placed higher than any non-NOW elements
            if ( (pLastElem != NULL) &&
                 (pLastElem->start_time > expireTime) && (pPiBlock->start_time <= expireTime) &&
                 (top_row > pLastElem->text_row - NETBOX_ELEM_MIN_HEIGHT))
            {
               top_row = pLastElem->text_row - NETBOX_ELEM_MIN_HEIGHT;
            }

            // make sure the element is placed higher than any other which starts later than it ends
            // - note: default is unlimited, i.e. elements may be partially invisible
            max_top_row = netbox.height * 2;
            for (tmpColIdx=0; tmpColIdx < netbox.col_count; tmpColIdx++)
            {
               if (pWalk[tmpColIdx].walkIdx + 1 < netbox.cols[tmpColIdx].entries)
               {
                  pTmpElem = netbox.cols[tmpColIdx].list + pWalk[tmpColIdx].walkIdx + 1;
                  if ( (pTmpElem->start_time >= pPiBlock->stop_time) &&
                       (max_top_row > pTmpElem->text_row) )
                  {
                     max_top_row = pTmpElem->text_row;
                  }
               }
            }

            if ( ( (top_row > 0 - (scrollRows + NETBOX_ELEM_MIN_HEIGHT)) &&
                   (max_top_row >= 0 - scrollRows) ) ||
                 (pPiBlock->start_time <= expireTime) )
            {
               if (netbox.pi_off > 0)
                  netbox.pi_off -= 1;

               // check for obsolete PI in all columns
               old_top_row = 0;
               pTmpCol = netbox.cols;
               for (tmpColIdx=0; tmpColIdx < netbox.col_count; tmpColIdx++, pTmpCol++)
               {
                  pTmpElem = pTmpCol->list + pWalk[tmpColIdx].walkIdx;
                  // - note: if there's an element with smaller start time in the listbox, it must have
                  //         been deleted from the db -> remove it; same if starts later but overlaps
                  for (elemIdx = pWalk[tmpColIdx].walkIdx; elemIdx >= 0; elemIdx--, pTmpElem--)
                     if ( (pTmpElem->start_time < pPiBlock->start_time) ||
                          ( (pTmpElem->start_time == pPiBlock->start_time) &&
                            ( (pTmpElem->netwop < pPiBlock->netwop_no) ||
                              ( (pTmpElem->netwop == pPiBlock->netwop_no) &&
                                (pTmpElem->upd_time == EpgDbGetPiUpdateTime(pPiBlock)) ))))
                        break;

                  // note: elemIdx ist the index of the first not to-be-deleted element
                  if (elemIdx < pWalk[tmpColIdx].walkIdx)
                  {  // remove the obsolete elements
                     sint delRowCount;
                     assert((pWalk[tmpColIdx].walkIdx >= 0) && (pWalk[tmpColIdx].walkIdx < pTmpCol->entries));
                     assert((elemIdx >= -1) && (elemIdx < pTmpCol->entries));

                     pTmpElem = pTmpCol->list + pWalk[tmpColIdx].walkIdx;
                     delRowCount = pTmpElem->text_row + pTmpElem->text_height - pTmpCol->list[elemIdx + 1].text_row;
                     dprintf9("REMOVE col %d idx %d-%d row %d-%d(%+d) time %d-%d %s", tmpColIdx, elemIdx+1, pWalk[tmpColIdx].walkIdx, pTmpElem->text_row, pTmpElem->text_row + delRowCount, pTmpCol->start_off, (int)pTmpCol->list[elemIdx+1].start_time, (int)pTmpElem->stop_time, ctime(&pTmpElem->start_time));

                     PiNetBox_RemoveSpace(tmpColIdx, pTmpCol->list[elemIdx + 1].text_row, delRowCount);

                     if (pTmpElem->text_row + pTmpElem->text_height > netbox.height)
                     {  // last element was partially invisible
                        delRowCount -= (pTmpElem->text_row + pTmpElem->text_height) - netbox.height;
                        pTmpCol->text_rows = netbox.height;
                     }

                     if (pWalk[tmpColIdx].walkIdx + 1 < pTmpCol->entries)
                     {  // there are elements behind the removed one -> move them forward; update row index
                        memmove(pTmpCol->list + elemIdx + 1, pTmpElem + 1, (pTmpCol->entries - (pWalk[tmpColIdx].walkIdx + 1)) * sizeof(*pTmpElem));
                     }
                     pTmpCol->entries -= pWalk[tmpColIdx].walkIdx - elemIdx;
                     pWalk[tmpColIdx].walkIdx = elemIdx;

                     for (elemIdx = 0; elemIdx <= pWalk[tmpColIdx].walkIdx; elemIdx++)
                        pTmpCol->list[elemIdx].text_row += delRowCount;
                     pTmpCol->start_off -= delRowCount;

                     pWalk[tmpColIdx].delta += delRowCount;
                     maxDelta = PiNetBox_GetMaxDeltaVector(pWalk);
                  }
               }

               pElem = pCol->list + pWalk[colIdx].walkIdx;

               if ( (pWalk[colIdx].walkIdx >= 0) &&
                    (pPiBlock->start_time == pElem->start_time) &&
                    (pPiBlock->netwop_no == pElem->netwop) &&
                    (pElem->upd_time == EpgDbGetPiUpdateTime(pPiBlock) ) )
               {  // block is unchanged -> skip
                  dprintf8("KEEP col %d idx %d row %d->%d(%+d) time %d-%d %s", colIdx, pWalk[colIdx].walkIdx, pElem->text_row, top_row, pCol->start_off, (int)pPiBlock->start_time, (int)pPiBlock->stop_time, ctime(&pPiBlock->start_time));

                  if (top_row + pElem->text_height > max_top_row)
                  {
                     top_row = max_top_row - pElem->text_height;
                  }

                  if (pElem->text_row > top_row)
                  {  // element is too low -> insert space below
                     sint addRowCount = pElem->text_row - top_row;
                     dprintf4("INSERT SPACE col %d row %d(%+d) count %d\n", colIdx, pElem->text_row + pElem->text_height, pCol->start_off, addRowCount);
                     PiNetBox_InsertSpace(colIdx, pElem->text_row + pElem->text_height, addRowCount);

                     for (elemIdx = 0; elemIdx <= pWalk[colIdx].walkIdx; elemIdx++)
                        pCol->list[elemIdx].text_row -= addRowCount;
                     pCol->start_off += addRowCount;

                     pWalk[colIdx].delta -= addRowCount;
                     maxDelta = PiNetBox_GetMaxDeltaVector(pWalk);
                  }
                  else if (top_row > pElem->text_row)
                  {  // elem could be placed lower -> check if there's space and if delta permits to move it
                     sint delRowCount;
                     // calulate how many lines of space is below which can be removed
                     if (pWalk[colIdx].walkIdx + 1 < pCol->entries)
                        delRowCount = (pElem + 1)->text_row - (pElem->text_row + pElem->text_height);
                     else if (netbox.height > (pElem->text_row + pElem->text_height /*- NETBOX_ELEM_GAP*/))  // XXX TODO special case galore (pCol->text_row++,delRow++)
                        delRowCount = netbox.height - (pElem->text_row + pElem->text_height /*- NETBOX_ELEM_GAP*/);
                     else
                        delRowCount = 0;

                     // check how much of the space is not mandatory
                     if (delRowCount > top_row - pElem->text_row)
                        delRowCount = top_row - pElem->text_row;

                     // update maximum by which elements can be shifted up
                     if (delRowCount > maxDelta - pWalk[colIdx].delta)
                     {
                        dprintf3("DELTA %d (reduced from %d: not enough space in col %d)\n", maxDelta - pWalk[colIdx].delta, delRowCount, colIdx);
                        assert(maxDelta - pWalk[colIdx].delta >= 0);  // maxDelta is supposed to be the max of all delta vectors

                        delRowCount = maxDelta - pWalk[colIdx].delta;
                     }

                     if (delRowCount > 0)
                     {  // move element down by removing space below
                        dprintf4("REMOVE SPACE col %d row %d(%+d) count %d\n", colIdx, pElem->text_row + pElem->text_height, pCol->start_off, delRowCount);
                        PiNetBox_RemoveSpace(colIdx, pElem->text_row + pElem->text_height, delRowCount);

                        for (elemIdx = 0; elemIdx <= pWalk[colIdx].walkIdx; elemIdx++)
                           pCol->list[elemIdx].text_row += delRowCount;
                        pCol->start_off -= delRowCount;

                        pWalk[colIdx].delta += delRowCount;
                        maxDelta = PiNetBox_GetMaxDeltaVector(pWalk);
                     }
                  }
                  top_row = pElem->text_row;
                  pLastElem = pElem;

                  pWalk[colIdx].walkIdx -= 1;
               }
               else
               {
                  // insert element as new

                  sint addSpaceCount;
                  sint addRowCount;
                  sint delSpaceCount;

                  if (pCol->entries == netbox.max_elems)
                  {  // table is full (should never happen, since it's large enough to hold 3 times the max. elem count)
                     debug2("PiNetBox-RefreshUpwards: table overflow col %d (%d elems)", colIdx, pCol->entries);
                     break;
                  }

                  // insert the PI into the listbox array after the "current" element
                  if (pWalk[colIdx].walkIdx + 1 < pCol->entries)
                  {
                     memmove( pCol->list + pWalk[colIdx].walkIdx + 1 + 1,
                              pCol->list + pWalk[colIdx].walkIdx + 1,
                              (pCol->entries - (pWalk[colIdx].walkIdx + 1)) * sizeof(*pCol->list) );
                  }
                  pElem     = pCol->list + pWalk[colIdx].walkIdx;
                  pLastElem = pCol->list + pWalk[colIdx].walkIdx + 1;
                  pCol->entries += 1;

                  pLastElem->netwop      = pPiBlock->netwop_no;
                  pLastElem->start_time  = pPiBlock->start_time;
                  pLastElem->stop_time   = pPiBlock->stop_time;
                  pLastElem->upd_time    = EpgDbGetPiUpdateTime(pPiBlock);

                  // calculate the insertion point: between preceding and following elements
                  if (pWalk[colIdx].walkIdx + 1 + 1 < pCol->entries)
                  {  // there's an element below -> insert directly above (don't know yet how many rows it will need)
                     top_row_in_col = (pElem + 1 + 1)->text_row;
                  }
                  else
                  {  // no element below: check above
                     if ( (pWalk[colIdx].walkIdx >= 0) &&
                          (pElem->text_row + pElem->text_height > top_row) )
                     {  // element above already too low -> insert below (insert space later)
                        top_row_in_col = pElem->text_row + pElem->text_height;
                     }
                     else  // target row is free -> insert at bottom
                        top_row_in_col = netbox.height - 1;
                  }
                  dprintf8("INSERT col %d idx %d row %d->%d(%+d) time %d-%d %s", colIdx, pWalk[colIdx].walkIdx + 1, top_row_in_col, top_row, pCol->start_off, (int)pPiBlock->start_time, (int)pPiBlock->stop_time, ctime(&pPiBlock->start_time));

                  pLastElem->text_height = PiNetBox_InsertPi(pPiBlock, colIdx, top_row_in_col);
                  pLastElem->text_row    = top_row_in_col;
                  addRowCount            = pLastElem->text_height;

                  if (top_row + pLastElem->text_height > max_top_row)
                  {
                     top_row = max_top_row - pLastElem->text_height;
                  }

                  if (pLastElem->text_row - addRowCount > top_row)
                  {  // element is too low -> insert empty lines below
                     addSpaceCount = (pLastElem->text_row - addRowCount) - top_row;
                     dprintf4("INSERT SPACE col %d row %d(%+d) count %d\n", colIdx, pLastElem->text_row + pLastElem->text_height, pCol->start_off, addSpaceCount);

                     PiNetBox_InsertSpace(colIdx, pLastElem->text_row + pLastElem->text_height, addSpaceCount);
                     addRowCount += addSpaceCount;
                  }
                  // note: other element's delta may be lower dues to following space removal
                  pLastElem->text_row -= addRowCount;
                  if ( (pLastElem->text_row + pLastElem->text_height > netbox.height) &&
                       (pWalk[colIdx].walkIdx + 1 + 1 < pCol->entries) )
                  {
                     pCol->text_rows = pLastElem->text_row + pLastElem->text_height;
                  }

                  // remove space above the element (XXX unless it's a replacement)
                  delSpaceCount = 0;
                  if ( (pWalk[colIdx].walkIdx >= 0) &&
                       (pElem->text_row + pElem->text_height < top_row_in_col) )
                  {
                     delSpaceCount = top_row_in_col - (pElem->text_row + pElem->text_height);
                     if (delSpaceCount > addRowCount)
                        delSpaceCount = addRowCount;
                     dprintf4("REMOVE SPACE col %d ABOVE row %d(%+d) count %d\n", colIdx, pLastElem->text_row, pCol->start_off, delSpaceCount);

                     PiNetBox_RemoveSpace(colIdx, pElem->text_row + pElem->text_height, delSpaceCount);

                     addRowCount -= delSpaceCount;
                  }

                  for (elemIdx = 0; elemIdx <= pWalk[colIdx].walkIdx; elemIdx++)
                     pCol->list[elemIdx].text_row -= addRowCount;
                  pCol->start_off += addRowCount;

                  pWalk[colIdx].delta -= addRowCount;
                  maxDelta = PiNetBox_GetMaxDeltaVector(pWalk);

                  top_row = pLastElem->text_row;
               }
            }
            else
            {  // exit loop -> all elements above are removed
               // XXX TODO: do not remove visible elements! (> MIN_HEIGHT)
               dprintf6("STOP BEFORE col %d, top %d, prev %d, height %d, PI: %d-%d\n", colIdx, top_row, pCol->text_rows, netbox.height, (uint)pPiBlock->start_time, (uint)pPiBlock->stop_time);
               break;
            }
         }
         else
            debug4("PiNetBox-RefreshUpwards: got unmapped network %d (abs. col idx %d, visible: %d-%d)", pPiBlock->netwop_no, colIdx, netbox.net_off, netbox.col_count);

         pPiBlock = EpgDbSearchPrevPi(dbc, pPiFilterContext, pPiBlock);
      }

      // clean up
      {
         sint  delRowCount;

         pCol = netbox.cols;
         for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
         {
            // remove elements that have become invisible (by insertions below) or were removed in the db
            if (pWalk[colIdx].walkIdx >= 0)
            {
               if (pWalk[colIdx].walkIdx + 1 < pCol->entries)
               {  // there are valid elements below
                  elemIdx = pWalk[colIdx].walkIdx;
                  pElem   = pCol->list + elemIdx;

                  // start deleting at start_off, so that obsolete space at top is included
                  if ( (pElem + 1)->text_row < 0 )
                     delRowCount = (pElem + 1)->text_row - (0 - pCol->start_off);
                  else if (pElem->text_row + pElem->text_height <= 0)
                     delRowCount = pCol->start_off;
                  else  // deleting above zero -> space will have to be inserted later
                     delRowCount = pElem->text_row + pElem->text_height - (0 - pCol->start_off);
                  dprintf8("REMOVE-TOP col %d idx 0-%d row %d-%d(%+d) time %d-%d %s", colIdx, elemIdx, (0 - pCol->start_off), (0 - pCol->start_off) + delRowCount, pCol->start_off, (int)pElem->start_time, (int)pElem->stop_time, ctime(&pElem->start_time));

                  PiNetBox_RemoveSpace(colIdx, 0 - pCol->start_off, delRowCount);
                  pCol->start_off -= delRowCount;

                  memmove(pCol->list, pCol->list + elemIdx + 1, (pCol->entries - (elemIdx + 1)) * sizeof(*pCol->list));
                  pCol->entries -= elemIdx + 1;
               }
               else
               {  // this column has become empty
                  pCol->entries   = 0;
               }
            }

            if (pCol->entries > 0)
            {  // column has not become empty

               if (pCol->start_off < 0)
               {  // fill vitual lines ("vacuum") at the top with blank text rows
                  PiNetBox_InsertSpace(colIdx, 0 - pCol->start_off, 0 - pCol->start_off);
                  pCol->start_off = 0;
               }
               else if (pCol->start_off > 0)
               {  // there are text rows in the invisible space above the listbox top border
                  if (pCol->list[0].text_row >= 0)
                  {  // only space above -> remove all of it
                     PiNetBox_RemoveSpace(colIdx, 0 - pCol->start_off, pCol->start_off);
                     pCol->start_off = 0;
                  }
                  else if ( /* (pCol->list[0].text_row < 0) &&  // implied by else */
                            (pCol->start_off > 0 - pCol->list[0].text_row) )
                  {  // partial inivible element & more rows above -> remove the latter ones
                     PiNetBox_RemoveSpace(colIdx, 0 - pCol->start_off, pCol->start_off - (0 - pCol->list[0].text_row));
                     pCol->start_off = 0 - pCol->list[0].text_row;
                  }
               }
            }
            else
            {
               sprintf(comm, ".all.pi.list.nets.n_%d delete 1.0 end\n", colIdx);
               eval_check(interp, comm);

               pCol->text_rows = netbox.height;
               pCol->start_off = 0;

               PiNetBox_InsertSpace(colIdx, 0, netbox.height);
            }

            assert(pCol->start_off == (((pCol->entries > 0) && (pCol->list[0].text_row < 0)) ? (0 - pCol->list[0].text_row) : 0));
         }
      }

      // if the top was reached, start re-aligning top-down
      // (e.g. make sure NOW items have no space above)
      if (pPiBlock == NULL)
      {
         top_row = PiNetBox_AlignNowItems(top_row, TRUE);
      }

      if ((top_row < 0) && (scrollRows > 0))
      {
         // count again space at bottom (may have increased due to removal of over-long items)
         scrollRows = PiNetBox_CountEmptyRowsAtBottom();
         if (scrollRows > 0 - top_row)
            scrollRows = 0 - top_row;

         if (scrollRows > 0)
            PiNetBox_MoveTextDown(scrollRows);
      }
   }
   xfree(pWalk);
}

// ----------------------------------------------------------------------------
// Re-Sync the listbox with the database
//
void PiNetBox_Refresh( void )
{
   const PI_BLOCK * pPiBlock;
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uint  colIdx;
   uint  elemIdx;
   sint  textRow;
   sint  emptyRows;

   if ( (netbox.net_off == 0) && (netbox.pi_count == 0) )
   {  // listbox empty -> reset completely
      PiNetBox_Reset();
   }
   else
   {
      EpgDbLockDatabase(dbc, TRUE);

      if ( PiNetBox_CheckNetwopFilterChange() )
      {
         PiNetBox_WithdrawCursor();
         PiNetBox_UpdateNetwopMap();
         PiNetBox_UpdateNetwopNames();
         PiNetBox_AdjustHorizontalScrollBar();

         // XXX TODO: search previously selected network in new mapping
         if (netbox.net_off + netbox.cur_col >= netbox.net_count)
         {
            if (netbox.net_count > 0)
               netbox.cur_col = netbox.net_count - 1;
            else
               netbox.cur_col = 0;
         }

         // XXX FIXME: discard content because netwops IDs in columns are already overwritten
         PiNetBox_ClearRows();
      }

      pPiBlock = NULL;

      // first try to find the currently selected element
      if (netbox.cur_col < netbox.col_count)
      {
         pCol = netbox.cols + netbox.cur_col;
         if (netbox.cur_idx < pCol->entries)
         {
            pElem = pCol->list + netbox.cur_idx;

            pPiBlock = EpgDbSearchPi(dbc, pElem->start_time, pElem->netwop);
            colIdx   = netbox.cur_col;
            textRow  = pElem->text_row;
         }
      }

      // if none found, search another element with the last requested start time & position
      if ((pPiBlock == NULL) || (EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) == FALSE))
      {
         pPiBlock = PiNetBox_PickVisiblePi(0, netbox.col_count, &colIdx, &textRow, &elemIdx);
      }

      // if none found, search any PI with the last requested start time
      if ((pPiBlock == NULL) || (EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) == FALSE))
      {
         pPiBlock = PiNetBox_PickNewPi(&colIdx, &textRow, &elemIdx);
      }

      if ((pPiBlock != NULL) && (elemIdx != INVALID_CUR_IDX))
      {
         // found one which is currently visible -> use that as anchor for a refresh

         netbox.pi_resync = FALSE;
         netbox.pi_off    = EpgDbCountPrevPi(dbc, pPiFilterContext, pPiBlock);
         netbox.pi_count  = netbox.pi_off + EpgDbCountPi(dbc, pPiFilterContext, pPiBlock);

         PiNetBox_WithdrawCursor();

         PiNetBox_RefreshDownwards(pPiBlock, colIdx);

         // check if bottom was hit -> scroll upwards
         emptyRows = PiNetBox_CountEmptyRowsAtBottom();

         PiNetBox_RefreshUpwards(pPiBlock, colIdx, emptyRows);

         if (netbox.pi_off == 0)
         {
            emptyRows = PiNetBox_CountEmptyRowsAtTop();
            if (emptyRows > 0)
            {  // upwards refresh scrolled upwards because not enough PI in database
               // -> fill empty space at bottom, if PI available
               PiNetBox_FillDownwards(emptyRows, FALSE);

               // count again to see if text could be scrolled enough
               emptyRows = PiNetBox_CountEmptyRowsAtTop();
               if (emptyRows > 0)
               {
                  PiNetBox_MoveTextUp(emptyRows);
                  PiNetBox_AppendSpace();
               }
            }
         }
         PiNetBox_SetVisible();
      }
      else
      {
         // rebuild the listbox content around the selected PI

         PiNetBox_FillAroundPi(pPiBlock, colIdx, textRow);
      }

      netbox.cur_idx  = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

      PiNetBox_ShowCursor();
      PiNetBox_AdjustVerticalScrollBar();
      PiNetBox_UpdateInfoText(FALSE);

      assert(PiNetBox_ConsistancyCheck());

      EpgDbLockDatabase(dbc, FALSE);
   }
}

// ----------------------------------------------------------------------------
// Move the cursor one line up, scroll if necessary
//
static int PiNetBox_CursorDown( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   uint  new_idx;

   if (netbox.pi_resync)
      PiNetBox_RefreshEvent((ClientData)TRUE);

   if (objc == 1)
   {
      if (netbox.cur_col < netbox.col_count)
      {
         if ((netbox.cur_idx == INVALID_CUR_IDX) && PiNetBox_IsPartialElemAtBottom(&new_idx))
         {
            PiNetBox_WithdrawCursor();

            netbox.cur_idx = new_idx;

            PiNetBox_ScrollIfPartial();
         }
         else if (netbox.cur_idx + 1 >= netbox.cols[netbox.cur_col].entries)
         {  // last item is selected -> scroll down

            PiNetBox_FillDownwards(NETBOX_ELEM_MIN_HEIGHT, TRUE);

            netbox.cur_idx = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

            PiNetBox_AdjustVerticalScrollBar();
         }
         else
         {  // move cursor one item down
            PiNetBox_WithdrawCursor();

            netbox.cur_idx += 1;

            PiNetBox_ScrollIfPartial();
         }
         PiNetBox_UpdateCursorReq(CUR_REQ_DOWN);
         PiNetBox_ShowCursor();
         PiNetBox_UpdateInfoText(FALSE);

         assert(PiNetBox_ConsistancyCheck());
      }
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Move the cursor one line down, scroll if necessary
//
static int PiNetBox_CursorUp( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   uint  new_idx;

   if (netbox.pi_resync)
      PiNetBox_RefreshEvent((ClientData)TRUE);

   if (objc == 1)
   {
      if (netbox.cur_col < netbox.col_count)
      {
         if ((netbox.cur_idx == INVALID_CUR_IDX) && PiNetBox_IsPartialElemAtTop(&new_idx))
         {
            PiNetBox_WithdrawCursor();
            netbox.cur_idx = new_idx;
            PiNetBox_ScrollIfPartial();
         }
         else if ((netbox.cur_idx == 0) || (netbox.cur_idx == INVALID_CUR_IDX))
         {  // first item is selected -> scroll up

            PiNetBox_FillUpwards(NETBOX_ELEM_MIN_HEIGHT, TRUE);

            netbox.cur_idx = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

            PiNetBox_AdjustVerticalScrollBar();
         }
         else
         {  // move cursor one position up (text widget line count starts at 1)
            PiNetBox_WithdrawCursor();

            netbox.cur_idx -= 1;

            PiNetBox_ScrollIfPartial();
         }
         PiNetBox_UpdateCursorReq(CUR_REQ_UP);
         PiNetBox_ShowCursor();
         PiNetBox_UpdateInfoText(FALSE);

         assert(PiNetBox_ConsistancyCheck());
      }
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Scroll one page down
//
static int PiNetBox_ScrollPageDown( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uint  colIdx;
   uint  delta;

   if (netbox.cur_col < netbox.col_count)
   {
      pCol  = netbox.cols;
      delta = netbox.height;

      // search for the first partially displayed item
      for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
      {
         if (pCol->entries > 0)
         {
            pElem = pCol->list + pCol->entries - 1;
            // scroll less than a page if there were partially displayed elements at the bottom
            if ( (pElem->text_row + pElem->text_height - NETBOX_ELEM_GAP > netbox.height) &&
                 (delta > pElem->text_row) &&
                 (pElem->text_row > 0) )
            {
               delta = pElem->text_row;
            }
         }
      }

      PiNetBox_FillDownwards(delta, FALSE);

      netbox.cur_idx = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

      PiNetBox_UpdateCursorReq(CUR_REQ_DOWN);
      PiNetBox_ShowCursor();
      PiNetBox_UpdateInfoText(FALSE);
      PiNetBox_AdjustVerticalScrollBar();

      assert(PiNetBox_ConsistancyCheck());
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Scroll one page up
//
static int PiNetBox_ScrollPageUp( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uint  colIdx;
   uint  delta;

   if (netbox.cur_col < netbox.col_count)
   {
      pCol  = netbox.cols;
      delta = netbox.height;

      // search for the first partially displayed item
      for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
      {
         if (pCol->entries > 0)
         {
            pElem = pCol->list;
            // scroll less than a page if there were partially displayed elements at the top
            if ( (pElem->text_row < 0) &&
                 (delta > netbox.height + pElem->text_row) &&
                 // make sure that delta doesn't become zero
                 (0 - pElem->text_row < netbox.height) )
            {
               delta = netbox.height + pElem->text_row;
            }
         }
      }

      PiNetBox_FillUpwards(delta, FALSE);

      netbox.cur_idx = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

      PiNetBox_UpdateCursorReq(CUR_REQ_UP);
      PiNetBox_ShowCursor();
      PiNetBox_AdjustVerticalScrollBar();
      PiNetBox_UpdateInfoText(FALSE);

      assert(PiNetBox_ConsistancyCheck());
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Move the visible window to an absolute position
//
static void PiNetBox_ScrollMoveto( uint newPiOff )
{
   const PI_BLOCK  * pPiBlock;
   const PI_BLOCK  * pPrevPi;
   NETBOX_COL      * pCol;
   bool  isPartial;
   uint  pi_off;
   uint  colIdx;
   sint  oldReqRow;

   isPartial = FALSE;
   if ((newPiOff == 0) && (netbox.pi_off == 0))
   {  // user wants to scroll to the very top & first PI already in listbox
      // -> check if the topmost PI are all fully visible
      pCol = netbox.cols;
      for (colIdx = 0; colIdx < netbox.col_count; colIdx++, pCol++)
         if ( (pCol->entries > 0) && (pCol->list[0].text_row < 0) )
            isPartial = TRUE;
   }

   if ((netbox.pi_off != newPiOff) || isPartial)
   {
      oldReqRow = netbox.cur_req_row;
      pi_off    = 0;
      pPrevPi   = NULL;

      EpgDbLockDatabase(dbc, TRUE);

      // get first PI to be displayed: by skipping the given number of PI
      pPiBlock = EpgDbSearchFirstPi(dbc, pPiFilterContext);
      while ((pPiBlock != NULL) && (pi_off < newPiOff))
      {
         pi_off  += 1;
         pPrevPi  = pPiBlock;

         pPiBlock = EpgDbSearchNextPi(dbc, pPiFilterContext, pPiBlock);
      }

      if ((pPiBlock == NULL) && (pPrevPi != NULL))
      {
         pPiBlock = pPrevPi;
      }

      colIdx = netbox.net_ai2sel[pPiBlock->netwop_no];
      if ((colIdx >= netbox.net_off) && (colIdx < netbox.net_off + netbox.col_count))
         colIdx -= netbox.net_off;
      else
         pPiBlock = NULL;

      PiNetBox_FillAroundPi(pPiBlock, colIdx, 0);

      netbox.cur_idx = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

      PiNetBox_UpdateCursorReq(CUR_REQ_GOTO);
      PiNetBox_ShowCursor();
      PiNetBox_AdjustVerticalScrollBar();
      PiNetBox_UpdateInfoText(FALSE);

      assert(PiNetBox_ConsistancyCheck());

      EpgDbLockDatabase(dbc, FALSE);
   }
}

// ----------------------------------------------------------------------------
// Move the visible window - interface to the vertical scrollbar
//
static int PiNetBox_ScrollVertical( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiBox_Scroll {moveto <fract>|scroll <delta> {pages|unit}}";
   uchar * pOption;
   double  fract;
   int     delta;
   int     result;

   if (netbox.pi_resync)
      PiNetBox_RefreshEvent((ClientData)TRUE);

   pOption = Tcl_GetString(objv[1]);
   if ((objc == 2+1) && (strcmp(pOption, "moveto") == 0))
   {
      result = Tcl_GetDoubleFromObj(interp, objv[2], &fract);
      if (result == TCL_OK)
      {
         delta = (sint)(0.5 + fract * netbox.pi_count);
         if (delta < 0)
            delta = 0;
         PiNetBox_ScrollMoveto(delta);
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
               PiNetBox_ScrollPageDown();
            else if (delta < 0)
               PiNetBox_ScrollPageUp();
         }
         else if (strncmp(pOption, "unit", 4) == 0)
         {
            if (delta > 0)
               PiNetBox_FillDownwards(delta, FALSE);
            else if (delta < 0)
               PiNetBox_FillUpwards(-delta, FALSE);

            if (delta != 0)
            {
               netbox.cur_idx = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

               PiNetBox_UpdateCursorReq((delta > 0) ? CUR_REQ_DOWN : CUR_REQ_UP);
               PiNetBox_ShowCursor();
               PiNetBox_AdjustVerticalScrollBar();
               PiNetBox_UpdateInfoText(FALSE);

               assert(PiNetBox_ConsistancyCheck());
            }
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
// Move the cursor one column to the left, scroll if neccessary
//
static int PiNetBox_CursorLeft( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const PI_BLOCK * pPiBlock;
   uint  colIdx;
   uint  elemIdx;
   sint  textRow;

   if (netbox.pi_resync)
      PiNetBox_RefreshEvent((ClientData)TRUE);

   if (netbox.cur_col == 0)
   {
      if (netbox.net_off > 0)
      {
         EpgDbLockDatabase(dbc, TRUE);

         pPiBlock = PiNetBox_PickVisiblePi(0, netbox.col_count - 1, &colIdx, &textRow, &elemIdx);

         netbox.net_off -= 1;

         PiNetBox_UpdateNetwopNames();
         PiNetBox_AdjustHorizontalScrollBar();

         if (pPiBlock == NULL)
            pPiBlock = PiNetBox_PickNewPi(&colIdx, &textRow, &elemIdx);
         else
            colIdx += 1;

         PiNetBox_FillAroundPi(pPiBlock, colIdx, textRow);

         netbox.cur_idx  = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

         PiNetBox_ShowCursor();
         PiNetBox_AdjustVerticalScrollBar();
         PiNetBox_UpdateInfoText(FALSE);

         assert(PiNetBox_ConsistancyCheck());

         EpgDbLockDatabase(dbc, FALSE);
      }
   }
   else
   {
      PiNetBox_WithdrawCursor();

      netbox.cur_col -= 1;
      netbox.cur_idx  = PiNetBox_PickCursorRow(netbox.cur_col, ABOVE_PARTIAL);
      PiNetBox_ScrollIfPartial();

      PiNetBox_ShowCursor();
      PiNetBox_UpdateInfoText(FALSE);

      assert(PiNetBox_ConsistancyCheck());
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Move the cursor one column to the right, scroll if neccessary
//
static int PiNetBox_CursorRight( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const PI_BLOCK * pPiBlock;
   uint  colIdx;
   uint  elemIdx;
   sint  textRow;

   if (netbox.pi_resync)
      PiNetBox_RefreshEvent((ClientData)TRUE);

   if (netbox.cur_col + 1 >= netbox.col_count)
   {
      if (netbox.net_off + netbox.col_count < netbox.net_count)
      {
         EpgDbLockDatabase(dbc, TRUE);

         pPiBlock = PiNetBox_PickVisiblePi(1, netbox.col_count - 1, &colIdx, &textRow, &elemIdx);

         netbox.net_off += 1;

         PiNetBox_UpdateNetwopNames();
         PiNetBox_AdjustHorizontalScrollBar();

         if (pPiBlock == NULL)
            pPiBlock = PiNetBox_PickNewPi(&colIdx, &textRow, &elemIdx);
         else
            colIdx -= 1;

         PiNetBox_FillAroundPi(pPiBlock, colIdx, textRow);

         netbox.cur_idx  = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

         PiNetBox_ShowCursor();
         PiNetBox_AdjustVerticalScrollBar();
         PiNetBox_UpdateInfoText(FALSE);

         assert(PiNetBox_ConsistancyCheck());

         EpgDbLockDatabase(dbc, FALSE);
      }
   }
   else
   {
      // there might be less networks than visible columns
      if (netbox.net_off + netbox.cur_col + 1 < netbox.net_count)
      {
         PiNetBox_WithdrawCursor();

         netbox.cur_col += 1;
         netbox.cur_idx  = PiNetBox_PickCursorRow(netbox.cur_col, ABOVE_PARTIAL);
         PiNetBox_ScrollIfPartial();

         PiNetBox_ShowCursor();
         PiNetBox_UpdateInfoText(FALSE);

         assert(PiNetBox_ConsistancyCheck());
      }
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Scroll the listbox view one column to the right, i.e. move content left
// - selection of selected PI / visible time range:
//   + the cursor remains in the same column, on the same element if the cursor
//     column remains visible (i.e. if scrolling delta is less than the number
//     of visible columns); hence the listbox is refilled around the selected PI
//   + if the scrolling delta is too large or the currently selected column empty
//     all currently visible columns are searched for an exact match on the
//     last requested start time (e.g. direct selection of PI by mouse click)
//   + if none is found that way either, any PI around the last requested
//     start time is searched for in the new network column selection
// - if the last column is already visible, only the cursor is moved
//
static int PiNetBox_ScrollRight( sint delta )
{
   const PI_BLOCK * pPiBlock;
   uint  colIdx;
   uint  elemIdx;
   sint  textRow;

   if (netbox.net_off + netbox.col_count < netbox.net_count)
   {
      EpgDbLockDatabase(dbc, TRUE);

      if (delta > netbox.net_count - (netbox.net_off + netbox.col_count))
         delta = netbox.net_count - (netbox.net_off + netbox.col_count);
      else if (delta < 0)
         delta = 0;

      pPiBlock = PiNetBox_PickCurPi(delta, netbox.col_count - delta, &colIdx, &textRow, &elemIdx);

      if (pPiBlock == NULL)
         pPiBlock = PiNetBox_PickVisiblePi(delta, netbox.col_count - delta, &colIdx, &textRow, &elemIdx);

      netbox.net_off += delta;
      colIdx         -= delta;

      if (netbox.cur_col >= delta)
         netbox.cur_col -= delta;
      else
         netbox.cur_col = 0;

      PiNetBox_UpdateNetwopNames();
      PiNetBox_AdjustHorizontalScrollBar();

      if (pPiBlock == NULL)
         pPiBlock = PiNetBox_PickNewPi(&colIdx, &textRow, &elemIdx);

      PiNetBox_FillAroundPi(pPiBlock, colIdx, textRow);

      netbox.cur_idx  = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

      PiNetBox_ShowCursor();
      PiNetBox_AdjustVerticalScrollBar();
      PiNetBox_UpdateInfoText(FALSE);

      assert(PiNetBox_ConsistancyCheck());

      EpgDbLockDatabase(dbc, FALSE);
   }
   else
   {  // the last column is already visible -> move cursor left if not already last column
      if ( (netbox.cur_col + 1 < netbox.col_count) &&
           (netbox.net_off + netbox.cur_col + 1 < netbox.net_count) )
      {
         PiNetBox_CursorRight(NULL, interp, 0, NULL);
      }
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Scroll the listbox view one column to the left, i.e. move content right
// - for comments see the above function
//
static int PiNetBox_ScrollLeft( sint delta )
{
   const PI_BLOCK * pPiBlock;
   uint  colIdx;
   uint  elemIdx;
   sint  textRow;

   if (netbox.net_off > 0)
   {
      EpgDbLockDatabase(dbc, TRUE);

      if (delta > netbox.net_off)
         delta = netbox.net_off;
      else if (delta < 0)
         delta = 0;

      pPiBlock = PiNetBox_PickCurPi(0, netbox.col_count - delta, &colIdx, &textRow, &elemIdx);

      if (pPiBlock == NULL)
         pPiBlock = PiNetBox_PickVisiblePi(0, netbox.col_count - delta, &colIdx, &textRow, &elemIdx);

      netbox.net_off -= delta;
      colIdx         += delta;

      if (delta < netbox.col_count)
      {
         if (netbox.cur_col < netbox.col_count - delta)
            netbox.cur_col += delta;
         else
            netbox.cur_col = netbox.col_count - 1;
      }

      PiNetBox_UpdateNetwopNames();
      PiNetBox_AdjustHorizontalScrollBar();

      if (pPiBlock == NULL)
         pPiBlock = PiNetBox_PickNewPi(&colIdx, &textRow, &elemIdx);

      PiNetBox_FillAroundPi(pPiBlock, colIdx, textRow);

      netbox.cur_idx  = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

      PiNetBox_ShowCursor();
      PiNetBox_AdjustVerticalScrollBar();
      PiNetBox_UpdateInfoText(FALSE);

      assert(PiNetBox_ConsistancyCheck());

      EpgDbLockDatabase(dbc, FALSE);
   }
   else
   {  // the last column is already visible -> move cursor left if not already last column
      if (netbox.cur_col > 0)
      {
         PiNetBox_CursorLeft(NULL, interp, 0, NULL);
      }
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Move the cursor one column to the left, scroll if neccessary
//
static int PiNetBox_ScrollHorizontal( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiBox_ScrollHorizontal {moveto <fract>|scroll <delta> {pages|unit}}";
   uchar * pOption;
   double  fract;
   uint    newOff;
   int     delta;
   int     result;

   if (netbox.pi_resync)
      PiNetBox_RefreshEvent((ClientData)TRUE);

   pOption = Tcl_GetString(objv[1]);
   if ((objc == 2+1) && (strcmp(pOption, "moveto") == 0))
   {
      result = Tcl_GetDoubleFromObj(interp, objv[2], &fract);
      if (result == TCL_OK)
      {
         if (netbox.net_count > netbox.col_count)
         {
            newOff = (uint)(0.5 + fract * netbox.net_count);
            if (newOff > netbox.net_count - netbox.col_count)
               newOff = netbox.net_count - netbox.col_count;

            if (newOff > netbox.net_off)
               PiNetBox_ScrollRight(newOff - netbox.net_off);
            else if (newOff < netbox.net_off)
               PiNetBox_ScrollLeft(netbox.net_off - newOff);
         }
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
            {  // right
               PiNetBox_ScrollRight(netbox.col_count);
            }
            else if (delta < 0)
            {  // left
               PiNetBox_ScrollLeft(netbox.col_count);
            }
         }
         else if (strncmp(pOption, "unit", 4) == 0)
         {
            if (delta > 0)
            {  // right
               PiNetBox_ScrollRight(delta);
            }
            else if (delta < 0)
            {  // left
               PiNetBox_ScrollLeft(0 - delta);
            }
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
// Set cursor onto specified item (after single mouse click)
//
static int PiNetBox_SelectItem( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiBox_SelectItem <text_row> <column>";
   NETBOX_COL   * pCol;
   uint  elemIdx;
   int   newRow;
   int   newCol;
   int   result;

   if (netbox.pi_resync)
      PiNetBox_RefreshEvent((ClientData)TRUE);

   if ( (objc == 3) &&
        (Tcl_GetIntFromObj(interp, objv[1], &newRow) == TCL_OK) &&
        (Tcl_GetIntFromObj(interp, objv[2], &newCol) == TCL_OK) )
   {
      // special case: column value -1 -> keep current value
      if (newCol < 0)
         newCol = netbox.cur_col;
      // if there are more columns than networks, limit new column index
      if (newCol + netbox.net_off >= netbox.net_count)
         newCol = netbox.net_count - netbox.net_off - 1;

      if ((newCol >= 0) && (newCol < netbox.col_count))
      {
         pCol = netbox.cols + newCol;

         newRow -= pCol->start_off;
         if (newRow < 0)
            newRow = 0;
         netbox.cur_req_row = newRow;

         elemIdx = PiNetBox_PickCursorRow(newCol, ABOVE_PARTIAL);

         // check if the cursor position has changed row or to a different element
         if ((newCol != netbox.cur_col) || (elemIdx != netbox.cur_idx))
         {
            // set cursor to new element
            PiNetBox_WithdrawCursor();

            netbox.cur_idx  = elemIdx;
            netbox.cur_col  = newCol;

            if (elemIdx != INVALID_CUR_IDX)
            {
               netbox.cur_req_time = pCol->list[elemIdx].start_time;

               PiNetBox_ScrollIfPartial();
            }

            PiNetBox_UpdateCursorReq(CUR_REQ_GOTO);
            PiNetBox_ShowCursor();
            PiNetBox_UpdateInfoText(FALSE);

            assert(PiNetBox_ConsistancyCheck());
         }
      }
      result = TCL_OK;
   }
   else
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get PI block for currently selected item
//
const PI_BLOCK * PiNetBox_GetSelectedPi( void )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   const PI_BLOCK * pPiBlock = NULL;

   assert(EpgDbIsLocked(dbc));

   if (netbox.cur_col < netbox.col_count)
   {
      pCol = netbox.cols + netbox.cur_col;
      if (netbox.cur_idx < pCol->entries)
      {
         pElem = pCol->list + netbox.cur_idx;

         pPiBlock = EpgDbSearchPi(dbc, pElem->start_time, pElem->netwop);
      }
   }

   return pPiBlock;
}

// ----------------------------------------------------------------------------
// Returns the CNI and name of the network of the currently selected PI
//
static int PiNetBox_GetSelectedNetwop( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiBox_GetSelectedNetwop";
   const AI_BLOCK *pAiBlock;
   Tcl_Obj      * pResultList;
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uchar strbuf[10];
   int result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (netbox.cur_col < netbox.col_count)
      {
         pCol = netbox.cols + netbox.cur_col;
         if (netbox.cur_idx < pCol->entries)
         {
            pElem = pCol->list + netbox.cur_idx;

            EpgDbLockDatabase(dbc, TRUE);
            pAiBlock = EpgDbGetAi(dbc);
            if ((pAiBlock != NULL) && (pElem->netwop < pAiBlock->netwopCount))
            {
               pResultList = Tcl_NewListObj(0, NULL);

               sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pElem->netwop)->cni);
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(strbuf, -1));
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(AI_GET_NETWOP_NAME(pAiBlock, pElem->netwop), -1));

               Tcl_SetObjResult(interp, pResultList);
            }
            EpgDbLockDatabase(dbc, FALSE);
         }
      }
      result = TCL_OK;
   }
   return result;
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
static int PiNetBox_GotoTime( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const PI_BLOCK * pPiBlock;
   const uchar    * pArg;
   EPGDB_TIME_SEARCH_MODE timeMode;
   int    min_start;
   int    param;
   uint   colIdx;

   if (netbox.pi_resync)
      PiNetBox_RefreshEvent((ClientData)TRUE);
   
   if (objc == 3)
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
      {  // start with currently running -> suppress start time restriction
         netbox.cur_req_time = 0;
      }
      else if (strcmp(pArg, "next") == 0)
      {  // start with the first that's not yet running -> start the next second
         netbox.cur_req_time = 1 + EpgDbFilterGetExpireTime(pPiFilterContext);
      }
      else if (Tcl_GetIntFromObj(interp, objv[2], &param) == TCL_OK)
      {  // absolute time given (UTC)
         netbox.cur_req_time = param;
      }
      else  // internal error
         netbox.cur_req_time = 0;

      EpgDbLockDatabase(dbc, TRUE);

      // search the first PI to be diplayed
      pPiBlock = EpgDbSearchFirstPiAfter(dbc, netbox.cur_req_time, timeMode, pPiFilterContext);
      if (pPiBlock == NULL)
         pPiBlock = EpgDbSearchFirstPiBefore(dbc, netbox.cur_req_time, timeMode, pPiFilterContext);

      if (pPiBlock != NULL)
      {
         colIdx = netbox.net_ai2sel[pPiBlock->netwop_no];
         if ((colIdx >= netbox.net_off) && (colIdx < netbox.net_off + netbox.col_count))
            colIdx -= netbox.net_off;
         else
            pPiBlock = NULL;
      }
      else
         colIdx = 0;

      netbox.cur_req_row  = 0;

      PiNetBox_FillAroundPi(pPiBlock, colIdx, 0);

      netbox.cur_col = colIdx;
      netbox.cur_idx = PiNetBox_PickCursorRow(netbox.cur_col, NO_PARTIAL);

      PiNetBox_ShowCursor();
      PiNetBox_AdjustVerticalScrollBar();
      PiNetBox_UpdateInfoText(FALSE);

      assert(PiNetBox_ConsistancyCheck());
      EpgDbLockDatabase(dbc, FALSE);
   }
   return TCL_OK; 
}

// ----------------------------------------------------------------------------
// Jump to the given program title
// - if the item is already in the listbox, just place the cursor on it,
//   if necessary scroll to make it (fully) visible
// - if the PI's network is visible but the item not, redraw the listbox
//   with the given PI at top
// - the listbox is scrolled horizontally if the network is not visible
// - XXX currently no expired PI can be displayed (e.g. for VPS)
//
void PiNetBox_GotoPi( const PI_BLOCK * pPiBlock )
{
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   uint  colIdx;
   uint  elemIdx;

   assert(EpgDbIsLocked(dbc));

   // check if the given PI matches the current filter criteria
   EpgDbFilterDisable(pPiFilterContext, FILTER_NETWOP_PRE2);
   if ( EpgDbFilterMatches(dbc, pPiFilterContext, pPiBlock) )
   {
      colIdx = netbox.net_ai2sel[pPiBlock->netwop_no];
      // check if that network's column is currently visible
      if ((colIdx >= netbox.net_off) && (colIdx < netbox.net_off + netbox.col_count))
      {  // the network is already visible

         EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP_PRE2);
         colIdx -= netbox.net_off;
         pCol    = netbox.cols + colIdx;

         // search for the PI in the listbox
         pElem   = pCol->list;
         for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
            if (pElem->start_time == pPiBlock->start_time)
               break;

         if (elemIdx < pCol->entries)
         {  // found -> simply place the cursor onto it
            PiNetBox_WithdrawCursor();

            netbox.cur_col      = colIdx;
            netbox.cur_idx      = elemIdx;
            netbox.cur_req_row  = pElem->text_row;
            netbox.cur_req_time = pPiBlock->start_time;

            PiNetBox_ScrollIfPartial();

            PiNetBox_ShowCursor();
            PiNetBox_UpdateInfoText(FALSE);
         }
         else
         {  // network visible, but PI not -> place the given PI at top and redraw listbox around it
            netbox.cur_req_time = pPiBlock->start_time;
            netbox.cur_req_row  = 0;

            PiNetBox_FillAroundPi(pPiBlock, colIdx, 0);

            netbox.cur_col = colIdx;
            netbox.cur_idx = 0;

            PiNetBox_ShowCursor();
            PiNetBox_AdjustVerticalScrollBar();
            PiNetBox_UpdateInfoText(FALSE);
         }
         assert(PiNetBox_ConsistancyCheck());
      }
      else if (colIdx != 0xff)
      {  // network not visible -> scroll horizontally

         netbox.cur_req_time = pPiBlock->start_time;
         netbox.cur_req_row  = 0;

         if (colIdx >= netbox.net_off + netbox.col_count)
         {  // scrolling to the right -> place new network in rightmost column
            netbox.net_off = colIdx - netbox.col_count + 1;
         }
         else  // scrolling to the left -> place new network in leftmost column
            netbox.net_off = colIdx;

         colIdx -= netbox.net_off;

         PiNetBox_UpdateNetwopNames();
         PiNetBox_AdjustHorizontalScrollBar();

         PiNetBox_FillAroundPi(pPiBlock, colIdx, 0);

         netbox.cur_col = colIdx;
         netbox.cur_idx = 0;

         PiNetBox_ShowCursor();
         PiNetBox_AdjustVerticalScrollBar();
         PiNetBox_UpdateInfoText(FALSE);

         assert(PiNetBox_ConsistancyCheck());
      }
      else
         EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP_PRE2);
   }
   else
      EpgDbFilterEnable(pPiFilterContext, FILTER_NETWOP_PRE2);
}

// ----------------------------------------------------------------------------
// Process changes in the PI database for the PI listbox
// - invoked by the mgmt module as a callback (function pointer is set during init)
// - XXX TODO check if start time of inserted/deleted PI falls into visible range
//   XXX      if not skip the refresh, do only recount
//
static bool PiNetBox_HandleAcqEvent( const EPGDB_CONTEXT * usedDbc, EPGDB_PI_ACQ_EVENT event,
                                     const PI_BLOCK * pPiBlock, const PI_BLOCK * pObsolete )
{
   uint  selIdx;
   bool  result = FALSE;

   if ( (netbox_init_state == PIBOX_LIST) && (usedDbc == dbc) )
   {
      switch (event)
      {
         case EPGDB_PI_INSERTED:
         case EPGDB_PI_PRE_UPDATE:
         case EPGDB_PI_POST_UPDATE:
         case EPGDB_PI_REMOVED:
            // ignore changes in invisible networks
            if (pPiBlock != NULL)
            {
               selIdx = netbox.net_ai2sel[pPiBlock->netwop_no];
               if ((selIdx >= netbox.net_off) && (selIdx < netbox.net_off + netbox.col_count))
               {
                  if (netbox.pi_resync == FALSE)
                  {
                     AddMainIdleEvent(PiNetBox_RefreshEvent, (ClientData) FALSE, TRUE);
                     netbox.pi_resync = TRUE;
                  }
               }
            }
            else
               fatal1("PiNetBox-HandleAcqEvent: illegal NULL PI block for event %d", event);
            break;

         case EPGDB_PI_RECOUNT:
            break;

         default:
            fatal1("PiNetBox-HandleAcqEvent: unknown event %d received", event);
            break;
      }
   }
   else if ( (netbox_init_state != PIBOX_LIST) && (usedDbc == dbc) &&
             (event == EPGDB_PI_INSERTED) )
   {  // listbox is in an error state (maybe db empty)
      // -> check if status changed due to db insertion
      UiControl_CheckDbState();
   }
   return result;
}

// ----------------------------------------------------------------------------
// Refresh the listbox after filter change
//
static int PiNetBox_RefreshCmd( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   PiNetBox_Refresh();
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Refresh the listbox after filter change
//
static int PiNetBox_ResetCmd( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   PiNetBox_Reset();
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Invalidate update time of all currently displayed elements
// - forces a subsequent refresh to redraw all elements (because only elements
//   which are new or have changed are redrawn)
// - used after attribute configuration change
//
static int PiNetBox_Invalidate( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiNetBox_Invalidate";
   NETBOX_COL     * pCol;
   NETBOX_ELEM    * pElem;
   uint  colIdx;
   sint  elemIdx;
   int   result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (netbox_init_state == PIBOX_LIST)
      {
         pCol = netbox.cols;
         for (colIdx = 0; colIdx < netbox.col_count; colIdx++, pCol++)
         {
            pElem = pCol->list;
            for (elemIdx=0; elemIdx < pCol->entries; elemIdx++, pElem++)
            {
               pElem->upd_time = 0;
            }
         }
      }
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Returns the CNI and name of the network of the currently selected PI
//
static int PiNetBox_GetCniList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PiBox_GetCniList";
   const AI_BLOCK * pAiBlock;
   Tcl_Obj        * pResultList;
   Tcl_Obj        * pColList;
   uint   netwop;
   uint   colIdx;
   uchar  strbuf[10];
   int    result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (netbox_init_state == PIBOX_LIST)
      {
         EpgDbLockDatabase(dbc, TRUE);
         pAiBlock = EpgDbGetAi(dbc);
         if (pAiBlock != NULL)
         {
            pResultList = Tcl_NewListObj(0, NULL);

            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(netbox.net_off));

            for (colIdx=0; colIdx < netbox.net_count; colIdx++)
            {
               pColList = Tcl_NewListObj(0, NULL);
               for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
               {
                  if (netbox.net_ai2sel[netwop] == colIdx)
                  {
                     sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);
                     Tcl_ListObjAppendElement(interp, pColList, Tcl_NewStringObj(strbuf, -1));
                  }
               }
               Tcl_ListObjAppendElement(interp, pResultList, pColList);
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
// Build network joins table
//
static void PiNetBox_BuildJoinMap( uchar * pMap, const AI_BLOCK * pAiBlock )
{
   Tcl_Obj   * pJoinVar;
   Tcl_Obj  ** pJoinList;
   Tcl_Obj  ** pCniList;
   uint  joinCount;
   uint  joinIdx;
   uint  cniCount;
   uint  cniIdx;
   int   cni;
   uchar netwop;

   memset(pMap, 0xff, MAX_NETWOP_COUNT * sizeof(*pMap));

   // retrieve global configuration variable
   pJoinVar = Tcl_GetVar2Ex(interp, "cfnetjoin", NULL, TCL_GLOBAL_ONLY);
   if (pJoinVar != NULL)
   {
      // split outer list, e.g. {{0x100 0x120} {0x200 0x101}} into array of lists
      if (Tcl_ListObjGetElements(interp, pJoinVar, &joinCount, &pJoinList) == TCL_OK)
      {
         for (joinIdx = 0; joinIdx < joinCount; joinIdx++)
         {
            // split inner list into array of CNIs
            if (Tcl_ListObjGetElements(interp, pJoinList[joinIdx], &cniCount, &pCniList) == TCL_OK)
            {
               for (cniIdx = 0; cniIdx < cniCount; cniIdx++)
               {
                  if (Tcl_GetIntFromObj(interp, pCniList[cniIdx], &cni) == TCL_OK)
                  {
                     // convert CNI into network index
                     for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
                        if (cni == AI_GET_NETWOP_N(pAiBlock, netwop)->cni)
                           break;

                     // silently ignore CNIs which are not part of the current db
                     if (netwop < pAiBlock->netwopCount)
                     {
                        ifdebug3(pMap[netwop] != 0xff, "Warning: CNI %04X joined more than once (map %d) in '%s'", netwop, pMap[netwop], Tcl_GetString(pJoinVar));
                        pMap[netwop] = joinIdx;
                     }
                  }
               }
            }
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Copy network mapping table from global Tcl variables
//
static void PiNetBox_UpdateNetwopMap( void )
{
   const AI_BLOCK * pAiBlock;
   Tcl_Obj  ** pMapList;
   Tcl_Obj   * pMapVar;
   uchar netFilter[MAX_NETWOP_COUNT];
   uchar netJoin[MAX_NETWOP_COUNT];
   uchar netJoinCol[MAX_NETWOP_COUNT];
   int  netwop;
   uint sel_idx;
   uint idx;
   int  count;
   bool result = FALSE;

   EpgDbLockDatabase(dbc, TRUE);
   pAiBlock = EpgDbGetAi(dbc);
   if (pAiBlock != NULL)
   {
      if (pAiBlock->netwopCount > 0)
      {
         PiNetBox_BuildJoinMap(netJoin, pAiBlock);
         memset(netJoinCol, 0xff, sizeof(netJoinCol));

         pMapVar = Tcl_GetVar2Ex(interp, "netwop_sel2ai", NULL, TCL_GLOBAL_ONLY);
         if (pMapVar != NULL)
         {
            result = Tcl_ListObjGetElements(interp, pMapVar, &count, &pMapList);
            if (result == TCL_OK)
            {
               memset(netbox.net_ai2sel, 0xff, sizeof(netbox.net_ai2sel));
               sel_idx = 0;
               EpgDbFilterGetNetwopFilter(pPiFilterContext, netFilter, pAiBlock->netwopCount);

               for (idx = 0; idx < count; idx++)
               {
                  if (Tcl_GetIntFromObj(interp, pMapList[idx], &netwop) == TCL_OK)
                  {
                     if (netwop < pAiBlock->netwopCount)
                     {
                        if (netFilter[netwop] != FALSE)
                        {
                           if (netJoin[netwop] == 0xff)
                           {
                              netbox.net_ai2sel[netwop] = sel_idx;
                              sel_idx += 1;
                           }
                           else
                           {
                              if (netJoinCol[netJoin[netwop]] == 0xff)
                              {
                                 netJoinCol[netJoin[netwop]] = sel_idx;
                                 netbox.net_ai2sel[netwop] = sel_idx;
                                 sel_idx += 1;
                              }
                              else
                              {
                                 netbox.net_ai2sel[netwop] = netJoinCol[netJoin[netwop]];
                              }
                           }
                        }
                     }
                     else
                     {
                        debug1("PiNetBox-UpdateNetwopMapping: invalid netwop %d", netwop);
                        break;
                     }
                  }
                  else
                  {
                     debug2("PiNetBox-UpdateNetwopMapping: netwop_sel2ai parse error element %d: '%s'", idx, Tcl_GetString(pMapList[idx]));
                     break;
                  }
               }
               result = (idx >= count);

               if (result)
               {
                  // append networks which are enabled in a network filter,
                  // but not part of the regular network filter selection
                  if (pPiFilterContext->enabledFilters & FILTER_NETWOP)
                  {
                     for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
                     {
                        if ( (netbox.net_ai2sel[netwop] == 0xff) &&
                             (netFilter[netwop] != FALSE) )
                        {
                           netbox.net_ai2sel[netwop] = sel_idx;
                           sel_idx += 1;
                        }
                     }
                  }
               }
               netbox.net_count = sel_idx;
            }
            else
               debug0("PiNetBox-UpdateNetwopMapping: not a list");
         }
         //else
            //debug0("PiNetBox-UpdateNetwopMapping: netwop_sel2ai not defined");
      }

      // no map yet or parse error -> generate 1:1 mapping
      if (result == FALSE)
      {
         netbox.net_count = pAiBlock->netwopCount;

         for (netwop=0; netwop < MAX_NETWOP_COUNT; netwop++)
         {
            netbox.net_ai2sel[netwop] = netwop;
         }
      }

      if (netbox.net_off + netbox.col_count > netbox.net_count)
      {
         if (netbox.net_count >= netbox.col_count)
            netbox.net_off = netbox.net_count - netbox.col_count;
         else
            netbox.net_off = 0;
      }

      // mark all content as invalid
      for (idx=0; idx < netbox.col_count; idx++)
      {
         netbox.cols[idx].entries = 0;
      }
   }
   else
   {  // db is empty
      netbox.net_count = 0;
      netbox.net_off   = 0;
   }
   EpgDbLockDatabase(dbc, FALSE);
}

// ----------------------------------------------------------------------------
// Configure the height and number of columns in the listbox
//
static int PiNetBox_Resize( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   NETBOX_COL   * old_list;
   NETBOX_COL   * pCol;
   NETBOX_ELEM  * pElem;
   Tcl_Obj      * pTmpObj;
   uint  colIdx;
   uint  size;
   uint  max_elems;
   int   height, col_count;

   // read number of lines in text widget from global Tcl variable
   pTmpObj = Tcl_GetVar2Ex(interp, "pibox_height", NULL, TCL_GLOBAL_ONLY);
   if ((pTmpObj == NULL) || (Tcl_GetIntFromObj(interp, pTmpObj, &height) != TCL_OK))
   {
      debug1("PiNetBox-Resize: invalid pibox_height \"%s\"", (pTmpObj == NULL) ? "NULL" : Tcl_GetString(pTmpObj));
      height = PIBOX_DEFAULT_HEIGHT;
   }
   // read configured number of network columns from global Tcl variable
   pTmpObj = Tcl_GetVar2Ex(interp, "pinetbox_col_count", NULL, TCL_GLOBAL_ONLY);
   if ((pTmpObj == NULL) || (Tcl_GetIntFromObj(interp, pTmpObj, &col_count) != TCL_OK))
   {
      debug1("PiNetBox-Resize: invalid pinetbox_col_count \"%s\"", (pTmpObj == NULL) ? "NULL" : Tcl_GetString(pTmpObj));
      col_count = NETBOX_DEFAULT_COL_COUNT;
   }

   // enforce minimums to avoid dealing with special cases
   if (height < NETBOX_ELEM_MIN_HEIGHT)
      height = NETBOX_ELEM_MIN_HEIGHT;
   if (col_count < 1)
      col_count = 1;
   else if (col_count > MAX_NETWOP_COUNT)
      col_count = MAX_NETWOP_COUNT;

   if ( (netbox.cols == NULL) ||
        (height != netbox.height) || (col_count != netbox.col_count) )
   {
      old_list  = netbox.cols;
      max_elems = ((height / NETBOX_ELEM_MIN_HEIGHT) * 3) + 3;
      size      = (col_count * sizeof(NETBOX_COL)) +
                  (max_elems * col_count * sizeof(NETBOX_ELEM));
      netbox.cols = xmalloc(size);

      memset(netbox.cols, 0, size);
      netbox.height    = height;
      netbox.col_count = col_count;
      netbox.max_elems = max_elems;

      pElem  = (NETBOX_ELEM *)((ulong)netbox.cols + (col_count * sizeof(NETBOX_COL)));
      pCol   = netbox.cols;
      for (colIdx=0; colIdx < netbox.col_count; colIdx++, pCol++)
      {
         pCol->list   = pElem;
         pElem += max_elems;
      }

      #if 0
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

         PiNetBox_Refresh();
      }
      else
      #endif
      if (old_list != NULL)
      {
         xfree(old_list);

         // only display during interactive refresh, not during start-up
         netbox.cur_type = CURTYPE_INVISIBLE;
         PiNetBox_UpdateNetwopMap();
         PiNetBox_UpdateNetwopNames();
         PiNetBox_AdjustHorizontalScrollBar();
         PiNetBox_Reset();
      }
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Possible change in network list -> refresh
//
void PiNetBox_AiStateChange( void )
{
   PiNetBox_WithdrawCursor();
   PiNetBox_UpdateNetwopMap();
   PiNetBox_UpdateNetwopNames();
   PiNetBox_AdjustHorizontalScrollBar();
   PiNetBox_Refresh();

   EpgDbSetPiAcqCallback(dbc, PiNetBox_HandleAcqEvent);
}

// ----------------------------------------------------------------------------
// Free resources allocated by the listbox
//
void PiNetBox_Destroy( void )
{
   netbox_init_state = PIBOX_NOT_INIT;

   if (netbox.cols != NULL)
      xfree(netbox.cols);
   netbox.cols = NULL;

   if (dbc != NULL)
      EpgDbSetPiAcqCallback(dbc, NULL);

   if (pPiFilterContext != NULL)
      EpgDbFilterDisable(pPiFilterContext, FILTER_NETWOP_PRE2);
}

// ----------------------------------------------------------------------------
// create the listbox and its commands
// - this should be called only once during start-up
//
void PiNetBox_Create( void )
{
   // common PI listbox API
   Tcl_CreateObjCommand(interp, "C_PiBox_Scroll", PiNetBox_ScrollVertical, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_ScrollHorizontal", PiNetBox_ScrollHorizontal, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_CursorDown", PiNetBox_CursorDown, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_CursorUp", PiNetBox_CursorUp, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_CursorLeft", PiNetBox_CursorLeft, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_CursorRight", PiNetBox_CursorRight, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_GotoTime", PiNetBox_GotoTime, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_SelectItem", PiNetBox_SelectItem, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_Refresh", PiNetBox_RefreshCmd, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_Reset", PiNetBox_ResetCmd, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_Resize", PiNetBox_Resize, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiBox_GetSelectedNetwop", PiNetBox_GetSelectedNetwop, (ClientData) NULL, NULL);

   // specific netbox API
   Tcl_CreateObjCommand(interp, "C_PiNetBox_GetCniList", PiNetBox_GetCniList, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_PiNetBox_Invalidate", PiNetBox_Invalidate, (ClientData) NULL, NULL);

   // initialize state struct
   netbox_init_state = PIBOX_LIST;
   memset(&netbox, 0, sizeof(netbox));

   // initialize config cache
   eval_check(interp, "UpdatePiListboxColumns");

   // allocate memory and initialize
   PiNetBox_Resize(NULL, interp, 0, NULL);
   PiNetBox_UpdateNetwopMap();

   // install callback to be notified about incoming PI blocks
   EpgDbSetPiAcqCallback(dbc, PiNetBox_HandleAcqEvent);
}

