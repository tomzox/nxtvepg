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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: statswin.h,v 1.4 2000/06/26 18:51:07 tom Exp tom $
 */

#ifndef __STATSWIN_H
#define __STATSWIN_H


// Initialization - Interface to main module
void StatsWin_Create( void );

// Interface to database management
void StatsWin_NewPi( EPGDB_CONTEXT * usedDbc, const PI_BLOCK *pPi, uchar stream );
void StatsWin_NewAi( void );

// Interface to acq control and provider selection menu
void StatsWin_ProvChange( int target );

#endif  // __STATSWIN_H
