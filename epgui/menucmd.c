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
 *
 *  Description:
 *
 *    Provide callbacks for various commands in the menu bar.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: menucmd.c,v 1.72 2002/05/05 20:51:38 tom Exp $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/epgnetio.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgscan.h"
#include "epgctl/epgctxmerge.h"
#include "epgui/epgmain.h"
#include "epgui/epgtxtdump.h"
#include "epgui/pilistbox.h"
#include "epgui/pifilter.h"
#include "epgui/menucmd.h"
#include "epgui/uictrl.h"
#include "epgui/xawtv.h"
#include "epgui/wintvcfg.h"
#include "epgctl/epgctxctl.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/cni_tables.h"
#include "epgvbi/tvchan.h"
#include "epgvbi/btdrv.h"
#ifdef WIN32
#include "dsdrv/debuglog.h"
#endif


static void MenuCmd_EpgScanHandler( ClientData clientData );

// ----------------------------------------------------------------------------
// Set the states of the entries in Control menu
//
static int MenuCmd_SetControlMenuStates(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_SetControlMenuStates";
   EPGACQ_DESCR acqState;
   uint uiCni;
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (IsDemoMode() == FALSE)
   {
      EpgAcqCtl_DescribeAcqState(&acqState);
      uiCni = EpgDbContextGetCni(pUiDbContext);

      // enable "dump database" only if UI database has at least an AI block
      sprintf(comm, ".menubar.ctrl entryconfigure \"Dump raw database...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);
      sprintf(comm, ".menubar.ctrl entryconfigure \"Dump in HTML...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "timescales" only if UI database has AI block
      sprintf(comm, ".menubar.ctrl entryconfigure \"View timescales...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "acq timescales" only if acq running on different db than ui
      sprintf(comm, ".menubar.ctrl entryconfigure \"View acq timescales...\" -state %s\n",
                    ((acqState.dbCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "db stats" only if UI db has AI block
      sprintf(comm, ".menubar.ctrl entryconfigure \"View statistics...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "acq stats" only if acq running
      sprintf(comm, ".menubar.ctrl entryconfigure \"View acq statistics...\" -state %s\n",
                    ((pAcqDbContext != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // check button of "Enable Acq" if acq is running
      sprintf(comm, "set menuStatusStartAcq %d\n", (acqState.state != ACQDESCR_DISABLED));
      eval_check(interp, comm);

      #ifdef USE_DAEMON
      // check button of "Connect to daemon" if acq is running
      // - note: the button also reflects "off" when the acq is actually still enabled
      //   but in "retry" mode after a network error. This must be taken into account
      //   in the callbacks for the "start acq" entries.
      sprintf(comm, "set menuStatusDaemon %d",
                    ((acqState.state != ACQDESCR_DISABLED) && acqState.isNetAcq) );
      eval_check(interp, comm);

      // enable "Enable acquisition" only if not connected to a non-local daemon
      sprintf(comm, ".menubar.ctrl entryconfigure \"Enable acquisition\" -state %s\n",
                    (( (acqState.state == ACQDESCR_DISABLED) ||
                       (acqState.isNetAcq == FALSE) ||
                       (acqState.isNetAcq && EpgAcqClient_IsLocalServer()) ) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "connect to acq. daemon" only if acq not already running locally
      sprintf(comm, ".menubar.ctrl entryconfigure \"Connect to acq. daemon\" -state %s\n",
                    (((acqState.state == ACQDESCR_DISABLED) || acqState.isNetAcq) ? "normal" : "disabled"));
      eval_check(interp, comm);
      #else
      // no daemon -> always disable the option
      sprintf(comm, ".menubar.ctrl entryconfigure \"Connect to acq. daemon\" -state disabled\n");
      eval_check(interp, comm);
      #endif

      result = TCL_OK;
   }
   else
      result = TCL_OK;

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
// Start acquisition in auto-detected mode
// - used at start-up and when signal HUP is received
// - starts acq in the last manually chosen mode; if that fails the other is tried
//
void AutoStartAcq( Tcl_Interp * interp )
{
   char  str_buf[10];
   bool  isNetAcq;

   if (EpgAcqCtl_Start() == FALSE)
   {
      // save previous acq mode to detect mode changes
      isNetAcq = IsRemoteAcqEnabled(interp);

      sprintf(str_buf, "%d", !isNetAcq);
      Tcl_SetVar(interp, "netacq_enable", str_buf, TCL_GLOBAL_ONLY);
      SetAcquisitionMode();

      if (EpgAcqCtl_Start())
      {  // ok -> update the rc/ini file
         eval_check(interp, "UpdateRcFile");
      }
      else
      {  // failed
         sprintf(str_buf, "%d", isNetAcq);
         Tcl_SetVar(interp, "netacq_enable", str_buf, TCL_GLOBAL_ONLY);
         SetAcquisitionMode();
      }
   }
}

// ----------------------------------------------------------------------------
// Start local acquisition
//
static void MenuCmd_StartLocalAcq( Tcl_Interp * interp )
{
   bool wasNetAcq;

   // save previous acq mode to detect mode changes
   wasNetAcq = IsRemoteAcqEnabled(interp);

   Tcl_SetVar(interp, "netacq_enable", "0", TCL_GLOBAL_ONLY);
   SetAcquisitionMode();

   if (EpgAcqCtl_Start())
   {
      // if acq mode changed, update the rc/ini file
      if (wasNetAcq)
         eval_check(interp, "UpdateRcFile");
   }
   else
   {
      #ifdef USE_DAEMON
      if (EpgNetIo_CheckConnect())
      {  // daemon is running locally, probably on the same device
         // XXX actually we'd have to check if the daemon uses the same card
         strcpy(comm, "tk_messageBox -type okcancel -icon error "
                      "-message {Failed to start acquisition: the daemon seems to be running. "
                                "Do you want to stop the daemon now?}\n");
         eval_check(interp, comm);
         if (strcmp(interp->result, "ok") == 0)
         {
            // if acq mode changed, update the rc/ini file
            if (wasNetAcq == FALSE)
               eval_check(interp, "UpdateRcFile");

            if (EpgMain_StopDaemon())
            {  // successfully terminated the daemon (the function doesn't return until the process is gone)
               // attempt to start again now (ignore errors)
               EpgAcqCtl_Start();
            }
            else
            {  // failed to stop the daemon
               strcpy(comm, "tk_messageBox -type ok -icon error -message {Failed to stop the daemon.}");
               eval_check(interp, comm);
            }
         }
      }
      else
      #endif
      #ifndef WIN32  //is handled by bt-driver in win32
      {
         strcpy(comm, "tk_messageBox -type ok -icon error "
                      "-message {Failed to start acquisition. "
                                "Close all other video applications and try again.}\n");
         eval_check(interp, comm);
      }
      #else
         ;  // required by "else" above (if daemon in use; doesn't harm otherwise)
      #endif

      // operation failed -> keep the old mode
      if (wasNetAcq)
         Tcl_SetVar(interp, "netacq_enable", "1", TCL_GLOBAL_ONLY);
   }
}

// ----------------------------------------------------------------------------
// Connect to acquisition daemon
// - automatically start daemon if not yet running
//
static void MenuCmd_StartRemoteAcq( Tcl_Interp * interp )
{
#ifdef USE_DAEMON
   bool wasNetAcq;

   // save previous acq mode to detect mode changes
   wasNetAcq = IsRemoteAcqEnabled(interp);

   Tcl_SetVar(interp, "netacq_enable", "1", TCL_GLOBAL_ONLY);
   SetAcquisitionMode();

   if (EpgAcqCtl_Start())
   {
      // if acq mode changed, update the rc/ini file
      if (wasNetAcq == FALSE)
         eval_check(interp, "UpdateRcFile");
   }
   else
   {  // failed to connect to the server

      if (EpgAcqClient_IsLocalServer())
      {
         strcpy(comm, "tk_messageBox -type okcancel -icon error "
                      "-message {The daemon seems not to be running (connect failed). "
                                "Do you want to start the daemon now?}\n");
         eval_check(interp, comm);
         if (strcmp(interp->result, "ok") == 0)
         {
            // if acq mode changed, update the rc/ini file
            if (wasNetAcq == FALSE)
               eval_check(interp, "UpdateRcFile");

            EpgMain_StartDaemon();
         }
      }
      else
         UiControlMsg_NetAcqError();

      // operation failed -> keep the old mode
      if (wasNetAcq == FALSE)
         Tcl_SetVar(interp, "netacq_enable", "0", TCL_GLOBAL_ONLY);
   }
#endif
}

// ----------------------------------------------------------------------------
// Toggle acquisition on/off
// - acquisition may be started either locally or via a network connection
//
static int MenuCmd_ToggleAcq(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ToggleAcq <enable-acq> <enable-daemon>";
   EPGACQ_DESCR acqState;
   int  enableAcq, enableDaemon;
   int  result;

   if (argc != 3)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( (Tcl_GetBoolean(interp, argv[1], &enableAcq) != TCL_OK) ||
             (Tcl_GetBoolean(interp, argv[2], &enableDaemon) != TCL_OK) )
   {  // parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else
   {
      EpgAcqCtl_DescribeAcqState(&acqState);

      if (acqState.state == ACQDESCR_DISABLED)
      {
         // if network acq is in a "retry connect after error" state, really stop it now
         if (acqState.isNetAcq)
            EpgAcqCtl_Stop();

         if ((enableAcq != FALSE) && (enableDaemon == FALSE))
         {  // start local acquisition
            MenuCmd_StartLocalAcq(interp);
         }
         else if ((enableAcq == FALSE) && (enableDaemon != FALSE))
         {  // connect to an acquisition daemon
            MenuCmd_StartRemoteAcq(interp);
         }
         else
            debug2("MenuCmd-ToggleAcq: illegal start params: enableAcq=%d, enableDaemon=%d", enableAcq, enableDaemon);
      }
      else
      {
         #ifdef USE_DAEMON
         if ((enableAcq == FALSE) && acqState.isNetAcq)
         {
            if (EpgAcqClient_IsLocalServer())
            {  // stop acquisition in the daemon -> kill the daemon
               EpgAcqCtl_Stop();
               if (EpgMain_StopDaemon() == FALSE)
               {  // failed to stop the daemon -> inform the user
                  strcpy(comm, "tk_messageBox -type ok -icon error -message {Failed to stop the daemon - you're disconnected but acquisition continues in the background.}");
                  eval_check(interp, comm);
               }
            }
            else
               debug0("MenuCmd-ToggleAcq: illegal stop params: requested to stop remote acq");
         }
         else
         #endif
         {  // stop local acq or disconnect from the server
            EpgAcqCtl_Stop();
         }
      }

      // update help message in listbox if database is empty
      UiControl_CheckDbState();

      Tcl_ResetResult(interp);
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Dump the complete database
//
static int MenuCmd_DumpDatabase(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_DumpDatabase <file-name> <pi=0/1> <xi=0/1> <ai=0/1>"
                                              " <ni=0/1> <oi=0/1> <mi=0/1> <li=0/1> <ti=0/1>";
   int do_pi, do_xi, do_ai, do_ni, do_oi, do_mi, do_li, do_ti;
   FILE *fp;
   int result;

   if (argc != 1+1+8)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( (Tcl_GetBoolean(interp, argv[2], &do_pi) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[3], &do_xi) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[4], &do_ai) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[5], &do_ni) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[6], &do_oi) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[7], &do_mi) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[8], &do_li) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[9], &do_ti) != TCL_OK) )
   {  // one of the params is not boolean; error msg is already set
      result = TCL_ERROR;
   }
   else
   {
      if ((argv[1] != NULL) && (argv[1][0] != 0))
      {
         fp = fopen(argv[1], "w");
         if (fp == NULL)
         {  // access, create or truncate failed -> inform the user
            sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumpdb -message \"Failed to open file '%s' for writing: %s\"",
                          argv[1], strerror(errno));
            eval_check(interp, comm);
            Tcl_ResetResult(interp);
         }
      }
      else
         fp = stdout;

      if (fp != NULL)
      {
         EpgTxtDump_Database(pUiDbContext, fp, (bool)do_pi, (bool)do_xi, (bool)do_ai, (bool)do_ni,
                                               (bool)do_oi, (bool)do_mi, (bool)do_li, (bool)do_ti);
         if (fp != stdout)
            fclose(fp);
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Switch the provider for the browser
// - only used for the provider selection popup,
//   not for the initial selection nor for provider merging
//
static int MenuCmd_ChangeProvider(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ChangeProvider <cni>";
   EPGDB_CONTEXT * pDbContext;
   int cni;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      // get the CNI of the selected provider
      if (Tcl_GetInt(interp, argv[1], &cni) == TCL_OK)
      {
         // note: illegal CNIs 0 and 0x00ff are caught in the open function
         pDbContext = EpgContextCtl_Open(cni, CTX_FAIL_RET_NULL, CTX_RELOAD_ERR_REQ);
         if (pDbContext != NULL)
         {
            EpgContextCtl_Close(pUiDbContext);
            pUiDbContext = pDbContext;

            // in case follow-ui acq mode is used, change the acq db too
            SetAcquisitionMode();

            UiControl_AiStateChange(DB_TARGET_UI);
            eval_check(interp, "C_ResetFilter all; ResetFilterState");

            PiListBox_Reset();

            // put the new CNI at the front of the selection order and update the config file
            sprintf(comm, "UpdateProvSelection 0x%04X\n", cni);
            eval_check(interp, comm);
         }

         result = TCL_OK;
      }
      else
      {
         sprintf(comm, "C_ChangeProvider: expected hex CNI but got: %s", argv[1]);
         Tcl_SetResult(interp, comm, TCL_VOLATILE);
         result = TCL_ERROR;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Return descriptive text for a given 16-bit network code
//
static int MenuCmd_GetCniDescription(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetCniDescription <cni>";
   const char * pName, * pCountry;
   int cni;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetInt(interp, argv[1], &cni) )
   {  // the parameter is not an integer
      result = TCL_ERROR;
   }
   else
   {
      pName = CniGetDescription(cni, &pCountry);
      if (pName != NULL)
      {
         if ((pCountry != NULL) && (strstr(pName, pCountry) == NULL))
         {
            sprintf(comm, "%s (%s)", pName, pCountry);
            Tcl_SetResult(interp, comm, TCL_VOLATILE);
         }
         else
            Tcl_SetResult(interp, (char *)pName, TCL_STATIC);
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Return service name and list of networks of the given database
// - used by provider selection popup
//
static int MenuCmd_GetProvServiceInfos(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetProvServiceInfos <cni>";
   const AI_BLOCK * pAi;
   const OI_BLOCK * pOi;
   EPGDB_CONTEXT  * pPeek;
   int cni, netwop;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetInt(interp, argv[1], &cni) )
   {  // the parameter is not an integer
      result = TCL_ERROR;
   }
   else
   {
      pPeek = EpgContextCtl_Peek(cni, CTX_RELOAD_ERR_ANY);
      if (pPeek != NULL)
      {
         EpgDbLockDatabase(pPeek, TRUE);
         pAi = EpgDbGetAi(pPeek);
         pOi = EpgDbGetOi(pPeek, 0);

         if (pAi != NULL)
         {
            // first element in return list is the service name
            Tcl_AppendElement(interp, AI_GET_SERVICENAME(pAi));

            // second element is OI header
            if ((pOi != NULL) && OI_HAS_HEADER(pOi))
               Tcl_AppendElement(interp, OI_GET_HEADER(pOi));
            else
               Tcl_AppendElement(interp, "");

            // third element is OI message
            if ((pOi != NULL) && OI_HAS_MESSAGE(pOi))
               Tcl_AppendElement(interp, OI_GET_MESSAGE(pOi));
            else
               Tcl_AppendElement(interp, "");

            // append names of all netwops
            for ( netwop = 0; netwop < pAi->netwopCount; netwop++ ) 
            {
               Tcl_AppendElement(interp, AI_GET_NETWOP_NAME(pAi, netwop));
            }
         }
         EpgDbLockDatabase(pPeek, FALSE);

         EpgContextCtl_ClosePeek(pPeek);
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Return the CNI of the database currently open for the browser
// - result is 0 if none is opened or 0x00ff for the merged db
//
static int MenuCmd_GetCurrentDatabaseCni(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetCurrentDatabaseCni";
   uint dbCni;
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {  // return the CNI of the currently used browser database
      dbCni = EpgDbContextGetCni(pUiDbContext);

      sprintf(comm, "0x%04X", dbCni);
      Tcl_SetResult(interp, comm, TCL_VOLATILE);

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get list of provider CNIs and names
// - for provider selection and merge popup
//
static int MenuCmd_GetProvCnisAndNames(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetProvCnisAndNames";
   const AI_BLOCK * pAi;
   const uint * pCniList;
   EPGDB_CONTEXT  * pPeek;
   uint idx, cniCount;
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pCniList = EpgContextCtl_GetProvList(&cniCount);
      for (idx=0; idx < cniCount; idx++)
      {
         pPeek = EpgContextCtl_Peek(pCniList[idx], CTX_RELOAD_ERR_ANY);
         if (pPeek != NULL)
         {
            EpgDbLockDatabase(pPeek, TRUE);
            pAi = EpgDbGetAi(pPeek);
            if (pAi != NULL)
            {
               sprintf(comm, "0x%04X", pCniList[idx]);
               Tcl_AppendElement(interp, comm);
               Tcl_AppendElement(interp, AI_GET_NETWOP_NAME(pAi, pAi->thisNetwop));
            }
            EpgDbLockDatabase(pPeek, FALSE);

            EpgContextCtl_ClosePeek(pPeek);
         }
      }
      if (pCniList != NULL)
         xfree((void *) pCniList);
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Append list of networks in a AI to the result
// - as a side-effect the netwop names are stored into a TCL array
//
static void MenuCmd_AppendNetwopList( Tcl_Interp *interp, const AI_BLOCK * pAiBlock, char * pArrName, bool unify )
{
   uchar strbuf[15];
   uint  netwop;

   if (pAiBlock != NULL)
   {
      for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
      {
         sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);

         // if requested check if the same CNI is already in the result
         // XXX hack: only works if name array is given
         if ( (unify == FALSE) || (pArrName[0] == 0) ||
              (Tcl_GetVar2(interp, pArrName, strbuf, 0) == NULL) )
         {
            // append the CNI in the format "0x0D94" to the TCL result list
            Tcl_AppendElement(interp, strbuf);

            // as a side-effect the netwop names are stored into a TCL array
            if (pArrName[0] != 0)
               Tcl_SetVar2(interp, pArrName, strbuf, (char *)AI_GET_NETWOP_NAME(pAiBlock, netwop), 0);
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Get List of netwop CNIs and array of names from AI for netwop selection
// - 1st argument selects the provider; 0 for the current browser database
// - 2nd argument names a Tcl array where to store the network names;
//   may be 0-string if not needed
// - optional 3rd argument may be "allmerged" if in case of a merged database
//   all CNIs of all source databases shall be returned
//
static int MenuCmd_GetAiNetwopList( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GetAiNetwopList <CNI> <varname> [allmerged]";
   EPGDB_CONTEXT  * pPeek;
   const AI_BLOCK * pAi;
   int result, cni;

   if ((argc != 3) && ((argc != 4) || strcmp(argv[3], "allmerged")))
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_GetInt(interp, argv[1], &cni);
      if (result == TCL_OK)
      {
         // clear the network names result array
         if (argv[2][0] != 0)
            Tcl_UnsetVar(interp, argv[2], 0);

         // special case: CNI 0 refers to the current browser db
         if (cni == 0)
            cni = EpgDbContextGetCni(pUiDbContext);

         if (cni != 0x00FF)
         {  // regular (non-merged) database -> get "peek" with (at least) AI and OI
            pPeek = EpgContextCtl_Peek(cni, CTX_RELOAD_ERR_ANY);
            if (pPeek != NULL)
            {
               EpgDbLockDatabase(pPeek, TRUE);
               pAi = EpgDbGetAi(pPeek);
               if (pAi != NULL)
                  MenuCmd_AppendNetwopList(interp, pAi, argv[2], FALSE);
               else
                  debug1("MenuCmd-GetAiNetwopList: no AI in peek of %04X", cni);
               EpgDbLockDatabase(pPeek, FALSE);

               EpgContextCtl_ClosePeek(pPeek);
            }
            else
               debug1("MenuCmd-GetAiNetwopList: requested db %04X not available", cni);
         }
         else
         {
            assert(EpgDbContextIsMerged(pUiDbContext));

            if (argc < 4)
            {  // merged db -> use the merged AI with the user-configured netwop list
               EpgDbLockDatabase(pUiDbContext, TRUE);
               pAi = EpgDbGetAi(pUiDbContext);
               if (pAi != NULL)
                  MenuCmd_AppendNetwopList(interp, pAi, argv[2], FALSE);
               else
                  debug0("MenuCmd-GetAiNetwopList: no AI in merged db");
               EpgDbLockDatabase(pUiDbContext, FALSE);
            }
            else
            {  // special mode for merged db: return all CNIs from all source dbs
               uint dbIdx, provCount, provCniTab[MAX_MERGED_DB_COUNT];

               if (EpgContextMergeGetCnis(pUiDbContext, &provCount, provCniTab))
               {
                  // peek into all merged dbs
                  for (dbIdx=0; dbIdx < provCount; dbIdx++)
                  {
                     pPeek = EpgContextCtl_Peek(provCniTab[dbIdx], CTX_RELOAD_ERR_ANY);
                     if (pPeek != NULL)
                     {
                        EpgDbLockDatabase(pPeek, TRUE);
                        pAi = EpgDbGetAi(pPeek);
                        if (pAi != NULL)
                           MenuCmd_AppendNetwopList(interp, pAi, argv[2], TRUE);
                        else
                           debug1("MenuCmd-GetAiNetwopList: no AI in peek of %04X (merge source)", cni);
                        EpgDbLockDatabase(pPeek, FALSE);

                        EpgContextCtl_ClosePeek(pPeek);
                     }
                  }
               }
            }
         }
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Retrieve CNI list from the merged database's user network selection
//
static int ProvMerge_ParseNetwopList( Tcl_Interp * interp, uint * pCniCount, uint * pCniTab )
{
   char **pCniArgv, **pSubLists;
   char * pTmpStr;
   int  * pCni, count;
   uint idx;
   int  result;

   pTmpStr = Tcl_GetVar2(interp, "cfnetwops", "0x00FF", TCL_GLOBAL_ONLY);
   if (pTmpStr != NULL)
   {
      // parse list of 2 lists, e.g. {{0x0dc1 0x0dc2} {0x0AC1 0x0AC2}}
      result = Tcl_SplitList(interp, pTmpStr, &count, &pSubLists);
      if (result == TCL_OK)
      {
         if (count == 2)
         {
            // parse CNI list, e.g. {0x0dc1 0x0dc2}
            result = Tcl_SplitList(interp, pSubLists[0], pCniCount, &pCniArgv);
            if (result == TCL_OK)
            {
               pCni = (int *) pCniTab;
               for (idx=0; (idx < *pCniCount) && (idx < MAX_NETWOP_COUNT) && (result == TCL_OK); idx++)
               {
                  result = Tcl_GetInt(interp, pCniArgv[idx], pCni++);
               }

               Tcl_Free((char *) pCniArgv);
            }
         }
         else
            result = TCL_ERROR;

         Tcl_Free((char *) pSubLists);
      }
   }
   else
      result = TCL_ERROR;

   // in case of parser errors, return an empty list
   if (result != TCL_OK)
      *pCniCount = 0;

   return result;
}

// ----------------------------------------------------------------------------
// Parse the Tcl provider merge config string and convert it to attribute matrix
//
static int
ProvMerge_ParseConfigString( Tcl_Interp *interp, uint *pCniCount, uint * pCniTab, MERGE_ATTRIB_VECTOR_PTR pMax )
{
   char **pCniArgv, **pAttrArgv, **pIdxArgv;
   uint attrCount, idxCount, idx, idx2, ati, matIdx, cni;
   char * pTmpStr;
   int result;

   pTmpStr = Tcl_GetVar(interp, "prov_merge_cnis", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      // parse CNI list, format: {0x0d94 ...}
      result = Tcl_SplitList(interp, pTmpStr, pCniCount, &pCniArgv);
      if (result == TCL_OK)
      {
         if (*pCniCount > MAX_MERGED_DB_COUNT)
            *pCniCount = MAX_MERGED_DB_COUNT;

         for (idx=0; (idx < *pCniCount) && (result == TCL_OK); idx++)
         {
            result = Tcl_GetInt(interp, pCniArgv[idx], pCniTab + idx);
         }
         Tcl_Free((char *) pCniArgv);

         if ((result == TCL_OK) && (*pCniCount > 0))
         {  // continue only if all CNIs could be parsed

            // parse attribute list, format is pairs of keyword and index list: {key {3 1 2} ...}
            pTmpStr = Tcl_GetVar(interp, "prov_merge_cf", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
            if (pTmpStr != NULL)
            {
               result = Tcl_SplitList(interp, pTmpStr, &attrCount, &pAttrArgv);
               if (result == TCL_OK)
               {
                  // initialize the attribute matrix with default index order: 0,1,2,...,n-1,0xff,...
                  memset(pMax, 0xff, sizeof(MERGE_ATTRIB_MATRIX));
                  for (ati=0; ati < MERGE_TYPE_COUNT; ati++)
                     for (idx=0; idx < *pCniCount; idx++)
                        pMax[ati][idx] = idx;

                  for (ati=0; (ati+1 < attrCount) && (result == TCL_OK); ati+=2)
                  {
                     // translate keyword to index into attribute matrix
                     if      (strcmp(pAttrArgv[ati], "cftitle") == 0) matIdx = MERGE_TYPE_TITLE;
                     else if (strcmp(pAttrArgv[ati], "cfdescr") == 0) matIdx = MERGE_TYPE_DESCR;
                     else if (strcmp(pAttrArgv[ati], "cfthemes") == 0) matIdx = MERGE_TYPE_THEMES;
                     else if (strcmp(pAttrArgv[ati], "cfseries") == 0) matIdx = MERGE_TYPE_SERIES;
                     else if (strcmp(pAttrArgv[ati], "cfsortcrit") == 0) matIdx = MERGE_TYPE_SORTCRIT;
                     else if (strcmp(pAttrArgv[ati], "cfeditorial") == 0) matIdx = MERGE_TYPE_EDITORIAL;
                     else if (strcmp(pAttrArgv[ati], "cfparental") == 0) matIdx = MERGE_TYPE_PARENTAL;
                     else if (strcmp(pAttrArgv[ati], "cfsound") == 0) matIdx = MERGE_TYPE_SOUND;
                     else if (strcmp(pAttrArgv[ati], "cfformat") == 0) matIdx = MERGE_TYPE_FORMAT;
                     else if (strcmp(pAttrArgv[ati], "cfrepeat") == 0) matIdx = MERGE_TYPE_REPEAT;
                     else if (strcmp(pAttrArgv[ati], "cfsubt") == 0) matIdx = MERGE_TYPE_SUBT;
                     else if (strcmp(pAttrArgv[ati], "cfmisc") == 0) matIdx = MERGE_TYPE_OTHERFEAT;
                     else if (strcmp(pAttrArgv[ati], "cfvps") == 0) matIdx = MERGE_TYPE_VPS;
                     else matIdx = MERGE_TYPE_COUNT;

                     if (matIdx < MERGE_TYPE_COUNT)
                     {
                        // parse index list
                        result = Tcl_SplitList(interp, pAttrArgv[ati + 1], &idxCount, &pIdxArgv);
                        if (result== TCL_OK)
                        {
                           if (idxCount <= *pCniCount)
                           {
                              for (idx=0; (idx < idxCount) && (result == TCL_OK); idx++)
                              {
                                 result = Tcl_GetInt(interp, pIdxArgv[idx], &cni);
                                 if (result == TCL_OK)
                                 {
                                    for (idx2=0; idx2 < *pCniCount; idx2++)
                                       if (pCniTab[idx2] == cni)
                                          break;

                                    if (idx2 < *pCniCount)
                                       pMax[matIdx][idx] = idx2;
                                    else
                                    {
                                       sprintf(comm, "C_ProvMerge_Start: illegal cni: 0x%X for attrib %s", cni, pAttrArgv[ati]);
                                       Tcl_SetResult(interp, comm, TCL_VOLATILE);
                                       result = TCL_ERROR;
                                       break;
                                    }
                                 }
                              }
                              // clear rest of index array, since used db count might be smaller than db merge count
                              for ( ; idx < MAX_MERGED_DB_COUNT; idx++)
                              {
                                 pMax[matIdx][idx] = 0xff;
                              }
                           }
                           else
                           {
                              sprintf(comm, "C_ProvMerge_Start: illegal index count: %d > cni count %d for attrib %s", idxCount, *pCniCount, pAttrArgv[ati]);
                              Tcl_SetResult(interp, comm, TCL_VOLATILE);
                              result = TCL_ERROR;
                              break;
                           }
                        }
                     }
                     else
                     {  // illegal keyword in attribute list
                        sprintf(comm, "C_ProvMerge_Start: unknown attrib type: %s", pAttrArgv[ati]);
                        Tcl_SetResult(interp, comm, TCL_VOLATILE);
                        result = TCL_ERROR;
                        break;
                     }
                  }
                  Tcl_Free((char *) pAttrArgv);
               }
            }
            else
               result = TCL_ERROR;
         }
         else if (*pCniCount == 0)
         {
            Tcl_SetResult(interp, "C_ProvMerge_Start: CNI count 0 is illegal", TCL_STATIC);
            result = TCL_ERROR;
         }
      }
   }
   else
      result = TCL_ERROR;

   return result;
}

// ----------------------------------------------------------------------------
// Update provider selection preference list for the merged database
// - put the fake "Merge" CNI plus the CNIs of all merged providers at the
//   front of the provider selection order
//
static void ProvMerge_UpdateProvSelectionList( uint provCount, uint * provCniTab )
{
   uint  idx;

   sprintf(comm, "UpdateMergedProvSelection {0x00FF ");
   for (idx=0; idx < provCount; idx++)
   {
     sprintf(comm + strlen(comm), "0x%04X ", provCniTab[idx]);
   }
   sprintf(comm + strlen(comm) - 1, "}");
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Initiate the database merging
// - called from the 'Merge providers' popup menu
// - parameters are taken from global Tcl variables, because the same
//   update is required at startup
//
static int ProvMerge_Start(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ProvMerge_Start";
   EPGDB_CONTEXT * pDbContext;
   MERGE_ATTRIB_MATRIX max;
   uint provCniTab[MAX_MERGED_DB_COUNT];
   uint netwopCniTab[MAX_NETWOP_COUNT];
   uint provCount, netwopCount;
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (ProvMerge_ParseConfigString(interp, &provCount, provCniTab, &max[0]) == TCL_OK)
      {
         ProvMerge_ParseNetwopList(interp, &netwopCount, netwopCniTab);

         pDbContext = EpgContextMerge(provCount, provCniTab, max, netwopCount, netwopCniTab);
         if (pDbContext != NULL)
         {
            EpgContextCtl_Close(pUiDbContext);
            pUiDbContext = pDbContext;

            SetAcquisitionMode();

            UiControl_AiStateChange(DB_TARGET_UI);
            eval_check(interp, "C_ResetFilter all; ResetFilterState");

            PiListBox_Reset();

            // put the fake "Merge" CNI plus the CNIs of all merged providers
            // at the front of the provider selection order
            ProvMerge_UpdateProvSelectionList(provCount, provCniTab);
         }

         result = TCL_OK;
      }
      else
         result = TCL_ERROR;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Open the initial database after program start
// - provider can be chosen by the -provider flag: warn if it does not exist
// - else try all dbs in list list of previously opened ones (saved in rc/ini file)
// - else scan the db directory or create an empty database
//
void OpenInitialDb( uint startUiCni )
{
   char **pCniArgv;
   char *pTmpStr;
   uint cni, provIdx, provCount;

   // prepare list of previously opened databases
   provCount = 0;
   pCniArgv = NULL;
   pTmpStr = Tcl_GetVar(interp, "prov_selection", TCL_GLOBAL_ONLY);
   if (pTmpStr != NULL)
   {
      if (Tcl_SplitList(interp, pTmpStr, &provCount, &pCniArgv) != TCL_OK)
      {
         provCount = 0;
         pCniArgv  = NULL;
      }
   }

   cni = 0;
   provIdx = 0;
   do
   {
      if (startUiCni != 0)
      {  // first use the CNI given on the command line
         cni = startUiCni;
      }
      else if (provIdx < provCount)
      {  // then try all providers given in the list of previously loaded databases
         if (Tcl_GetInt(interp, pCniArgv[provIdx], &cni) != TCL_OK)
         {
            debug2("OpenInitialDb: Tcl var prov_selection, elem #%d: '%s' invalid CNI", provIdx, pCniArgv[provIdx]);
            provIdx += 1;
            continue;
         }
         provIdx += 1;
      }
      else
      {  // if everything above failed, open any database found in the dbdir or create an empty one
         // (the CNI 0 has a special handling in the context open function)
         cni = 0;
      }

      if (cni == 0x00ff)
      {  // special case: merged db
         MERGE_ATTRIB_MATRIX max;
         uint pProvCniTab[MAX_MERGED_DB_COUNT];
         uint netwopCniTab[MAX_NETWOP_COUNT];
         uint provCount, netwopCount;

         if (ProvMerge_ParseConfigString(interp, &provCount, pProvCniTab, &max[0]) == TCL_OK)
         {
            ProvMerge_ParseNetwopList(interp, &netwopCount, netwopCniTab);
            pUiDbContext = EpgContextMerge(provCount, pProvCniTab, max, netwopCount, netwopCniTab);
            if (pUiDbContext != NULL)
               ProvMerge_UpdateProvSelectionList(provCount, pProvCniTab);
         }
      }
      else if (cni != 0)
      {  // regular database
         pUiDbContext = EpgContextCtl_Open(cni, CTX_FAIL_RET_NULL, CTX_RELOAD_ERR_REQ);
      }
      else
      {  // no CNI left in list -> load any db or use dummy
         pUiDbContext = EpgContextCtl_OpenAny(CTX_RELOAD_ERR_REQ);
         cni = EpgDbContextGetCni(pUiDbContext);
      }

      // clear the cmd-line CNI since it's already been used
      startUiCni = 0;
   }
   while (pUiDbContext == NULL);

   if (pCniArgv != NULL)
      Tcl_Free((char *) pCniArgv);

   // update rc/ini file with new CNI order
   if ((cni != 0) && (EpgDbContextIsMerged(pUiDbContext) == FALSE))
   {
      sprintf(comm, "UpdateProvSelection 0x%04X\n", cni);
      eval_check(interp, comm);
   }
   // note: the usual provider change events are not triggered here because
   // at the time this function is called the other modules are not yet initialized.
}

// ----------------------------------------------------------------------------
// Fetch the acquisition mode and CNI list from global Tcl variables
//
static EPGACQ_MODE GetAcquisitionModeParams( uint * pCniCount, uint * cniTab )
{
   char **pCniArgv;
   const char * pTmpStr;
   uint cniIdx, dbIdx;
   EPGACQ_MODE mode;

   pTmpStr = Tcl_GetVar(interp, "acq_mode", TCL_GLOBAL_ONLY);
   if (pTmpStr != NULL)
   {
      if      (strcmp(pTmpStr, "passive") == 0)      mode = ACQMODE_PASSIVE;
      else if (strcmp(pTmpStr, "external") == 0)     mode = ACQMODE_EXTERNAL;
      else if (strcmp(pTmpStr, "follow-ui") == 0)    mode = ACQMODE_FOLLOW_UI;
      else if (strcmp(pTmpStr, "cyclic_2") == 0)     mode = ACQMODE_CYCLIC_2;
      else if (strcmp(pTmpStr, "cyclic_012") == 0)   mode = ACQMODE_CYCLIC_012;
      else if (strcmp(pTmpStr, "cyclic_02") == 0)    mode = ACQMODE_CYCLIC_02;
      else if (strcmp(pTmpStr, "cyclic_12") == 0)    mode = ACQMODE_CYCLIC_12;
      else                                           mode = ACQMODE_COUNT;  //illegal value

      switch (mode)
      {
         case ACQMODE_PASSIVE:
         case ACQMODE_EXTERNAL:
         case ACQMODE_FOLLOW_UI:
            *pCniCount = 0;
            break;

         case ACQMODE_CYCLIC_2:
         case ACQMODE_CYCLIC_012:
         case ACQMODE_CYCLIC_02:
         case ACQMODE_CYCLIC_12:
            // cyclic mode -> expect list of CNIs in format: {0x0d94, ...}
            pTmpStr = Tcl_GetVar(interp, "acq_mode_cnis", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
            if (pTmpStr != NULL)
            {
               if (Tcl_SplitList(interp, pTmpStr, pCniCount, &pCniArgv) == TCL_OK)
               {
                  if (*pCniCount > MAX_MERGED_DB_COUNT)
                  {
                     debug2("GetAcquisitionModeParams: too many CNIs: %d - limiting to %d", *pCniCount, MAX_MERGED_DB_COUNT);
                     *pCniCount = MAX_MERGED_DB_COUNT;
                  }

                  dbIdx = 0;
                  for (cniIdx=0; cniIdx < *pCniCount; cniIdx++)
                  {
                     if ( (Tcl_GetInt(interp, pCniArgv[cniIdx], cniTab + dbIdx) == TCL_OK) &&
                          (cniTab[dbIdx] != 0) &&
                          (cniTab[dbIdx] != 0x00FF) )
                     {
                        dbIdx++;
                     }
                     else
                        debug2("GetAcquisitionModeParams: arg #%d in '%s' is not a valid CNI", cniIdx, pCniArgv[cniIdx]);
                  }
                  Tcl_Free((char *) pCniArgv);

                  if (dbIdx == 0)
                  {
                     debug1("GetAcquisitionModeParams: mode %d: no valid CNIs found - illegal mode setting", mode);
                     mode = ACQMODE_COUNT;
                  }
               }
               else
               {
                  debug2("GetAcquisitionModeParams: mode %d: Tcl cni list is not a valid Tcl list: %s", mode, pTmpStr);
                  mode = ACQMODE_COUNT;
               }
            }
            else
            {
               debug1("GetAcquisitionModeParams: mode %d: Tcl cni list variable not found", mode);
               mode = ACQMODE_COUNT;
            }
            break;

         default:
            debug1("GetAcquisitionModeParams: illegal mode string: %s", pTmpStr);
            break;
      }
   }
   else
   {
      debug0("GetAcquisitionModeParams: Tcl var acq_mode not defined - using default mode");
      mode = ACQMODE_COUNT;
   }

   return mode;
}

// ----------------------------------------------------------------------------
// If the UI db is empty, move the UI CNI to the front of the list
// - only if a manual acq mode is configured
//
static void SortAcqCniList( uint cniCount, uint * cniTab )
{
   FILTER_CONTEXT  * pfc;
   const PI_BLOCK  * pPiBlock;
   uint uiCni;
   uint idx, mergeIdx;
   uint mergeCniCount, mergeCniTab[MAX_MERGED_DB_COUNT];
   sint startIdx;

   uiCni = EpgDbContextGetCni(pUiDbContext);
   if ((uiCni != 0) && (cniCount > 1))
   {  // provider present -> check for PI

      EpgDbLockDatabase(pUiDbContext, TRUE);
      // create a filter context with only an expire time filter set
      pfc = EpgDbFilterCreateContext();
      EpgDbFilterSetExpireTime(pfc, time(NULL));
      EpgDbFilterEnable(pfc, FILTER_EXPIRE_TIME);

      // check if there are any non-expired PI in the database
      pPiBlock = EpgDbSearchFirstPi(pUiDbContext, pfc);
      if (pPiBlock == NULL)
      {  // no PI in database
         if (EpgDbContextIsMerged(pUiDbContext) == FALSE)
         {
            for (startIdx=0; startIdx < cniCount; startIdx++)
               if (cniTab[startIdx] == uiCni)
                  break;
         }
         else
         {  // Merged database
            startIdx = -1;
            if (EpgContextMergeGetCnis(pUiDbContext, &mergeCniCount, mergeCniTab))
            {
               // check if the current acq CNI is one of the merged
               for (mergeIdx=0; mergeIdx < mergeCniCount; mergeIdx++)
                  if (cniTab[0] == mergeCniTab[mergeIdx])
                     break;
               if (mergeIdx >= mergeCniCount)
               {  // current CNI is not part of the merge -> search if any other is
                  for (mergeIdx=0; (mergeIdx < mergeCniCount) && (startIdx == -1); mergeIdx++)
                     for (idx=0; (idx < cniCount) && (startIdx == -1); idx++)
                        if (cniTab[idx] == mergeCniTab[mergeIdx])
                           startIdx = idx;
               }
            }
         }

         if ((startIdx > 0) && (startIdx < cniCount))
         {  // move the UI CNI to the front of the list
            dprintf2("SortAcqCniList: moving provider 0x%04X from idx %d to 0\n", cni, startIdx);
            uiCni = cniTab[startIdx];
            for (idx=1; idx <= startIdx; idx++)
               cniTab[idx] = cniTab[idx - 1];
            cniTab[0] = uiCni;
         }
      }
      EpgDbFilterDestroyContext(pfc);
      EpgDbLockDatabase(pUiDbContext, FALSE);
   }
}

// ----------------------------------------------------------------------------
// Pass acq mode and CNI list to acquisition control
// - called after the user leaves acq mode dialog or client/server dialog
//   with "Ok", plus whenever the browser database provider is changed
//   -> acq ctl must check if parameters have changed before resetting acq
// - this function is not used by the acquisition daemon; here on client-side
//   network mode equals follow-ui because only content that's currently used
//   in the display should be forwarded from the server
// - if no valid config is found, the default mode is used: Follow-UI
//
void SetAcquisitionMode( void )
{
   uint cniCount, cniTab[MAX_MERGED_DB_COUNT];
   EPGACQ_MODE mode;

   mode = GetAcquisitionModeParams(&cniCount, cniTab);
   if (mode >= ACQMODE_COUNT)
   {  // unrecognized mode or other param error -> fall back to default mode
      mode = ACQMODE_FOLLOW_UI;
      cniCount = 0;
   }

   // check if network client mode is enabled -> if yes, override acq mode
   if (IsRemoteAcqEnabled(interp))
      mode = ACQMODE_NETWORK;

   if ((mode == ACQMODE_FOLLOW_UI) || (mode == ACQMODE_NETWORK))
   {
      if ( EpgDbContextIsMerged(pUiDbContext) )
      {
         if (EpgContextMergeGetCnis(pUiDbContext, &cniCount, cniTab) == FALSE)
         {  // error
            cniCount = 0;
         }
      }
      else
      {
         cniTab[0] = EpgDbContextGetCni(pUiDbContext);
         cniCount  = ((cniTab[0] != 0) ? 1 : 0);
      }
   }

   // move browser provider's CNI to the front if the db is empty
   SortAcqCniList(cniCount, cniTab);

   // pass the params to the acquisition control module
   EpgAcqCtl_SelectMode(mode, cniCount, cniTab);
}

#ifdef USE_DAEMON
// ----------------------------------------------------------------------------
// For Daemon mode only: Pass acq mode and CNI list to acquisition control
// - in Follow-UI mode the browser CNI must be determined here since
//   no db is opened for the browser
//
bool SetDaemonAcquisitionMode( uint cmdLineCni, bool forcePassive )
{
   EPGACQ_MODE   mode;
   const char  * pTmpStr;
   const uint  * pProvList;
   char       ** pCniArgv;
   uint cniCount, cniTab[MAX_MERGED_DB_COUNT];
   bool result = FALSE;

   if (cmdLineCni != 0)
   {  // CNI given on command line with -provider option
      assert(forcePassive == FALSE);  // checked by argv parser
      mode      = ACQMODE_CYCLIC_2;
      cniCount  = 1;
      cniTab[0] = cmdLineCni;
   }
   else if (forcePassive)
   {  // -acqpassive given on command line
      mode      = ACQMODE_PASSIVE;
      cniCount  = 0;
   }
   else
   {  // else use acq mode from rc/ini file
      mode = GetAcquisitionModeParams(&cniCount, cniTab);
      if (mode == ACQMODE_FOLLOW_UI)
      {
         // fetch CNI from list of last used providers
         pTmpStr = Tcl_GetVar(interp, "prov_selection", TCL_GLOBAL_ONLY);
         cniCount = 0;
         if (pTmpStr != NULL)
         {
            if (Tcl_SplitList(interp, pTmpStr, &cniCount, &pCniArgv) == TCL_OK)
            {
               if ( (cniCount > 0) &&
                    (Tcl_GetInt(interp, pCniArgv[0], cniTab) == TCL_OK) )
               {
                  cniCount = 1;
               }
               Tcl_Free((char *) pCniArgv);
            }
         }
         if (cniCount == 0)
         {  // last used provider not known (e.g. right after the initial scan) -> use all providers
            pProvList = EpgContextCtl_GetProvList(&cniCount);
            if (pProvList != NULL)
            {
               if (cniCount > MAX_MERGED_DB_COUNT)
                  cniCount = MAX_MERGED_DB_COUNT;
               memcpy(cniTab, pProvList, cniCount * sizeof(uint));
               xfree((void *) pProvList);
            }
            else
            {  // no providers known yet -> set count to zero, acq starts in passive mode
               cniCount = 0;
            }
         }
      }
      else if (mode >= ACQMODE_COUNT)
         EpgNetIo_Logger(LOG_ERR, -1, "acqmode parameters error", NULL);
   }

   if (mode < ACQMODE_COUNT)
   {
      if ((mode == ACQMODE_FOLLOW_UI) && (cniTab[0] == 0x00ff))
      {  // Merged database -> retrieve CNI list
         MERGE_ATTRIB_MATRIX max;
         if (ProvMerge_ParseConfigString(interp, &cniCount, cniTab, &max[0]) != TCL_OK)
         {
            EpgNetIo_Logger(LOG_ERR, -1, "no network list found for merged database", NULL);
            mode = ACQMODE_COUNT;
         }
      }

      // pass the params to the acquisition control module
      if (mode < ACQMODE_COUNT)
      {
         result = EpgAcqCtl_SelectMode(mode, cniCount, cniTab);
      }
      else
         result = FALSE;
   }

   return result;
}
#endif  // USE_DAEMON

// ----------------------------------------------------------------------------
// Select acquisition mode and choose provider for acq
// - called after change of acq mode via the popup menu
// - parameters are taken from global Tcl variables, because the same
//   update is required at startup
//
static int MenuCmd_UpdateAcquisitionMode(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_UpdateAcquisitionMode";
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      SetAcquisitionMode();

      // update help message in listbox if database is empty
      UiControl_CheckDbState();

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Fetch the list of provider frequencies from all available databases
// - called when the EPG scan dialog is opened
// - returns a flat list of CNIs and frequencies;
//   the caller should merge the result with the Tcl list prov_freqs
// - required only in case the user deleted the rc file and the prov_freqs list
//   is gone; else all frequencies should alo be available in the rc/ini file
//
static int MenuCmd_LoadProvFreqsFromDbs(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_LoadProvFreqsFromDbs";
   ulong * pDbFreqTab;
   uint  * pDbCniTab;
   uint    dbCount;
   uint    idx;
   uchar   strbuf[30];
   int     result;

   if (argc != 1)
   {  // no arguments expected
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pDbCniTab  = NULL;
      pDbFreqTab = NULL;
      // extract frequencies from databases, even if incompatible version
      dbCount = EpgContextCtl_GetFreqList(&pDbCniTab, &pDbFreqTab);

      // generate Tcl list from the arrays
      for (idx = 0; idx < dbCount; idx++)
      {
         sprintf(strbuf, "0x%04X", pDbCniTab[idx]);
         Tcl_AppendElement(interp, strbuf);

         sprintf(strbuf, "%ld", pDbFreqTab[idx]);
         Tcl_AppendElement(interp, strbuf);
      }

      // free the intermediate arrays
      if (pDbCniTab != NULL)
         xfree(pDbCniTab);
      if (pDbFreqTab != NULL)
         xfree(pDbFreqTab);

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Fetch the frequency for the given provider from the prov_freqs Tcl variable
// - used by acquisition, e.g. if the freq. cannot be read from the database
// - returns 0 if no frquency is available for the given provider
//
ulong GetProvFreqForCni( uint provCni )
{
   const char *pTmpStr;
   char **pCniFreqArgv;
   int   cni, freq;
   int   idx, freqCount;
   ulong provFreq;

   provFreq = 0;

   pTmpStr = Tcl_GetVar(interp, "prov_freqs", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      // break the Tcl list up into an array of one string per element
      if (Tcl_SplitList(interp, pTmpStr, &freqCount, &pCniFreqArgv) == TCL_OK)
      {
         // retrieve CNI and frequencies from the list pairwise
         for (idx = 0; idx + 1 < freqCount; idx += 2)
         {
            // convert the strings to binary values
            if ( (Tcl_GetInt(interp, pCniFreqArgv[idx], &cni) == TCL_OK) &&
                 (Tcl_GetInt(interp, pCniFreqArgv[idx + 1], &freq) == TCL_OK) )
            {
               if (cni == provCni)
               {  // found the requested provider
                  provFreq = freq;
                  break;
               }
            }
            else
               debug2("GetProvFreqForCni: failed to parse CNI='%s' or freq='%s'", pCniFreqArgv[idx], pCniFreqArgv[idx + 1]);
         }

         Tcl_Free((char *) pCniFreqArgv);
      }
      else
         debug1("GetProvFreqForCni: parse error in Tcl list prov_freqs: %s", pTmpStr);
   }
   else
      debug0("GetProvFreqForCni: Tcl variable prov_freqs not defined");

   return provFreq;
}

// ----------------------------------------------------------------------------
// Fetch the list of known provider frequencies and CNIs
// - the list in Tcl variable 'prov_freqs' contains pairs of CNI and frequency
// - returns separate arrays for frequencies and CNIs; the length of both lists
//   is identical, hence there is only one count result
// - returns if no frequencies are available or the list was not parsable
//
static uint GetProvFreqTab( ulong ** pFreqTab, uint ** pCniTab )
{
   const char *pTmpStr;
   char **pCniFreqArgv;
   ulong *freqTab;
   uint  *cniTab;
   uint  freqIdx;
   int   cni, freq;
   int   idx, freqCount;

   // initialize result values
   freqIdx = 0;
   freqTab = NULL;
   cniTab  = NULL;

   pTmpStr = Tcl_GetVar(interp, "prov_freqs", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      // break the Tcl list up into an array of one string per element
      if (Tcl_SplitList(interp, pTmpStr, &freqCount, &pCniFreqArgv) == TCL_OK)
      {
         if (freqCount > 0)
         {
            freqTab = xmalloc((freqCount / 2) * sizeof(ulong));
            cniTab  = xmalloc((freqCount / 2) * sizeof(uint));

            // retrieve CNI and frequency from the list pairwise
            for (idx = 0; idx + 1 < freqCount; idx += 2)
            {
               if ( (Tcl_GetInt(interp, pCniFreqArgv[idx], &cni) == TCL_OK) &&
                    (Tcl_GetInt(interp, pCniFreqArgv[idx + 1], &freq) == TCL_OK) )
               {
                  freqTab[freqIdx] = (ulong) freq;
                  cniTab[freqIdx] = (uint) cni;
                  freqIdx += 1;
               }
               else
                  debug2("GetProvFreqTab: failed to parse CNI='%s' or freq='%s'", pCniFreqArgv[idx], pCniFreqArgv[idx + 1]);
            }

            if (freqIdx == 0)
            {  // discard the allocated list is no valid items were found
               xfree(freqTab);
               xfree(cniTab);
               freqTab = NULL;
               cniTab  = NULL;
            }
         }
         Tcl_Free((char *) pCniFreqArgv);
      }
      else
         debug1("GetProvFreqTab: parse error in Tcl list prov_freqs: %s", pTmpStr);
   }
   else
      debug0("GetProvFreqTab: Tcl variable prov_freqs not defined");

   *pFreqTab = freqTab;
   *pCniTab  = cniTab;

   return freqIdx;
}

// ----------------------------------------------------------------------------
// Append a line to the EPG scan messages
//
static void MenuCmd_AddEpgScanMsg( const char * pMsg )
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
   const char * const pUsage = "Usage: C_StartEpgScan <input source> <slow=0/1> <refresh=0/1> <xawtv=0/1>";
   EPGSCAN_START_RESULT scanResult;
   ulong *freqTab;
   uint  *cniTab;
   int freqCount;
   int inputSource, isOptionSlow, isOptionRefresh, isOptionXawtv;
   uint rescheduleMs;
   int result;

   if (argc != 5)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if ( (Tcl_GetInt(interp, argv[1], &inputSource) == TCL_OK) &&
           (Tcl_GetInt(interp, argv[2], &isOptionSlow) == TCL_OK) &&
           (Tcl_GetInt(interp, argv[3], &isOptionRefresh) == TCL_OK) &&
           (Tcl_GetInt(interp, argv[4], &isOptionXawtv) == TCL_OK) )
      {
         freqCount = 0;
         freqTab = NULL;
         cniTab  = NULL;
         if (isOptionRefresh)
         {  // in this mode only previously stored provider frequencies are visited
            // the returned lists are freed by the EPG scan module
            freqCount = GetProvFreqTab(&freqTab, &cniTab);
            if ( (freqCount == 0) || (freqTab == NULL) || (cniTab == NULL) )
            {  // it's an error if the provider frequency list is empty
               // the caller has to check this condition beforehand
               eval_check(interp, ".epgscan.all.fmsg.msg insert end {Frequency list is empty or invalid - abort\n}");
               return TCL_OK;
            }
         }
         else if (isOptionXawtv)
         {  // in this mode only channels which are defined in the .xawtv file are visited
            if (
                 #ifndef WIN32
                 (Xawtv_GetFreqTab(interp, &freqTab, &freqCount) == FALSE) ||
                 #else
                 (WintvCfg_GetFreqTab(interp, &freqTab, &freqCount) == FALSE) ||
                 #endif
                 (freqTab == NULL) || (freqCount == 0) )
            {  // message-box with explanation was already displayed
               return TCL_OK;
            }
         }

         // clear message window
         sprintf(comm, ".epgscan.all.fmsg.msg delete 1.0 end\n");
         eval_check(interp, comm);

         scanResult = EpgScan_Start(inputSource, isOptionSlow, isOptionXawtv, isOptionRefresh,
                                    cniTab, freqTab, freqCount,
                                    &rescheduleMs, &MenuCmd_AddEpgScanMsg);
         switch (scanResult)
         {
            case EPGSCAN_ACCESS_DEV_VBI:
               #ifndef WIN32
               sprintf(comm, "tk_messageBox -type ok -icon error -parent .epgscan -message \""
                             "Failed to open the VBI (i.e. teletext) device "
                             "(/dev/vbi[expr {[info exists hwcfg] ? [lindex $hwcfg 4] : 0}]). "
                             "Close all other video or teletext related applications and try again."
                             "\"");
               eval_check(interp, comm);
               break;
               #endif
               // Win32: fall-through!

            case EPGSCAN_ACCESS_DEV_VIDEO:
               sprintf(comm, "tk_messageBox -type ok -icon error -parent .epgscan -message \""
                             "Failed to open the video device"
                             #ifndef WIN32
                             " (/dev/video[expr {[info exists hwcfg] ? [lindex $hwcfg 4] : 0}]). "
                             "Close all other video or teletext related applications and try again"
                             #endif
                             ".\"");
               eval_check(interp, comm);
               break;

            case EPGSCAN_NO_TUNER:
               sprintf(comm, "tk_messageBox -type ok -icon error -parent .epgscan "
                             "-message {The input source you have set in the 'TV card input' configuration "
                                       "is not a TV tuner device. Either change that setting or exit the "
                                       "EPG scan and tune the providers you're interested in manually.}\n");
               eval_check(interp, comm);
               break;

            case EPGSCAN_INTERNAL:
               // parameter inconsistancy - should not be reached
               eval_check(interp, ".epgscan.all.fmsg.msg insert end {internal error - abort\n}");
               break;

            case EPGSCAN_OK:
               // grab focus, disable all command buttons except "Abort"
               sprintf(comm, "EpgScanButtonControl start\n");
               eval_check(interp, comm);
               // update PI listbox help message, if there's no db in the browser yet
               UiControl_CheckDbState();
               // update main window status line
               UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
               // Install the event handler
               Tcl_CreateTimerHandler(rescheduleMs, MenuCmd_EpgScanHandler, NULL);
               break;

            default:
               SHOULD_NOT_BE_REACHED;
               break;
         }
         Tcl_ResetResult(interp);
         result = TCL_OK;
      }
      else
         result = TCL_ERROR;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Stop EPG scan
// - called from UI after Abort button or when popup is destroyed
// - also called from scan handler when scan has finished
//
static int MenuCmd_StopEpgScan(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   if (argc > 0)
   {  // called from UI -> stop scan handler
      EpgScan_Stop();
   }

   // release grab, re-enable command buttons
   sprintf(comm, "EpgScanButtonControl stop\n");
   eval_check(interp, comm);

   UiControl_CheckDbState();

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Modify speed of EPG scan while scan is running
// - ignored if called otherwise (speed is also passed in start command)
//
static int MenuCmd_SetEpgScanSpeed(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_SetEpgScanSpeed <slow=0/1>";
   int isOptionSlow;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetInt(interp, argv[1], &isOptionSlow) )
   {  // the parameter is not an integer
      result = TCL_ERROR;
   }
   else
   {
      EpgScan_SetSpeed(isOptionSlow);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Backgroundhandler for the EPG scan
//
static void MenuCmd_EpgScanHandler( ClientData clientData )
{
   uint rescheduleMs;

   // check if the scan was aborted (normally it stops itself inside the handler)
   if (EpgScan_IsActive())
   {
      rescheduleMs = EpgScan_EvHandler();

      if (rescheduleMs > 0)
      {
         // update the progress bar
         sprintf(comm, ".epgscan.all.baro.bari configure -width %d\n", (int)(EpgScan_GetProgressPercentage() * 140.0));
         eval_check(interp, comm);

         // Install the event handler
         Tcl_CreateTimerHandler(rescheduleMs, MenuCmd_EpgScanHandler, NULL);
      }
      else
      {
         MenuCmd_StopEpgScan(NULL, interp, 0, NULL);

         // ring the bell when the scan has finished
         eval_check(interp, "bell");
         eval_check(interp, ".epgscan.all.baro.bari configure -width 140\n");
      }
   }
}

// ----------------------------------------------------------------------------
// Get list of all tuner types
// - on Linux the tuner is already configured in the bttv driver
//
static int MenuCmd_GetTunerList(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   #ifdef WIN32
   const char * pName;
   uint idx;

   idx = 0;
   while (1)
   {
      pName = BtDriver_GetTunerName(idx);
      if (pName != NULL)
         Tcl_AppendElement(interp, (char *) pName);
      else
         break;
      idx += 1;
   }
   #endif
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Get list of all TV card names
//
static int MenuCmd_GetTvCardList(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_HwCfgGetTvCardList";
   const char * pName;
   uint idx;
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      #ifdef __NetBSD__
      // On NetBSD BtDriver_GetCardName fetches its data from a struct which is filled here
      BtDriver_ScanDevices(TRUE);
      #endif

      idx = 0;
      do
      {
         pName = BtDriver_GetCardName(idx);
         if (pName != NULL)
            Tcl_AppendElement(interp, (char *) pName);

         idx += 1;
      }
      while (pName != NULL);

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Get list of all input types
//
static int MenuCmd_GetInputList(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetInputList <card index>";
   const char * pName;
   int  cardIndex;
   uint idx;
   int  result;

   if ( (argc != 2) || (Tcl_GetInt(interp, argv[1], &cardIndex) != TCL_OK) )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      idx = 0;
      while (1)
      {
         pName = BtDriver_GetInputName(cardIndex, idx);

         if (pName != NULL)
            Tcl_AppendElement(interp, (char *) pName);
         else
            break;
         idx += 1;
      }

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Set the hardware config params
// - called at startup or after user configuration via the GUI
// - the card index can also be set via command line and is passed here
//   from main; a value of -1 means don't care
//
int SetHardwareConfig( Tcl_Interp *interp, int newCardIndex )
{
   char **pParamsArgv;
   char * pTmpStr;
   int idxCount, input, tuner, pll, prio, cardidx, ftable;
   #ifdef WIN32
   int dsdrvLog;
   #endif
   int result;

   pTmpStr = Tcl_GetVar(interp, "hwcfg", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr == NULL)
      pTmpStr = Tcl_GetVar(interp, "hwcfg_default", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      result = Tcl_SplitList(interp, pTmpStr, &idxCount, &pParamsArgv);
      if (result == TCL_OK)
      {
         if (idxCount == 6)
         {
            if ( (Tcl_GetInt(interp, pParamsArgv[0], &input) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[1], &tuner) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[2], &pll) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[3], &prio) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[4], &cardidx) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[5], &ftable) == TCL_OK) )
            {
               if ((newCardIndex >= 0) && (newCardIndex != cardidx))
               {
                  cardidx = newCardIndex;
                  sprintf(comm, "HardwareConfigUpdateCardIdx %d\n", cardidx);
                  eval_check(interp, comm);
               }

               #ifdef WIN32
               pTmpStr = Tcl_GetVar(interp, "hwcf_dsdrv_log", TCL_GLOBAL_ONLY);
               if (pTmpStr != NULL)
               {
                  if (Tcl_GetInt(interp, pTmpStr, &dsdrvLog) == TCL_OK)
                     HwDrv_SetLogging(dsdrvLog);
                  else
                     debug1("Set-HardwareConfig: could not parse Tcl var dsdrvLog='%s'", pTmpStr);
               }
               #endif

               // pass the frequency table selection to the TV-channel module
               TvChannels_SelectFreqTable(ftable);
               // pass the hardware config params to the driver
               if (BtDriver_Configure(cardidx, tuner, pll, prio))
               {
                  // pass the input selection to acquisition control
                  EpgAcqCtl_SetInputSource(input);
               }
               else
                  EpgAcqCtl_Stop();
            }
            else
               result = TCL_ERROR;
         }
         else
         {
            Tcl_SetResult(interp, "SetHardwareConfig: must get 6 params", TCL_STATIC);
            result = TCL_ERROR;
         }
      }
   }
   else
      result = TCL_ERROR;

   return result;
}

// ----------------------------------------------------------------------------
// Apply the user-configured hardware configuration
// - called when hwcfg popup was closed
//
static int MenuCmd_UpdateHardwareConfig(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_UpdateHardwareConfig";
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = SetHardwareConfig(interp, -1);

      // update help message in listbox if database is empty
      UiControl_CheckDbState();
   }
   return result;
}

// ----------------------------------------------------------------------------
// Return TRUE if the client is in network acq mode
//
bool IsRemoteAcqEnabled( Tcl_Interp * interp )
{
   char * pTmpStr;
   int    is_enabled;

   pTmpStr = Tcl_GetVar(interp, "netacq_enable", TCL_GLOBAL_ONLY);
   if (pTmpStr != NULL)
   {
      if (Tcl_GetBoolean(interp, pTmpStr, &is_enabled) != TCL_OK)
      {
         debug1("IsRemoteAcq-Enabled: parse error in '%s'", pTmpStr);
         is_enabled = FALSE;
      }
   }
   else
   {
      debug0("IsRemoteAcq-Enabled: Tcl var menuStatusDaemon undefined");
      is_enabled = FALSE;
   }

   return (bool) is_enabled;
}

// ----------------------------------------------------------------------------
// Read network connection and server parameters from Tcl variables
//
void SetNetAcqParams( Tcl_Interp * interp, bool isServer )
{
#ifdef USE_DAEMON
   char **cfArgv;
   const char * pTmpStr;
   int  cfArgc, cf_idx;
   char *pHostName, *pPort, *pIpStr, *pLogfileName;
   int  enable, do_tcp_ip, max_conn, fileloglev, sysloglev, remote_ctl;

   // initialize the config variables
   enable = max_conn = fileloglev = sysloglev = 0;
   pHostName = pPort = pIpStr = pLogfileName = NULL;

   pTmpStr = Tcl_GetVar(interp, "netacqcf", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      // parse configuration list: pairs of config keyword (string) and parameter (string, int or bool)
      if (Tcl_SplitList(interp, pTmpStr, &cfArgc, &cfArgv) == TCL_OK)
      {
         ifdebug1(((cfArgc & 1) != 0), "SetNetAcqParams: warning: uneven number of params: %d", cfArgc);
         for (cf_idx = 0; cf_idx < cfArgc; cf_idx += 2)
         {
            if (strcmp(cfArgv[cf_idx], "do_tcp_ip") == 0)
            {
               if (Tcl_GetBoolean(interp, cfArgv[cf_idx + 1], &do_tcp_ip) != TCL_OK)
                  debug2("SetNetAcqParams: keyword '%s' illegally assigned: '%s'", cfArgv[cf_idx], cfArgv[cf_idx + 1]);
            }
            else if (strcmp(cfArgv[cf_idx], "host") == 0)
            {
               if ((cfArgv[cf_idx + 1] != NULL) && (*cfArgv[cf_idx + 1] != 0))
                  pHostName = cfArgv[cf_idx + 1];
            }
            else if (strcmp(cfArgv[cf_idx], "port") == 0)
            {
               if ((cfArgv[cf_idx + 1] != NULL) && (*cfArgv[cf_idx + 1] != 0))
                  pPort = cfArgv[cf_idx + 1];
            }
            else if (strcmp(cfArgv[cf_idx], "ip") == 0)
            {
               if ((cfArgv[cf_idx + 1] != NULL) && (*cfArgv[cf_idx + 1] != 0))
                  pIpStr = cfArgv[cf_idx + 1];
            }
            else if (strcmp(cfArgv[cf_idx], "max_conn") == 0)
            {
               if (Tcl_GetInt(interp, cfArgv[cf_idx + 1], &max_conn) != TCL_OK)
                  debug2("SetNetAcqParams: keyword '%s' illegally assigned: '%s'", cfArgv[cf_idx], cfArgv[cf_idx + 1]);
            }
            else if (strcmp(cfArgv[cf_idx], "sysloglev") == 0)
            {
               if (Tcl_GetInt(interp, cfArgv[cf_idx + 1], &sysloglev) != TCL_OK)
                  debug2("SetNetAcqParams: keyword '%s' illegally assigned: '%s'", cfArgv[cf_idx], cfArgv[cf_idx + 1]);
            }
            else if (strcmp(cfArgv[cf_idx], "fileloglev") == 0)
            {
               if (Tcl_GetInt(interp, cfArgv[cf_idx + 1], &fileloglev) != TCL_OK)
                  debug2("SetNetAcqParams: keyword '%s' illegally assigned: '%s'", cfArgv[cf_idx], cfArgv[cf_idx + 1]);
            }
            else if (strcmp(cfArgv[cf_idx], "logname") == 0)
            {
               if ((cfArgv[cf_idx + 1] != NULL) && (*cfArgv[cf_idx + 1] != 0))
                  pLogfileName = cfArgv[cf_idx + 1];
            }
            else if (strcmp(cfArgv[cf_idx], "remctl") == 0)
            {
               if (Tcl_GetInt(interp, cfArgv[cf_idx + 1], &remote_ctl) != TCL_OK)
                  debug2("SetNetAcqParams: keyword '%s' illegally assigned: '%s'", cfArgv[cf_idx], cfArgv[cf_idx + 1]);
            }
            else
               debug1("SetNetAcqParams: unknown keyword: '%s'", cfArgv[cf_idx]);
         }

         // apply the parameters: pass them to the client/server module
         if (isServer)
         {
            // XXX TODO: remote_ctl
            EpgAcqServer_SetMaxConn(max_conn);
            EpgAcqServer_SetAddress(do_tcp_ip, pIpStr, pPort);
            EpgNetIo_SetLogging(sysloglev, fileloglev, pLogfileName);
         }
         else
            EpgAcqClient_SetAddress(pHostName, pPort);

         Tcl_Free((char *) cfArgv);
      }
      else
         debug1("SetNetAcqParams: cfg not a list: %s", pTmpStr);
   }
#endif
}

// ----------------------------------------------------------------------------
// Apply the user-configured client/server configuration
// - called when client/server configuration dialog is closed
//
static int MenuCmd_UpdateNetAcqConfig(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_UpdateNetAcqConfig";
   int  result;

   if (argc != 1)
   {  // parameter count is invalid: none expected
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      SetNetAcqParams(interp, FALSE);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Return information about the system time zone
//
static int MenuCmd_GetTimeZone(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetTimeZone";
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      struct tm *tm;
      time_t now;
      sint lto;

      tzset();
      tm = localtime(&now);
      lto = EpgLtoGet(now);

      sprintf(comm, "%d %d {%s}", lto/60, tm->tm_isdst, (tm->tm_isdst ? tzname[1] : tzname[0]));
      Tcl_SetResult(interp, comm, TCL_VOLATILE);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Initialize the module
//
void MenuCmd_Init( bool isDemoMode )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_ToggleAcq", &cmdInfo) == 0)
   {  // Create callback functions
      Tcl_CreateCommand(interp, "C_ToggleAcq", MenuCmd_ToggleAcq, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ToggleDumpStream", MenuCmd_ToggleDumpStream, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_DumpDatabase", MenuCmd_DumpDatabase, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SetControlMenuStates", MenuCmd_SetControlMenuStates, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_ChangeProvider", MenuCmd_ChangeProvider, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_GetCniDescription", MenuCmd_GetCniDescription, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetProvServiceInfos", MenuCmd_GetProvServiceInfos, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetCurrentDatabaseCni", MenuCmd_GetCurrentDatabaseCni, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetProvCnisAndNames", MenuCmd_GetProvCnisAndNames, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetAiNetwopList", MenuCmd_GetAiNetwopList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ProvMerge_Start", ProvMerge_Start, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_UpdateAcquisitionMode", MenuCmd_UpdateAcquisitionMode, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_StartEpgScan", MenuCmd_StartEpgScan, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_StopEpgScan", MenuCmd_StopEpgScan, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SetEpgScanSpeed", MenuCmd_SetEpgScanSpeed, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_LoadProvFreqsFromDbs", MenuCmd_LoadProvFreqsFromDbs, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_HwCfgGetTvCardList", MenuCmd_GetTvCardList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_HwCfgGetInputList", MenuCmd_GetInputList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_HwCfgGetTunerList", MenuCmd_GetTunerList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_UpdateHardwareConfig", MenuCmd_UpdateHardwareConfig, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_UpdateNetAcqConfig", MenuCmd_UpdateNetAcqConfig, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_GetTimeZone", MenuCmd_GetTimeZone, (ClientData) NULL, NULL);

      if (isDemoMode)
      {  // create menu with warning labels and disable some menu commands
         sprintf(comm, "CreateDemoModePseudoMenu\n");
         eval_check(interp, comm);
      }
      #ifndef USE_DAEMON
      eval_check(interp, ".menubar.config entryconfigure \"Client/Server...\" -state disabled");
      #endif
   }
   else
      debug0("MenuCmd-Init: commands were already created");
}

