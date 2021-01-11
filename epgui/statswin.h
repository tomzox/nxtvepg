/*
 *  Nextview EPG GUI: Database statistics and main window status line
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
 *  $Id: statswin.h,v 1.13 2002/01/26 15:22:04 tom Exp tom $
 */

#ifndef __STATSWIN_H
#define __STATSWIN_H


// Initialization - Interface to main module
void StatsWin_Create( void );

// Interface to ui event distribution
void StatsWin_UiStatsUpdate( bool provChange, bool dbUpdate );
void StatsWin_AcqStatsUpdate( bool provChange );


#endif  // __STATSWIN_H
