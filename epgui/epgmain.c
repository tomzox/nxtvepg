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
 *    Main entry point for nxtvepg (but not nxtvepgd): contains init
 *    of all modules and GUI main loop.
 *    See README in the top level directory for a general description.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgmain.c,v 1.163 2008/10/19 17:52:30 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#ifndef WIN32
#include <unistd.h>
#include <sys/param.h>
#include <sys/time.h>
#include <signal.h>
#else
#include <windows.h>
#include <winsock2.h>
#include <io.h>
#include <direct.h>
#endif
#include <locale.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
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
#include "epgui/epgsetup.h"
#include "epgui/pibox.h"
#include "epgui/pioutput.h"
#include "epgui/pifilter.h"
#include "epgui/piremind.h"
#include "epgui/xiccc.h"
#include "epgui/xawtv.h"
#include "epgui/wmhooks.h"
#include "epgui/wintv.h"
#include "epgui/wintvui.h"
#include "epgui/uidump.h"
#include "epgui/shellcmd.h"
#include "epgui/loadtcl.h"
#include "epgui/rcfile.h"
#include "epgui/cmdline.h"
#include "epgui/daemon.h"

#include "images/nxtv_logo.xbm"
#include "images/nxtv_small.xbm"
#include "images/ptr_up.xbm"
#include "images/ptr_down.xbm"
#include "images/ptr_down_right.xbm"
#include "images/ptr_up_left.xbm"
#include "images/pan_updown.xbm"
#include "images/qmark.xbm"
#include "images/zoom_in.xbm"
#include "images/zoom_out.xbm"
#include "images/colsel.xbm"
#include "images/col_plus.xbm"
#include "images/col_minus.xbm"
#include "images/hatch.xbm"

#include "epgvbi/btdrv.h"
#include "epgvbi/winshmsrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/syserrmsg.h"
#include "epgui/epgmain.h"


#ifndef USE_PRECOMPILED_TCL_LIBS
# if !defined(TCL_LIBRARY_PATH) || !defined(TK_LIBRARY_PATH)
#  error "Must define both TK_ and TCL_LIBRARY_PATH"
# endif
#else
# define TCL_LIBRARY_PATH  "."
# define TK_LIBRARY_PATH   "."
#endif

Tcl_Interp * interp;               // Interpreter for application
char comm[TCL_COMM_BUF_SIZE + 1];  // Command buffer (one extra byte for overflow detection)

// handling of global timer events & signals
static bool should_exit = FALSE;
static Tcl_TimerToken clockHandler = NULL;
static Tcl_TimerToken expirationHandler = NULL;
#ifndef WIN32
static Tcl_AsyncHandler exitAsyncHandler = NULL;
static Tcl_AsyncHandler signalAsyncHandler = NULL;
#endif
Tcl_Encoding encIso88591 = NULL;

#ifdef WIN32
HINSTANCE  hMainInstance;  // copy of win32 instance handle
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
static time_t   uiMinuteTime;

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
               pLast->pNext = pWalk->pNext;
            else
               pMainIdleEventQueue = pWalk->pNext;
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
// Query current UI wallclock time
// - used to determine if PI fall into past, now or then (<, ==, >)
// - formerly the expire time threshold was used, but this doesn't work while
//   the threshold is moved into the past (to show expired PI in pibox)
//
time_t EpgGetUiMinuteTime( void )
{
   return uiMinuteTime;
}

// ---------------------------------------------------------------------------
// Regular event, called every minute
//
static void ClockMinEvent( ClientData clientData )
{
   // rounding expire time down to full minute, in case the handler is called late
   // so that programmes which end in that minute do always expire; also add a few
   // seconds to round up to the next minute in case we're called early
   uiMinuteTime  = time(NULL) + 2;
   uiMinuteTime -= uiMinuteTime % 60;

   // remove expired PI from the listing
   PiFilter_Expire();

   // refresh the listbox to remove expired PI
   PiBox_Refresh();
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
      if ( EpgAcqCtl_IsActive() )
      {  // acq currently not running -> start
         AutoStartAcq();
      }
      else
      {  // acq currently running -> stop it or disconnect from server
         EpgAcqCtl_Stop();
      }
   }
}
#endif

#ifdef WIN32
// ---------------------------------------------------------------------------
// WIN32: Advise user to configure the TV card
//
static void Main_PopupTvCardSetup( ClientData clientData )
{
   MenuCmd_PopupTvCardSetup();
}
#endif

