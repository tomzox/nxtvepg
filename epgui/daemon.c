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
 *  Description:
 *
 *    This module contains the acquisition daemon's top control layer.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: daemon.c,v 1.20 2020/06/15 09:59:15 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#ifndef WIN32
#include <unistd.h>
#include <sys/param.h>
#include <sys/time.h>
#include <pwd.h>
#include <signal.h>
#else
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <pbt.h>
#endif
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/epgversion.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgnetio.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgctl/epgscan.h"
#include "epgui/uictrl.h"
#include "epgui/epgsetup.h"
#include "epgui/epgquery.h"
#include "epgui/rcfile.h"
#include "epgui/pdc_themes.h"
#include "epgui/pidescr.h"
#include "epgui/dumptext.h"
#include "epgui/dumpraw.h"
#include "epgui/dumpxml.h"
#include "epgctl/epgctxctl.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/winshmsrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/syserrmsg.h"
#include "epgvbi/tvchan.h"
#include "epgui/cmdline.h"
#include "epgui/epgmain.h"
#include "epgui/daemon.h"


#ifdef USE_DAEMON

static bool should_exit;
#ifdef WIN32
extern HINSTANCE  hMainInstance;  // copy of win32 instance handle
#endif

// ---------------------------------------------------------------------------
// Fetch current time from teletext
//
void Daemon_SystemClockCmd( EPG_CLOCK_CTRL_MODE clockMode, uint cni )
{
   EPGDB_CONTEXT * pPeek;
   bool  isTuner;
   uint  freq;
   uint  waitCnt;
   sint  lto;
   time_t ttxTime;

   EpgSetup_CardDriver(mainOpts.videoCardIndex);

   BtDriver_SelectSlicer(VBI_SLICER_ZVBI);
   BtDriver_SetChannelProfile(VBI_CHANNEL_PRIO_BACKGROUND, 0, 0, 0);

   if (BtDriver_StartAcq())
   {
      if (cni != 0)
      {
         freq = RcFile_GetProvFreqForCni(cni);
         if (freq == 0)
         {
            pPeek = EpgContextCtl_Peek(cni, CTX_RELOAD_ERR_ACQ);
            if (pPeek != NULL)
            {
               freq = pPeek->tunerFreq;
               EpgContextCtl_ClosePeek(pPeek);
            }
            else
               debug1("EpgAcqCtl-UpdateProvider: peek for 0x%04X failed", cni);
         }
      }
      else
         freq = 0;

      if ( BtDriver_TuneChannel(RcFile_Query()->tvcard.input, freq, FALSE, &isTuner) )
      {
         if ( isTuner && (freq == 0) && (cni != 0) )
         {
            fprintf(stderr, "nxtvepg: warning: cannot tune channel for provider 0x%04X: "
                            "frequency unknown\n", cni);
         }
      }
      else
         fprintf(stderr, "Failed to tune provider channel: %s\n", BtDriver_GetLastError());

      ttxTime = 0;
      waitCnt = 0;
      while ((should_exit == FALSE) && (waitCnt < 5*25))
      {
#ifndef WIN32
         struct timeval  tv;
         tv.tv_sec  = 0;
         tv.tv_usec = 40 * 1000L;  // 1/25 sec = 40 ms
         select(0, NULL, NULL, NULL, &tv);
#else
         Sleep(40);
#endif

         ttxTime = TtxDecode_GetDateTime(&lto);
         if (ttxTime != 0)
            break;
         waitCnt += 1;
      }

      if (ttxTime != 0)
      {
         dprintf3("... offset %d to UTC: GMT%+d %s", (int)(time(NULL)-ttxTime), lto/(60*60), ctime(&ttxTime));
         switch (clockMode)
         {
            case CLOCK_CTRL_SET:
            {
#ifdef WIN32
               SYSTEMTIME st;
               struct tm * ptm;

               ptm = gmtime(&ttxTime);
               memset(&st, 0, sizeof(st));
               st.wYear = ptm->tm_year + 1900;
               st.wMonth = ptm->tm_mon + 1;
               st.wDay = ptm->tm_mday;
               st.wHour = ptm->tm_hour;
               st.wMinute = ptm->tm_min;
               st.wSecond = ptm->tm_sec;
               if (SetSystemTime(&st) == FALSE)
               {
                  char * errmsg = NULL;
                  SystemErrorMessage_Set(&errmsg, GetLastError(), "Failed to modify system time", NULL);
                  fprintf(stderr, "%s\n", errmsg);
               }
#else
               struct timeval tv;

               tv.tv_sec = ttxTime;
               tv.tv_usec = 0;
               if (settimeofday(&tv, NULL) != 0)
               {
                  fprintf(stderr, "Failed to modify system time: %s\n", strerror(errno));
               }
#endif
              break;
            }
            case CLOCK_CTRL_PRINT:
            {
              struct tm * ptm;
              char buf[256];

              ptm = localtime(&ttxTime);
              if (strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Z %Y", ptm))
              {
                 printf("%s\n", buf);
              }
              break;
            }
            default:
              fatal1("SystemClock-Cmd: unknown mode %d", clockMode);
              break;
         }
      }
      else
         fprintf(stderr, "No time packets received from teletext - giving up.\n");

      BtDriver_StopAcq();
   }
   else
      fprintf(stderr, "Failed to start acquisition: %s\n", BtDriver_GetLastError());
}

// ---------------------------------------------------------------------------
// Start database export
//
void Daemon_StartDump( void )
{
   FILTER_CONTEXT * fc;
   uint provCni;

   EpgSetup_DbExpireDelay();

   provCni = CmdLine_GetStartProviderCni();
   if (provCni == MERGED_PROV_CNI)
   {
      pUiDbContext = EpgSetup_MergeDatabases();
      if (pUiDbContext == NULL)
         printf("<!-- nxtvepg database merge failed: check merge configuration -->\n");
   }
   else
   {
      pUiDbContext = EpgContextCtl_Open(provCni, FALSE, CTX_FAIL_RET_NULL, CTX_RELOAD_ERR_REQ);
      if (pUiDbContext == NULL)
         printf("<!-- nxtvepg failed to load database %04X -->\n", provCni);
   }

   if (pUiDbContext != NULL)
   {
      // determine language for theme categories from AI block (copied from SetUserLanguage)
      PdcThemeSetLanguage( EpgSetup_GetDefaultLang(pUiDbContext) );

      if (mainOpts.optDumpFilter != NULL)
      {
         fc = EpgQuery_Parse(pUiDbContext, mainOpts.optDumpFilter);
      }
      else
         fc = NULL;

      switch (mainOpts.optDumpMode)
      {
         case EPG_DUMP_XMLTV:
            if (mainOpts.optDumpSubMode != DUMP_XMLTV_ANY)
               EpgDumpXml_Standalone(pUiDbContext, fc, stdout, mainOpts.optDumpSubMode);
            else
               printf("<!-- XML DTD version must be specified -->\n");
            break;
         case EPG_DUMP_TEXT:
            EpgDumpText_Standalone(pUiDbContext, fc, stdout, mainOpts.optDumpSubMode);
            break;
         case EPG_DUMP_RAW:
            EpgDumpRaw_Standalone(pUiDbContext, fc, stdout);
            break;
         default:
            break;
      }

      if (fc != NULL)
      {
         EpgDbFilterDestroyContext(fc);
      }
      EpgContextCtl_Close(pUiDbContext);
   }
}

// ----------------------------------------------------------------------------
// Append a line to the EPG scan messages
//
static void Daemon_ProvScanAddMsg( const char * pMsg, bool bold )
{
   if (pMsg != NULL)
   {
      if (bold)
         printf("\n*** %s ***\n", pMsg);
      else
         printf("%s\n", pMsg);
   }
}

// dummy function, only used in GUI
static void Daemon_ProvScanAddProvDelButton( uint cni )
{
}

// ----------------------------------------------------------------------------
// Start EPG scan
// - during the scan the focus is forced into the .epgscan popup
//
static bool Daemon_ProvScanSetup( void )
{
   EPGSCAN_START_RESULT scanResult;
   uint rescheduleMs;
   bool result;

   result = FALSE;

   // pass the frequency table selection to the TV-channel module
   TvChannels_SelectFreqTable(mainOpts.optDumpSubMode);

   scanResult = EpgScan_Start(RcFile_Query()->tvcard.input,
                              FALSE, FALSE, FALSE,
                              NULL, NULL, 0, &rescheduleMs,
                              &Daemon_ProvScanAddMsg, &Daemon_ProvScanAddProvDelButton);
   switch (scanResult)
   {
      case EPGSCAN_ACCESS_DEV_VBI:
      case EPGSCAN_ACCESS_DEV_VIDEO:
      {
         #ifdef WIN32
         bool isEnabled, hasDriver;

         if ( BtDriver_GetState(&isEnabled, &hasDriver, NULL) &&
              (isEnabled != FALSE) && (hasDriver == FALSE) )
         {
            fprintf(stderr, "Cannot start the EPG scan while the TV application is blocking "
                            "the TV card.  Terminate the TV application and try again.\n");
         }
         else
         #endif
         {
            #ifndef WIN32
            fprintf(stderr, "Failed to open input device\n");
            #else
            fprintf(stderr, "Failed to start the EPG scan due to a TV card driver problem\n");
            #endif
         }
         break;
      }

      case EPGSCAN_NO_TUNER:
         fprintf(stderr, "The provider search cannot be used for external video sources.\n"
                         "Tune an EPG provider's TV channel manually.\n");
         break;

      case EPGSCAN_INTERNAL:
         // parameter inconsistancy - should not be reached
         break;

      case EPGSCAN_OK:
         result = TRUE;
         break;

      default:
         SHOULD_NOT_BE_REACHED;
         break;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Console-mode EPG provider scan main loop
//
static void Daemon_ProvScanMainLoop( void )
{
#ifndef WIN32
   struct timeval tv;
   sint    selSockCnt;
#endif
   uint    rescheduleMs;

   const char aniChars[] = "-\\|/";
   uint  aniIdx = 0;

   while ((should_exit == FALSE) && EpgScan_IsActive() )
   {
      rescheduleMs = EpgScan_EvHandler();
      if (rescheduleMs > 0)
      {
         // check if output is redirected into a file
         if (mainOpts.pStdOutFileName == NULL)
         {
            // display spinning dash below cursor
            fprintf(stdout, "%c\b", aniChars[aniIdx]);
            aniIdx = (aniIdx + 1) % sizeof(aniChars);
            fflush(stdout);
         }

#ifndef WIN32
         tv.tv_sec  = (rescheduleMs / 1000);
         tv.tv_usec = (rescheduleMs % 1000) * 1000L;

         selSockCnt = select(0, NULL, NULL, NULL, &tv);

         if (selSockCnt == -1)
         {
            if (errno != EINTR)
            {  // select syscall failed
               debug1("Daemon-MainLoop: select: %s", strerror(errno));
               break;
            }
         }
#else // WIN32
         // note: not using select() on Windows to avoid dealing with
         // winsock library which requires extra setup and shutdown calls
         Sleep(rescheduleMs);
#endif
      }
      else
      {  // scan is finished
         should_exit = TRUE;
      }
   }
}

// ---------------------------------------------------------------------------
// WIN32 daemon: maintain an invisible Window to catch shutdown messages
// - on win32 the only way to terminate the daemon is by sending a command to this
//   window (the daemon MUST NOT be terminated via the task manager, because
//   this would not allow it to shut down the driver, hence Windows would crash)
// - the existance of the window is also used to detect if the daemon is running
//   by the GUI client
//
#ifdef WIN32

static const char * const daemonWndClassname = "nxtvepg_daemon";
static HWND      hRemCtrlWnd = NULL;
static HANDLE    remCtrlWinThreadHandle = NULL;

// struct used to pass parameters to started thread
typedef struct
{
   HANDLE       evHandle;
   WNDPROC      lpfnWndProc;
   LPCSTR       lpszClassName;
} TMP_THR_PAR;

// ---------------------------------------------------------------------------
// Message handler for the daemon's invisible window
// - only "destructive" events are caught
//
static LRESULT CALLBACK DaemonControlWindowMsgCb( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam )
{
   LRESULT result = 0;

   switch (message)
   {
      case WM_QUERYENDSESSION:
         result = TRUE;
         // fall-through
      case WM_ENDSESSION:
      case WM_DESTROY:
      case WM_CLOSE:
         // terminate the daemon main loop
         if (should_exit == FALSE)
         {
            debug1("DaemonControl-WindowMsgCb: received message %d - shutting down", message);
            should_exit = TRUE;
            // give the daemon main loop time to process the flag
            Sleep(250);
         }
         // quit the window message loop
         PostQuitMessage(0);
         return result;

       case WM_POWERBROADCAST:
         debug1("WM_POWERBROADCAST: event %d", wParam);
         if ((wParam == PBT_APMQUERYSUSPEND) ||
             (wParam == PBT_APMQUERYSTANDBY))
         {
            Daemon_UpdateRcFile(TRUE);
         }
         else if ((wParam == PBT_APMSUSPEND) ||
                  (wParam == PBT_APMSTANDBY))
         {
            EpgAcqCtl_Stop();
         }
         else if ((wParam == PBT_APMRESUMESUSPEND) ||
                  (wParam == PBT_APMRESUMESTANDBY))
         {
            EpgAcqCtl_Start();
         }
         return result;
   }
   return DefWindowProc(hwnd, message, wParam, lParam);
}

// ----------------------------------------------------------------------------
// Remote control window message handler thread
// - first creates an invisible window, the triggers the waiting caller thread,
//   then enters the message loop
// - the thread finishes when the QUIT message is posted by the msg handler cb
//
static DWORD WINAPI RemoteControlWindowThread( LPVOID argEvHandle )
{
   TMP_THR_PAR *pThrPar = (TMP_THR_PAR *) argEvHandle;
   WNDCLASSEX  wc;
   MSG         msg;

   memset(&wc, 0, sizeof(wc));
   wc.cbSize        = sizeof(WNDCLASSEX);
   wc.hInstance     = hMainInstance;
   wc.lpfnWndProc   = pThrPar->lpfnWndProc;
   wc.lpszClassName = pThrPar->lpszClassName;
   RegisterClassEx(&wc);

   // Create an invisible window
   hRemCtrlWnd = CreateWindowEx(0, wc.lpszClassName, wc.lpszClassName, WS_POPUP, CW_USEDEFAULT,
                                0, CW_USEDEFAULT, 0, NULL, NULL, hMainInstance, NULL);

   if (hRemCtrlWnd != NULL)
   {
      dprintf0("RemoteControl-WindowThread: created control window\n");

      // notify the main thread that initialization is complete
      if (pThrPar->evHandle != NULL)
      {
         if (SetEvent(pThrPar->evHandle) == 0)
            debug1("RemoteControl-WindowThread: SetEvent: %ld", GetLastError());
         // note: the event handle is closed by the main thread and must not be used again
      }

      while (GetMessage(&msg, hRemCtrlWnd, 0, 0))
      {
         TranslateMessage(&msg);
         DispatchMessage(&msg);
      }

      // QUIT message received -> destroy the window
      DestroyWindow(hRemCtrlWnd);
      hRemCtrlWnd = NULL;
   }
   else
   {
      debug1("RemoteControl-WindowThread: CreateWindowEx: %ld", GetLastError());
      SetEvent(pThrPar->evHandle);
   }

   return 0;  // dummy
}

// ---------------------------------------------------------------------------
// Create the daemon control window and the message hander task
//
bool RemoteControlWindowCreate( WNDPROC pMsgCb, LPCSTR pClassName )
{
   TMP_THR_PAR thrPar;
   DWORD   threadID;
   HANDLE  evHandle;
   bool    result = FALSE;

   // open a temporary event handle that's passed to the thread to tell us when it's ready
   evHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
   if (evHandle != NULL)
   {
      memset(&thrPar, 0, sizeof(thrPar));
      thrPar.lpfnWndProc   = pMsgCb;
      thrPar.lpszClassName = pClassName;
      thrPar.evHandle      = evHandle;

      remCtrlWinThreadHandle = CreateThread(NULL, 0, RemoteControlWindowThread, &thrPar, 0, &threadID);
      if (remCtrlWinThreadHandle != NULL)
      {
         // wait until the window is created, because if the daemon was started by the
         // GUI, it checks for the existance of the window - so we must be sure it exists
         // before we trigger the GUI that we're ready
         if (WaitForSingleObject(evHandle, 2 * 1000) == WAIT_FAILED)
            debug1("RemoteControl-WindowCreate: WaitForSingleObject: %ld", GetLastError());

         result = TRUE;
      }
      else
         debug1("RemoteControl-WindowCreate: cannot start thread: %ld", GetLastError());

      CloseHandle(evHandle);
   }
   else
      debug1("RemoteControl-WindowCreate: CreateEvent: %ld", GetLastError());

   return result;
}

// ---------------------------------------------------------------------------
// Destroy the daemon control window and stop the message handler task
//
static void RemoteControlWindowDestroy( void )
{
   if ((remCtrlWinThreadHandle != NULL) && (hRemCtrlWnd != NULL))
   {
      PostMessage(hRemCtrlWnd, WM_CLOSE, 0, 0);
      WaitForSingleObject(remCtrlWinThreadHandle, 2000);
      CloseHandle(remCtrlWinThreadHandle);
      remCtrlWinThreadHandle = NULL;
   }
}

// ---------------------------------------------------------------------------
// WIN32: Terminate the daemon by sending a message to the daemon's invisible window
//
bool Daemon_RemoteStop( void )
{
   bool  result = FALSE;
   uint  idx;
   HWND  hWnd;

   hWnd = FindWindow(daemonWndClassname, NULL);
   if (hWnd != NULL)
   {  // send message to the daemon
      PostMessage(hWnd, WM_CLOSE, 0, 0);

      // poll until the daemon is gone (XXX should find something to block on)
      for (idx=0; idx < 6; idx++)
      {
         Sleep(250);
         if (FindWindow(daemonWndClassname, NULL) == NULL)
            break;
      }
      result = TRUE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Windows only: check if the daemon is running
//
bool Daemon_CheckIfRunning( void )
{
   return (FindWindow(daemonWndClassname, NULL) != NULL);
}
#endif  // WIN32

#ifndef WIN32
// ---------------------------------------------------------------------------
// UNIX: Terminate the daemon by sending a signal
// - the pid is obtained from the pid file (usually /tmp/.vbi_pid#)
// - the function doesn't return until the process is gone
//
bool Daemon_RemoteStop( void )
{
   char * pErrMsg;
   bool  result;

   result = EpgAcqClient_TerminateDaemon(&pErrMsg);

   if (pErrMsg != NULL)
   {
      if (mainOpts.optDaemonMode == DAEMON_STOP)
         fprintf(stderr, "%s\n", pErrMsg);
      xfree(pErrMsg);
   }

   return result;
}
#endif  // not WIN32

// ---------------------------------------------------------------------------
// graceful exit upon signals
//
#ifndef WIN32
static void Daemon_SignalHandler( int sigval )
{
   if (IS_DAEMON(mainOpts))
   {
      char str_buf[10];

      sprintf(str_buf, "%d", sigval);
      EpgNetIo_Logger(LOG_NOTICE, -1, 0, "terminated by signal ", str_buf, NULL);
   }
   else
   {
      if (sigval != SIGINT)
         fprintf(stderr, "%s caught deadly signal %d\n", mainOpts.pOptArgv0, sigval);
   }
   // flush debug output
   DBGONLY(fflush(stdout); fflush(stderr));

   should_exit = TRUE;

   signal(sigval, Daemon_SignalHandler);
}

static void Daemon_SignalSetup( void )
{
   signal(SIGINT, Daemon_SignalHandler);
   signal(SIGTERM, Daemon_SignalHandler);
   // ignore signal CHLD because we don't care about our children (e.g. auto-started daemon)
   // this is overridden by the BT driver in non-threaded mode (to catch death of the acq slave)
   signal(SIGCHLD, SIG_IGN);
}

#else
// ---------------------------------------------------------------------------
// Win32: Handle events on console window (non-detached mode only)
//
static BOOL CALLBACK Daemon_ConsoleCtrlHandler( DWORD evtype ) 
{ 
   BOOL  result;

   switch( evtype ) 
   { 
      case CTRL_C_EVENT: 
      case CTRL_CLOSE_EVENT: 
      case CTRL_LOGOFF_EVENT: 
      case CTRL_SHUTDOWN_EVENT: 
         if (IS_DAEMON(mainOpts) && (should_exit == FALSE))
         {
            debug0("Console control signal received - shutting down");
            EpgNetIo_Logger(LOG_NOTICE, -1, 0, "terminated by console control", NULL);
            should_exit = TRUE;
            result = TRUE;
            break;
         }
         // fall-through
      case CTRL_BREAK_EVENT: 
         debug0("Console control signal received - aborting driver");
         EpgAcqCtl_Destroy(TRUE);
         ExitProcess(-1);
         // don't invoke default handlers
         result = TRUE;
         break;

      default: 
         result = FALSE;
         break;
   } 
   return result;
} 
#endif // not WIN32


#ifdef WIN32
static bool daemonWinShmEvent = FALSE;

// ---------------------------------------------------------------------------
// Called by TV event receptor thread
//
static void Wintv_CbTvEvent( void )
{
   if (daemonWinShmEvent == FALSE)
   {
      // XXX cannot easily wake up main loop which blocks within select()
      // but for most purposes it's sufficient to poll every 250 us
      daemonWinShmEvent = TRUE;
   }
}

// ---------------------------------------------------------------------------
// Incoming TV app message: TV tuner was granted or reposessed
// - the enable parameter is currently not used because the check function
//   detects this automatically upon attempting to change the channel
//
static void Wintv_CbTunerGrant( bool enable )
{
   bool drvEnabled, hasDriver;

   BtDriver_GetState(&drvEnabled, &hasDriver, NULL);

   if (drvEnabled && !hasDriver)
   { // have the acq control update it's device state
      EpgAcqCtl_CheckDeviceAccess();
   }
}

static void Wintv_CbEpgQuery( void )
{
}

// ---------------------------------------------------------------------------
// Incoming TV app message: TV channel was changed
//
static void Wintv_CbStationSelected( void )
{
   bool drvEnabled, hasDriver;

   // reset EPG decoder if acq is using the TTX stream provided by the TV app
   BtDriver_GetState(&drvEnabled, &hasDriver, NULL);
   if (drvEnabled && !hasDriver)
   {
      EpgAcqCtl_ResetVpsPdc();
   }
}

// ---------------------------------------------------------------------------
// TV application has attached or detached from nxtvepg
//
static void Wintv_CbAttachTv( bool enable, bool acqEnabled, bool slaveStateChange )
{
   if (acqEnabled)
   {  // update acquisition status, e.g. set to "forced passive" if TV card is owned by TV app now
      dprintf2("Wintv-CbAttachTv: TV app attached: acq=%d, slave-change=%d\n", acqEnabled, slaveStateChange);
      if (slaveStateChange)
      {
         EpgAcqCtl_CheckDeviceAccess();
      }
   }
   else
   {  // switch off acquisition (if enabled) if the driver restart failed in the new mode
      // (e.g. invalid TV card index was configured while in slave mode)
      EpgAcqCtl_Stop();
   }
}

// ----------------------------------------------------------------------------
// Struct with callback functions for shared memory server module
//
static const WINSHMSRV_CB winShmSrvCb =
{
   Wintv_CbTvEvent,
   Wintv_CbStationSelected,
   Wintv_CbEpgQuery,
   Wintv_CbTunerGrant,
   Wintv_CbAttachTv
};
#endif


// ---------------------------------------------------------------------------
// Compatibility definitions for millisecond timer handling
//
#ifdef WIN32
typedef struct
{
   uint  msecs;
} msecTimer;
#define gettimeofday(PT,N)    do {(PT)->msecs = GetCurrentTime();} while(0)
#define CmpMsecTimer(A,B,CMP) ((A)->msecs CMP (B)->msecs)
#define AddMsecTimer(T,VAL)   ((T)->msecs += (VAL) * 1000)
#define AddSecTimer(T,VAL)    ((T)->msecs += (VAL))

#else
typedef struct timeval  msecTimer;
#define CmpMsecTimer(A,B,CMP) timercmp(A,B,CMP)
#define AddMsecTimer(T,VAL)   ((T)->tv_sec += (VAL))
#define AddSecTimer(T,VAL)    do { (T)->tv_usec += (VAL) * 1000L; \
                                    if (tvXawtv.tv_usec > 1000000L) { \
                                       tvXawtv.tv_sec  += 1L; \
                                       tvXawtv.tv_usec -= 1000000L; \
                              }} while (0)
#endif

// ---------------------------------------------------------------------------
// Daemon main loop
//
static void Daemon_MainLoop( void )
{
   msecTimer tvAcq;
   msecTimer tvXawtv;
   msecTimer tvNow;
   struct timeval tv;
   fd_set  rd, wr;
   sint    max_fd;
   sint    selSockCnt;

   gettimeofday(&tvAcq, NULL);
   tvXawtv = tvAcq;

   while ((should_exit == FALSE) && EpgAcqCtl_IsActive())
   {
      gettimeofday(&tvNow, NULL);
      if (CmpMsecTimer(&tvNow, &tvAcq, >))
      {  // read VBI device and add blocks to db
         if (EpgAcqCtl_ProcessPackets())
            EpgAcqCtl_ProcessBlocks();
         tvAcq = tvNow;
         AddMsecTimer(&tvAcq, 1);
      }
      if (CmpMsecTimer(&tvNow, &tvXawtv, >))
      {  // handle VPS/PDC forwarding
         EpgAcqCtl_ProcessVps();
         tvXawtv = tvNow;
         AddSecTimer(&tvXawtv, 200);
      }

      FD_ZERO(&rd);
      FD_ZERO(&wr);
      max_fd = EpgAcqServer_GetFdSet(&rd, &wr);

      // wait for any event, but max. 250 ms
      tv.tv_sec  = 0;
      tv.tv_usec = 250000L;

      selSockCnt = select(((max_fd > 0) ? (max_fd + 1) : 0), &rd, &wr, NULL, &tv);
      if (selSockCnt != -1)
      {  // forward new blocks to network clients, handle incoming messages, check for timeouts
         DBGONLY( if (selSockCnt > 0) )
            dprintf1("Daemon-MainLoop: select: events on %d sockets\n", selSockCnt);
         EpgAcqServer_HandleSockets(&rd, &wr);

         #ifdef WIN32
         if (daemonWinShmEvent)
         {
            daemonWinShmEvent = FALSE;
            WintvSharedMem_HandleTvCmd();
         }
         #endif
      }
      else
      {
         #ifndef WIN32
         if (errno != EINTR)
         {  // select syscall failed
            debug2("Daemon-MainLoop: select with max. fd %d: %s", max_fd, strerror(errno));
            sleep(1);  // sleep 1 second to avoid busy looping
         }
         #else  // WIN32
         if (WSAGetLastError() != WSAEINTR)
         {
            debug2("Daemon-MainLoop: select with max. fd %d: %d", max_fd, WSAGetLastError());
            Sleep(1000);  // 1000 milliseconds == 1 second
         }
         #endif
      }
   }
}

// ---------------------------------------------------------------------------
// Send daemon process to the background
//
void Daemon_ForkIntoBackground( void )
{
#ifndef WIN32
   // fork off a background process for the daemon
   if (mainOpts.optNoDetach == FALSE)
   {
      if (fork() > 0)
         exit(0);
      close(0);
      open("/dev/null", O_RDONLY, 0);
      #if DEBUG_SWITCH == OFF
      if (mainOpts.pStdOutFileName == NULL)
      {
         close(1);
         open("/dev/null", O_WRONLY, 0);
      }
      close(2);
      dup(1);
      #endif
      setsid();
   }
#else  // WIN32
   // Windows behaves opposite to UNIX: daemon process is a GUI app
   // and detached by default; need to attach
   if (mainOpts.optNoDetach)
   {
      HINSTANCE hInstance;
      BOOL (WINAPI *pAttachConsole)(VOID);
      hInstance = LoadLibrary("kernel32.dll");
      // this function is only available starting with Windows XP
      pAttachConsole = (void *) GetProcAddress(hInstance, "AttachConsole");
      if (pAttachConsole != NULL)
      {
         if ( pAttachConsole() )
         {
            if (!SetConsoleCtrlHandler(Daemon_ConsoleCtrlHandler, TRUE))
               debug1("Daemon-ForkIntoBackground: SetConsoleCtrlHandler: %ld", GetLastError());
            if (!SetConsoleTitle("nxtvepg daemon"))
               debug1("Daemon-ForkIntoBackground: SetConsoleTitle: %ld", GetLastError());
         }
         else
            debug1("Daemon-ForkIntoBackground: AttachConsole: %ld", GetLastError());
      }
      FreeLibrary(hInstance);
   }
   else
   {  // just to be save: detach console window so that no CTRL-C will arrive
      FreeConsole();
   }
#endif
}

// ---------------------------------------------------------------------------
// Notify the GUI that the daemon is ready
// - on WIN32 it's also used to notify the GUI that the acq start failed
//
static void Daemon_TriggerGui( void )
{
   #ifdef WIN32
   HANDLE  parentEvHd;
   uchar   id_buf[20];
   #else
   ssize_t wstat;
   #endif

   if (mainOpts.optGuiPipe != -1)
   {
      #ifndef WIN32
      // the cmd line param contains the file handle of the pipe between daemon and GUI
      wstat = write(mainOpts.optGuiPipe, "OK", 3);
      ifdebug1(wstat < 0, "Daemon-TriggerGui: failed to write to pipe: %d", errno);
      close(mainOpts.optGuiPipe);

      #else  // WIN32
      // the cmd line param contains the ID of the parent process
      dprintf1("Daemon-TriggerGui: triggering GUI process ID %d\n", mainOpts.optGuiPipe);
      sprintf(id_buf, "nxtvepg_gui_%d", mainOpts.optGuiPipe);
      parentEvHd = CreateEvent(NULL, FALSE, FALSE, id_buf);
      if (parentEvHd != NULL)
      {
         ifdebug1((GetLastError() != ERROR_ALREADY_EXISTS), "Daemon-TriggerGui: parent id %d has closed event handle", mainOpts.optGuiPipe);

         if (SetEvent(parentEvHd) == 0)
            debug1("Daemon-TriggerGui: SetEvent: %ld", GetLastError());

         CloseHandle(parentEvHd);
      }
      else
         debug2("Daemon-TriggerGui: CreateEvent \"%s\": %ld", id_buf, GetLastError());

      #endif

      mainOpts.optGuiPipe = -1;
   }
}

// ---------------------------------------------------------------------------
// Write config data to disk
// - equivalent function is in epgmain.c, but the difference here is that non-GUI
//   sections are read from the old config file and copied into the new one
// - note the copy is repeated intentionally upon every save to avoid overwriting
//   more recent data
//
void Daemon_UpdateRcFile( bool immediate )
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
            MessageBox(NULL, pErrMsg, "nxtvepg", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
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
// Run an EPG provider scan across all channels in a TV frequency band
// - called from main with option -provscan <country>
// - the country sub-mode defines which bands are scanned and if PAL or SECAM is used
//
void Daemon_ProvScanStart( void )
{
   EpgSetup_CardDriver(mainOpts.videoCardIndex);
   BtDriver_SelectSlicer(VBI_SLICER_ZVBI);
   #ifdef WIN32
   // create remote control message handler to allow to abort the scan
   RemoteControlWindowCreate(DaemonControlWindowMsgCb, daemonWndClassname);
   #endif

   if ( Daemon_ProvScanSetup() )
   {
      Daemon_ProvScanMainLoop();
   }
}

// ---------------------------------------------------------------------------
// Query & print the acquisition status of a running daemon
//
void Daemon_StatusQuery( void )
{
   char * pMsgBuf;
   char * pErrMsg = NULL;

   EpgAcqClient_Init(NULL);
   EpgSetup_NetAcq(FALSE);
   EpgSetup_CardDriver(mainOpts.videoCardIndex);

   pMsgBuf = EpgAcqClient_QueryAcqStatus(&pErrMsg);
   if (pMsgBuf != NULL)
   {
      printf("%s", pMsgBuf);
      xfree(pMsgBuf);
   }
   if (pErrMsg != NULL)
   {
#ifndef WIN32
      fprintf(stderr, "%s\n", pErrMsg);
#else
      printf("%s\n", pErrMsg);
#endif
      xfree(pErrMsg);
   }

   EpgAcqClient_Destroy();
}

// ---------------------------------------------------------------------------
// Search and stop a running daemon process
// - called by main function for "daemon-stop" mode
//
void Daemon_Stop( void )
{
   EpgAcqClient_Init(NULL);
   EpgSetup_NetAcq(FALSE);
   EpgSetup_CardDriver(mainOpts.videoCardIndex);

   Daemon_RemoteStop();

   EpgAcqClient_Destroy();
}

// ---------------------------------------------------------------------------
// Start the daemon
//
void Daemon_Start( void )
{
#ifdef WIN32
   const char * pShmErrMsg;
   bool acqEnabled;

   WintvSharedMem_SetCallbacks(&winShmSrvCb);
   if (WintvSharedMem_StartStop(TRUE, &acqEnabled) == FALSE)
   {
      pShmErrMsg = WinSharedMem_GetErrorMsg();
      if (pShmErrMsg != NULL)
      {
         EpgNetIo_Logger(LOG_ERR, -1, 0, "failed to set up TV app interaction", pShmErrMsg, NULL);

         xfree((void *) pShmErrMsg);
      }
   }
#endif
   EpgAcqServer_Init(mainOpts.optNoDetach);

   // pass configurable parameters to the network server (e.g. enable logging)
   EpgSetup_NetAcq(TRUE);
   EpgSetup_CardDriver(mainOpts.videoCardIndex);
#ifdef USE_TTX_GRABBER
   EpgSetup_TtxGrabber();
#endif

   if (EpgSetup_DaemonAcquisitionMode(CmdLine_GetStartProviderCni(),
                                      mainOpts.optAcqPassive, mainOpts.optAcqOnce))
   {
      #ifndef WIN32
      Daemon_SignalSetup();
      #endif

      // setup cut-off time for expired PI blocks during database reload
      EpgContextCtl_SetPiExpireDelay(RcFile_Query()->db.piexpire_cutoff * 60);

      if (EpgAcqCtl_Start())
      {
         // start listening for client connections (at least on the named socket in /tmp)
         if (EpgAcqServer_Listen())
         {
            #ifdef WIN32
            RemoteControlWindowCreate(DaemonControlWindowMsgCb, daemonWndClassname);
            #endif
            // if the daemon was started by the GUI, notify it that the daemon is ready
            Daemon_TriggerGui();

            Daemon_MainLoop();
         }
      }
      else
      {  // failed to start acq -> error logging
         EpgNetIo_Logger(LOG_ERR, -1, 0, "failed to start acquisition: ", EpgAcqCtl_GetLastError(), NULL);
      }
   }
   // shut down acquisition
   EpgAcqServer_Destroy();
   EpgAcqCtl_Stop();

   #ifdef WIN32
   // notify the GUI in case we haven't done so before
   Daemon_TriggerGui();
   // remove the window now to indicate the driver is down and the TV card free
   RemoteControlWindowDestroy();
   #endif
}

// ---------------------------------------------------------------------------
// Free resources
//
void Daemon_Destroy( void )
{
   RcFile_Destroy();
}

// ---------------------------------------------------------------------------
// Initialize the module
//
void Daemon_Init( void )
{
   char * pErrMsg = NULL;
   /*bool loadOk;*/

   RcFile_Init();
   /*loadOk =*/ RcFile_Load(mainOpts.rcfile, !mainOpts.isUserRcFile, &pErrMsg);

   if (pErrMsg != NULL)
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
         MessageBox(NULL, pErrMsg, "nxtvepg", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
#endif
      }
      xfree(pErrMsg);
   }
}

#endif  // USE_DAEMON
