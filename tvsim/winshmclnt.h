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
 *  $Id: winshmclnt.h,v 1.1 2002/04/29 18:38:11 tom Exp tom $
 */

#ifndef __WINSHMCLNT_H
#define __WINSHMCLNT_H


// ---------------------------------------------------------------------------
// Structure which holds callback function addresses and initial parameters
//
typedef struct
{
   const char * pAppName;
   uint         tvFeatures;

   void (* pCbEpgEvent)( void );
   void (* pCbUpdateProgInfo)( const char * pTitle, time_t start, time_t stop, uchar themeCount, const uchar * pThemes );
   void (* pCbHandleEpgCmd)( uint argc, const char * pArgStr );
   void (* pCbReqTuner)( uint inputSrc, uint freq );
   void (* pCbAttach)( bool attach );

} WINSHMCLNT_TVAPP_INFO;

// ---------------------------------------------------------------------------
// declaration of service interface functions
//
bool WinSharedMemClient_GrantTuner( bool doGrant );
bool WinSharedMemClient_SetStation( const char * pChanName, uint cni, uint inputSrc, uint freq );
bool WinSharedMemClient_SetInputFreq( uint inputSrc, uint freq );
void WinSharedMemClient_HandleEpgEvent( void );
bool WinSharedMemClient_Init( const WINSHMCLNT_TVAPP_INFO * pInitInfo );
void WinSharedMemClient_Exit( void );


#endif  // __WINSHMCLNT_H