// ---------------------------------------------------------------------------
// Start database export
//
static void MainStartDump( void )
{
   uint provCni;

   provCni = CmdLine_GetStartProviderCni();
   if (provCni == MERGED_PROV_CNI)
   {
      pUiDbContext = EpgSetup_MergeDatabases();
      if (pUiDbContext == NULL)
         printf("<!-- nxtvepg database merge failed: check merge configuration -->\n");
   }
   else if (provCni != 0)
   {
      pUiDbContext = EpgContextCtl_Open(provCni, FALSE, CTX_FAIL_RET_NULL, CTX_RELOAD_ERR_REQ);
      if (pUiDbContext == NULL)
         printf("<!-- nxtvepg failed to load database %04X -->\n", provCni);
   }
   else
   {  // this case should be prevented in the command line parameter check
      printf("<!-- no provider specified -->\n");
      pUiDbContext = NULL;
   }

   if (pUiDbContext != NULL)
   {
      SetUserLanguage(interp);

      EpgDump_Standalone(pUiDbContext, stdout,
                         mainOpts.optDumpMode, mainOpts.optDumpSubMode, mainOpts.optDumpFilter);

      EpgContextCtl_Close(pUiDbContext);
   }
}

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
#endif  // USE_DAEMON

#ifndef WIN32
// ----------------------------------------------------------------------------
// Execute remote command
//
static char * EpgMain_ExecRemoteCmd( char ** pArgv, uint argc )
{
   if (argc > 0)
   {
      if (strcasecmp(pArgv[0], "quit") == 0)
      {
         sprintf(comm, "destroy .");
         eval_check(interp, comm);
      }
      else if (strcasecmp(pArgv[0], "raise") == 0)
      {
         sprintf(comm, "wm deiconify .\nraise .");
         eval_check(interp, comm);
      }
      else if (strcasecmp(pArgv[0], "iconify") == 0)
      {
         sprintf(comm, "wm iconify .");
         eval_check(interp, comm);
      }
      else if (strcasecmp(pArgv[0], "deiconify") == 0)
      {
         sprintf(comm, "wm deiconify .");
         eval_check(interp, comm);
      }
      else if (strcasecmp(pArgv[0], "acq_on") == 0)
      {
         if ( EpgAcqCtl_IsActive() == FALSE )
         {
            AutoStartAcq();
         }
      }
      else if (strcasecmp(pArgv[0], "acq_off") == 0)
      {
         EpgAcqCtl_Stop();
      }
      else
      {
         debug1("EpgMain-ExecRemoteCmd: unknown command '%s'", pArgv[0]);
      }
   }
   return NULL;
}

// ----------------------------------------------------------------------------
// Event handler
// - this function is called for every incoming X event
// - filters out events from the xawtv main window
//
static int RemoteControl_XEvent( ClientData clientData, XEvent *eventPtr )
{
   XICCC_STATE * pXi;
   bool needHandler;

   pXi = (void *) clientData;

   return Xiccc_XEvent(eventPtr, pXi, &needHandler);
}

// ----------------------------------------------------------------------------
// Search other nxtvepg instance and pass remote control command
//
static void RemoteControlCmd( void )
{
   XICCC_STATE xiccc;
   Tk_Window  mainWindow;
   Display   * dpy;
   const char * pCmdStr;

   switch (mainOpts.optRemCtrl)
   {
      case REM_CTRL_QUIT: pCmdStr = "quit"; break;
      case REM_CTRL_RAISE: pCmdStr = "raise"; break;
      case REM_CTRL_ICONIFY: pCmdStr = "iconify"; break;
      case REM_CTRL_DEICONIFY: pCmdStr = "deiconify"; break;
      case REM_CTRL_ACQ_ON: pCmdStr = "acq_on"; break;
      case REM_CTRL_ACQ_OFF: pCmdStr = "acq_off"; break;
      default:
         debug1("RemoteControlCmd: unknown mode %d", mainOpts.optRemCtrl);
         pCmdStr = NULL;
         break;
   }
   if (pCmdStr != NULL)
   {
      eval_check(interp, "wm withdraw .");

      // wait until window is open and everything displayed
      while ( (Tk_GetNumMainWindows() > 0) &&
              Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT) )
         ;

      mainWindow = Tk_MainWindow(interp);
      if (mainWindow != NULL)
      {
         dpy = Tk_Display(mainWindow);
         if (dpy != NULL)
         {
            char * pIdArgv;
            uint   idLen;

            Tk_CreateGenericHandler(RemoteControl_XEvent, (void *)&xiccc);

            Xiccc_BuildArgv(&pIdArgv, &idLen, ICCCM_PROTO_VSTR, "nxtvepg -remctrl", EPG_VERSION_STR, "", NULL);
            Xiccc_Initialize(&xiccc, dpy, FALSE, pIdArgv, idLen);
            xfree(pIdArgv);

            Xiccc_SearchPeer(&xiccc);
            if (xiccc.events & XICCC_NEW_PEER)
            {
               xiccc.events &= ~XICCC_NEW_PEER;
               Xiccc_SendQuery(&xiccc, pCmdStr, strlen(pCmdStr)+1, xiccc.atoms._NXTVEPG_REMOTE,
                               xiccc.atoms._NXTVEPG_REMOTE_RESULT);

               while (should_exit == FALSE)
               {
                  Tcl_DoOneEvent(0);

                  if (xiccc.events & XICCC_LOST_PEER)
                  {
                     fprintf(stderr, "Lost connection\n");
                     xiccc.events &= ~XICCC_LOST_PEER;
                     break;
                  }
                  if (xiccc.events & XICCC_REMOTE_REPLY)
                  {
                     char ** pArgv;
                     uint  argc;

                     xiccc.events &= ~XICCC_REMOTE_REPLY;

                     if (Xiccc_SplitArgv(dpy, xiccc.manager_wid, xiccc.atoms._NXTVEPG_REMOTE_RESULT, &pArgv, &argc))
                     {
                        // note: we're expecting only one result arg
                        if (argc > 0)
                           printf("%s", pArgv[0]);
                        xfree(pArgv);
                     }
                     break;
                  }
               }
            }

            Xiccc_Destroy(&xiccc);
         }
      }
   }
}

