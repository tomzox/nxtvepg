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
 *  $Id: pilistbox.h,v 1.7 2001/02/10 16:02:07 tom Exp tom $
 */

#ifndef __PILISTBOX_H
#define __PILISTBOX_H


// Definition of PI listbox column types
typedef enum
{
   PIBOX_COL_NETNAME,
   PIBOX_COL_TIME,
   PIBOX_COL_WEEKDAY,
   PIBOX_COL_DAY,
   PIBOX_COL_DAY_MONTH,
   PIBOX_COL_DAY_MONTH_YEAR,
   PIBOX_COL_TITLE,
   PIBOX_COL_DESCR,
   PIBOX_COL_PIL,
   PIBOX_COL_THEME,
   PIBOX_COL_SOUND,
   PIBOX_COL_FORMAT,
   PIBOX_COL_ED_RATING,
   PIBOX_COL_PAR_RATING,
   PIBOX_COL_LIVE_REPEAT,
   PIBOX_COL_SUBTITLES,
   PIBOX_COL_COUNT

} PIBOX_COL_TYPES;


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
void PiListBox_UpdateNowItems( const EPGDB_CONTEXT *usedDbc );

#endif  // __PILISTBOX_H
