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
 *  Description:
 *
 *    This module takes background messages from lower layers for the GUI,
 *    schedules them via the main loop and then displays a text in a
 *    message box.  The module also manages side-effects from switching the
 *    browser database.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: uictrl.c,v 1.8 2001/02/04 20:22:35 tom Exp tom $
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
#include "epgctl/epgmain.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgscan.h"
#include "epgui/pifilter.h"
#include "epgui/pilistbox.h"
#include "epgui/uictrl.h"
#include "epgui/statswin.h"
#include "epgctl/epgctxctl.h"


typedef struct
{
   uint                     cni;
   EPGDB_RELOAD_RESULT      dberr;
   CONTEXT_RELOAD_ERR_HAND  errHand;
} MSG_RELOAD_ERR;


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

   if (pUiDbContext != NULL)
   {
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      if (pAiBlock == NULL)
      {  // no AI block in current database
         state = EpgAcqCtl_GetDbState(0);
      }
      else
      {  // provider present -> check for PI
         pPiBlock = EpgDbSearchFirstPi(pUiDbContext, NULL);
         if (pPiBlock == NULL)
         {  // no PI in database (probably all expired)
            state = EpgAcqCtl_GetDbState(EpgDbContextGetCni(pUiDbContext));
         }
         else
         {
            pPreFilterContext = EpgDbFilterCopyContext(pPiFilterContext);
            EpgDbFilterDisable(pPreFilterContext, FILTER_ALL & ~FILTER_NETWOP_PRE);

            pPiBlock = EpgDbSearchFirstPi(pUiDbContext, pPreFilterContext);
            if (pPiBlock == NULL)
            {  // PI present, but none match the prefilter
               state = EPGDB_PREFILTERED_EMPTY;
            }
            else
            {  // everything is ok
               state = EPGDB_OK;
            }
            EpgDbFilterDestroyContext(pPreFilterContext);
         }
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
   bool msgFromAcq = (bool) ((int) clientData);
   const AI_BLOCK *pAiBlock;

   if ( (EpgDbContextGetCni(pUiDbContext) == 0) &&
        (EpgDbContextGetCni(pAcqDbContext) != 0) &&
        (EpgScan_IsActive() == FALSE) )
   {  // UI db is completely empty (should only happen if there's no provider at all)
      dprintf1("UiControl-AiStateChange: browser db empty, switch to acq db 0x%04X\n", EpgDbContextGetCni(pAcqDbContext));
      // switch browser to acq db
      EpgContextCtl_Close(pUiDbContext);
      pUiDbContext = EpgContextCtl_Open(EpgDbContextGetCni(pAcqDbContext), CTX_RELOAD_ERR_ANY);
      // display the data from the new db
      PiListBox_Reset();
   }

   // check if the message relates to the browser db
   if ( (pUiDbContext != NULL) &&
        ((msgFromAcq == FALSE) || (pAcqDbContext == pUiDbContext)) )
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
      {  // db now empty -> reset window title
         sprintf(comm, "wm title . {Nextview EPG}\n");
         eval_check(interp, comm);
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);

      // if db is now empty, display help message
      UiControl_CheckDbState();
      // update the info in the status line below the browser window
      StatsWin_UpdateDbStatusLine(NULL);
   }
}

// ----------------------------------------------------------------------------
// Accept message from acquisition control about initial AI or AI version change
// - the message must not be handled right away, since it's sent from the
//   AI callback, when the db is not in a consistent state.
//
void UiControlMsg_AiStateChange( void )
{
   AddMainIdleEvent( UiControl_AiStateChange, (ClientData) ((int)TRUE), FALSE );
}

// ----------------------------------------------------------------------------
// Add or update an EPG provider channel frequency in the rc/ini file
// - Called by the AI callback
//
void UiControlMsg_NewProvFreq( uint cni, ulong freq )
{
   sprintf(comm, "UpdateProvFrequency 0x%04X %ld\n", cni, freq);
   eval_check(interp, comm);
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
            if (pMsg->dberr != EPGDB_RELOAD_EXIST)
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
void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, CONTEXT_RELOAD_ERR_HAND errHand )
{
   uchar * pTmpStr, * pSavedResult;
   MSG_RELOAD_ERR * pMsg;

   if (errHand != CTX_RELOAD_ERR_NONE)
   {
      pMsg          = xmalloc(sizeof(MSG_RELOAD_ERR));
      pMsg->cni     = cni;
      pMsg->dberr   = dberr;
      pMsg->errHand = errHand;

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
   AddMainIdleEvent(UiControl_MissingTunerFreq, (ClientData) cni, FALSE);
}

// ----------------------------------------------------------------------------
// Warn the user about acquisition mode error
//
static void UiControl_AcqPassive( ClientData clientData )
{
   char * comm2 = xmalloc(2048);

   sprintf(comm2, "tk_messageBox -type ok -icon warning -parent . "
                  "-message {Since the selected input source is not a TV tuner, "
                            "only the passive acquisition mode is possible. Either "
                            "select a different input source in the 'TV card input' "
                            "menu or select acquisition mode 'passive' to avoid "
                            "this message."
                            "}\n");
   eval_check(interp, comm2);
   xfree(comm2);
}

// ----------------------------------------------------------------------------
// Accept message from acq control about input source problem
//
void UiControlMsg_AcqPassive( void )
{
   AddMainIdleEvent(UiControl_AcqPassive, (ClientData) NULL, TRUE);
}

