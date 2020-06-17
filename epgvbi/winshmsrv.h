/*
 *  Communication with external TV application via shared memory
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
 *  $Id: winshmsrv.h,v 1.6 2020/06/17 19:30:22 tom Exp tom $
 */

#ifndef __WINSHMSRV_H
#define __WINSHMSRV_H

// ---------------------------------------------------------------------------
// Structure which holds callback function addresses
//
typedef struct
{
   void (* pCbTvEvent)( void );
   void (* pCbStationSelected)( void );
   void (* pCbEpgQuery)( void );
   void (* pCbTunerGrant)( bool enable );
   void (* pCbAttachTv)( bool enable, bool acqEnabled, bool slaveStateChange );
} WINSHMSRV_CB;

// ---------------------------------------------------------------------------
// Declaration of service interface functions
//
bool WintvSharedMem_Init( bool isDaemon );
void WintvSharedMem_Exit( void );
void WintvSharedMem_SetCallbacks( const WINSHMSRV_CB * pCb );

// interface to message handler on GUI level
const char * WinSharedMem_GetErrorMsg( void );
bool WintvSharedMem_GetCniAndPil( uint * pCni, uint * pPil );
bool WintvSharedMem_IsConnected( char * pAppName, uint maxNameLen, uint * pFeatures );
bool WintvSharedMem_SetEpgCommand( uint argc, const char * pArgStr, uint cmdlen );
bool WintvSharedMem_SetEpgInfo( const char * pData, uint dataLen, uint reqIdx, bool curStation );
bool WintvSharedMem_GetEpgQuery( char * pBuffer, uint maxLen );
bool WintvSharedMem_GetStation( char * pStation, uint maxLen, uint * pChanIdx, uint * pEpgCnt );
bool WintvSharedMem_StartStop( bool start, bool * pAcqEnabled );
void WintvSharedMem_HandleTvCmd( void );

// interface to Bt8x8 driver (in slave mode)
bool WintvSharedMem_ReqTvCardIdx( uint cardIdx, bool * pEpgHasDriver );
void WintvSharedMem_FreeTvCard( void );
bool WintvSharedMem_SetInputSrc( uint inputIdx );
bool WintvSharedMem_SetTunerFreq( uint freq, uint norm );
uint WintvSharedMem_GetInputSource( void );
bool WintvSharedMem_GetTunerFreq( uint * pFreq, bool * pIsTuner );
volatile EPGACQ_BUF * WintvSharedMem_GetVbiBuf( void );


#endif  // __WINVSHMSRV_H

