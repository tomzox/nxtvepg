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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: uictrl.h,v 1.6 2001/09/12 18:48:27 tom Exp tom $
 */

#ifndef __UICTRL_H
#define __UICTRL_H


// define possible handling of reload errors
typedef enum
{
   CTX_RELOAD_ERR_NONE,        // ignore all errors (e.g. during EPG scan)
   CTX_RELOAD_ERR_ANY,         // caller does not care about CNI of db
   CTX_RELOAD_ERR_ACQ,         // db requested by acq control
   CTX_RELOAD_ERR_REQ,         // db was requested by the user interaction
   CTX_RELOAD_ERR_DEMO         // demo database
} CONTEXT_RELOAD_ERR_HAND;

// ---------------------------------------------------------------------------
// Interface to other GUI modules
#ifdef _TCL
void UiControl_AiStateChange( ClientData clientData );
void UiControl_CheckDbState( void );
void UiControl_ReloadError( ClientData clientData );
#endif

// Interface to acquisition control
void UiControlMsg_AiStateChange( void );
void UiControlMsg_MissingTunerFreq( uint cni );
void UiControlMsg_AcqPassive( void );
void UiControlMsg_NewProvFreq( uint cni, ulong freq );

// Interface to context control
#ifdef __EPGDBSAV_H
void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, CONTEXT_RELOAD_ERR_HAND errHand );
#endif

void UiControl_Init( void );

#endif  // __UICTRL_H
