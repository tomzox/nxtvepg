/*
 *  TV application interaction simulator for nxtvepg
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
 *    This module is the main module of the small standalone tool
 *    "tvsim.exe".  It's a demonstration of how nxtvepg can interact
 *    with TV applications.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: tvsim_main.c,v 1.3 2002/05/04 18:22:18 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_TVSIM
//#define DPRINTF_OFF

#include <windows.h>
#include <locale.h>

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/btdrv.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbacq.h"
#include "epgui/wintvcfg.h"
#include "epgui/pdc_themes.h"
#include "tvsim/winshmclnt.h"


#ifndef USE_PRECOMPILED_TCL_LIBS
# if !defined(TCL_LIBRARY_PATH) || !defined(TK_LIBRARY_PATH)
#  error "Must define both TK_ and TCL_LIBRARY_PATH"
# endif
#else
# define TCL_LIBRARY_PATH  "."
# define TK_LIBRARY_PATH   "."
#endif

#define TVSIM_VERSION_STR "1.7"

static char *tvsim_version_str = TVSIM_VERSION_STR;
char tvsim_rcs_id_str[] = "$Id";

extern char gui_tcl_script[];
extern char tcl_init_scripts[];

Tcl_Interp *interp;          // Interpreter for application
char comm[1000];             // Command buffer

static Tcl_AsyncHandler asyncThreadHandler = NULL;
static Tcl_TimerToken   popDownEvent       = NULL;
static bool             haveIdleHandler    = FALSE;

// command line options
static const char * const defaultRcFile = "nxtvepg.ini";
static const char * rcfile = NULL;
static int  videoCardIndex = -1;
static bool startIconified = FALSE;

// used by wintvcfg.c; declare here to avoid compiler warning
int eval_check(Tcl_Interp *interp, char *cmd);

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
// Open messagebox with system error string (e.g. "invalid path")
//
#ifdef COMPILE_UNUSED_CODE
static void DisplaySystemErrorMsg( char * message, DWORD code )
{
   char * buf;

   buf = malloc(strlen(message) + 300);
   if (buf != NULL)
   {
      strcpy(buf, message);
      strcat(buf, ": ");

      // translate the error code into a human readable text
      FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, code, LANG_USER_DEFAULT,
                    buf + strlen(buf), 300 - strlen(buf) - 1, NULL);
      // open a small dialog window with the error message and an OK button
      MessageBox(NULL, buf, "XTerm Launch", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);

      free(buf);
   }
}
#endif

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
// Print Usage and exit
//
static void Usage( const char *argv0, const char *argvn, const char * reason )
{
   sprintf(comm, "%s: %s: %s\n"
                   "Usage: %s [options] [database]\n"
                   "       -help       \t\t: this message\n"
                   "       -geometry <geometry>\t: window position\n"
                   "       -iconic     \t\t: iconify window\n"
                   "       -rcfile <path>      \t: path and file name of setup file\n"
                   "       -card <digit>       \t: index of TV card for acq (starting at 0)\n",
                   argv0, reason, argvn, argv0);
   MessageBox(NULL, comm, "TV interaction simulator usage", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);

   exit(1);
}

// ---------------------------------------------------------------------------
// Parse command line options
//
static void ParseArgv( int argc, char * argv[] )
{
   int argIdx = 1;

   rcfile = defaultRcFile;

   while (argIdx < argc)
   {
      if (argv[argIdx][0] == '-')
      {
         if (!strcmp(argv[argIdx], "-help"))
         {
            char versbuf[50];
            sprintf(versbuf, "(version %s)", tvsim_version_str);
            Usage(argv[0], versbuf, "the following command line options are available");
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
         else
            Usage(argv[0], argv[argIdx], "unknown option");
      }
      else
         Usage(argv[0], argv[argIdx], "Too many arguments");
   }
}

// ----------------------------------------------------------------------------
// Set the hardware config params
// - called at startup
// - the card index can also be set via command line and is passed here
//   from main; a value of -1 means don't care
//
static int SetHardwareConfig( Tcl_Interp *interp, int newCardIndex )
{
   char **pParamsArgv;
   char * pTmpStr;
   int idxCount, input, tuner, pll, prio, cardidx, ftable;
   int result;

   pTmpStr = Tcl_GetVar(interp, "hwcfg", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr == NULL)
      pTmpStr = Tcl_GetVar(interp, "hwcfg_default", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      result = Tcl_SplitList(interp, pTmpStr, &idxCount, &pParamsArgv);
      if (result == TCL_OK)
      {
         if (idxCount == 6)
         {
            if ( (Tcl_GetInt(interp, pParamsArgv[0], &input) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[1], &tuner) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[2], &pll) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[3], &prio) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[4], &cardidx) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[5], &ftable) == TCL_OK) )
            {
               // override TV card index with the command line switch -card
               if (newCardIndex >= 0)
                  cardidx = newCardIndex;

               // pass the hardware config params to the driver
               BtDriver_SetInputSource(input, FALSE, NULL);
               BtDriver_Configure(cardidx, tuner, pll, prio);
            }
            else
               result = TCL_ERROR;
         }
         else
         {
            Tcl_SetResult(interp, "SetHardwareConfig: must get 6 params", TCL_STATIC);
            result = TCL_ERROR;
         }
      }
   }
   else
      result = TCL_ERROR;

   return result;
}

// ---------------------------------------------------------------------------
// Dummy functions to satisfy the BT driver and EPG decoder module
//
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"
bool WintvSharedMem_ReqTvCardIdx( uint cardIdx ) { return TRUE; }
void WintvSharedMem_FreeTvCard( void ) {}
bool WintvSharedMem_SetInputSrc( uint inputIdx ) { return FALSE; }
bool WintvSharedMem_SetTunerFreq( uint freq ) { return FALSE; }
uint WintvSharedMem_GetTunerFreq( void ) { return 0; }
volatile EPGACQ_BUF * WintvSharedMem_GetVbiBuf( void ) { return NULL; }

#include "epgdb/epgblock.h"
void EpgBlockSetAlphabets( const AI_BLOCK *pAiBlock ) {}

#include "epgdb/epgstream.h"
void EpgStreamInit( EPGDB_QUEUE *pDbQueue, bool bWaitForBiAi, uint appId ) {}
void EpgStreamClear( void ) {}
bool EpgStreamNewPage( uint sub ) { return FALSE; }
void EpgStreamDecodePacket( uchar packNo, const uchar * dat ) {}
void EpgStreamSyntaxScanInit( void ) {}
void EpgStreamSyntaxScanHeader( uint page, uint sub ) {};
bool EpgStreamSyntaxScanPacket( uchar mag, uchar packNo, const uchar * dat ) { return FALSE; }

// ----------------------------------------------------------------------------
// Handle EPG events
// - executed inside the main thread, but triggered by the msg receptor thread
// - the driver invokes callbacks into the tvsim module to trigger GUI action,
//   e.g. to display incoming program title info
//
static void TvSimu_IdleHandler( ClientData clientData )
{
   haveIdleHandler = FALSE;
   WinSharedMemClient_HandleEpgEvent();
}

// ----------------------------------------------------------------------------
// 2nd stage of EPG event handling: delay handling until GUI is idle
//
static int TvSimu_AsyncThreadHandler( ClientData clientData, Tcl_Interp *interp, int code )
{
   // if an idle handler is already installed, nothing needs to be done
   // because the handler processes all incoming messages (not only one)
   if (haveIdleHandler == FALSE)
   {
      haveIdleHandler = TRUE;
      Tcl_DoWhenIdle(TvSimu_IdleHandler, NULL);
   }

   return code;
}

// ---------------------------------------------------------------------------
// Called by TV event receptor thread
// - trigger an event, so that the Tcl/Tk event handler immediately wakes up;
//   no message processing is done here because we're running in a separate
//   thread here!  We'd have to do some serious mutual exclusion efforts.
// - note that there are 2 stages until the message is actually handled:
//   1. install an async. event at the top of the event queue; it'll get executed
//      A.S.A.P., i.e. next time the Tcl event loop is entered
//   2. in the async handler an idle handler is installed, which delays the
//      actual handling until the current actions are completed
//   This is neccessary to avoid that we draw something in the EPG message
//   handler, which is then overwritten by an action that was already scheduled
//   earlier (i.e. EPG info is cleared again by a timer event in the queue)
//
static void TvSimuMsg_EpgEvent( void )
{
   if (asyncThreadHandler != NULL)
   {
      // install event at top of Tcl event loop
      Tcl_AsyncMark(asyncThreadHandler);
   }
}

// ----------------------------------------------------------------------------
// Shm callback: EPG application has started or terminated
//
static void TvSimuMsg_Attach( bool attach )
{
   // update the connection indicator in the GUI
   sprintf(comm, "ConnectEpg %d\n", attach);
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Shm callback: EPG peer requested a new input source or tuner frequency
// - this callback is only invoked if the TV app granted the tuner to EPG before
//   (but feel free to check again here if the tuner is really free)
//
static void TvSimuMsg_ReqTuner( uint inputSrc, uint freq )
{
   bool isTuner;

   // set the TV input source: tuner, composite, S-video, ...
   BtDriver_SetInputSource(inputSrc, TRUE, &isTuner);
   if (isTuner)
   {  // TV tuner is input source -> also set tuner frequency
      BtDriver_TuneChannel(freq, FALSE);
   }
   else
      BtDriver_CloseDevice();

   // inform EPG app that the frequency has been set (note: this function is a
   // variant of _SetStation which does not request EPG info for the new channel)
   WinSharedMemClient_SetInputFreq(inputSrc, freq);
}

// ----------------------------------------------------------------------------
// Shm callback: EPG peer sent command vector
// - a command vector is a concatenation of null terminated strings
//   the number of concatenated strings is passed as first argument
// - the first string in the vectors holds the command name;
//   the following ones optional arguments (depending on the command)
//
static void TvSimuMsg_HandleEpgCmd( uint argc, const char * pArgStr )
{
   const char * pArg2;

   if (argc == 2)
   {
      // XXX TODO: consistancy checking of argv string (i.e. max length)
      pArg2 = pArgStr + strlen(pArgStr) + 1;

      if (strcmp(pArgStr, "setstation") == 0)
      {
         // check for special modes: prev/next (+/-) and back (toggle)
         if (strcmp(pArg2, "back") == 0)
            sprintf(comm, "TuneChanBack\n");
         else if (strcmp(pArg2, "next") == 0)
            sprintf(comm, "TuneChanNext\n");
         else if (strcmp(pArg2, "prev") == 0)
            sprintf(comm, "TuneChanPrev\n");
         else
         {  // not a special mode -> search for the name in the channel table

            // Note: when the search fails, you should do a sub-string search too.
            // This is required for multi-network channels like "Arte / KiKa".
            // In nxtvepg these are separate, i.e. the TV app will receive setstation
            // requests for either "Arte" or "KiKa" - the TV app should allow both and
            // switch to the channel named "Arte / KiKa".  See gui.tcl for an example
            // how to implement this.

            sprintf(comm, "TuneChanByName {%s}\n", pArg2);
         }

         eval_check(interp, comm);
      }
      else if (strcmp(pArgStr, "capture") == 0)
      {
         // when capturing is switched off, grant tuner to EPG
         sprintf(comm, "set grant_tuner %d; GrantTuner\n", (strcmp(pArg2, "off") == 0));
         eval_check(interp, comm);
      }
      else if (strcmp(pArgStr, "volume") == 0)
      {
         // command ignored in simulation because audio is not supported
      }
   }
}

// ----------------------------------------------------------------------------
// Timout handler: clear title display max. X msecs after channel change
// - only to be fail-safe, in case EPG app answers to slowly
//
static void TvSimu_TitleChangeTimer( ClientData clientData )
{
   popDownEvent = NULL;

   sprintf(comm, "set program_title {}; set program_times {}; set program_theme {}\n");
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// EPG peer replied with title information
// - display the information in the GUI
//
static void TvSimuMsg_UpdateProgInfo( const char * pTitle, time_t start, time_t stop, uchar themeCount, const uchar * pThemes )
{
   const char * pThemeStr;
   uint   themeIdx;
   char   str_buf[50];

   // remove the pop-down timer (which would clear the EPG display if no answer arrives in time)
   if (popDownEvent != NULL)
      Tcl_DeleteTimerHandler(popDownEvent);
   popDownEvent = NULL;

   if ( (pTitle[0] != 0) && (start != stop) )
   {
      // Process start and stop time
      // (times are given in UNIX format, i.e. seconds since 1.1.1970 0:00am UTC)
      strftime(str_buf, sizeof(str_buf), "%a %d.%m %H:%M - ", localtime(&start));
      strftime(str_buf + strlen(str_buf), sizeof(str_buf) - strlen(str_buf), "%H:%M", localtime(&stop));

      // Process PDC theme array: convert theme code to text
      // Note: up to 7 theme codes can be supplied (e.g. "movie general" plus "comedy")
      //       you have to decide yourself how to display them; you might use icons for the
      //       main themes (e.g. movie, news, talk show) and add text for sub-categories.
      // Use loop to search for the first valid theme code (simplest possible handling)
      pThemeStr = "";
      for (themeIdx = 0; themeIdx < themeCount; themeIdx++)
      {
         if (pThemes[themeIdx] >= 128)
         {  // PDC codes 0x80..0xff identify series (most EPG providers just use 0x80 for
            // all series, but they could use different codes for every series of a network)
            pThemeStr = pdc_series;
            break;
         }
         else if (pdc_themes[pThemes[themeIdx]] != NULL)
         {  // this is a known PDC series code
            pThemeStr = pdc_themes[pThemes[themeIdx]];
            break;
         }
         // else: unknown series code -> keep searching
      }

      // Finally display program title, start/stop times and theme text
      // Note: the Tcl variables are bound to the "entry" widget
      //       by writing to the variables the widget is automatically updated
      sprintf(comm, "set program_title {%s}; set program_times {%s}; set program_theme {%s}\n", pTitle, str_buf, pThemeStr);
      eval_check(interp, comm);
   }
   else
   {  // empty string transmitted (EPG has no info for the current channel) -> clear display
      sprintf(comm, "set program_title {}; set program_times {}; set program_theme {}\n");
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// Structure which is passed to the shm client init function
//
static const WINSHMCLNT_TVAPP_INFO tvSimuInfo =
{
   "TV application simulator " TVSIM_VERSION_STR,
   TVAPP_FEAT_ALL_000701,

   TvSimuMsg_EpgEvent,
   TvSimuMsg_UpdateProgInfo,
   TvSimuMsg_HandleEpgCmd,
   TvSimuMsg_ReqTuner,
   TvSimuMsg_Attach
};

// ----------------------------------------------------------------------------
// Tcl/Tk callback for toggle of "Grant tuner to EPG" button
//
static int TclCb_GrantTuner( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GrantTuner <bool>";
   int    doGrant;
   int    result;

   if (argc != 2)
   {  // unexpected parameter count
      Tcl_SetResult(interp, (char *) pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = Tcl_GetInt(interp, argv[1], &doGrant);
      if (result == TCL_OK)
      {
         WinSharedMemClient_GrantTuner(doGrant);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Tcl/Tk callback function for channel selection
//
static int TclCb_TuneChan( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "C_TuneChan <idx> <name>";
   ulong * pFreqTab;
   uint    freqCount;
   int     freqIdx;
   int     result;

   if (argc != 3)
   {  // unexpected parameter count
      Tcl_SetResult(interp, (char *) pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = Tcl_GetInt(interp, argv[1], &freqIdx);
      if (result == TCL_OK)
      {
         if ( WintvCfg_GetFreqTab(interp, &pFreqTab, &freqCount) &&
              (pFreqTab != NULL) && (freqCount > 0))
         {
            if ((freqIdx < freqCount) && (freqIdx >= 0))
            {
               // Change the tuner frequency via the BT8x8 driver
               BtDriver_TuneChannel(pFreqTab[freqIdx], FALSE);

               // Request EPG info & update frequency info
               // - input source is hard-wired to TV tuner in this simulation
               // - channel ID is hard-wired to 0 too (see comments in the winshmclnt.c)
               WinSharedMemClient_SetStation(argv[2], 0, 0, pFreqTab[freqIdx]);

               // Install a timer handler to clear the title display in case EPG app fails to answer
               popDownEvent = Tcl_CreateTimerHandler(420, TvSimu_TitleChangeTimer, NULL);
            }
            else
            {
               Tcl_SetResult(interp, "C_TuneChan: invalid channel index", TCL_STATIC);
               result = TCL_ERROR;
            }
            xfree(pFreqTab);
         }
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Initialize the Tcl/Tk interpreter and load our scripts
//
static int ui_init( int argc, char **argv )
{
   char * args;

   // set up the default locale to be standard "C" locale so parsing is performed correctly
   setlocale(LC_ALL, "C");

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
   if (Tk_Init(interp) != TCL_OK)
   {
      #ifndef USE_PRECOMPILED_TCL_LIBS
      fprintf(stderr, "%s\n", interp->result);
      exit(1);
      #endif
   }

   #ifdef USE_PRECOMPILED_TCL_LIBS
   if (Tcl_VarEval(interp, tcl_init_scripts, NULL) != TCL_OK)
   {
   }
   #endif

   #ifdef DISABLE_TCL_BGERR
   // switch off Tcl/Tk background error reports for release version
   sprintf(comm, "proc bgerror foo {}\n");
   eval_check(interp, comm);
   #endif

   // create an asynchronous event source that allows to receive triggers from the EPG message receptor thread
   asyncThreadHandler = Tcl_AsyncCreate(TvSimu_AsyncThreadHandler, NULL);

   Tcl_CreateCommand(interp, "C_TuneChan", TclCb_TuneChan, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_GrantTuner", TclCb_GrantTuner, (ClientData) NULL, NULL);

   eval_check(interp, gui_tcl_script);

   sprintf(comm, "LoadRcFile {%s}", rcfile);
   eval_check(interp, comm);

   Tcl_ResetResult(interp);
   return (TRUE);
}

// ---------------------------------------------------------------------------
// entry point
//
int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
   int argc;
   char ** argv;

   // mark Tcl/Tk interpreter as uninitialized
   interp = NULL;

   // initialize Tcl/Tk interpreter and compile all scripts
   SetArgv(&argc, &argv);
   ParseArgv(argc, argv);
   ui_init(argc, argv);
   WintvCfg_Init(FALSE);

   BtDriver_Init();
   if (WinSharedMemClient_Init(&tvSimuInfo))
   {
      // set up callback to catch shutdown messages (requires tk83.dll patch!)
      Tk_RegisterMainDestructionHandler(WinApiDestructionHandler);
      #ifndef __MINGW32__
      __try {
      #else
      SetUnhandledExceptionFilter(WinApiExceptionHandler);
      #endif

      // pass bt8x8 driver parameters to the driver
      SetHardwareConfig(interp, videoCardIndex);

      // fill channel listbox with names from TV app channel table
      // a warning is issued if the table is empty (used during startup)
      sprintf(comm, "LoadChanTable");
      eval_check(interp, comm);

      if (BtDriver_StartAcq())
      {
         // wait until window is open and everything displayed
         while ( (Tk_GetNumMainWindows() > 0) &&
                 Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT) )
            ;

         // change window title - cannot be set before the window is mapped
         eval_check(interp, "wm title . {TV app simulator}\n");
         sprintf(comm, "wm minsize . [winfo reqwidth .] [winfo reqheight .]\n");
         eval_check(interp, comm);
         if (startIconified)
            eval_check(interp, "wm iconify .");

         // process GUI events & callbacks until the main window is closed
         while (Tk_GetNumMainWindows() > 0)
         {
            Tcl_DoOneEvent(TCL_ALL_EVENTS);
         }

         BtDriver_StopAcq();
      }
      else
         debug0("Fatal: failed to start acq - quitting now");

      #if defined(WIN32) && !defined(__MINGW32__)
      }
      __except (EXCEPTION_EXECUTE_HANDLER)
      {  // caught a fatal exception -> stop the driver to prevent system crash ("blue screen")
         debug1("FATAL exception caught: %d", GetExceptionCode());
         // skip EpgAcqCtl_Stop() because it tries to dump the db - do as little as possible
         BtDriver_Exit();
         ExitProcess(-1);
      }
      #endif

      BtDriver_Exit();
      WinSharedMemClient_Exit();
   }
   else
      debug0("Fatal: failed to set up IPC resources");

   WintvCfg_Destroy();
   exit(0);
   return(0);
}

