/*
 *  Nextview decoder: main module
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
 *  $Id: epgmain.h,v 1.23 2003/09/19 21:55:37 tom Exp tom $
 */

#ifndef __EPGMAIN_H
#define __EPGMAIN_H


// software version in form of a string
extern char *epg_version_str;
extern char epg_rcs_id_str[];

// Interface to Tcl/Tk interpreter for UI modules
#ifdef _TCL
#define TCL_COMM_BUF_SIZE  1000
extern Tcl_Interp *interp;
// add one extra byte at the end of the comm buffer for overflow detection
extern char comm[TCL_COMM_BUF_SIZE + 1];

void AddMainIdleEvent( Tcl_IdleProc *IdleProc, ClientData clientData, bool unique );
bool RemoveMainIdleEvent( Tcl_IdleProc * IdleProc, ClientData clientData, bool matchData );

bool EpgMain_StartDaemon( void );
bool EpgMain_StopDaemon( void );
#ifdef WIN32
bool EpgMain_CheckDaemon( void );
#endif
bool IsDemoMode( void );
#endif  // _TCL

// Access to databases from UI and acq-ctl
extern EPGDB_CONTEXT * pUiDbContext;

time_t EpgGetUiMinuteTime( void );

#endif  // __EPGMAIN_H
