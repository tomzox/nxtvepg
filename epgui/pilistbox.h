/*
 *  Nextview GUI: PI listbox
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
 *  $Id: pilistbox.h,v 1.16 2003/01/11 19:52:30 tom Exp tom $
 */

#ifndef __PILISTBOX_H
#define __PILISTBOX_H


// Initialization - Interface to main module
void PiListBox_Create( void );
void PiListBox_Destroy( void );
void PiListBox_Reset( void );

// Interface to filter control module
void PiListBox_Refresh( void );
const PI_BLOCK * PiListBox_GetSelectedPi( void );

// Interface to ui control module
void PiListBox_ErrorMessage( const uchar * pMessage );
void PiListBox_AiStateChange( void );

// interface to TV app interaction module
void PiListBox_GotoPi( const PI_BLOCK * pPiBlock );


#endif  // __PILISTBOX_H
