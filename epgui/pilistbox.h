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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: pilistbox.h,v 1.2 2000/06/13 18:24:18 tom Exp tom $
 */

#ifndef __PILISTBOX_H
#define __PILISTBOX_H


// Initialization - Interface to main module
void PiListBox_Create( void );
void PiListBox_Reset( void );

// Interface to filter control module
void PiListBox_Refresh( void );

// Interface to database management
void PiListBox_DbInserted( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pPiBlock );
void PiListBox_DbUpdated( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pPiBlock );
void PiListBox_DbRemoved( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pPiBlock );
void PiListBox_DbRecount( const EPGDB_CONTEXT *usedDbc );
void PiListBox_UpdateNowItems( const EPGDB_CONTEXT *usedDbc );
bool PiListBox_ConsistancyCheck( void );

#endif  // __PILISTBOX_H