#else // WIN32
// ---------------------------------------------------------------------------
// WIN32 daemon: maintain an invisible Window to catch shutdown messages
// - on win32 the only way to terminate the daemon is by sending a command to this
//   window (the daemon MUST NOT be terminated via the task manager, because
//   this would not allow it to shut down the driver, hence Windows would crash)
// - the existance of the window is also used to detect if the daemon is running
//   by the GUI client
//

static const char * const remCtrlWndClassname = "nxtvepg_remctrl";
static uint      remoteControlMsg = 0;
static Tcl_AsyncHandler remoteControlMsgHandler = NULL;

// ---------------------------------------------------------------------------
// Process remote control message sent via nxtvepg command line
//
static void EventHandler_RemoteControlMsg( ClientData clientData )
{
   switch ( PVOID2UINT(clientData) )
   {
      case UWM_ACQ_ON:
         if ( EpgAcqCtl_IsActive() == FALSE )
         {
            AutoStartAcq();
         }
         break;

      case UWM_ACQ_OFF:
         EpgAcqCtl_Stop();
         break;

      case UWM_RAISE:
         sprintf(comm, "wm deiconify .\nraise .");
         eval_check(interp, comm);
         break;

      case UWM_ICONIFY:
         sprintf(comm, "wm iconify .");
         eval_check(interp, comm);
         break;

      case UWM_DEICONIFY:
         sprintf(comm, "wm deiconify .");
         eval_check(interp, comm);
         break;

      case UWM_QUIT:
         sprintf(comm, "destroy .");
         eval_check(interp, comm);
         break;

      default:
         debug1("EventHandler-RemoteControlMsg: unknown message code 0x%X\n", PVOID2UINT(clientData));
         break;
   }
}

// ---------------------------------------------------------------------------
// Triggered by the remote control event thread
// - here only an event is inserted into the main event handler
//
static int AsyncHandler_RemoteControlMsg( ClientData clientData, Tcl_Interp *interp, int code )
{
   AddMainIdleEvent(EventHandler_RemoteControlMsg, UINT2PVOID(remoteControlMsg), TRUE);

   return code;
}

// ---------------------------------------------------------------------------
// Message handler for the GUI's invisible remote control window
// - only "destructive" events are caught
//
static LRESULT CALLBACK RemoteControlWindowMsgCb( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam )
{
   switch (message)
   {
      case UWM_QUIT:
      case UWM_RAISE:
      case UWM_ICONIFY:
      case UWM_DEICONIFY:
      case UWM_ACQ_ON:
      case UWM_ACQ_OFF:
         if (remoteControlMsgHandler != NULL)
         {
            remoteControlMsg = message;
            Tcl_AsyncMark(remoteControlMsgHandler);
         }
         else
            debug0("RemoteControl-WindowMsgCb: message handler not initialized");
         return TRUE;
   }
   return DefWindowProc(hwnd, message, wParam, lParam);
}

