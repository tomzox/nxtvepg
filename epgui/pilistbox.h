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
 *  $Id: pilistbox.h,v 1.14 2002/02/28 19:19:13 tom Exp tom $
 */

#ifndef __PILISTBOX_H
#define __PILISTBOX_H


// describing the state of the ui db
typedef enum
{
   EPGDB_NOT_INIT,
   EPGDB_WAIT_SCAN,
   EPGDB_PROV_SCAN,
   EPGDB_PROV_WAIT,
   EPGDB_PROV_SEL,
   EPGDB_ACQ_NO_FREQ,
   EPGDB_ACQ_NO_TUNER,
   EPGDB_ACQ_ACCESS_DEVICE,
   EPGDB_ACQ_PASSIVE,
   EPGDB_ACQ_WAIT,
   EPGDB_ACQ_WAIT_DAEMON,
   EPGDB_ACQ_OTHER_PROV,
   EPGDB_EMPTY,
   EPGDB_PREFILTERED_EMPTY,
   EPGDB_OK
} EPGDB_STATE;

// Initialization - Interface to main module
void PiListBox_Create( void );
void PiListBox_Destroy( void );
void PiListBox_Reset( void );

// Interface to filter control module
void PiListBox_Refresh( void );
const PI_BLOCK * PiListBox_GetSelectedPi( void );

#ifdef __EPGACQCTL_H
// Interface to ui control module
void PiListBox_UpdateState( EPGDB_STATE newDbState );
#endif

// Interface to database management
void PiListBox_DbInserted( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pPiBlock );
bool PiListBox_DbPreUpdate( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pObsolete, const PI_BLOCK *pPiBlock );
void PiListBox_DbPostUpdate( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pObsolete, const PI_BLOCK *pPiBlock );
void PiListBox_DbRemoved( const EPGDB_CONTEXT *usedDbc, const PI_BLOCK *pPiBlock );
void PiListBox_DbRecount( const EPGDB_CONTEXT *usedDbc );
void PiListBox_UpdateNowItems( void );
void PiListBox_GotoPi( const PI_BLOCK * pPiBlock );
void PiListBox_Lock( bool lock );

#endif  // __PILISTBOX_H
