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
 *  $Id: tvsim_main.c,v 1.16 2002/11/19 20:58:21 tom Exp tom $
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
#include "epgvbi/winshm.h"
#include "epgvbi/ttxdecode.h"
#include "epgdb/epgblock.h"
#include "epgui/wintvcfg.h"
#include "epgui/pdc_themes.h"
#include "tvsim/winshmclnt.h"
#include "tvsim/tvsim_gui.h"
#include "tvsim/tvsim_version.h"


// prior to 8.4 there's a SEGV when evaluating const scripts (Tcl tries to modify the string)
#if (TCL_MAJOR_VERSION > 8) || ((TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4))
# define TCL_EVAL_CONST(INTERP, SCRIPT) Tcl_EvalEx(INTERP, SCRIPT, -1, TCL_EVAL_GLOBAL)
#else
# define TCL_EVAL_CONST(INTERP, SCRIPT) Tcl_VarEval(INTERP, (char *) SCRIPT, NULL)
#endif

#ifndef USE_PRECOMPILED_TCL_LIBS
# if !defined(TCL_LIBRARY_PATH) || !defined(TK_LIBRARY_PATH)
#  error "Must define both TK_ and TCL_LIBRARY_PATH"
# endif
#else
# define TCL_LIBRARY_PATH  "."
# define TK_LIBRARY_PATH   "."
# include "epgtcl/tcl_libs.h"
# include "epgtcl/tk_libs.h"
#endif

char tvsim_rcs_id_str[] = TVSIM_VERSION_RCS_ID;

Tcl_Interp *interp;          // Interpreter for application
#define TCL_COMM_BUF_SIZE  1000
// add one extra byte at the end of the comm buffer for overflow detection
char comm[TCL_COMM_BUF_SIZE + 1];

static Tcl_AsyncHandler asyncThreadHandler = NULL;
static Tcl_TimerToken   popDownEvent       = NULL;
static bool             haveIdleHandler    = FALSE;

// Default TV card index - identifies which TV card is used by the simulator
// (note that in contrary to the other TV card parameters this value is not
// taken from the nxtvepg rc/ini file; can be overriden with -card option)
#define TVSIM_CARD_IDX   0
// input source index of the TV tuner with btdrv4win.c
#define TVSIM_INPUT_IDX  0

// select English language for PDC theme output
#define TVSIM_PDC_THEME_LANGUAGE  0

// command line options
static const char * const defaultRcFile = "nxtvepg.ini";
static const char * rcfile = NULL;
static uint videoCardIndex = TVSIM_CARD_IDX;
static bool startIconified = FALSE;

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
            sprintf(versbuf, "(version %s)", TVSIM_VERSION_STR);
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
               videoCardIndex = (uint) cardIdx;
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
// Set the Bt8x8 driver config params
// - the parameters must be loaded before from the nxtvepg rc/ini file,
//   except for the TV card index, which is set by a command line switch only
//
static void SetHardwareConfig( Tcl_Interp *interp, uint cardIdx )
{
   CONST84 char **pParamsArgv;
   const char * pTmpStr;
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
                 (Tcl_GetInt(interp, pParamsArgv[4], &cardidx) == TCL_OK) &&  // unused
                 (Tcl_GetInt(interp, pParamsArgv[5], &ftable) == TCL_OK) )    // unused
            {
               // pass the hardware config params to the driver
               BtDriver_Configure(cardIdx, tuner, pll, prio);
            }
            else
               debug1("Set-HardwareConfig: parse error in one of the integers in Tcl list hwcfg='%s'", pTmpStr);
         }
         else
            debug2("Set-HardwareConfig: %d elements in Tcl list hwcfg='%s' (expected 6)", idxCount, pTmpStr);
      }
      else
         debug1("Set-HardwareConfig: failed to split Tcl list hwcfg='%s'", pTmpStr);
   }
   else
      debug0("Set-HardwareConfig: TV card config var hwcfg and hwcfg_default undefined'");
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
// Shm message handler: Failed to attach to an EPG application
// - note: this handler will be called every time an attach is attempted and
//   fails, which might be several times a second if the EPG app with an
//   incompatible version is generating multiple events
// - for this reason this handler could supress multiple popups, e.g. by
//   including a "don't show this error any more" checkbutton
//
static void TvSimuMsg_BackgroundError( void )
{
   const char * pShmErrMsg;
   char * pErrBuf;

   pShmErrMsg = WinSharedMemClient_GetErrorMsg();
   if (pShmErrMsg != NULL)
   {
      pErrBuf = xmalloc(strlen(pShmErrMsg) + 100);

      sprintf(pErrBuf, "tk_messageBox -type ok -icon error -message {%s}", pShmErrMsg);
      eval_check(interp, pErrBuf);

      xfree((void *) pErrBuf);
      xfree((void *) pShmErrMsg);
   }
}

