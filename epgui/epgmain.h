/*
 *  Nextview decoder: main module
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

#ifndef __EPGMAIN_H
#define __EPGMAIN_H


// Interface to Tcl/Tk interpreter for UI modules
#ifdef _TCL
#define TCL_COMM_BUF_SIZE  1000
extern Tcl_Interp *interp;
// add one extra byte at the end of the comm buffer for overflow detection
extern char comm[TCL_COMM_BUF_SIZE + 1];

void AddMainIdleEvent( Tcl_IdleProc *IdleProc, ClientData clientData, bool unique );
bool RemoveMainIdleEvent( Tcl_IdleProc * IdleProc, ClientData clientData, bool matchData );

typedef enum
{
   EPG_ENC_UTF8,        // text from XMLTV import or Tcl/Tk
   EPG_ENC_SYSTEM,      // text from current locale (e.g. weekday names) or local files
   EPG_ENC_ISO_8859_1,  // hard-coded ISO-8859-1 (CNI network name table)
} T_EPG_ENCODING;
extern Tcl_Encoding encIso88591;

Tcl_Obj * TranscodeToUtf8( T_EPG_ENCODING enc,
                           const char * pPrefix, const char * pStr, const char * pPostfix
                           /*,const char * pCallerFile, int callerLine*/ );
//#define TranscodeToUtf8(E,S0,S1,S2) TranscodeToUtf8_Dbg(E,S0,S1,S2,__FILE__,__LINE__)

bool EpgMain_StartDaemon( void );
bool EpgMain_StopDaemon( void );
#ifdef WIN32
bool EpgMain_CheckDaemon( void );
#endif
#endif  // _TCL

// Access to databases from UI and acq-ctl
extern EPGDB_CONTEXT * pUiDbContext;

time_t EpgGetUiMinuteTime( void );

#endif  // __EPGMAIN_H
