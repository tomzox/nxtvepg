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
 *
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgdbmgmt.h,v 1.18 2003/10/05 19:12:57 tom Exp tom $
 */

#ifndef __EPGDBMGMT_H
#define __EPGDBMGMT_H


typedef struct 
{
   bool (* pAiCallback)( const AI_BLOCK *pNewAi );
} EPGDB_ADD_CB;


// ----------------------------------------------------------------------------
// function declarations, to be shared only with other db-internal modules
//
bool EpgDbCheckChains( const EPGDB_CONTEXT * dbc );

void EpgDbPiRemove( EPGDB_CONTEXT * dbc, EPGDB_BLOCK * pObsolete );
void EpgDbLinkPi( EPGDB_CONTEXT * dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK * pPrev, EPGDB_BLOCK * pNext );
void EpgDbReplacePi( EPGDB_CONTEXT * dbc, EPGDB_BLOCK * pObsolete, EPGDB_BLOCK * pBlock );
bool EpgDbAddDefectPi( EPGDB_CONTEXT * dbc, EPGDB_BLOCK *pBlock );

// ----------------------------------------------------------------------------
// Declaration of database management functions
// - the "Get" functions are located in the interface module EpgDbIf.c
// - the block structures are defined in EpgBlock.h
//
EPGDB_CONTEXT * EpgDbCreate( void );
void EpgDbDestroy( EPGDB_CONTEXT * dbc, bool keepAiOi );

#ifdef __EPGTSCQUEUE_H
void EpgDbProcessQueue( EPGDB_CONTEXT * const * pdbc, EPGDB_QUEUE * pQueue,
                        EPGDB_PI_TSC * tsc, const EPGDB_ADD_CB * pCb );
#endif


#endif  // __EPGDBMGMT_H
