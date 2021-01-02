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
 *  $Id: uictrl.c,v 1.61 2020/06/21 07:37:46 tom Exp tom $
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
#include "epgui/daemon.h"
#include "epgui/epgmain.h"
#include "epgui/daemon.h"
#include "epgui/pifilter.h"
#include "epgui/pibox.h"
#include "epgui/piremind.h"
#include "epgui/uictrl.h"
#include "epgui/epgsetup.h"
#include "epgui/menucmd.h"
#include "epgui/statswin.h"
#include "epgui/timescale.h"
#include "epgui/cmdline.h"
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
                "but its data is not selected for display. Use the Control menu "
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
         pMsg = "No XMLTV file selected. Please load one or more via the Control menu."
                "Else configure the Teletext EPG "
                "grabber for creating XMLTV files via acquisition from a TV capture card.";
         break;

      case EPGDB_PROV_SEL:
         pMsg = "No XMLTV file selected. Please load one or more via the Control menu.";
         break;

      case EPGDB_ACQ_NO_TUNER:
         pMsg = "The loaded database is empty, or all programmes are expired. "
                "Since you have configured a video input source that does not support "
                "channel tuning, you have to manually select a TV channel at the "
                "external video equipment to enable nxtvepg to refresh its database. "
                "After channel changes wait apx. 20 seconds for acquisition to start.";
         break;

      case EPGDB_ACQ_ACCESS_DEVICE:
         pMsg = "The loaded database is empty, or all programmes are expired. "
                "The TV channel could not be changed because the video device is "
                "kept busy by another application. Therefore you have to make sure "
                "you have tuned the TV channel of the selected EPG provider "
                "or select a different EPG provider via the Configure menu.";
         break;

      case EPGDB_ACQ_PASSIVE:
         pMsg = "The loaded database is empty, or all programmes are expired. "
                "You have configured acquisition mode to passive, so EPG data "
                "can only be acquired once you use another application to tune to "
                "the selected EPG provider's TV channel.";
         break;

      case EPGDB_ACQ_WAIT:
         pMsg = "The loaded database is empty, or all programmes are expired. "
                "Please wait a few minutes until EPG data is received "
                "or select a different EPG source via the Control menu.";
         break;

      case EPGDB_ACQ_WAIT_DAEMON:
         pMsg = "The loaded database is empty, or all programmes are expired. "
                "The EPG acquisition daemon is active, however not working on the "
                "EPG source selected for display. "
                "Choose a different source via the Control menu.";
         break;

      case EPGDB_ACQ_OTHER_PROV:
         pMsg = "The loaded database is empty, or all programmes are expired. "
                "The Teletext EPG grabber is active, but not working on the "
                "EPG source selected for display. "
                "Choose a different source via the Control menu, or reconfigure "
                "the Teletext grabber via the Configure menu.";
         break;

      case EPGDB_EMPTY:
         pMsg = "The loaded database is empty, or all programmes are expired. "
                "Load EPG data from a different XMLTV file via the Control menu, "
                "or enable EPG acquisition from teletext for updating the EPG data.";
         break;

      case EPGDB_PREFILTERED_EMPTY:
         pMsg = "None of the programmes in this database match your network "
                "preselection. Either add more networks for this provider or "
                "select a different one in the Configure menus. Starting EPG "
                "acquisition might also help.";
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
         dbState = EPGDB_PROV_NONE_BUT_ACQ;
      else if (EpgContextCtl_HaveProviders() == FALSE)
         dbState = ((drvType != BTDRV_SOURCE_NONE) ? EPGDB_PROV_NONE_BUT_TTX : EPGDB_PROV_NONE);
      else
         dbState = ((drvType != BTDRV_SOURCE_NONE) ? EPGDB_PROV_SEL_OR_TTX : EPGDB_PROV_SEL_OR_TTX);
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
         const char * pXmlPath = XmltvCni_LookupProviderPath(uiCni);
         if (pXmlPath != NULL)
            acqWorksOnUi = EpgSetup_QueryTtxPath(pXmlPath);
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

   dprintf2("UiControl-CheckDbState: pibox state #%d (%.45s...)\n", state, UiControl_GetDbStateMsg(state));
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
   uint target = PVOID2UINT(clientData);
   const AI_BLOCK *pAiBlock;
   Tcl_DString cmd_dstr;

