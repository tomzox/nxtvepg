/*
 *  M$ Windows TV application remote control module
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
 *    This module implements integration with M$ Windows TV viewing
 *    applications.  It receives channel change messages from the
 *    TV application and replies with EPG info, e.g. program title
 *    string.
 */

#ifndef WIN32
#error "This module is intended only for Win32 systems"
#else

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

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
#include "epgvbi/tvchan.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/winshmsrv.h"
#include "epgvbi/winshm.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgacqctl.h"
#include "epgui/epgmain.h"
#include "epgui/epgsetup.h"
#include "epgui/pibox.h"
#include "epgui/pidescr.h"
#include "epgui/uidump.h"
#include "epgui/dumptext.h"
#include "epgui/wintvui.h"
#include "epgui/wintv.h"


static Tcl_TimerToken   pollVpsEvent = NULL;
static Tcl_AsyncHandler asyncThreadHandler = NULL;

// default allocation size for frequency table (will grow if required)
#define CHAN_CLUSTER_SIZE  50


// ----------------------------------------------------------------------------
// Structure to hold user configuration (copy of global Tcl variables)
//
typedef struct
{
   bool     shmEnable;
   bool     tunetv;
   bool     follow;
   bool     doPop;
} WINTVCF;

static WINTVCF wintvcf = {1, 1, 1, 1};

// ----------------------------------------------------------------------------
// define struct to hold state of CNI & PIL supervision
//
static struct
{
   uint      pil;
   uint      cni;
   uint      stationPoll;
   uint      stationCni;
   uint      lastCni;
   time_t    lastStartTime;
   uint32_t  chanQueryIdx;
   uint32_t  chanEpgCnt;
} followTvState = {INVALID_VPS_PIL, 0, FALSE, 0, 0, 0, 0, 0};

// ----------------------------------------------------------------------------
// Compare given name with all network names
// - returns 0 if the given name doesn't match any known networks
// - note: function 100% identical to UNIX version
//
static uint Wintv_MapName2Cni( const char * station )
{
   const AI_BLOCK *pAiBlock;
   const char * pNetName;
   uint netwop;
   uint cni;

   cni = 0;

   EpgDbLockDatabase(pUiDbContext, TRUE);
   pAiBlock = EpgDbGetAi(pUiDbContext);
   if (pAiBlock != NULL)
   {
      for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
      {
         // get user-configured name for this network (fall-back to AI name, if none)
         pNetName = EpgSetup_GetNetName(pAiBlock, netwop, NULL);

         if ((pNetName[0] == station[0]) && (strcmp(pNetName, station) == 0))
         {
            cni  = AI_GET_NET_CNI_N(pAiBlock, netwop);
            break;
         }
      }
   }
   EpgDbLockDatabase(pUiDbContext, FALSE);

   return cni;
}

// ----------------------------------------------------------------------------
// Determine TV program by PIL or current time
//
static const PI_BLOCK * Wintv_SearchCurrentPi( uint cni, uint pil )
{
   FILTER_CONTEXT *fc;
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK *pPiBlock;
   const AI_NETWOP *pNetwop;
   time_t now;
   uint netwop;
   
   assert(EpgDbIsLocked(pUiDbContext));
   pPiBlock = NULL;
   now = time(NULL);

   pAiBlock = EpgDbGetAi(pUiDbContext);
   if (pAiBlock != NULL)
   {
      // convert the CNI parameter to a netwop index
      pNetwop = AI_GET_NETWOPS(pAiBlock);
      for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++, pNetwop++ ) 
         if (cni == AI_GET_NET_CNI(pNetwop))
            break;

      if (netwop < pAiBlock->netwopCount)
      {
         if (VPS_PIL_IS_VALID(pil))
         {
            // search the running title by its PIL
            pPiBlock = EpgDbSearchPiByPil(pUiDbContext, netwop, pil);
         }

         if (pPiBlock == NULL)
         {
            // search the running title by its running time

            fc = EpgDbFilterCreateContext();
            // filter for the given network and start time >= now
            EpgDbFilterInitNetwop(fc, pAiBlock->netwopCount);
            EpgDbFilterSetNetwop(fc, netwop);
            EpgDbFilterEnable(fc, FILTER_NETWOP);

            pPiBlock = EpgDbSearchFirstPiAfter(pUiDbContext, now, RUNNING_AT, fc);

            if ((pPiBlock != NULL) && (pPiBlock->start_time > now))
            {  // found one, but it's in the future
               debug2("Wintv-SearchCurrentPi: first PI on netwop %d is %d minutes in the future", netwop, (int)(pPiBlock->start_time - now));
               pPiBlock = NULL;
            }

            EpgDbFilterDestroyContext(fc);
         }
      }
   }

   return pPiBlock;
}

