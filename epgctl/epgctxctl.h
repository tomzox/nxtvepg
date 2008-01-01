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
 *  $Id: epgctxctl.h,v 1.19 2007/01/21 11:57:41 tom Exp tom $
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

// ---------------------------------------------------------------------------
// Declaration of service interface functions
//
EPGDB_CONTEXT * EpgContextCtl_Peek( uint cni, int failMsgMode );
EPGDB_CONTEXT * EpgContextCtl_Open( uint cni, bool forceOpen,
                                    CTX_FAIL_RET_MODE failRetMode, int failMsgMode );
EPGDB_CONTEXT * EpgContextCtl_OpenAny( int failMsgMode );
EPGDB_CONTEXT * EpgContextCtl_OpenDummy( void );
EPGDB_CONTEXT * EpgContextCtl_OpenDemo( const char * pDemoDatabase );
void EpgContextCtl_Close( EPGDB_CONTEXT * pContext );
void EpgContextCtl_ClosePeek( EPGDB_CONTEXT * pDbContext );

uint EpgContextCtl_GetProvCount( bool nxtvOnly );
const uint * EpgContextCtl_GetProvList( uint * pCount );
uint EpgContextCtl_GetFreqList( uint ** ppProvList, uint ** ppFreqList );
time_t EpgContextCtl_GetAiUpdateTime( uint cni, bool reload );
uint EpgContextCtl_Remove( uint cni );
void EpgContextCtl_ScanDbDir( bool nxtvOnly );

void EpgContextCtl_SetPiExpireDelay( time_t expireDelay );
bool EpgContextCtl_UpdateFreq( uint cni, uint freq );
void EpgContextCtl_LockDump( bool enable );

void EpgContextCtl_Init( void );
void EpgContextCtl_Destroy( void );


#endif  // __EPGCTXCTL_H
