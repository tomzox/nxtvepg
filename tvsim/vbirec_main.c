/*
 *  VBI recording tool and shared memory monitor
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
 *    "vbirec.exe".  It's purpose is in debugging the shared memory
 *    interface of TV applications.  The tool takes the place of
 *    nxtvepg, i.e. it creates a shared memory and waits for a TV app
 *    to connect.  While a TV application is connected, all TV app
 *    controled values are displayed and constantly updated in the
 *    GUI.  It also offers the possibility to record all teletext
 *    packets that are forwarded by the TV app into a file, which
 *    later can be played back with vbiplay.exe
 *
 *  Author: Tom Zoerner
 *
 *  $Id: vbirec_main.c,v 1.16 2002/11/03 12:16:26 tom Exp tom $
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

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"
#include "epgvbi/cni_tables.h"
#include "epgvbi/hamming.h"
#include "epgdb/epgblock.h"
#include "tvsim/vbirec_gui.h"
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
static Tcl_TimerToken   clockHandler       = NULL;
static bool             haveIdleHandler    = FALSE;

volatile EPGACQ_BUF * pVbiBuf = NULL;
static int fdTtxFile = -1;

typedef struct
{
   uint   cni;
   uint   pil;
   uchar  text[PDC_TEXT_LEN + 1];
} CNIPIL;

typedef struct
{
   CNIPIL  vps;
   CNIPIL  pdc;
   CNIPIL  p8301;
} CNI_DEC_STATS;

static TTX_DEC_STATS ttxStats;
static CNI_DEC_STATS cniStats;
static uint          epgPageNo;

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

   WintvSharedMem_Exit();

   // exit the application
   ExitProcess(0);
}

#ifdef __MINGW32__
static LONG WINAPI WinApiExceptionHandler(struct _EXCEPTION_POINTERS *exc_info)
{
   //debug1("FATAL exception caught: %d", GetExceptionCode());
   debug0("FATAL exception caught");
   WintvSharedMem_Exit();
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

// ----------------------------------------------------------------------------
// Handle EPG events
// - executed inside the main thread, but triggered by the msg receptor thread
// - the driver invokes callbacks into the tvsim module to trigger GUI action,
//   e.g. to display incoming program title info
//
static void VbiRec_IdleHandler( ClientData clientData )
{
   haveIdleHandler = FALSE;
   WintvSharedMem_HandleTvCmd();
}

// ----------------------------------------------------------------------------
// 2nd stage of EPG event handling: delay handling until GUI is idle
//
static int VbiRec_AsyncThreadHandler( ClientData clientData, Tcl_Interp *interp, int code )
{
   // if an idle handler is already installed, nothing needs to be done
   // because the handler processes all incoming messages (not only one)
   if (haveIdleHandler == FALSE)
   {
      haveIdleHandler = TRUE;
      Tcl_DoWhenIdle(VbiRec_IdleHandler, NULL);
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
static void VbiRec_CbTvEvent( void )
{
   if (asyncThreadHandler != NULL)
   {
      // install event at top of Tcl event loop
      Tcl_AsyncMark(asyncThreadHandler);
   }
}

// ----------------------------------------------------------------------------
// TV application changed the input channel
//
static void VbiRec_CbStationSelected( void )
{
   char station[50];
   uint chanQueryIdx;
   TVAPP_COMM * pTvShm;

   // channel change -> reset results & state machine
   pVbiBuf->chanChangeReq += 1;
   memset(&cniStats, 0, sizeof(cniStats));

   eval_check(interp, "InitGuiVars");

   // query which station was selected
   if ( WintvSharedMem_QueryChanName(station, sizeof(station), &chanQueryIdx) )
   {
      Tcl_SetVar(interp, "tvChanName", station, TCL_GLOBAL_ONLY);
   }

   epgPageNo = pVbiBuf->epgPageNo = 0x1DF;
   Tcl_SetVar(interp, "ttx_pgno", "1DF", TCL_GLOBAL_ONLY);

   // really dirty hack to get the pointer to shared memory from the VBI buffer address
   pTvShm = (TVAPP_COMM *)((uchar *)pVbiBuf - (uchar *)&((TVAPP_COMM *)0)->vbiBuf);

   if (pTvShm->tvChanCni != 0)
   {
      sprintf(comm, "0x%04x", pTvShm->tvChanCni);
      Tcl_SetVar(interp, "tvChanCni", comm, TCL_GLOBAL_ONLY);
   }
   else
      Tcl_SetVar(interp, "tvChanCni", "---", TCL_GLOBAL_ONLY);

   if (pTvShm->tvCurInput != EPG_REQ_INPUT_NONE)
   {
      sprintf(comm, "%d", pTvShm->tvCurInput);
      Tcl_SetVar(interp, "tvCurInput", comm, TCL_GLOBAL_ONLY);
   }

   if (pTvShm->tvCurFreq != EPG_REQ_FREQ_NONE)
   {
      sprintf(comm, "%d = %.2f MHz", pTvShm->tvCurFreq, (double)pTvShm->tvCurFreq / 16);
      Tcl_SetVar(interp, "tvCurFreq", comm, TCL_GLOBAL_ONLY);
   }

   if (pTvShm->tvGrantTuner)
   {
      Tcl_SetVar(interp, "tvGrantTuner", "yes", TCL_GLOBAL_ONLY);
   }

   // always reply will NULL info
   WintvSharedMem_SetEpgInfo(0, 0, "", 0, NULL, chanQueryIdx);
}

// ----------------------------------------------------------------------------
// TV application changed tuner grant setting
//
static void VbiRec_CbTunerGrant( bool enable )
{
   Tcl_SetVar(interp, "tvGrantTuner", (enable ? "yes" : "no"), TCL_GLOBAL_ONLY);
}

// ----------------------------------------------------------------------------
// TV application attached or detached
//
static void VbiRec_CbAttachTv( bool enable, bool acqEnabled, bool slaveStateChange )
{
   char  tvAppName[100];

   if (WintvSharedMem_IsConnected(tvAppName, sizeof(tvAppName), NULL))
   {
      sprintf(comm, "ConnectEpg 1 {%s}\n", tvAppName);
   }
   else
      sprintf(comm, "ConnectEpg 0 {}\n");

   eval_check(interp, comm);
}

// ---------------------------------------------------------------------------
// Update a CNI/PIL/text and network variable
//
static void UpdateCni( volatile CNI_ACQ_STATE * pNew, CNIPIL * pOld, char * pVar )
{
   if ( (pNew->haveCni) &&
        ( (pOld->cni != pNew->haveCni) ||
          (pOld->pil != (pNew->havePil ? pNew->outPil : INVALID_VPS_PIL)) ))
   {
      // CNI and/or PIL has changed -> copy the new values
      pOld->cni = pNew->outCni;
      pOld->pil = (pNew->havePil ? pNew->outPil : INVALID_VPS_PIL);
      pOld->text[0] = 0;

      if (pOld->cni != 0)
      {
         if ( VPS_PIL_IS_VALID(pOld->pil) )
         {  // both CNI and PIL are available
            sprintf(comm, "%04X, %02d.%02d. %02d:%02d",
                             pOld->cni,
                             (pOld->pil >> 15) & 0x1F, (pOld->pil >> 11) & 0x0F,
                             (pOld->pil >>  6) & 0x1F, (pOld->pil) & 0x3F);
         }
         else if (pOld->pil == VPS_PIL_CODE_EMPTY)
            sprintf(comm, "%04X (PIL: \"fill material\")", pOld->cni);
         else if (pOld->pil == VPS_PIL_CODE_PAUSE)
            sprintf(comm, "%04X (PIL: \"pause\")", pOld->cni);
         else
            sprintf(comm, "%04X", pOld->cni);
      }
      else
         strcpy(comm, "---");

      Tcl_SetVar(interp, pVar, comm, TCL_GLOBAL_ONLY);
   }

   if ( (pNew->haveText) && (pOld->cni == 0) &&
        (strncmp(pOld->text, (char *) pNew->outText, PDC_TEXT_LEN) != 0) )
   {  // no CNI, but "status display" text is available
      strncpy(pOld->text, (char *) pNew->outText, sizeof(pOld->text));
      pNew->haveText = FALSE;

      // display the text instead of CNI value, but place it inside ""
      sprintf(comm, "\"%s\"", pOld->text);
      Tcl_SetVar(interp, pVar, comm, TCL_GLOBAL_ONLY);
   }
}

// ---------------------------------------------------------------------------
// Update network name from the "best" source
//
static void UpdateCniName( volatile CNI_ACQ_STATE * pNew )
{
   const char * pName;
   const char * pCountry;
   CNI_TYPE type;

   if (pVbiBuf != NULL)
   {
      // search for the best available source
      for (type=0; type < CNI_TYPE_COUNT; type++)
         if (pVbiBuf->cnis[type].haveCni)
            break;

      comm[0] = 0;
      if (type < CNI_TYPE_COUNT)
      {
         // Return descriptive text for a given 16-bit network code
         pName = CniGetDescription(pVbiBuf->cnis[type].outCni, &pCountry);
         if (pName != NULL)
         {
            if ((pCountry != NULL) && (strstr(pName, pCountry) == NULL))
            {
               sprintf(comm, "%s (%s)", pName, pCountry);
            }
            else
               strcpy(comm, pName);
         }
      }
      Tcl_SetVar(interp, "cni_name", comm, TCL_GLOBAL_ONLY);

      // Reset all result values (but not the entire state machine,
      // i.e. repetition counters stay at 2 so that any newly received
      // CNI will immediately make an result available again)
      for (type=0; type < CNI_TYPE_COUNT; type++)
      {
         pVbiBuf->cnis[type].haveCni = FALSE;
      }
   }
}

// ---------------------------------------------------------------------------
// Update teletext header
//
static void UpdateTtxHeader( const uchar * pHeaderData )
{
   uchar  headerText[41];
   schar  dec;
   uint   textIdx;
   uint   dataIdx;

   textIdx=0;
   for (dataIdx=0; dataIdx < 32; dataIdx++)
   {
      dec = (schar)parityTab[pHeaderData[dataIdx]];
      if (dec < 0)
         headerText[textIdx++] = '_';
      else if (dec >= ' ')
         headerText[textIdx++] = dec;
      else
         headerText[textIdx++] = ' ';
   }
   headerText[textIdx] = 0;
   Tcl_SetVar(interp, "ttx_head", headerText, TCL_GLOBAL_ONLY);
}

// ---------------------------------------------------------------------------
// Second timer: read EPG packets and store them in the file
//
static void SecTimerEvent( ClientData clientData )
{
   if (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf)
   {
      if ( (pVbiBuf->mipPageNo != EPG_ILLEGAL_PAGENO) &&
           (pVbiBuf->mipPageNo != epgPageNo) )
      {
         pVbiBuf->chanChangeReq += 1;
         pVbiBuf->epgPageNo = pVbiBuf->mipPageNo;
         epgPageNo          = pVbiBuf->mipPageNo;

         sprintf(comm, "%03X", epgPageNo);
         Tcl_SetVar(interp, "ttx_pgno", comm, TCL_GLOBAL_ONLY);
      }

      while (pVbiBuf->reader_idx != pVbiBuf->writer_idx)
      {
         if (fdTtxFile != -1)
         {
            write(fdTtxFile, (char *) &pVbiBuf->line[pVbiBuf->reader_idx], sizeof(VBI_LINE));
         }

         pVbiBuf->reader_idx = (pVbiBuf->reader_idx + 1) % EPGACQ_BUF_COUNT;
      }

      if (pVbiBuf->lastHeader.pageno != 0xffff)
         UpdateTtxHeader((char *)pVbiBuf->lastHeader.data + 13 - 5);

      if (ttxStats.ttxPkgCount != pVbiBuf->ttxStats.ttxPkgCount)
      {
         sprintf(comm, "%d", pVbiBuf->ttxStats.ttxPkgCount);
         Tcl_SetVar(interp, "ttx_pkg", comm, TCL_GLOBAL_ONLY);
         ttxStats.ttxPkgCount = pVbiBuf->ttxStats.ttxPkgCount;
      }
      if (ttxStats.ttxPkgRate != pVbiBuf->ttxStats.ttxPkgRate)
      {
         sprintf(comm, "%.1f (%.0f baud)",
                       (double)pVbiBuf->ttxStats.ttxPkgRate / (1 << TTX_PKG_RATE_FIXP),
                       (double)pVbiBuf->ttxStats.ttxPkgRate / (1 << TTX_PKG_RATE_FIXP) * 42 * 8 * 25);
         Tcl_SetVar(interp, "ttx_rate", comm, TCL_GLOBAL_ONLY);
         ttxStats.ttxPkgRate = pVbiBuf->ttxStats.ttxPkgRate;
      }
      if (ttxStats.vpsLineCount != pVbiBuf->ttxStats.vpsLineCount)
      {
         sprintf(comm, "%d", pVbiBuf->ttxStats.vpsLineCount);
         Tcl_SetVar(interp, "vps_cnt", comm, TCL_GLOBAL_ONLY);
         ttxStats.vpsLineCount = pVbiBuf->ttxStats.vpsLineCount;
      }
      if (ttxStats.epgPkgCount != pVbiBuf->ttxStats.epgPkgCount)
      {
         sprintf(comm, "%d", pVbiBuf->ttxStats.epgPkgCount);
         Tcl_SetVar(interp, "epg_pkg", comm, TCL_GLOBAL_ONLY);
         ttxStats.epgPkgCount = pVbiBuf->ttxStats.epgPkgCount;
      }
      if (ttxStats.epgPagCount != pVbiBuf->ttxStats.epgPagCount)
      {
         sprintf(comm, "%d", pVbiBuf->ttxStats.epgPagCount);
         Tcl_SetVar(interp, "epg_pag", comm, TCL_GLOBAL_ONLY);
         ttxStats.epgPagCount = pVbiBuf->ttxStats.epgPagCount;
      }

      UpdateCni(pVbiBuf->cnis + CNI_TYPE_VPS, &cniStats.vps, "cni_vps");
      UpdateCni(pVbiBuf->cnis + CNI_TYPE_PDC, &cniStats.pdc, "cni_pdc");
      UpdateCni(pVbiBuf->cnis + CNI_TYPE_NI, &cniStats.p8301, "cni_p8301");

      UpdateCniName(pVbiBuf->cnis);
   }

   clockHandler = Tcl_CreateTimerHandler(500, SecTimerEvent, NULL);
}

// ---------------------------------------------------------------------------
// Toggle teletext dump on/off
//
static char * TclCb_EnableDump( ClientData clientData, Tcl_Interp * interp, char * name1, CONST84 char * name2, int flags )
{
   const char * pFileName;
   CONST84 char * pTmpStr;
   int    enable;

   pFileName = Tcl_GetVar(interp, "dumpttx_filename", TCL_GLOBAL_ONLY);
   pTmpStr = Tcl_GetVar(interp, "dumpttx_enable", TCL_GLOBAL_ONLY);
   if ((pFileName != NULL) && (*pFileName != 0) && (pTmpStr != NULL))
   {
      if (Tcl_GetInt(interp, pTmpStr, &enable) == TCL_OK)
      {
         if (enable && (fdTtxFile == -1))
         {  // dump has been switched on
            fdTtxFile = open(pFileName, O_WRONLY|O_CREAT|O_APPEND|O_BINARY, 0666);
            if (fdTtxFile == -1)
            {  // failed to create the file -> give error message and disable dump
               Tcl_SetVar(interp, "dumpttx_enable", "0", TCL_GLOBAL_ONLY);
               sprintf(comm, "tk_messageBox -type ok -icon error "
                             "-message {Failed to open '%s' for writing: %s}", pFileName, strerror(errno));
               eval_check(interp, comm);
            }
         }
         else if ((enable == FALSE) && (fdTtxFile != -1))
         {  // dump has been switched off
            close(fdTtxFile);
            fdTtxFile = -1;
         }
      }
   }
   else
   {
      Tcl_SetVar(interp, "dumpttx_enable", "0", TCL_GLOBAL_ONLY);
   }
   return NULL;
}

// ---------------------------------------------------------------------------
// Dummy functions for winshmsrv.c
//
bool BtDriver_Init( void ) { return TRUE; }
void BtDriver_Exit( void ) {}
bool BtDriver_StartAcq( void ) { return TRUE; }
void BtDriver_StopAcq( void ) {}
bool BtDriver_Restart( void ) { return TRUE; }
bool BtDriver_GetState( bool * pEnabled, bool * pHasDriver, uint * pCardIdx )
{
   if (pEnabled != NULL)
      *pEnabled = FALSE;
   return TRUE;
}
bool TtxDecode_GetCniAndPil( uint * pCni, uint *pPil, volatile EPGACQ_BUF *pThisVbiBuf ) { return FALSE; }
void TtxDecode_NotifyChannelChange( volatile EPGACQ_BUF * pThisVbiBuf ) {}

// ----------------------------------------------------------------------------
// Struct with callback functions for shared memory server module
//
static const WINSHMSRV_CB vbiRecCb =
{
   VbiRec_CbTvEvent,
   VbiRec_CbStationSelected,
   VbiRec_CbTunerGrant,
   VbiRec_CbAttachTv
};

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
   asyncThreadHandler = Tcl_AsyncCreate(VbiRec_AsyncThreadHandler, NULL);

   if (TCL_EVAL_CONST(interp, vbirec_gui_tcl_static) != TCL_OK)
   {
      debug1("vbirec_gui_tcl_static error: %s\n", Tcl_GetStringResult(interp));
      debugTclErr(interp, "vbirec_gui_tcl_static");
   }

   Tcl_TraceVar(interp, "dumpttx_enable", TCL_TRACE_WRITES|TCL_GLOBAL_ONLY, TclCb_EnableDump, NULL);

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
   memset(&ttxStats, 0, sizeof(ttxStats));
   memset(&cniStats, 0, sizeof(cniStats));

   // initialize Tcl/Tk interpreter and compile all scripts
   SetArgv(&argc, &argv);
   ui_init(argc, argv);

   if (WintvSharedMem_Init())
   {
      // set up callback to catch shutdown messages (requires tk83.dll patch!)
      Tk_RegisterMainDestructionHandler(WinApiDestructionHandler);
      #ifndef __MINGW32__
      __try {
      #else
      SetUnhandledExceptionFilter(WinApiExceptionHandler);
      #endif

      WintvSharedMem_SetCallbacks(&vbiRecCb);
      if (WintvSharedMem_StartStop(TRUE, NULL))
      {
         pVbiBuf = WintvSharedMem_GetVbiBuf();
         if (pVbiBuf != NULL)
         {
            // skip first VBI frame, reset ttx decoder, then set reader idx to writer idx
            pVbiBuf->chanChangeReq = pVbiBuf->chanChangeCnf = 0;
            pVbiBuf->reader_idx = pVbiBuf->writer_idx;

            // pass the configuration variables to the ttx process via IPC
            epgPageNo = pVbiBuf->epgPageNo = 0x1DF;
            pVbiBuf->isEpgScan = FALSE;

            // enable acquisition in the slave process/thread
            pVbiBuf->chanChangeReq = pVbiBuf->chanChangeCnf + 2;
            pVbiBuf->isEnabled = TRUE;

            VbiRec_CbStationSelected();

            // trigger second event for the first time to install the timer
            SecTimerEvent(NULL);

            // set window title & disable resizing
            eval_check(interp, "wm title . {VBI recorder " TVSIM_VERSION_STR "}\nwm resizable . 0 0\n");

            // wait until window is open and everything displayed
            while ( (Tk_GetNumMainWindows() > 0) &&
                    Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT) )
               ;

            // process GUI events & callbacks until the main window is closed
            while (Tk_GetNumMainWindows() > 0)
            {
               Tcl_DoOneEvent(TCL_ALL_EVENTS);
            }

            if (clockHandler != NULL)
               Tcl_DeleteTimerHandler(clockHandler);

            Tcl_UntraceVar(interp, "dumpttx_enable", TCL_TRACE_WRITES|TCL_GLOBAL_ONLY, TclCb_EnableDump, NULL);
            if (fdTtxFile != -1)
               close(fdTtxFile);
         }
      }
      else
      {
         const char * pShmErrMsg;
         char * pErrBuf;

         pShmErrMsg = WinSharedMem_GetErrorMsg();
         if (pShmErrMsg != NULL)
         {
            pErrBuf = xmalloc(strlen(pShmErrMsg) + 100);

            sprintf(pErrBuf, "tk_messageBox -type ok -icon error -message {%s}", pShmErrMsg);
            eval_check(interp, pErrBuf);

            xfree((void *) pErrBuf);
            xfree((void *) pShmErrMsg);
         }
      }

      #if defined(WIN32) && !defined(__MINGW32__)
      }
      __except (EXCEPTION_EXECUTE_HANDLER)
      {  // caught a fatal exception -> free IPC resources
         debug1("FATAL exception caught: %d", GetExceptionCode());
         WintvSharedMem_Exit();
         ExitProcess(-1);
      }
      #endif

      WintvSharedMem_Exit();
   }

   exit(0);
   return (0);
}

