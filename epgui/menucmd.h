/*
 *  Nextview GUI: Execute commands and control status of the menu bar
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
 *  $Id: menucmd.h,v 1.20 2003/02/08 14:35:48 tom Exp tom $
 */

#ifndef __MENUCMD_H
#define __MENUCMD_H


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
void MenuCmd_Init( bool isDemoMode );
void OpenInitialDb( uint startUiCni );

void SetUserLanguage( Tcl_Interp *interp );
void SetAcquisitionMode( NETACQ_SET_MODE netAcqSetMode );
bool SetDaemonAcquisitionMode( uint cmdLineCni, bool forcePassive );
int  SetHardwareConfig( Tcl_Interp *interp, int cardIndex );
void SetNetAcqParams( Tcl_Interp * interp, bool isServer );
void AutoStartAcq( Tcl_Interp * interp );
void MenuCmd_AcqStatsUpdate( void );
uint GetProvFreqForCni( uint provCni );
EPGDB_CONTEXT * MenuCmd_MergeDatabases( void );
#ifdef WIN32
bool MenuCmd_CheckTvCardConfig( void );
#endif


#endif  // __MENUCMD_H
