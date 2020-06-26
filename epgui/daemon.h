/*
 *  Daemon main control
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
 *  $Id: daemon.h,v 1.6 2009/03/28 21:28:03 tom Exp tom $
 */

#ifndef __DAEMON_H
#define __DAEMON_H


// ---------------------------------------------------------------------------
// Type definitions
//
typedef enum
{
   CLOCK_CTRL_NONE,
   CLOCK_CTRL_SET,
   CLOCK_CTRL_PRINT,
   CLOCK_CTRL_COUNT
} EPG_CLOCK_CTRL_MODE;

#ifdef WIN32
#ifdef WM_USER
#define UWM_SYSTRAY     (WM_USER + 1)   // Sent by the systray
#define UWM_QUIT        (WM_USER + 2)   // Codes sent between nxtvepg instances
#define UWM_RAISE       (WM_USER + 3)
#define UWM_ICONIFY     (WM_USER + 4)
#define UWM_DEICONIFY   (WM_USER + 5)
#define UWM_ACQ_ON      (WM_USER + 6)
#define UWM_ACQ_OFF     (WM_USER + 7)
#endif
#endif  // WIN32


// ---------------------------------------------------------------------------
// Interface functions
//
void Daemon_Init( void );
void Daemon_Destroy( void );
void Daemon_Start( void );
void Daemon_Stop( void );
void Daemon_StatusQuery( void );
bool Daemon_RemoteStop( void );
void Daemon_ForkIntoBackground( void );
void Daemon_SystemClockCmd( EPG_CLOCK_CTRL_MODE clockMode, uint cni );
void Daemon_ProvScanStart( void );
void Daemon_StartDump( void );
void Daemon_UpdateRcFile( bool immediate );

#ifdef WIN32
bool Daemon_CheckIfRunning( void );
bool RemoteControlWindowCreate( WNDPROC pMsgCb, LPCSTR pClassName );
#endif

#endif // __DAEMON_H
