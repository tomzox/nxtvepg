/*
 *  Nextview EPG block database management
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
 *  $Id: epgdbmgmt.h,v 1.10 2000/12/17 18:41:17 tom Exp tom $
 */

#ifndef __EPGDBMGMT_H
#define __EPGDBMGMT_H


// ----------------------------------------------------------------------------
// function declarations, to be shared only with other db-internal modules
//
bool EpgDbCheckChains( const EPGDB_CONTEXT * dbc );
bool EpgDbPiCmpBlockNoGt( const EPGDB_CONTEXT * dbc, uint no1, uint no2, uchar netwop );
uint EpgDbGetGenericMaxCount( const EPGDB_CONTEXT * dbc, BLOCK_TYPE type );
bool EpgDbPiBlockNoValid( const EPGDB_CONTEXT * dbc, uint block_no, uchar netwop );

void EpgDbPiRemove( EPGDB_CONTEXT * dbc, EPGDB_BLOCK * pObsolete );
void EpgDbLinkPi( EPGDB_CONTEXT * dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK * pPrev, EPGDB_BLOCK * pNext );
void EpgDbReplacePi( EPGDB_CONTEXT * dbc, EPGDB_BLOCK * pObsolete, EPGDB_BLOCK * pBlock );

// ----------------------------------------------------------------------------
// Declaration of database management functions
// - the "Get" functions are located in the interface module EpgDbIf.c
// - the block structures are defined in EpgBlock.h
//
EPGDB_CONTEXT * EpgDbCreate( void );
void EpgDbDestroy( EPGDB_CONTEXT * dbc );

void EpgDbSetDateTime( EPGDB_CONTEXT * dbc );

bool EpgDbAddBlock( EPGDB_CONTEXT * dbc, EPGDB_BLOCK * pBlock );


#endif  // __EPGDBMGMT_H
