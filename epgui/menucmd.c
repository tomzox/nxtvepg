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
 *  $Id: menucmd.c,v 1.137 2020/06/17 19:34:20 tom Exp tom $
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
#include "epgdb/epgdbmerge.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgscan.h"
#include "epgctl/epgctxmerge.h"
#include "epgui/epgmain.h"
#include "epgui/epgsetup.h"
#include "epgui/pibox.h"
#include "epgui/piremind.h"
#include "epgui/pifilter.h"
#include "epgui/pdc_themes.h"
#include "epgui/menucmd.h"
#include "epgui/uictrl.h"
#include "epgui/rcfile.h"
#include "epgui/wintvcfg.h"
#include "epgui/cmdline.h"
#include "epgui/daemon.h"
#include "epgctl/epgctxctl.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/cni_tables.h"
#include "epgvbi/tvchan.h"
#include "epgvbi/btdrv.h"
#include "epgtcl/dlg_hwcfg.h"


static void MenuCmd_EpgScanHandler( ClientData clientData );

#define TRANSCODE_NON_NULL(S) TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, (((S)!=NULL)?(S):""), NULL)

// ----------------------------------------------------------------------------
// Set the states of the entries in Control menu
//
static int MenuCmd_SetControlMenuStates( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_SetControlMenuStates";
   EPGACQ_DESCR acqState;
   uint uiCni;
   int result;
#ifdef USE_DAEMON
   bool allow_opt;
#endif

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      EpgAcqCtl_DescribeAcqState(&acqState);
      uiCni = EpgDbContextGetCni(pUiDbContext);

      // enable "dump database" only if UI database has at least an AI block
      sprintf(comm, ".menubar.ctrl entryconfigure \"Export as text...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);
      sprintf(comm, ".menubar.ctrl entryconfigure \"Export as XMLTV...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);
      sprintf(comm, ".menubar.ctrl entryconfigure \"Export as HTML...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "timescales" only if UI database has AI block
      sprintf(comm, ".menubar.ctrl entryconfigure \"View coverage timescales...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

//TODO teletext?
//      // enable "acq timescales" only if acq running on different db than ui
//      sprintf(comm, ".menubar.ctrl entryconfigure \"Nextview acq. timescales...\" -state %s\n",
//                    ((acqState.nxtvDbCni != 0) ? "normal" : "disabled"));
//      eval_check(interp, comm);

#ifdef USE_TTX_GRABBER
      sprintf(comm, ".menubar.ctrl entryconfigure \"Teletext grabber statistics...\" -state %s\n",
                    (RcFile_Query()->ttx.ttx_enable ? "normal" : "disabled"));
      eval_check(interp, comm);
#endif

      // check button of "Enable Acq" if acq is running
      sprintf(comm, "set menuStatusStartAcq %d\n", (acqState.ttxGrabState != ACQDESCR_DISABLED));
      eval_check(interp, comm);

      #ifdef USE_DAEMON
      // check button of "Connect to daemon" if acq is running
      // - note: the button also reflects "off" when the acq is actually still enabled
      //   but in "retry" mode after a network error. This must be taken into account
      //   in the callbacks for the "start acq" entries.
      sprintf(comm, "set menuStatusDaemon %d",
                    ((acqState.ttxGrabState != ACQDESCR_DISABLED) && acqState.isNetAcq) );
      eval_check(interp, comm);

      // enable "Enable acquisition" only if not connected to a non-local daemon
      if (RcFile_Query()->ttx.ttx_enable)
      {
         allow_opt = ( (acqState.ttxGrabState == ACQDESCR_DISABLED) ||
                       (acqState.isNetAcq == FALSE) ||
                       (acqState.isNetAcq && EpgAcqClient_IsLocalServer()) );
         allow_opt &= EpgSetup_HasLocalTvCard();
      }
      else
      {
         allow_opt = FALSE;
      }
      sprintf(comm, ".menubar.ctrl entryconfigure \"Enable acquisition\" -state %s\n",
                    (allow_opt ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "connect to acq. daemon" only if acq not already running locally
      allow_opt = RcFile_Query()->ttx.ttx_enable &&
                  ((acqState.ttxGrabState == ACQDESCR_DISABLED) || acqState.isNetAcq);
      sprintf(comm, ".menubar.ctrl entryconfigure \"Connect to acq. daemon\" -state %s\n",
                    (allow_opt ? "normal" : "disabled"));
      eval_check(interp, comm);
      #else
      // no daemon -> always disable the option
      eval_check(interp, ".menubar.ctrl entryconfigure \"Connect to acq. daemon\" -state disabled\n");
      #ifdef WIN32
      eval_check(interp, ".systray      entryconfigure \"Connect to acq. daemon\" -state disabled\n");
      #endif
      #endif

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Start acquisition in auto-detected mode
// - used at start-up and when signal HUP is received
// - starts acq in the last manually chosen mode; if that fails the other is tried
// - auto-detection is enabled for daemons running on localhost only (i.e. acq from
//   the same TV card hardware) because anything else seems unplausible
//
// - Explanation of usage of $netacq_enable
//   +  netacq_enable contains the default mode for auto-start (i.e. next program start)
//   +  netacq_enable is updated when
//      1. the user manually changes the mode and acq doesn't fail right away,
//         (i.e. due to device busy)
//      2. when auto-start fails for the default and is successfully started for
//         the inverted mode.  In this case the update of netacq_enable is delayed
//         until an AI block is captured or the network server has been successfully
//         connected (i.e. compatible version etc.): See MenuCmd-AcqStatsUpdate()
//   + it should be noted that due to the "auto-inversion" acq may run with a different
//     mode than indicated by netacq_enable, hence that variable must not be used to
//     query the currently active mode; see also MenuCmd-IsNetAcqActive()
//
void AutoStartAcq( void )
{
   const RCFILE * pRc = RcFile_Query();

   if ( (pRc->acq.acq_start == ACQ_START_AUTO) &&
        (pRc->ttx.ttx_enable) &&
        (pRc->tvcard.drv_type != BTDRV_SOURCE_NONE) )
   {
#ifndef WIN32
      EpgSetup_AcquisitionMode(NETACQ_DEFAULT);
      if (EpgAcqCtl_Start() == FALSE)
      {
#ifdef USE_DAEMON
         if (EpgAcqClient_IsLocalServer())
         {
            // invert the network acq mode and try again
            EpgSetup_AcquisitionMode(NETACQ_INVERT);

            EpgAcqCtl_Start();
         }
#endif  // USE_DAEMON
      }
#else  // below: WIN32

#ifdef USE_DAEMON
      if (EpgAcqClient_IsLocalServer())
      {
         if (Daemon_CheckIfRunning() == FALSE)
            EpgSetup_AcquisitionMode(NETACQ_NO);
         else
            EpgSetup_AcquisitionMode(NETACQ_YES);
      }
      else
#endif
         EpgSetup_AcquisitionMode(NETACQ_DEFAULT);

      EpgAcqCtl_Start();
#endif
   }
}

// ----------------------------------------------------------------------------
// Notification of successful acquisition start
// - after auto-start has failed for the default mode, and acq restarted with the
//   inverted mode, this function updates netacq_enable with the current mode
//
void MenuCmd_AcqStatsUpdate( void )
{
#ifdef USE_DAEMON
   EPGACQ_DESCR    acqState;
   EPGDBSRV_DESCR  netState;
   bool            doNetAcq;

   // get the default acq mode setting from Tcl
   doNetAcq = IsRemoteAcqDefault();

   // get the current acq mode
   EpgAcqCtl_DescribeAcqState(&acqState);

   // check if acq is running & mode differs from default mode
   if ( (doNetAcq == FALSE) && (acqState.isNetAcq) &&
        EpgAcqClient_DescribeNetState(&netState) &&
        (netState.state >= NETDESCR_LOADING) )
   {
      // default is still local, but acq is running remote -> update
      dprintf0("MenuCmd-AcqStatsUpdate: setting default netacq mode to 1\n");

      RcFile_SetNetAcqEnable(TRUE);
      UpdateRcFile(TRUE);
   }
   else if ( (doNetAcq) && (acqState.isNetAcq == FALSE) &&
             (acqState.ttxGrabState >= ACQDESCR_STARTING) )
   {
      // default is still remote, but acq is running locally -> update
      dprintf0("MenuCmd-AcqStatsUpdate: setting default netacq mode to 0\n");

      RcFile_SetNetAcqEnable(FALSE);
      UpdateRcFile(TRUE);
   }
#endif
}

// ----------------------------------------------------------------------------
// Start local acquisition
//
static void MenuCmd_StartLocalAcq( Tcl_Interp * interp )
{
   #ifndef WIN32
   const char * pErrStr;
   #endif
   bool wasNetAcq;

   // Warn user if TV card is not configured
   if ( !EpgSetup_CheckTvCardConfig() && MenuCmd_PopupTvCardSetup() )
   {
      // User quit popup via "cancel" button
      return;
   }

   // save previous acq mode to detect mode changes
   wasNetAcq = IsRemoteAcqDefault();

   RcFile_SetNetAcqEnable(FALSE);
   EpgSetup_AcquisitionMode(NETACQ_DEFAULT);

   if (EpgAcqCtl_Start())
   {
      // if acq mode changed, update the rc/ini file
      if (wasNetAcq)
         UpdateRcFile(TRUE);
   }
   else
   {
      #ifdef USE_DAEMON
      #ifndef WIN32
      if (EpgNetIo_CheckConnect())
      #else
      if (EpgAcqClient_IsLocalServer() && Daemon_CheckIfRunning())
      #endif
      {  // daemon is running locally, probably on the same device
         // XXX UNIX: actually we'd have to check if the daemon uses the same card
         strcpy(comm, "tk_messageBox -type okcancel -icon error "
                      "-message {Failed to start acquisition: the daemon seems to be running. "
                                "Do you want to stop the daemon now?}\n");
         eval_check(interp, comm);
         if (strcmp(Tcl_GetStringResult(interp), "ok") == 0)
         {
            // if acq mode changed, update the rc/ini file
            if (wasNetAcq == FALSE)
               UpdateRcFile(TRUE);

            if (Daemon_RemoteStop())
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
      {
      #ifndef WIN32  //is handled by bt-driver in win32
         strcpy(comm, "tk_messageBox -type ok -icon error -parent . -message {Failed to start acquisition: ");
         pErrStr = EpgAcqCtl_GetLastError();
         if (pErrStr != NULL)
            strcat(comm, pErrStr);
         else
            strcat(comm, "(Internal error, please report. Try to restart the application.)\n");
         strcat(comm, "}\n");
         eval_check(interp, comm);
      #endif
      }

      // operation failed -> keep the old mode
      if (wasNetAcq)
         RcFile_SetNetAcqEnable(TRUE);
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

   #ifdef WIN32
   if (RcFile_Query()->netacq.do_tcp_ip == FALSE)
   {
      // XXX warning message if TCP/IP is disabled because currently we support only TCP/IP on WIN32
      strcpy(comm, "tk_messageBox -type ok -icon error -message "
                   "{Currently only the TCP/IP protocol is supported for Windows, "
                    "but is disabled by default for security reasons. Please see "
                    "the Help section on Configuration, chapter Client/Server "
                    "for details.}");
      eval_check(interp, comm);
      return;
   }
   #endif

   // save previous acq mode to detect mode changes
   wasNetAcq = IsRemoteAcqDefault();

   RcFile_SetNetAcqEnable(TRUE);
   EpgSetup_AcquisitionMode(NETACQ_DEFAULT);

   #ifndef WIN32
   if (EpgAcqCtl_Start())
   #else
   if ( (!EpgAcqClient_IsLocalServer() || Daemon_CheckIfRunning()) &&
        (EpgAcqCtl_Start()) )
   #endif
   {
      // if acq mode changed, update the rc/ini file
      if (wasNetAcq == FALSE)
         UpdateRcFile(TRUE);
   }
   else
   {  // failed to connect to the server

      if (EpgAcqClient_IsLocalServer())
      {
         strcpy(comm, "tk_messageBox -type okcancel -icon error "
                      "-message {The daemon seems not to be running. "
                                "Do you want to start it now?}\n");
         eval_check(interp, comm);
         if (strcmp(Tcl_GetStringResult(interp), "ok") == 0)
         {
            // if acq mode changed, update the rc/ini file
            if (wasNetAcq == FALSE)
               UpdateRcFile(TRUE);
            wasNetAcq = TRUE;

            EpgMain_StartDaemon();
         }
      }
      else
         UiControlMsg_NetAcqError();

      // operation failed -> keep the old mode
      if (wasNetAcq == FALSE)
         RcFile_SetNetAcqEnable(FALSE);
   }
#endif
}

// ----------------------------------------------------------------------------
// Toggle acquisition on/off
// - acquisition may be started either locally or via a network connection
//
static int MenuCmd_ToggleAcq( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ToggleAcq <enable-acq> <enable-daemon>";
   EPGACQ_DESCR acqState;
   int  enableAcq, enableDaemon;
   int  result;

   if (objc != 3)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( (Tcl_GetBooleanFromObj(interp, objv[1], &enableAcq) != TCL_OK) ||
             (Tcl_GetBooleanFromObj(interp, objv[2], &enableDaemon) != TCL_OK) )
   {  // parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else
   {
      EpgAcqCtl_DescribeAcqState(&acqState);

      if (acqState.ttxGrabState == ACQDESCR_DISABLED)
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
               if (Daemon_RemoteStop() == FALSE)
               {  // failed to stop the daemon -> inform the user
                  strcpy(comm, "tk_messageBox -type ok -icon error -message {Failed to stop the daemon - you're disconnected but acquisition continues in the background.}");
                  eval_check(interp, comm);
               }
               EpgAcqCtl_Stop();
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
// Query if acq is currently enabled and in remote acq mode
//
static int MenuCmd_IsAcqEnabled( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_IsAcqEnabled";
   EPGACQ_DESCR acqState;
   bool isActive;
   int  result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else 
   {
      EpgAcqCtl_DescribeAcqState(&acqState);
      isActive = acqState.ttxGrabState != ACQDESCR_DISABLED;

      Tcl_SetObjResult(interp, Tcl_NewBooleanObj(isActive));

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Query if acq is currently enabled and in remote acq mode
//
static int MenuCmd_IsNetAcqActive( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_IsNetAcqActive {clear_errors|default}";
   #ifdef USE_DAEMON
   EPGACQ_DESCR acqState;
   #endif
   bool isActive;
   int  result;

   if (objc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else 
   {
      #ifdef USE_DAEMON
      EpgAcqCtl_DescribeAcqState(&acqState);
      isActive = acqState.isNetAcq;

      if (strcmp(Tcl_GetString(objv[1]), "clear_errors") == 0)
      {
         // stop acq if network acquisition is in error state
         if ( (acqState.isNetAcq) && (acqState.ttxGrabState == ACQDESCR_DISABLED) )
         {
            EpgAcqCtl_Stop();

            isActive = FALSE;
         }
      }
      else
      {  // passive mode: also return TRUE if default is network acq
         isActive |= IsRemoteAcqDefault();
      }
      #else
      isActive = FALSE;
      #endif

      // netacq is TRUE if and only if acq is enabled in netacq mode so we need not check the general
      // acq state (actually we must not check for DISABLED because that's set after network errors)

      Tcl_SetResult(interp, (isActive ? "1" : "0"), TCL_STATIC);

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Query if video input source is external (i.e. not a tuner)
// - currently used to cleanly prevent start of EPG scan
// - only works while acquisition is enabled (XXX FIXME)
//
static int MenuCmd_IsAcqExternal( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_IsAcqExternal";
   EPGACQ_DESCR acqState;
   bool  isExternal;
   int result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else 
   {
      EpgAcqCtl_DescribeAcqState(&acqState);

      if ( (acqState.ttxGrabState != ACQDESCR_DISABLED) &&
           (acqState.passiveReason == ACQPASSIVE_NO_TUNER) )
         isExternal = TRUE;
      else
         isExternal = FALSE;

      Tcl_SetResult(interp, (isExternal ? "1" : "0"), TCL_STATIC);

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Set user configured language of PDC themes
// - called during startup and manual config change, and due to automatic language
//   selection mode also after provider switch and AI version change
//
void SetUserLanguage( Tcl_Interp *interp )
{
   CONST84 char * pTmpStr;
   int    lang;
   static int last_lang = -1;

   pTmpStr = Tcl_GetVar(interp, "menuUserLanguage", TCL_GLOBAL_ONLY);
   if (pTmpStr != NULL)
   {
      if (Tcl_GetInt(interp, pTmpStr, &lang) == TCL_OK)
      {
         if (lang == 7)
         {
            // automatic mode: determine language from AI block
            lang = EpgSetup_GetDefaultLang(pUiDbContext);
         }

         // pass the language index to the PDC themes module
         PdcThemeSetLanguage(lang);

         if ((lang != last_lang) && (last_lang != -1))
         {
            // rebuild the themes filter menu
            eval_check(interp, "FilterMenuAdd_Themes .menubar.filter.themes 0");
         }
         last_lang = lang;
      }
      else
         debug1("SetUserLanguage: failed to parse '%s'", pTmpStr);
   }
   else
      debug0("SetUserLanguage: Tcl var menuUserLanguage is undefined");
}

// ----------------------------------------------------------------------------
// Trigger update of user configured language after menu selection
//
static int MenuCmd_UpdateLanguage( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   // load the new language setting from the Tcl config variable
   SetUserLanguage(interp);

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Switch the provider for the browser
// - only used for the provider selection popup,
//   not for the initial selection nor for provider merging
//
static int MenuCmd_ChangeProvider( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ChangeProvider <cni>";
   EPGDB_CONTEXT * pDbContext;
   int cni;
   int result;

   if (objc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      // get the CNI of the selected provider
      if (Tcl_GetIntFromObj(interp, objv[1], &cni) == TCL_OK)
      {
         // note: illegal CNIs 0 and 0x00ff are caught in the open function
         pDbContext = EpgContextCtl_Open(cni, TRUE, CTX_RELOAD_ERR_REQ);
         if (pDbContext != NULL)
         {
            EpgContextCtl_Close(pUiDbContext);
            pUiDbContext = pDbContext;

            // in case follow-ui acq mode is used, change the acq db too
            EpgSetup_AcquisitionMode(NETACQ_KEEP);

            UiControl_AiStateChange(DB_TARGET_UI);
            eval_check(interp, "ResetFilterState");

            PiBox_Reset();

            // put the new CNI at the front of the selection order and update the config file
            RcFile_UpdateProvSelection(cni);
            UpdateRcFile(TRUE);
         }

         result = TCL_OK;
      }
      else
      {
         sprintf(comm, "C_ChangeProvider: expected hex CNI but got: %s", Tcl_GetString(objv[1]));
         Tcl_SetResult(interp, comm, TCL_VOLATILE);
         result = TCL_ERROR;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Return descriptive text for a given 16-bit network code
//
static int MenuCmd_GetCniDescription( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetCniDescription <cni>";
   const char * pName, * pCountry;
   Tcl_Obj * pCniDesc;
   int cni;
   int result;

   if (objc != 1+1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetIntFromObj(interp, objv[1], &cni) )
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
            pCniDesc = TranscodeToUtf8(EPG_ENC_ISO_8859_1, NULL, pName, " (");
            AppendToUtf8(EPG_ENC_ISO_8859_1, pCniDesc, pCountry, ")");
         }
         else
         {
            pCniDesc = TranscodeToUtf8(EPG_ENC_ISO_8859_1, NULL, pName, NULL);
         }
         Tcl_SetObjResult(interp, pCniDesc);
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Return service name and list of networks of the given database
// - used by provider selection popup
//
static int MenuCmd_GetProvServiceName( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetProvServiceName <cni>";
   const AI_BLOCK * pAi;
   EPGDB_CONTEXT  * pPeek;
   const char * pStr;
   int cni;
   int result;

   if (objc != 1+1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetIntFromObj(interp, objv[1], &cni) )
   {  // the parameter is not an integer
      result = TCL_ERROR;
   }
   else
   {
      if (IS_XMLTV_CNI(cni))
      {
         pPeek = EpgContextCtl_Peek(cni, CTX_RELOAD_ERR_ANY);
         if (pPeek != NULL)
         {
            // TODO optimize to avoid PEEK? - XML file name is sufficient
            EpgDbLockDatabase(pPeek, TRUE);
            pAi = EpgDbGetAi(pPeek);
            if (pAi != NULL)
            {
               pStr = AI_GET_SERVICENAME(pAi);
               Tcl_SetObjResult(interp, TranscodeToUtf8(EPG_ENC_XMLTV, NULL, pStr, NULL));
            }
            EpgDbLockDatabase(pPeek, FALSE);

            EpgContextCtl_ClosePeek(pPeek);
         }
      }
      else if (cni == MERGED_PROV_CNI)
      {
         Tcl_SetObjResult(interp, Tcl_NewStringObj("merged", -1));
      }
      else
      {
         fatal1("MenuCmd-GetProvServiceName: CNI neither merged nor XML:0x%X\n", cni);
         Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown", -1));
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Return service name and list of networks of the given database
// - used by provider selection popup
//
static int MenuCmd_GetProvServiceInfos( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetProvServiceInfos <cni>";
   const AI_BLOCK * pAi;
   const OI_BLOCK * pOi;
   EPGDB_CONTEXT  * pPeek;
   Tcl_Obj * pResultList;
   const char * pStr;
   int cni, netwop;
   int result;

   if (objc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetIntFromObj(interp, objv[1], &cni) )
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
         pOi = EpgDbGetOi(pPeek);

         if (pAi != NULL)
         {
            pResultList = Tcl_NewListObj(0, NULL);

            // first element in return list is the service name
            pStr = AI_GET_SERVICENAME(pAi);
            Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8(EPG_ENC_XMLTV, NULL, pStr, NULL));

            // second element is OI header
            if ((pOi != NULL) && OI_HAS_HEADER(pOi))
            {
               pStr = OI_GET_HEADER(pOi);
               Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8(EPG_ENC_XMLTV, NULL, pStr, NULL));
            }
            else
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("", 0));

            // third element is OI message
            if ((pOi != NULL) && OI_HAS_MESSAGE(pOi))
            {
               pStr = OI_GET_MESSAGE(pOi);
               Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8(EPG_ENC_XMLTV, NULL, pStr, NULL));
            }
            else
               Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("", 0));

            // append names of all netwops
            for ( netwop = 0; netwop < pAi->netwopCount; netwop++ ) 
            {
               pStr = AI_GET_NETWOP_NAME(pAi, netwop);
               Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8(EPG_ENC_XMLTV, NULL, pStr, NULL));
            }
            Tcl_SetObjResult(interp, pResultList);
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
static int MenuCmd_GetCurrentDatabaseCni( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetCurrentDatabaseCni";
   char buf[16+2+1];
   uint dbCni;
   int result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {  // return the CNI of the currently used browser database
      dbCni = EpgDbContextGetCni(pUiDbContext);

      sprintf(buf, "0x%04X", dbCni);
      Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get list of provider CNIs and names
// - for provider selection and merge popup
//
static int MenuCmd_GetProvCnisAndNames( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetProvCnisAndNames <dir>";
   const AI_BLOCK * pAi;
   const uint * pCniList;
   EPGDB_CONTEXT  * pPeek;
   const char * pDir;
   Tcl_Obj * pResultList;
   char buf[16+2+1];
   uint idx, cniCount;
   int result;

   if (objc != 1+1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pDir = Tcl_GetString(objv[1]);
      pResultList = Tcl_NewListObj(0, NULL);

      pCniList = EpgContextCtl_GetProvList(pDir, &cniCount);
      if (pCniList != NULL)
      {
         for (idx=0; idx < cniCount; idx++)
         {
            pPeek = EpgContextCtl_Peek(pCniList[idx], CTX_RELOAD_ERR_ANY);
            if (pPeek != NULL)
            {
               EpgDbLockDatabase(pPeek, TRUE);
               pAi = EpgDbGetAi(pPeek);
               if (pAi != NULL)
               {
                  sprintf(buf, "0x%04X", pCniList[idx]);
                  Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(buf, -1));

                  Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8(EPG_ENC_XMLTV, NULL, AI_GET_SERVICENAME(pAi), NULL));
               }
               EpgDbLockDatabase(pPeek, FALSE);

               EpgContextCtl_ClosePeek(pPeek);
            }
         }
         xfree((void *) pCniList);
      }
      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get network selection configuration for a given provider
// - called when the network selection configuration dialog is opened
// - returns a list of two lists:
//   (1) List of CNIs of selected networks: defined network order
//   (2) List of CNIs of excluded networks
//
static int MenuCmd_GetProvCniConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetProvCniConfig <prov_cni>";
   Tcl_Obj * pResultList;
   Tcl_Obj * pOrdList;
   Tcl_Obj * pSupList;
   char cni_buf[16+2+1];
   const uint * pSelCnis;
   const uint * pSupCnis;
   uint selCount, supCount;
   uint netIdx;
   int prov_cni;
   int result;

   if ((objc != 1+1) || (Tcl_GetIntFromObj(interp, objv[1], &prov_cni) != TCL_OK))
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);
      pOrdList = Tcl_NewListObj(0, NULL);
      pSupList = Tcl_NewListObj(0, NULL);

      // retrieve config from rc/ini file
      RcFile_GetNetworkSelection(prov_cni, &selCount, &pSelCnis, &supCount, &pSupCnis);

      if (pSelCnis != NULL)
      {
         for (netIdx = 0; netIdx < selCount; netIdx++)
         {
            sprintf(cni_buf, "0x%04X", pSelCnis[netIdx]);
            Tcl_ListObjAppendElement(interp, pOrdList, Tcl_NewStringObj(cni_buf, -1));
         }
      }
      if (pSupCnis != NULL)
      {
         for (netIdx = 0; netIdx < supCount; netIdx++)
         {
            sprintf(cni_buf, "0x%04X", pSupCnis[netIdx]);
            Tcl_ListObjAppendElement(interp, pSupList, Tcl_NewStringObj(cni_buf, -1));
         }
      }
      Tcl_ListObjAppendElement(interp, pResultList, pOrdList);
      Tcl_ListObjAppendElement(interp, pResultList, pSupList);
      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get network selection configuration for a given provider
// - called when the network selection configuration dialog is closed
// - gets the provider's CNI and two lists with network CNIs as parameters;
//   meaning of the lists is the same is in the "Get" function above
//
static int MenuCmd_UpdateProvCniConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_UpdateProvCniConfig <prov_cni> <selected_list> <suppressed_list>";
   Tcl_Obj ** pSelCniObjv;
   Tcl_Obj ** pSupCniObjv;
   uint * pSelCni, * pSupCni;
   uint cniSelCount;
   uint cniSupCount;
   uint provCni;
   uint idx;
   int result;

   if ((objc != 1+3) || (Tcl_GetIntFromObj(interp, objv[1], (int*)&provCni) != TCL_OK))
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = Tcl_ListObjGetElements(interp, objv[2], (int*)&cniSelCount, &pSelCniObjv);
      if (result == TCL_OK)
      {
         if (cniSelCount > RC_MAX_DB_NETWWOPS)
            cniSelCount = RC_MAX_DB_NETWWOPS;
         pSelCni = xmalloc(sizeof(*pSelCni) * (cniSelCount + 1));  // +1 to avoid zero-len alloc
         for (idx = 0; (idx < cniSelCount) && (result == TCL_OK); idx++)
            result = Tcl_GetIntFromObj(interp, pSelCniObjv[idx], (int*)&pSelCni[idx]);

         if (result == TCL_OK)
         {
            result = Tcl_ListObjGetElements(interp, objv[3], (int*)&cniSupCount, &pSupCniObjv);
            if (result == TCL_OK)
            {
               if (cniSupCount > RC_MAX_DB_NETWWOPS)
                  cniSupCount = RC_MAX_DB_NETWWOPS;

               pSupCni = xmalloc(sizeof(*pSupCni) * (cniSupCount + 1));
               for (idx = 0; (idx < cniSupCount) && (result == TCL_OK); idx++)
                  result = Tcl_GetIntFromObj(interp, pSupCniObjv[idx], (int*)&pSupCni[idx]);

               if (result == TCL_OK)
               {
                  RcFile_UpdateNetworkSelection(provCni, cniSelCount, pSelCni, cniSupCount, pSupCni);
                  UpdateRcFile(TRUE);
               }
               xfree(pSupCni);
            }
            else
               debugTclErr(interp, "MenuCmd-UpdateProvCniConfig: parse error sel list");
         }
         xfree(pSelCni);
      }
      else
         debugTclErr(interp, "MenuCmd-UpdateProvCniConfig: parse error sup list");
   }

   return result;
}

// ----------------------------------------------------------------------------
// Append list of networks in a AI to the result
// - as a side-effect the netwop names are stored into a TCL array
//
static void MenuCmd_AppendNetwopList( Tcl_Interp *interp, Tcl_Obj * pList,
                                      EPGDB_CONTEXT * pDbContext, char * pArrName,
                                      bool unify, bool ai_names )
{
   const AI_BLOCK * pAiBlock;
   const AI_NETWOP *pNetwop;
   const char * pCfNetname;
   char strbuf[16+2+1];
   Tcl_Obj * pNetwopObj;
   bool  isFromAi;
   uint  netwop;
   uint  cni;

   EpgDbLockDatabase(pDbContext, TRUE);
   pAiBlock = EpgDbGetAi(pDbContext);
   if (pAiBlock != NULL)
   {
      pNetwop = AI_GET_NETWOPS(pAiBlock);
      for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++, pNetwop++ ) 
      {
         cni = AI_GET_NET_CNI(pNetwop);
         //if (IS_NXTV_CNI(cni))
         //   cni = CniConvertUnknownToPdc(cni);
         sprintf(strbuf, "0x%04X", cni);

         // if requested check if the same CNI is already in the result
         // XXX hack: only works if name array is given
         if ( (unify == FALSE) || (pArrName[0] == 0) ||
              (Tcl_GetVar2(interp, pArrName, strbuf, 0) == NULL) )
         {
            // append the CNI in the format "0x0D94" to the TCL result list
            Tcl_ListObjAppendElement(interp, pList, Tcl_NewStringObj(strbuf, -1));

            // as a side-effect the netwop names are stored into a TCL array
            if (pArrName[0] != 0)
            {
               if (ai_names)
               {
                  pCfNetname = AI_GET_NETWOP_NAME(pAiBlock, netwop);
                  isFromAi = TRUE;
               }
               else
               {
                  pCfNetname = EpgSetup_GetNetName(pAiBlock, netwop, &isFromAi);
               }
               pNetwopObj = TranscodeToUtf8(EPG_ENC_NETNAME(isFromAi), NULL, pCfNetname, NULL);
               Tcl_SetVar2Ex(interp, pArrName, strbuf, pNetwopObj, 0);

               if (unify && IS_NXTV_CNI(cni))
               {
                  sprintf(strbuf, "0x%04X", CniConvertUnknownToPdc(cni));
                  Tcl_SetVar2Ex(interp, pArrName, strbuf, pNetwopObj, 0);
               }
            }
         }
      }
   }
   else
      debug1("MenuCmd-AppendNetwopList: no AI in peek of %04X", EpgDbContextGetCni(pDbContext));
   EpgDbLockDatabase(pDbContext, FALSE);
}

// ----------------------------------------------------------------------------
// Get List of netwop CNIs and array of names from AI for netwop selection
// - 1st argument selects the provider; 0 for the current browser database
// - 2nd argument names a Tcl array where to store the network names;
//   may be 0-string if not needed
// - optional 3rd argument may be "allmerged" if in case of a merged database
//   all CNIs of all source databases shall be returned; may also be "ai_names"
//   to suppress use of user-configured names
//
static int MenuCmd_GetAiNetwopList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetAiNetwopList <CNI> <varname> [allmerged|ai_names]";
   EPGDB_CONTEXT  * pPeek;
   char * pVarName;
   Tcl_Obj * pResultList;
   bool allmerged = FALSE;
   bool ai_names = FALSE;
   int result, cni;

   if ((objc != 2+1) && (objc != 3+1))
   {  // wrong # of parameters for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      if (objc == 3+1)
      {
         allmerged = (strcmp(Tcl_GetString(objv[3]), "allmerged") == 0);
         ai_names = (strcmp(Tcl_GetString(objv[3]), "ai_names") == 0);
      }
      result = Tcl_GetIntFromObj(interp, objv[1], &cni);
      if (result == TCL_OK)
      {
         pResultList = Tcl_NewListObj(0, NULL);

         // clear the network names result array
         pVarName = Tcl_GetString(objv[2]);
         if (Tcl_GetCharLength(objv[2]) != 0)
            Tcl_UnsetVar(interp, pVarName, 0);

         // special case: CNI 0 refers to the current browser db
         if (cni == 0)
            cni = EpgDbContextGetCni(pUiDbContext);

         if (cni == 0)
         {  // no provider selected -> return empty list (no error)
         }
         else if (cni != MERGED_PROV_CNI)
         {  // regular (non-merged) database -> get "peek" with (at least) AI and OI
            pPeek = EpgContextCtl_Peek(cni, CTX_RELOAD_ERR_ANY);
            if (pPeek != NULL)
            {
               MenuCmd_AppendNetwopList(interp, pResultList, pPeek, pVarName, FALSE, ai_names);

               EpgContextCtl_ClosePeek(pPeek);
            }
            else
               debug1("MenuCmd-GetAiNetwopList: requested db %04X not available", cni);
         }
         else
         {  // merged database

            if (allmerged == FALSE)
            {  // merged db -> use the merged AI with the user-configured netwop list
               MenuCmd_AppendNetwopList(interp, pResultList, pUiDbContext, pVarName, FALSE, ai_names);
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
                        MenuCmd_AppendNetwopList(interp, pResultList, pPeek, pVarName, TRUE, ai_names);

                        EpgContextCtl_ClosePeek(pPeek);
                     }
                  }
               }
            }
         }
         Tcl_SetObjResult(interp, pResultList);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Get list of network name definitions
// - only returns user-configured names (i.e. not names in AI)
// - returns a list of CNI/name pairs in one flat list
//
static int MenuCmd_GetNetwopNames( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetNetwopNames";
   const RCFILE * pRc;
   Tcl_Obj * pResultList;
   char cni_buf[16+2+1];
   uint idx;
   int result;

   if (objc != 1)
   {  // wrong # of parameters for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);

      pRc = RcFile_Query();

      for (idx = 0; idx < pRc->net_names_count; idx++)
      {
         if (pRc->net_names[idx].name != NULL)
         {
            sprintf(cni_buf, "0x%04X", pRc->net_names[idx].net_cni);
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(cni_buf, -1));

            Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, pRc->net_names[idx].name, NULL));
         }
         else
            debug1("MenuCmd-GetNetwopNames: skipping empty name for net 0x%04X", pRc->net_names[idx].net_cni);
      }

      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Update list of network name definitions
// - parameter is a flat list of CNIs and names, alternating
//   (i.e. same format as delivered by _GetNetwopNames)
// - called when the network name configuration dialog is closed with "OK"
//
static int MenuCmd_UpdateNetwopNames( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_UpdateNetwopNames {<cni> <name> ...}";
   Tcl_Obj ** pObjArgv;
   int objCount;
   uint * pCniList;
   const char ** pNameList;
   Tcl_DString * pDstrList;
   uint idx;
   int result;

   if (objc != 1+1)
   {  // wrong # of parameters for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_ListObjGetElements(interp, objv[1], &objCount, &pObjArgv);
      if (result == TCL_OK)
      {
         pCniList = xmalloc(sizeof(*pCniList) * objCount/2);
         pNameList = xmalloc(sizeof(*pNameList) * objCount/2);
         pDstrList = xmalloc(sizeof(*pDstrList) * objCount/2);
         for (idx = 0; idx < objCount/2; idx++)
            Tcl_DStringInit(&pDstrList[idx]);

         for (idx = 0; idx < objCount/2; idx++)
         {
            Tcl_UtfToExternalDString(NULL, Tcl_GetString(pObjArgv[idx*2 + 1]), -1, &pDstrList[idx]);
            pNameList[idx] = Tcl_DStringValue(&pDstrList[idx]);

            if (Tcl_GetIntFromObj(interp, pObjArgv[idx*2], (int*)&pCniList[idx]) != TCL_OK)
            {
               result = TCL_ERROR;
            }
         }
         if (result == TCL_OK)
         {
            RcFile_UpdateNetworkNames(objCount/2, pCniList, pNameList);
         }
         UpdateRcFile(TRUE);

         xfree(pCniList);
         xfree(pNameList);
         for (idx = 0; idx < objCount/2; idx++)
            Tcl_DStringFree(&pDstrList[idx]);
         xfree(pDstrList);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Normalize and return the given CNI
//
static int MenuCmd_NormalizeCni( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_NormalizeCni <cni>";
   uint cni;
   char cni_buf[16+2+1];
   int result;

   if (objc != 1+1)
   {  // wrong # of parameters for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if (Tcl_GetIntFromObj(interp, objv[1], (int*)&cni) != TCL_OK)
   {
      result = TCL_ERROR; 
   }
   else
   {
      if (IS_NXTV_CNI(cni))
      {
         sprintf(cni_buf, "0x%04X", CniConvertUnknownToPdc(cni));
         Tcl_SetObjResult(interp, Tcl_NewStringObj(cni_buf, -1));
      }
      else
      {  // return CNI unchanged
         Tcl_SetObjResult(interp, objv[1]);
      }
      result = TCL_OK; 
   }
   return result;
}

// ----------------------------------------------------------------------------
// Update database merge options in the rc file
// - called after merge dialog is left with OK
//
static int MenuCmd_UpdateMergeOptions( Tcl_Interp *interp, Tcl_Obj * pAttrListObj )
{
   Tcl_Obj       ** pAttrArgv;
   Tcl_Obj       ** pIdxArgv;
   uint cniBuf[MAX_MERGED_DB_COUNT];
   uint cniBufCount;
   int attrCount, idxCount, idx, ati, matIdx, cni;
   int  result;

   // note order must match that of enum MERGE_ATTRIB_TYPE
   static CONST84 char * pKeywords[] = { "cftitle", "cfdescr", "cfthemes",
      "cfseries", "cfeditorial", "cfparental",
      "cfsound", "cfformat", "cfrepeat", "cfsubt",
      "cfmisc", "cfvps", (char *) NULL };

   result = Tcl_ListObjGetElements(interp, pAttrListObj, &attrCount, &pAttrArgv);
   if (result == TCL_OK)
   {
      // initialize the attribute matrix with default (empty CNI list means "all providers
      // in original order"; to exclude all providers 0xFF is inserted, see below)
      for (ati = 0; ati < MERGE_TYPE_COUNT; ati++)
      {
         RcFile_UpdateDbMergeOptions(ati, NULL, 0);
      }

      // options are stored in a Tcl list in pairs of keyword and a CNI sub-list
      for (ati = 0; (ati+1 < attrCount) && (result == TCL_OK); ati += 2)
      {
         result = Tcl_GetIndexFromObj(interp, pAttrArgv[ati], pKeywords, "keyword", TCL_EXACT, &matIdx);
         if (result == TCL_OK)
         {
            // parse CNI list
            result = Tcl_ListObjGetElements(interp, pAttrArgv[ati + 1], &idxCount, &pIdxArgv);
            if (result== TCL_OK)
            {
               cniBufCount = 0;
               for (idx=0; (idx < idxCount) && (result == TCL_OK); idx++)
               {
                  result = Tcl_GetIntFromObj(interp, pIdxArgv[idx], &cni);
                  if (result == TCL_OK)
                  {
                     cniBuf[cniBufCount++] = cni;
                  }
               }
               if (cniBufCount == 0)
               {  // special case: all providers excluded
                  cniBuf[0] = 0xFF;
                  RcFile_UpdateDbMergeOptions(matIdx, cniBuf, 1);
               }
               else
                  RcFile_UpdateDbMergeOptions(matIdx, cniBuf, cniBufCount);
            }
         }
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Update database merge provider list in the rc file
// - called after merge dialog is left with OK
//
static int MenuCmd_UpdateMergeProviders( Tcl_Interp *interp, Tcl_Obj * pCniListObj )
{
   Tcl_Obj ** pCniObjv;
   uint cniBuf[MAX_MERGED_DB_COUNT];
   int  cniCount, cni, idx;
   int  result;

   // parse CNI list, format: {0x0d94 ...}
   result = Tcl_ListObjGetElements(interp, pCniListObj, &cniCount, &pCniObjv);
   if (result == TCL_OK)
   {
      if (cniCount > MAX_MERGED_DB_COUNT)
         cniCount = MAX_MERGED_DB_COUNT;

      for (idx=0; (idx < cniCount) && (result == TCL_OK); idx++)
      {
         result = Tcl_GetIntFromObj(interp, pCniObjv[idx], &cni);
         cniBuf[idx] = cni;
      }

      if (result == TCL_OK)
      {
         RcFile_UpdateDbMergeCnis(cniBuf, cniCount);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Retrieve merge provider CNI list from rc file for merge config dialog
//
static int MenuCmd_GetMergeProviderList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetMergeProviderList";
   const RCFILE * pRc;
   Tcl_Obj * pResultList;
   char cni_buf[16+2+1];
   int  idx;
   int  result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);
      pRc = RcFile_Query();

      for (idx = 0; idx < pRc->db.prov_merge_count; idx++)
      {
         sprintf(cni_buf, "0x%04X", pRc->db.prov_merge_cnis[idx]);
         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(cni_buf, -1));
      }
      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Retrieve merge options from rc file for merge config dialog
//
static int MenuCmd_GetMergeOptions( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetMergeOptions";
   const RCFILE * pRc;
   Tcl_Obj * pResultList;
   Tcl_Obj * pCniList;
   char cni_buf[16+2+1];
   int  ati, idx;
   int  result;

   static const char * pKeywords[] = { "cftitle", "cfdescr", "cfthemes",
      "cfseries", "cfeditorial", "cfparental",
      "cfsound", "cfformat", "cfrepeat", "cfsubt",
      "cfmisc", "cfvps", (char *) NULL };

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);
      pRc = RcFile_Query();

      // initialize the attribute matrix with default index order: 0,1,2,...,n-1,0xff,...
      for (ati=0; ati < MERGE_TYPE_COUNT; ati++)
      {
         // skip attributes with default value (empty list means default; "all providers
         // removed" is represented by CNI 0xff, see below)
         if (pRc->db.prov_merge_opt_count[ati] != 0)
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(pKeywords[ati], -1));
            pCniList = Tcl_NewListObj(0, NULL);

            if ( (pRc->db.prov_merge_opt_count[ati] != 1) ||
                 (pRc->db.prov_merge_opts[ati][0] != 0xff) )
            {
               for (idx = 0; idx < pRc->db.prov_merge_opt_count[ati]; idx++)
               {
                  sprintf(cni_buf, "0x%04X", pRc->db.prov_merge_opts[ati][idx]);
                  Tcl_ListObjAppendElement(interp, pCniList, Tcl_NewStringObj(cni_buf, -1));
               }
            }
            Tcl_ListObjAppendElement(interp, pResultList, pCniList);
         }
      }
      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Initiate the database merging
// - called by the 'Merge XMLTV files' dialog
// - parameters are taken from global Tcl variables, because the same
//   update is required at startup
//
static int MenuCmd_ProvMerge_Start( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ProvMerge_Start [<prov-list> <options>]";
   EPGDB_CONTEXT * pDbContext;
   int  result;

   if ((objc != 1) && (objc != 3))
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (objc == 3)
      {
         MenuCmd_UpdateMergeProviders(interp, objv[1]);
         MenuCmd_UpdateMergeOptions(interp, objv[1+1]);
      }

      pDbContext = EpgSetup_MergeDatabases();
      if (pDbContext != NULL)
      {
         EpgContextCtl_Close(pUiDbContext);
         pUiDbContext = pDbContext;

         EpgSetup_AcquisitionMode(NETACQ_KEEP);

         UiControl_AiStateChange(DB_TARGET_UI);
         eval_check(interp, "ResetFilterState");

         PiBox_Reset();
         UpdateRcFile(TRUE);
      }
      // note: db load errors are reported via callback
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Callback for expire cut-off delay configuration dialog
// 
static int MenuCmd_UpdatePiExpireDelay( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_UpdatePiExpireDelay <value>";
   int  value;
   int  result;

   if (objc != 1+1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetIntFromObj(interp, objv[1], &value) != TCL_OK)
   {
      result = TCL_ERROR;
   }
   else
   {
      RcFile_SetDbExpireDelay(value);
      UpdateRcFile(TRUE);

      EpgSetup_DbExpireDelay();
#ifdef USE_TTX_GRABBER
      EpgSetup_TtxGrabber();
#endif

      UiControlMsg_AcqEvent(ACQ_EVENT_PI_EXPIRED);
      UiControl_AiStateChange(DB_TARGET_UI);

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Retrieve current PI expire time parameter for config dialog
// 
static int MenuCmd_GetPiExpireDelay( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetPiExpireDelay";
   uint expireTime;
   int  result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      expireTime = RcFile_Query()->db.piexpire_cutoff;

      Tcl_SetObjResult(interp, Tcl_NewIntObj(expireTime));
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Pre-check for cut-off time configuration
// - returns the number of PI which would be deleted
// 
static int MenuCmd_CountExpiredPi( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_CountExpiredPi <threshold>";
   const PI_BLOCK  * pPiBlock;
   int  cutOffTime;
   int  piCount;
   int  result;

   if ( (objc != 1+1) ||
        (Tcl_GetIntFromObj(interp, objv[1], &cutOffTime) != TCL_OK) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      cutOffTime = EpgGetUiMinuteTime() - (cutOffTime * 60);

      EpgDbLockDatabase(pUiDbContext, TRUE);
      pPiBlock = EpgDbSearchFirstPi(pUiDbContext, NULL);
      piCount = 0;
      while ((pPiBlock != NULL) && (pPiBlock->start_time < cutOffTime))
      {
         if (pPiBlock->stop_time <= cutOffTime)
            piCount += 1;

         pPiBlock = EpgDbSearchNextPi(pUiDbContext, NULL, pPiBlock);
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);

      Tcl_SetObjResult(interp, Tcl_NewIntObj(piCount));
      result = TCL_OK;
   }
   return result;
}


// ----------------------------------------------------------------------------
// Query acquisition mode and acquisition provider list
//
static int MenuCmd_GetAcqConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetAcqConfig";
   const RCFILE * pRc;
   Tcl_Obj * pResultList;
   const char * pAcqModeStr;
   int  result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pRc = RcFile_Query();
      if (pRc != NULL)
      {
         pResultList = Tcl_NewListObj(0, NULL);

         pAcqModeStr = RcFile_GetAcqModeStr(pRc->acq.acq_mode);
         if (pAcqModeStr == NULL)
            pAcqModeStr = RcFile_GetAcqModeStr(ACQ_DFLT_ACQ_MODE);
         if (pAcqModeStr == NULL)
            pAcqModeStr = "";
         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(pAcqModeStr, -1));

         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(pRc->acq.acq_start));

         Tcl_SetObjResult(interp, pResultList);
      }
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Append a line to the EPG scan messages
//
static void MenuCmd_AddEpgScanMsg( const char * pMsg, bool bold )
{
   Tcl_DString cmd_dstr;

   if (pMsg != NULL)
   {
      Tcl_DStringInit(&cmd_dstr);
      Tcl_DStringAppend(&cmd_dstr, "EpgScanAddMessage ", -1);
      // append message (plus a newline) as list element, so that '{' etc. is escaped properly
      Tcl_DStringAppendElement(&cmd_dstr, pMsg);
      Tcl_DStringAppend(&cmd_dstr, (bold ? " bold" : " {}"), -1);

      eval_check(interp, Tcl_DStringValue(&cmd_dstr));

      Tcl_DStringFree(&cmd_dstr);
   }
   else
      debug0("MenuCmd-AddEpgScanMsg: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Start EPG scan
// - during the scan the focus is forced into the .epgscan popup
//
static int MenuCmd_StartEpgScan( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_StartEpgScan <slow=0/1> <ftable>";
   EPGSCAN_START_RESULT scanResult;
   char * pErrMsg;
   char * chnNames;
   EPGACQ_TUNER_PAR * freqTab;
   uint freqCount;
   int isOptionSlow, ftableIdx;
   uint rescheduleMs;
   int result;

   if (objc != 1+2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if ( (Tcl_GetIntFromObj(interp, objv[1], &isOptionSlow) == TCL_OK) &&
           (Tcl_GetIntFromObj(interp, objv[2], &ftableIdx) == TCL_OK) )
      {
         freqCount = 0;
         chnNames = NULL;
         freqTab = NULL;
         if (ftableIdx == 0)
         {  // in this mode only channels which are defined in the .xawtv file are visited
            pErrMsg = NULL;
            if (WintvCfg_GetFreqTab(&chnNames, &freqTab, &freqCount, &pErrMsg) == FALSE)
            {
               sprintf(comm, "tk_messageBox -type ok -icon error -parent .epgscan -message {"
                             "%s "
                             "(Note the \"Test\" button in the TV app. interaction dialog.)}",
                             ((pErrMsg != NULL) ? pErrMsg : "Please check your TV application settings."));
               eval_check(interp, comm);

               if (pErrMsg != NULL)
                  xfree(pErrMsg);
               Tcl_ResetResult(interp);
               return TCL_OK;
            }
            else if ( (freqTab == NULL) || (freqCount == 0) )
            {
               sprintf(comm, "tk_messageBox -type ok -icon error -parent .epgscan -message {"
                             "No channel assignments found. "
                             "Please disable option \"Load channel table from %s\"}",
                             WintvCfg_GetName());
               eval_check(interp, comm);

               Tcl_ResetResult(interp);
               return TCL_OK;
            }
         }
         else
         {  // pass the frequency table selection to the TV-channel module
            TvChannels_SelectFreqTable(ftableIdx - 1);
         }

         RcFile_SetAcqScanOpt(ftableIdx);
         UpdateRcFile(FALSE);

         scanResult = EpgScan_Start(RcFile_Query()->tvcard.input,
                                    isOptionSlow, (ftableIdx == 0),
                                    chnNames, freqTab, freqCount, &rescheduleMs,
                                    &MenuCmd_AddEpgScanMsg);
         switch (scanResult)
         {
            case EPGSCAN_ACCESS_DEV_VBI:
            case EPGSCAN_ACCESS_DEV_VIDEO:
            {
               #ifdef WIN32
               bool isEnabled, hasDriver;

               if ( BtDriver_GetState(&isEnabled, &hasDriver, NULL) &&
                    (isEnabled != FALSE) && (hasDriver == FALSE) )
               {
                  sprintf(comm, "tk_messageBox -type ok -icon error -parent .epgscan -message \""
                                "Cannot start the EPG scan while the TV application is blocking "
                                "the TV card.  Terminate the TV application and try again.\"");
               }
               else
               #endif
               {
                  sprintf(comm, "tk_messageBox -type ok -icon error -parent .epgscan -message {"
                                #ifndef WIN32
                                "Failed to open input device: see the scan output window for the system error message"
                                #else
                                "Failed to start the EPG scan due to a TV card driver problem. "
                                "Press 'Help' for more information."
                                #endif
                                "}");
               }
               eval_check(interp, comm);
               break;
            }

            case EPGSCAN_NO_TUNER:
               sprintf(comm, "tk_messageBox -type ok -icon error -parent .epgscan "
                             "-message {The provider search cannot be used for external video sources. "
                                       "Quit the scan dialog and tune an EPG provider's TV channel "
                                       "manually (e.g. in your satellite receiver or set-top box.)}\n");
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
static int MenuCmd_StopEpgScan( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   if (objc > 0)
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
static int MenuCmd_SetEpgScanSpeed( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_SetEpgScanSpeed <slow=0/1>";
   int isOptionSlow;
   int result;

   if (objc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetIntFromObj(interp, objv[1], &isOptionSlow) )
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
// Background handler for the EPG scan
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
// Retrieve list of physically present cards from driver
// - this list may no longer match the configured card list, hence for WIN32
//   the chip type is stored in the config data and compared to the scan result
//
static int MenuCmd_ScanTvCards( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_HwCfgScanTvCards <drv-type> <show-drv-err=0/1>";
   const char * pName;
   Tcl_Obj * pResultList;
   uint cardIdx;
   int  drvType;
   int  showDrvErr;
   int  result;

   if ( (objc != 3) ||
        (Tcl_GetBooleanFromObj(interp, objv[1], &drvType) != TCL_OK) ||
        (Tcl_GetBooleanFromObj(interp, objv[2], &showDrvErr) != TCL_OK) )
   {  // parameter count is invalid or parser failed
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      #if defined(__NetBSD__) || defined(__FreeBSD__)
      // On NetBSD BtDriver_GetCardName fetches its data from a struct which is filled here
      BtDriver_ScanDevices(TRUE);
      #endif

      pResultList = Tcl_NewListObj(0, NULL);
      cardIdx = 0;
      do
      {
         pName = BtDriver_GetCardName(drvType, cardIdx, showDrvErr);
         if (pName != NULL)
            Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, pName, NULL));

         cardIdx += 1;
      }
      while (pName != NULL);

      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Get list of all input types
//
static int MenuCmd_GetInputList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_HwCfgGetInputList <card_index> <drv_type>";
   const char * pName;
   Tcl_Obj * pResultList;
   int  cardIndex;
   int  drvType;
   uint inputIdx;
   int  result;

   if ( (objc != 1+2) ||
        (Tcl_GetIntFromObj(interp, objv[1], &cardIndex) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[2], &drvType) != TCL_OK) )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);
      inputIdx = 0;
      while (1)
      {
         pName = BtDriver_GetInputName(cardIndex, drvType, inputIdx);

         if (pName != NULL)
            Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, pName, NULL));
         else
            break;
         inputIdx += 1;
      }

      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

#if 0  // unused code
// ----------------------------------------------------------------------------
// Helper func: read integer from global Tcl var
//
static int MenuCmd_ReadTclInt( Tcl_Interp *interp,
                               CONST84 char * pName, int fallbackVal )
{
   Tcl_Obj  * pVarObj;
   int  value;

   if (pName != NULL)
   {
      pVarObj = Tcl_GetVar2Ex(interp, pName, NULL, TCL_GLOBAL_ONLY);
      if ( (pVarObj == NULL) ||
           (Tcl_GetIntFromObj(interp, pVarObj, &value) != TCL_OK) )
      {
         debug3("MenuCmd-ReadTclInt: cannot read Tcl var %s (%s) - use default val %d", pName, ((pVarObj != NULL) ? Tcl_GetString(pVarObj) : "*undef*"), fallbackVal);
         value = fallbackVal;
      }
   }
   else
   {
      fatal0("MenuCmd-ReadTclInt: illegal NULL ptr param");
      value = fallbackVal;
   }
   return value;
}
#endif

// ----------------------------------------------------------------------------
// Query the hardware configuration
//
static int MenuCmd_GetHardwareConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetHardwareConfig";
   const RCFILE  * pRc;
   Tcl_Obj * pResultList;
   uint drvType;
   int  result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pRc = RcFile_Query();
      pResultList = Tcl_NewListObj(0, NULL);

      drvType = pRc->tvcard.drv_type;
      if (drvType == BTDRV_SOURCE_UNDEF)
      {
         drvType = BtDriver_GetDefaultDrvType();
      }

      Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(pRc->tvcard.card_idx));
      Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(pRc->tvcard.input));
      Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(drvType));
      Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(pRc->tvcard.acq_prio));
      Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(pRc->tvcard.slicer_type));

      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Apply the user-configured hardware configuration
// - called when config dialog is closed
//
static int MenuCmd_UpdateHardwareConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_UpdateHardwareConfig <card-idx> <input-idx> <drv-type> <prio> <slicer>";
   RCFILE_TVCARD rcCard;
   int  cardIdx, input, drvType, prio, slicer;
   int  result;

   if (objc != 1+5)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( (Tcl_GetIntFromObj(interp, objv[1], &cardIdx) != TCL_OK) ||
             (Tcl_GetIntFromObj(interp, objv[2], &input) != TCL_OK) ||
             (Tcl_GetIntFromObj(interp, objv[3], &drvType) != TCL_OK) ||
             (Tcl_GetIntFromObj(interp, objv[4], &prio) != TCL_OK) ||
             (Tcl_GetIntFromObj(interp, objv[5], &slicer) != TCL_OK) )
   {
      result = TCL_ERROR;
   }
   else
   {
      rcCard = RcFile_Query()->tvcard;

      rcCard.drv_type = drvType;
      rcCard.card_idx = cardIdx;
      rcCard.input = input;
      rcCard.acq_prio = prio;
      rcCard.slicer_type = slicer;

      RcFile_SetTvCard(&rcCard);
      UpdateRcFile(TRUE);

      EpgSetup_CardDriver(-1);

      // update help message in listbox if database is empty
      UiControl_CheckDbState();

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Query if parameters for current card are available
//
static int MenuCmd_TclCbCheckTvCardConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_CheckTvCardConfig";
   bool isOk;
   int  result;

   if (objc != +1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
#ifndef WIN32
      isOk = TRUE;
#else
      isOk = EpgSetup_CheckTvCardConfig();
#endif
      Tcl_SetResult(interp, (isOk ? "1" : "0"), TCL_STATIC);
      result = TCL_OK;
   }
   return result;
}

// ---------------------------------------------------------------------------
// WIN32: Advise user to configure the TV card
// - returns TRUE if user confirmed start of TV card config (i.e. clicked "OK")
// - returns FALSE after cancel, or subsequent invocations
//
bool MenuCmd_PopupTvCardSetup( void )
{
   static bool haveWarned = FALSE;
   uint uiCni;
   bool result = FALSE;

   // warn only once, to avoid becoming a nuisance
   if (haveWarned == FALSE)
   {
      haveWarned = TRUE;
   
      uiCni = EpgDbContextGetCni(pUiDbContext);

      sprintf(comm, "tk_messageBox -type okcancel -default ok -icon info -parent . "
                    "-title {Welcome to nxtvepg} -message {%s%s}",
                              "Before you can start loading Nextview EPG data, you need "
                              "to configure your TV card type in the following dialog. ",
                    ((uiCni == 0) ?
                              "Afterwards please note the instructions in the main window."
                              : "")
                    );
      if (Tcl_EvalEx(interp, comm, -1, 0) == TCL_OK)
      {
         if (strcmp(Tcl_GetStringResult(interp), "ok") == 0)
         {
            // open the TV card configuration dialog; auto-enable acq after dialog is closed
            sprintf(comm, "set ::hwcf_acq_reenable 1\n"
                          "PopupHardwareConfig\n");
            eval_check(interp, comm);
            result = TRUE;
         }
      }
      else
         debugTclErr(interp, "Main-PopupTvCardSetup");
   }

   return result;
}

// ----------------------------------------------------------------------------
// Query client/server configuration
// - called when client/server configuration dialog is opened
//
static int MenuCmd_GetNetAcqConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetNetAcqConfig <array_name>";
   const RCFILE * pRc;
   char * pArrName;
   int  result;

   if (objc != 1+1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pRc = RcFile_Query();
      if (pRc != NULL)
      {
         pArrName = Tcl_GetString(objv[1]);

         Tcl_SetVar2Ex(interp, pArrName, "remctl", Tcl_NewIntObj(pRc->netacq.remctl), 0);
         Tcl_SetVar2Ex(interp, pArrName, "do_tcp_ip", Tcl_NewIntObj(pRc->netacq.do_tcp_ip), 0);
         Tcl_SetVar2Ex(interp, pArrName, "host", TRANSCODE_NON_NULL(pRc->netacq.pHostName), 0);
         Tcl_SetVar2Ex(interp, pArrName, "ip", TRANSCODE_NON_NULL(pRc->netacq.pIpStr), 0);
         Tcl_SetVar2Ex(interp, pArrName, "port", TRANSCODE_NON_NULL(pRc->netacq.pPort), 0);
         Tcl_SetVar2Ex(interp, pArrName, "max_conn", Tcl_NewIntObj(pRc->netacq.max_conn), 0);
         Tcl_SetVar2Ex(interp, pArrName, "sysloglev", Tcl_NewIntObj(pRc->netacq.sysloglev), 0);
         Tcl_SetVar2Ex(interp, pArrName, "fileloglev", Tcl_NewIntObj(pRc->netacq.fileloglev), 0);
         Tcl_SetVar2Ex(interp, pArrName, "logname", TRANSCODE_NON_NULL(pRc->netacq.pLogfileName), 0);
      }
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Apply the user-configured client/server configuration
// - called when client/server configuration dialog is closed
//
static int MenuCmd_UpdateNetAcqConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_UpdateNetAcqConfig <list>";
   RCFILE_NETACQ  rcNet;
   Tcl_DString    dstr;
   Tcl_Obj     ** cfArgv;
   int  cfArgc, cf_idx;
   const char *pHostName, *pPort, *pIpStr, *pLogfileName;
   int  do_tcp_ip, max_conn, fileloglev, sysloglev, remote_ctl;
   int  keyIndex;
   int  result;

   static CONST84 char * pKeywords[] = { "do_tcp_ip", "host", "port", "ip",
      "max_conn", "sysloglev", "fileloglev", "logname", "remctl", (char *) NULL };
   enum netcf_keys { NETCF_DO_TCP_IP, NETCF_HOSTNAME, NETCF_PORT, NETCF_IPADDR,
      NETCF_MAX_CONN, NETCF_SYSLOGLEV, NETCF_FILELOGLEV, NETCF_LOGNAME, NETCF_REMCTRL };

   if (objc != 2)
   {  // parameter count is invalid: none expected
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = Tcl_ListObjGetElements(interp, objv[1], &cfArgc, &cfArgv);
      if (result == TCL_OK)
      {
         ifdebug1(((cfArgc & 1) != 0), "MenuCmd-UpdateNetAcqConfig: warning: uneven number of params: %d", cfArgc);
         // initialize the config variables
         max_conn = fileloglev = sysloglev = 0;
         pHostName = pPort = pIpStr = pLogfileName = NULL;

         for (cf_idx = 0; (cf_idx < cfArgc) && (result == TCL_OK); cf_idx += 2)
         {
            result = Tcl_GetIndexFromObj(interp, cfArgv[cf_idx], pKeywords, "keyword", TCL_EXACT, &keyIndex);
            if (result == TCL_OK)
            {
               switch (keyIndex)
               {
                  case NETCF_DO_TCP_IP:
                     result = Tcl_GetBooleanFromObj(interp, cfArgv[cf_idx + 1], &do_tcp_ip);
                     break;
                  case NETCF_HOSTNAME:
                     pHostName = Tcl_GetString(cfArgv[cf_idx + 1]);
                     if ((pHostName != NULL) && (pHostName[0] == 0))
                        pHostName = NULL;
                     break;
                  case NETCF_PORT:
                     pPort = Tcl_GetString(cfArgv[cf_idx + 1]);
                     if ((pPort != NULL) && (pPort[0] == 0))
                        pPort = NULL;
                     break;
                  case NETCF_IPADDR:
                     pIpStr = Tcl_GetString(cfArgv[cf_idx + 1]);
                     if ((pIpStr != NULL) && (pIpStr[0] == 0))
                        pIpStr = NULL;
                     break;
                  case NETCF_MAX_CONN:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &max_conn);
                     break;
                  case NETCF_SYSLOGLEV:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &sysloglev);
                     break;
                  case NETCF_FILELOGLEV:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &fileloglev);
                     break;
                  case NETCF_LOGNAME:
                     pLogfileName = Tcl_GetString(cfArgv[cf_idx + 1]);
                     if ((pLogfileName != NULL) && (pLogfileName[0] == 0))
                        pLogfileName = NULL;
                     break;
                  case NETCF_REMCTRL:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &remote_ctl);
                     break;
                  default:
                     fatal2("MenuCmd-UpdateNetAcqConfig: unknown index %d for keyword: '%s'", keyIndex, Tcl_GetString(cfArgv[cf_idx]));
                     break;
               }
               if (result != TCL_OK)
                  debug3("MenuCmd-UpdateNetAcqConfig: keyword '%s': parse error '%s': %s", Tcl_GetString(cfArgv[cf_idx]), Tcl_GetString(cfArgv[cf_idx + 1]), Tcl_GetStringResult(interp));
            }
            else
               debug1("MenuCmd-UpdateNetAcqConfig: %s", Tcl_GetStringResult(interp));
         }

         if (result == TCL_OK)
         {
            memset(&rcNet, 0, sizeof(rcNet));
            rcNet.netacq_enable = RcFile_Query()->netacq.netacq_enable;
            rcNet.do_tcp_ip = do_tcp_ip;
            rcNet.max_conn = max_conn;
            rcNet.fileloglev = fileloglev;
            rcNet.sysloglev = sysloglev;

            if (pHostName != NULL)
            {
               pHostName = Tcl_UtfToExternalDString(NULL, pHostName, -1, &dstr);
               rcNet.pHostName = xstrdup(pHostName);
               Tcl_DStringFree(&dstr);
            }
            if (pPort != NULL)
            {
               pPort = Tcl_UtfToExternalDString(NULL, pPort, -1, &dstr);
               rcNet.pPort = xstrdup(pPort);
               Tcl_DStringFree(&dstr);
            }
            if (pIpStr != NULL)
            {
               pIpStr = Tcl_UtfToExternalDString(NULL, pIpStr, -1, &dstr);
               rcNet.pIpStr = xstrdup(pIpStr);
               Tcl_DStringFree(&dstr);
            }
            if (pLogfileName != NULL)
            {
               pLogfileName = Tcl_UtfToExternalDString(NULL, pLogfileName, -1, &dstr);
               rcNet.pLogfileName = xstrdup(pLogfileName);
               Tcl_DStringFree(&dstr);
            }
            RcFile_SetNetAcq(&rcNet);
         }
      }

      EpgSetup_NetAcq(FALSE);
      UpdateRcFile(TRUE);
   }
   return result;
}

#ifdef USE_TTX_GRABBER
// ----------------------------------------------------------------------------
// Query teletext grabber configuration
// - called when the configuration dialog is opened
//
static int MenuCmd_GetTtxConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_GetTtxConfig <array_name>";
   const RCFILE * pRc;
   char * pArrName;
   int  result;

   if (objc != 1+1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pRc = RcFile_Query();
      if (pRc != NULL)
      {
         pArrName = Tcl_GetString(objv[1]);

         Tcl_SetVar2Ex(interp, pArrName, "enable", Tcl_NewIntObj(pRc->ttx.ttx_enable), 0);
         Tcl_SetVar2Ex(interp, pArrName, "net_count", Tcl_NewIntObj(pRc->ttx.ttx_chn_count), 0);
         Tcl_SetVar2Ex(interp, pArrName, "pg_start", Tcl_NewIntObj(pRc->ttx.ttx_start_pg), 0);
         Tcl_SetVar2Ex(interp, pArrName, "pg_end", Tcl_NewIntObj(pRc->ttx.ttx_end_pg), 0);
         Tcl_SetVar2Ex(interp, pArrName, "ovpg", Tcl_NewIntObj(pRc->ttx.ttx_ov_pg), 0);
         Tcl_SetVar2Ex(interp, pArrName, "duration", Tcl_NewIntObj(pRc->ttx.ttx_duration), 0);
         Tcl_SetVar2Ex(interp, pArrName, "keep_ttx", Tcl_NewIntObj(pRc->ttx.keep_ttx_data), 0);
      }
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Apply the user-configured client/server configuration
// - called when teletext grabber configuration dialog is closed
//
static int MenuCmd_UpdateTtxConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_UpdateTtxConfig <acq_mode> <acq_auto> <list>";
   RCFILE_TTX  rxTtxGrab;
   Tcl_Obj     ** cfArgv;
   int  cfArgc, cf_idx;
   int  tmpInt;
   int  keyIndex;
   int  autoStart;
   int  result;

   static CONST84 char * pKeywords[] = { "enable", "net_count", "pg_start",
         "pg_end", "ovpg", "duration", "keep_ttx", (char *) NULL };
   enum ttxcf_keys { TTXGRAB_ENABLE, TTXGRAB_NET_COUNT, TTXGRAB_PG_START,
      TTXGRAB_PG_END, TTXGRAB_OV_PG, TTXGRAB_DURATION, TTXGRAB_KEEP };

   if ( (objc != 1+3) ||
        (Tcl_GetBooleanFromObj(interp, objv[2], &autoStart) != TCL_OK) )
   {  // parameter count is invalid: none expected
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      RcFile_SetAcqMode(Tcl_GetString(objv[1]));
      RcFile_SetAcqAutoStart(autoStart);

      result = Tcl_ListObjGetElements(interp, objv[3], &cfArgc, &cfArgv);
      if (result == TCL_OK)
      {
         ifdebug1(((cfArgc & 1) != 0), "MenuCmd-UpdateTtxConfig: warning: uneven number of params: %d", cfArgc);
         // initialize the config variables
         memset(&rxTtxGrab, 0, sizeof(rxTtxGrab));

         for (cf_idx = 0; (cf_idx < cfArgc) && (result == TCL_OK); cf_idx += 2)
         {
            result = Tcl_GetIndexFromObj(interp, cfArgv[cf_idx], pKeywords, "keyword", TCL_EXACT, &keyIndex);
            if (result == TCL_OK)
            {
               switch (keyIndex)
               {
                  case TTXGRAB_ENABLE:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &tmpInt);
                     rxTtxGrab.ttx_enable = tmpInt;
                     break;
                  case TTXGRAB_NET_COUNT:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &tmpInt);
                     rxTtxGrab.ttx_chn_count = tmpInt;
                     break;
                  case TTXGRAB_PG_START:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &tmpInt);
                     rxTtxGrab.ttx_start_pg = tmpInt;
                     break;
                  case TTXGRAB_PG_END:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &tmpInt);
                     rxTtxGrab.ttx_end_pg = tmpInt;
                     break;
                  case TTXGRAB_OV_PG:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &tmpInt);
                     rxTtxGrab.ttx_ov_pg = tmpInt;
                     break;
                  case TTXGRAB_DURATION:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &tmpInt);
                     rxTtxGrab.ttx_duration = tmpInt;
                     break;
                  case TTXGRAB_KEEP:
                     result = Tcl_GetIntFromObj(interp, cfArgv[cf_idx + 1], &tmpInt);
                     rxTtxGrab.keep_ttx_data = tmpInt;
                     break;
                  default:
                     fatal2("MenuCmd-UpdateTtxConfig: unknown index %d for keyword: '%s'", keyIndex, Tcl_GetString(cfArgv[cf_idx]));
                     break;
               }
               if (result != TCL_OK)
                  debug3("MenuCmd-UpdateTtxConfig: keyword '%s': parse error '%s': %s", Tcl_GetString(cfArgv[cf_idx]), Tcl_GetString(cfArgv[cf_idx + 1]), Tcl_GetStringResult(interp));
            }
            else
               debug1("MenuCmd-UpdateTtxConfig: %s", Tcl_GetStringResult(interp));
         }

         if (result == TCL_OK)
         {
            RcFile_SetTtxGrabOpt(&rxTtxGrab);
         }
      }

      // apply the new parameters
      EpgSetup_TtxGrabber();
      EpgSetup_AcquisitionMode(NETACQ_KEEP);

      UpdateRcFile(TRUE);

      // update help message in listbox if database is empty
      UiControl_CheckDbState();

      // enable aquisition, if auto-start enabled
      const RCFILE * pRc = RcFile_Query();
      if ( (autoStart == ACQ_START_AUTO) &&
           pRc->ttx.ttx_enable &&
           !EpgScan_IsActive() && !EpgAcqCtl_IsActive() )
      {
         AutoStartAcq();
      }
      else if (pRc->ttx.ttx_enable == FALSE)
      {
         EpgAcqCtl_Stop();
      }
   }
   return result;
}
#endif // USE_TTX_GRABBER

// ----------------------------------------------------------------------------
// Write the rc/ini file
//
static int MenuCmd_UpdateRcFile( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_UpdateRcFile";
   int  result;

   if (objc != 1)
   {  // parameter count or format is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      UpdateRcFile(TRUE);

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Return the current time as UNIX "epoch"
// - replacement for Tcl's [clock seconds] which requires an overly complicated
//   library script since 8.5
//
static int MenuCmd_ClockSeconds( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ClockSeconds";
   int  result;

   if (objc != 1)
   {  // parameter count or format is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      Tcl_SetObjResult(interp, Tcl_NewIntObj(time(NULL)));
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Format time in the given format
// - workaround for Tcl library on Windows: current locale is not used for weekdays etc.
//
static int MenuCmd_ClockFormat( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ClockFormat <time> <format>";
   long reqTimeVal;
   time_t reqTime;
   int  result;

   if ( (objc != 1+2) ||
        (Tcl_GetLongFromObj(interp, objv[1], &reqTimeVal) != TCL_OK) ||
        (Tcl_GetString(objv[2]) == NULL) )
   {  // parameter count or format is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      reqTime = (time_t) reqTimeVal;
      if (strftime(comm, sizeof(comm) - 1, Tcl_GetString(objv[2]), localtime(&reqTime)) == 0)
      {  // error
         comm[0] = 0;
      }
      Tcl_SetObjResult(interp, TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, comm, NULL));
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Initialize the module
//
void MenuCmd_Init( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_ToggleAcq", &cmdInfo) == 0)
   {  // Create callback functions
      Tcl_CreateObjCommand(interp, "C_ToggleAcq", MenuCmd_ToggleAcq, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_IsAcqEnabled", MenuCmd_IsAcqEnabled, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_IsNetAcqActive", MenuCmd_IsNetAcqActive, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_IsAcqExternal", MenuCmd_IsAcqExternal, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SetControlMenuStates", MenuCmd_SetControlMenuStates, (ClientData) NULL, NULL);

      Tcl_CreateObjCommand(interp, "C_ChangeProvider", MenuCmd_ChangeProvider, (ClientData) NULL, NULL);

      Tcl_CreateObjCommand(interp, "C_UpdateLanguage", MenuCmd_UpdateLanguage, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetCniDescription", MenuCmd_GetCniDescription, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetProvServiceName", MenuCmd_GetProvServiceName, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetProvServiceInfos", MenuCmd_GetProvServiceInfos, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetCurrentDatabaseCni", MenuCmd_GetCurrentDatabaseCni, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetProvCnisAndNames", MenuCmd_GetProvCnisAndNames, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetProvCniConfig", MenuCmd_GetProvCniConfig, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_UpdateProvCniConfig", MenuCmd_UpdateProvCniConfig, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetAiNetwopList", MenuCmd_GetAiNetwopList, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetNetwopNames", MenuCmd_GetNetwopNames, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_UpdateNetwopNames", MenuCmd_UpdateNetwopNames, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_NormalizeCni", MenuCmd_NormalizeCni, (ClientData) NULL, NULL);

      Tcl_CreateObjCommand(interp, "C_GetMergeProviderList", MenuCmd_GetMergeProviderList, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetMergeOptions", MenuCmd_GetMergeOptions, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_ProvMerge_Start", MenuCmd_ProvMerge_Start, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_UpdatePiExpireDelay", MenuCmd_UpdatePiExpireDelay, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetPiExpireDelay", MenuCmd_GetPiExpireDelay, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_CountExpiredPi", MenuCmd_CountExpiredPi, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetAcqConfig", MenuCmd_GetAcqConfig, (ClientData) NULL, NULL);

      Tcl_CreateObjCommand(interp, "C_StartEpgScan", MenuCmd_StartEpgScan, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_StopEpgScan", MenuCmd_StopEpgScan, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_SetEpgScanSpeed", MenuCmd_SetEpgScanSpeed, (ClientData) NULL, NULL);

      Tcl_CreateObjCommand(interp, "C_HwCfgScanTvCards", MenuCmd_ScanTvCards, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_HwCfgGetInputList", MenuCmd_GetInputList, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetHardwareConfig", MenuCmd_GetHardwareConfig, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_UpdateHardwareConfig", MenuCmd_UpdateHardwareConfig, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_CheckTvCardConfig", MenuCmd_TclCbCheckTvCardConfig, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_GetNetAcqConfig", MenuCmd_GetNetAcqConfig, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_UpdateNetAcqConfig", MenuCmd_UpdateNetAcqConfig, (ClientData) NULL, NULL);
#ifdef USE_TTX_GRABBER
      Tcl_CreateObjCommand(interp, "C_GetTtxConfig", MenuCmd_GetTtxConfig, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_UpdateTtxConfig", MenuCmd_UpdateTtxConfig, (ClientData) NULL, NULL);
#endif

      Tcl_CreateObjCommand(interp, "C_ClockSeconds", MenuCmd_ClockSeconds, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_ClockFormat", MenuCmd_ClockFormat, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_UpdateRcFile", MenuCmd_UpdateRcFile, (ClientData) NULL, NULL);

#ifndef USE_DAEMON
      eval_check(interp, ".menubar.config entryconfigure \"Client/Server...\" -state disabled");
#endif
   }
   else
      debug0("MenuCmd-Init: commands were already created");
}

