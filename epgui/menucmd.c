/*
 *  Nextview GUI: Execute commands and control status of the menu bar
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
 *  Description:
 *
 *    Provide callbacks for various commands in the menu bar.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: menucmd.c,v 1.13.1.1 2000/11/16 21:33:07 tom Exp $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "tcl.h"
#include "tk.h"

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgtxtdump.h"
#include "epgctl/epgmain.h"
#include "epgctl/epgacqctl.h"
#include "epgui/pilistbox.h"
#include "epgui/pifilter.h"
#include "epgui/statswin.h"
#include "epgui/menucmd.h"


// ----------------------------------------------------------------------------
// Set the states of the entries in Control menu
//
static int SetControlMenuStates(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_SetControlMenuStates";
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      // enable "dump database" only if UI database has at least an AI block
      sprintf(comm, ".menubar.ctrl entryconfigure 2 -state %s\n",
                    ((EpgDbContextGetCni(pUiDbContext) != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "timescales" only if UI database has AI block
      sprintf(comm, ".menubar.ctrl entryconfigure 4 -state %s\n",
                    ((EpgDbContextGetCni(pUiDbContext) != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "acq timescales" only if acq running on different db than ui
      sprintf(comm, ".menubar.ctrl entryconfigure 5 -state %s\n",
                    (((EpgDbContextGetCni(pAcqDbContext) != 0) && (pAcqDbContext != pUiDbContext)) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "acq stats" only if acq running
      sprintf(comm, ".menubar.ctrl entryconfigure 6 -state %s\n",
                    ((EpgDbContextGetCni(pAcqDbContext) != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // check button of "Enable Acq" if acq is running
      sprintf(comm, "set menuStatusStartAcq %d", (pAcqDbContext != NULL));
      eval_check(interp, comm);

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Toggle dump of incoming PI
//
static int MenuCmd_ToggleDumpStream(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ToggleDumpStream <boolean>";
   uint value;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetBoolean(interp, argv[1], &value))
   {  // string parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else
   {
      EpgTxtDump_Toggle();
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Toggle acquisition on/off
//
static int MenuCmd_ToggleAcq(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ToggleAcq <boolean>";
   uint value;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetBoolean(interp, argv[1], &value))
   {  // string parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else
   {
      if (EpgAcqCtl_Toggle(value) != value)
      {
         sprintf(comm, "tk_messageBox -type ok -icon error "
                       "-message {Failed to %s acquisition. "
                                 "Close all other video applications and try again.}\n",
                       (value ? "start" : "stop"));
         eval_check(interp, comm);
      }
      // update help message in listbox if database is empty
      PiListBox_UpdateState();

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Dump the complete database
//
static int MenuCmd_DumpDatabase(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_DumpDatabase <file-name> <pi=0/1> <ai=0/1> <bi=0/1>"
                                              " <ni=0/1> <oi=0/1> <mi=0/1> <li=0/1> <ti=0/1>";
   int do_pi, do_ai, do_bi, do_ni, do_oi, do_mi, do_li, do_ti;
   FILE *fp;
   int result;

   if (argc != 1+1+8)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetBoolean(interp, argv[2], &do_pi) || 
             Tcl_GetBoolean(interp, argv[3], &do_ai) || 
             Tcl_GetBoolean(interp, argv[4], &do_bi) || 
             Tcl_GetBoolean(interp, argv[5], &do_ni) || 
             Tcl_GetBoolean(interp, argv[6], &do_oi) || 
             Tcl_GetBoolean(interp, argv[7], &do_mi) || 
             Tcl_GetBoolean(interp, argv[8], &do_li) || 
             Tcl_GetBoolean(interp, argv[9], &do_ti) )
   {  // one of the params is not boolean
      result = TCL_ERROR;
   }
   else
   {
      if ((argv[1] != NULL) && (argv[1][0] != 0))
         fp = fopen(argv[1], "w");
      else
         fp = stdout;

      if (fp != NULL)
      {
         EpgTxtDump_Database(pUiDbContext, fp, do_pi, do_ai, do_bi, do_ni, do_oi, do_mi, do_li, do_ti);
         if (fp != stdout)
            fclose(fp);
         result = TCL_OK;
      }
      else
      {
         sprintf(comm, "Dump database: file create failed: %s\n", strerror(errno));
         interp->result = comm;
         result = TCL_ERROR;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Show the info for a selected provider in the Provider Window
//
static void ProvWin_UpdateInfo( const EPGDBSAV_PEEK *pPeek )
{
   uchar netwop;

   if ((pPeek != NULL) && (pPeek->pAiBlock != NULL))
   {
      // update the service name
      sprintf(comm, "set provwin_servicename {%s}\n", AI_GET_SERVICENAME(&pPeek->pAiBlock->blk.ai));
      eval_check(interp, comm);

      // remove the old netwop list
      sprintf(comm, ".provwin.n.info.net.list configure -state normal\n"
                    ".provwin.n.info.net.list delete 1.0 end\n");
      eval_check(interp, comm);

      for ( netwop = 0; netwop < pPeek->pAiBlock->blk.ai.netwopCount; netwop++ ) 
      {
         sprintf(comm, ".provwin.n.info.net.list insert end {%s%s}\n",
                       AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, netwop),
                       ((netwop == pPeek->pAiBlock->blk.ai.netwopCount - 1) ? "" : ", "));
         eval_check(interp, comm);
      }

      sprintf(comm, ".provwin.n.info.net.list configure -state disabled\n");
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// Provider window opened -> fill with list of providers
//
static int ProvWin_Open(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ProvWin_Open";
   const EPGDBSAV_PEEK *pPeek;
   const AI_BLOCK *pDbAi;
   uint cni, dbCni;
   int index;
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = TCL_OK;

      // get the CNI of the currently used database
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pDbAi = EpgDbGetAi(pUiDbContext);
      if (pDbAi != NULL)
         dbCni = AI_GET_NETWOP_N(pDbAi, pDbAi->thisNetwop)->cni;
      else
         dbCni = 0;
      EpgDbLockDatabase(pUiDbContext, FALSE);

      index = 0;
      while ( (cni = EpgDbReloadScan(index)) != 0 )
      {
         pPeek = EpgDbPeek(cni);
         if (pPeek != NULL)
         {
            assert(cni == AI_GET_NETWOP_N(&pPeek->pAiBlock->blk.ai, pPeek->pAiBlock->blk.ai.thisNetwop)->cni);

            sprintf(comm, "set provwin_list(%d) 0x%04X\n", index, cni);
            eval_check(interp, comm);

            // add name of provider's network to the listbox
            sprintf(comm, ".provwin.n.b.list insert end {%s}\n",
                          AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, pPeek->pAiBlock->blk.ai.thisNetwop));
            eval_check(interp, comm);

            // select the currently active provider's entry in the listbox
            if (dbCni == cni)
            {
               sprintf(comm, ".provwin.n.b.list selection set %d\n", index);
               eval_check(interp, comm);

               ProvWin_UpdateInfo(pPeek);
            }

            EpgDbPeekDestroy(pPeek);
         }
         else
            break;

         index += 1;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Provider selected for info
//
static int ProvWin_Select(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ProvWin_Select <prov-idx>";
   const EPGDBSAV_PEEK *pPeek;
   char *cni_str;
   uint cni;
   int index;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetInt(interp, argv[1], &index) )
   {  // the parameter is not an integer
      result = TCL_ERROR;
   }
   else
   {
      sprintf(comm, "%d", index);
      cni_str = Tcl_GetVar2(interp, "provwin_list", comm, TCL_GLOBAL_ONLY);
      if ((cni_str != NULL) && (Tcl_GetInt(interp, cni_str, &cni) == TCL_OK))
      {
         pPeek = EpgDbPeek(cni);
         if (pPeek != NULL)
         {
            //sprintf(comm, ".provwin.n.b.list selection set %d\n", index);
            //eval_check(interp, comm);

            ProvWin_UpdateInfo(pPeek);

            EpgDbPeekDestroy(pPeek);
         }
         result = TCL_OK;
      }
      else
      {
         sprintf(comm, "C_ProvWin_Select: no provider found at index %d", index);
         Tcl_SetResult(interp, comm, TCL_VOLATILE);
         result = TCL_ERROR;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Provider window closed -> change provider
//
static int ProvWin_Exit(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ProvWin_Exit [<prov-idx>]";
   int cni, oldAcqCni;
   char *cni_str;
   int index;
   int result;

   if ((argc != 1) && (argc != 2))
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = TCL_OK;

      // get the index of the selected provider, or -1 if none
      if ((argc == 2) && (Tcl_GetInt(interp, argv[1], &index) == TCL_OK) && (index != -1))
      {
         sprintf(comm, "%d", index);
         cni_str = Tcl_GetVar2(interp, "provwin_list", comm, TCL_GLOBAL_ONLY);
         if ((cni_str != NULL) && (Tcl_GetInt(interp, cni_str, &cni) == TCL_OK))
         {
            oldAcqCni = EpgDbContextGetCni(pAcqDbContext);

            EpgAcqCtl_CloseDb(DB_TARGET_UI);
            EpgAcqCtl_OpenDb(DB_TARGET_UI, cni);

            StatsWin_ProvChange(DB_TARGET_UI);

            // note that acq db may change in parallel, if there is no EPG
            // reception on the current channel; this is to avoid uselessly
            // keeping a 2nd db open
            if (oldAcqCni != EpgDbContextGetCni(pAcqDbContext))
               StatsWin_ProvChange(DB_TARGET_ACQ);

            PiFilter_UpdateNetwopList(NULL);
            eval_check(interp, "C_ResetFilter all; ResetFilterState");

            PiListBox_UpdateState();
            PiListBox_Reset();

            // put the new CNI at the front of the selection order and update the config file
            sprintf(comm, "UpdateProvSelection 0x%04X\n", cni);
            eval_check(interp, comm);
         }
      }
   }

   return result;
}

#ifndef WIN32
// ----------------------------------------------------------------------------
// Append a line to the EPG scan messages
//
void MenuCmd_AddEpgScanMsg( char *pMsg )
{
   if (Tcl_VarEval(interp, ".epgscan.all.fmsg.msg insert end {", pMsg, "\n}\n"
                           ".epgscan.all.fmsg.msg see {end linestart - 2 lines}\n",
                           NULL
                  ) != TCL_OK)
      debug0("MenuCmd_AddEpgScanMsg: Tcl/Tk cmd failed");
}

// ----------------------------------------------------------------------------
// Start EPG scan
// - during the scan the focus is forced into the .epgscan popup
//
static int MenuCmd_StartEpgScan(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   // clear message window
   sprintf(comm, ".epgscan.all.fmsg.msg delete 1.0 end\n");
   eval_check(interp, comm);

   if (EpgAcqCtl_StartScan())
   {
      sprintf(comm, ".epgscan.cmd.start configure -state disabled\n"
                    ".epgscan.cmd.stop configure -state normal\n"
                    ".epgscan.cmd.dismiss configure -state disabled\n");
      eval_check(interp, comm);
   }
   else
   {
      sprintf(comm, "tk_messageBox -type ok -icon error "
                    "-message {Failed to open video device. "
                              "Close all other video applications and try again.}\n");
      eval_check(interp, comm);
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Stop EPG scan
// - called from UI after Abort button or when popup is destroyed
// - also called from scan handler when scan has finished
//
int MenuCmd_StopEpgScan(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   if (argc > 0)
   {  // called from UI -> stop scan handler
      EpgAcqCtl_StopScan();
   }

   sprintf(comm, "if {[string length [info commands .epgscan.cmd]] > 0} {"
                 "   .epgscan.cmd.start configure -state normal\n"
                 "   .epgscan.cmd.stop configure -state disabled\n"
                 "   .epgscan.cmd.dismiss configure -state normal\n"
                 "}\n");
   eval_check(interp, comm);

   PiListBox_UpdateState();

   return TCL_OK;
}
#endif //WIN32

// ----------------------------------------------------------------------------
// Initialize the module
//
void MenuCmd_Init( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_ToggleAcq", &cmdInfo) == 0)
   {  // Create callback functions
      Tcl_CreateCommand(interp, "C_ToggleAcq", MenuCmd_ToggleAcq, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ToggleDumpStream", MenuCmd_ToggleDumpStream, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_DumpDatabase", MenuCmd_DumpDatabase, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SetControlMenuStates", SetControlMenuStates, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_ProvWin_Open", ProvWin_Open, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ProvWin_Select", ProvWin_Select, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ProvWin_Exit", ProvWin_Exit, (ClientData) NULL, NULL);

      #ifndef WIN32
      Tcl_CreateCommand(interp, "C_StartEpgScan", MenuCmd_StartEpgScan, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_StopEpgScan", MenuCmd_StopEpgScan, (ClientData) NULL, NULL);
      #else
      sprintf(comm, ".menubar.config entryconfigure 1 -state disabled\n");
      eval_check(interp, comm);
      #endif

      if (pDemoDatabase != NULL)
      {  // create menu with warning labels and disable some menu commands
         sprintf(comm, "CreateDemoModePseudoMenu\n");
         eval_check(interp, comm);
      }
   }
}

