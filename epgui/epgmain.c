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
 *  $Id: epgmain.c,v 1.79 2002/01/31 20:35:07 tom Exp tom $
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
#include <locale.h>
#include <io.h>
#include <direct.h>
#endif
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

#include "epgui/nxtv_logo.xbm"
#include "epgui/ptr_up.xbm"
#include "epgui/ptr_down.xbm"
#include "epgui/pan_updown.xbm"
#include "epgui/qmark.xbm"

#include "epgvbi/btdrv.h"
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

extern char epgui_tcl_script[];
extern char help_tcl_script[];
extern char tcl_init_scripts[];

Tcl_Interp *interp;          // Interpreter for application
char comm[1000];             // Command buffer

// command line options
#ifdef WIN32
static const char * const defaultRcFile = "nxtvepg.ini";
static const char * const defaultDbDir  = ".";
#else
static const char * defaultRcFile = "~/.nxtvepgrc";
static const char * defaultDbDir  = EPG_DB_DIR;
#endif
static const char * rcfile = NULL;
static const char * dbdir  = NULL;
static int  videoCardIndex = -1;
static bool disableAcq = FALSE;
static bool optDaemonMode = FALSE;
static bool optNoDetach   = FALSE;
static bool optAcqPassive = FALSE;
static bool startIconified = FALSE;
static uint startUiCni = 0;
static const char *pDemoDatabase = NULL;
static const char *pOptArgv0 = NULL;
static int  optGuiPipe = -1;

// Global variable: if TRUE, will exit on the next iteration of the main loop
Tcl_AsyncHandler exitHandler = NULL;
Tcl_TimerToken clockHandler = NULL;
Tcl_TimerToken expirationHandler = NULL;
Tcl_TimerToken vpsPollingHandler = NULL;
int should_exit;

// queue for events called from the main loop when Tcl is idle
typedef struct MAIN_IDLE_EVENT_STRUCT
{
   struct MAIN_IDLE_EVENT_STRUCT *pNext;
   Tcl_IdleProc                  *IdleProc;
   ClientData                    clientData;
} MAIN_IDLE_EVENT;

MAIN_IDLE_EVENT * pMainIdleEventQueue = NULL;

EPGDB_CONTEXT * pUiDbContext;

// ---------------------------------------------------------------------------
// Execute a Tcl/Tk command line and check the result
//
int eval_check(Tcl_Interp *interp, char *cmd)
{
   int result;

   result = Tcl_Eval(interp, cmd);

   #if DEBUG_SWITCH == ON
   if (result != TCL_OK)
   {
      if (strlen(cmd) > 256)
         cmd[256] = 0;
      #if (DEBUG_SWITCH_TCL_BGERR == ON)
      {
         char *errbuf = xmalloc(strlen(cmd) + strlen(interp->result) + 20);
         sprintf(errbuf, "bgerror {%s: %s}", cmd, interp->result);
         Tcl_Eval(interp, errbuf);
         xfree(errbuf);
      }
      #else
      debug2("Command: %s\nError: %s", cmd, interp->result);
      #endif
   }
   #endif

   return result;
}

// ---------------------------------------------------------------------------
// Execute a Tcl/Tk command line and check the result
//
int eval_global(Tcl_Interp *interp, char *cmd)
{
   int result;

   result = Tcl_GlobalEval(interp, cmd);

   #if DEBUG_SWITCH == ON
   if (result != TCL_OK)
   {
      if (strlen(cmd) > 256)
         cmd[256] = 0;
      debug2("Command: %s\nError: %s", cmd, interp->result);
   }
   #endif

   return result;
}

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
// Regular event, called every second
//
static void MainEventHandler_ProcessScanPackets( ClientData clientData )
{
   EpgScan_ProcessPackets();
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
      strftime(comm, sizeof(comm) - 1, ".all.shortcuts.clock configure -text {%a %H:%M:%S}\n", localtime(&now));
      eval_check(interp, comm);

      if (EpgScan_IsActive() == FALSE)
      {
         // process VBI packets in the ring buffer
         if (EpgAcqCtl_ProcessPackets())
         {  // next time when idle, insert new blocks into the database
            AddMainIdleEvent(MainEventHandler_ProcessBlocks, NULL, TRUE);
         }
      }
      else
         AddMainIdleEvent(MainEventHandler_ProcessScanPackets, NULL, TRUE);

      clockHandler = Tcl_CreateTimerHandler(1000, EventHandler_UpdateClock, NULL);
   }
   else
      clockHandler = NULL;
}

