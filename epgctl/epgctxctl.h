/*
 *  Nextview database context management
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

#ifndef __EPGCTXCTL_H
#define __EPGCTXCTL_H


// ---------------------------------------------------------------------------
// Declaration of modes for handling database open failure
//

// result codes for reload and peek (ordered by increasing user relevance)
typedef enum
{
   EPGDB_RELOAD_OK,            // no error
   EPGDB_RELOAD_ACCESS,        // file open failed
   EPGDB_RELOAD_MERGE,         // invalid merge config
   EPGDB_RELOAD_EXIST,         // file does not exist
   EPGDB_RELOAD_XML_CNI,       // XMLTV CNI but path unknown
   EPGDB_RELOAD_XML_MASK = 0x40000000 // XMLTV specific error
} EPGDB_RELOAD_RESULT;

// macro to compare severity of reload errors
// (to be used if multiple errors occur in a loop across all databases)
#define RELOAD_ERR_WORSE(X,Y)  ((X)>(Y))

// ---------------------------------------------------------------------------
// Declaration of service interface functions
//
EPGDB_CONTEXT * EpgContextCtl_Peek( uint cni, int failMsgMode );
EPGDB_CONTEXT * EpgContextCtl_Open( uint cni, bool forceOpen, int failMsgMode );
EPGDB_CONTEXT * EpgContextCtl_OpenDummy( void );
void EpgContextCtl_Close( EPGDB_CONTEXT * pContext );
void EpgContextCtl_ClosePeek( EPGDB_CONTEXT * pDbContext );

uint EpgContextCtl_StatProvider( const char * pXmlPath );
bool EpgContextCtl_HaveProviders( void );
const uint * EpgContextCtl_GetProvList( const char * pPath, const char * pExtension, uint * pCount );
const char * EpgContextCtl_GetProvPath( uint cni );
bool EpgContextCtl_CheckFileModified( uint cni );

void EpgContextCtl_Init( void );
void EpgContextCtl_Destroy( void );


#endif  // __EPGCTXCTL_H
