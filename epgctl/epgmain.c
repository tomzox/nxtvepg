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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgmain.c,v 1.34 2000/10/15 18:45:57 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#ifndef WIN32
#include <unistd.h>
#include <sys/param.h>
#include <pwd.h>
#include <signal.h>
#else
#include <windows.h>
#include <io.h>
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

#include "tcl.h"
#include "tk.h"

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbacq.h"
#include "epgctl/epgacqctl.h"
#include "epgui/statswin.h"
#include "epgui/menucmd.h"
#include "epgui/pilistbox.h"
#include "epgui/pifilter.h"
#include "epgui/nxtv_logo.xbm"

#include "epgctl/vbidecode.h"
#include "epgctl/epgmain.h"


extern char epgui_tcl_script[];
extern char help_tcl_script[];

Tcl_Interp *interp;          // Interpreter for application
char comm[1000];             // Command buffer

// command line options
#ifdef WIN32
static const char *rcfile = "NXTVEPG.INI";
       const char *dbdir  = ".";
#else
static const char *rcfile = "~/.nxtvepgrc";
       const char *dbdir  = "/usr/tmp/nxtvdb";
static uchar videoCardPostfix = '\0';
#endif
static bool disableAcq = FALSE;
static bool startIconified = FALSE;
static uint startUiCni = 0;

sint lto = 0;

// Global variable: if TRUE, will exit on the next iteration of the main loop
Tcl_AsyncHandler exitHandler = NULL;
int should_exit;

EPGDB_CONTEXT * pUiDbContext;
EPGDB_CONTEXT * pAcqDbContext;

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
      debug2("Command: %s\nError: %s", cmd, interp->result);
   }
   #endif

   return result;
}

// ---------------------------------------------------------------------------
// process all available lines from VBI
// - if database is unlocked, the lines are immediately processed
//
static void IdleEvent_VbiBufferService( ClientData clientData )
{
   if (pAcqDbContext != NULL)
   {
      if (EpgDbIsLocked(pAcqDbContext) == FALSE)
      {
         EpgAcqCtl_ProcessPackets();
      }
      else if ((long)clientData < 10)
      {  // obviously we are not yet idle -> try again later
         Tcl_DoWhenIdle(IdleEvent_VbiBufferService, (ClientData) ((long)clientData + 1));
      }
      else
         debug0("IdleEvent-VbiBufferService: db still locked after 10 tries - giving up");
   }
}

// ---------------------------------------------------------------------------
// called every minute: remove expired PI from the database and listbox
// 
static void IdleEvent_TimerDbSetDateTime( ClientData clientData )
{
   if (EpgDbIsLocked(pUiDbContext) == FALSE)
   {
      EpgDbSetDateTime(pUiDbContext);
   }
   else if ((long)clientData < 10)
   {  // obviously we are not yet idle -> try again later
      Tcl_DoWhenIdle(IdleEvent_TimerDbSetDateTime, (ClientData) ((long)clientData + 1));
   }
   else
      debug0("IdleEvent-TimerDbSetDateTime: db still locked after 10 tries - giving up");

   if ((pAcqDbContext != NULL) && (pAcqDbContext != pUiDbContext) &&
       (EpgDbIsLocked(pAcqDbContext) == FALSE))
   {  // if acq uses a separate db, update that too. it should never be locked.
      EpgDbSetDateTime(pAcqDbContext);
   }
}

static void EventHandler_TimerDbSetDateTime( ClientData clientData )
{
   // not executed until all current events are processed
   Tcl_DoWhenIdle(IdleEvent_TimerDbSetDateTime, NULL);

   Tcl_CreateTimerHandler(1000 * (60 - time(NULL) % 60), EventHandler_TimerDbSetDateTime, NULL);
}

// ---------------------------------------------------------------------------
// called every second
//
static void EventHandler_UpdateClock( ClientData clientData )
{
   if (should_exit == FALSE)
   {
      //  Update the clock every second with the current time
      sprintf(comm, ".all.shortcuts.clock configure -text [clock format [clock seconds] -format {%%a %%H:%%M:%%S} -gmt 0]\n");
      eval_check(interp, comm);

      // process teletext packets in the buffer
      if (EpgDbAcqCheckForPackets())
      {
         Tcl_DoWhenIdle(IdleEvent_VbiBufferService, NULL);
      }

      // check upon acquisition progress
      EpgAcqCtl_Idle();

      Tcl_CreateTimerHandler(1000, EventHandler_UpdateClock, NULL);
   }
}

// ---------------------------------------------------------------------------
// called by interrupt handlers when the application should exit
//
static int AsyncHandler_AppTerminate( ClientData clientData, Tcl_Interp *interp, int code)
{
   // do nothing - the only purpose was to wake up the event handler,
   // so that we can leave the main loop after this NOP was processed
   return code;
}