// ----------------------------------------------------------------------------
// Shm message handler: EPG peer requested a new input source or tuner frequency
// - this message handler is only invoked if the TV app granted the tuner to EPG before
//   (but feel free to check again here if the tuner is really free)
//
static void TvSimuMsg_ReqTuner( void )
{
   uint  inputSrc;
   uint  freq;
   bool  dummy;

   if (WinSharedMemClient_GetInpFreq(&inputSrc, &freq))
   {
      // set the TV input source (tuner, composite, S-video) and frequency
      BtDriver_TuneChannel(inputSrc, freq, FALSE, &dummy);

      // inform EPG app that the frequency has been set (note: this function is a
      // variant of _SetStation which does not request EPG info for the new channel)
      WinSharedMemClient_SetInputFreq(inputSrc, freq);
   }
}

// ----------------------------------------------------------------------------
// Shm message handler: EPG peer sent command vector
// - a command vector is a concatenation of null terminated strings
//   the number of concatenated strings is passed as first argument
// - the first string in the vectors holds the command name;
//   the following ones optional arguments (depending on the command)
//
static void TvSimuMsg_HandleEpgCmd( void )
{
   uint argc;
   char argStr[EPG_CMD_MAX_LEN];
   char * pArg2;

   if (WinSharedMemClient_GetCmdArgv(&argc, argStr, sizeof(argStr)))
   {
      if (argc >= 1)
      {
         // XXX TODO: consistancy checking of argv string (i.e. max length)
         pArg2 = argStr + strlen(argStr) + 1;

         if (strcmp(argStr, "setstation") == 0)
         {
            if (argc == 2)
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
         }
         else if (strcmp(argStr, "capture") == 0)
         {
            // when capturing is switched off, grant tuner to EPG
            if (argc == 2)
            {
               sprintf(comm, "set grant_tuner %d; GrantTuner\n", (strcmp(pArg2, "off") == 0));
               eval_check(interp, comm);
            }
         }
         else if (strcmp(argStr, "volume") == 0)
         {
            // command ignored in simulation because audio is not supported
         }
         else if (strcmp(argStr, "record") == 0)
         {
            argc -= 1;
            while (argc)
            {
               dprintf1("RECORD ARG: %s", pArg2);
               pArg2 += strlen(pArg2) + 1;
               argc -= 1;
            }
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Timout handler: clear title display max. X msecs after channel change
// - only to be fail-safe, in case EPG app answers too slowly or not at all
//
static void TvSimu_TitleChangeTimer( ClientData clientData )
{
   popDownEvent = NULL;

   debug0("TvSimu-TitleChangeTimer: no answer from EPG app - clearing display");

   sprintf(comm, "set program_title {}; set program_times {}; set program_theme {}\n");
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Shm message handler: EPG peer replied with program title information
// - display the information in the GUI
//
static void TvSimuMsg_UpdateProgInfo( void )
{
   uchar  epgProgTitle[EPG_TITLE_MAX_LEN];
   time_t epgStartTime;
   time_t epgStopTime;
   uint   epgPdcThemeCount;
   uchar  epgPdcThemes[7];
   const char * pThemeStr;
   uint   themeIdx;
   char   str_buf[50];

   if ( WinSharedMemClient_GetProgInfo(&epgStartTime, &epgStopTime,
                                       epgPdcThemes, &epgPdcThemeCount,
                                       epgProgTitle, sizeof(epgProgTitle)) )
   {
      // remove the pop-down timer (which would clear the EPG display if no answer arrives in time)
      if (popDownEvent != NULL)
         Tcl_DeleteTimerHandler(popDownEvent);
      popDownEvent = NULL;

      if ( (epgProgTitle[0] != 0) && (epgStartTime != epgStopTime) )
      {
         // Process start and stop time
         // (times are given in UNIX format, i.e. seconds since 1.1.1970 0:00am UTC)
         strftime(str_buf, sizeof(str_buf), "%a %d.%m %H:%M - ", localtime(&epgStartTime));
         strftime(str_buf + strlen(str_buf), sizeof(str_buf) - strlen(str_buf), "%H:%M", localtime(&epgStopTime));

         // Process PDC theme array: convert theme code to text
         // Note: up to 7 theme codes can be supplied (e.g. "movie general" plus "comedy")
         //       you have to decide yourself how to display them; you might use icons for the
         //       main themes (e.g. movie, news, talk show) and add text for sub-categories.
         // Use loop to search for the first valid theme code (simplest possible handling)
         pThemeStr = "";
         for (themeIdx = 0; themeIdx < epgPdcThemeCount; themeIdx++)
         {
            if (epgPdcThemes[themeIdx] >= 128)
            {  // PDC codes 0x80..0xff identify series (most EPG providers just use 0x80 for
               // all series, but they could use different codes for every series of a network)
               pThemeStr = PdcThemeGet(PDC_THEME_SERIES);
               break;
            }
            else if (PdcThemeIsDefined(epgPdcThemes[themeIdx]))
            {  // this is a known PDC series code
               pThemeStr = PdcThemeGet(epgPdcThemes[themeIdx]);
               break;
            }
            // else: unknown series code -> keep searching
         }

         // Finally display program title, start/stop times and theme text
         // Note: the Tcl variables are bound to the "entry" widget
         //       by writing to the variables the widget is automatically updated
         sprintf(comm, "set program_title {%s}; set program_times {%s}; set program_theme {%s}\n", epgProgTitle, str_buf, pThemeStr);
         eval_check(interp, comm);
      }
      else
      {  // empty string transmitted (EPG has no info for the current channel) -> clear display
         sprintf(comm, "set program_title {}; set program_times {}; set program_theme {}\n");
         eval_check(interp, comm);
      }
   }
}

// ----------------------------------------------------------------------------
// Process events triggered by EPG application
// - executed inside the main thread, but triggered by the msg receptor thread
// - this functions polls shared memory for changes in the EPG controlled
//   parameters; one such change is reported for each call of GetEpgEvent(),
//   most important ones first (e.g. attach first)
//
static void TvSimu_IdleHandler( ClientData clientData )
{
   WINSHMCLNT_EVENT curEvent;
   uint  loopCount;
   bool  shouldExit;

   shouldExit = FALSE;
   loopCount = 3;
   do
   {
      curEvent = WinSharedMemClient_GetEpgEvent();
      switch (curEvent)
      {
         case SHM_EVENT_ATTACH:
            TvSimuMsg_Attach(TRUE);
            break;

         case SHM_EVENT_DETACH:
            TvSimuMsg_Attach(FALSE);
            shouldExit = TRUE;
            break;

         case SHM_EVENT_ATTACH_ERROR:
            TvSimuMsg_BackgroundError();
            shouldExit = TRUE;
            break;

         case SHM_EVENT_PROG_INFO:
            TvSimuMsg_UpdateProgInfo();
            break;

         case SHM_EVENT_CMD_ARGV:
            TvSimuMsg_HandleEpgCmd();
            break;

         case SHM_EVENT_INP_FREQ:
            TvSimuMsg_ReqTuner();
            break;

         case SHM_EVENT_NONE:
            shouldExit = TRUE;
            break;

         default:
            fatal1("TvSimu-IdleHandler: unknown EPG event %d", curEvent);
            break;
      }
      loopCount -= 1;
   }
   while ((shouldExit == FALSE) && (loopCount > 0));

   // if not all requests were processed schedule the handler again
   if (shouldExit == FALSE)
      Tcl_DoWhenIdle(TvSimu_IdleHandler, NULL);
   else
      haveIdleHandler = FALSE;
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
// Structure which is passed to the shm client init function
//
static const WINSHMCLNT_TVAPP_INFO tvSimuInfo =
{
   "TV application simulator " TVSIM_VERSION_STR,
   "",
   TVAPP_NONE,
   TVAPP_FEAT_ALL_000701 | TVAPP_FEAT_VCR,
   TvSimuMsg_EpgEvent
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

         // check for an already pending request & execute it
         if (doGrant)
            TvSimuMsg_ReqTuner();
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
   uint  * pFreqTab;
   uint    freqCount;
   int     freqIdx;
   bool    isTuner;
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
         // read frequencies & norms from channel table file
         if ( WintvCfg_GetFreqTab(interp, &pFreqTab, &freqCount) &&
              (pFreqTab != NULL) && (freqCount > 0))
         {
            if ((freqIdx < freqCount) && (freqIdx >= 0))
            {
               // Change the tuner frequency via the BT8x8 driver
               BtDriver_TuneChannel(TVSIM_INPUT_IDX, pFreqTab[freqIdx], FALSE, &isTuner);

               // Request EPG info & update frequency info
               // - norms are coded into the high byte of the frequency dword (0=PAL-BG, 1=NTSC, 2=SECAM)
               // - input source is hard-wired to TV tuner in this simulation
               // - channel ID is hard-wired to 0 too (see comments in the winshmclnt.c)
               WinSharedMemClient_SetStation(argv[2], 0, 0, pFreqTab[freqIdx]);

               // Install a timer handler to clear the title display in case EPG app fails to answer
               if (popDownEvent != NULL)
                  Tcl_DeleteTimerHandler(popDownEvent);
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
   Tcl_SetVar(interp, "TVSIM_VERSION", TVSIM_VERSION_STR, TCL_GLOBAL_ONLY);

   Tcl_Init(interp);
   if (Tk_Init(interp) != TCL_OK)
   {
      #ifndef USE_PRECOMPILED_TCL_LIBS
      fprintf(stderr, "Failed to initialise the Tk library at '%s' - exiting.\nTk error message: %s\n",
                      TK_LIBRARY_PATH, Tcl_GetStringResult(interp));
      exit(1);
      #endif
   }

   #ifdef USE_PRECOMPILED_TCL_LIBS
   if (TCL_EVAL_CONST(interp, tcl_libs_tcl_static) != TCL_OK)
   {
      debug1("tcl_libs_tcl_static error: %s\n", Tcl_GetStringResult(interp));
      debugTclErr(interp, "tcl_libs_tcl_static");
   }
   if (TCL_EVAL_CONST(interp, tk_libs_tcl_static) != TCL_OK)
   {
      debug1("tk_libs_tcl_static error: %s\n", Tcl_GetStringResult(interp));
      debugTclErr(interp, "tk_libs_tcl_static");
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

   if (TCL_EVAL_CONST(interp, tvsim_gui_tcl_static) != TCL_OK)
   {
      debug1("tvsim_gui_tcl_static error: %s\n", Tcl_GetStringResult(interp));
      debugTclErr(interp, "tvsim_gui_tcl_static");
   }

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
   WINSHMCLNT_EVENT  attachEvent;
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
   if (WinSharedMemClient_Init(&tvSimuInfo, videoCardIndex, &attachEvent))
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

      // select language for PDC theme text output
      PdcThemeSetLanguage(TVSIM_PDC_THEME_LANGUAGE);

      // fill channel listbox with names from TV app channel table
      // a warning is issued if the table is empty (used during startup)
      sprintf(comm, "LoadChanTable");
      eval_check(interp, comm);

      if (BtDriver_StartAcq())
      {
         // set window title
         eval_check(interp, "wm title . {TV app simulator " TVSIM_VERSION_STR "}\n");
         if (startIconified)
            eval_check(interp, "wm iconify .");

         // wait until window is open and everything displayed
         while ( (Tk_GetNumMainWindows() > 0) &&
                 Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT) )
            ;

         // set window minimum size - cannot be set before the window is mapped
         sprintf(comm, "wm minsize . [winfo reqwidth .] [winfo reqheight .]\n");
         eval_check(interp, comm);

         // report attach failures during initialization
         if (attachEvent == SHM_EVENT_ATTACH)
            TvSimuMsg_Attach(TRUE);
         else if (attachEvent == SHM_EVENT_ATTACH_ERROR)
            TvSimuMsg_BackgroundError();

         // process GUI events & callbacks until the main window is closed
         while (Tk_GetNumMainWindows() > 0)
         {
            Tcl_DoOneEvent(TCL_ALL_EVENTS);
         }

         BtDriver_StopAcq();
      }
      else
      {
         debug0("Fatal: failed to start acq - quitting now");

         // this might have been the cause for failure, so display the message
         if (attachEvent == SHM_EVENT_ATTACH_ERROR)
            TvSimuMsg_BackgroundError();
      }

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
   {  // Fatal: failed to set up IPC resources
      TvSimuMsg_BackgroundError();
   }

   WintvCfg_Destroy();

   #if CHK_MALLOC == ON
   xfree(argv);
   // check for allocated memory that was not freed
   chk_memleakage();
   #endif

   exit(0);
   return(0);
}

