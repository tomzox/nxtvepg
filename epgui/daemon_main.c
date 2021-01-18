/*
 *  Daemon main entry
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
 *  Description:
 *
 *    This module just holds the entry function for the daemon-only
 *    process, i.e. it's the counterpart to epgmain.  However all
 *    substantial functionality is in the shared daemon module.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: daemon_main.c,v 1.13 2020/06/17 19:32:20 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#ifdef WIN32
#include <windows.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <locale.h>

#include "epgctl/mytypes.h"
#include "epgctl/epgversion.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgnetio.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/winshmsrv.h"
#include "epgvbi/syserrmsg.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgctxctl.h"
#include "epgui/uictrl.h"
#include "epgui/cmdline.h"
#include "epgui/rcfile.h"
#include "epgui/daemon.h"


#ifdef WIN32
HINSTANCE  hMainInstance;  // copy of win32 instance handle
#endif
EPGDB_CONTEXT * pUiDbContext;

void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, int errHand, bool isNewDb )
{
   char msgBuf[100];

   // non-GUI, non-acq background errors are reported only
   // at the first access and if it's not just a missing db
   if ( ((errHand != CTX_RELOAD_ERR_NONE) && (errHand != CTX_RELOAD_ERR_ANY)) ||
        ((isNewDb == FALSE) && (dberr != EPGDB_RELOAD_EXIST)) )
   {
      if (cni == MERGED_PROV_CNI)
      {
         // since the daemon doesn't merge databases this should never happen
         EpgNetIo_Logger(LOG_ERR, -1, 0, "database merge failed - check configuration", NULL);
      }
      else
      {
         sprintf(msgBuf, "failed to load database 0x%04X (code %d)", cni, dberr);
         EpgNetIo_Logger(LOG_WARNING, -1, 0, msgBuf, NULL);
      }
   }
}
void UiControlMsg_NetAcqError( void )
{
   // cannot occur in daemon mode (client lost connection to daemon)
}
void UiControlMsg_AcqEvent( ACQ_EVENT acqEvent )
{
   // no GUI - nothing to do here
}

// ---------------------------------------------------------------------------
// Write config data to disk
//
void UpdateRcFile( bool immediate )
{
   Daemon_UpdateRcFile(immediate);
}

// ---------------------------------------------------------------------------
// Win32 exception handler
// - invoked by OS when fatal exceptions occur (e.g. page fault)
// - used to cleanly shut down driver, then exit
//
#ifdef __MINGW32__
static LONG WINAPI WinApiExceptionHandler(struct _EXCEPTION_POINTERS *exc_info)
{
   //debug1("FATAL exception caught: %d", GetExceptionCode());
   debug0("FATAL exception caught");
   // emergency driver shutdown - do as little as possible here (skip db dump etc.)
   EpgAcqCtl_Destroy(TRUE);
   ExitProcess(-1);
   // dummy return
   return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// ---------------------------------------------------------------------------
// entry point
//
#ifdef WIN32
int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
#else
int main( int argc, char *argv[] )
#endif
{
   #ifdef WIN32
   int argc;
   char ** argv;
   #endif

   // set db states to not initialized
   pUiDbContext = NULL;

   // set up the user-configured locale
   setlocale(LC_ALL, "");
   setlocale(LC_NUMERIC, "C");  // required for Tcl or parsing of floating point numbers fails

   EpgLtoInit();

   #ifdef WIN32
   hMainInstance = hInstance;
   // set up an handler to catch fatal exceptions
   #ifndef __MINGW32__
   __try {
   #else
   SetUnhandledExceptionFilter(WinApiExceptionHandler);
   #endif
   CmdLine_WinApiSetArgv(&argc, &argv);
   #endif  // WIN32

   CmdLine_Process(argc, argv, TRUE);

   #ifdef USE_DAEMON
   if ( (mainOpts.optDaemonMode == DAEMON_RUN) && !IS_DUMP_MODE(mainOpts) )
   {  // deamon mode -> detach from tty
      Daemon_ForkIntoBackground();
   }
   #endif

   // scan the database directory for provider databases
   EpgContextCtl_Init();

   if ( !IS_DUMP_MODE(mainOpts) )
   {
      // UNIX must fork the VBI slave before GUI startup or the slave will inherit all X11 file handles
      EpgAcqCtl_Init();
      #ifdef WIN32
      WintvSharedMem_Init(TRUE);
      #endif
   }

   Daemon_Init();

   if (mainOpts.optDaemonMode == DAEMON_RUN)
   {  // Daemon mode: no GUI - just do acq and handle requests from GUI via sockets
      Daemon_Start();
   }
   else if (mainOpts.optDaemonMode == DAEMON_QUERY)
   {
      Daemon_StatusQuery();
   }
   else if (mainOpts.optDaemonMode == DAEMON_STOP)
   {
      Daemon_Stop();
   }
   else if (mainOpts.optDaemonMode == EPG_CLOCK_CTRL)
   {
      Daemon_SystemClockCmd(mainOpts.optClockSubMode);
   }
   else if ( IS_DUMP_MODE(mainOpts) )
   {
      Daemon_StartDump();
   }
   else
   {
      fatal1("not in any expected mode: opmode %d\n", mainOpts.optDaemonMode);
   }
   Daemon_Destroy();

   #if defined(WIN32) && !defined(__MINGW32__)
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {  // caught a fatal exception -> stop the driver to prevent system crash ("blue screen")
      debug1("FATAL exception caught: %d", GetExceptionCode());
      // emergency driver shutdown - do as little as possible here (skip db dump etc.)
      EpgAcqCtl_Destroy(TRUE);
      ExitProcess(-1);
   }
   #endif

   if ( !IS_DUMP_MODE(mainOpts) )
   {
      #ifdef WIN32
      WintvSharedMem_Exit();
      #endif
      EpgAcqCtl_Destroy(FALSE);
   }

   EpgContextCtl_Destroy();
   #ifdef WIN32
   xfree(argv);
   #endif
   CmdLine_Destroy();

   #if CHK_MALLOC == ON
   // check for allocated memory that was not freed
   chk_memleakage();
   #endif

   return 0;
}

