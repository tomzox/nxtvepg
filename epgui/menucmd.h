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
 *  $Id: menucmd.h,v 1.25 2005/01/06 19:12:19 tom Exp tom $
 */

#ifndef __MENUCMD_H
#define __MENUCMD_H


// ---------------------------------------------------------------------------
// Interface to main control module und UI control module
//
void MenuCmd_Init( bool isDemoMode );

void AutoStartAcq( void );
void MenuCmd_AcqStatsUpdate( void );

void SetUserLanguage( Tcl_Interp *interp );


#endif  // __MENUCMD_H