// ---------------------------------------------------------------------------
// called every 200 milliseconds (every 5 frames)
// 
#ifndef WIN32
static void EventHandler_TimerVPSPolling( ClientData clientData )
{
   EpgAcqCtl_ProcessVps();

   vpsPollingHandler = Tcl_CreateTimerHandler(200, EventHandler_TimerVPSPolling, NULL);
}
#endif

// ---------------------------------------------------------------------------
// Invoked from main loop after SIGHUP
// - toggles acquisition on/off
// - the mode (i.e. local vs. daemon) is the last one used manually
//
#ifndef WIN32
static void EventHandler_SigHup( ClientData clientData )
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
#endif

// ---------------------------------------------------------------------------
// Handle client connection to EPG acquisition daemon
// - when the client is in network acquisition mode, it installs this handler
//   which is called whenever there is incoming data (or ready for writing under
//   certain circumstances)
// - note: in contrary to direct acquisition from the TV card we do not fork a
//   separate process, because there are no real-time constraints in this case.
// - incoming EPG blocks are put in a queue and picked up every second just like
//   in the other acquisition modes.
// - important: the handler must be deleted when the connection is closed, else
//   the Tcl/Tk event handling will hang up.
//
#ifdef USE_DAEMON
static int  networkFileHandle = -1;
static void EventHandler_Network( ClientData clientData, int mask );

// create, update or delete the handler with params returned from the epgacqclnt module
// may also be invoked as callback from the epgacqclnt module, e.g. when acq is stopped
static void EventHandler_NetworkUpdate( EPGACQ_EVHAND * pAcqEv )
{
   int mask;

   // remove the old handler if neccessary
   if ((pAcqEv->fd != networkFileHandle) && (networkFileHandle != -1))
   {
      Tcl_DeleteFileHandler(networkFileHandle);
      networkFileHandle = -1;
   }

   // create the event handler or update event mask
   if (pAcqEv->fd != -1)
   {
      // EPG blocks or other messages pending -> schedule handler
      if (pAcqEv->processQueue)
         AddMainIdleEvent(MainEventHandler_ProcessBlocks, NULL, TRUE);

      mask = 0;
      if (pAcqEv->blockOnRead)
         mask |= TCL_READABLE;
      if (pAcqEv->blockOnWrite)
         mask |= TCL_WRITABLE;

      Tcl_CreateFileHandler(pAcqEv->fd, mask, EventHandler_Network, (ClientData) pAcqEv->fd);
      networkFileHandle = pAcqEv->fd;
   }
}

// the actual callback
static void EventHandler_Network( ClientData clientData, int mask )
{
   EPGACQ_EVHAND acqEv;
   
   // invoke the handler
   acqEv.blockOnRead    = (mask & TCL_READABLE) != 0;
   acqEv.blockOnWrite   = (mask & TCL_WRITABLE) != 0;
   acqEv.processQueue   = FALSE;
   EpgAcqClient_HandleSocket(&acqEv);

   EventHandler_NetworkUpdate(&acqEv);
}

