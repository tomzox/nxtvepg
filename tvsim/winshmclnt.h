/*
 *  Communication with external TV application via shared memory
 *
 *  Copyright (C) 2002-2008 T. Zoerner
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

#ifndef __WINSHMCLNT_H
#define __WINSHMCLNT_H


// ---------------------------------------------------------------------------
// Definition of event codes that are returned by GetEpgEvent
//
typedef enum
{
   SHM_EVENT_NONE,
   SHM_EVENT_ATTACH,
   SHM_EVENT_DETACH,
   SHM_EVENT_ATTACH_ERROR,
   SHM_EVENT_STATION_INFO,
   SHM_EVENT_CMD_ARGV,
   SHM_EVENT_INP_FREQ,
   SHM_EVENT_EPG_INFO

} WINSHMCLNT_EVENT;

// ---------------------------------------------------------------------------
// Structure which holds initial parameters
//
typedef struct
{
   const char * pAppName;
   const char * tvAppPath;
   TVAPP_NAME   tvAppType;
   uint         tvFeatures;

   void      (* pCbEpgEvent)( void );

} WINSHMCLNT_TVAPP_INFO;

// ---------------------------------------------------------------------------
// declaration of service interface functions
//
char * WinSharedMemClient_GetProgInfo( void );
bool WinSharedMemClient_GetCmdArgv( uint * pArgc, uint * pArgLen, char * pCmdBuf, uint cmdMaxLen );
bool WinSharedMemClient_GetInpFreq( uint * pInputSrc, uint * pFreq, uint * pNorm );
const char * WinSharedMemClient_GetErrorMsg( void );
WINSHMCLNT_EVENT WinSharedMemClient_GetEpgEvent( void );

bool WinSharedMemClient_GrantTuner( bool doGrant );
bool WinSharedMemClient_SetStation( const char * pChanName, sint nameLen, uint cni,
                                    bool isTuner, uint freq, uint norm, uint epgPiCnt );
bool WinSharedMemClient_SetInputFreq( bool isTuner, uint freq, uint norm );
void WinSharedMemClient_HandleEpgEvent( void );
bool WinSharedMemClient_Init( const WINSHMCLNT_TVAPP_INFO * pInitInfo,
                              uint cardIdx, WINSHMCLNT_EVENT * pEvent );
void WinSharedMemClient_Exit( void );


#endif  // __WINSHMCLNT_H
