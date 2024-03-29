/*
 *  Nextview GUI: Execute commands and control status of the menu bar
 *
 *  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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

#ifndef __MENUCMD_H
#define __MENUCMD_H


// ---------------------------------------------------------------------------
// Interface to main control module und UI control module
//
void MenuCmd_Init( void );

void AutoStartAcq( void );
void MenuCmd_AcqStatsUpdate( void );


#endif  // __MENUCMD_H