#if 0
   if ( (EpgDbContextGetCni(pUiDbContext) == 0) &&
        (EpgAcqCtl_GetProvCni() != 0) &&
        (EpgScan_IsActive() == FALSE) )
   {  // UI db is completely empty (should only happen if there's no provider at all)
      dprintf1("UiControl-AiStateChange: browser db empty, switch to acq db 0x%04X\n", EpgAcqCtl_GetProvCni());
      // switch browser to acq db
      EpgContextCtl_Close(pUiDbContext);
      pUiDbContext = EpgContextCtl_Open(EpgAcqCtl_GetProvCni(), FALSE, CTX_FAIL_RET_DUMMY, CTX_RELOAD_ERR_ANY);

      // update acq CNI list in case follow-ui acq mode is selected
      EpgSetup_AcquisitionMode(NETACQ_KEEP);
      // display the data from the new db
      PiBox_Reset();
   }
#endif

   // check if the message relates to the browser db
   if ((pUiDbContext != NULL) && (target == DB_TARGET_UI))
   {
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      if (pAiBlock != NULL)
      {
         // set the window title according to the new AI
         const char * prefix, * name;
#ifdef USE_XMLTV_IMPORT
         if ( EpgDbContextIsXmltv(pUiDbContext) )
         {
            prefix = "XMLTV";
            name = XmltvCni_LookupProviderPath(EpgDbContextGetCni(pUiDbContext));
         }
         else
#endif
         if ( EpgDbContextIsMerged(pUiDbContext) )
         {
            prefix = "nxtvepg";
            name = AI_GET_SERVICENAME(pAiBlock);
         }
         else
         {
            prefix = "Nextview EPG";
            name = AI_GET_SERVICENAME(pAiBlock);
         }
         // limit length of title to apx. 100 characters
         // - because else the assignment may silently fail, e.g. with fvwm)
         // - also we could overflow the "comm" buffer
         if (strlen(name) > 104)
            sprintf(comm, "%s: %.104s ...", prefix, name);
         else
            sprintf(comm, "%s: %s", prefix, name);

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

         // update the language (if in automatic mode)
         SetUserLanguage(interp);

         // update the netwop prefilter and refresh the display
         PiBox_AiStateChange();

         // apply new filter cache to reminders (done after PI-box refresh to reduce delay)
         sprintf(comm, "Reminder_UpdateTimer");
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
   UiControl_CheckDbState();

   if (target == DB_TARGET_UI)
   {  // update main window status line and other db related output
      StatsWin_StatsUpdate(DB_TARGET_UI);
      TimeScale_ProvChange();
   }
}

#ifdef USE_XMLTV_IMPORT
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
   uint  count;
   uint  cni;

   if (pUiDbContext != NULL)
   {
      if ( EpgDbContextIsMerged(pUiDbContext) )
      {
         if (EpgContextMergeGetCnis(pUiDbContext, &provCount, provCniTab))
         {
            count = 0;
            cni = 0;
            for (dbIdx=0; dbIdx < provCount; dbIdx++)
            {
               if ( IS_XMLTV_CNI(provCniTab[dbIdx]) &&
                    (EpgContextCtl_GetAiUpdateTime(provCniTab[dbIdx], FALSE) <
                       EpgContextCtl_GetAiUpdateTime(provCniTab[dbIdx], TRUE)) )
               {
                  if (count == 0)
                     cni = provCniTab[dbIdx];
                  count += 1;
               }
            }
            if (count > 0)
            {
               pDbContext = EpgContextCtl_Open(cni, TRUE, CTX_RELOAD_ERR_NONE);
               if (pDbContext != NULL)
               {
                  dprintf2("UiControl-LoadAcqDb: merge DB 0x%04X (first DB of %d)\n", cni, count);

                  if (EpgContextMergeUpdateDb(pDbContext))
                  {
                     EpgContextCtl_Close(pDbContext);

                     PiBox_Refresh();
                     UiControl_AiStateChange(DB_TARGET_UI);

                     if (count > 1)
                     {
                        AddMainIdleEvent(UiControl_LoadAcqDb, NULL, TRUE);
                     }
                  }
               }
               else
                  debug2("UiControl-LoadAcqDb: failed to load db 0x%04X for merge update (total %d CNIs)\n", cni, count);
            }
         }
      }
      else if ( EpgDbContextIsXmltv(pUiDbContext) )
      {
         cni = EpgDbContextGetCni(pUiDbContext);

         dprintf1("UiControl-LoadAcqDb: XMLTV DB 0x%04X\n", cni);

         if ( EpgContextCtl_GetAiUpdateTime(cni, FALSE) <
                EpgContextCtl_GetAiUpdateTime(cni, TRUE) )
         {
            pDbContext = EpgContextCtl_Open(cni, TRUE, CTX_RELOAD_ERR_NONE);
            if (pDbContext != NULL)
            {
               EpgContextCtl_Close(pUiDbContext);
               pUiDbContext = pDbContext;

               PiBox_Refresh();
               UiControl_AiStateChange(DB_TARGET_UI);
            }
            else
               debug1("UiControl-LoadAcqDb: failed to load db 0x%04X\n", cni);
         }
      }
      else
         dprintf0("UiControl-LoadAcqDb: ignoring trigger\n");
   }
}
#endif // USE_XMLTV_IMPORT

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
// Add or update an EPG provider channel frequency in the rc/ini file
// - called by acq control when the first AI is received after a provider change
//
void UiControlMsg_NewProvFreq( uint cni, uint freq )
{
   if ( RcFile_UpdateProvFrequency(cni, freq) )
   {
      UpdateRcFile(TRUE);
   }
}

