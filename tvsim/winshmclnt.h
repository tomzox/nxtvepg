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
 *  $Id: winshmclnt.h,v 1.5 2002/07/20 16:26:29 tom Exp tom $
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
   SHM_EVENT_PROG_INFO,
   SHM_EVENT_CMD_ARGV,
   SHM_EVENT_INP_FREQ

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
bool WinSharedMemClient_GetProgInfo( time_t * pStart, time_t * pStop,
                                     uchar * pThemes, uint * pThemeCount,
                                     uchar * pTitle, uint maxTitleLen );
bool WinSharedMemClient_GetCmdArgv( uint * pArgc, char * pCmdBuf, uint cmdMaxLen );
bool WinSharedMemClient_GetInpFreq( uint * pInputSrc, uint * pFreq );
const uchar * WinSharedMemClient_GetErrorMsg( void );
WINSHMCLNT_EVENT WinSharedMemClient_GetEpgEvent( void );

bool WinSharedMemClient_GrantTuner( bool doGrant );
bool WinSharedMemClient_SetStation( const char * pChanName, uint cni, uint inputSrc, uint freq );
bool WinSharedMemClient_SetInputFreq( uint inputSrc, uint freq );
void WinSharedMemClient_HandleEpgEvent( void );
bool WinSharedMemClient_Init( const WINSHMCLNT_TVAPP_INFO * pInitInfo,
                              uint cardIdx, WINSHMCLNT_EVENT * pEvent );
void WinSharedMemClient_Exit( void );


#endif  // __WINSHMCLNT_H
