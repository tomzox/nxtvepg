/*
 *  Nextview GUI: programme list meta module
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
 *  $Id: pibox.h,v 1.2 2020/06/17 19:32:20 tom Exp tom $
 */

#ifndef __PIBOX_H
#define __PIBOX_H


// Initialization - Interface to main module
void PiBox_Create( void );
void PiBox_Destroy( void );

// Interface to other GUI modules
void PiBox_Reset( void );
void PiBox_Refresh( void );
void PiBox_AiStateChange( void );
void PiBox_ErrorMessage( const char * pMessage );
const PI_BLOCK * PiBox_GetSelectedPi( void );

// interface to TV app interaction module
void PiBox_GotoPi( const PI_BLOCK * pPiBlock );


#endif  // __PIBOX_H