// ---------------------------------------------------------------------------
// Daemon main loop
//
static void DaemonMainLoop( void )
{
   struct timeval tvIdle;
   struct timeval tvAcq;
   struct timeval tvXawtv;
   struct timeval tv;
   fd_set  rd, wr;
   uint    max;

   gettimeofday(&tvAcq, NULL);
   tvIdle = tvAcq;
   tvXawtv = tvAcq;

   while ((should_exit == FALSE) && (pAcqDbContext != NULL))
   {
      gettimeofday(&tv, NULL);
      if (timercmp(&tv, &tvAcq, >))
      {  // read VBI device and add blocks to db
         if (EpgAcqCtl_ProcessPackets())
            EpgAcqCtl_ProcessBlocks();
         tvAcq = tv;
         tvAcq.tv_sec += 1;
      }
      if (timercmp(&tv, &tvIdle, >))
      {  // handle acquisition timeouts
         EpgAcqCtl_Idle();
         tvIdle = tv;
         tvIdle.tv_sec += 20;
      }
      if (timercmp(&tv, &tvXawtv, >))
      {  // handle VPS/PDC forwarding
         EpgAcqCtl_ProcessVps();
         tvXawtv = tv;
         tvXawtv.tv_usec += 200000L;
         if (tvXawtv.tv_usec > 1000000L)
         {
            tvXawtv.tv_sec  += 1L;
            tvXawtv.tv_usec -= 1000000L;
         }
      }

      FD_ZERO(&rd);
      FD_ZERO(&wr);
      max = EpgAcqServer_GetFdSet(&rd, &wr);

      // wait for any event, but max. 250 ms
      tv.tv_sec  = 0;
      tv.tv_usec = 250000L;

      if (select(((max > 0) ? (max + 1) : 0), &rd, &wr, NULL, &tv) != -1)
      {  // forward new blocks to network clients, handle incoming messages
         EpgAcqServer_HandleSockets(&rd, &wr);
      }
      else
      {
         if (errno != EINTR)
         {  // select syscall failed
            debug2("Daemon-MainLoop: select with max. fd %d: %s", max, strerror(errno));
            sleep(1);
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Background-wait for the daemon to be ready to accept client connection
//
static void EventHandler_DaemonStart( ClientData clientData, int mask )
{
   int fd = (int) clientData;
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
                            "-message {The daemon failed to start. Check the daemon log file for the cause.}");
         Tcl_ResetResult(interp);
      }
      // else: keep waiting
   }
}

// ---------------------------------------------------------------------------
// Start the daemon process from the GUI
//
bool EpgMain_StartDaemon( void )
{
   char  * daemonArgv[10];
   int     daemonArgc;
   char    fd_buf[10];
   int     pipe_fd[2];
   pid_t   pid;
   bool    result = FALSE;

   if (pipe(pipe_fd) == 0)
   {
      sprintf(fd_buf, "%d", pipe_fd[1]);

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
      daemonArgv[daemonArgc++] = NULL;

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
            Tcl_CreateFileHandler(pipe_fd[0], TCL_READABLE, EventHandler_DaemonStart, (ClientData) pipe_fd[0]);
            result = TRUE;
            break;
      }
   }
   else
      fprintf(stderr, "Failed to create pipe to communicate with daemon: %s\n", strerror(errno));

   return result;
}

// ---------------------------------------------------------------------------
// Terminate the daemon by sending a signal
// - the pid is obtained from the pid file (usually /tmp/.vbi_pid#)
// - the function doesn't return until the process is gone
//
bool EpgMain_StopDaemon( void )
{
   bool result = FALSE;
   #ifndef WIN32
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

   #endif
   return result;
}
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
   char str_buf[10];

   if ((sigval == SIGHUP) && (optDaemonMode == FALSE))
   {  // toggle acquisition on/off (unless in daemon mode)
      AddMainIdleEvent(EventHandler_SigHup, NULL, TRUE);
   }
   else
   {
      if (optDaemonMode)
      {
         sprintf(str_buf, "%d", sigval);
         EpgNetIo_Logger(LOG_NOTICE, -1, "terminated by signal ", str_buf, NULL);
      }
      else
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
   EpgAcqCtl_Stop();
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
   HICON hIcon;
   HWND  hWnd;

   hIcon = LoadIcon(hInstance, "NXTVEPG_ICON");
   if(hIcon != NULL)
   {
      sprintf(comm, "wm frame .\n");
      if (Tcl_Eval(interp, comm) == TCL_OK)
      {
         if (sscanf(interp->result, "0x%x", (int *)&hWnd) == 1)
         {
            SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
         }
      }
   }
}
#endif  // ICON_PATCHED_INTO_DLL

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
                   "       -display <display>  \t: X11 display\n"
                   #endif
                   "       -geometry <geometry>\t: window position\n"
                   "       -iconic     \t\t: iconify window\n"
                   "       -rcfile <path>      \t: path and file name of setup file\n"
                   "       -dbdir <path>       \t: directory where to store databases\n"
                   "       -card <digit>       \t: index of TV card for acq (starting at 0)\n"
                   "       -provider <cni>     \t: network id of EPG provider (hex)\n"
                   "       -noacq              \t: disable acquisition\n"
                   #ifdef USE_DAEMON
                   "       -daemon             \t: no GUI; acquisition only\n"
                   "       -nodetach           \t: daemon stays in the foreground\n"
                   "       -acqpassive         \t: force daemon to passive acquisition mode\n"
                   #endif
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
            optNoDetach = TRUE;
            argIdx += 1;
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
               optGuiPipe = strtol(argv[argIdx + 1], &pe, 16);
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
   }
   else
   {
      if (optAcqPassive)
         Usage(argv[0], "-acqpassive", "Only meant for -daemon mode");
   }
}

