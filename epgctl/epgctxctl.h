/*
 *  Nextview database context management
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
 *  $Id: epgctxctl.h,v 1.21 2008/10/19 14:26:57 tom Exp tom $
 */

#ifndef __EPGCTXCTL_H
#define __EPGCTXCTL_H


// ---------------------------------------------------------------------------
// Declaration of modes for handling database open failure
//
typedef enum
{
   CTX_FAIL_RET_NULL,       // return NULL pointer upon error
   CTX_FAIL_RET_DUMMY,      // return dummy context (empty db, not for acq)
   CTX_FAIL_RET_CREATE,     // create new, empty db with given CNI, for acq
} CTX_FAIL_RET_MODE;

// result codes for reload and peek (ordered by increasing user relevance)
typedef enum
{
   EPGDB_RELOAD_OK,            // no error
   EPGDB_RELOAD_ACCESS,        // file open failed
   EPGDB_RELOAD_VERSION,       // incompatible version
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
EPGDB_CONTEXT * EpgContextCtl_Open( uint cni, bool forceOpen,
                                    CTX_FAIL_RET_MODE failRetMode, int failMsgMode );
EPGDB_CONTEXT * EpgContextCtl_OpenAny( int failMsgMode );
EPGDB_CONTEXT * EpgContextCtl_OpenDummy( void );
void EpgContextCtl_Close( EPGDB_CONTEXT * pContext );
void EpgContextCtl_ClosePeek( EPGDB_CONTEXT * pDbContext );

uint EpgContextCtl_GetProvCount( void );
const uint * EpgContextCtl_GetProvList( uint * pCount );
time_t EpgContextCtl_GetAiUpdateTime( uint cni, bool reload );
void EpgContextCtl_ScanDbDir( void );

void EpgContextCtl_SetPiExpireDelay( time_t expireDelay );
void EpgContextCtl_LockDump( bool enable );

void EpgContextCtl_Init( void );
void EpgContextCtl_Destroy( void );


#endif  // __EPGCTXCTL_H
