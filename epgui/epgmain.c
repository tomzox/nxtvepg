/*
 *  Nextview decoder: main module
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
 *    Entry point for the Nextview EPG decoder: contains command
 *    line argument parsing, initialisation of all modules and
 *    GUI main loop.
 *    See README in the top level directory for a general description.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgmain.c,v 1.108 2002/11/10 20:35:33 tom Exp tom $
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
#include <windows.h>
#include <winsock2.h>
#include <io.h>
#include <direct.h>
#endif
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>   /* abs() */
#include <string.h>
#include <ctype.h>    /* isdigit */
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include <tcl.h>
#include <tk.h>

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
#include "epgctl/epgscan.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgui/uictrl.h"
#include "epgctl/epgctxctl.h"
#include "epgui/uictrl.h"
#include "epgui/statswin.h"
#include "epgui/timescale.h"
#include "epgui/menucmd.h"
#include "epgui/pilistbox.h"
#include "epgui/pioutput.h"
#include "epgui/pifilter.h"
#include "epgui/xawtv.h"
#include "epgui/wintv.h"
#include "epgui/wintvcfg.h"
#include "epgui/epgtabdump.h"
#include "epgui/loadtcl.h"

#include "images/nxtv_logo.xbm"
#include "images/ptr_up.xbm"
#include "images/ptr_down.xbm"
#include "images/pan_updown.xbm"
#include "images/qmark.xbm"
#include "images/zoom_in.xbm"
#include "images/zoom_out.xbm"
#include "images/colsel.xbm"

#include "epgvbi/btdrv.h"
#include "epgvbi/winshmsrv.h"
#include "epgui/epgmain.h"


#ifndef USE_PRECOMPILED_TCL_LIBS
# if !defined(TCL_LIBRARY_PATH) || !defined(TK_LIBRARY_PATH)
#  error "Must define both TK_ and TCL_LIBRARY_PATH"
# endif
#else
# define TCL_LIBRARY_PATH  "."
# define TK_LIBRARY_PATH   "."
#endif

char *epg_version_str = EPG_VERSION_STR;
char epg_rcs_id_str[] = EPG_VERSION_RCS_ID;

Tcl_Interp * interp;               // Interpreter for application
char comm[TCL_COMM_BUF_SIZE + 1];  // Command buffer (one extra byte for overflow detection)

// command line options
#ifdef WIN32
static const char * const defaultRcFile = "nxtvepg.ini";
static const char * const defaultDbDir  = ".";
#else
static char * defaultRcFile = "~/.nxtvepgrc";
static char * defaultDbDir  = NULL;
#endif
static const char * rcfile = NULL;
static const char * dbdir  = NULL;
static int  videoCardIndex = -1;
static bool disableAcq = FALSE;
static bool optDaemonMode = FALSE;
static EPGTAB_DUMP_MODE optDumpMode = EPGTAB_DUMP_NONE;
#ifdef USE_DAEMON
static bool optNoDetach   = FALSE;
#endif
static bool optAcqPassive = FALSE;
static bool startIconified = FALSE;
static uint startUiCni = 0;
static const char *pDemoDatabase = NULL;
static const char *pOptArgv0 = NULL;
static int  optGuiPipe = -1;

// handling of global timer events & signals
static bool should_exit;
static Tcl_AsyncHandler exitHandler = NULL;
static Tcl_TimerToken clockHandler = NULL;
static Tcl_TimerToken expirationHandler = NULL;

#ifdef WIN32
static HINSTANCE  hMainInstance;  // copy of win32 instance handle
#endif

// queue for events called from the main loop when Tcl is idle
typedef struct MAIN_IDLE_EVENT_STRUCT
{
   struct MAIN_IDLE_EVENT_STRUCT *pNext;
   Tcl_IdleProc                  *IdleProc;
   ClientData                    clientData;
} MAIN_IDLE_EVENT;

static MAIN_IDLE_EVENT * pMainIdleEventQueue = NULL;

EPGDB_CONTEXT * pUiDbContext;

// ---------------------------------------------------------------------------
// Append a main idle event to the FIFO queue
// - the event handle will be called from the main loop if no other events are available
// - cannot call the events via the normal Tcl/Tk event handler
//   because those eventually get invoked from deep down the call stack, too
//
void AddMainIdleEvent( Tcl_IdleProc *IdleProc, ClientData clientData, bool unique )
{
   MAIN_IDLE_EVENT *pNew, *pWalk, *pLast;
   bool found;

   pWalk = pMainIdleEventQueue;
   pLast = NULL;
   found = FALSE;
   while (pWalk != NULL)
   {
      if (pWalk->IdleProc == IdleProc)
      {
         found = TRUE;
      }
      pLast = pWalk;
      pWalk = pWalk->pNext;
   }

   if ((unique == FALSE) || (found == FALSE))
   {
      pNew = xmalloc(sizeof(MAIN_IDLE_EVENT));
      pNew->IdleProc = IdleProc;
      pNew->clientData = clientData;
      pNew->pNext = NULL;

      if (pLast != NULL)
         pLast->pNext = pNew;
      else
         pMainIdleEventQueue = pNew;
   }
}

// ---------------------------------------------------------------------------
// Remove a main idle event from the queue
// - the event is identified by the function pointer; if parameter matchData
//   is TRUE the given clientData must also be equal to the event's argument.
//
bool RemoveMainIdleEvent( Tcl_IdleProc * IdleProc, ClientData clientData, bool matchData )
{
   MAIN_IDLE_EVENT  * pWalk;
   MAIN_IDLE_EVENT  * pLast;
   bool  found;

   pWalk = pMainIdleEventQueue;
   pLast = NULL;
   found = FALSE;

   while (pWalk != NULL)
   {
      if (pWalk->IdleProc == IdleProc)
      {
         if ((matchData == FALSE) || (pWalk->clientData == clientData))
         {
            if (pLast != NULL)
               pMainIdleEventQueue = pWalk->pNext;
            else
               pLast->pNext = pWalk->pNext;
            xfree(pWalk);
            found = TRUE;
            break;
         }
      }
      pLast = pWalk;
      pWalk = pWalk->pNext;
   }
   return found;
}

// ---------------------------------------------------------------------------
// Schedeule the first main idle event from the FIFO queue
//
static void ProcessMainIdleEvent( void )
{
   MAIN_IDLE_EVENT * pEvent;

   if (pMainIdleEventQueue != NULL)
   {
      pEvent = pMainIdleEventQueue;
      pMainIdleEventQueue = pEvent->pNext;

      pEvent->IdleProc(pEvent->clientData);
      xfree(pEvent);
   }
}

// ---------------------------------------------------------------------------
// Query if the program was started in demo mode
//
bool IsDemoMode( void )
{
   return (pDemoDatabase != NULL);
}

// ---------------------------------------------------------------------------
// Discard all events in the main idle queue
// - only required if memory-leak detection is enabled
//
#if CHK_MALLOC == ON
static void DiscardAllMainIdleEvents( void )
{
   MAIN_IDLE_EVENT *pWalk, *pNext;

   pWalk = pMainIdleEventQueue;
   while (pWalk != NULL)
   {
      pNext = pWalk->pNext;
      if ((pWalk->IdleProc == UiControl_ReloadError) && (pWalk->clientData != NULL))
      {  // special case: this event gets an allocated message
         xfree(pWalk->clientData);
      }
      xfree(pWalk);
      pWalk = pNext;
   }
   pMainIdleEventQueue = NULL;
}
#endif

// ---------------------------------------------------------------------------
// Process acquisition EPG block queue
//
static void MainEventHandler_ProcessBlocks( ClientData clientData )
{
   EpgAcqCtl_ProcessBlocks();
}

// ---------------------------------------------------------------------------
// Regular event, called every minute
//
static void ClockMinEvent( ClientData clientData )
{
   // remove expired PI from the listing
   PiFilter_Expire();

   // check upon acquisition progress
   EpgAcqCtl_Idle();
}

// ---------------------------------------------------------------------------
// called every full minute
// 
static void EventHandler_TimerDbSetDateTime( ClientData clientData )
{
   // not executed until all current events are processed
   AddMainIdleEvent(ClockMinEvent, NULL, TRUE);

   // update expiration statistics in status line and db stats popup
   StatsWin_StatsUpdate(DB_TARGET_UI);

   expirationHandler = Tcl_CreateTimerHandler(1000 * (60 - time(NULL) % 60), EventHandler_TimerDbSetDateTime, NULL);
}

// ---------------------------------------------------------------------------
// called once a second
//
static void EventHandler_UpdateClock( ClientData clientData )
{
   time_t now;

   if (should_exit == FALSE)
   {
      //  Update the clock every second with the current time
      now = time(NULL);
      strftime(comm, TCL_COMM_BUF_SIZE, ".all.shortcuts.clock configure -text {%a %H:%M:%S}\n", localtime(&now));
      eval_check(interp, comm);

      if (EpgScan_IsActive() == FALSE)
      {
         // process VBI packets in the ring buffer
         if (EpgAcqCtl_ProcessPackets())
         {  // next time when idle, insert new blocks into the database
            AddMainIdleEvent(MainEventHandler_ProcessBlocks, NULL, TRUE);
         }
      }

      clockHandler = Tcl_CreateTimerHandler(1000, EventHandler_UpdateClock, NULL);
   }
   else
      clockHandler = NULL;
}

// ---------------------------------------------------------------------------
// Invoked from main loop after SIGHUP
// - toggles acquisition on/off
// - the mode (i.e. local vs. daemon) is the last one used manually
//
#ifndef WIN32
static void EventHandler_SigHup( ClientData clientData )
{
   if (EpgScan_IsActive() == FALSE)
   {
      if (pAcqDbContext == NULL)
      {  // acq currently not running -> start
         AutoStartAcq(interp);
      }
      else
      {  // acq currently running -> stop it or disconnect from server
         EpgAcqCtl_Stop();
      }
   }
}
#endif

