/*
 *  Nextview EPG acquisition and database parameter setup
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
 *  $Id: epgsetup.h,v 1.1 2005/01/06 19:12:04 tom Exp tom $
 */

#ifndef __EPGSETUP_H
#define __EPGSETUP_H


// enum parameter to Set-Acquisition-Mode
typedef enum
{
   NETACQ_DEFAULT,   // set netacq mode if netacq_enable == TRUE
   NETACQ_INVERT,    // set inverse of netacq_enable
   NETACQ_YES,       // enable network mode
   NETACQ_NO,        // disable network mode
   NETACQ_KEEP       // keep currently active mode if acq running; else netacq_enable
} NETACQ_SET_MODE;

// ---------------------------------------------------------------------------
// Interface to main control module und UI control module
//
void EpgSetup_OpenUiDb( uint startUiCni );
void EpgSetup_DbExpireDelay( void );
void EpgSetup_AcquisitionMode( NETACQ_SET_MODE netAcqSetMode );
bool EpgSetup_DaemonAcquisitionMode( uint cmdLineCni, bool forcePassive, int maxPhase );
void EpgSetup_CardDriver( int cardIndex );
void EpgSetup_NetAcq( bool isServer );
EPGDB_CONTEXT * EpgSetup_MergeDatabases( void );
bool IsRemoteAcqDefault( void );
#ifdef WIN32
bool EpgSetup_CheckTvCardConfig( void );
#endif

#endif  // __EPGSETUP_H

