/*
 *  Nextview GUI: Reminder interface to database
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
 *  $Id: piremind.h,v 1.3 2004/06/19 18:58:51 tom Exp tom $
 */

#ifndef __PIREMIND_H
#define __PIREMIND_H


// Initialization and destruction - Interface to the EPG main module
void PiRemind_Create( void );
void PiRemind_Destroy( void );
void PiRemind_CheckDb( void );

#endif  // __PIREMIND_H
