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
 *  Description: see C source file.
 */

#ifndef __UICTRL_H
#define __UICTRL_H

#include "epgctl/epgctxctl.h"  // EPGDB_RELOAD_RESULT

// define possible handling of reload errors
typedef enum
{
   CTX_RELOAD_ERR_NONE,        // ignore all errors (e.g. during EPG scan)
   CTX_RELOAD_ERR_ANY,         // caller does not care about CNI of db
   CTX_RELOAD_ERR_ACQ,         // db requested by acq control
   CTX_RELOAD_ERR_REQ,         // db was requested by the user interaction
   CTX_RELOAD_ERR_DFLT,        // db is from previous session
} CONTEXT_RELOAD_ERR_HAND;

typedef enum
{
   ACQ_EVENT_PROV_CHANGE,
   ACQ_EVENT_STATS_UPDATE,
   ACQ_EVENT_CTL,
   ACQ_EVENT_NEW_DB,
   ACQ_EVENT_VPS_PDC,
} ACQ_EVENT;

// ---------------------------------------------------------------------------
// Interface to other GUI modules
#ifdef _TCL
void UiControl_AiStateChange( ClientData clientData );
void UiControl_CheckDbState( ClientData clientData );
void UiControl_ReloadError( ClientData clientData );
void UiControl_DisplayErrorMessage( char * pMsg );
#endif

// Interface to acquisition control
void UiControlMsg_AcqEvent( ACQ_EVENT acqEvent );
void UiControlMsg_NetAcqError( void );
void UpdateRcFile( bool immediate );
void LoadRcFile( void );

// Interface to context control
// FIXME CC errHand should be CONTEXT_RELOAD_ERR_HAND
void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, int errHand, bool isNewDb );

void UiControl_Init( void );

#endif  // __UICTRL_H