// ---------------------------------------------------------------------------
// Handle client connection to EPG acquisition daemon
// - when the client is in network acquisition mode, it installs an event handler
//   which is called whenever there is incoming data (or ready for writing under
//   certain circumstances)
// - On UNIX a Tcl file handler is used; events are then triggered by the Tcl
//   event loop. On Windows Tcl does not dupport file handlers, so they are
//   implemented by use of a separate thread which uses native Winsock2.dll calls
//   to block on socket events.
// - note: in contrary to direct acquisition from the TV card we do not fork a
//   separate process, because there are no real-time constraints in this case.
// - incoming EPG blocks are put in a queue and picked up every second just like
//   in the other acquisition modes.
// - important: the Tcl file handler must be deleted when the socket is closed,
//   else the Tcl/Tk event handling will hang up.
//
#ifdef USE_DAEMON
static int  networkFileHandle = -1;
static void EventHandler_NetworkUpdate( EPGACQ_EVHAND * pAcqEv );

#ifdef WIN32
static Tcl_AsyncHandler asyncSocketHandler = NULL;
static WSAEVENT  socketEventHandle = WSA_INVALID_EVENT;
static HANDLE    socketBlockHandle = NULL;
static int       socketEventMask = 0;
static HANDLE    socketThreadHandle = NULL;
static BOOL      stopSocketThread;

// ----------------------------------------------------------------------------
// Handle events on the network acq client socket
// - executed inside the main thread, but triggered by the winsock event thread
//
static void WinSocket_IdleHandler( ClientData clientData )
{
   WSANETWORKEVENTS  ev;
   EPGACQ_EVHAND acqEv;

   if (socketEventHandle != WSA_INVALID_EVENT)
   {
      // get bitfield of events that occurred since the last call
      if (WSAEnumNetworkEvents(networkFileHandle, socketEventHandle, &ev) == 0)
      {
         acqEv.errCode = ERROR_SUCCESS;
         memset(&acqEv, 0, sizeof(acqEv));

         // translate windows event codes to internal format
         // - the order is relevent for priority of error codes: using only the first error code found
         if (ev.lNetworkEvents & FD_CONNECT)
         {  // connect completed
            acqEv.blockOnWrite = TRUE;
            acqEv.errCode = ev.iErrorCode[FD_CONNECT_BIT];
         }
         if (ev.lNetworkEvents & FD_CLOSE)
         {  // connection was terminated
            acqEv.blockOnRead = TRUE;
            if (acqEv.errCode == ERROR_SUCCESS)
               acqEv.errCode = ev.iErrorCode[FD_CLOSE_BIT];
         }
         if (ev.lNetworkEvents & FD_READ)
         {  // incoming data available
            acqEv.blockOnRead = TRUE;
            if (acqEv.errCode == ERROR_SUCCESS)
               acqEv.errCode = ev.iErrorCode[FD_READ_BIT];
         }
         if (ev.lNetworkEvents & FD_WRITE)
         {  // output buffer space available
            acqEv.blockOnWrite = TRUE;
            if (acqEv.errCode == ERROR_SUCCESS)
               acqEv.errCode = ev.iErrorCode[FD_WRITE_BIT];
         }
         dprintf3("Socket-IdleHandler: fd %d got event 0x%lX errCode=%d\n", networkFileHandle, ev.lNetworkEvents, acqEv.errCode);

         // process the event (i.e. read data or close the connection upon errors etc.)
         EpgAcqClient_HandleSocket(&acqEv);
         // update the event mask if neccessary & unblock the event handler thread
         EventHandler_NetworkUpdate(&acqEv);
      }
      else
         debug1("WSAEnumNetworkEvents: %d", WSAGetLastError());
   }
}

// ----------------------------------------------------------------------------
// 2nd stage of socket event handling: delay handling until GUI is idle
//
static int WinSocket_AsyncThreadHandler( ClientData clientData, Tcl_Interp *interp, int code )
{
   AddMainIdleEvent(WinSocket_IdleHandler, NULL, TRUE);

   return code;
}

// ----------------------------------------------------------------------------
// Windows Socket thread: waits for events
// - using two event handles:
//   +  the first is a standard winsock2 event, i.e. it's not automatically reset
//      when the thread unblocks; it's reset when the main thread has fetched the
//      event bitfield with WSAEnumNetworkEvents()
//   +  the second event handle is used to block the thread until the main thread
//      has processed the events of the last trigger; this prevents a busy loop
//      starting with a socket event until the main thread has fetched the events.
//
static DWORD WINAPI WinSocket_EventThread( LPVOID dummy )
{
   HANDLE  sh[2];

   for (;;)
   {
      sh[0] = socketBlockHandle;
      sh[1] = socketEventHandle;

      // block infinitly until BOTH event handles are signaled
      if (WSAWaitForMultipleEvents(2, sh, TRUE, WSA_INFINITE, FALSE) != WSA_WAIT_FAILED)
      {
         if ((stopSocketThread == FALSE) && (asyncSocketHandler != NULL))
         {
            dprintf0("WinSocket-EventThread: trigger main thread\n");
            Tcl_AsyncMark(asyncSocketHandler);
         }
         else
            break;
      }
      else
      {
         debug1("WinSocket-EventThread: WSAWaitForMultipleEvents: %d", WSAGetLastError());
         break;
      }
   }
   return 0;
}

// ----------------------------------------------------------------------------
// Create socket handler or update event mask
//
static void WinSocket_CreateHandler( int sock_fd, int tclMask )
{
   DWORD threadID;
   int   mask;

   if (socketEventHandle == WSA_INVALID_EVENT)
   {
      socketEventHandle = WSACreateEvent();
      if (socketEventHandle != WSA_INVALID_EVENT)
      {
         socketBlockHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
         if (socketBlockHandle != NULL)
         {
            socketEventMask = 0;
            stopSocketThread = FALSE;
            socketThreadHandle = CreateThread(NULL, 0, WinSocket_EventThread, NULL, 0, &threadID);
            if (socketThreadHandle != NULL)
            {  // success -> create Tcl event trigger
               asyncSocketHandler = Tcl_AsyncCreate(WinSocket_AsyncThreadHandler, NULL);
            }
            else
            {  // failed to create thread -> destroy socket event handle again
               debug1("CreateThread: %ld", GetLastError());
               WSACloseEvent(socketEventHandle);
               socketEventHandle = WSA_INVALID_EVENT;
            }
         }
         else
         {
            debug1("CreateEvent: %ld", GetLastError());
            WSACloseEvent(socketEventHandle);
            socketEventHandle = WSA_INVALID_EVENT;
         }
      }
      else
         debug1("WSACreateEvent: %d", WSAGetLastError());
   }

   // set the event mask
   if (socketEventHandle != WSA_INVALID_EVENT)
   {
      mask = FD_CLOSE;
      if (tclMask & TCL_EXCEPTION)
         mask |= FD_CONNECT;
      if (tclMask & TCL_READABLE)
         mask |= FD_READ;
      if (tclMask & TCL_WRITABLE)
         mask |= FD_WRITE;

      // check if the mask needs to be updated
      if (mask != socketEventMask)
      {
         dprintf3("CreateFileHandler: fd %d, mask=0x%X (was 0x%X)\n", sock_fd, mask, socketEventMask);
         if (WSAEventSelect(sock_fd, socketEventHandle, mask) == 0)
         {  // ok
            socketEventMask = mask;
         }
         else
            debug1("WSAEventSelect: %d", WSAGetLastError());
      }

      // unblock the socket thread
      if (SetEvent(socketBlockHandle) == 0)
         debug1("SetEvent: %ld", GetLastError());
   }
}

// ----------------------------------------------------------------------------
// Destroy the socket handler thread & free resources
//
static void WinSocket_DeleteFileHandler( int sock_fd )
{
   // terminate the thread: manually signal both of it's blocking events
   stopSocketThread = TRUE;
   if ( (socketEventHandle != WSA_INVALID_EVENT) &&
        (WSASetEvent(socketEventHandle)) )
   {
      if ((socketBlockHandle != NULL) && (SetEvent(socketBlockHandle) == 0))
         debug1("SetEvent: %ld", GetLastError());

      WaitForSingleObject(socketThreadHandle, 6000);
      CloseHandle(socketThreadHandle);
      socketThreadHandle = NULL;
   }

   // close the event handle
   if (socketEventHandle != WSA_INVALID_EVENT)
   {
      if (WSACloseEvent(socketEventHandle) == FALSE)
         debug1("WSACloseEvent: %d", WSAGetLastError());
      socketEventHandle = WSA_INVALID_EVENT;
   }

   if (socketBlockHandle != NULL)
   {
      CloseHandle(socketBlockHandle);
      socketBlockHandle = NULL;
   }

   // remove the async. event source (even if already marked)
   if (asyncSocketHandler != NULL)
   {
      Tcl_AsyncDelete(asyncSocketHandler);
      asyncSocketHandler = NULL;
   }
}
#endif  // WIN32

// ---------------------------------------------------------------------------
// UNIX callback for events on the client netacq socket
//
#ifndef WIN32
static void EventHandler_Network( ClientData clientData, int mask )
{
   EPGACQ_EVHAND acqEv;
   
   // invoke the handler
   acqEv.blockOnRead    = (mask & TCL_READABLE) != 0;
   acqEv.blockOnWrite   = (mask & TCL_WRITABLE) != 0;
   acqEv.processQueue   = FALSE;
   EpgAcqClient_HandleSocket(&acqEv);

   // update the event mask if neccessary
   EventHandler_NetworkUpdate(&acqEv);
}
#endif

// ---------------------------------------------------------------------------
// Create, update or delete the handler with params returned from the epgacqclnt module
// - may also be invoked as callback from the epgacqclnt module, e.g. when acq is stopped
//
static void EventHandler_NetworkUpdate( EPGACQ_EVHAND * pAcqEv )
{
   int mask;

   // remove the old handler if neccessary
   if ((pAcqEv->fd != networkFileHandle) && (networkFileHandle != -1))
   {
      #ifndef WIN32
      Tcl_DeleteFileHandler(networkFileHandle);
      #else
      WinSocket_DeleteFileHandler(networkFileHandle);
      #endif
      networkFileHandle = -1;
   }

   // create the event handler or update event mask
   if (pAcqEv->fd != -1)
   {
      // EPG blocks or other messages pending -> schedule handler
      if (pAcqEv->processQueue)
         AddMainIdleEvent(MainEventHandler_ProcessBlocks, NULL, TRUE);

      networkFileHandle = pAcqEv->fd;

      mask = 0;
      if (pAcqEv->blockOnRead)
         mask |= TCL_READABLE;
      if (pAcqEv->blockOnWrite)
         mask |= TCL_WRITABLE;

      #ifndef WIN32
      Tcl_CreateFileHandler(pAcqEv->fd, mask, EventHandler_Network, INT2PVOID(pAcqEv->fd));
      #else
      if (pAcqEv->blockOnConnect)
         mask |= TCL_EXCEPTION;
      WinSocket_CreateHandler(pAcqEv->fd, mask);
      #endif
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
static HWND      hDaemonWnd = NULL;
static HANDLE    daemonWinThreadHandle = NULL;

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
            debug1("DaemonControlWindow-MsgCb: received message %d - shutting down", message);
            should_exit = TRUE;
            // give the daemon main loop time to process the flag
            Sleep(250);
         }
         // quit the window message loop
         PostQuitMessage(0);
         return result;
   }
   return DefWindowProc(hwnd, message, wParam, lParam);
}

