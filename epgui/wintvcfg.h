/*
 *  Retrieve channel table and TV card config from TV app INI files
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
 *  $Id: wintvcfg.h,v 1.4 2004/01/24 18:54:34 tom Exp tom $
 */

#ifndef __WINTVCFG_H
#define __WINTVCFG_H


// Interface to GUI
bool WintvCfg_GetFreqTab( Tcl_Interp * interp, uint ** pFreqTab, uint * pCount );
bool WintvCfg_IsEnabled( void );

// Interface to TV application interaction
uint WintvCfg_StationNameToCni( char * pName, uint MapName2Cni(const char * station) );
bool WintvCfg_CheckAirTimes( uint cni );

// Initialisation
void WintvCfg_Init( bool enableChanTabFilter );
void WintvCfg_Destroy( void );


#endif  // __WINTVCFG_H