// ---------------------------------------------------------------------------
// graceful exit upon signals
//
#ifndef WIN32
static void signal_handler(int sigval)
{
   if (sigval != SIGINT)
   {
      printf("Caught signal %d\n", sigval);
   }
   DBGONLY(fflush(stdout));

   if (exitHandler != NULL)
   {  // trigger an event, so that the Tcl/Tk event handler immediately wakes up
      Tcl_AsyncMark(exitHandler);
   }
   should_exit = TRUE;

   signal(sigval, signal_handler);
}
#endif

// ---------------------------------------------------------------------------
// determine local time offset to UTC
//
static void InitLTO( void )
{
   tzset();
   lto = 60*60 * daylight - timezone;

   //printf("LTO = %d min, %s/%s, off=%ld, daylight=%d\n", lto/60, tzname[0], tzname[1], timezone/60, daylight);
}

#ifndef WIN32
// ---------------------------------------------------------------------------
// Print Usage and exit
//
static void Usage( const char *argv0, const char *argvn, const char * reason )
{
   fprintf(stderr, "%s: %s: %s\n"
                   "Usage: -help                : this message\n"
                   "       -display <display>   : X11 display\n"
                   "       -geometry <geometry> : window geometry\n"
                   "       -iconic              : iconify window\n"
                   "       -rcfile <path>       : path and file name of setup file\n"
                   "       -dbdir <path>        : directory where to store databases\n"
                   "       -card <digit>        : index of TV card (1-4)\n"
                   "       -provider <cni>      : network id of EPG provider (hex)\n"
                   "       -noacq               : disable acquisition\n",
                   argv0, reason, argvn);
   exit(1);
}

// ---------------------------------------------------------------------------
// Parse command line options
//
static void ParseArgv( int argc, char * argv[] )
{
   struct stat st;
   int argIdx = 1;

   while (argIdx < argc)
   {
      if (argv[argIdx][0] == '-')
      {
         if (!strcmp(argv[argIdx], "-help"))
         {
            Usage(argv[0], "", "the following command line options are available");
         }
         else if (!strcmp(argv[argIdx], "-noacq"))
         {  // do not enable acquisition
            disableAcq = TRUE;
            argIdx += 1;
         }
         else if (!strcmp(argv[argIdx], "-rcfile"))
         {
            if (argIdx + 1 < argc)
            {  // read file name of rc/ini file: warn if file does not exist
               rcfile = argv[argIdx + 1];
               if (stat(rcfile, &st))
                  perror("Warning -rcfile");
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
               videoCardPostfix = '0' + cardIdx;
               argIdx += 2;
            }
            else
               Usage(argv[0], argv[argIdx], "missing card index after");
         }
         else if (!strcmp(argv[argIdx], "-provider"))
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
         else if ( !strcmp(argv[argIdx], "-iconic") )
         {  // start with iconified main window
            startIconified = TRUE;
            argIdx += 1;
         }
         else if ( !strcmp(argv[argIdx], "-display") ||
                   !strcmp(argv[argIdx], "-geometry") ||
                   !strcmp(argv[argIdx], "-name") )
         {  // ignore arguments that are handled by Tk
            if (argIdx + 1 >= argc)
               Usage(argv[0], argv[argIdx], "missing argument after");
            argIdx += 2;
         }
         else
            Usage(argv[0], argv[argIdx], "unknown option");
      }
      else
         Usage(argv[0], argv[argIdx], "Unexpected argument");
   }
}
#endif  //WIN32

