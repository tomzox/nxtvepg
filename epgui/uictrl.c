/*
 *  General GUI housekeeping
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
 *    This module takes background messages from lower layers for the GUI,
 *    schedules them via the main loop and then displays a text in a
 *    message box.  The module also manages side-effects from switching the
 *    browser database.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: uictrl.c,v 1.28 2002/05/19 22:01:05 tom Exp $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <string.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgctl/epgctxmerge.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgscan.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgui/epgmain.h"
#include "epgui/pifilter.h"
#include "epgui/pilistbox.h"
#include "epgui/uictrl.h"
#include "epgui/menucmd.h"
#include "epgui/statswin.h"
#include "epgui/timescale.h"
#include "epgctl/epgctxctl.h"


typedef struct
{
   uint                     cni;
   EPGDB_RELOAD_RESULT      dberr;
   CONTEXT_RELOAD_ERR_HAND  errHand;
   bool                     isNewDb;
} MSG_RELOAD_ERR;


static bool uiControlInitialized = FALSE;


// ---------------------------------------------------------------------------
// Determine state of UI database and acquisition
// - this is used to display a helpful message in the PI listbox as long as
//   the database is empty
//
static EPGDB_STATE UiControl_GetDbState( void )
{
   EPGACQ_DESCR acqState;
   EPGDB_STATE  dbState;
   uint  uiCni;
   bool  acqWorksOnUi;

   uiCni = EpgDbContextGetCni(pUiDbContext);
   EpgAcqCtl_DescribeAcqState(&acqState);

   if (uiCni == 0)
   {  // no AI block in current UI database
      if (acqState.state == ACQDESCR_SCAN)
      {
         dbState = EPGDB_WAIT_SCAN;
      }
      else if (EpgContextCtl_GetProvCount() == 0)
      {
         if ( (acqState.state != ACQDESCR_DISABLED) &&
              (acqState.mode == ACQMODE_FORCED_PASSIVE) &&
              (acqState.passiveReason == ACQPASSIVE_NO_TUNER) )
            dbState = EPGDB_PROV_WAIT;
         else
            dbState = EPGDB_PROV_SCAN;
      }
      else
         dbState = EPGDB_PROV_SEL;
   }
   else
   {  // AI present, but no PI in database

      // check if acquisition is working for the browser database
      if ( (acqState.state == ACQDESCR_NET_CONNECT) ||
           (acqState.state == ACQDESCR_DISABLED) ||
           (acqState.state == ACQDESCR_SCAN) ||
           (acqState.dbCni == 0) )
         acqWorksOnUi = FALSE;
      else if ( EpgDbContextIsMerged(pUiDbContext) )
         acqWorksOnUi = EpgContextMergeCheckForCni(pUiDbContext, acqState.dbCni);
      else
         acqWorksOnUi = (acqState.dbCni == uiCni);

      if (acqState.state == ACQDESCR_NET_CONNECT)
      {  // in network acq mode: no stats available yet
         dbState = EPGDB_ACQ_WAIT_DAEMON;
      }
      else if (acqState.state == ACQDESCR_DISABLED)
      {
         dbState = EPGDB_EMPTY;
      }
      else if (acqState.state == ACQDESCR_SCAN)
      {
         dbState = EPGDB_WAIT_SCAN;
      }
      else if ((acqState.mode == ACQMODE_PASSIVE) || (acqState.mode == ACQMODE_EXTERNAL))
      {
         if ((acqState.state == ACQDESCR_RUNNING) && acqWorksOnUi)
         {  // note: this state should actually never be reached, because once acq has reached
            // "running" state an AI has been received and usually some PI too, so the UI db
            // would no longer be empty
            dbState = EPGDB_ACQ_WAIT;
         }
         else
            dbState = EPGDB_ACQ_PASSIVE;
      }
      else if (acqState.mode == ACQMODE_FORCED_PASSIVE)
      {
         // translate forced-passive reason to db state
         switch (acqState.passiveReason)
         {
            case ACQPASSIVE_NO_TUNER:
               dbState = EPGDB_ACQ_NO_TUNER;
               break;
            case ACQPASSIVE_NO_FREQ:
               dbState = EPGDB_ACQ_NO_FREQ;
               break;
            case ACQPASSIVE_NO_DB:
               dbState = EPGDB_ACQ_PASSIVE;
               break;
            case ACQPASSIVE_ACCESS_DEVICE:
               dbState = EPGDB_ACQ_ACCESS_DEVICE;
               break;
            default:
               fatal1("EpgAcqCtl-GetDbState: illegal state %d", acqState.passiveReason);
               dbState = EPGDB_ACQ_PASSIVE;
               break;
         }
      }
      else if ( (ACQMODE_IS_CYCLIC(acqState.mode) == FALSE) || acqWorksOnUi )
      {  // acq is running for the same provider as the UI
         dbState = EPGDB_ACQ_WAIT;
      }
      else if (acqState.isNetAcq)
      {  // acq not working for ui db AND in (non-follow-ui) network acq mode
         // (note: if the ui db is empty but somewhere in the acq CNI list, it's automatically
         // moved to the front during the provider change - however the acq daemon does not
         // care about the order of requested CNIs unless in follow-ui mode)
         dbState = EPGDB_ACQ_WAIT_DAEMON;
      }
      else
      {  // acq not working for ui db AND the ui db is not among the acq CNIs
         dbState = EPGDB_ACQ_OTHER_PROV;
      }
   }

   return dbState;
}

// ---------------------------------------------------------------------------
// Determine state of UI database and acquisition
// - this is used to display a helpful message in the PI listbox as long as
//   the database is empty
//
void UiControl_CheckDbState( void )
{
   FILTER_CONTEXT *pPreFilterContext;
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK *pPiBlock;
   EPGDB_STATE state;
   int filterMask;

   if (pUiDbContext != NULL)
   {
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      if (pAiBlock == NULL)
      {  // no AI block in current database
         state = UiControl_GetDbState();
      }
      else
      {  // provider present -> check for PI
         pPreFilterContext = EpgDbFilterCopyContext(pPiFilterContext);
         filterMask = pPreFilterContext->enabledFilters;
         // disable all filters except the expire time
         EpgDbFilterDisable(pPreFilterContext, FILTER_ALL & ~FILTER_EXPIRE_TIME);

         pPiBlock = EpgDbSearchFirstPi(pUiDbContext, pPreFilterContext);
         if (pPiBlock == NULL)
         {  // no PI in database (probably all expired)
            state = UiControl_GetDbState();
         }
         else
         {
            if (filterMask & FILTER_NETWOP_PRE)
            {  // re-enable the network pre-filter
               EpgDbFilterEnable(pPreFilterContext, FILTER_NETWOP_PRE);
            }

            pPiBlock = EpgDbSearchFirstPi(pUiDbContext, pPreFilterContext);
            if (pPiBlock == NULL)
            {  // PI present, but none match the prefilter
               state = EPGDB_PREFILTERED_EMPTY;
            }
            else
            {  // everything is ok
               state = EPGDB_OK;
            }
         }
         EpgDbFilterDestroyContext(pPreFilterContext);
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
   }
   else
      state = EPGDB_NOT_INIT;

   PiListBox_UpdateState(state);
}

// ----------------------------------------------------------------------------
// Update UI after possible change of netwop list
// - called as idle function after AI update or when initial AI received
// - if the browser is still empty, it takes over the acq db
// - else just update the provider name and network list
//
void UiControl_AiStateChange( ClientData clientData )
{
   uint target = (uint) clientData;
   const AI_BLOCK *pAiBlock;

   if ( (EpgDbContextGetCni(pUiDbContext) == 0) &&
        (EpgDbContextGetCni(pAcqDbContext) != 0) &&
        (EpgScan_IsActive() == FALSE) )
   {  // UI db is completely empty (should only happen if there's no provider at all)
      dprintf1("UiControl-AiStateChange: browser db empty, switch to acq db 0x%04X\n", EpgDbContextGetCni(pAcqDbContext));
      // switch browser to acq db
      EpgContextCtl_Close(pUiDbContext);
      pUiDbContext = EpgContextCtl_Open(EpgDbContextGetCni(pAcqDbContext), CTX_FAIL_RET_DUMMY, CTX_RELOAD_ERR_ANY);

      // update acq CNI list in case follow-ui acq mode is selected
      SetAcquisitionMode(NETACQ_KEEP);
      // display the data from the new db
      PiListBox_Reset();
   }

   // check if the message relates to the browser db
   if ( (pUiDbContext != NULL) &&
        ((target == DB_TARGET_UI) || (pAcqDbContext == pUiDbContext)) )
   {
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      if (pAiBlock != NULL)
      {
         // set the window title according to the new AI
         sprintf(comm, "wm title . {Nextview EPG: %s}\n", AI_GET_SERVICENAME(pAiBlock));
         eval_check(interp, comm);

         // update the netwop filter bar and the netwop prefilter
         PiFilter_UpdateNetwopList();
      }
      else
      {  // no AI block in db -> reset window title to empty
         sprintf(comm, "wm title . {Nextview EPG}\n");
         eval_check(interp, comm);
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
   }

   // if db is empty, display or update the help message
   UiControl_CheckDbState();

   if (target == DB_TARGET_UI)
   {  // update main window status line and other db related output
      StatsWin_StatsUpdate(DB_TARGET_UI);
      TimeScale_ProvChange(DB_TARGET_UI);
   }
}

// ----------------------------------------------------------------------------
// Add or update an EPG provider channel frequency in the rc/ini file
// - called by acq control when the first AI is received after a provider change
//
void UiControlMsg_NewProvFreq( uint cni, uint freq )
{
   sprintf(comm, "UpdateProvFrequency {0x%04X %d}\n", cni, freq);
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Query the frequency for the given provider stored in the rc/ini file
// - used by acq control upon provider changes
//
uint UiControlMsg_QueryProvFreq( uint cni )
{
   return GetProvFreqForCni(cni);
}

// ----------------------------------------------------------------------------
// Inform the user about an error during reload of a database
//
void UiControl_ReloadError( ClientData clientData )
{
   MSG_RELOAD_ERR * pMsg = (MSG_RELOAD_ERR *) clientData;
   const char *pReason, *pHint;
   char * comm2 = xmalloc(2048);

   pReason = NULL;
   pHint = "";

   // translate the error result code from the reload or peek function to human readable form
   switch (pMsg->dberr)
   {
      case EPGDB_RELOAD_CORRUPT:
         pReason = "the file is corrupt";
         pHint = "You should start an EPG scan from the Configure menu or remove this file from the database directory. ";
         break;
      case EPGDB_RELOAD_WRONG_MAGIC:
         pReason = "the file was not generated by this software";
         pHint = "You should remove this file from the database directory and start an EPG scan from the Configure menu. ";
         break;
      case EPGDB_RELOAD_ACCESS:
         pReason = "of file access permissions";
         pHint = "Please make this file readable and writable for all users. ";
         break;
      case EPGDB_RELOAD_ENDIAN:
         pReason = "of an 'endian' mismatch, i.e. the database was generated on a different CPU architecture";
         break;
      case EPGDB_RELOAD_VERSION:
         pReason = "it's an incompatible version";
         pHint = "You should start an EPG scan from the Configure menu. ";
         break;
      case EPGDB_RELOAD_EXIST:
         pReason = "the database file does not exist";
         pHint = "You should start an EPG scan from the Configure menu. ";
         break;
      case EPGDB_RELOAD_OK:
         SHOULD_NOT_BE_REACHED;
         break;
      default:
         SHOULD_NOT_BE_REACHED;
      case EPGDB_RELOAD_INTERNAL:
         pReason = "an internal error occurred";
         break;
   }

   if (pReason != NULL)
   {
      switch (pMsg->errHand)
      {
         case CTX_RELOAD_ERR_ANY:
            if (pMsg->isNewDb && (pMsg->dberr != EPGDB_RELOAD_EXIST))
            {
               sprintf(comm2, "tk_messageBox -type ok -icon warning -message {"
                                "Found a database (provider %X) which failed to be loaded because %s. %s"
                                "}\n", pMsg->cni, pReason, pHint);
               eval_check(interp, comm2);
            }
            break;

         case CTX_RELOAD_ERR_ACQ:
            sprintf(comm2, "tk_messageBox -type ok -icon warning -message {"
                             "Failed to load the database of provider %X because %s. "
                             "Cannot switch the TV channel to start acquisition for this provider. "
                             "%s%s}\n",
                             pMsg->cni, pReason, pHint,
                             ((pMsg->dberr == EPGDB_RELOAD_EXIST) ? "Or choose a different acquisition mode. " : ""));
            eval_check(interp, comm2);
            break;

         case CTX_RELOAD_ERR_REQ:
            sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                             "Failed to load the database of provider %X because %s. "
                             "%s"
                             "}\n", pMsg->cni, pReason, pHint);
            eval_check(interp, comm2);
            break;

         case CTX_RELOAD_ERR_DEMO:
            sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                             "Failed to load the demo database because %s. "
                             "Cannot enter demo mode."
                             "}\n", pReason);
            eval_check(interp, comm2);
            break;

         default:
            SHOULD_NOT_BE_REACHED;
            break;
      }
   }

   xfree(pMsg);
   xfree(comm2);
}

// ----------------------------------------------------------------------------
// Accept message from context control about db reload error
//
void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, CONTEXT_RELOAD_ERR_HAND errHand, bool isNewDb )
{
   uchar * pTmpStr, * pSavedResult;
   MSG_RELOAD_ERR * pMsg;

   if (errHand != CTX_RELOAD_ERR_NONE)
   {
      if (uiControlInitialized)
      {
         pMsg          = xmalloc(sizeof(MSG_RELOAD_ERR));
         pMsg->cni     = cni;
         pMsg->dberr   = dberr;
         pMsg->errHand = errHand;
         pMsg->isNewDb = isNewDb;

         if (errHand == CTX_RELOAD_ERR_REQ)
         {  // display the message immediately (as reply to user interaction)

            // have to save the Tcl interpreter's result string, so that the caller's result is not destroyed
            pTmpStr = Tcl_GetStringResult(interp);
            if (pTmpStr != NULL)
            {
               pSavedResult = xmalloc(strlen(pTmpStr) + 1);
               memcpy(pSavedResult, pTmpStr, strlen(pTmpStr) + 1);
            }
            else
               pSavedResult = NULL;

            // make sure that the window is mapped
            eval_check(interp, "update");

            // display the message and wait for the user's confirmation
            UiControl_ReloadError((ClientData) pMsg);

            // restore the Tcl interpreter's result
            if (pSavedResult != NULL)
            {
               Tcl_SetResult(interp, pSavedResult, TCL_VOLATILE);
               xfree(pSavedResult);
            }
            else
               Tcl_ResetResult(interp);
         }
         else
         {  // display the message from the main loop
            AddMainIdleEvent(UiControl_ReloadError, (ClientData) pMsg, FALSE);
         }
      }
      else
      {  // not a result of user interaction

         // non-GUI, non-acq background errors are reported only
         // at the first access and if it's not just a missing db
         if ( (errHand != CTX_RELOAD_ERR_ANY) ||
              ((isNewDb == FALSE) && (dberr != EPGDB_RELOAD_EXIST)) )
         {
            fprintf(stderr, "nxtvepg: warning: failed to load database 0x%04X\n", cni);
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Warn the user about missing tuner frequency
//
static void UiControl_MissingTunerFreq( ClientData clientData )
{
   char * comm2 = xmalloc(2048);
   uint cni = (uint) clientData;

   sprintf(comm2, "tk_messageBox -type ok -icon warning -parent . "
                  "-message {Found a database (CNI %X) with unknown tuner frequency. "
                            "Cannot switch the TV channel to start acquisition for this provider. "
                            "You should start an EPG scan from the Configure menu, "
                            "or choose acquisition mode 'passive' to avoid this message."
                            "}\n", cni);
   eval_check(interp, comm2);
   xfree(comm2);
}

// ----------------------------------------------------------------------------
// Accept message from acq control about missing tuner frequency
//
void UiControlMsg_MissingTunerFreq( uint cni )
{
   if (uiControlInitialized)
      AddMainIdleEvent(UiControl_MissingTunerFreq, (ClientData) cni, FALSE);
   else
      fprintf(stderr, "nxtvepg: warning: cannot tune channel for provider 0x%04X: frequency unknown\n", cni);
}

// ----------------------------------------------------------------------------
// Warn the user about network connection error
//
static void UiControl_NetAcqError( ClientData clientData )
{
#ifdef USE_DAEMON
   EPGDBSRV_DESCR netState;
   char           * comm2;

   EpgAcqClient_DescribeNetState(&netState);
   if (netState.cause != NULL)
   {
      comm2 = xmalloc(2048);
      sprintf(comm2, "tk_messageBox -type ok -icon warning -parent . "
                     "-message {An error occurred in the network connection to the acquisition "
                               "server: %s"
                               "}\n", netState.cause);
      eval_check(interp, comm2);
      xfree(comm2);
   }
#endif
}

// ----------------------------------------------------------------------------
// Accept message from acq control about network connection error
// - only used on client-side, hence no output method for non-GUI mode needed
//
void UiControlMsg_NetAcqError( void )
{
   if (uiControlInitialized)
      AddMainIdleEvent(UiControl_NetAcqError, NULL, TRUE);
}

// ----------------------------------------------------------------------------
// Warn the user about acquisition mode error
//
static void UiControl_AcqPassive( ClientData clientData )
{
   char * comm2 = xmalloc(2048);

   sprintf(comm2, "tk_messageBox -type ok -icon warning -parent . "
                  "-message {Since the selected input source is not a TV tuner, "
                            #ifndef WIN32
                            "only the 'passive' and 'external' acquisition modes are possible. "
                            #else
                            "only the 'external' acquisition mode is possible. "
                            #endif
                            "Change either source or mode to avoid this message."
                            "}\n");
   eval_check(interp, comm2);
   xfree(comm2);
}

// ----------------------------------------------------------------------------
// Accept message from acq control about input source problem
//
void UiControlMsg_AcqPassive( void )
{
   if (uiControlInitialized)
      AddMainIdleEvent(UiControl_AcqPassive, (ClientData) NULL, TRUE);
   else
      fprintf(stderr, "nxtvepg: fatal: invalid acquisition mode for the selected input source\n");
}

// ----------------------------------------------------------------------------
// Distribute acquisition events to GUI modules
//
void UiControlMsg_AcqEvent( ACQ_EVENT acqEvent )
{
   if (uiControlInitialized)
   {
      switch (acqEvent)
      {
         case ACQ_EVENT_PROV_CHANGE:
            TimeScale_ProvChange(DB_TARGET_ACQ);
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            AddMainIdleEvent(UiControl_AiStateChange, (ClientData) DB_TARGET_ACQ, FALSE);
            break;

         case ACQ_EVENT_AI_VERSION_CHANGE:
            TimeScale_VersionChange();
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            AddMainIdleEvent(UiControl_AiStateChange, (ClientData) DB_TARGET_ACQ, FALSE);
            break;

         case ACQ_EVENT_AI_PI_RANGE_CHANGE:
            TimeScale_VersionChange();
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            break;

         case ACQ_EVENT_STATS_UPDATE:
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            TimeScale_AcqStatsUpdate();
            MenuCmd_AcqStatsUpdate();
            break;

         case ACQ_EVENT_PI_ADDED:
            TimeScale_AcqPiAdded();
            break;

         case ACQ_EVENT_PI_MERGED:
            TimeScale_AcqPiMerged();
            break;

         case ACQ_EVENT_VPS_PDC:
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            break;
      }
   }
}

// ----------------------------------------------------------------------------
// Acq input queue overflow
// - a very large number of PI blocks in the in queue
// - if the acq db is identical to the ui db, the direct connection must
//   be lifted or the GUI will hang
//
bool UiControlMsg_AcqQueueOverflow( bool prepare )
{
   bool acqWorksOnUi = FALSE;

   // XXX commented out the whole function: seems this not required
   // XXX performance problems only arise while epgdbmgmt and pilistbox consistancy checks are enabled
#if 0
   uint acqCni;

   if (prepare)
   {
      acqCni = EpgDbContextGetCni(pAcqDbContext);
      if (acqCni != 0)
      {
         debug0("UiControlMsg-AcqQueueOverflow: locking acq -> ui connection");
         // check if acquisition is working for the browser database
         if ( EpgDbContextIsMerged(pUiDbContext) )
            acqWorksOnUi = EpgContextMergeCheckForCni(pUiDbContext, acqCni);
         else
            acqWorksOnUi = (acqCni == EpgDbContextGetCni(pUiDbContext));
      }
      else
         acqWorksOnUi = TRUE;

      if (acqWorksOnUi)
      {
         if (EpgDbContextIsMerged(pUiDbContext))
            EpgContextMergeLockInsert(pUiDbContext, TRUE);
         PiListBox_Lock(TRUE);
      }
   }
   else
   {
      if (EpgDbContextIsMerged(pUiDbContext))
      {
         eval_check(interp, "C_ProvMerge_Start");
      }
      PiListBox_Lock(FALSE);
   }
#endif

   return acqWorksOnUi;
}

// ----------------------------------------------------------------------------
// Initialize module
//
void UiControl_Init( void )
{
   uiControlInitialized = TRUE;
}