// ----------------------------------------------------------------------------
// Daemon window message handler thread
// - first creates an invisible window, the triggers the waiting caller thread,
//   then enters the message loop
// - the thread finishes when the QUIT message is posted by the msg handler cb
//
static DWORD WINAPI DaemonControlWindowThread( LPVOID argEvHandle )
{
   WNDCLASSEX  wc;
   MSG         msg;

   memset(&wc, 0, sizeof(wc));
   wc.cbSize        = sizeof(WNDCLASSEX);
   wc.lpfnWndProc   = DaemonControlWindowMsgCb;
   wc.hInstance     = hMainInstance;
   wc.lpszClassName = daemonWndClassname;
   RegisterClassEx(&wc);

   // Create an invisible window
   hDaemonWnd = CreateWindowEx(0, daemonWndClassname, daemonWndClassname, WS_POPUP, CW_USEDEFAULT,
                               0, CW_USEDEFAULT, 0, NULL, NULL, hMainInstance, NULL);

   if (hDaemonWnd != NULL)
   {
      dprintf0("DaemonControlWindow-Thread: created control window\n");

      // notify the main thread that initialization is complete
      if (argEvHandle != NULL)
         if (SetEvent((HANDLE)argEvHandle) == 0)
            debug1("DaemonControlWindow-Thread: SetEvent: %ld", GetLastError());
      // the event handle is closed by the main thread and must not be used again
      argEvHandle = NULL;

      while (GetMessage(&msg, hDaemonWnd, 0, 0))
      {
         TranslateMessage(&msg);
         DispatchMessage(&msg);
      }

      // QUIT message received -> destroy the window
      DestroyWindow(hDaemonWnd);
      hDaemonWnd = NULL;
   }
   else
   {
      debug1("DaemonControlWindow-Thread: CreateWindowEx: %ld", GetLastError());
      SetEvent((HANDLE)argEvHandle);
   }

   return 0;  // dummy
}

// ---------------------------------------------------------------------------
// Create the daemon control window and the message hander task
//
static bool DaemonControlWindowCreate( void )
{
   DWORD   threadID;
   HANDLE  evHandle;
   bool    result = FALSE;

   // open a temporary event handle that's passed to the thread to tell us when it's ready
   evHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
   if (evHandle != NULL)
   {
      daemonWinThreadHandle = CreateThread(NULL, 0, DaemonControlWindowThread, evHandle, 0, &threadID);
      if (daemonWinThreadHandle != NULL)
      {
         // wait until the window is created, because if the daemon was started by the
         // GUI, it checks for the existance of the window - so we must be sure it exists
         // before we trigger the GUI that we're ready
         if (WaitForSingleObject(evHandle, 2 * 1000) == WAIT_FAILED)
            debug1("DaemonControlWindow-Create: WaitForSingleObject: %ld", GetLastError());

         result = TRUE;
      }
      else
         debug1("DaemonControlWindow-Create: cannot start thread: %ld", GetLastError());

      CloseHandle(evHandle);
   }
   else
      debug1("DaemonControlWindow-Create: CreateEvent: %ld", GetLastError());

   return result;
}