// ----------------------------------------------------------------------------
// Search other nxtvepg instance and pass remote control command
//
static void RemoteControlCmd( void )
{
   ATOM classAtom;
   HWND OtherNxtvepgHWnd = NULL;
   WNDCLASSEX  wc;
   DWORD uwm;

   memset(&wc, 0, sizeof(wc));
   wc.cbSize        = sizeof(WNDCLASSEX);
   wc.hInstance     = hMainInstance;
   wc.lpfnWndProc   = RemoteControlWindowMsgCb;
   wc.lpszClassName = remCtrlWndClassname;
   classAtom = RegisterClassEx(&wc);
   if (classAtom != 0)
   {
      OtherNxtvepgHWnd = FindWindowEx(NULL, NULL, INT2PVOID(classAtom), NULL);
      if (OtherNxtvepgHWnd != NULL) 
      {  // found another instance of the application

         switch (mainOpts.optRemCtrl)
         {
            case REM_CTRL_QUIT: uwm = UWM_QUIT; break;
            case REM_CTRL_RAISE: uwm = UWM_RAISE; break;
            case REM_CTRL_ICONIFY: uwm = UWM_ICONIFY; break;
            case REM_CTRL_DEICONIFY: uwm = UWM_DEICONIFY; break;
            case REM_CTRL_ACQ_ON: uwm = UWM_ACQ_ON; break;
            case REM_CTRL_ACQ_OFF: uwm = UWM_ACQ_OFF; break;
            default: uwm = 0;
               debug1("RemoteControlCmd: unknown mode %d", mainOpts.optRemCtrl);
               break;
         }

         if (uwm != 0)
         {
            PostMessage(OtherNxtvepgHWnd, uwm, 0, 0);
         }
         ExitProcess(0);
      }
   }
   // error: no remote instance found
   // do not display message box to allow use by batch scripts
   ExitProcess(1);
}
#endif // WIN32

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
               sprintf(comm, "tk_messageBox -type ok -icon error -title {nxtvepg} "
                             "-message {nxtvepg executable not found. Make sure the program file is in your $PATH.}");
            }
            else
            {  // any other errors: print system error message
               sprintf(comm, "tk_messageBox -type ok -icon error -title {nxtvepg} "
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
         eval_check(interp, "tk_messageBox -type ok -icon error -title {nxtvepg} "
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
      if (mainOpts.isUserRcFile)
      {
         daemonArgv[daemonArgc++] = "-rcfile";
         daemonArgv[daemonArgc++] = (char *) mainOpts.rcfile;
      }
      if (strcmp(mainOpts.defaultDbDir, mainOpts.dbdir) != 0)
      {
         daemonArgv[daemonArgc++] = "-dbdir";
         daemonArgv[daemonArgc++] = (char *) mainOpts.dbdir;
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

            if (mainOpts.pOptArgv0 != NULL)
               execv(mainOpts.pOptArgv0, daemonArgv);
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

   if ( Daemon_CheckIfRunning() )
   {  // daemon successfuly started -> connect via socket
      if ( EpgAcqCtl_Start() == FALSE )
      {
         UiControlMsg_NetAcqError();
      }
   }
   else
   {  // inform the user about the failure
      eval_check(interp, "tk_messageBox -type ok -icon error -title {nxtvepg} "
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

   pCmdLineStr = xmalloc(100 + strlen(mainOpts.rcfile) + strlen(mainOpts.dbdir));
   sprintf(pCmdLineStr, "nxtvepg.exe -daemon -guipipe %ld", GetCurrentProcessId());
   pErrMsg = NULL;
   errCode = 0;

   if (mainOpts.isUserRcFile)
   {
      sprintf(pCmdLineStr + strlen(pCmdLineStr), " -rcfile \"%s\"", (char *) mainOpts.rcfile);
   }
   if (strcmp(mainOpts.defaultDbDir, mainOpts.dbdir) != 0)
   {
      sprintf(pCmdLineStr + strlen(pCmdLineStr), " -dbdir \"%s\"", (char *) mainOpts.dbdir);
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
      sprintf(comm, "tk_messageBox -type ok -icon error -title {nxtvepg} -message {%s: ", pErrMsg);
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
#endif  // WIN32

#ifndef WIN32
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
// called by signal handler when HUP arrives
//
static int AsyncHandler_Signalled( ClientData clientData, Tcl_Interp *interp, int code )
{
   AddMainIdleEvent(EventHandler_SigHup, NULL, TRUE);

   return code;
}

// ---------------------------------------------------------------------------
// graceful exit upon signals
//
static void MainSignalHandler( int sigval )
{
   if ((sigval == SIGHUP) && IS_GUI_MODE(mainOpts))
   {  // toggle acquisition on/off (unless in daemon mode)
      if (signalAsyncHandler != NULL)
      {  // trigger an event, so that the Tcl/Tk event handler immediately wakes up
         Tcl_AsyncMark(signalAsyncHandler);
      }
   }
   else
   {
      #ifdef USE_DAEMON
      if (IS_DAEMON(mainOpts))
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

         if (exitAsyncHandler != NULL)
         {  // trigger an event, so that the Tcl/Tk event handler immediately wakes up
            Tcl_AsyncMark(exitAsyncHandler);
         }
      }
      // flush debug output
      DBGONLY(fflush(stdout); fflush(stderr));

      // this flag breaks the main loop
      should_exit = TRUE;
   }

   signal(sigval, MainSignalHandler);
}

static void MainSignalSetup( void )
{
   signal(SIGINT, MainSignalHandler);
   signal(SIGTERM, MainSignalHandler);
   signal(SIGHUP, MainSignalHandler);
   // ignore signal CHLD because we don't care about our children (e.g. auto-started daemon)
   // this is overridden by the BT driver in non-threaded mode (to catch death of the acq slave)
   signal(SIGCHLD, SIG_IGN);
}
#else  // WIN32

// ---------------------------------------------------------------------------
// Callback to deal with Windows shutdown
// - called when WM_QUERYENDSESSION or WM_ENDSESSION message is received
//   this currently requires a patch in the Tk library! (see README.tcl)
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
   EpgAcqCtl_Destroy(FALSE);

   // exit the application
   ExitProcess(0);
}

#if (TCL_MAJOR_VERSION != 8) || (TCL_MINOR_VERSION >= 5)
static int TclCbWinHandleShutdown( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   WinApiDestructionHandler(NULL);
   return TCL_OK;
}
#endif

static LONG WINAPI WinApiExceptionHandler(struct _EXCEPTION_POINTERS *exc_info)
{
   static bool inExit = FALSE;

   //debug1("FATAL exception caught: %d", GetExceptionCode());
   debug0("FATAL exception caught");

   // prevent recursive calls - may occur upon crash in acq stop function
   if (inExit == FALSE)
   {
      inExit = TRUE;

      // emergency driver shutdown - do as little as possible here (skip db dump etc.)
      EpgAcqCtl_Destroy(TRUE);
      ExitProcess(-1);
   }
   else
   {
      ExitProcess(-1);
   }

   // dummy return
   return EXCEPTION_EXECUTE_HANDLER;
}

#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION <= 4)
// Declare the new callback registration function
// this function is patched into the tk83.dll and hence not listed in the standard header files (see README.tcl)
extern TCL_STORAGE_CLASS void Tk_RegisterMainDestructionHandler( Tcl_CloseProc * handler );
#endif

#endif

static void TclTkPanicHandler( CONST84 char * format, ... )
{
   fatal1("Tcl/Tk panic caught: %s", format);

   EpgAcqCtl_Destroy(TRUE);
#ifdef WIN32
   ExitProcess(-2);
#else
   exit(-2);
#endif
}


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
#endif  // WIN32

// ----------------------------------------------------------------------------
// Systray state
//
#ifdef WIN32

static Tcl_AsyncHandler asyncSystrayHandler = NULL;
static HWND       hSystrayWnd = NULL;
static HANDLE     systrayThreadHandle;
static BOOL       stopSystrayThread;
static BOOL       systrayDoubleClick;
static POINT      systrayCursorPos;

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
      // note "tk_popup" does no longer work since Tk 8.4.9: menu pos is off by 3cm
      sprintf(comm, ".systray post %ld %ld", systrayCursorPos.x, systrayCursorPos.y);
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
               GetCursorPos(&systrayCursorPos);
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
      strcpy(nid.szTip, "nxtvepg");

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

// ----------------------------------------------------------------------------
// Search if an nxtvepg GUI is already running
//
static bool RaiseNxtvepgGuiWindow( void )
{
   ATOM classAtom;
   HWND OtherNxtvepgHWnd = NULL;
   int  answer;
   WNDCLASSEX  wc;

   memset(&wc, 0, sizeof(wc));
   wc.cbSize        = sizeof(WNDCLASSEX);
   wc.hInstance     = hMainInstance;
   wc.lpfnWndProc   = RemoteControlWindowMsgCb;
   wc.lpszClassName = remCtrlWndClassname;
   classAtom = RegisterClassEx(&wc);
   if (classAtom != 0)
   {
      OtherNxtvepgHWnd = FindWindowEx(NULL, NULL, INT2PVOID(classAtom), NULL);
      if (OtherNxtvepgHWnd != NULL) 
      {  // found another instance of the application

         answer = MessageBox(NULL, "nxtvepg is already running.\n"
                                   "Really start a second time?", "nxtvepg",
                                   MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON2 | MB_TASKMODAL | MB_SETFOREGROUND);
         if (answer == IDNO)
         {  // "No" (default) -> raise other window
            if (mainOpts.startIconified == FALSE)
            {
               #if 0
               if (IsIconic(OtherNxtvepgHWnd))
                  ShowWindow(OtherNxtvepgHWnd, SW_RESTORE);
               SetForegroundWindow(OtherNxtvepgHWnd);
               #else
               PostMessage(OtherNxtvepgHWnd, UWM_RAISE, 0, 0);
               #endif
            }
            ExitProcess(0);
         }
         else if (answer == IDCANCEL)
         {  // "Cancel"
            ExitProcess(0);
         }
         else if (answer == IDYES)
         {  // "Yes" -> continue launch, but prevent unnecessary warnings
            // (note: tvapp interaction setup is suppressed by checking window handle)

            if (mainOpts.videoCardIndex == -1)
            {  // suppress acq through default card to avoid warning from driver
               mainOpts.disableAcq = TRUE;
            }
         }
      }
   }

   remoteControlMsgHandler = Tcl_AsyncCreate(AsyncHandler_RemoteControlMsg, NULL);
   RemoteControlWindowCreate(RemoteControlWindowMsgCb, remCtrlWndClassname);

   return (OtherNxtvepgHWnd != NULL);
}
#endif  // WIN32

// ---------------------------------------------------------------------------
// Helper function to convert text content into UTF-8
// - required for text display in the GUI
// - note when compile switch USE_UTF8 is defined, PI content is already in UTF-8
// - prefix and postfix must be encoded in ASCII and are appended unchanged
//
Tcl_Obj * TranscodeToUtf8( T_EPG_ENCODING enc,
                           const char * pPrefix, const char * pStr, const char * pPostfix
                           /*, const char * pCallerFile, int callerLine*/ )
{
   Tcl_Obj * pObj;
   Tcl_DString dstr;

   if (pStr == NULL)
   {
      //debug2("TranscodeToUtf8: invalid NULL ptr param in %s, line %d", pCallerFile, callerLine);
      fatal0("TranscodeToUtf8: invalid NULL ptr param");
      return Tcl_NewStringObj("0", -1);
   }

   switch (enc)
   {
      case EPG_ENC_ASCII:
         Tcl_DStringInit(&dstr);
         Tcl_DStringAppend(&dstr, pStr, -1);
         break;

      case EPG_ENC_NXTVEPG:
#ifdef USE_UTF8
         Tcl_DStringInit(&dstr);
         Tcl_DStringAppend(&dstr, pStr, -1);
#else
         Tcl_ExternalToUtfDString(encIso88591, pStr, -1, &dstr);
#endif
         break;

      case EPG_ENC_ISO_8859_1:
         Tcl_ExternalToUtfDString(encIso88591, pStr, -1, &dstr);
         break;

      case EPG_ENC_SYSTEM:
      default:
         Tcl_ExternalToUtfDString(NULL, pStr, -1, &dstr);
         break;
   }

   if ((pPrefix != NULL) && (*pPrefix != 0))
   {
      pObj = Tcl_NewStringObj(pPrefix, -1);
      Tcl_AppendToObj(pObj, Tcl_DStringValue(&dstr), Tcl_DStringLength(&dstr));
   }
   else
   {
      pObj = Tcl_NewStringObj(Tcl_DStringValue(&dstr), Tcl_DStringLength(&dstr));
   }
   Tcl_DStringFree(&dstr);

   if ((pPostfix != NULL) && (*pPostfix != 0))
   {
      Tcl_AppendToObj(pObj, pPostfix, -1);
   }
   return pObj;
}

// ---------------------------------------------------------------------------
// Helper function to convert and append text to an UTF-8 string
// - postfix must be encoded in ASCII and is appended unchanged
//
Tcl_Obj * AppendToUtf8( T_EPG_ENCODING enc, Tcl_Obj * pObj,
                        const char * pStr, const char * pPostfix )
{
   Tcl_DString dstr;

   if ((pStr != NULL) && (*pStr != 0))
   {
      switch (enc)
      {
         case EPG_ENC_ASCII:
            Tcl_DStringInit(&dstr);
            Tcl_DStringAppend(&dstr, pStr, -1);
            break;

         case EPG_ENC_NXTVEPG:
#ifdef USE_UTF8
            Tcl_DStringInit(&dstr);
            Tcl_DStringAppend(&dstr, pStr, -1);
#else
            Tcl_ExternalToUtfDString(encIso88591, pStr, -1, &dstr);
#endif
            break;

         case EPG_ENC_ISO_8859_1:
            Tcl_ExternalToUtfDString(encIso88591, pStr, -1, &dstr);
            break;

         case EPG_ENC_SYSTEM:
         default:
            Tcl_ExternalToUtfDString(NULL, pStr, -1, &dstr);
            break;
      }

      Tcl_AppendToObj(pObj, Tcl_DStringValue(&dstr), Tcl_DStringLength(&dstr));
      Tcl_DStringFree(&dstr);
   }

   if ((pPostfix != NULL) && (*pPostfix != 0))
   {
      Tcl_AppendToObj(pObj, pPostfix, -1);
   }

   return pObj;
}


// ---------------------------------------------------------------------------
// Initialize the Tcl/Tk interpreter
//
static int ui_init( int argc, char **argv, bool withTk )
{
   CONST84 char * pLanguage;
   char *args;

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
      Tcl_SetVar2Ex(interp, "argc", NULL, Tcl_NewIntObj(argc - 1), TCL_GLOBAL_ONLY);
   }
   Tcl_SetVar(interp, "argv0", argv[0], TCL_GLOBAL_ONLY);

   Tcl_SetVar(interp, "tcl_library", TCL_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "tk_library", TK_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "tcl_interactive", "0", TCL_GLOBAL_ONLY);

   #ifndef WIN32
   Tcl_SetVar(interp, "x11_appdef_path", X11_APP_DEFAULTS, TCL_GLOBAL_ONLY);

   Tcl_SetVar2Ex(interp, "is_unix", NULL, Tcl_NewIntObj(1), TCL_GLOBAL_ONLY);
   #else
   Tcl_SetVar2Ex(interp, "is_unix", NULL, Tcl_NewIntObj(0), TCL_GLOBAL_ONLY);
   #endif

   // query language for default shortcut selection
   pLanguage = setlocale(LC_TIME, NULL);
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

   Tcl_SetPanicProc(TclTkPanicHandler);

   // load all Tcl/Tk scripts which handle toplevel window, menus and dialogs
   LoadTcl_Init(withTk);

   #if (DEBUG_SWITCH_TCL_BGERR != ON)
   // switch off Tcl/Tk background error reports for release version
   sprintf(comm, "proc bgerror foo {}\n");
   eval_check(interp, comm);
   #endif

   if (withTk)
   {
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_ptr_up"), ptr_up_bits, ptr_up_width, ptr_up_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_ptr_down"), ptr_down_bits, ptr_down_width, ptr_down_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_ptr_down_right"), ptr_down_right_bits, ptr_down_right_width, ptr_down_right_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_ptr_up_left"), ptr_up_left_bits, ptr_up_left_width, ptr_up_left_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_pan_updown"), pan_updown_bits, pan_updown_width, pan_updown_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_qmark"), qmark_bits, qmark_width, qmark_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_zoom_in"), zoom_in_bits, zoom_in_width, zoom_in_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_zoom_out"), zoom_out_bits, zoom_out_width, zoom_out_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_colsel"), colsel_bits, colsel_width, colsel_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_col_plus"), col_plus_bits, col_plus_width, col_plus_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_col_minus"), col_minus_bits, col_minus_width, col_minus_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_hatch"), hatch_bits, hatch_width, hatch_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_gray"), "\x01\x02", 2, 2);
      Tk_DefineBitmap(interp, Tk_GetUid("nxtv_logo"), nxtv_logo_bits, nxtv_logo_width, nxtv_logo_height);
      Tk_DefineBitmap(interp, Tk_GetUid("nxtv_small"), nxtv_small_bits, nxtv_small_width, nxtv_small_height);

      #ifdef WIN32
      Tcl_CreateCommand(interp, "C_SystrayIcon", TclCbWinSystrayIcon, (ClientData) NULL, NULL);
      #if (TCL_MAJOR_VERSION != 8) || (TCL_MINOR_VERSION >= 5)
      Tcl_CreateCommand(interp, "C_WinHandleShutdown", TclCbWinHandleShutdown, (ClientData) NULL, NULL);
      eval_check(interp, "wm protocol . WM_SAVE_YOURSELF C_WinHandleShutdown\n");
      #endif
      #endif

      sprintf(comm, "wm title . {nxtvepg}\n"
                    "wm resizable . 0 1\n"
      #ifndef WIN32
                    "wm iconbitmap . nxtv_logo\n"
                    "wm iconname . {nxtvepg}\n"
      #endif
                    "wm withdraw .\n");
      eval_check(interp, comm);

      #ifdef WIN32
      eval_check(interp, "update\n");
      #endif

      // set font for error messages
      // (for messages during startup, is overridden later with user-configured value)
      sprintf(comm, "option add *Dialog.msg.font {helvetica -12 bold} userDefault");
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
   bool is2ndInstance = FALSE;
   #endif

   // mark Tcl/Tk interpreter as uninitialized
   interp = NULL;
   // set db states to not initialized
   pUiDbContext = NULL;

   // set up the user-configured locale
   setlocale(LC_ALL, "");
   setlocale(LC_NUMERIC, "C");  // required for Tcl or parsing of floating point numbers fails

   EpgLtoInit();

   #ifdef WIN32
   hMainInstance = hInstance;
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION <= 4)
   // set up callback to catch shutdown messages (requires tk83.dll patch, see README.tcl83)
   Tk_RegisterMainDestructionHandler(WinApiDestructionHandler);
