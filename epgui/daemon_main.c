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
 *  $Id: daemon_main.c,v 1.3 2005/01/08 15:16:29 tom Exp tom $
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
#include "epgdb/epgdbsav.h"
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

void UiControlMsg_NewProvFreq( uint cni, uint freq )
{
   if ( RcFile_UpdateProvFrequency(cni, freq) )
   {
      UpdateRcFile(TRUE);
   }
}
uint UiControlMsg_QueryProvFreq( uint cni )
{
   return RcFile_GetProvFreqForCni(cni);
}
void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, CONTEXT_RELOAD_ERR_HAND errHand, bool isNewDb )
{
   uchar msgBuf[100];

   // non-GUI, non-acq background errors are reported only
   // at the first access and if it's not just a missing db
   if ( ((errHand != CTX_RELOAD_ERR_NONE) && (errHand != CTX_RELOAD_ERR_ANY)) ||
        ((isNewDb == FALSE) && (dberr != EPGDB_RELOAD_EXIST)) )
   {
      if (cni == 0x00ff)
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
void UiControlMsg_MissingTunerFreq( uint cni )
{
   uchar msgBuf[100];

   sprintf(msgBuf, "cannot tune channel for provider 0x%04X: frequency unknown", cni);
   EpgNetIo_Logger(LOG_NOTICE, -1, 0, msgBuf, NULL);
}
void UiControlMsg_NetAcqError( void )
{
   // cannot occur in daemon mode (client lost connection to daemon)
}
void UiControlMsg_AcqPassive( void )
{
   EpgNetIo_Logger(LOG_ERR, -1, 0, "invalid acquisition mode for the selected input source", NULL);
}
void UiControlMsg_AcqEvent( ACQ_EVENT acqEvent )
{
   // no GUI - nothing to do here
}
bool UiControlMsg_AcqQueueOverflow( bool prepare )
{
   // unused (also in GUI)
   return FALSE;
}

#include "epgui/dumpraw.h"
void EpgDumpRaw_IncomingBlock( const EPGDB_BLOCK_UNION * pUnion, BLOCK_TYPE type, uchar stream )
{
}
void EpgDumpRaw_IncomingUnknown( BLOCK_TYPE type, uint size, uchar stream )
{
}

// ---------------------------------------------------------------------------
// Write config data to disk
// - equivalent function is in epgmain.c, but the difference here is that non-GUI
//   sections are read from the old config file and copied into the new one
// - note the copy is repeated intentionally upon every save to avoid overwriting
//   more recent data
//
void UpdateRcFile( bool immediate )
{
   static bool reportErr = TRUE;
   char * pErrMsg = NULL;
   char * pGuiBuf;
   uint   bufLen;
   FILE * fp;
   size_t wlen;
   bool   writeOk;

   pGuiBuf = NULL;
   if ( RcFile_CopyForeignSections(mainOpts.rcfile, &pGuiBuf, &bufLen) )
   {
      fp = RcFile_WriteCreateFile(mainOpts.rcfile, &pErrMsg);
      if (fp != NULL)
      {
         // write C level sections
         writeOk = RcFile_WriteOwnSections(fp) && (fflush(fp) == 0);

         // copy GUI sections
         if (pGuiBuf != NULL)
         {
            fwrite("\n", sizeof(char), 1, fp);

            wlen = fwrite(pGuiBuf, sizeof(char), bufLen, fp);

            if (wlen != bufLen)
            {
               debug2("UpdateRcFile: short write: %d of %d", (int)wlen, bufLen);
               writeOk = FALSE;
            }
         }

         if (ferror(fp) && (pErrMsg == NULL))
         {
            SystemErrorMessage_Set(&pErrMsg, errno, "Write error in new config file: ", NULL);
         }

         RcFile_WriteCloseFile(fp, writeOk, mainOpts.rcfile, &pErrMsg);
      }
   }

   if (pErrMsg != NULL)
   {
      if (reportErr)
      {
         if (mainOpts.optDaemonMode == DAEMON_RUN)
         {
            EpgNetIo_Logger(LOG_ERR, -1, 0, pErrMsg, NULL);
         }
         else
         {
#ifndef WIN32
            fprintf(stderr, "nxtvepg: %s\n", pErrMsg);
#else
            MessageBox(NULL, pErrMsg, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
#endif
         }
      }
      reportErr = FALSE;
   }
   else
      reportErr = TRUE;

   if (pGuiBuf != NULL)
      xfree(pGuiBuf);
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
   if (mainOpts.optDaemonMode == DAEMON_RUN)
   {  // deamon mode -> detach from tty
      Daemon_ForkIntoBackground();
   }
   #endif

   // set up the directory for the databases (unless in demo mode)
   if (EpgDbSavSetupDir(mainOpts.dbdir, mainOpts.pDemoDatabase) == FALSE)
   {  // failed to create dir: message was already issued, so just exit
      exit(-1);
   }
   // scan the database directory for provider databases
   EpgContextCtl_InitCache();

   if ( !IS_DUMP_MODE(mainOpts) )
   {
      // UNIX must fork the VBI slave before GUI startup or the slave will inherit all X11 file handles
      EpgAcqCtl_Init();
      #ifdef WIN32
      WintvSharedMem_Init();
      #endif
   }

   Daemon_Init();

   if (mainOpts.optDaemonMode == DAEMON_RUN)
   {  // Daemon mode: no GUI - just do acq and handle requests from GUI via sockets
      Daemon_Start();
   }
   else if (mainOpts.optDaemonMode == DAEMON_STOP)
   {
      Daemon_Stop();
   }
   else if (mainOpts.optDaemonMode == EPG_CLOCK_CTRL)
   {
      Daemon_SystemClockCmd(mainOpts.optDumpSubMode, mainOpts.startUiCni);
   }
   else if (mainOpts.optDaemonMode == EPG_CL_PROV_SCAN)
   {
      Daemon_ProvScanStart();
   }
   else
      fatal1("unexpectedly not in daemon mode: mode %d\n", mainOpts.optDaemonMode);

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

   #if CHK_MALLOC == ON
   EpgContextCtl_ClearCache();
   #ifdef WIN32
   xfree(argv);
   #else
   xfree((char*)mainOpts.defaultDbDir);
   #endif
   // check for allocated memory that was not freed
   chk_memleakage();
   #endif

   return 0;
}