// ---------------------------------------------------------------------------
// Destroy the daemon control window and stop the message handler task
//
static void DaemonControlWindowDestroy( void )
{
   if ((daemonWinThreadHandle != NULL) && (hDaemonWnd != NULL))
   {
      PostMessage(hDaemonWnd, WM_CLOSE, 0, 0);
      WaitForSingleObject(daemonWinThreadHandle, 2000);
      CloseHandle(daemonWinThreadHandle);
      daemonWinThreadHandle = NULL;
   }
}
#endif  // WIN32

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
static void DaemonMainLoop( void )
{
   msecTimer tvIdle;
   msecTimer tvAcq;
   msecTimer tvXawtv;
   msecTimer tvNow;
   struct timeval tv;
   fd_set  rd, wr;
   uint    max;
   sint    selSockCnt;

   gettimeofday(&tvAcq, NULL);
   tvIdle = tvAcq;
   tvXawtv = tvAcq;

   while ((should_exit == FALSE) && (pAcqDbContext != NULL))
   {
      gettimeofday(&tvNow, NULL);
      if (CmpMsecTimer(&tvNow, &tvAcq, >))
      {  // read VBI device and add blocks to db
         if (EpgAcqCtl_ProcessPackets())
            EpgAcqCtl_ProcessBlocks();
         tvAcq = tvNow;
         AddMsecTimer(&tvAcq, 1);
      }
      if (CmpMsecTimer(&tvNow, &tvIdle, >))
      {  // check for acquisition timeouts
         EpgAcqCtl_Idle();
         tvIdle = tvNow;
         AddMsecTimer(&tvIdle, 20);
      }
      if (CmpMsecTimer(&tvNow, &tvXawtv, >))
      {  // handle VPS/PDC forwarding
         EpgAcqCtl_ProcessVps();
         tvXawtv = tvNow;
         AddSecTimer(&tvXawtv, 200);
      }

      FD_ZERO(&rd);
      FD_ZERO(&wr);
      max = EpgAcqServer_GetFdSet(&rd, &wr);

      // wait for any event, but max. 250 ms
      tv.tv_sec  = 0;
      tv.tv_usec = 250000L;

      selSockCnt = select(((max > 0) ? (max + 1) : 0), &rd, &wr, NULL, &tv);
      if (selSockCnt != -1)
      {  // forward new blocks to network clients, handle incoming messages, check for timeouts
         DBGONLY( if (selSockCnt > 0) )
            dprintf1("Daemon-MainLoop: select: events on %d sockets\n", selSockCnt);
         EpgAcqServer_HandleSockets(&rd, &wr);
      }
      else
      {
         if (errno != EINTR)
         {  // select syscall failed
            #ifndef WIN32
            debug2("Daemon-MainLoop: select with max. fd %d: %s", max, strerror(errno));
            sleep(1);
            #else
            debug2("Daemon-MainLoop: select with max. fd %d: %d", max, WSAGetLastError());
            Sleep(1000);
            #endif
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Notify the GUI that the daemon is ready
// - on WIN32 it's also used to notify the GUI that the acq start failed
//
static void DaemonTriggerGui( void )
{
   #ifdef WIN32
   HANDLE  parentEvHd;
   uchar   id_buf[20];
   #endif

   if (optGuiPipe != -1)
   {
      #ifndef WIN32
      // the cmd line param contains the file handle of the pipe between daemon and GUI
      write(optGuiPipe, "OK", 3);
      close(optGuiPipe);

      #else  // WIN32
      // the cmd line param contains the ID of the parent process
      dprintf1("Daemon-TriggerGui: triggering GUI process ID %d\n", optGuiPipe);
      sprintf(id_buf, "nxtvepg_gui_%d", optGuiPipe);
      parentEvHd = CreateEvent(NULL, FALSE, FALSE, id_buf);
      if (parentEvHd != NULL)
      {
         ifdebug1((GetLastError() != ERROR_ALREADY_EXISTS), "Daemon-TriggerGui: parent id %d has closed event handle", optGuiPipe);

         if (SetEvent(parentEvHd) == 0)
            debug1("Daemon-TriggerGui: SetEvent: %ld", GetLastError());

         CloseHandle(parentEvHd);
      }
      else
         debug2("Daemon-TriggerGui: CreateEvent \"%s\": %ld", id_buf, GetLastError());

      #endif

      optGuiPipe = -1;
   }
}

#ifndef WIN32
// ---------------------------------------------------------------------------
// Background-wait for the daemon to be ready to accept client connection
// - implemented differently for UNIX and WIN32
//   + on UNIX there's a pipe between GUI and the daemon during the startup;
//     the filehandle of the unnamed pipe is passed via an undocumented option;
//     the GUI waits until the pipe becomes readable
//   + on Win32 a pipe solution would have been too much effort; instead a
//     named event handle is created on which a temporary thread blocks;
//     when the event is signaled the main thread is triggered
//
static void EventHandler_DaemonStart( ClientData clientData, int mask )
{
   int fd = PVOID2INT(clientData);
   int execErrno;
   ssize_t res;
   char    buf[10];

   if (mask & TCL_READABLE)
   {
      res = read(fd, buf, sizeof(buf));
      if (res > 0)
      {  // daemon did send reply
         Tcl_DeleteFileHandler(fd);
         close(fd);

         if (strcmp(buf, "OK") == 0)
         {  // daemon successfuly started -> connect via socket
            if (EpgAcqCtl_Start() == FALSE)
            {
               UiControlMsg_NetAcqError();
            }
         }
         else
         {
            if (sscanf(buf, "ERR=%d", &execErrno) != 1)
               execErrno = 0;
            if (execErrno == ENOENT)
            {  // most probably exec error: file not found
               sprintf(comm, "tk_messageBox -type ok -icon error "
                             "-message {nxtvepg executable not found. Make sure the program file is in your $PATH.}");
            }
            else
            {  // any other errors: print system error message
               sprintf(comm, "tk_messageBox -type ok -icon error "
                             "-message {Failed to execute the daemon process: %s.}",
                             (execErrno ? strerror(execErrno) : "communication error"));
            }
            eval_check(interp, comm);
         }
      }
      else if (res <= 0)
      {  // daemon died
         Tcl_DeleteFileHandler(fd);
         close(fd);
         // inform the user
         eval_check(interp, "tk_messageBox -type ok -icon error "
                            "-message {The daemon failed to start. Check syslog or the daemon log file for the cause.}");
         Tcl_ResetResult(interp);
      }
      // else: keep waiting
   }
}

// ---------------------------------------------------------------------------
// UNIX: Start the daemon process from the GUI
//
bool EpgMain_StartDaemon( void )
{
   int     pipe_fd[2];
   pid_t   pid;
   char  * daemonArgv[10];
   int     daemonArgc;
   char    fd_buf[20];
   bool    result = FALSE;

   if (pipe(pipe_fd) == 0)
   {
      daemonArgc = 0;
      daemonArgv[daemonArgc++] = "nxtvepg";
      daemonArgv[daemonArgc++] = "-daemon";
      if (strcmp(defaultRcFile, rcfile) != 0)
      {
         daemonArgv[daemonArgc++] = "-rcfile";
         daemonArgv[daemonArgc++] = (char *) rcfile;
      }
      if (strcmp(defaultDbDir, dbdir) != 0)
      {
         daemonArgv[daemonArgc++] = "-dbdir";
         daemonArgv[daemonArgc++] = (char *) dbdir;
      }
      daemonArgv[daemonArgc++] = "-guipipe";
      daemonArgv[daemonArgc++] = fd_buf;
      daemonArgv[daemonArgc] = NULL;

      sprintf(fd_buf, "%d", pipe_fd[1]);
      pid = fork();
      switch (pid)
      {
         case 0:   // child
            close(pipe_fd[0]);

            if (pOptArgv0 != NULL)
               execv(pOptArgv0, daemonArgv);
            execvp("nxtvepg", daemonArgv);

            fprintf(stderr, "Failed to execute the daemon: %s\n", strerror(errno));
            // pass error to GUI
            sprintf(fd_buf, "ERR=%d\n", errno);
            write(pipe_fd[1], fd_buf, strlen(fd_buf) + 1);
            close(pipe_fd[1]);
            exit(1);
            // never reached
            break;

         case -1:  // error during fork
            fprintf(stderr, "Failed to fork for daemon: %s\n", strerror(errno));
            break;

         default:  // parent
            close(pipe_fd[1]);
            // wait for the daemon to start up (it will write to the pipe when it's done)
            Tcl_CreateFileHandler(pipe_fd[0], TCL_READABLE, EventHandler_DaemonStart, INT2PVOID(pipe_fd[0]));
            result = TRUE;
            break;
      }
   }
   else
      fprintf(stderr, "Failed to create pipe to communicate with daemon: %s\n", strerror(errno));

   return result;
}

// ---------------------------------------------------------------------------
// UNIX: Terminate the daemon by sending a signal
// - the pid is obtained from the pid file (usually /tmp/.vbi_pid#)
// - the function doesn't return until the process is gone
//
bool EpgMain_StopDaemon( void )
{
   bool result = FALSE;
   struct timeval tv;
   uint idx;
   int  pid;

   pid = BtDriver_GetDeviceOwnerPid();
   if (pid != -1)
   {
      if (kill(pid, SIGTERM) == 0)
      {
         // wait until the daemon process is gone
         for (idx=0; idx < 5; idx++)
         {
            // terminate loop if the process is gone
            if (kill(pid, 0) != 0)
            {
               result = TRUE;
               break;
            }

            tv.tv_sec  = 0;
            tv.tv_usec = 50 * 1000L;
            select(0, NULL, NULL, NULL, &tv);
         }
         ifdebug1(result==FALSE, "EpgMain-StopDaemon: VBI user pid %d still alive after signal TERM", pid);
      }
      else
         debug1("EpgMain-StopDaemon: could not signal VBI user pid %d", pid);
   }
   else
      debug0("EpgMain-StopDaemon: could not determine pid of VBI user");

   return result;
}

#else  // WIN32

static Tcl_AsyncHandler daemonStartWaitHandler = NULL;
static Tcl_TimerToken   daemonStartWaitTimer   = NULL;
static HANDLE           daemonStartWaitThread  = NULL;
static HANDLE           daemonStartWaitEvent   = NULL;

// ---------------------------------------------------------------------------
// Check for timout in waiting for response from daemon
//
static void DaemonStartWaitTimerHandler( ClientData clientData )
{
   if ( (daemonStartWaitThread != NULL) &&
        (daemonStartWaitEvent != NULL) )
   {
      debug0("DaemonStartWait-TimerHandler: timeout waiting for daemon event");

      // thread is still alive -> wake it up by manually signaling the thread's blocking event
      // note: this also invokes the regular async trigger handler function below
      if (SetEvent(daemonStartWaitEvent) != 0)
      {
         // wait for the thread to terminate
         if (WaitForSingleObject(daemonStartWaitThread, 3000) == WAIT_FAILED)
            debug1("DaemonStartWait-TimerHandler: WaitForSingleObject: %ld", GetLastError());
      }
      else
         debug1("DaemonStartWait-TimerHandler: SetEvent: %ld", GetLastError());
   }
}

// ---------------------------------------------------------------------------
// Start acq after the daemon is started & initialized
// - triggered by the daemon via a named event
//
static void EventHandler_DaemonStartWait( ClientData clientData )
{
   dprintf0("EventHandler-DaemonStartWait: received trigger from daemon\n");

   // #1: free resources

   if (daemonStartWaitTimer != NULL)
   {  // remove the timeout handler, else it could interfere with subsequent start attempts
      Tcl_DeleteTimerHandler(daemonStartWaitTimer);
      daemonStartWaitTimer = NULL;
   }

   if (daemonStartWaitHandler != NULL)
   {
      Tcl_AsyncDelete(daemonStartWaitHandler);
      daemonStartWaitHandler = NULL;
   }

   CloseHandle(daemonStartWaitThread);
   daemonStartWaitThread = NULL;

   CloseHandle(daemonStartWaitEvent);
   daemonStartWaitEvent = NULL;

   // #2: check the result of the daemon start operation

   if ( EpgMain_CheckDaemon() )
   {  // daemon successfuly started -> connect via socket
      if ( EpgAcqCtl_Start() == FALSE )
      {
         UiControlMsg_NetAcqError();
      }
   }
   else
   {  // inform the user about the failure
      eval_check(interp, "tk_messageBox -type ok -icon error "
                         "-message {The daemon failed to start. Check the daemon log file for the cause.}");
      Tcl_ResetResult(interp);
   }
}

// ---------------------------------------------------------------------------
// Triggered by the daemon start event thread
// - here only an event is inserted into the main event handler
//
static int AsyncHandler_DaemonStartWait( ClientData clientData, Tcl_Interp *interp, int code )
{
   AddMainIdleEvent(EventHandler_DaemonStartWait, NULL, TRUE);

   return code;
}

// ---------------------------------------------------------------------------
// Win32 thread to wait for an event trigger from the daemon after it's started acq
//
static DWORD WINAPI DaemonStartWaitThread( LPVOID dummy )
{
   if (WaitForSingleObject(daemonStartWaitEvent, INFINITE) != WAIT_FAILED)
   {
      if (daemonStartWaitHandler != NULL)
      {  // trigger an event, so that the Tcl/Tk event handler immediately wakes up
         Tcl_AsyncMark(daemonStartWaitHandler);
      }
   }
   else
      debug1("Daemon-WaitThread: WaitForSingleObject %ld", GetLastError());

   return 0;
}

// ---------------------------------------------------------------------------
// WIN32: Start the daemon process from the GUI
//
bool EpgMain_StartDaemon( void )
{
   STARTUPINFO  startup;
   PROCESS_INFORMATION proc_info;
   const uchar * pErrMsg;
   DWORD   errCode;
   DWORD   threadID;
   uchar   id_buf[20];
   uchar * pCmdLineStr;
   bool    result = FALSE;

   pCmdLineStr = xmalloc(100 + strlen(rcfile) + strlen(dbdir));
   sprintf(pCmdLineStr, "nxtvepg.exe -daemon -guipipe %ld", GetCurrentProcessId());
   pErrMsg = NULL;
   errCode = 0;

   if (strcmp(defaultRcFile, rcfile) != 0)
   {
      sprintf(pCmdLineStr + strlen(pCmdLineStr), " -rcfile \"%s\"", (char *) rcfile);
   }
   if (strcmp(defaultDbDir, dbdir) != 0)
   {
      sprintf(pCmdLineStr + strlen(pCmdLineStr), " -dbdir \"%s\"", (char *) dbdir);
   }
   dprintf1("EpgMain-StartDaemon: daemon command line: %s\n", pCmdLineStr);

   // create a named event handle that's signalled by the daemon when it's ready
   sprintf(id_buf, "nxtvepg_gui_%ld", GetCurrentProcessId());
   daemonStartWaitEvent = CreateEvent(NULL, FALSE, FALSE, id_buf);
   if (daemonStartWaitEvent != NULL)
   {
      memset(&proc_info, 0, sizeof(proc_info));
      memset(&startup, 0, sizeof(startup));
      startup.cb = sizeof(STARTUPINFO);

      // start the daemon process in the background
      if (CreateProcess(NULL, pCmdLineStr, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, ".", &startup, &proc_info))
      {
         CloseHandle(proc_info.hProcess);
         CloseHandle(proc_info.hThread);

         if (daemonStartWaitHandler == NULL)
            daemonStartWaitHandler = Tcl_AsyncCreate(AsyncHandler_DaemonStartWait, NULL);
         // start a thread that waits for the event handle to be signaled by the daemon
         daemonStartWaitThread = CreateThread(NULL, 0, DaemonStartWaitThread, NULL, 0, &threadID);
         if (daemonStartWaitThread != NULL)
         {
            // create a timer to limit the maximum wait time in the thread
            daemonStartWaitTimer = Tcl_CreateTimerHandler(7 * 1000, DaemonStartWaitTimerHandler, NULL);

            result = TRUE;
         }
         else
         {  // failed to create thread
            errCode = GetLastError();
            pErrMsg = "Failed to set up communication with the daemon - cannot determine it's status: failed to start thread";
            debug1("Daemon-WaitThread: CreateThread: %ld", errCode);
         }
      }
      else
      {  // failed to start the daemon process
         errCode = GetLastError();
         pErrMsg = "Failed to start nxtvepg.exe";
         debug1("Daemon-WaitThread: CreateProcess: %ld", errCode);
      }

      if (result == FALSE)
      {  // close the previously created event handle upon failure
         CloseHandle(daemonStartWaitEvent);
         daemonStartWaitEvent = NULL;
      }
   }
   else
   {  // failed to create named event handle
      errCode = GetLastError();
      pErrMsg = "Failed to set up communication with the daemon - cannot start daemon: failed to create event handle";
      debug2("Daemon-WaitThread: CreateEvent \"%s\": %ld", id_buf, errCode);
   }

   if ((result == FALSE) && (pErrMsg != NULL))
   {
      sprintf(comm, "tk_messageBox -type ok -icon error -message {%s: ", pErrMsg);
      // append system error message to the message box output
      FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errCode, LANG_USER_DEFAULT,
                    comm + strlen(comm), TCL_COMM_BUF_SIZE - strlen(comm) - 2, NULL);
      strcat(comm, "}");
      eval_check(interp, comm);
      Tcl_ResetResult(interp);
   }

   xfree(pCmdLineStr);

   return result;
}

// ---------------------------------------------------------------------------
// WIN32: Terminate the daemon by sending a message to the daemon's invisible window
//
bool EpgMain_StopDaemon( void )
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
bool EpgMain_CheckDaemon( void )
{
   return (FindWindow(daemonWndClassname, NULL) != NULL);
}

#endif  // WIN32
#endif  // USE_DAEMON

// ---------------------------------------------------------------------------
// called by interrupt handlers when the application should exit
//
static int AsyncHandler_AppTerminate( ClientData clientData, Tcl_Interp *interp, int code )
{
   // do nothing - the only purpose was to wake up the event handler,
   // so that we can leave the main loop after this NOP was processed
   return code;
}

// ---------------------------------------------------------------------------
// graceful exit upon signals
//
#ifndef WIN32
static void signal_handler( int sigval )
{
   if ((sigval == SIGHUP) && (optDaemonMode == FALSE) && (optDumpMode == EPGTAB_DUMP_NONE))
   {  // toggle acquisition on/off (unless in daemon mode)
      AddMainIdleEvent(EventHandler_SigHup, NULL, TRUE);
   }
   else
   {
      #ifdef USE_DAEMON
      if (optDaemonMode)
      {
         char str_buf[10];

         sprintf(str_buf, "%d", sigval);
         EpgNetIo_Logger(LOG_NOTICE, -1, 0, "terminated by signal ", str_buf, NULL);
      }
      else
      #endif
      {
         if (sigval != SIGINT)
            fprintf(stderr, "nxtvepg caught deadly signal %d\n", sigval);

         if (exitHandler != NULL)
         {  // trigger an event, so that the Tcl/Tk event handler immediately wakes up
            Tcl_AsyncMark(exitHandler);
         }
      }
      // flush debug output
      DBGONLY(fflush(stdout); fflush(stderr));

      // this flag breaks the main loop
      should_exit = TRUE;
   }

   signal(sigval, signal_handler);
}
#else

// ---------------------------------------------------------------------------
// Callback to deal with Windows shutdown
// - called when WM_QUERYENDSESSION or WM_ENDSESSION message is received
//   this currenlty requires a patch in the tk library!
// - the driver must be stopped before the applications exits
//   or the system will crash ("blue-screen")
//
static void WinApiDestructionHandler( ClientData clientData)
{
   debug0("received destroy event");

   // properly shut down the acquisition
   EpgScan_Stop();
   EpgAcqCtl_Stop();
   WintvSharedMem_Exit();
   BtDriver_Exit();

   // exit the application
   ExitProcess(0);
}

#ifdef __MINGW32__
static LONG WINAPI WinApiExceptionHandler(struct _EXCEPTION_POINTERS *exc_info)
{
   //debug1("FATAL exception caught: %d", GetExceptionCode());
   debug0("FATAL exception caught");
   // skip EpgAcqCtl_Stop() because it tries to dump the db - do as little as possible here
   BtDriver_Exit();
   ExitProcess(-1);
   // dummy return
   return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// Declare the new callback registration function
// this function is patched into the tk83.dll and hence not listed in the standard header files
extern TCL_STORAGE_CLASS void Tk_RegisterMainDestructionHandler( Tcl_CloseProc * handler );

#endif


#ifdef WIN32
// ---------------------------------------------------------------------------
// Set the application icon for window title bar and taskbar
// - Note: the window must be mapped before the icon can be set!
//
#ifndef ICON_PATCHED_INTO_DLL
static void SetWindowsIcon( HINSTANCE hInstance )
{
   Tcl_Obj * pId;
   HICON hIcon;
   int   hWnd;

   hIcon = LoadIcon(hInstance, "NXTVEPG_ICON");
   if (hIcon != NULL)
   {
      sprintf(comm, "wm frame .\n");
      if (Tcl_EvalEx(interp, comm, -1, 0) == TCL_OK)
      {
         pId = Tcl_GetObjResult(interp);
         if (pId != NULL)
         {
            if (Tcl_GetIntFromObj(interp, pId, &hWnd) == TCL_OK)
            {
               SendMessage((HWND)hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            }
            else
               debug1("SetWindowsIcon: frame ID has invalid format '%s'", Tcl_GetStringResult(interp));
         }
         else
            debug0("SetWindowsIcon: Tcl error: frame ID result missing");
      }
      else
         debugTclErr(interp, "SetWindowsIcon");
   }
   else
      debug0("SetWindowsIcon: NXTVEPG-ICON resource not found");
}
#endif  // ICON_PATCHED_INTO_DLL

// ----------------------------------------------------------------------------
// Systray state
//
#ifdef WIN32

#define UWM_SYSTRAY (WM_USER + 1) // Sent to us by the systray
static Tcl_AsyncHandler asyncSystrayHandler = NULL;
static HWND       hSystrayWnd = NULL;
static HANDLE     systrayThreadHandle;
static BOOL       stopSystrayThread;
static BOOL       systrayDoubleClick;
static POINT      pt;

// forward function declaration
static bool WinSystrayIcon( bool enable );

// ----------------------------------------------------------------------------
// Handle systray events
// - executed inside the main thread, but triggered by the msg receptor thread
//
static void Systray_IdleHandler( ClientData clientData )
{
   if (systrayDoubleClick == FALSE)
   {  // right mouse button click -> display popup menu
      sprintf(comm, "tk_popup .systray %ld %ld 0", pt.x, pt.y);
      eval_check(interp, comm);
   }
   else
   {  // double click -> open the main window
      WinSystrayIcon(FALSE);
      eval_check(interp, "wm deiconify .");
   }
}

// ----------------------------------------------------------------------------
// 2nd stage of systray event handling: delay handling until GUI is idle
//
static int Systray_AsyncThreadHandler( ClientData clientData, Tcl_Interp *interp, int code )
{
   AddMainIdleEvent(Systray_IdleHandler, NULL, TRUE);

   return code;
}

// ---------------------------------------------------------------------------
// Message handler for systray icon
// - executed inside the separate systray thread
//
static LRESULT CALLBACK WinSystrayWndMsgCb( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam )
{
   NOTIFYICONDATA nid;

   switch (message)
   {
      case WM_DESTROY:
         // notification by the main thread to remove the systray icon
         nid.cbSize  = sizeof(NOTIFYICONDATA);
         nid.hWnd    = hwnd;
         nid.uID     = 1;
         nid.uFlags  = NIF_TIP;
         Shell_NotifyIcon(NIM_DELETE, &nid);
         // quit the window message loop
         PostQuitMessage(0);
         return TRUE;

      case WM_QUIT:
         stopSystrayThread = TRUE;
         return TRUE;

      case UWM_SYSTRAY:
         // notification of mouse activity over the systray icon
         switch (lParam)
         {
            case WM_RBUTTONUP:
               GetCursorPos(&pt);
               systrayDoubleClick = FALSE;
               if (asyncSystrayHandler != NULL)
               {
                  Tcl_AsyncMark(asyncSystrayHandler);
               }
               break;

            case WM_LBUTTONDBLCLK:
               systrayDoubleClick = TRUE;
               if (asyncSystrayHandler != NULL)
               {
                  Tcl_AsyncMark(asyncSystrayHandler);
               }
               break;
         }
         return TRUE; // I don't think that it matters what you return.
   }
   return DefWindowProc(hwnd, message, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Systray thread: create systray icon and enter message loop
// - The systray code is based on tray42.zip by Michael Smith
//   <aa529@chebucto.ns.ca>, 5 Aug 1997
//   Copyright 1997 Michael T. Smith / R.A.M. Technology.
//
static DWORD WINAPI WinSystrayThread( LPVOID argEvHandle )
{
   WNDCLASSEX wc;
   MSG msg;
   NOTIFYICONDATA nid;
   const char * const classname = "nxtvepg_systray_class";

   // Create a window class for the window that receives systray notifications
   wc.cbSize        = sizeof(WNDCLASSEX);
   wc.style         = 0;
   wc.lpfnWndProc   = WinSystrayWndMsgCb;
   wc.cbClsExtra    = wc.cbWndExtra = 0;
   wc.hInstance     = hMainInstance;
   wc.hIcon         = LoadIcon(hMainInstance, "NXTVEPG_ICON");
   if (wc.hIcon == NULL)
      debug1("WinSystrayThread: LoadIcon: %ld", GetLastError());
   wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
   wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
   wc.lpszMenuName  = NULL;
   wc.lpszClassName = classname;
   wc.hIconSm       = LoadImage(hMainInstance, "NXTVEPG_ICON", IMAGE_ICON,
                                GetSystemMetrics(SM_CXSMICON),
                                GetSystemMetrics(SM_CYSMICON), 0);
   RegisterClassEx(&wc);

   // Create window. Note that WS_VISIBLE is not used, and window is never shown
   hSystrayWnd = CreateWindowEx(0, classname, classname, WS_POPUP, CW_USEDEFAULT, 0,
                                CW_USEDEFAULT, 0, NULL, NULL, hMainInstance, NULL);

   if (hSystrayWnd != NULL)
   {
      // Fill out NOTIFYICONDATA structure
      nid.cbSize  = sizeof(NOTIFYICONDATA); // size
      nid.hWnd    = hSystrayWnd; // window to receive notifications
      nid.uID     = 1;     // application-defined ID for icon (can be any UINT value)
      nid.uFlags  = NIF_MESSAGE |  // nid.uCallbackMessage is valid, use it
                    NIF_ICON |     // nid.hIcon is valid, use it
                    NIF_TIP;       // nid.szTip is valid, use it
      nid.uCallbackMessage = UWM_SYSTRAY; // message sent to nid.hWnd
      nid.hIcon   = LoadImage(hMainInstance, "NXTVEPG_ICON", IMAGE_ICON,
                              GetSystemMetrics(SM_CXSMICON),
                              GetSystemMetrics(SM_CYSMICON), 0); // 16x16 icon
      // szTip is the ToolTip text (64 byte array including NULL)
      strcpy(nid.szTip, "nexTView");

      // NIM_ADD: Add icon; NIM_DELETE: Remove icon; NIM_MODIFY: modify icon
      Shell_NotifyIcon(NIM_ADD, &nid);

      // inform the main thread that we're done with initialization
      if (SetEvent((HANDLE) argEvHandle) == 0)
         debug1("WinSystrayThread: Setevent: %ld", GetLastError());

      while ((stopSystrayThread == FALSE) && GetMessage(&msg, hSystrayWnd, 0, 0))
      {
         TranslateMessage(&msg);
         DispatchMessage(&msg);
      }

      DestroyWindow(hSystrayWnd);
      hSystrayWnd = NULL;
   }
   else
   {
      debug1("WinSystrayThread: CreateWindowEx: %ld", GetLastError());
      if (SetEvent((HANDLE) argEvHandle) == 0)
         debug1("WinSystrayThread: Setevent: %ld", GetLastError());
   }

   return 0;  // dummy
}

// ----------------------------------------------------------------------------
// Show or hide the systray icon
// - showing the systray icon required creating a thread that receives asynchronous
//   events on the icon, i.e. mostly mouse button events
//
static bool WinSystrayIcon( bool enable )
{
   DWORD   threadID;
   HANDLE  evHandle;

   if (enable)
   {
      if (systrayThreadHandle == NULL)
      {
         // create an asynchronous event source that allows to wait until the thread is ready
         asyncSystrayHandler = Tcl_AsyncCreate(Systray_AsyncThreadHandler, NULL);

         evHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
         if (evHandle != NULL)
         {
            stopSystrayThread = FALSE;
            systrayThreadHandle = CreateThread(NULL, 0, WinSystrayThread, evHandle, 0, &threadID);
            if (systrayThreadHandle != NULL)
            {
               // wait until the systray thread is initialized
               WaitForSingleObject(evHandle, 10 * 1000);
            }
            else
               debug1("WinSystrayIcon: CreateThread: %ld", GetLastError());

            CloseHandle(evHandle);
         }
         else
            debug1("WinSystrayIcon: failed to create event: %ld", GetLastError());
      }
   }
   else
   {
      // remove the systray icon
      if (systrayThreadHandle != NULL)
      {
         PostMessage(hSystrayWnd, WM_DESTROY, 0, 0);
         WaitForSingleObject(systrayThreadHandle, 2000);
         CloseHandle(systrayThreadHandle);
         systrayThreadHandle = NULL;
      }
      // remove the async. event source (even if already marked)
      if (asyncSystrayHandler != NULL)
      {
         Tcl_AsyncDelete(asyncSystrayHandler);
         asyncSystrayHandler = NULL;
      }
   }
   return (hSystrayWnd != NULL);
}

// ----------------------------------------------------------------------------
// Tcl callback procedure: Create or destroy icon in system tray
//
static int TclCbWinSystrayIcon( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   const char * const pUsage = "Usage: C_SystrayIcon <boolean>";
   bool withdraw;
   int  enable;
   int  result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetBoolean(interp, argv[1], &enable))
   {  // wrong parameter format
      result = TCL_ERROR;
   }
   else
   {
      withdraw = WinSystrayIcon(enable) && enable;

      Tcl_SetResult(interp, (withdraw ? "1" : "0"), TCL_STATIC);
      result = TCL_OK;
   }
   return result;
}
#endif

/*
 *-------------------------------------------------------------------------
 *
 * setargv --     [This is taken from the wish main in the Tcl/Tk package]
 *
 *	Parse the Windows command line string into argc/argv.  Done here
 *	because we don't trust the builtin argument parser in crt0.  
 *	Windows applications are responsible for breaking their command
 *	line into arguments.
 *
 *	2N backslashes + quote -> N backslashes + begin quoted string
 *	2N + 1 backslashes + quote -> literal
 *	N backslashes + non-quote -> literal
 *	quote + quote in a quoted string -> single quote
 *	quote + quote not in quoted string -> empty string
 *	quote -> begin quoted string
 *
 * Results:
 *	Fills argcPtr with the number of arguments and argvPtr with the
 *	array of arguments.
 *
 * Parameters:
 *   int *argcPtr;        Filled with number of argument strings
 *   char ***argvPtr;     Filled with argument strings (malloc'd)
 *
 *--------------------------------------------------------------------------
 */
static void SetArgv( int * argcPtr, char *** argvPtr )
{
    char *cmdLine, *p, *arg, *argSpace;
    char **argv;
    int argc, size, inquote, copy, slashes;
    
    cmdLine = GetCommandLine();

    // Precompute an overly pessimistic guess at the number of arguments
    // in the command line by counting non-space spans.
    size = 2;
    for (p = cmdLine; *p != '\0'; p++) {
        if (isspace(*p)) {
            size++;
            while (isspace(*p)) {
                p++;
            }
            if (*p == '\0') {
                break;
            }
        }
    }
    argSpace = (char *) xmalloc((unsigned) (size * sizeof(char *) + strlen(cmdLine) + 1));
    argv = (char **) argSpace;
    argSpace += size * sizeof(char *);
    size--;

    p = cmdLine;
    for (argc = 0; argc < size; argc++) {
        argv[argc] = arg = argSpace;
        while (isspace(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        inquote = 0;
        slashes = 0;
        while (1) {
            copy = 1;
            while (*p == '\\') {
                slashes++;
                p++;
            }
            if (*p == '"') {
                if ((slashes & 1) == 0) {
                    copy = 0;
                    if ((inquote) && (p[1] == '"')) {
                        p++;
                        copy = 1;
                    } else {
                        inquote = !inquote;
                    }
                }
                slashes >>= 1;
            }

            while (slashes) {
                *arg = '\\';
                arg++;
                slashes--;
            }

            if ((*p == '\0') || (!inquote && isspace(*p))) {
                break;
            }
            if (copy != 0) {
                *arg = *p;
                arg++;
            }
            p++;
        }
        *arg = '\0';
        argSpace = arg + 1;
    }
    argv[argc] = NULL;

    *argcPtr = argc;
    *argvPtr = argv;
}

// ---------------------------------------------------------------------------
// Determine the working directory from executable file path and chdir there
// - used when a db is given on the command line
// - required because when the program is started by dropping a db onto the
//   executable the working dir is set to the desktop, which is certainly
//   not the right place to create the ini file
//
static void SetWorkingDirectoryFromExe( const char *argv0 )
{
   char *pDirPath;
   int len;

   // search backwards from the end for the begin of the file name
   len = strlen(argv0);
   while (--len >= 0)
   {
      if (argv0[len] == PATH_SEPARATOR)
      {
         pDirPath = strdup(argv0);
         pDirPath[len] = 0;
         // change to the directory
         if (chdir(pDirPath) != 0)
         {
            debug2("Cannot change working dir to %s: %s", pDirPath, strerror(errno));
         }
         free(pDirPath);
         break;
      }
   }
}
#endif

// ---------------------------------------------------------------------------
// Print Usage and exit
//
static void Usage( const char *argv0, const char *argvn, const char * reason )
{
#ifndef WIN32
   fprintf(stderr, "%s: %s: %s\n"
#else
   sprintf(comm, "%s: %s: %s\n"
#endif
                   "Usage: %s [options] [database]\n"
                   "       -help       \t\t: this message\n"
                   #ifndef WIN32
                   "       -display <display>  \t: X11 server, if different from $DISPLAY\n"
                   #endif
                   "       -geometry <geometry>\t: initial window position\n"
                   #ifndef WIN32
                   "       -iconic     \t\t: start with window iconified\n"
                   #else
                   "       -iconic     \t\t: start with window minimized\n"
                   #endif
                   "       -rcfile <path>      \t: path and file name of setup file\n"
                   "       -dbdir <path>       \t: directory where to store databases\n"
                   #ifndef WIN32
                   #ifdef EPG_DB_ENV
                   "                           \t: default: $" EPG_DB_ENV "/" EPG_DB_DIR "\n"
                   #else
                   "                           \t: default: " EPG_DB_DIR "\n"
                   #endif
                   #endif
                   "       -card <digit>       \t: index of TV card for acq (starting at 0)\n"
                   "       -provider <cni>     \t: network id of EPG provider (hex)\n"
                   "       -noacq              \t: don't start acquisition automatically\n"
                   #ifdef USE_DAEMON
                   "       -daemon             \t: don't open any windows; acquisition only\n"
                   "       -nodetach           \t: daemon remains connected to tty\n"
                   "       -acqpassive         \t: force daemon to passive acquisition mode\n"
                   #endif
                   "       -dump pi|ai|pdc|xml     \t: dump database\n"
                   "       -demo <db-file>     \t: load database in demo mode\n",
                   argv0, reason, argvn, argv0);
#if 0
   /*balance brackets for syntax highlighting*/  )
#endif
#ifdef WIN32
   MessageBox(NULL, comm, "Nextview-EPG-Decoder Usage", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
#endif

   exit(1);
}

// ---------------------------------------------------------------------------
// Parse command line options
//
static void ParseArgv( int argc, char * argv[] )
{
   struct stat st;
   int argIdx = 1;

#ifndef WIN32
#ifdef EPG_DB_ENV
   char * pEnvPath = getenv(EPG_DB_ENV);
   if (pEnvPath != NULL)
   {
      defaultDbDir = xmalloc(strlen(pEnvPath) + strlen(EPG_DB_DIR) + 1+1);
      strcpy(defaultDbDir, pEnvPath);
      strcat(defaultDbDir, "/" EPG_DB_DIR);
   }
   else
#endif
   {
      defaultDbDir = xmalloc(strlen(EPG_DB_DIR) + 1);
      strcpy(defaultDbDir, EPG_DB_DIR);
   }
#endif

   rcfile = defaultRcFile;
   dbdir  = defaultDbDir;

   while (argIdx < argc)
   {
      if (argv[argIdx][0] == '-')
      {
         if (!strcmp(argv[argIdx], "-help"))
         {
            char versbuf[50];
            sprintf(versbuf, "(version %s)", epg_version_str);
            Usage(argv[0], versbuf, "the following command line options are available");
         }
         else if (!strcmp(argv[argIdx], "-noacq"))
         {  // do not enable acquisition
            disableAcq = TRUE;
            argIdx += 1;
         }
         #ifdef USE_DAEMON
         else if (!strcmp(argv[argIdx], "-daemon"))
         {  // suppress GUI
            optDaemonMode = TRUE;
            argIdx += 1;
         }
         else if (!strcmp(argv[argIdx], "-nodetach"))
         {  // daemon stays in the foreground
            #ifndef WIN32
            optNoDetach = TRUE;
            argIdx += 1;
            #else
            Usage(argv[0], argv[argIdx], "unsupported option");
            #endif
         }
         else if (!strcmp(argv[argIdx], "-acqpassive"))
         {  // set passive acquisition mode (not saved to rc/ini file)
            optAcqPassive = TRUE;
            argIdx += 1;
         }
         #else
         else if ( !strcmp(argv[argIdx], "-daemon") ||
                   !strcmp(argv[argIdx], "-nodetach") ||
                   !strcmp(argv[argIdx], "-acqpassive") )
         {
            Usage(argv[0], argv[argIdx], "unsupported option");
         }
         #endif
         else if (!strcmp(argv[argIdx], "-dump"))
         {  // dump database and exit
            if (argIdx + 1 < argc)
            {
               optDumpMode = EpgTabDump_GetMode(argv[argIdx + 1]);
               if (optDumpMode == EPGTAB_DUMP_NONE)
                  Usage(argv[argIdx + 1], argv[argIdx], "illegal mode keyword for");
               argIdx += 2;
            }
            else
               Usage(argv[0], argv[argIdx], "missing mode keyword after");
         }
         else if (!strcmp(argv[argIdx], "-rcfile"))
         {
            if (argIdx + 1 < argc)
            {  // read file name of rc/ini file
               rcfile = argv[argIdx + 1];
               argIdx += 2;
            }
            else
               Usage(argv[0], argv[argIdx], "missing file name after");
         }
         else if (!strcmp(argv[argIdx], "-dbdir"))
         {
            if (argIdx + 1 < argc)
            {  // read path of database directory
               dbdir = argv[argIdx + 1];
               argIdx += 2;
            }
            else
               Usage(argv[0], argv[argIdx], "missing path name after");
         }
         else if (!strcmp(argv[argIdx], "-card"))
         {
            if (argIdx + 1 < argc)
            {  // read index of TV card device
               char *pe;
               ulong cardIdx = strtol(argv[argIdx + 1], &pe, 0);
               if ((pe != (argv[argIdx + 1] + strlen(argv[argIdx + 1]))) || (cardIdx > 9))
                  Usage(argv[0], argv[argIdx+1], "invalid index (range 0-9)");
               videoCardIndex = (int) cardIdx;
               argIdx += 2;
            }
            else
               Usage(argv[0], argv[argIdx], "missing card index after");
         }
         else if ( !strcmp(argv[argIdx], "-provider") ||
                   !strcmp(argv[argIdx], "-prov") )
         {
            if (argIdx + 1 < argc)
            {  // read hexadecimal CNI of selected provider
               char *pe;
               startUiCni = strtol(argv[argIdx + 1], &pe, 16);
               if (pe != (argv[argIdx + 1] + strlen(argv[argIdx + 1])))
                  Usage(argv[0], argv[argIdx+1], "invalid CNI (must be hexadecimal, e.g. 0x0d94 or d94)");
               argIdx += 2;
            }
            else
               Usage(argv[0], argv[argIdx], "missing provider cni after");
         }
         else if (!strcmp(argv[argIdx], "-demo"))
         {
            if (argIdx + 1 < argc)
            {  // save file name of demo database
               pDemoDatabase = argv[argIdx + 1];
               if (stat(pDemoDatabase, &st) != 0)
               {
                  perror("demo database");
                  exit(-1);
               }
               argIdx += 2;
            }
            else
               Usage(argv[0], argv[argIdx], "missing database file name after");
         }
         else if ( !strcmp(argv[argIdx], "-iconic") )
         {  // start with iconified main window
            startIconified = TRUE;
            argIdx += 1;
         }
         else if ( !strcmp(argv[argIdx], "-geometry")
                   #ifndef WIN32
                   || !strcmp(argv[argIdx], "-display")
                   || !strcmp(argv[argIdx], "-name")
                   #endif
                 )
         {  // ignore arguments that are handled by Tk
            if (argIdx + 1 >= argc)
               Usage(argv[0], argv[argIdx], "missing argument after");
            argIdx += 2;
         }
         else if ( !strcmp(argv[argIdx], "-guipipe") )
         {  // undocumented option (internal use only): pass fd of pipe to GUI for daemon start
            if (argIdx + 1 < argc)
            {  // read decimal fd (silently ignore errors)
               char *pe;
               optGuiPipe = strtol(argv[argIdx + 1], &pe, 0);
               if (pe != (argv[argIdx + 1] + strlen(argv[argIdx + 1])))
                  optGuiPipe = -1;
               argIdx += 2;
            }
         }
         else
            Usage(argv[0], argv[argIdx], "unknown option");
      }
      else if (argIdx + 1 == argc)
      {  // database file argument -> determine dbdir and provider from path
         EpgDbDumpGetDirAndCniFromArg(argv[argIdx], &dbdir, &startUiCni);
         if (startUiCni == 0)
            pDemoDatabase = argv[argIdx];
         //printf("dbdir=%s CNI=%04X\n", dbdir, startUiCni);
         #ifdef WIN32
         SetWorkingDirectoryFromExe(argv[0]);
         #endif
         argIdx += 1;
      }
      else
         Usage(argv[0], argv[argIdx], "Too many arguments");
   }

   // Check for disallowed option combinations
   if (optDaemonMode)
   {
      if (disableAcq)
         Usage(argv[0], "-daemon", "Cannot combine with -noacq");
      else if (pDemoDatabase != NULL)
         Usage(argv[0], "-daemon", "Cannot combine with -demo mode");
      else if ((startUiCni != 0) && optAcqPassive)
         Usage(argv[0], "-provider", "Cannot combine with -acqpassive");
      else if (optDumpMode != EPGTAB_DUMP_NONE)
         Usage(argv[0], "-daemon", "Cannot combine with -dump");
   }
   else
   {
      if (optAcqPassive)
         Usage(argv[0], "-acqpassive", "Only meant for -daemon mode");
   }

   if (optDumpMode != EPGTAB_DUMP_NONE)
   {
      if (startUiCni == 0)
         Usage(argv[0], "-dump", "Must also specify -provider");
      else if (pDemoDatabase != NULL)
         Usage(argv[0], "-dump", "Cannot combine with -demo mode");
   }
}

// ---------------------------------------------------------------------------
// Initialize the Tcl/Tk interpreter
//
static int ui_init( int argc, char **argv, bool withTk )
{
   CONST84 char * pLanguage;
   char *args;

   // set up the user-configured locale
   setlocale(LC_ALL, "C");  // required for Tcl or parsing of floating point numbers fails
   pLanguage = setlocale(LC_TIME, "");

   if (argc >= 1)
   {
      Tcl_FindExecutable(argv[0]);
   }

   #if DEBUG_SWITCH == ON
   // set last byte of command buffer to zero to detect overflow
   comm[sizeof(comm) - 1] = 0;
   #endif

   interp = Tcl_CreateInterp();

   if (argc > 1)
   {
      args = Tcl_Merge(argc - 1, (CONST84 char **) argv + 1);
      Tcl_SetVar(interp, "argv", args, TCL_GLOBAL_ONLY);
      sprintf(comm, "%d", argc - 1);
      Tcl_SetVar(interp, "argc", comm, TCL_GLOBAL_ONLY);
   }
   Tcl_SetVar(interp, "argv0", argv[0], TCL_GLOBAL_ONLY);

   Tcl_SetVar(interp, "tcl_library", TCL_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "tk_library", TK_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "tcl_interactive", "0", TCL_GLOBAL_ONLY);

   #ifndef WIN32
   Tcl_SetVar(interp, "is_unix", "1", TCL_GLOBAL_ONLY);
   #else
   Tcl_SetVar(interp, "is_unix", "0", TCL_GLOBAL_ONLY);
   #endif

   Tcl_SetVar(interp, "user_language", ((pLanguage != NULL) ? pLanguage : ""), TCL_GLOBAL_ONLY);

   sprintf(comm, "0x%06X", EPG_VERSION_NO);
   Tcl_SetVar(interp, "EPG_VERSION_NO", comm, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "EPG_VERSION", epg_version_str, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "NXTVEPG_URL", NXTVEPG_URL, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "NXTVEPG_MAILTO", NXTVEPG_MAILTO, TCL_GLOBAL_ONLY);

   Tcl_Init(interp);
   if (withTk)
   {
      if (Tk_Init(interp) != TCL_OK)
      {
         #ifndef USE_PRECOMPILED_TCL_LIBS
         fprintf(stderr, "Failed to initialise the Tk library at '%s' - exiting.\nTk error message: %s\n",
                         TK_LIBRARY_PATH, Tcl_GetStringResult(interp));
         exit(1);
         #endif
      }
   }

   #if (DEBUG_SWITCH_TCL_BGERR != ON)
   // switch off Tcl/Tk background error reports for release version
   sprintf(comm, "proc bgerror foo {}\n");
   eval_check(interp, comm);
   #endif

   // load all Tcl/Tk scripts which handle toplevel window, menus and dialogs
   LoadTcl_Init(withTk);

   if (withTk)
   {
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_ptr_up"), ptr_up_bits, ptr_up_width, ptr_up_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_ptr_down"), ptr_down_bits, ptr_down_width, ptr_down_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_pan_updown"), pan_updown_bits, pan_updown_width, pan_updown_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_qmark"), qmark_bits, qmark_width, qmark_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_zoom_in"), zoom_in_bits, zoom_in_width, zoom_in_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_zoom_out"), zoom_out_bits, zoom_out_width, zoom_out_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_colsel"), colsel_bits, colsel_width, colsel_height);
      Tk_DefineBitmap(interp, Tk_GetUid("nxtv_logo"), nxtv_logo_bits, nxtv_logo_width, nxtv_logo_height);

      #ifdef WIN32
      Tcl_CreateCommand(interp, "C_SystrayIcon", TclCbWinSystrayIcon, (ClientData) NULL, NULL);
      #endif

      sprintf(comm, "wm title . {Nextview EPG Decoder}\n"
                    "wm resizable . 0 1\n"
                    "wm iconbitmap . nxtv_logo\n"
                    "wm iconname . {Nextview EPG}\n");
      eval_check(interp, comm);
   }

   Tcl_ResetResult(interp);
   return (TRUE);
}

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

   // mark Tcl/Tk interpreter as uninitialized
   interp = NULL;
   // set db states to not initialized
   pUiDbContext = NULL;

   EpgLtoInit();

   #ifndef WIN32
   signal(SIGINT, signal_handler);
   signal(SIGTERM, signal_handler);
   signal(SIGHUP, signal_handler);
   // ignore signal CHLD because we don't care about our children (e.g. auto-started daemon)
   // this is overridden by the BT driver in non-threaded mode (to catch death of the acq slave)
   signal(SIGCHLD, SIG_IGN);
   #else
   hMainInstance = hInstance;
   // set up callback to catch shutdown messages (requires tk83.dll patch!)
   Tk_RegisterMainDestructionHandler(WinApiDestructionHandler);
   // set up an handler to catch fatal exceptions
   #ifndef __MINGW32__
   __try {
   #else
   SetUnhandledExceptionFilter(WinApiExceptionHandler);
   #endif
   SetArgv(&argc, &argv);
   #endif
   ParseArgv(argc, argv);
   pOptArgv0 = argv[0];

   #ifdef USE_DAEMON
   if (optDaemonMode)
   {  // deamon mode -> detach from tty
      #ifndef WIN32
      if (optNoDetach == FALSE)
      {
         if (fork() > 0)
            exit(0);
         close(0);
         open("/dev/null", O_RDONLY, 0);
         #if DEBUG_SWITCH == OFF
         close(1);
         open("/dev/null", O_WRONLY, 0);
         close(2);
         dup(1);
         #endif
         setsid();
      }
      #endif
      EpgAcqServer_Init(optNoDetach);
      EpgAcqCtl_InitDaemon();
   }
   #endif

   // set up the directory for the databases (unless in demo mode)
   if (EpgDbSavSetupDir(dbdir, pDemoDatabase) == FALSE)
   {  // failed to create dir: message was already issued, so just exit
      exit(-1);
   }
   // scan the database directory
   EpgContextCtl_InitCache();

   if (optDumpMode == EPGTAB_DUMP_NONE)
   {
      // UNIX must fork the VBI slave before GUI startup or the slave will inherit all X11 file handles
      BtDriver_Init();
      #ifdef WIN32
      WintvSharedMem_Init();
      #endif
   }

   // initialize Tcl interpreter and compile all scripts
   // Tk is only initialized if a GUI will be opened
   ui_init(argc, argv, ((optDaemonMode == FALSE) && (optDumpMode == EPGTAB_DUMP_NONE)));

   should_exit = FALSE;
   exitHandler = Tcl_AsyncCreate(AsyncHandler_AppTerminate, NULL);

   // load the user configuration from the rc/ini file
   sprintf(comm, "LoadRcFile {%s} %d", rcfile, (strcmp(defaultRcFile, rcfile) == 0));
   eval_check(interp, comm);

   if (optDumpMode != EPGTAB_DUMP_NONE)
   {  // dump mode: just dump the database, then exit
      if (startUiCni == 0x00ff)
         pUiDbContext = MenuCmd_MergeDatabases();
      else
         pUiDbContext = EpgContextCtl_Open(startUiCni, CTX_FAIL_RET_NULL, CTX_RELOAD_ERR_REQ);

      if (pUiDbContext != NULL)
      {
         if (optDumpMode == EPGTAB_DUMP_XML)
            PiOutput_DumpDatabaseXml(pUiDbContext, stdout);
         else
            EpgTabDump_Database(pUiDbContext, stdout, optDumpMode);
      }
      EpgContextCtl_Close(pUiDbContext);
   }
   else if (optDaemonMode == FALSE)
   {  // normal GUI mode

      eval_check(interp, "LoadWidgetOptions\n"
                         "CreateMainWindow\n"
                         "CreateMenubar\n"
                         "ApplyRcSettingsToMenu\n");

      UiControl_Init();

      if (pDemoDatabase != NULL)
      {  // demo mode -> open the db given by -demo cmd line arg
         pUiDbContext = EpgContextCtl_OpenDemo();
         if (EpgDbContextGetCni(pUiDbContext) == 0)
         {
            pDemoDatabase = NULL;
            if (EpgDbSavSetupDir(dbdir, pDemoDatabase) == FALSE)
            {  // failed to create dir: message was already issued, so just exit
               exit(-1);
            }
         }
         disableAcq = TRUE;
      }
      else
      {  // open the database given by -prov or the last one used
         OpenInitialDb(startUiCni);
      }
      #ifdef USE_DAEMON
      EpgAcqClient_Init(&EventHandler_NetworkUpdate);
      SetNetAcqParams(interp, FALSE);
      #endif
      // pass TV card hardware parameters to the driver
      SetHardwareConfig(interp, videoCardIndex);
      SetAcquisitionMode(NETACQ_DEFAULT);
      SetUserLanguage(interp);

      // initialize the GUI control modules
      StatsWin_Create();
      TimeScale_Create();
      MenuCmd_Init(pDemoDatabase != NULL);
      PiFilter_Create();
      PiOutput_Create();
      PiListBox_Create();
      #ifndef WIN32
      Xawtv_Init();
      #else
      WintvCfg_Init(TRUE);
      Wintv_Init();
      #endif

      // draw the clock and update it every second afterwords
      EventHandler_UpdateClock(NULL);

      if (startIconified)
         eval_check(interp, "wm iconify .");

      // init main window title, PI listbox state and status line
      UiControl_AiStateChange(DB_TARGET_UI);

      // wait until window is open and everything displayed
      while ( (Tk_GetNumMainWindows() > 0) &&
              Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT) )
         ;

      #if defined(WIN32) && !defined(ICON_PATCHED_INTO_DLL)
      // set app icon in window title bar - note: must be called *after* the window is mapped!
      SetWindowsIcon(hInstance);
      #endif

      if (disableAcq == FALSE)
      {  // enable EPG acquisition
         AutoStartAcq(interp);
      }

      if (Tk_GetNumMainWindows() > 0)
      {
         // remove expired items from database and listbox every minute
         expirationHandler = Tcl_CreateTimerHandler(1000 * (60 - time(NULL) % 60), EventHandler_TimerDbSetDateTime, NULL);

         while (Tk_GetNumMainWindows() > 0)
         {
            if (pMainIdleEventQueue == NULL)
            {
               Tcl_DoOneEvent(0);
            }
            else
            {
               if (Tcl_DoOneEvent(TCL_DONT_WAIT) == 0)
               {  // no events pending -> schedule my idle events
                  ProcessMainIdleEvent();
               }
            }

            if (should_exit)
            {
               break;
            }
         }

         if (Tk_GetNumMainWindows() > 0)
         {
            // remove handlers to prevent invokation after death of main window
            if (clockHandler != NULL)
            {
               Tcl_DeleteTimerHandler(clockHandler);
               clockHandler = NULL;
            }
            if (expirationHandler != NULL)
            {
               Tcl_DeleteTimerHandler(expirationHandler);
               expirationHandler = NULL;
            }
            Tcl_AsyncDelete(exitHandler);
            exitHandler = NULL;
            // execute pending updates and close main window
            sprintf(comm, "update; destroy .");
            eval_check(interp, comm);
         }
      }
      else
         debug0("could not open the main window - exiting.");
   }
   #ifdef USE_DAEMON
   else
   {  // Daemon mode: no GUI - just do acq and handle requests from GUI via sockets

      // pass configurable parameters to the network server (e.g. enable logging)
      SetNetAcqParams(interp, TRUE);
      SetHardwareConfig(interp, videoCardIndex);

      if (SetDaemonAcquisitionMode(startUiCni, optAcqPassive))
      {
         if (EpgAcqCtl_Start())
         {
            // start listening for client connections (at least on the named socket in /tmp)
            if (EpgAcqServer_Listen())
            {
               #ifdef WIN32
               DaemonControlWindowCreate();
               #endif
               // if the daemon was started by the GUI, notify it that the daemon is ready
               DaemonTriggerGui();

               DaemonMainLoop();
            }
         }
         else
            EpgNetIo_Logger(LOG_ERR, -1, 0, "failed to start acquisition", NULL);
      }
      EpgAcqServer_Destroy();
   }
   #endif

   #if defined(WIN32) && !defined(__MINGW32__)
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {  // caught a fatal exception -> stop the driver to prevent system crash ("blue screen")
      debug1("FATAL exception caught: %d", GetExceptionCode());
      // skip EpgAcqCtl_Stop() because it tries to dump the db - do as little as possible here
      BtDriver_Exit();
      ExitProcess(-1);
   }
   #endif

   // stop EPG acquisition and the driver
   EpgScan_Stop();
   EpgAcqCtl_Stop();
   #ifdef WIN32
   WintvSharedMem_Exit();
   #endif
   BtDriver_Exit();

   // shut down all GUI modules
   if ((optDaemonMode == FALSE) && (optDumpMode == EPGTAB_DUMP_NONE))
   {
      EpgContextCtl_Close(pUiDbContext);
      pUiDbContext = NULL;

      PiFilter_Destroy();
      PiListBox_Destroy();
      PiOutput_Destroy();
      #ifndef WIN32
      Xawtv_Destroy();
      #else
      Wintv_Destroy();
      WintvCfg_Destroy();
      WinSystrayIcon(FALSE);
      #endif
      #ifdef USE_DAEMON
      EpgAcqClient_Destroy();
      #endif
   }
   #if defined(WIN32) && defined(USE_DAEMON)
   else if (optDaemonMode)
   {  // notify the GUI in case we haven't done so before
      DaemonTriggerGui();
      // remove the window now to indicate the driver is down and the TV card free
      DaemonControlWindowDestroy();
   }
   #endif

   #if CHK_MALLOC == ON
   DiscardAllMainIdleEvents();
   EpgContextCtl_ClearCache();
   #ifdef WIN32
   xfree(argv);
   #else
   xfree((char*)defaultDbDir);
   #endif
   // check for allocated memory that was not freed
   chk_memleakage();
   #endif

   return 0;
}

