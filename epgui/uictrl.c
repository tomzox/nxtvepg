/*
 *  General GUI housekeeping
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <string.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgctxmerge.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgscan.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgvbi/syserrmsg.h"
#include "epgui/epgmain.h"
#include "epgui/pifilter.h"
#include "epgui/pibox.h"
#include "epgui/piremind.h"
#include "epgui/uictrl.h"
#include "epgui/epgsetup.h"
#include "epgui/menucmd.h"
#include "epgui/statswin.h"
#include "epgui/timescale.h"
#include "epgui/cmdline.h"
#include "epgui/daemon.h"
#include "epgui/rcfile.h"
#include "xmltv/xmltv_main.h"
#include "xmltv/xmltv_cni.h"
#include "epgctl/epgctxctl.h"


// describing the state of the ui db
typedef enum
{
   EPGDB_NOT_INIT,
   EPGDB_TVCARD_CFG,
   EPGDB_WAIT_SCAN,
   EPGDB_PROV_NONE_BUT_ACQ,
   EPGDB_PROV_NONE_BUT_TTX,
   EPGDB_PROV_NONE,
   EPGDB_PROV_SEL_OR_TTX,
   EPGDB_PROV_SEL,
   EPGDB_ACQ_NO_TUNER,
   EPGDB_ACQ_ACCESS_DEVICE,
   EPGDB_ACQ_PASSIVE,
   EPGDB_EMPTY_ACQ_WAIT,
   EPGDB_ACQ_WAIT,
   EPGDB_ACQ_WAIT_DAEMON,
   EPGDB_ACQ_OTHER_PROV,
   EPGDB_EMPTY,
   EPGDB_PREFILTERED_EMPTY,
   EPGDB_OK
} EPGDB_STATE;

typedef struct
{
   uint                     cni;
   EPGDB_RELOAD_RESULT      dberr;
   CONTEXT_RELOAD_ERR_HAND  errHand;
   bool                     isNewDb;
} MSG_RELOAD_ERR;


static bool uiControlInitialized = FALSE;


// ---------------------------------------------------------------------------
// Generate a help message for an db error state
//
static const char * UiControl_GetDbStateMsg( EPGDB_STATE state )
{
   const char * pMsg;

   switch (state)
   {
      case EPGDB_TVCARD_CFG:
         pMsg = "Before EPG data can be acquired via teletext, you have to "
                "configure your TV card and tuner types. To do so please open "
                "the 'TV card input' dialog in the configure menu and select "
                "'Configure card'.";
         break;

      case EPGDB_WAIT_SCAN:
         pMsg = "Please wait until the channel scan has finished.";
         break;

      case EPGDB_PROV_NONE_BUT_ACQ:
         pMsg = "No EPG data is loaded yet. The Teletext EPG grabber is running, "
                "but its output is not selected for display. Use the Control menu "
                "for loading an XMLTV file or Teletext EPG.";
         break;

      case EPGDB_PROV_NONE_BUT_TTX:
         pMsg = "No EPG data is loaded. If you have XMLTV files with EPG data, "
                "use the Control menu for loading them. Else configure the Teletext EPG "
                "grabber for creating XMLTV files via acquisition from a TV capture card.";
         break;

      case EPGDB_PROV_NONE:
         pMsg = "No EPG data is loaded. If you have XMLTV files with EPG data, "
                "use the Control menu for loading them.";
         break;

      case EPGDB_PROV_SEL_OR_TTX:
         pMsg = "No EPG data loaded. Please load one or more XMLTV files via the "
                "Control menu, or configure the Teletext EPG grabber for creating "
                "XMLTV files via acquisition from a TV capture card.";
         break;

      case EPGDB_PROV_SEL:
         pMsg = "No EPG data loaded. Please load one or more XMLTV files via the "
                "Control menu.";
         break;

      case EPGDB_ACQ_NO_TUNER:
         pMsg = "The loaded XMLTV file is empty, or all programmes are expired. "
                "Since you have configured a video input source that does not support "
                "channel tuning, you have to manually select a TV channel at the "
                "external video equipment to enable nxtvepg to refresh its database. "
                "After channel changes wait apx. 20 seconds for acquisition to start.";
         break;

      case EPGDB_ACQ_ACCESS_DEVICE:
         pMsg = "The loaded XMLTV file is empty, or all programmes are expired. "
                "The TV channel could not be changed because the video device is "
                "kept busy by another application. Therefore you have to make sure "
                "you have tuned the TV channel of the selected EPG provider "
                "or select a different EPG provider via the Configure menu.";
         break;

      case EPGDB_ACQ_PASSIVE:
         pMsg = "The loaded XMLTV file is empty, or all programmes are expired. "
                "You have configured acquisition mode to passive, so EPG data "
                "can only be acquired once you use another application to tune to "
                "the selected EPG provider's TV channel.";
         break;

      case EPGDB_EMPTY_ACQ_WAIT:
         pMsg = "Please wait a few minutes until Teletext EPG data is received. "
                "You can monitor progress using \"Teletext grabber statistics\" in the Control menu. "
                "Alternatively, load data from XMLTV files via the Control menu.";
         break;

      case EPGDB_ACQ_WAIT:
         pMsg = "The loaded XMLTV file is empty, or all programmes are expired. "
                "Please wait a few minutes until EPG data is received "
                "or choose a different EPG source via the Control menu.";
         break;

      case EPGDB_ACQ_WAIT_DAEMON:
         pMsg = "The loaded XMLTV file is empty, or all programmes are expired. "
                "The EPG acquisition daemon is active, however not working on the "
                "EPG source selected for display. "
                "Choose a different source via the Control menu.";
         break;

      case EPGDB_ACQ_OTHER_PROV:
         pMsg = "The loaded XMLTV file is empty, or all programmes are expired. "
                "The Teletext EPG grabber is active, but not working on the "
                "EPG source selected for display. "
                "Choose a different source via the Control menu, or reconfigure "
                "the Teletext grabber via the Configure menu.";
         break;

      case EPGDB_EMPTY:
         pMsg = "The loaded XMLTV file is empty, or all programmes are expired. "
                "Load EPG data from a different XMLTV file via the Control menu, "
                "or enable EPG acquisition from teletext for updating the EPG data.";
         break;

      case EPGDB_PREFILTERED_EMPTY:
         pMsg = "None of the programmes in the loaded EPG data match your network "
                "preselection. Either enable display of all networks for this provider, "
                "or load a different XMLTV file via the Configure menu.";
         break;

      case EPGDB_OK:
         // db is now ready -> clear message display
         pMsg = NULL;
         break;

      default:
         fatal1("UiControl-GetDbStateMsg: unknown state %d", state);
         pMsg = "Internal error";
         break;
   }

   return pMsg;
}

// ---------------------------------------------------------------------------
// Determine state of UI database and acquisition
// - this is used to display a helpful message in the PI listbox
// - called when no PI is found with default pre-filters enabled
//   or if no provider is configured yet
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
      int drvType = RcFile_Query()->tvcard.drv_type;
      if (drvType == BTDRV_SOURCE_UNDEF)
      {
         drvType = BtDriver_GetDefaultDrvType();
      }
      //if (EpgSetup_CheckTvCardConfig() == FALSE)
      //   dbState = EPGDB_TVCARD_CFG;
      //else
      if (acqState.ttxGrabState != ACQDESCR_DISABLED)
      {
         if (RcFile_Query()->db.auto_merge_ttx)
            dbState = EPGDB_EMPTY_ACQ_WAIT;
         else
            dbState = EPGDB_PROV_NONE_BUT_ACQ;
      }
      else if (EpgContextCtl_HaveProviders() == FALSE)
         dbState = ((drvType != BTDRV_SOURCE_NONE) ? EPGDB_PROV_NONE_BUT_TTX : EPGDB_PROV_NONE);
      else
         dbState = ((drvType != BTDRV_SOURCE_NONE) ? EPGDB_PROV_SEL_OR_TTX : EPGDB_PROV_SEL);
   }
   else
   {  // AI present, but no PI in database
      acqWorksOnUi = FALSE;

#ifdef USE_TTX_GRABBER
      // check if acquisition is working for the browser database
      if ( (acqState.ttxGrabState != ACQDESCR_NET_CONNECT) &&
           (acqState.ttxGrabState != ACQDESCR_DISABLED) &&
           (acqState.ttxGrabState != ACQDESCR_SCAN) )
      {
         acqWorksOnUi = EpgSetup_IsAcqWorkingForUiDb();
      }
#endif

      if (acqState.ttxGrabState == ACQDESCR_NET_CONNECT)
      {  // in network acq mode: no stats available yet
         dbState = EPGDB_ACQ_WAIT_DAEMON;
      }
      else if (acqState.ttxGrabState == ACQDESCR_DISABLED)
      {
         dbState = EPGDB_EMPTY;
      }
      else if (acqState.ttxGrabState == ACQDESCR_SCAN)
      {
         dbState = EPGDB_WAIT_SCAN;
      }
      else if (acqState.mode == ACQMODE_PASSIVE)
      {
         if ((acqState.ttxGrabState == ACQDESCR_RUNNING) && acqWorksOnUi)
            dbState = EPGDB_ACQ_WAIT;
         else
            dbState = EPGDB_ACQ_PASSIVE;
      }
      else if (acqState.passiveReason != ACQPASSIVE_NONE)
      {
         // translate forced-passive reason to db state
         switch (acqState.passiveReason)
         {
            case ACQPASSIVE_NO_TUNER:
               dbState = EPGDB_ACQ_NO_TUNER;
               break;
            case ACQPASSIVE_ACCESS_DEVICE:
               dbState = EPGDB_ACQ_ACCESS_DEVICE;
               break;
            default:
               fatal1("UiControl-GetDbState: illegal state %d", acqState.passiveReason);
               dbState = EPGDB_ACQ_PASSIVE;
               break;
         }
      }
      else if (acqWorksOnUi)
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
void UiControl_CheckDbState( ClientData clientData )
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
         filterMask = pPreFilterContext->enabledPreFilters;
         // disable all filters except the expire time
         EpgDbFilterDisable(pPreFilterContext, FILTER_NONPERM);
         EpgDbPreFilterDisable(pPreFilterContext, FILTER_PERM & ~FILTER_EXPIRE_TIME);

         pPiBlock = EpgDbSearchFirstPi(pUiDbContext, pPreFilterContext);
         if (pPiBlock == NULL)
         {  // no PI in database (probably all expired)
            state = UiControl_GetDbState();
         }
         else
         {
            if (filterMask & FILTER_NETWOP_PRE)
            {  // re-enable the network pre-filter
               EpgDbPreFilterEnable(pPreFilterContext, FILTER_NETWOP_PRE);
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

   dprintf2("UiControl-CheckDbState: pibox state #%d (%.45s...)\n", state, ((state != EPGDB_OK) ? UiControl_GetDbStateMsg(state) : "OK"));
   PiBox_ErrorMessage(UiControl_GetDbStateMsg(state));
}

// ----------------------------------------------------------------------------
// Update UI after possible change of netwop list
// - called as idle function after AI update or when initial AI received
// - if the browser is still empty, it takes over the acq db
// - else just update the provider name and network list
//
void UiControl_AiStateChange( ClientData clientData )
{
   const AI_BLOCK *pAiBlock;
   const char * name;
   Tcl_DString cmd_dstr;

   // check if the message relates to the browser db
   if (pUiDbContext != NULL)
   {
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      if (pAiBlock != NULL)
      {
         // set the window title according to the new AI
         if ( EpgDbContextIsMerged(pUiDbContext) )
            name = AI_GET_SERVICENAME(pAiBlock);
         else
            name = XmltvCni_LookupProviderPath(EpgDbContextGetCni(pUiDbContext));

         // limit length of title to apx. 100 characters
         // - because else the assignment may silently fail, e.g. with fvwm)
         // - also we could overflow the "comm" buffer
         if (strlen(name) > 104)
            sprintf(comm, "nxtvepg: %.104s ...", name);
         else
            sprintf(comm, "nxtvepg: %s", name);

         Tcl_DStringInit(&cmd_dstr);
         Tcl_DStringAppend(&cmd_dstr, "wm title .", -1);
         // append message as list element, so that '{' etc. is escaped properly
         Tcl_DStringAppendElement(&cmd_dstr, comm);

         eval_check(interp, Tcl_DStringValue(&cmd_dstr));

         Tcl_DStringFree(&cmd_dstr);

         // generate the netwop mapping tables and update the netwop filter bar
         EpgSetup_UpdateProvCniTable();
         sprintf(comm, "UpdateProvCniTable 0x%04X\n"
                       "UpdateNetwopFilterBar\n",
                       EpgDbContextGetCni(pUiDbContext));
         eval_check(interp, comm);

         // update network prefilters (restrict to user-selected networks)
         EpgSetup_SetNetwopPrefilter(pUiDbContext, pPiFilterContext);
         PiFilter_UpdateAirTime();

         // adapt reminder list to database (network index cache)
         PiRemind_CheckDb();

         // update filter cache (network indices may have changed)
         eval_check(interp, "DownloadUserDefinedColumnFilters");
         eval_check(interp, "UpdatePiListboxColumParams");

         // update the netwop prefilter and refresh the display
         PiBox_AiStateChange();

         // apply new filter cache to reminders (done after PI-box refresh to reduce delay)
         sprintf(comm, "Reminder_UpdateTimer");
         eval_check(interp, comm);

         // update list of theme category names defined by provider database
         sprintf(comm, "UpdateThemeCategories");
         eval_check(interp, comm);
      }
      else
      {  // no AI block in db -> reset window title to empty
         sprintf(comm, "wm title . {nxtvepg}\n");
         eval_check(interp, comm);

         // clear the programme display
         PiBox_Reset();
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
   }

   // if db is empty, display or update the help message
   UiControl_CheckDbState(NULL);

   // update main window status line and other db related output
   StatsWin_UiStatsUpdate(FALSE, TRUE);
   TimeScale_ProvChange();
}

// ----------------------------------------------------------------------------
// Process notification about XMLTV update by external process
// - triggered by the TTX grabber after updating an XML file
// - check open UI database if newer version are available on disk;
//   if yes re-open the db, or merge in the new data
//
static void UiControl_LoadAcqDb( ClientData clientData )
{
   EPGDB_CONTEXT * pDbContext;
   uint  dbIdx, provCount;
   uint  provCniTab[MAX_MERGED_DB_COUNT];
   uint  updCount;
   uint  addCount;
   uint  cni;

   if (pUiDbContext != NULL)
   {
      if ( EpgDbContextIsMerged(pUiDbContext) )
      {
         // merged database: get list of provider CNIs
         if (EpgContextMergeGetCnis(pUiDbContext, &provCount, provCniTab))
         {
            // append CNIs of all XMLTV files generated by Teletext grabber
            uint newProvCount = provCount;
            if (RcFile_Query()->db.auto_merge_ttx)
               EpgSetup_AppendTtxProviders(&newProvCount, provCniTab);
            addCount = newProvCount - provCount;

            // remove CNIs of XML files that have not changed
            updCount = 0;
            for (dbIdx=0; dbIdx < provCount; dbIdx++)
            {
               if ( EpgContextCtl_CheckFileModified(provCniTab[dbIdx]) )
               {
                  provCniTab[updCount] = provCniTab[dbIdx];
                  updCount += 1;
               }
            }
            // keep CNIs of added providers at the end of the list of changed files
            if (updCount < provCount)
               memmove(provCniTab + updCount, provCniTab + provCount, addCount * sizeof(uint));

            dprintf3("UiControl-LoadAcqDb: update:%d of %d DBs and add:%d\n", updCount, provCount, addCount);
            if (updCount + addCount > 0)
            {
               // found at least one changed file: redo merge of PI in affected networks only
               if (EpgContextMergeUpdateDb(updCount + addCount, addCount, provCniTab, CTX_RELOAD_ERR_ACQ))
               {
                  PiBox_Refresh();
                  UiControl_AiStateChange(NULL);

                  TimeScale_VersionChange();
                  StatsWin_UiStatsUpdate(FALSE, TRUE);
               }
            }
         }
      }
      else
      {
         cni = EpgDbContextGetCni(pUiDbContext);
         if (cni != 0)
         {  // plain XMLTV import
            dprintf1("UiControl-LoadAcqDb: XMLTV DB 0x%04X\n", cni);

            if ( EpgContextCtl_CheckFileModified(cni) )
            {
               pDbContext = EpgContextCtl_Open(cni, TRUE, CTX_RELOAD_ERR_NONE);
               if (pDbContext != NULL)
               {
                  EpgContextCtl_Close(pUiDbContext);
                  pUiDbContext = pDbContext;

                  PiBox_Refresh();
                  UiControl_AiStateChange(NULL);

                  TimeScale_VersionChange();
                  StatsWin_UiStatsUpdate(FALSE, TRUE);
               }
               else
                  debug1("UiControl-LoadAcqDb: failed to load db 0x%04X", cni);
            }
         }
         else if (RcFile_Query()->db.auto_merge_ttx)
         {  // database completely empty
            dprintf0("UiControl-LoadAcqDb: initial merge\n");

            // zero providers explicitly selected
            RcFile_UpdateDbMergeCnis(NULL, 0);

            // clear merge provider options
            for (uint ati = 0; ati < MERGE_TYPE_COUNT; ati++)
            {
               RcFile_UpdateDbMergeOptions(ati, NULL, 0);
            }

            pDbContext = EpgSetup_MergeDatabases(CTX_RELOAD_ERR_NONE);
            if (pDbContext != NULL)
            {
               EpgContextCtl_Close(pUiDbContext);
               pUiDbContext = pDbContext;

               PiBox_Reset();
               UiControl_AiStateChange(NULL);

               TimeScale_VersionChange();
               StatsWin_UiStatsUpdate(TRUE, TRUE);
            }
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Display error message in a popup message box
//
void UiControl_DisplayErrorMessage( char * pMsg )
{
   Tcl_DString cmd_dstr;

   if (pMsg != NULL)
   {
      if (interp != NULL)
      {
         {
            Tcl_DStringInit(&cmd_dstr);

            if (Tk_GetNumMainWindows() > 0)
               Tcl_DStringAppend(&cmd_dstr, "tk_messageBox -type ok -icon error -parent . -message ", -1);
            else
               Tcl_DStringAppend(&cmd_dstr, "tk_messageBox -type ok -icon error -message ", -1);

            // append message as list element, so that '{' etc. is escaped properly
            Tcl_DStringAppendElement(&cmd_dstr, pMsg);

            eval_check(interp, Tcl_DStringValue(&cmd_dstr));

            Tcl_DStringFree(&cmd_dstr);
         }
      }
      else
      {
         fprintf(stderr, "nxtvepg: %s\n", pMsg);
      }
   }
   else
      debug0("UiControl-DisplayErrorMessage: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Inform the user about an error during reload of a database
//
void UiControl_ReloadError( ClientData clientData )
{
   MSG_RELOAD_ERR * pMsg = (MSG_RELOAD_ERR *) clientData;
   const char *pReason, *pHint;
   char * comm2 = (char*) xmalloc(2048);
   const char *pXmlPath;

   pReason = NULL;
   pHint = "";

   if (pMsg->dberr & EPGDB_RELOAD_XML_MASK)
   {
      pReason = Xmltv_TranslateErrorCode(pMsg->dberr & ~EPGDB_RELOAD_XML_MASK);
      pHint = "";
   }
   else  // translate the error result code from the reload or peek function to human readable form
   switch (pMsg->dberr)
   {
      case EPGDB_RELOAD_ACCESS:
         pReason = "of file access permissions";
         pHint = "Please make this file readable and writable for all users. ";
         break;
      case EPGDB_RELOAD_EXIST:
         pReason = "the database file does not exist";
         break;
      case EPGDB_RELOAD_MERGE:
         pReason = "databases could not be merged";
         pHint = "You should check your database selection in the merge configuration dialog. ";
         break;
      case EPGDB_RELOAD_XML_CNI:
         pReason = "its XMLTV file path is unknown";
         pHint = "Probably the nxtvepg cofiguration file (rc/ini file) was manually edited or corrupted. ";
         break;
      case EPGDB_RELOAD_OK:
         SHOULD_NOT_BE_REACHED;
         break;
      default:
         SHOULD_NOT_BE_REACHED;
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
            if ((pXmlPath = XmltvCni_LookupProviderPath(pMsg->cni)) != NULL)
               sprintf(comm2, "tk_messageBox -type ok -icon warning -message {"
                                "Failed to update merged database with "
                                "XMLTV file \"%s\" because %s. "
                                "%s}\n", pXmlPath, pReason, pHint);
            else
               sprintf(comm2, "tk_messageBox -type ok -icon warning -message {"
                                "Failed to update merged database with provider "
                                "with unknown identifier %X: %s. "
                                "%s}\n", pMsg->cni, pReason, pHint);
            eval_check(interp, comm2);
            break;

         case CTX_RELOAD_ERR_REQ:
            if (pMsg->cni == MERGED_PROV_CNI)
               sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                                "Failed to create a merged database. %s"
                                "}\n", pHint);
            else if ((pXmlPath = XmltvCni_LookupProviderPath(pMsg->cni)) != NULL)
               sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                                "Failed to load XML file \"%s\" because %s. "
                                "%s"
                                "}\n", pXmlPath, pReason, pHint);
            else  // should never be reached
               sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                                "Failed to load the database of provider "
                                "with unknown identifier %X: %s. "
                                "%s"
                                "}\n", pMsg->cni, pReason, pHint);
            eval_check(interp, comm2);
            break;

         case CTX_RELOAD_ERR_DFLT:
            if (pMsg->cni == MERGED_PROV_CNI)
               sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                                "Failed to merge all the previously used databases because %s. "
                                "%s}\n", pReason, pHint);
            else
               sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                                "Failed to load the previously used database because %s. "
                                "%s}\n", pReason, pHint);
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
void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, int errHandInt, bool isNewDb )
{
   CONTEXT_RELOAD_ERR_HAND errHand = (CONTEXT_RELOAD_ERR_HAND) errHandInt;  //FIXME CC
   const char * pTmpStr;
   char       * pSavedResult;
   MSG_RELOAD_ERR * pMsg;

   if (errHand != CTX_RELOAD_ERR_NONE)
   {
      if (uiControlInitialized)
      {
         pMsg          = (MSG_RELOAD_ERR*) xmalloc(sizeof(MSG_RELOAD_ERR));
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
               pSavedResult = (char*) xmalloc(strlen(pTmpStr) + 1);
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
            if (cni == MERGED_PROV_CNI)
               fprintf(stderr, "nxtvepg: warning: database merge failed: check configuration\n");
            else
               fprintf(stderr, "nxtvepg: warning: failed to load database 0x%04X\n", cni);
         }
      }
   }
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
      comm2 = (char*) xmalloc(2048);
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
   dprintf0("UiControlMsg-NetAcqError\n");

   if (uiControlInitialized)
      AddMainIdleEvent(UiControl_NetAcqError, NULL, TRUE);
}

// ----------------------------------------------------------------------------
// Distribute acquisition events to GUI modules
//
void UiControlMsg_AcqEvent( ACQ_EVENT acqEvent )
{
   if (uiControlInitialized)
   {
      dprintf1("UiControlMsg-AcqEvent: event #%d\n", acqEvent);

      switch (acqEvent)
      {
         case ACQ_EVENT_PROV_CHANGE:
            // sent when starting/stopping acq or upon change of channel table
            StatsWin_AcqStatsUpdate(TRUE);
            if (EpgDbContextGetCni(pUiDbContext) == 0)
            {
               AddMainIdleEvent(UiControl_CheckDbState, NULL, TRUE);
            }
            break;

         case ACQ_EVENT_STATS_UPDATE:
            // sent upon driver config change, driver failure, tuning failure
            StatsWin_AcqStatsUpdate(FALSE);
            TimeScale_AcqStatsUpdate();
            MenuCmd_AcqStatsUpdate();
            break;

         case ACQ_EVENT_CTL:
            // regular TTX acq scheduling (e.g. channel change)
            StatsWin_AcqStatsUpdate(FALSE);
            break;

         case ACQ_EVENT_NEW_DB:
            // sent after post-processing of TTX grabber: update DB or new DB
            // further events are triggered after DB is reloaded
            AddMainIdleEvent(UiControl_LoadAcqDb, NULL, TRUE);
            break;

         case ACQ_EVENT_VPS_PDC:
            StatsWin_AcqStatsUpdate(FALSE);
            break;
      }
   }
}

// ---------------------------------------------------------------------------
// Write all configuration parameters into a file
//
void UpdateRcFile( bool immediate )
{
   const char * pRcFile = ((mainOpts.rcfile != NULL) ? mainOpts.rcfile : mainOpts.defaultRcFile);
   char * pErrMsg = NULL;
   FILE * fp;
   bool   writeOk;

   #ifdef USE_DAEMON
   if (uiControlInitialized == FALSE)
   {
      Daemon_UpdateRcFile(immediate);
      return;
   }
   #endif

   fp = RcFile_WriteCreateFile(pRcFile, &pErrMsg);
   if (fp != NULL)
   {
      // write C level sections
      writeOk = RcFile_WriteOwnSections(fp) && (fflush(fp) == 0);

      if (ferror(fp) && (pErrMsg == NULL))
      {
         SystemErrorMessage_Set(&pErrMsg, errno, "Error while writing configuration file: ", NULL);
      }

      // write GUI level sections
      if (Tcl_EvalEx(interp, "GetGuiRcData", -1, TCL_EVAL_GLOBAL) == TCL_OK)
      {
         // query GUI config in form of a string
         const char * pExtStr = Tcl_GetStringResult(interp);

         // write string to file in internal Tcl encoding (UTF-8) (since version 3.0.4)
         size_t wlen = fprintf(fp, "\n%s", pExtStr);

         if (wlen != 1 + strlen(pExtStr))
         {
            debug2("UpdateRcFile: short write: %d of %d", (int)wlen, (int)(1 + strlen(pExtStr)));
            writeOk = FALSE;
         }
      }
      else
      {
         debugTclErr(interp, "UpdateRcFile: GetGuiRcData");
         writeOk = FALSE;
      }
      Tcl_ResetResult(interp);

      RcFile_WriteCloseFile(fp, writeOk, pRcFile, &pErrMsg);
   }
   if (pErrMsg != NULL)
   {
      UiControl_DisplayErrorMessage(pErrMsg);
      xfree(pErrMsg);
   }
}

// ----------------------------------------------------------------------------
// Load config data from rc/ini file
//
void LoadRcFile( void )
{
   const char * pRcFile = ((mainOpts.rcfile != NULL) ? mainOpts.rcfile : mainOpts.defaultRcFile);
   Tcl_DString msg_dstr;
   Tcl_DString cmd_dstr;
   bool loadOk;
   char * pErrMsg = NULL;

   // first load the sections managed at epgui level
   loadOk = RcFile_Load(pRcFile, mainOpts.rcfile == NULL, &pErrMsg);

   if (pErrMsg != NULL)
   {
      UiControl_DisplayErrorMessage(pErrMsg);
      xfree(pErrMsg);
   }

   // secondly load the sections managed at Tcl level
   if (loadOk)
   {
      if (Tcl_ExternalToUtfDString(NULL, pRcFile, -1, &msg_dstr) != NULL)
      {
         Tcl_DStringInit(&cmd_dstr);
         Tcl_DStringAppend(&cmd_dstr, "LoadRcFile", -1);
         // append file name as list element, so that '{' etc. is escaped properly
         Tcl_DStringAppendElement(&cmd_dstr, Tcl_DStringValue(&msg_dstr));

         eval_check(interp, Tcl_DStringValue(&cmd_dstr));

         Tcl_DStringFree(&cmd_dstr);
         Tcl_DStringFree(&msg_dstr);
      }
   }
}

// ----------------------------------------------------------------------------
// Initialize module
//
void UiControl_Init( void )
{
   uiControlInitialized = TRUE;
}

