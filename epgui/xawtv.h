/*
 *  Nextview decoder: xawtv remote control module
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
 *  $Id: xawtv.h,v 1.5 2002/05/11 15:52:12 tom Exp tom $
 */

#ifndef __XAWTV_H
#define __XAWTV_H


// Interface to GUI
bool Xawtv_GetFreqTab( Tcl_Interp * interp, uint ** ppFreqTab, uint * pCount );

// Initialisation
void Xawtv_Init( void );
void Xawtv_Destroy( void );


#endif  // __XAWTV_H
