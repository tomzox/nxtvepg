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
 *  $Id: epgmain.h,v 1.18 2002/01/26 15:20:12 tom Exp tom $
 */

#ifndef __EPGMAIN_H
#define __EPGMAIN_H


// software version in form of a string
extern char *epg_version_str;
extern char epg_rcs_id_str[];

// Interface to Tcl/Tk interpreter for UI modules
#ifdef _TCL
extern Tcl_Interp *interp;
extern char comm[1000];

int eval_check(Tcl_Interp *i, char *c);
int eval_global(Tcl_Interp *interp, char *cmd);
void AddMainIdleEvent( Tcl_IdleProc *IdleProc, ClientData clientData, bool unique );
bool RemoveMainIdleEvent( Tcl_IdleProc * IdleProc, ClientData clientData, bool matchData );

bool EpgMain_StartDaemon( void );
bool EpgMain_StopDaemon( void );
bool IsDemoMode( void );
#endif  // _TCL

// Access to databases from UI and acq-ctl
extern EPGDB_CONTEXT * pUiDbContext;

#endif  // __EPGMAIN_H
