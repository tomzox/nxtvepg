/*
 *  Nextview GUI: programme list meta module
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
 *    This module is the parent of the actual PI listbox implementations
 *    between which the user can choose.  It performs th task of
 *    initializing and destroying the currently used child upon startup
 *    or layout change.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: pibox.c,v 1.4 2005/01/10 14:43:30 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
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
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgui/pioutput.h"
#include "epgui/pidescr.h"
#include "epgui/pilistbox.h"
#include "epgui/pinetbox.h"
#include "epgui/pibox.h"


// ----------------------------------------------------------------------------
// local variables
//
typedef enum
{
   PIBOX_TYPE_NULL,
   PIBOX_TYPE_LISTBOX,
   PIBOX_TYPE_NETBOX
} PIBOX_TYPE;

static PIBOX_TYPE pibox_type = PIBOX_TYPE_NULL;

// ----------------------------------------------------------------------------
// Forward calls
//
void PiBox_Reset( void )
{
   switch (pibox_type)
   {
      case PIBOX_TYPE_LISTBOX:
         PiListBox_Reset();
         break;
      case PIBOX_TYPE_NETBOX:
         PiNetBox_Reset();
         break;
      default:
         debug0("PiBox-Reset: ignore call while not initialized");
         break;
   }
}

void PiBox_Refresh( void )
{
   switch (pibox_type)
   {
      case PIBOX_TYPE_LISTBOX:
         PiListBox_Refresh();
         break;
      case PIBOX_TYPE_NETBOX:
         PiNetBox_Refresh();
         break;
      default:
         debug0("PiBox-Refresh: ignore call while not initialized");
         break;
   }
}

const PI_BLOCK * PiBox_GetSelectedPi( void )
{
   switch (pibox_type)
   {
      case PIBOX_TYPE_LISTBOX:
         return PiListBox_GetSelectedPi();

      case PIBOX_TYPE_NETBOX:
         return PiNetBox_GetSelectedPi();

      default:
         debug0("PiBox-GetSelectedPi: ignore call while not initialized");
         return NULL;
   }
}

void PiBox_GotoPi( const PI_BLOCK * pPiBlock )
{
   switch (pibox_type)
   {
      case PIBOX_TYPE_LISTBOX:
         PiListBox_GotoPi(pPiBlock);
         break;
      case PIBOX_TYPE_NETBOX:
         PiNetBox_GotoPi(pPiBlock);
         break;
      default:
         debug0("PiBox-GotoPi: ignore call while not initialized");
         break;
   }
}

// ----------------------------------------------------------------------------
// Copy network mapping table from global Tcl variables
//
void PiBox_AiStateChange( void )
{
   switch (pibox_type)
   {
      case PIBOX_TYPE_LISTBOX:
         PiListBox_AiStateChange();
         break;
      case PIBOX_TYPE_NETBOX:
         PiNetBox_AiStateChange();
         break;
      default:
         debug0("PiBox-AiStateChange: ignore call while not initialized");
         break;
   }
}

// ----------------------------------------------------------------------------
// Update the listbox state according to database and acq state
//
void PiBox_ErrorMessage( const uchar * pMessage )
{
   switch (pibox_type)
   {
      case PIBOX_TYPE_LISTBOX:
         PiListBox_ErrorMessage(pMessage);
         break;
      case PIBOX_TYPE_NETBOX:
         PiNetBox_ErrorMessage(pMessage);
         break;
      default:
         debug0("PiBox-ErrorMessage: ignore call while not initialized");
         break;
   }
}

// ----------------------------------------------------------------------------
// Free resources allocated by the listbox
//
void PiBox_Destroy( void )
{
   switch (pibox_type)
   {
      case PIBOX_TYPE_LISTBOX:
         PiListBox_Destroy();
         break;
      case PIBOX_TYPE_NETBOX:
         PiNetBox_Destroy();
         break;
      default:
         break;
   }
   pibox_type = PIBOX_TYPE_NULL;

   PiOutput_Destroy();
}

// ----------------------------------------------------------------------------
// Link the listbox commands with this box type
// - client data <ttp> is a boolean parameter: "isInitial": TRUE when called
//   during application start -> suppress PI box reset
//
static int PiBox_Toggle( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   Tcl_Obj * pTmpObj;
   int       newType;
   bool      isInitial = PVOID2INT(ttp);

   pTmpObj = Tcl_GetVar2Ex(interp, "pibox_type", NULL, TCL_GLOBAL_ONLY);
   if ((pTmpObj != NULL) && (Tcl_GetIntFromObj(interp, pTmpObj, &newType) == TCL_OK))
   {
      if (newType == 0)
      {
         if (pibox_type != PIBOX_TYPE_LISTBOX)
         {
            if (pibox_type == PIBOX_TYPE_NETBOX)
               PiNetBox_Destroy();
            PiListBox_Create();

            pibox_type = PIBOX_TYPE_LISTBOX;

            if (isInitial == FALSE)
            {  // if in database exception state, re-display the message
               UiControl_AiStateChange(DB_TARGET_UI);
            }
         }
      }
      else if (newType == 1)
      {
         if (pibox_type != PIBOX_TYPE_NETBOX)
         {
            if (pibox_type == PIBOX_TYPE_LISTBOX)
               PiListBox_Destroy();
            PiNetBox_Create();

            pibox_type = PIBOX_TYPE_NETBOX;

            if (isInitial == FALSE)
            {  // if in database exception state, re-display the message
               UiControl_AiStateChange(DB_TARGET_UI);
            }
         }
      }
      else
         debug1("PiBox-Toggle: unknown type %d", newType);
   }
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// create the listbox and its commands
// - this should be called only once during start-up
//
void PiBox_Create( void )
{
   Tcl_CreateObjCommand(interp, "C_PiBox_Toggle", PiBox_Toggle, INT2PVOID(FALSE), NULL);

   PiOutput_Init();

   // check if the widget is enabled; if yes allocate memory and initialize
   PiBox_Toggle(INT2PVOID(TRUE), interp, 0, NULL);
}