// ----------------------------------------------------------------------------
// Inform the user about an error during reload of a database
//
void UiControl_ReloadError( ClientData clientData )
{
   MSG_RELOAD_ERR * pMsg = (MSG_RELOAD_ERR *) clientData;
   const char *pReason, *pHint;
   char * comm2 = xmalloc(2048);
#ifdef USE_XMLTV_IMPORT
   const char *pXmlPath;
#endif

   pReason = NULL;
   pHint = "";

#ifdef USE_XMLTV_IMPORT
   if (pMsg->dberr & EPGDB_RELOAD_XML_MASK)
   {
      pReason = Xmltv_TranslateErrorCode(pMsg->dberr & ~EPGDB_RELOAD_XML_MASK);
      pHint = "";
   }
   else
#endif // USE_XMLTV_IMPORT
   // translate the error result code from the reload or peek function to human readable form
   switch (pMsg->dberr)
   {
      case EPGDB_RELOAD_ACCESS:
         pReason = "of file access permissions";
         pHint = "Please make this file readable and writable for all users. ";
         break;
      case EPGDB_RELOAD_EXIST:
         pReason = "the database file does not exist";
         if (IS_XMLTV_CNI(pMsg->cni) == FALSE)
            pHint = "You should start an EPG scan from the Configure menu. ";
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
#ifdef USE_XMLTV_IMPORT
            if ( IS_XMLTV_CNI(pMsg->cni) &&
                 ((pXmlPath = XmltvCni_LookupProviderPath(pMsg->cni)) != NULL) )
               sprintf(comm2, "tk_messageBox -type ok -icon warning -message {"
                                "Failed to load XML file \"%s\" (CNI %X) because %s. "
                                "%s}\n",
                                pXmlPath, pMsg->cni, pReason, pHint);
            else
#endif
               sprintf(comm2, "tk_messageBox -type ok -icon warning -message {"
                                "Failed to load the database of provider %X because %s. "
                                "Cannot switch the TV channel to start acquisition for this provider. "
                                "%s%s}\n",
                                pMsg->cni, pReason, pHint,
                                ((pMsg->dberr == EPGDB_RELOAD_EXIST) ? "Or choose a different acquisition mode. " : ""));
            eval_check(interp, comm2);
            break;

         case CTX_RELOAD_ERR_REQ:
            if (pMsg->cni == MERGED_PROV_CNI)
               sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                                "Failed to create a merged database. %s"
                                "}\n", pHint);
#ifdef USE_XMLTV_IMPORT
            else if ( IS_XMLTV_CNI(pMsg->cni) &&
                      ((pXmlPath = XmltvCni_LookupProviderPath(pMsg->cni)) != NULL) )
               sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                                "Failed to load XML file \"%s\" (CNI %X) because %s. "
                                "%s"
                                "}\n", pXmlPath, pMsg->cni, pReason, pHint);
