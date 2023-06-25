/*
 *  Nextview EPG block database management
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *  Description: see C source file.
 */

#ifndef __EPGDBMGMT_H
#define __EPGDBMGMT_H


// ----------------------------------------------------------------------------
// function declarations, to be shared only with other db-internal modules
//
bool EpgDbCheckChains( const EPGDB_CONTEXT * dbc );

void EpgDbMergeLinkNetworkPi( EPGDB_CONTEXT * dbc, EPGDB_PI_BLOCK ** pFirstNetwopBlock );
void EpgDbReplacePi( EPGDB_CONTEXT * dbc, EPGDB_PI_BLOCK * pObsolete, EPGDB_PI_BLOCK * pBlock );
bool EpgDbAddDefectPi( EPGDB_CONTEXT * dbc, EPGDB_PI_BLOCK *pBlock );

// ----------------------------------------------------------------------------
// Declaration of database management functions
// - the "Get" functions are located in the interface module EpgDbIf.c
// - the block structures are defined in EpgBlock.h
//
EPGDB_CONTEXT * EpgDbCreate( void );
void EpgDbDestroy( EPGDB_CONTEXT * dbc, bool keepAi );

#endif  // __EPGDBMGMT_H
