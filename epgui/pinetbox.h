/*
 *  Nextview GUI: PI listbox in "network cols" layout
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
 *  $Id: pinetbox.h,v 1.4 2003/02/09 17:37:03 tom Exp tom $
 */

#ifndef __PINETBOX_H
#define __PINETBOX_H


// Initialization - Interface to main module
void PiNetBox_Create( void );
void PiNetBox_Destroy( void );
void PiNetBox_Reset( void );
void PiNetBox_Refresh( void );

void PiNetBox_AiStateChange( void );
void PiNetBox_ErrorMessage( const uchar * pMessage );

const PI_BLOCK * PiNetBox_GetSelectedPi( void );
void PiNetBox_GotoPi( const PI_BLOCK * pPiBlock );


#endif  // __PINETBOX_H
