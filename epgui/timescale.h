/*
 *  Nextview EPG GUI: PI timescale window
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
 *  $Id: timescale.h,v 1.2 2002/01/02 17:08:31 tom Exp tom $
 */

#ifndef __TIMESCALE_H
#define __TIMESCALE_H


// Initialization - Interface to main module
void TimeScale_Create( void );

// Interface to acq control and provider selection menu
void TimeScale_ProvChange( void );
void TimeScale_VersionChange( void );
void TimeScale_AcqStatsUpdate( void );


#endif  // __TIMESCALE_H