// ---------------------------------------------------------------------------
// Initialize the Tcl/Tk interpreter
//
static int ui_init( int argc, char **argv )
{
   char *args;

   #ifdef WIN32
   // set up the default locale to be standard "C" locale so parsing is performed correctly
   setlocale(LC_ALL, "C");

   // Increase the application queue size from default value of 8.
   // At the default value, cross application SendMessage of WM_KILLFOCUS
   // will fail because the handler will not be able to do a PostMessage!
   // This is only needed for Windows 3.x, since NT dynamically expands
   // the queue.
   //SetMessageQueue(64);
   #endif

   if (argc >= 1)
   {
      Tcl_FindExecutable(argv[0]);
   }

   interp = Tcl_CreateInterp();

   if (argc > 1)
   {
      args = Tcl_Merge(argc - 1, argv + 1);
      Tcl_SetVar(interp, "argv", args, TCL_GLOBAL_ONLY);
      sprintf(comm, "%d", argc - 1);
      Tcl_SetVar(interp, "argc", comm, TCL_GLOBAL_ONLY);
   }
   Tcl_SetVar(interp, "argv0", argv[0], TCL_GLOBAL_ONLY);

   Tcl_SetVar(interp, "tcl_library", TCL_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "tk_library", TK_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "tcl_interactive", "0", TCL_GLOBAL_ONLY);

   Tcl_Init(interp);
   if (optDaemonMode == FALSE)
   {
      if (Tk_Init(interp) != TCL_OK)
      {
         #ifndef USE_PRECOMPILED_TCL_LIBS
         fprintf(stderr, "%s\n", interp->result);
         exit(1);
         #endif
      }
   }

   #ifdef USE_PRECOMPILED_TCL_LIBS
   if (Tcl_VarEval(interp, tcl_init_scripts, NULL) != TCL_OK)
   {
      debug1("tcl_init_scripts error: %s\n", interp->result);
   }
   #endif

   #if (DEBUG_SWITCH_TCL_BGERR != ON)
   // switch off Tcl/Tk background error reports for release version
   sprintf(comm, "proc bgerror foo {}\n");
   eval_check(interp, comm);
   #endif

   sprintf(comm, "0x%06X", EPG_VERSION_NO);
   Tcl_SetVar(interp, "EPG_VERSION_NO", comm, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "EPG_VERSION", epg_version_str, TCL_GLOBAL_ONLY);

   if (optDaemonMode == FALSE)
   {
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_ptr_up"), ptr_up_bits, ptr_up_width, ptr_up_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_ptr_down"), ptr_down_bits, ptr_down_width, ptr_down_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_pan_updown"), pan_updown_bits, pan_updown_width, pan_updown_height);
      Tk_DefineBitmap(interp, Tk_GetUid("bitmap_qmark"), qmark_bits, qmark_width, qmark_height);
      Tk_DefineBitmap(interp, Tk_GetUid("nxtv_logo"), nxtv_logo_bits, nxtv_logo_width, nxtv_logo_height);

      sprintf(comm, "wm title . {Nextview EPG Decoder}\n"
                    "wm resizable . 0 1\n"
                    "wm iconbitmap . nxtv_logo\n"
                    "wm iconname . {Nextview EPG}\n");
      eval_check(interp, comm);

      if (startIconified)
         eval_check(interp, "wm iconify .");

   }
   eval_check(interp, epgui_tcl_script);

   if (optDaemonMode == FALSE)
      eval_check(interp, "CreateMainWindow; CreateMenubar\n");

   eval_check(interp, help_tcl_script);

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
      if (optNoDetach == FALSE)
      {
         if (fork() > 0)
            exit(0);
         close(0);
         #if DEBUG_SWITCH == OFF
         close(1);
         close(2);
         #endif
         setsid();
      }
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

   // UNIX must fork the VBI slave before GUI startup or the slave will inherit all X11 file handles
   BtDriver_Init();

   // initialize Tcl/Tk interpreter and compile all scripts
   ui_init(argc, argv);
   should_exit = FALSE;
   exitHandler = Tcl_AsyncCreate(AsyncHandler_AppTerminate, NULL);

   // load the user configuration from the rc/ini file
   sprintf(comm, "LoadRcFile {%s} %d", rcfile, (strcmp(defaultRcFile, rcfile) == 0));
   eval_check(interp, comm);

   // pass TV card hardware parameters to the driver
   SetHardwareConfig(interp, videoCardIndex);

   if (optDaemonMode == FALSE)
   {
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
      SetAcquisitionMode();

      // initialize the GUI control modules
      StatsWin_Create();
      TimeScale_Create();
      PiFilter_Create();
      PiOutput_Create();
      PiListBox_Create();
      MenuCmd_Init(pDemoDatabase != NULL);
      #ifndef WIN32
      Xawtv_Init();
      #endif

      // draw the clock and update it every second afterwords
      EventHandler_UpdateClock(NULL);
      // init main window title, PI listbox state and status line
      UiControl_AiStateChange(DB_TARGET_UI);

      // wait until window is open and everthing displayed
      while ( (Tk_GetNumMainWindows() > 0) &&
              Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT) )
         ;

      #if defined(WIN32) && !defined(ICON_PATCHED_INTO_DLL)
      // set app icon in window title bar - note: must be called *after* the window is mapped!
      SetWindowsIcon(hInstance);
      #endif

      if (disableAcq == FALSE)
      {
         AutoStartAcq(interp);
      }

      if (Tk_GetNumMainWindows() > 0)
      {
         // remove expired items from database and listbox every minute
         expirationHandler = Tcl_CreateTimerHandler(1000 * (60 - time(NULL) % 60), EventHandler_TimerDbSetDateTime, NULL);
         #ifndef WIN32
         // poll VPS PIL to follow channel changes made by an external TV app
         vpsPollingHandler = Tcl_CreateTimerHandler(200, EventHandler_TimerVPSPolling, NULL);
         #endif

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
               Tcl_DeleteTimerHandler(clockHandler);
            if (expirationHandler != NULL)
               Tcl_DeleteTimerHandler(expirationHandler);
            if (vpsPollingHandler != NULL)
               Tcl_DeleteTimerHandler(vpsPollingHandler);
            Tcl_AsyncDelete(exitHandler);
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

      if (SetDaemonAcquisitionMode(startUiCni, optAcqPassive))
      {
         if (EpgAcqCtl_Start())
         {
            // start listening for client connections (at least on the named socket in /tmp)
            if (EpgAcqServer_Listen())
            {
               if (optGuiPipe != -1)
               {
                  write(optGuiPipe, "OK", 3);
                  close(optGuiPipe);
               }

               DaemonMainLoop();
            }
         }
         else
            EpgNetIo_Logger(LOG_ERR, -1, "failed to start acquisition", NULL);
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

   EpgAcqCtl_Stop();
   BtDriver_Exit();

   if (optDaemonMode == FALSE)
   {
      EpgContextCtl_Close(pUiDbContext);
      pUiDbContext = NULL;

      PiFilter_Destroy();
      PiListBox_Destroy();
      PiOutput_Destroy();
      #ifndef WIN32
      Xawtv_Destroy();
      #endif
      #ifdef USE_DAEMON
      EpgAcqClient_Destroy();
      #endif
   }

   #if CHK_MALLOC == ON
   DiscardAllMainIdleEvents();
   EpgContextCtl_ClearCache();
   #ifdef WIN32
   xfree(argv);
   #endif
   // check for allocated memory that was not freed
   chk_memleakage();
   #endif

   return 0;
}