#endif
            else
               sprintf(comm2, "tk_messageBox -type ok -icon error -message {"
                                "Failed to load the database of provider %X because %s. "
                                "%s"
                                "}\n", pMsg->cni, pReason, pHint);
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
   const char * pTmpStr;
   char       * pSavedResult;
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
   dprintf0("UiControlMsg-NetAcqError\n");

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
                            "only the 'passive' acquisition mode is possible. "
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
   dprintf0("UiControlMsg-AcqPassive\n");

   if (uiControlInitialized)
      AddMainIdleEvent(UiControl_AcqPassive, (ClientData) NULL, TRUE);
   else
      fprintf(stderr, "nxtvepg: warning: invalid acquisition mode for the selected input source\n");
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
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            if (EpgDbContextGetCni(pUiDbContext) == 0)
            {
               AddMainIdleEvent(UiControl_AiStateChange, (ClientData) DB_TARGET_ACQ, FALSE);
            }
            break;

         case ACQ_EVENT_STATS_UPDATE:
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            TimeScale_AcqStatsUpdate();
            MenuCmd_AcqStatsUpdate();
            break;

         case ACQ_EVENT_CTL:
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            break;

         case ACQ_EVENT_PI_EXPIRED:
            TimeScale_ProvChange();
            StatsWin_StatsUpdate(DB_TARGET_UI);
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            break;

         // also sent when updating an existing DB
         case ACQ_EVENT_NEW_DB:
            AddMainIdleEvent(UiControl_LoadAcqDb, NULL, TRUE);
            // following copied from former "AI_VERSION_CHANGE"
            TimeScale_VersionChange();
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            // following is raised also from within UiControl_LoadAcqDb
            //AddMainIdleEvent(UiControl_AiStateChange, (ClientData) DB_TARGET_ACQ, FALSE);
            break;

         case ACQ_EVENT_VPS_PDC:
            StatsWin_StatsUpdate(DB_TARGET_ACQ);
            break;
      }
   }
}

// ---------------------------------------------------------------------------
// Write config data to disk
//
void UpdateRcFile( bool immediate )
{
   Tcl_DString dstr;
   const char * pRcFile = ((mainOpts.rcfile != NULL) ? mainOpts.rcfile : mainOpts.defaultRcFile);
   char * pErrMsg = NULL;
   FILE * fp;
   size_t wlen;
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
         SystemErrorMessage_Set(&pErrMsg, errno, "Write error in new config file: ", NULL);
      }

      // write GUI level sections
      if (Tcl_EvalEx(interp, "GetGuiRcData", -1, TCL_EVAL_GLOBAL) == TCL_OK)
      {
         Tcl_UtfToExternalDString(NULL, Tcl_GetStringResult(interp), -1, &dstr);
         wlen = fprintf(fp, "\n%s", Tcl_DStringValue(&dstr));

         if (wlen != 1 + Tcl_DStringLength(&dstr))
         {
            debug2("UpdateRcFile: short write: %d of %d", (int)wlen, 1 + Tcl_DStringLength(&dstr));
            writeOk = FALSE;
         }
         Tcl_DStringFree(&dstr);
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
   Tcl_Obj  * pVarObj;
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

         pVarObj = Tcl_GetVar2Ex(interp, "rcfileUpgradeStr", NULL, TCL_GLOBAL_ONLY);
         if (pVarObj != NULL)
         {
            Tcl_UtfToExternalDString(NULL, Tcl_GetString(pVarObj), -1, &msg_dstr);
            RcFile_LoadFromString(Tcl_DStringValue(&msg_dstr));
            Tcl_DStringFree(&msg_dstr);

            Tcl_UnsetVar(interp, "rcfileUpgradeStr", TCL_GLOBAL_ONLY);
         }
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