// ---------------------------------------------------------------------------
// Initialize the Tcl/Tk interpreter
//
#if !defined(TCL_LIBRARY_PATH) || !defined(TK_LIBRARY_PATH)
#include "epgui/tcl_libs.c"
#endif
static int ui_init( int argc, char **argv )
{
   char *args, buffer[128];

   if (argc >= 1)
   {
      Tcl_FindExecutable(argv[0]);
   }

   interp = Tcl_CreateInterp();

   if (argc > 1)
   {
      args = Tcl_Merge(argc - 1, argv + 1);
      Tcl_SetVar(interp, "argv", args, TCL_GLOBAL_ONLY);
      sprintf(buffer, "%d", argc - 1);
      Tcl_SetVar(interp, "argc", buffer, TCL_GLOBAL_ONLY);
      Tcl_SetVar(interp, "argv0", argv[0], TCL_GLOBAL_ONLY);
   }
   #if defined(TCL_LIBRARY_PATH) && defined(TK_LIBRARY_PATH)
   Tcl_SetVar(interp, "tcl_library", TCL_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "tk_library", TK_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   #endif
   Tcl_SetVar(interp, "tcl_interactive", "0", TCL_GLOBAL_ONLY);

   Tcl_Init(interp);
   if (Tk_Init(interp) != TCL_OK)
   {
      fprintf(stderr, "%s\n", interp->result);
      exit(1);
   }

   #if !defined(TCL_LIBRARY_PATH) || !defined(TK_LIBRARY_PATH)
   if (Tcl_VarEval(interp, TCL_LIBS, NULL) != TCL_OK)
   {
      debug1("TCL_LIBS error: %s\n", interp->result);
   }
   #endif

   Tk_DefineBitmap(interp, Tk_GetUid("nxtv_logo"), nxtv_logo_bits, nxtv_logo_width, nxtv_logo_height);

   sprintf(comm, "wm title . {Nextview EPG Decoder}\n"
                 "wm resizable . 0 0\n"
                 "wm iconbitmap . nxtv_logo\n"
                 "wm iconname . {Nextview EPG}\n");
   eval_check(interp, comm);

   if (startIconified)
      eval_check(interp, "wm iconify .");

   eval_check(interp, epgui_tcl_script);
   eval_check(interp, help_tcl_script);

   Tcl_ResetResult(interp);
   return (TRUE);
}


// ---------------------------------------------------------------------------
// entry point
//
#ifdef WIN32
int NextviewMain( void )
#else
int main( int argc, char *argv[] )
#endif
{
   Tcl_TimerToken clockHandler, expirationHandler;

   // mark Tcl/Tk interpreter as uninitialized
   interp = NULL;
   // set db states to not initialized
   pUiDbContext = NULL;
   pAcqDbContext = NULL;

   InitLTO();

   #ifndef WIN32
   ParseArgv(argc, argv);
   VbiDecodeInit(videoCardPostfix);

   ui_init(argc, argv);

   should_exit = FALSE;
   exitHandler = Tcl_AsyncCreate(AsyncHandler_AppTerminate, NULL);
   signal(SIGINT, signal_handler);
   signal(SIGTERM, signal_handler);
   #else
   ui_init(0, NULL);
   #endif

   sprintf(comm, "LoadRcFile %s\n", rcfile);
   eval_check(interp, comm);

   // set up the directory for the databases
   if (dbdir != NULL)
   {
      struct stat st;
      if (stat(dbdir, &st) != 0)
      {
         if ((errno != ENOENT) || (mkdir(dbdir, 0777) != 0) || (stat(dbdir, &st) != 0))
         {
            fprintf(stderr, "cannot create dbdir %s: %s\n", dbdir, strerror(errno));
            exit(1);
         }
      }
      // set permissions of database directory: world-r/w-access & sticky-bit
      if ((st.st_mode != (0777 | S_ISVTX)) && (chmod(dbdir, 0777 | S_ISVTX) != 0))
      {
         fprintf(stderr, "cannot set permissions for dbdir %s: %s\n", dbdir, strerror(errno));
         exit(1);
      }
   }

   if (startUiCni == 0)
   {
      uint iniCni;
      if ( (eval_check(interp, "lindex $prov_selection 0") == TCL_OK) &&
           (sscanf(interp->result, "0x%04X", &iniCni) == 1) )
      {
         startUiCni = iniCni;
         // update rc/ini file with new CNI order
         sprintf(comm, "UpdateProvSelection 0x%04X\n", iniCni);
         eval_check(interp, comm);
      }
   }
   if ( (EpgAcqCtl_OpenDb(DB_TARGET_UI, startUiCni) == FALSE) && (startUiCni != 0) )
   {
      sprintf(comm, "tk_messageBox -type ok -icon error "
                    "-message {Failed to open the database of the requested provider 0x%04X. "
                    "Please use the Configure menu to choose a different one.}\n", startUiCni);
      eval_check(interp, comm);
   }

   // initialize the GUI control modules
   StatsWin_Create();
   PiFilter_Create();
   PiListBox_Create();
   MenuCmd_Init();

   // wait until window is open
   while ( (Tk_GetNumMainWindows() > 0) &&
           Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT) )
      ;

   if (disableAcq == FALSE)
   {
      EpgAcqCtl_Start();
      PiListBox_UpdateState();
   }

   if (Tk_GetNumMainWindows() > 0)
   {
      // update the clock every second
      clockHandler = Tcl_CreateTimerHandler(1, EventHandler_UpdateClock, NULL);

      // remove expired items from database and listbox every minute
      expirationHandler = Tcl_CreateTimerHandler(1000 * (60 - time(NULL) % 60), EventHandler_TimerDbSetDateTime, NULL);

      while (Tk_GetNumMainWindows() > 0)
      {
         Tcl_DoOneEvent(0);

         if (should_exit)
         {
            break;
         }
      }

      if (Tk_GetNumMainWindows() > 0)
      {
         // remove handlers to prevent invokation after death of main window
         Tcl_DeleteTimerHandler(clockHandler);
         Tcl_DeleteTimerHandler(expirationHandler);
         Tcl_AsyncDelete(exitHandler);
         // execute pending updates and close main window
         sprintf(comm, "update; destroy .");
         eval_check(interp, comm);
      }
   }
   else
      debug0("could not open the main window - exiting.");

   EpgAcqCtl_Stop();
   #ifndef WIN32
   VbiDecodeExit();
   #endif
   EpgAcqCtl_CloseDb(DB_TARGET_UI);
   PiFilter_Destroy();

   #if CHK_MALLOC == ON
   // check for allocated memory that was not freed
   chk_memleakage();
   #endif

   return 0;
}

