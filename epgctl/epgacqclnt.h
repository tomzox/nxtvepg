/*
 *  Nextview EPG network acquisition client
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
 *  $Id: epgacqclnt.h,v 1.3 2002/05/19 21:53:54 tom Exp tom $
 */

#ifndef __EPGACQCLNT_H
#define __EPGACQCLNT_H

#include "epgdb/epgnetio.h"

// ---------------------------------------------------------------------------
// Structures for callback installation
//
typedef struct
{
   int          fd;
   bool         blockOnRead;
   bool         blockOnWrite;
   bool         blockOnConnect;
   bool         processQueue;
   #ifdef WIN32
   int          errCode;
   #endif
} EPGACQ_EVHAND;

// ---------------------------------------------------------------------------
// Description of the current connection status
// - to be used for user information only, e.g. in the main window status line
//
typedef enum
{
   NETDESCR_DISABLED,
   NETDESCR_ERROR,
   NETDESCR_CONNECT,
   NETDESCR_STARTUP,
   NETDESCR_LOADING,
   NETDESCR_RUNNING,
} NETDESCR_STATE;

typedef struct
{
   NETDESCR_STATE state;
   const char   * cause;
   ulong          rxTotal;
   time_t         rxStartTime;

} EPGDBSRV_DESCR;

// ----------------------------------------------------------------------------
// Declaration of the service interface functions
//
void EpgAcqClient_Init( void (* pUpdateEvHandler) ( EPGACQ_EVHAND * pAcqEv ) );
void EpgAcqClient_Destroy( void );
bool EpgAcqClient_SetAddress( const char * pHostName, const char * pPort );
#ifdef __EPGTSCQUEUE_H
bool EpgAcqClient_StartAcq( uint * pCniTab, uint cniCount,
                           EPGDB_QUEUE * pDbQueue, EPGDB_PI_TSC *pTscQueue );
#endif
void EpgAcqClient_StopAcq( void );
void EpgAcqClient_HandleSocket( EPGACQ_EVHAND * pAcqEv );
bool EpgAcqClient_CheckTimeouts( void );
bool EpgAcqClient_ChangeProviders( const uint * pCniTab, uint cniCount );
bool EpgAcqClient_SetAcqStatsMode( bool enable );
bool EpgAcqClient_SetAcqTscMode( bool enable,  bool allProviders );
bool EpgAcqClient_SetVpsPdcMode( bool enable, bool update );
bool EpgAcqClient_IsLocalServer( void );
bool EpgAcqClient_DescribeNetState( EPGDBSRV_DESCR * pNetState );
bool EpgAcqClient_GetNetStats( EPGDB_STATS * pAcqStats, EPGACQ_DESCR * pAcqDescr, bool * pAiFollows );


#endif  // __EPGACQCLNT_H
