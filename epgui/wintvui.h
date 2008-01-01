/*
 *  Tcl interface and helper functions to TV app configuration
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: wintvui.h,v 1.1 2006/11/26 12:56:10 tom Exp tom $
 */

#ifndef __WINTVUI_H
#define __WINTVUI_H


// Interface to TV application interaction
uint WintvUi_StationNameToCni( char * pName, uint MapName2Cni(const char * station) );
bool WintvUi_CheckAirTimes( uint cni );

// Initialisation
void WintvUi_Init( void );
void WintvUi_Destroy( void );


#endif  // __WINTVUI_H