// ----------------------------------------------------------------------------
// Query database and convert PI to text for a station update
//
static char * Wintv_PiQuery( const PI_BLOCK *pPiBlock, uint count )
{
   PI_DESCR_BUF pbuf;
   FILTER_CONTEXT *fc;
   uint  idx;

   memset(&pbuf, 0, sizeof(pbuf));

   // create a search filter to return PI for the current station only
   fc = EpgDbFilterCreateContext();
   EpgDbFilterInitNetwop(fc, pUiDbContext->netwopCount);
   EpgDbFilterSetNetwop(fc, pPiBlock->netwop_no);
   EpgDbFilterEnable(fc, FILTER_NETWOP);

   for (idx = 0; (idx < count) && (pPiBlock != NULL); idx++)
   {
      if (EpgDumpText_Single(pUiDbContext, pPiBlock, &pbuf) == FALSE)
         break;

      pPiBlock = EpgDbSearchNextPi(pUiDbContext, fc, pPiBlock);
   }
   EpgDbFilterDestroyContext(fc);

   return pbuf.pStrBuf;
}

// ----------------------------------------------------------------------------
// Query if a TV app is connected
//
static bool Wintv_IsConnected( void )
{
   return ( (wintvcf.shmEnable) &&
            (WintvSharedMem_IsConnected(NULL, 0, NULL)) );
}

// ----------------------------------------------------------------------------
// Query which TV app we're connected to, if any
//
static int Wintv_IsConnected_TclCb( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   Tcl_SetObjResult(interp, Tcl_NewIntObj(Wintv_IsConnected() ? 1 : 0));
   return TCL_OK;
}

// ---------------------------------------------------------------------------
// Send an already parsed command to the remote TV application
// - the command is a list of strings separated by 0; terminated by two 0
// - used to send commands from C level (e.g. context menu)
//
void Wintv_SendCmdArgv( Tcl_Interp *interp, const char * pCmdStr, uint cmdLen )
{
   uint  cmdArgCount;
   uint  idx;

   if ( Wintv_IsConnected() )
   {
      if ((cmdLen > 0) && (pCmdStr[cmdLen - 1] == 0))
      {
         // count command arguments: count zero bytes
         cmdArgCount = 0;
         for (idx=0; idx < cmdLen; idx++)
            if (pCmdStr[idx] == '\0')
               cmdArgCount += 1;

         if (WintvSharedMem_SetEpgCommand(cmdArgCount, pCmdStr, cmdLen) == FALSE)
         {
            sprintf(comm, "tk_messageBox -type ok -icon error -parent . "
                          "-message \"Failed to send the command to the TV app.\"");
            eval_check(interp, comm);
            Tcl_ResetResult(interp);
         }
      }
      else
         fatal1("Wintv-SendCmdArgv: command length zero (%d) or not 0-terminated", cmdLen);
   }
   else
   {
      sprintf(comm, "tk_messageBox -type ok -icon error -parent . "
                    "-message \"Cannot send command: no TV application connected!\"");
      eval_check(interp, comm);
      Tcl_ResetResult(interp);
   }
}