#endif
   // set up an handler to catch fatal exceptions
   #ifndef __MINGW32__
   __try {
   #else
   SetUnhandledExceptionFilter(WinApiExceptionHandler);
   #endif
   CmdLine_WinApiSetArgv(&argc, &argv);
   #endif  // WIN32

   CmdLine_Process(argc, argv, FALSE);

   #ifdef USE_DAEMON
   if (mainOpts.optDaemonMode == DAEMON_RUN)
   {  // deamon mode -> detach from tty
      Daemon_ForkIntoBackground();
   }
   #endif
   #ifdef WIN32
   if (IS_GUI_MODE(mainOpts))
   {
      is2ndInstance = RaiseNxtvepgGuiWindow();
   }
   #endif

   // set up the directory for the databases
   if (EpgDbSavSetupDir(mainOpts.dbdir) == FALSE)
   {  // failed to create dir: message was already issued, so just exit
      exit(-1);
   }
   EpgContextCtl_Init();

   if ( !IS_DUMP_MODE(mainOpts) && !IS_REMCTL_MODE(mainOpts) )
   {
      #ifndef WIN32
      MainSignalSetup();
      #endif
      // UNIX must fork the VBI slave before GUI startup or the slave will inherit all X11 file handles
      EpgAcqCtl_Init();
      #ifdef WIN32
      WintvSharedMem_Init(FALSE);
      #endif
   }

   if ( IS_GUI_MODE(mainOpts) || IS_DUMP_MODE(mainOpts) )
   {
      // initialize Tcl interpreter and compile all scripts
      // Tk is only initialized if a GUI will be opened
      ui_init(argc, argv, IS_GUI_MODE(mainOpts));

      #ifndef WIN32
      exitAsyncHandler = Tcl_AsyncCreate(AsyncHandler_AppTerminate, NULL);
      signalAsyncHandler = Tcl_AsyncCreate(AsyncHandler_Signalled, NULL);
      #endif

      encIso88591 = Tcl_GetEncoding(interp, "iso8859-1");

      RcFile_Init();
      LoadRcFile();
   }

   if ( IS_DUMP_MODE(mainOpts) )
   {
      EpgSetup_DbExpireDelay();
      MainStartDump();
   }
   else if ( IS_REMCTL_MODE(mainOpts) )
   {
      ui_init(argc, argv, TRUE);
      RemoteControlCmd();
   }
   #ifdef USE_DAEMON
   else if ( !IS_GUI_MODE(mainOpts) )
   {
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
         Daemon_SystemClockCmd(mainOpts.optDumpSubMode, CmdLine_GetStartProviderCni());
      }
      else if (mainOpts.optDaemonMode == EPG_CL_PROV_SCAN)
      {
         Daemon_ProvScanStart();
      }
      Daemon_Destroy();
   }
   #endif
   else
   {  // normal GUI mode

      eval_check(interp, "LoadWidgetOptions\n"
                         "CreateMainWindow\n"
                         "CreateMenubar\n"
                         "ApplyRcSettingsToMenu\n");

      UiControl_Init();

      // setup cut-off time for expired PI blocks during database reload
      EpgSetup_DbExpireDelay();

      // open the database given by -prov or the last one used
      EpgSetup_OpenUiDb( CmdLine_GetStartProviderCni() );

      #ifdef USE_DAEMON
      EpgAcqClient_Init(&EventHandler_NetworkUpdate);
      EpgSetup_NetAcq(FALSE);
      #endif
      // pass TV card hardware parameters to the driver
      EpgSetup_CardDriver(mainOpts.videoCardIndex);
      #ifdef USE_TTX_GRABBER
      EpgSetup_TtxGrabber();
      #endif
      SetUserLanguage(interp);
      uiMinuteTime  = time(NULL);
      uiMinuteTime -= uiMinuteTime % 60;

      // initialize the GUI control modules
      StatsWin_Create();
      TimeScale_Create();
      MenuCmd_Init();
      PiRemind_Create();
      PiFilter_Create();
      PiBox_Create();

      EpgDump_Init();
      ShellCmd_Init();
      WmHooks_Init(interp);
      WintvUi_Init();
      #ifndef WIN32
      Xawtv_Init(mainOpts.pTvX11Display, EpgMain_ExecRemoteCmd);
      #else
      Wintv_Init(is2ndInstance == FALSE);
      #endif

      // draw the clock and update it every second afterwords
      EventHandler_UpdateClock(NULL);

      if (mainOpts.startIconified)
         eval_check(interp, "wm iconify .");

      // wait until window is open and everything displayed
      while ( (Tk_GetNumMainWindows() > 0) &&
              Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT) )
         ;

      #if defined(WIN32) && !defined(ICON_PATCHED_INTO_DLL)
      // set app icon in window title bar - note: must be called *after* the window is mapped!
      SetWindowsIcon(hInstance);
      #elif !defined(WIN32)
      eval_check(interp, "C_Wm_SetIcon .");
      #endif
      sprintf(comm, "DisplayMainWindow %d", mainOpts.startIconified);
      eval_check(interp, comm);

      if (mainOpts.disableAcq == FALSE)
      {  // enable EPG acquisition
#ifdef WIN32
         if ( EpgSetup_CheckTvCardConfig() == FALSE )
         {
            AddMainIdleEvent(Main_PopupTvCardSetup, NULL, TRUE);
         }
         else
#endif
         {
            AutoStartAcq();
         }
      }

      // init main window title, PI listbox state and status line
      UiControl_AiStateChange(DB_TARGET_UI);

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
            #ifndef WIN32
            Tcl_AsyncDelete(exitAsyncHandler);
            exitAsyncHandler = NULL;
            Tcl_AsyncDelete(signalAsyncHandler);
            signalAsyncHandler = NULL;
            #endif
            Tcl_FreeEncoding(encIso88591);
            encIso88591 = NULL;
            // execute pending updates and close main window
            sprintf(comm, "update; destroy .");
            eval_check(interp, comm);
         }
      }
      else
         debug0("could not open the main window - exiting.");

      // stop EPG acquisition and the driver
      EpgScan_Stop();
      EpgAcqCtl_Stop();

      EpgContextCtl_Close(pUiDbContext);
      pUiDbContext = NULL;

      EpgDump_Destroy();
      ShellCmd_Destroy();

      PiFilter_Destroy();
      PiRemind_Destroy();
      PiBox_Destroy();
      WmHooks_Destroy();
      #ifndef WIN32
      Xawtv_Destroy();
      #else
      Wintv_Destroy();
      WintvUi_Destroy();
      WinSystrayIcon(FALSE);
      #endif
      #ifdef USE_DAEMON
      EpgAcqClient_Destroy();
      #endif
   }
   #if defined(WIN32) && !defined(__MINGW32__)
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {
      WinApiExceptionHandler(NULL);
   }
   #endif

   if ( !IS_DUMP_MODE(mainOpts) && !IS_REMCTL_MODE(mainOpts) )
   {
      #ifdef WIN32
      WintvSharedMem_Exit();
      #endif
      EpgAcqCtl_Destroy(FALSE);
   }
   RcFile_Destroy();

   #if CHK_MALLOC == ON
   DiscardAllMainIdleEvents();
   EpgContextCtl_Destroy();
   #ifdef WIN32
   xfree(argv);
   #endif
   CmdLine_Destroy();
   // check for allocated memory that was not freed
   chk_memleakage();
   #endif

   return 0;
}

