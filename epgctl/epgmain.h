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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgmain.h,v 1.9 2000/09/20 10:57:51 tom Exp tom $
 */

#ifndef __EPGMAIN_H
#define __EPGMAIN_H


// local time offset (in minutes relative to GMT)
extern sint lto;

// Interface to Tcl/Tk interpreter for UI modules
#ifdef _TCL
extern Tcl_Interp *interp;
extern char comm[1000];

int eval_check(Tcl_Interp *i, char *c);
#endif  // _TCL

// Access to databases from UI and acq-ctl
#ifdef __EPGBLOCK_H
extern EPGDB_CONTEXT * pUiDbContext;
extern EPGDB_CONTEXT * pAcqDbContext;
#endif

// database directory
extern const char * dbdir;

#endif  // __EPGMAIN_H
