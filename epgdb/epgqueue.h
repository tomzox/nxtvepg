/*
 *  Nextview EPG block queue (FIFO)
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
 *  $Id: epgqueue.h,v 1.4 2001/12/19 14:01:12 tom Exp tom $
 */

#ifndef __EPGQUEUE_H
#define __EPGQUEUE_H


// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
void EpgDbQueue_Init( EPGDB_QUEUE * pQueue );
void EpgDbQueue_Clear( EPGDB_QUEUE * pQueue );
void EpgDbQueue_Add( EPGDB_QUEUE * pQueue, EPGDB_BLOCK * pBlock );
uint EpgDbQueue_GetBlockCount( EPGDB_QUEUE * pQueue );
EPGDB_BLOCK * EpgDbQueue_Get( EPGDB_QUEUE * pQueue );
EPGDB_BLOCK * EpgDbQueue_GetByType( EPGDB_QUEUE * pQueue, BLOCK_TYPE type );
const EPGDB_BLOCK * EpgDbQueue_Peek( EPGDB_QUEUE * pQueue );


#endif  // __EPGQUEUE_H
