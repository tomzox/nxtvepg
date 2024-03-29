/*
 *  Tcl interface and helper functions to TV app configuration
 *
 *  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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
 *    This module contains support functions for the TV application
 *    interaction configuration dialog.  In other words, it's mainly
 *    a wrapper to wintvcfg.c and the configuration data in rcfile.c
 *
 *    The module also contains a few common subroutines for the UNIX
 *    and WIN32 TV interaction modules for lack of a better place.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#ifdef WIN32
#include <windows.h>
#else
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgvbi/tvchan.h"
#ifdef WIN32
#include "epgvbi/winshm.h"
#endif
#include "epgctl/epgacqctl.h"
#include "epgui/rcfile.h"
#include "epgui/epgmain.h"
#include "epgui/epgsetup.h"
#include "epgui/uictrl.h"
#include "epgui/wintvcfg.h"
#include "epgui/wintvui.h"

#include "epgtcl/dlg_xawtvcf.h"


// ----------------------------------------------------------------------------
// Checks if the given time is outside or air times restrictions for the given CNI
// - returns TRUE if the time is inside contraints
//
bool WintvUi_CheckAirTimes( uint cni )
{
   const char * pAirStr;
   char  cnibuf[16+2+1];
   time_t now;
   uint  nowMoD, startMoD, stopMoD;
   struct tm * pTm;
   bool  result = TRUE;

   sprintf(cnibuf, "0x%04X", cni);
   pAirStr = Tcl_GetVar2(interp, "cfnettimes", cnibuf, TCL_GLOBAL_ONLY);
   if (pAirStr != NULL)
   {
      if (sscanf(pAirStr, "%u,%u", &startMoD, &stopMoD) == 2)
      {
         now = time(NULL);
         pTm = localtime(&now);
         nowMoD = pTm->tm_min + (pTm->tm_hour * 60);

         if (startMoD < stopMoD)
         {
            result = (nowMoD >= startMoD) && (nowMoD < stopMoD);
         }
         else if (startMoD > stopMoD)
         {
            result = (nowMoD >= startMoD) || (nowMoD < stopMoD);
         }
         // else: start==stop: not filtered
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Iterate across multiple network names in a channel name, separated by slashes
// - gets pointer to a callback function which searches names in a database
//   (callback required to avoid dependency of this module on the EPG database
//   since the module is also used by tvsim)
//
uint WintvUi_StationNameToCni( char * pName, uint MapName2Cni(const char * station) )
{
   char * ps, * pe, * pc;
   char  c;
   uint  cni;

   if (pName != NULL)
   {
      // append the name as-is to the result
      cni = MapName2Cni(pName);
      if ((cni != 0) && WintvUi_CheckAirTimes(cni))
         goto found;

      ps = pName;
      pe = strchr(ps, '/');
      if (pe != NULL)
      {
         do
         {
            pc = pe;
            // remove trailing white space
            while ((pc > ps) && (*(pc - 1) == ' '))
                pc--;
            if (pc > ps)
            {
               c = *pc;
               *pc = 0;

               cni = MapName2Cni(ps);
               if ((cni != 0) && WintvUi_CheckAirTimes(cni))
                  goto found;
               *pc = c;
            }
            ps = pe + 1;
            // skip whitespace following the separator
            while (*ps == ' ')
                ps++;
         }
         while ((pe = strchr(ps, '/')) != NULL);

         // add the segment after the last separator
         while (*ps == ' ')
            ps++;
         if (*ps != 0)
         {
            cni = MapName2Cni(ps);
            if ((cni != 0) && WintvUi_CheckAirTimes(cni))
               goto found;
         }
      }
   }
   else
      debug0("WintvUi-StationNameToCni: illegal NULL param");

   cni = 0;
found:
   return cni;
}

// ----------------------------------------------------------------------------
// Return list of names of supported TV applications
//
static int WintvUi_GetTvappList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_GetTvappList";
   Tcl_Obj * pResultList;
   uint appIdx;
   const char * pAppName;
   int   result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char*) pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);

      for (appIdx=0; appIdx < TVAPP_COUNT; appIdx++)
      {
         if ( WintvCfg_QueryApp(appIdx, &pAppName, NULL) )
         {
            Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(pAppName, -1));
         }
      }
      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Return default TV application
// - searches for the first TV app in table for which a config file is present
//
static int WintvUi_GetDefaultApp( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_GetDefaultApp";
#ifndef WIN32
   const char * pChanTabPath;
   struct stat fstat;
   time_t max_ts;
   uint   max_idx;
   uint   appIdx;
#endif
   int   result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char*) pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
#ifndef WIN32
      max_idx = TVAPP_NONE;
      max_ts = 0;
      for (appIdx=0; appIdx < TVAPP_COUNT; appIdx++)
      {
         pChanTabPath = WintvCfg_GetRcPath(NULL, appIdx);
         if (pChanTabPath != NULL)
         {
            // select last modified of all found TV app config files
            if ( (stat(pChanTabPath, &fstat) == 0) &&
                 S_ISREG(fstat.st_mode) &&
                 (fstat.st_mtime > max_ts) )
            {
               max_ts  = fstat.st_mtime;
               max_idx = appIdx;
            }
            xfree((void *)pChanTabPath);
         }
      }

      if (max_idx != TVAPP_NONE)
      {
         Tcl_SetObjResult(interp, Tcl_NewIntObj(max_idx));
      }
      else
#endif
         Tcl_SetObjResult(interp, Tcl_NewIntObj(TVAPP_NONE));

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Add a channel name to a Tcl result list
// - assumes that leading and trailing spaces have already been stripped
//
static void WintvUi_TclAddChannelName( Tcl_Interp * interp, const char * pChanName, bool enableSplit )
{
   Tcl_DString dstr;
   char *pName, *ps, *pc, *pe, c;

   // convert string from system encoding into UTF-8
   Tcl_ExternalToUtfDString(NULL, pChanName, -1, &dstr);
   pName = Tcl_DStringValue(&dstr);

   // append the name as-is to the result
   Tcl_AppendElement(interp, pName);

   if (enableSplit)
   {
      // split multi-channel names at separator '/' and append each segment separately
      ps = pName;
      pe = strchr(ps, '/');
      if (pe != NULL)
      {
         do
         {
            pc = pe;
            // remove trailing white space
            while ((pc > ps) && (*(pc - 1) == ' '))
                pc--;
            if (pc > ps)
            {
               c = *pc;
               *pc = 0;
               Tcl_AppendElement(interp, ps);
               *pc = c;
            }
            ps = pe + 1;
            // skip whitespace following the separator
            while (*ps == ' ')
                ps++;
         }
         while ((pe = strchr(ps, '/')) != NULL);

         // add the segment after the last separator
         while (*ps == ' ')
            ps++;
         if (*ps != 0)
            Tcl_AppendElement(interp, ps);
      }
   }
   Tcl_DStringFree(&dstr);
}

// ----------------------------------------------------------------------------
// Get channel names listed in TV app configuration file as Tcl list
//
static int WintvUi_GetStationNames( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_GetStationNames <split=0/1>";
   const TVAPP_CHAN_TAB * pChanTab;
   const char * pName;
   uint    chanIdx;
   int     enableSplit;
   int     result;

   if ( (objc != 1+1) ||
        (Tcl_GetBooleanFromObj(interp, objv[1], &enableSplit) != TCL_OK) )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pChanTab = WintvCfg_GetFreqTab(NULL);
      if (pChanTab != NULL)
      {
         // copy the names into a Tcl result list
         pName = pChanTab->pNameTab;
         for (chanIdx = 0; chanIdx < pChanTab->chanCount; chanIdx++)
         {
            if ( (*pName != 0) || (enableSplit == FALSE) )
            {
               WintvUi_TclAddChannelName(interp, pName, enableSplit);
            }
            pName += strlen(pName) + 1;
         }
      }

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Load configuration params from TV app ini file into the dialog
//
static int WintvUi_CfgNeedsPath( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_CfgNeedsPath: <tvAppIdx> [varDefaultPath]";
   char * pPath;
   char * pVarName;
   int appIdx;
   int pathType;
   int result;

   if ( ((objc != 2) && (objc != 3)) ||
        (Tcl_GetIntFromObj(interp, objv[1], &appIdx) != TCL_OK) )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char*) pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (WintvCfg_QueryApp(appIdx, NULL, &pathType) == FALSE)
      {
         debug1("WintvUi-CfgNeedsPath: invalid TV app index %d", appIdx);
         pathType = EPGTCL_TVAPP_PATH_TYPE_NONE;
      }

      if (objc == 3)
      {
         pVarName = Tcl_GetString(objv[2]);

         pPath = WintvCfg_GetRcPath(NULL, appIdx);
         if (pPath != NULL)
         {
            Tcl_SetVar2Ex(interp, pVarName, NULL, Tcl_NewStringObj(pPath, strlen(pPath)), 0);
            xfree(pPath);
         }
      }

      Tcl_SetObjResult(interp, Tcl_NewIntObj(pathType));

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Tcl callback to check if a TV app is configured
//
static int WintvUi_Enabled( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_Enabled";
   bool is_enabled;
   int result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      is_enabled = (WintvCfg_GetAppIdx() != TVAPP_NONE);

      Tcl_SetObjResult(interp, Tcl_NewBooleanObj(is_enabled));
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Test-read of the TV channel table
// - mainly used for the "Test" button in the "TV. app interaction" config dialog;
//   in this case param "showerr" is TRUE and diagnostics are prnted
// - also used for silent checks if a TV channel table is available; in this case
//   the result code reflects the result: returns number of channels read, or -1
//
static int WintvUi_TestChanTab( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_TestChanTab: <tvAppIdx> <path>";
   Tcl_Obj * pResultList;
   Tcl_Obj * pResultMsg;
   const char * pChanTabPath;
   const char * pAppName;
   const char * pTvAppPath;
   char * pErrMsg;
   int    needPath;
   int    newAppIdx;
   int    chnCount;
   int    result;

   if ( (objc != 1+2) ||
        (Tcl_GetIntFromObj(interp, objv[1], &newAppIdx) != TCL_OK) )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char*) pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);
      pTvAppPath = Tcl_GetString(objv[2]);
      pResultMsg = Tcl_NewStringObj("", 0);
      chnCount = -1;

      if ( (newAppIdx == TVAPP_NONE) ||
           (WintvCfg_QueryApp(newAppIdx, &pAppName, &needPath) == FALSE) )
      {  // no TV app selected yet (i.e. option "none" selected)
         const char * pMsg = "Please select a TV application from which to load the channel table.";
         Tcl_SetStringObj(pResultMsg, pMsg, strlen(pMsg));
      }
      else
      {
         if ((needPath == EPGTCL_TVAPP_PATH_TYPE_NONE) ||
             ((pTvAppPath != NULL) && (*pTvAppPath != 0)))
         {
            pChanTabPath = WintvCfg_GetRcPath(pTvAppPath, newAppIdx);
            if (pChanTabPath != NULL)
            {
               pErrMsg = NULL;
               if (WintvCfg_GetChanTab(newAppIdx, pChanTabPath, &pErrMsg, NULL, NULL, (uint*)&chnCount))
               {
                  // sucessfully opened: return number of found names
               }
               if (pErrMsg != NULL)
               {
                  Tcl_SetStringObj(pResultMsg, pErrMsg, strlen(pErrMsg));
                  xfree(pErrMsg);
               }
               xfree((void *)pChanTabPath);
            }
         }
         else
         {  // no TV app directory specified -> abort with error msg
            const char * pMsg = (needPath == EPGTCL_TVAPP_PATH_TYPE_DIR) ?
                                "Please specify the directory where the channel table is located." :
                                "Please specify the location of your channel configuration file (usually named channels.conf)";
            Tcl_SetStringObj(pResultMsg, pMsg, strlen(pMsg));
         }
      }

      Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(chnCount));
      Tcl_ListObjAppendElement(interp, pResultList, pResultMsg);
      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Query parameters for the TV application configuration dialog
// - called when "TV app. interaction" configuration dialog is opened
//
static int WintvUi_GetConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_GetConfig";
   const RCFILE * pRc;
   const char * appPath;
   uint appType;
   Tcl_Obj * pResultList;
   int  result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pRc = RcFile_Query();
#ifdef WIN32
      appType = pRc->tvapp.tvapp_win;
      appPath = pRc->tvapp.tvpath_win;
#else
      appType = pRc->tvapp.tvapp_unix;
      appPath = pRc->tvapp.tvpath_unix;
#endif

      pResultList = Tcl_NewListObj(0, NULL);
      Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(appType));

      if (appPath != NULL)
         Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, appPath, NULL));
      else
         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj("", 0));

      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Apply the user-configured TV app configuration
// - called when "TV app. interaction" configuration dialog is closed
//
static int WintvUi_UpdateConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_UpdateConfig: <tvAppIdx> <path>";
   int appIdx;
   int result;

   if ( (objc != 1+2) ||
        (Tcl_GetIntFromObj(interp, objv[1], &appIdx) != TCL_OK) )
   {
      Tcl_SetResult(interp, (char*) pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (WintvCfg_QueryApp(appIdx, NULL, NULL))
      {
         WintvCfg_InvalidateCache();

         // note: destinction between UNIX and WIN32 config is inside
         RcFile_SetTvApp(appIdx, Tcl_GetString(objv[2]));

         // reconfigure the TTX grabber as it depends on the channel table
         if (EpgSetup_AcquisitionMode(NETACQ_KEEP) == FALSE)
         {
            EpgAcqCtl_Stop();
         }

         // in case channel table changed
         if (RcFile_Query()->db.auto_merge_ttx)
         {
            // FIXME need to redo merge in case channel count was reduced
            UiControlMsg_AcqEvent(ACQ_EVENT_NEW_DB);
         }
      }
      else
         debug1("WintvUi-UpdateConfig: cannot save invalid index %d", appIdx);

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Shut the module down: free resources
//
void WintvUi_Destroy( void )
{
}

// ----------------------------------------------------------------------------
// Initialize the module
//
void WintvUi_Init( void )
{
   Tcl_CreateObjCommand(interp, "C_Tvapp_Enabled", WintvUi_Enabled, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_GetStationNames", WintvUi_GetStationNames, (ClientData) NULL, NULL);
   //Tcl_CreateObjCommand(interp, "C_Tvapp_GetChannelTable", WintvUi_GetChannelTable, (ClientData) NULL, NULL);

   Tcl_CreateObjCommand(interp, "C_Tvapp_GetTvappList", WintvUi_GetTvappList, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_TestChanTab", WintvUi_TestChanTab, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_CfgNeedsPath", WintvUi_CfgNeedsPath, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_GetDefaultApp", WintvUi_GetDefaultApp, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_GetConfig", WintvUi_GetConfig, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_UpdateConfig", WintvUi_UpdateConfig, (ClientData) NULL, NULL);
}

