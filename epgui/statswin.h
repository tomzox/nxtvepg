/*
 *  Nextview GUI: Statistics window
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
 *  $Id: statswin.h,v 1.9 2001/04/03 19:59:12 tom Exp tom $
 */

#ifndef __STATSWIN_H
#define __STATSWIN_H


// Initialization - Interface to main module
void StatsWin_Create( bool isDemoMode );

// Interface to database management
void StatsWin_NewPi( EPGDB_CONTEXT * usedDbc, const PI_BLOCK *pPi, uchar stream );
void StatsWin_NewAi( EPGDB_CONTEXT * usedDbc );

// Interface to acq control and provider selection menu
void StatsWin_ProvChange( int target );
void StatsWin_VersionChange( void );

// Interface to UI control
#ifdef _TCL
void StatsWin_UpdateDbStatsWin( ClientData clientData );
void StatsWin_UpdateDbStatusLine( ClientData clientData );
#endif


#endif  // __STATSWIN_H