// ---------------------------------------------------------------------------
// Send a command to the remote TV application
//
static int Wintv_SendCmd(ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[])
{
   const char * const pUsage = "Usage: C_Tvapp_SendCmd <command> [<args> [<...>]]";
   Tcl_DString *pass_dstr, *tmp_dstr;
   char * pass;
   int idx, len;
   int  result;

   if (argc < 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      // sum up the total length of all parameters, including terminating 0-Bytes
      pass_dstr = xmalloc(sizeof(Tcl_DString) * argc);  // allocate one too many
      //dprintf0("SendCmd: ");
      len = 0;
      for (idx = 1; idx < argc; idx++)
      {
         tmp_dstr = pass_dstr + idx - 1;
         // convert Tcl internal Unicode to Latin-1
         Tcl_UtfToExternalDString(NULL, argv[idx], -1, tmp_dstr);
         //dprintf1("%s ", Tcl_DStringValue(tmp_dstr));
         len += Tcl_DStringLength(tmp_dstr) + 1;
      }
      //dprintf0("\n");

      // concatenate the parameters into one char-array, separated by 0-Bytes
      pass = xmalloc(len);
      len = 0;
      pass[0] = 0;
      for (idx = 1; idx < argc; idx++)
      {
         tmp_dstr = pass_dstr + idx - 1;
         strcpy(pass + len, Tcl_DStringValue(tmp_dstr));
         len += strlen(pass + len) + 1;
         Tcl_DStringFree(tmp_dstr);
      }
      xfree(pass_dstr);

      // send the string to the TV app
      WintvSharedMem_SetEpgCommand(argc - 1, pass, len);

      xfree(pass);

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// React upon new CNI and/or PIL
// - CNI may have been acquired either through shared memory or VPS/PDC
//   PIL changes can be detected via VPS/PDC only (on networks which support it)
// - first search for a matching PI in the database
//   if not found, just remove a possible old popup
// - action depends on user configuration: can be popup and/or cursor movement
//
static void Wintv_FollowTvNetwork( void )
{
   const PI_BLOCK *pPiBlock;
   char * pPiDescr;
   
   EpgDbLockDatabase(pUiDbContext, TRUE);
   pPiBlock = Wintv_SearchCurrentPi(followTvState.cni, followTvState.pil);
   if (pPiBlock != NULL)
   {
      if ( (followTvState.cni != followTvState.lastCni) ||
           (pPiBlock->start_time != followTvState.lastStartTime) )
      {
         // remember PI to suppress double-popups
         followTvState.lastCni       = followTvState.cni;
         followTvState.lastStartTime = pPiBlock->start_time;

         // send the programme information to the TV app
         if (wintvcf.doPop)
         {
            pPiDescr = Wintv_PiQuery(pPiBlock, followTvState.chanEpgCnt);
            if (pPiDescr != NULL)
            {
               WintvSharedMem_SetEpgInfo(pPiDescr, strlen(pPiDescr) + 1,
                                         followTvState.chanQueryIdx, TRUE);
               xfree(pPiDescr);
            }
         }

         // jump with the cursor on the current programme
         if (wintvcf.follow)
            PiBox_GotoPi(pPiBlock);
      }
   }
   else
   {  // unsupported network or no appropriate PI found -> remove popup
      WintvSharedMem_SetEpgInfo("", 1, followTvState.chanQueryIdx, TRUE);
   }
   EpgDbLockDatabase(pUiDbContext, FALSE);
}

// ----------------------------------------------------------------------------
// Poll VPS/PDC for channel changes
// - invoked by a timer every 200 ms
// - if CNI or PIL has changed, EPG info for the channel is sent to the TV app
//
static void Wintv_PollVpsPil( ClientData clientData )
{
   uint  cni;
   uint  pil;

   pollVpsEvent = NULL;

   if ((wintvcf.follow || wintvcf.doPop) && (followTvState.stationPoll == 0))
   {
      if ( WintvSharedMem_GetCniAndPil(&cni, &pil) &&
           WintvUi_CheckAirTimes(cni) )
      {
         if ( (followTvState.cni != cni) ||
              ((pil != followTvState.pil) && VPS_PIL_IS_VALID(pil)) )
         {
            followTvState.pil = pil;
            followTvState.cni = cni;
            dprintf5("Wintv-PollVpsPil: %02d.%02d. %02d:%02d (0x%04X)\n", (pil >> 15) & 0x1F, (pil >> 11) & 0x0F, (pil >>  6) & 0x1F, (pil      ) & 0x3F, cni);

            Wintv_FollowTvNetwork();
         }
      }
   }
   pollVpsEvent = Tcl_CreateTimerHandler(200, Wintv_PollVpsPil, NULL);
}

// ----------------------------------------------------------------------------
// Idle handler to invoke Follow-TV from timer handler
//
static void Wintv_FollowTvHandler( ClientData clientData )
{
   Wintv_FollowTvNetwork();
   followTvState.stationCni  = 0;
   followTvState.stationPoll = 0;
}

// ----------------------------------------------------------------------------
// Timer handler for VPS polling after property notification
//
static void Wintv_StationTimer( ClientData clientData )
{
   bool keepWaiting = FALSE;
   uint  cni;
   uint  pil;

   pollVpsEvent = NULL;
   ifdebug0(followTvState.stationPoll == 0, "Wintv-StationTimer: station timer is zero");  // XXX FIXME was an assert, but reportedly failed sometimes

   if ( WintvSharedMem_GetCniAndPil(&cni, &pil) &&
        WintvUi_CheckAirTimes(cni) )
   {  // channel ID received - check if it's the expected one
      if ( (cni == followTvState.stationCni) || (followTvState.stationCni == 0) ||
           (followTvState.stationPoll >= 360) )
      {
         followTvState.cni = cni;
         followTvState.pil = pil;
         dprintf7("Wintv-StationPollVpsPil: after %d ms: 0x%04X: PIL: %02d.%02d. %02d:%02d (0x%04X)\n", followTvState.stationPoll, cni, (pil >> 15) & 0x1F, (pil >> 11) & 0x0F, (pil >>  6) & 0x1F, (pil      ) & 0x3F, cni );
      }
      else
      {  // not the expected CNI -> keep waiting
         dprintf7("Wintv-StationPollVpsPil: Waiting for 0x%04X, got 0x%04X: PIL: %02d.%02d. %02d:%02d (0x%04X)\n", followTvState.stationCni, cni, (pil >> 15) & 0x1F, (pil >> 11) & 0x0F, (pil >>  6) & 0x1F, (pil      ) & 0x3F, cni );
         keepWaiting = TRUE;
      }
   }
   else
   {  // no VPS reception -> wait some more, until limit is reached
      if (followTvState.stationPoll >= 360)
      {
         dprintf0("Wintv-StationPollVpsPil: no PIL received\n");
         followTvState.cni = followTvState.stationCni;
         followTvState.pil = INVALID_VPS_PIL;
      }
      else
         keepWaiting = TRUE;
   }

   if (keepWaiting == FALSE)
   {  // dispatch the popup from the main loop
      AddMainIdleEvent(Wintv_FollowTvHandler, NULL, TRUE);

      pollVpsEvent = Tcl_CreateTimerHandler(200, Wintv_PollVpsPil, NULL);
   }
   else
   {  // keep waiting
      pollVpsEvent = Tcl_CreateTimerHandler(120, Wintv_StationTimer, NULL);
      followTvState.stationPoll += 120;
   }
}

// ----------------------------------------------------------------------------
// Notification about channel change by TV application
// - invoked after a property notification event
//
static void Wintv_StationSelected( void )
{
   char station[50];
   bool drvEnabled, hasDriver;

   // query name of the selected TV station
   if ( WintvSharedMem_GetStation(station, sizeof(station),
                                  &followTvState.chanQueryIdx, &followTvState.chanEpgCnt) )
   {
      if (pollVpsEvent != NULL)
      {  // remove the regular polling timer - we'll install one with a higher frequency later
         Tcl_DeleteTimerHandler(pollVpsEvent);
         pollVpsEvent = NULL;
         followTvState.stationPoll = 0;
      }

      // there definitly was a station change -> remove suppression of popup
      followTvState.lastCni = 0;

      // translate the station name to a CNI by searching the network table
      followTvState.stationCni = WintvUi_StationNameToCni(station, Wintv_MapName2Cni);
      if (followTvState.stationCni != 0)
      {
         // wait for VPS/PDC to determine PIL (and confirm CNI)
         dprintf2("Wintv-StationSelected: delay EPG info for 0x%04X (\"%s\")\n", followTvState.stationCni, station);
      }
      else
      {  // unknown station name -> remove existing popup
         // note that multi-network station may be identified via VPS shortly after
         dprintf1("Wintv-StationSelected: unknown station: \"%s\"\n", station);
      }

      // reset EPG decoder if acq is using the TTX stream provided by the TV app
      BtDriver_GetState(&drvEnabled, &hasDriver, NULL);
      if (drvEnabled && !hasDriver)
      {
         EpgAcqCtl_ResetVpsPdc();
      }

      followTvState.stationPoll = 120;
      pollVpsEvent = Tcl_CreateTimerHandler(120, Wintv_StationTimer, NULL);
   }

   if (pollVpsEvent == NULL)
      pollVpsEvent = Tcl_CreateTimerHandler(200, Wintv_PollVpsPil, NULL);
}

// ---------------------------------------------------------------------------
// Tcl callback to send a command to connected TV application
//
static int Wintv_ShowEpg(ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[])
{
   const char * const pUsage = "Usage: C_Tvapp_ShowEpg";
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      Wintv_StationSelected();
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Query which TV app we're connected to, if any
//
static int Wintv_QueryTvapp( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   char  tvAppName[100];

   if (wintvcf.shmEnable)
   {
      if (WintvSharedMem_IsConnected(tvAppName, sizeof(tvAppName), NULL))
      {
         Tcl_SetResult(interp, tvAppName, TCL_VOLATILE);
      }
   }

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Read user configuration from Tcl global variables
// 
static int Wintv_ReadConfig( Tcl_Interp *interp, WINTVCF *pNewWintvcf )
{
   const char    * pTmpStr;
   CONST84 char ** cfArgv;
   int shmEnable, tunetv, follow, doPop;
   int cfArgc, idx;
   int result = TCL_ERROR;

   memset(pNewWintvcf, 0, sizeof(*pNewWintvcf));

   pTmpStr = Tcl_GetVar(interp, "wintvcf", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      result = Tcl_SplitList(interp, pTmpStr, &cfArgc, &cfArgv);
      if (result == TCL_OK)
      {
         if ((cfArgc & 1) == 0)
         {
            // copy old values into "int" variables for Tcl conversion funcs
            shmEnable = wintvcf.shmEnable;
            tunetv    = wintvcf.tunetv;
            follow    = wintvcf.follow;
            doPop     = wintvcf.doPop;

            // parse config list; format is pairs of keyword and value
            for (idx=0; (idx + 1 < cfArgc) && (result == TCL_OK); idx += 2)
            {
               if (strcmp("shm", cfArgv[idx]) == 0)
                  result = Tcl_GetInt(interp, cfArgv[idx + 1], &shmEnable);
               else if (strcmp("tunetv", cfArgv[idx]) == 0)
                  result = Tcl_GetBoolean(interp, cfArgv[idx + 1], &tunetv);
               else if (strcmp("follow", cfArgv[idx]) == 0)
                  result = Tcl_GetBoolean(interp, cfArgv[idx + 1], &follow);
               else if (strcmp("dopop", cfArgv[idx]) == 0)
                  result = Tcl_GetBoolean(interp, cfArgv[idx + 1], &doPop);
               else
               {
                  sprintf(comm, "C_Wintv_ReadConfig: unknown config type: %s", cfArgv[idx]);
                  Tcl_SetResult(interp, comm, TCL_VOLATILE);
                  result = TCL_ERROR;
               }
            }

            if (result == TCL_OK)
            {
               if (pNewWintvcf != NULL)
               {  // all went well -> copy new values into the config struct
                  pNewWintvcf->shmEnable = shmEnable;
                  pNewWintvcf->tunetv    = tunetv;
                  pNewWintvcf->follow    = follow;
                  pNewWintvcf->doPop     = doPop;
               }
               else
                  debug0("Wintv-ReadConfig: illegal NULL param");
            }
         }
         else
         {
            sprintf(comm, "C_Wintv_ReadConfig: odd number of list elements: %s", pTmpStr);
            Tcl_SetResult(interp, comm, TCL_VOLATILE);
            result = TCL_ERROR;
         }
         Tcl_Free((char *) cfArgv);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Read user configuration from Tcl global variables
// - called during init and after user config changes
// - loads config from rc/ini file
//
static int Wintv_InitConfig( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   const char * pShmErrMsg;
   WINTVCF newWintvCf;
   bool acqEnabled;
   int result;

   if (pollVpsEvent != NULL)
   {
      Tcl_DeleteTimerHandler(pollVpsEvent);
      pollVpsEvent = NULL;
   }

   // read config from global Tcl variables
   result = Wintv_ReadConfig(interp, &newWintvCf);
   if (result == TCL_OK)
   {  // parsed ok -> copy new config over the old
      wintvcf = newWintvCf;
   }

   // during start-up automatic attach may be suppressed
   if ( PVOID2INT(ttp) == FALSE )
   {  // FIXME: this state is not properly reflected by the dialog (still appears enabled)
      wintvcf.shmEnable = FALSE;
   }

   if (WintvSharedMem_StartStop(wintvcf.shmEnable, &acqEnabled) == FALSE)
   {  // failed to enable the shared memory server
      wintvcf.shmEnable = FALSE;

      pShmErrMsg = WinSharedMem_GetErrorMsg();
      if (pShmErrMsg != NULL)
      {
         if (Tcl_VarEval(interp, "tk_messageBox -type ok -icon error -message {", pShmErrMsg, "}", NULL) != TCL_OK)
            debugTclErr(interp, "Wintv-InitConfig msgbox");

         xfree((void *) pShmErrMsg);
      }
   }
   // switch off acquisition (if enabled) if the switch failed
   if (acqEnabled == FALSE)
      EpgAcqCtl_Stop();

   // display the "Tune TV" button in the main window is an app is attached
   if ( (wintvcf.tunetv) && (wintvcf.shmEnable) && Wintv_IsConnected() )
   {
      eval_check(interp, "CreateTuneTvButton\n");
   }
   else
      eval_check(interp, "RemoveTuneTvButton\n");

   if ( (wintvcf.shmEnable) && (wintvcf.follow || wintvcf.doPop) )
   {
      // create a timer to regularily poll for VPS/PDC
      pollVpsEvent = Tcl_CreateTimerHandler(200, Wintv_PollVpsPil, NULL);
   }

   return result;
}

// ---------------------------------------------------------------------------
// Trigger TV event processing
// - the handler checks the status inside shared memory and invokes callback
//   functions for all elements which values have changed
//
static void Wintv_HandleTvCmd( ClientData clientData )
{
   if (wintvcf.shmEnable)
   {
      WintvSharedMem_HandleTvCmd();
   }
}

// ---------------------------------------------------------------------------
// Called by asnychronous event handler after TV app message was received
// - triggered by the below function; please see explanation there
//
static int Wintv_AsyncThreadHandler( ClientData clientData, Tcl_Interp *interp, int code )
{
   AddMainIdleEvent(Wintv_HandleTvCmd, NULL, TRUE);

   return code;
}

// ---------------------------------------------------------------------------
// Called by TV event receptor thread
// - this function is executed in another thread - nothing must be done here
//   except inserting an event in the main loop event queue; the actual work
//   is then done in the idle event inside the main thread
// - note that there are 2 stages until the message is actually handled:
//   1. install an async. event at the top of the event queue; it'll get executed
//      A.S.A.P., i.e. next time the Tcl event loop is entered
//   2. in the async handler an idle handler is installed, which delays the
//      actual handling until the current actions are completed
//   This is neccessary to avoid that we draw something in the EPG message
//   handler, which is then overwritten by an action that was already scheduled
//   earlier (i.e. EPG info is cleared again by a timer event in the queue)
//
static void Wintv_CbTvEvent( void )
{
   if (asyncThreadHandler != NULL)
   {  // trigger an event, so that the Tcl/Tk event handler immediately wakes up
      Tcl_AsyncMark(asyncThreadHandler);
   }
}

// ---------------------------------------------------------------------------
// Incoming TV app message: TV channel was changed
//
static void Wintv_CbStationSelected( void )
{
   Wintv_StationSelected();
}

// ---------------------------------------------------------------------------
// Incoming EPG query
//
static void Wintv_CbEpgQuery( void )
{
   char  buf[EPG_CMD_MAX_LEN];

   if ( WintvSharedMem_GetEpgQuery(buf, sizeof(buf)) )
   {
      // TODO
   }
}

// ---------------------------------------------------------------------------
// Incoming TV app message: TV tuner was granted or reposessed
// - the enable parameter is currently not used because the check function
//   detects this automatically upon attempting to change the channel
//
static void Wintv_CbTunerGrant( bool enable )
{
   bool drvEnabled, hasDriver;

   BtDriver_GetState(&drvEnabled, &hasDriver, NULL);

   if (drvEnabled && !hasDriver)
   { // have the acq control update it's device state
      EpgAcqCtl_CheckDeviceAccess();
   }
}

// ---------------------------------------------------------------------------
// TV application has attached or detached from nxtvepg
//
static void Wintv_CbAttachTv( bool enable, bool acqEnabled, bool slaveStateChange )
{
   uint tvFeatures;

   if (acqEnabled)
   {  // update acquisition status, e.g. set to "forced passive" if TV card is owned by TV app now
      dprintf2("Wintv-CbAttachTv: TV app attached: acq=%d, slave-change=%d\n", acqEnabled, slaveStateChange);
      if (slaveStateChange)
      {
         EpgAcqCtl_CheckDeviceAccess();
      }
   }
   else
   {  // switch off acquisition (if enabled) if the driver restart failed in the new mode
      // (e.g. invalid TV card index was configured while in slave mode)
      EpgAcqCtl_Stop();
   }

   // display or remove TuneTV button from main window
   if ( (wintvcf.shmEnable) && (wintvcf.tunetv) && (enable) )
   {
      eval_check(interp, "CreateTuneTvButton\n");
   }
   else
      eval_check(interp, "RemoveTuneTvButton\n");

   // add "record" button to context menu if supported by TV app
   if ( (wintvcf.shmEnable) && (enable) &&
        (WintvSharedMem_IsConnected(NULL, 0, &tvFeatures)) &&
        ((tvFeatures & TVAPP_FEAT_VCR) != 0) )
   {
      eval_check(interp, "ContextMenuAddWintvVcr\n");
   }

   // update TV app name in TV interaction config dialog (if currently open)
   sprintf(comm, "XawtvConfigShmAttach %d\n", enable);
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Struct with callback functions for shared memory server module
//
static const WINSHMSRV_CB winShmSrvCb =
{
   Wintv_CbTvEvent,
   Wintv_CbStationSelected,
   Wintv_CbEpgQuery,
   Wintv_CbTunerGrant,
   Wintv_CbAttachTv
};

// ----------------------------------------------------------------------------
// Shut the module down
//
void Wintv_Destroy( void )
{
   if (asyncThreadHandler != NULL)
   {
      Tcl_AsyncDelete(asyncThreadHandler);
      asyncThreadHandler = NULL;
   }

   if (pollVpsEvent != NULL)
   {
      Tcl_DeleteTimerHandler(pollVpsEvent);
      pollVpsEvent = NULL;
   }
}

// ----------------------------------------------------------------------------
// Initialize the module
// - note that the wintvcfg.c module must be initialized before
// - boolean param can be used to start with interaction disabled (override config)
//   currently used when another nxtvepg instance is already running to avoid err.msg.
//
void Wintv_Init( bool enable )
{
   // create an asynchronous event source that allows to receive triggers from the TV message receptor thread
   asyncThreadHandler = Tcl_AsyncCreate(Wintv_AsyncThreadHandler, NULL);

   // Create callback functions
   Tcl_CreateCommand(interp, "C_Tvapp_InitConfig", Wintv_InitConfig, INT2PVOID(TRUE), NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_SendCmd", Wintv_SendCmd, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_ShowEpg", Wintv_ShowEpg, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_QueryTvapp", Wintv_QueryTvapp, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_IsConnected", Wintv_IsConnected_TclCb, (ClientData) NULL, NULL);

   WintvSharedMem_SetCallbacks(&winShmSrvCb);

   // read user config and initialize local vars
   Wintv_InitConfig(INT2PVOID(enable), interp, 0, NULL);
}

#endif  // not WIN32

