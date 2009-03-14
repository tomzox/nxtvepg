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
 *  $Id: uictrl.h,v 1.18 2008/10/19 14:25:55 tom Exp tom $
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
} CONTEXT_RELOAD_ERR_HAND;

enum
{
   DB_TARGET_UI   = 0,
   DB_TARGET_ACQ  = 1,
   DB_TARGET_BOTH = 2
};

typedef enum
{
   ACQ_EVENT_PROV_CHANGE,
   ACQ_EVENT_AI_VERSION_CHANGE,
   ACQ_EVENT_AI_PI_RANGE_CHANGE,
   ACQ_EVENT_STATS_UPDATE,
   ACQ_EVENT_CTL,
   ACQ_EVENT_PI_ADDED,
   ACQ_EVENT_PI_MERGED,
   ACQ_EVENT_PI_EXPIRED,
   ACQ_EVENT_NEW_DB,
   ACQ_EVENT_VPS_PDC,
} ACQ_EVENT;

// ---------------------------------------------------------------------------
// Interface to other GUI modules
#ifdef _TCL
void UiControl_AiStateChange( ClientData clientData );
void UiControl_CheckDbState( void );
void UiControl_ReloadError( ClientData clientData );
void UiControl_DisplayErrorMessage( char * pMsg );
#endif

// Interface to acquisition control
void UiControlMsg_AcqEvent( ACQ_EVENT acqEvent );
bool UiControlMsg_AcqQueueOverflow( bool prepare );
void UiControlMsg_MissingTunerFreq( uint cni );
void UiControlMsg_AcqPassive( void );
void UiControlMsg_NetAcqError( void );
void UiControlMsg_NewProvFreq( uint cni, uint freq );
uint UiControlMsg_QueryProvFreq( uint cni );
void UpdateRcFile( bool immediate );
void LoadRcFile( void );

// Interface to context control
#ifdef __EPGDBSAV_H
void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, CONTEXT_RELOAD_ERR_HAND errHand, bool isNewDb );
#endif

void UiControl_Init( void );

#endif  // __UICTRL_H
