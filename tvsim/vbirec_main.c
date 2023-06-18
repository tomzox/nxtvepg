/*
 *  VBI recording tool and shared memory monitor
 *
 *  Copyright (C) 2002-2008 T. Zoerner
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
 *    "vbirec".  It's purpose is in debugging the EPG interface
 *    of TV applications.  The tool takes the place of nxtvepg,
 *    i.e. it sets up communication channels and waits for a TV app
 *    to connect.  While a TV application is connected, all TV app
 *    controled values are displayed and constantly updated in the
 *    GUI.
 *
 *    It also offers the possibility to record all EPG teletext packets
 *    into a file.  In case of Windows teletext data is forwarded by
 *    the TV app, in case of UNIX it's read from the VBI device. On
 *    Windows the recorded data can later be played back with vbiplay,
 *    on UNIX it requires patching of ttxdecode.c  (Plackback is meant
 *    for debugging by developers only anyways.)
 */

#define DEBUG_SWITCH DEBUG_SWITCH_TVSIM
//#define DPRINTF_OFF

#ifdef WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#ifndef WIN32
#include <signal.h>
#endif

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
#include "epgui/rcfile.h"
#include "epgui/epgmain.h"
#include "epgtcl/combobox.h"
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

#ifndef O_BINARY
#define O_BINARY       0          // for M$-Windows only
#endif

char tvsim_rcs_id_str[] = TVSIM_VERSION_RCS_ID;

Tcl_Interp *interp;          // Interpreter for application
#define TCL_COMM_BUF_SIZE  1000
// add one extra byte at the end of the comm buffer for overflow detection
char comm[TCL_COMM_BUF_SIZE + 1];

#ifdef WIN32
static Tcl_AsyncHandler asyncThreadHandler = NULL;
static bool             haveIdleHandler    = FALSE;
#else
static Tcl_AsyncHandler exitAsyncHandler   = NULL;
#endif
static Tcl_TimerToken   clockHandler       = NULL;
static bool should_exit;

#ifndef WIN32
typedef enum
{
   EPG_IPC_BOTH,
   EPG_IPC_XAWTV,
   EPG_IPC_ICCCM,
   EPG_IPC_COUNT
} EPG_IPC_PROTOCOL;
#define IPC_XAWTV_ENABLED (epgIpcProtocol != EPG_IPC_ICCCM)
#define IPC_ICCCM_ENABLED (epgIpcProtocol != EPG_IPC_XAWTV)
#endif

#ifdef WIN32
volatile EPGACQ_BUF * pVbiBuf = NULL;
#endif
static int fdTtxFile = -1;

typedef struct
{
   uint   cni;
   uint   pil;
   char   text[PDC_TEXT_LEN + 1];
} CNIPIL;

typedef struct
{
   CNIPIL  vps;
   CNIPIL  pdc;
   CNIPIL  p8301;
} CNI_DEC_STATS;

static TTX_DEC_STATS ttxStats;
static CNI_DEC_STATS cniStats;
static uint          epgPageStart;  // TODO needs to be editable
static uint          epgPageStop;

// Default TV card index - identifies which TV card is used by the simulator
// (note that in contrary to the other TV card parameters this value is not
// taken from the nxtvepg rc/ini file; can be overriden with -card option)
#define TVSIM_CARD_IDX   0
// input source index of the TV tuner with btdrv4win.c
#define TVSIM_INPUT_IDX  0

#ifndef WIN32
static uint videoCardIndex = TVSIM_CARD_IDX;
static EPG_IPC_PROTOCOL epgIpcProtocol = EPG_IPC_BOTH;
#endif
static bool startIconified = FALSE;

#ifndef WIN32
// ---------------------------------------------------------------------------
// Dummies for xawtv module
//
#include "epgdb/epgdbfil.h"
#include "epgctl/epgacqctl.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgsetup.h"
#include "epgui/shellcmd.h"
#include "epgui/pibox.h"
#include "epgui/wintvui.h"
#include "epgui/xawtv.h"

EPGDB_CONTEXT dummyDbContext;
EPGDB_CONTEXT * pUiDbContext = &dummyDbContext;
EPGDB_CONTEXT * pAcqDbContext = NULL;
static AI_BLOCK * pFakeAi = NULL;
static PI_BLOCK * pFakePi = NULL;

void  EpgDbLockDatabase( EPGDB_CONTEXT * dbc, uchar enable ) {}
bool  EpgDbIsLocked( const EPGDB_CONTEXT * dbc ) { return TRUE; }
const AI_BLOCK * EpgDbGetAi( const EPGDB_CONTEXT * dbc ) { return pFakeAi; }
const PI_BLOCK * EpgDbSearchPiByPil( const EPGDB_CONTEXT * dbc, uint netwop_no, uint pil ) { return pFakePi; }
const PI_BLOCK * EpgDbSearchFirstPiAfter( const EPGDB_CONTEXT * dbc, time_t min_time, EPGDB_TIME_SEARCH_MODE startOrStop, const FILTER_CONTEXT *fc ) { return pFakePi; }
const PI_BLOCK * EpgDbSearchFirstPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc ) { return pFakePi; }
const PI_BLOCK * EpgDbSearchNextPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock ) { return pFakePi; }
bool EpgDbContextIsMerged( const EPGDB_CONTEXT * dbc ) { return FALSE; }

FILTER_CONTEXT * EpgDbFilterCreateContext( void ) { return NULL; }
void   EpgDbFilterDestroyContext( FILTER_CONTEXT * fc ) {}
void   EpgDbFilterEnable( FILTER_CONTEXT *fc, uint mask ) {}
void   EpgDbFilterInitNetwop( FILTER_CONTEXT *fc, uint netwopCount ) {}
void   EpgDbFilterSetNetwop( FILTER_CONTEXT *fc, uint netwopNo ) {}

const char * EpgSetup_GetNetName( const AI_BLOCK * pAiBlock, uint netIdx, bool * pIsFromAi )
{
   const char * pResult;

   if ((pAiBlock != NULL) && (netIdx < pAiBlock->netwopCount))
      pResult = AI_GET_NETWOP_NAME(pAiBlock, netIdx);
   else
      pResult = "";

   if (pIsFromAi != NULL)
      *pIsFromAi = TRUE;
   return pResult;
}

void AddMainIdleEvent( Tcl_IdleProc *IdleProc, ClientData clientData, bool unique )
{
   Tcl_CancelIdleCall(IdleProc, clientData);
   Tcl_DoWhenIdle(IdleProc, clientData);
}
bool RemoveMainIdleEvent( Tcl_IdleProc * IdleProc, ClientData clientData, bool matchData )
{
   Tcl_CancelIdleCall(IdleProc, clientData);
   return FALSE;
}
void EpgAcqCtl_DescribeAcqState( EPGACQ_DESCR * pAcqState ) {}
bool EpgAcqCtl_GetVpsPdc( EPG_ACQ_VPS_PDC * pVpsPdc, VPSPDC_REQ_ID clientId, bool force ) { return FALSE; }
bool EpgAcqCtl_CheckDeviceAccess( void ) { return TRUE; }
void EpgAcqCtl_ResetVpsPdc( void ) {}

void PiBox_GotoPi( const PI_BLOCK * pPiBlock ) {}
void PiOutput_ExecuteScript( Tcl_Interp *interp, Tcl_Obj * pCmdObj, const PI_BLOCK * pPiBlock ) {}
uint WintvUi_StationNameToCni( char * pName, uint MapName2Cni(const char * station) ) { return 0; }
bool WintvUi_CheckAirTimes( uint cni ) { return TRUE; }
const char * RcFile_GetNetworkName( uint cni ) { return NULL; }

static void TvSimu_CreateFakeAiAndPi( uint cni, time_t start_time, time_t stop_time,
                                      const char * pTitle )
{
   AI_NETWOP * pNetwop;
   char * pNextStr;
   uint  blockLen;
   uint  netwopCount = 1+1;
   uint  netwop;

   if (pFakeAi != NULL)
      xfree(pFakeAi);
   blockLen = sizeof(AI_BLOCK) + netwopCount * sizeof(AI_NETWOP) + 12+1 + netwopCount * 15;
   pFakeAi = xmalloc(blockLen);
   memset(pFakeAi, 0, blockLen);

   pFakeAi->netwopCount = netwopCount;
   pFakeAi->off_netwops = sizeof(AI_BLOCK);
   pNetwop = (AI_NETWOP *)((char *)pFakeAi + pFakeAi->off_netwops);

   pFakeAi->off_serviceNameStr = sizeof(AI_BLOCK) + sizeof(AI_NETWOP) * netwopCount;
   pNextStr = (char *)pFakeAi + pFakeAi->off_serviceNameStr;
   strcpy(pNextStr, "FAKE SERVICE");
   pNextStr += 12+1;

   for (netwop = 0; netwop < netwopCount; netwop++, pNetwop++)
   {
      if (netwop == 0)
         pNetwop->netCni = 0;
      else
         pNetwop->netCni = cni & XMLTV_NET_CNI_MASK;
      pNetwop->off_name = (char*) pNextStr - (char*) pFakeAi;
      sprintf(pNextStr, "Network 0x%04X", cni);
      assert(strlen(pNextStr) == 15-1);
      pNextStr += 7;
   }

   if (pFakePi != NULL)
      xfree(pFakePi);
   blockLen = sizeof(PI_BLOCK) + strlen(pTitle)+1;
   pFakePi = xmalloc(blockLen);
   memset(pFakePi, 0, blockLen);

   pFakePi->netwop_no = 0;
   pFakePi->start_time = start_time;
   pFakePi->stop_time = stop_time;
   pFakePi->off_title = sizeof(PI_BLOCK);
   strcpy((char *)pFakePi + pFakePi->off_title, pTitle);
}

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
static void signal_handler( int sigval )
{
   should_exit = TRUE;

   if (exitAsyncHandler != NULL)
   {  // trigger an event, so that the Tcl/Tk event handler immediately wakes up
      Tcl_AsyncMark(exitAsyncHandler);
   }
}

// ----------------------------------------------------------------------------
// TV application changed the input channel
//
static int Xawtv_CbStationChange(ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[])
{
   const char * const pUsage = "Usage: XawtvConfigShmChannelChange <name> <freq>";
   int tvCurFreq;
   int result;

   if ( (argc != 3) ||
        (Tcl_GetInt(interp, argv[2], &tvCurFreq) != TCL_OK) )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      // channel change -> reset results & state machine
      pVbiBuf->buf[0].chanChangeReq += 1;
      memset(&cniStats, 0, sizeof(cniStats));

      eval_check(interp, "InitGuiVars");

      epgPageStart = pVbiBuf->startPageNo[0] = 0x300;
      epgPageStop = pVbiBuf->stopPageNo[0] = 0x399;
      sprintf(comm, "%03X-%03X", epgPageStart, epgPageStop);
      Tcl_SetVar(interp, "ttx_pgno", comm, TCL_GLOBAL_ONLY);

      Tcl_SetVar(interp, "tvChanName", argv[1], TCL_GLOBAL_ONLY);

      if (tvCurFreq != 0)
      {
         sprintf(comm, "%d = %.2f MHz", tvCurFreq, (double)tvCurFreq / 16);
         Tcl_SetVar(interp, "tvCurTvInput", comm, TCL_GLOBAL_ONLY);
      }
      result = TCL_OK;
   }
   return result;
}

#else // WIN32

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

#if (TCL_MAJOR_VERSION != 8) || (TCL_MINOR_VERSION >= 5)
static int TclCbWinHandleShutdown( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   WinApiDestructionHandler(NULL);
   return TCL_OK;
}
#endif

#ifdef __MINGW32__
static LONG WINAPI WinApiExceptionHandler(struct _EXCEPTION_POINTERS *exc_info)
{
   static bool inExit = FALSE;

   //debug1("FATAL exception caught: %d", GetExceptionCode());
   debug0("FATAL exception caught");

   // prevent recursive calls - may occur upon crash in acq stop function
   if (inExit == FALSE)
   {
      inExit = TRUE;

      // skip EpgAcqCtl_Stop() because it tries to dump the db - do as little as possible here
      WintvSharedMem_Exit();
      ExitProcess(-1);
   }
   else
   {
      ExitProcess(-1);
   }

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

static void VbiRec_CbEpgQuery( void )
{
}

// ----------------------------------------------------------------------------
// TV application changed the input channel
//
static void VbiRec_CbStationSelected( void )
{
   char station[50];
   uint chanQueryIdx;
   uint epgCnt;
   TVAPP_COMM * pTvShm;

   // channel change -> reset results & state machine
   pVbiBuf->buf[0].chanChangeReq += 1;
   memset(&cniStats, 0, sizeof(cniStats));

   eval_check(interp, "InitGuiVars");

   // query which station was selected
   if ( WintvSharedMem_GetStation( station, sizeof(station), &chanQueryIdx, &epgCnt) )
   {
      Tcl_SetVar(interp, "tvChanName", station, TCL_GLOBAL_ONLY);
   }

   epgPageStart = pVbiBuf->startPageNo[0] = 0x300;
   epgPageStop = pVbiBuf->stopPageNo[0] = 0x399;
   sprintf(comm, "%03X-%03X", epgPageStart, epgPageStop);
   Tcl_SetVar(interp, "ttx_pgno", comm, TCL_GLOBAL_ONLY);

   // really dirty hack to get the pointer to shared memory from the VBI buffer address
   pTvShm = (TVAPP_COMM *)((uchar *)pVbiBuf - (uchar *)&((TVAPP_COMM *)0)->vbiBuf);

#if 0
   if (pTvShm->tvChanCni != 0)
   {
      sprintf(comm, "0x%04x", pTvShm->tvChanCni);
      Tcl_SetVar(interp, "tvChanCni", comm, TCL_GLOBAL_ONLY);
   }
   else
      Tcl_SetVar(interp, "tvChanCni", "---", TCL_GLOBAL_ONLY);
#endif

   if (pTvShm->tvCurIsTuner)
   {
      if (pTvShm->tvCurFreq != EPG_REQ_FREQ_NONE)
      {
         sprintf(comm, "%d = %.2f MHz (%s)",
                       pTvShm->tvCurFreq, (double)pTvShm->tvCurFreq / 16,
                       (pTvShm->tvCurNorm == 0) ? "PAL" :
                          ((pTvShm->tvCurNorm == 1) ? "SECAM" :
                             ((pTvShm->tvCurNorm == 2) ? "NTSC" : "invalid")) );
         Tcl_SetVar(interp, "tvCurTvInput", comm, TCL_GLOBAL_ONLY);
      }
      else
         Tcl_SetVar(interp, "tvCurTvInput", "Tuner: freq. unknown", TCL_GLOBAL_ONLY);
   }
   else
   {
      Tcl_SetVar(interp, "tvCurTvInput", "Not a tuner", TCL_GLOBAL_ONLY);
   }

   Tcl_SetVar(interp, "tvGrantTuner", pTvShm->tvGrantTuner ? "yes" : "no", TCL_GLOBAL_ONLY);

   if (pTvShm->tvReqTvCard)
   {
      sprintf(comm, "%d", pTvShm->tvCardIdx);
      Tcl_SetVar(interp, "tvReqCardIdx", comm, TCL_GLOBAL_ONLY);
   }
   else
      Tcl_SetVar(interp, "tvReqCardIdx", "none", TCL_GLOBAL_ONLY);

   // always reply will NULL info
   WintvSharedMem_SetEpgInfo("", 0, chanQueryIdx, TRUE);
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
#endif  // WIN32

// ---------------------------------------------------------------------------
// Update CNI/PIL/text in the display for one source
//
static void UpdateCni( volatile CNI_ACQ_STATE * pNew, CNIPIL * pOld, const char * pVar )
{
   char *ps, *pe;
   uint outlen;
   bool update;

   // check if the CNI or PIL changed
   update = ( (pNew->haveCni) &&
              ( (pOld->cni != pNew->haveCni) ||
                (pOld->pil != (pNew->havePil ? pNew->outPil : INVALID_VPS_PIL)) ));
   // check if the text changed
   update |= ( (pNew->haveText) &&
               (strncmp(pOld->text, (char *) pNew->outText, PDC_TEXT_LEN) != 0) );

   if (update)
   {
      // CNI and/or PIL has changed -> copy the new values
      pOld->cni = pNew->outCni;
      pOld->pil = (pNew->havePil ? pNew->outPil : INVALID_VPS_PIL);

      if (pOld->cni != 0)
      {
         if ( VPS_PIL_IS_VALID(pOld->pil) )
         {  // both CNI and PIL are available
            outlen = sprintf(comm, "%04X, %02d.%02d. %02d:%02d",
                                   pOld->cni,
                                   (pOld->pil >> 15) & 0x1F, (pOld->pil >> 11) & 0x0F,
                                   (pOld->pil >>  6) & 0x1F, (pOld->pil) & 0x3F);
         }
         else if (pOld->pil == VPS_PIL_CODE_EMPTY)
            outlen = sprintf(comm, "%04X (PIL: \"fill material\")", pOld->cni);
         else if (pOld->pil == VPS_PIL_CODE_PAUSE)
            outlen = sprintf(comm, "%04X (PIL: \"pause\")", pOld->cni);
         else
            outlen = sprintf(comm, "%04X", pOld->cni);
      }
      else
         outlen = sprintf(comm, "---");

      if (pNew->haveText)
      {
         // "status display" text is available
         strncpy(pOld->text, (char *) pNew->outText, sizeof(pOld->text));
         pNew->haveText = FALSE;

         // skip any spaces at the start of the name
         ps = pOld->text;
         while (*ps == ' ')
            ps += 1;

         // chop any spaces at the end of the name
         if (*ps != 0)
         {
            pe = ps + strlen(ps) - 1;
            while ((pe > ps) && (*pe == ' '))
               pe--;

            *(++pe) = 0;
            outlen += sprintf(comm + outlen, "  \"%s\"", ps);
         }
      }
      else
         pOld->text[0] = 0;

      Tcl_SetVar(interp, (char*) pVar, comm, TCL_GLOBAL_ONLY);
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
         if (pVbiBuf->buf[0].cnis[type].haveCni)
            break;

      comm[0] = 0;
      if (type < CNI_TYPE_COUNT)
      {
         // Return descriptive text for a given 16-bit network code
         pName = CniGetDescription(pVbiBuf->buf[0].cnis[type].outCni, &pCountry);
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
         pVbiBuf->buf[0].cnis[type].haveCni = FALSE;
      }
   }
}

// ---------------------------------------------------------------------------
// Update teletext header
//
static void UpdateTtxHeader( const char * pHeaderData )
{
   char   headerText[41];
   schar  dec;
   uint   textIdx;
   uint   dataIdx;

   textIdx=0;
   for (dataIdx=0; dataIdx < 32; dataIdx++)
   {
      dec = (schar)parityTab[(uchar)pHeaderData[dataIdx]];
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
   ssize_t wstat;
   uint idx;

   if (pVbiBuf->buf[0].chanChangeReq == pVbiBuf->buf[0].chanChangeCnf)
   {
      while ((idx = pVbiBuf->buf[0].reader_idx) != pVbiBuf->buf[0].writer_idx)
      {
         if (fdTtxFile != -1)
         {
            wstat = write(fdTtxFile, (char *) &pVbiBuf->buf[0].line[idx], sizeof(VBI_LINE));
            if (wstat < sizeof(VBI_LINE))
            {
               sprintf(comm, "tk_messageBox -type ok -icon error "
                             "-message {Write error in export file: %s}", strerror(errno));
               eval_check(interp, comm);

#ifndef WIN32
               TtxDecode_StopTtxAcq();
#endif
               close(fdTtxFile);
               fdTtxFile = -1;
            }
         }

         pVbiBuf->buf[0].reader_idx = (idx + 1) % TTXACQ_BUF_COUNT;
      }

      if (pVbiBuf->buf[0].ttxHeader.fill_cnt != 0)
      {
         pVbiBuf->buf[0].ttxHeader.write_lock = TRUE;
         idx = (pVbiBuf->buf[0].ttxHeader.write_idx + (EPGACQ_ROLL_HEAD_COUNT - 1)) % EPGACQ_ROLL_HEAD_COUNT;
         UpdateTtxHeader((char *)pVbiBuf->buf[0].ttxHeader.ring_buf[idx].data + 8);
         pVbiBuf->buf[0].ttxHeader.write_lock = FALSE;
      }
      if (ttxStats.ttxPkgCount != pVbiBuf->buf[0].ttxStats.ttxPkgCount)
      {
         sprintf(comm, "%d", pVbiBuf->buf[0].ttxStats.ttxPkgCount);
         Tcl_SetVar(interp, "ttx_pkg", comm, TCL_GLOBAL_ONLY);
         ttxStats.ttxPkgCount = pVbiBuf->buf[0].ttxStats.ttxPkgCount;
      }
      if (ttxStats.ttxPkgRate != pVbiBuf->buf[0].ttxStats.ttxPkgRate)
      {
         sprintf(comm, "%.1f (%.0f baud)",
                       (double)pVbiBuf->buf[0].ttxStats.ttxPkgRate / (1 << TTX_PKG_RATE_FIXP),
                       (double)pVbiBuf->buf[0].ttxStats.ttxPkgRate / (1 << TTX_PKG_RATE_FIXP) * 42 * 8 * 25);
         Tcl_SetVar(interp, "ttx_rate", comm, TCL_GLOBAL_ONLY);
         ttxStats.ttxPkgRate = pVbiBuf->buf[0].ttxStats.ttxPkgRate;
      }
      if (ttxStats.vpsLineCount != pVbiBuf->buf[0].ttxStats.vpsLineCount)
      {
         sprintf(comm, "%d", pVbiBuf->buf[0].ttxStats.vpsLineCount);
         Tcl_SetVar(interp, "vps_cnt", comm, TCL_GLOBAL_ONLY);
         ttxStats.vpsLineCount = pVbiBuf->buf[0].ttxStats.vpsLineCount;
      }
      if (ttxStats.ttxPkgGrab != pVbiBuf->buf[0].ttxStats.ttxPkgGrab)
      {
         sprintf(comm, "%d", pVbiBuf->buf[0].ttxStats.ttxPkgGrab);
         Tcl_SetVar(interp, "epg_pkg", comm, TCL_GLOBAL_ONLY);
         ttxStats.ttxPkgGrab = pVbiBuf->buf[0].ttxStats.ttxPkgGrab;
      }
      if (ttxStats.ttxPagGrab != pVbiBuf->buf[0].ttxStats.ttxPagGrab)
      {
         sprintf(comm, "%d", pVbiBuf->buf[0].ttxStats.ttxPagGrab);
         Tcl_SetVar(interp, "epg_pag", comm, TCL_GLOBAL_ONLY);
         ttxStats.ttxPagGrab = pVbiBuf->buf[0].ttxStats.ttxPagGrab;
      }

      UpdateCni(pVbiBuf->buf[0].cnis + CNI_TYPE_VPS, &cniStats.vps, "cni_vps");
      UpdateCni(pVbiBuf->buf[0].cnis + CNI_TYPE_PDC, &cniStats.pdc, "cni_pdc");
      UpdateCni(pVbiBuf->buf[0].cnis + CNI_TYPE_NI, &cniStats.p8301, "cni_p8301");

      UpdateCniName(pVbiBuf->buf[0].cnis);
   }

   clockHandler = Tcl_CreateTimerHandler(500, SecTimerEvent, NULL);
}

// ---------------------------------------------------------------------------
// Toggle teletext dump on/off
//
static char * TclCb_EnableDump( ClientData clientData, Tcl_Interp * interp, CONST84 char * name1, CONST84 char * name2, int flags )
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
            if (fdTtxFile >= 0)
            {
#ifndef WIN32
               TtxDecode_StartTtxAcq(FALSE, 0, 0xFFF);
#endif
            }
            else
            {  // failed to create the file -> give error message and disable dump
               Tcl_SetVar(interp, "dumpttx_enable", "0", TCL_GLOBAL_ONLY);
               sprintf(comm, "tk_messageBox -type ok -icon error "
                             "-message {Failed to open '%s' for writing: %s}", pFileName, strerror(errno));
               eval_check(interp, comm);
            }
         }
         else if ((enable == FALSE) && (fdTtxFile != -1))
         {  // dump has been switched off
#ifndef WIN32
            TtxDecode_StopTtxAcq();
#endif
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

// ----------------------------------------------------------------------------
// Callback to send EPG OSD info to TV app
//
static int TclCb_SendEpgOsd( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_SendEpgOsd <title> <start-time> <stop-time>";
   int  start_time, stop_time;
   int  result;

   if ( (objc != 1+3) ||
        (Tcl_GetIntFromObj(interp, objv[2], &start_time) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[3], &stop_time) != TCL_OK) )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
#ifdef WIN32
      uint  chanQueryIdx;
      uint  epgCnt;
      char  station[50];
      char  start_str[40], stop_str[20];
      struct tm *pTm;
      time_t time_ts;

      WintvSharedMem_GetStation(station, sizeof(station), &chanQueryIdx, &epgCnt);

      time_ts = start_time;
      pTm = localtime(&time_ts);
      if (pTm != NULL)
         strftime(start_str, sizeof(start_str), "%Y-%m-%d\t%H:%M:00", pTm);
      else
         strcpy(start_str, "");

      time_ts = stop_time;
      pTm  = localtime(&time_ts);
      if (pTm != NULL)
         strftime(stop_str, sizeof(stop_str), "%H:%M:00", pTm);
      else
         strcpy(stop_str, "");

      sprintf(comm, "%s\t"
                  "%u\t%s\t%s\t%s\t%u\t%u\t"           // netwop ... e-rat
                  "%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t"   // features
                  "%u\t%u\t%u\t%u\t%u\t%u\t%u\t"       // themes
                  "%s\t%s\n",                          // title, description
              station,
              0, //pPi->netwop_no,
              start_str,
              stop_str,
              "", //pilStr,
              0, //pPi->parental_rating *2,
              0, //pPi->editorial_rating,
              "", //pStrSoundFormat,
              0, //((pPi->feature_flags & PI_FEATURE_FMT_WIDE) ? 1 : 0),
              0, //((pPi->feature_flags & PI_FEATURE_PAL_PLUS) ? 1 : 0),
              0, //((pPi->feature_flags & PI_FEATURE_DIGITAL) ? 1 : 0),
              0, //((pPi->feature_flags & PI_FEATURE_ENCRYPTED) ? 1 : 0),
              0, //((pPi->feature_flags & PI_FEATURE_LIVE) ? 1 : 0),
              0, //((pPi->feature_flags & PI_FEATURE_REPEAT) ? 1 : 0),
              0, //((pPi->feature_flags & PI_FEATURE_SUBTITLES) ? 1 : 0),
              0, //((pPi->no_themes > 0) ? pPi->themes[0] : 0),
              0, //((pPi->no_themes > 1) ? pPi->themes[1] : 0),
              0, //((pPi->no_themes > 2) ? pPi->themes[2] : 0),
              0, //((pPi->no_themes > 3) ? pPi->themes[3] : 0),
              0, //((pPi->no_themes > 4) ? pPi->themes[4] : 0),
              0, //((pPi->no_themes > 5) ? pPi->themes[5] : 0),
              0, //((pPi->no_themes > 6) ? pPi->themes[6] : 0),
              Tcl_GetString(objv[1]), //((char*) (pPi->off_title != 0) ? PI_GET_TITLE(pPi) : (uchar *) "")
              ""
      );
      WintvSharedMem_SetEpgInfo(comm, strlen(comm) + 1, chanQueryIdx, TRUE);
#else
      uint  cni;

      if (pVbiBuf->buf[0].cnis[CNI_TYPE_VPS].haveCni)
         cni = pVbiBuf->buf[0].cnis[CNI_TYPE_VPS].outCni;
      else if (pVbiBuf->buf[0].cnis[CNI_TYPE_PDC].haveCni)
         cni = pVbiBuf->buf[0].cnis[CNI_TYPE_PDC].outCni;
      else if (pVbiBuf->buf[0].cnis[CNI_TYPE_NI].haveCni)
         cni = pVbiBuf->buf[0].cnis[CNI_TYPE_NI].outCni;
      else
         cni = 0;

      TvSimu_CreateFakeAiAndPi(cni, start_time, stop_time, Tcl_GetString(objv[1]));

      Xawtv_SendEpgOsd(pFakePi);
      //eval_check(interp, "C_Tvapp_ShowEpg");
#endif
      result = TCL_OK;
   }
   return result;
}

#ifdef WIN32
// ----------------------------------------------------------------------------
// Callback to send a command to TV app
//
static int TclCb_SendTvCmd(ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[])
{
   const char * const pUsage = "Usage: C_Tvapp_SendCmd <command> [<args> [<...>]]";
   Tcl_DString *pass_dstr, *tmp_dstr;
   char * pass;
   int idx, len;
   int  result;

   if (argc < 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      // sum up the total length of all parameters, including terminating 0-Bytes
      pass_dstr = xmalloc(sizeof(Tcl_DString) * argc);  // allocate one too many
      //dprintf0("SendCmd: ");
      len = 0;
      for (idx = 1; idx < argc; idx++)
      {
         tmp_dstr = pass_dstr + idx - 1;
         // convert Tcl internal Unicode to Latin-1
         Tcl_UtfToExternalDString(NULL, argv[idx], -1, tmp_dstr);
         //dprintf1("%s ", Tcl_DStringValue(tmp_dstr));
         len += Tcl_DStringLength(tmp_dstr) + 1;
      }
      //dprintf0("\n");

      // concatenate the parameters into one char-array, separated by 0-Bytes
      pass = xmalloc(len);
      len = 0;
      pass[0] = 0;
      for (idx = 1; idx < argc; idx++)
      {
         tmp_dstr = pass_dstr + idx - 1;
         strcpy(pass + len, Tcl_DStringValue(tmp_dstr));
         len += strlen(pass + len) + 1;
         Tcl_DStringFree(tmp_dstr);
      }
      xfree(pass_dstr);

      // send the string to the TV app
      WintvSharedMem_SetEpgCommand(argc - 1, pass, len);

      xfree(pass);

      result = TCL_OK;
   }
   return result;
}
#endif // WIN32

// ---------------------------------------------------------------------------
// Dummy functions for winshmsrv.c
//
#ifdef WIN32
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
bool TtxDecode_GetCniAndPil( uint bufIdx, uint * pCni, uint * pPil, CNI_TYPE * pCniType,
                             uint pCniInd[CNI_TYPE_COUNT], uint pPilInd[CNI_TYPE_COUNT],
                             volatile EPGACQ_BUF * pThisVbiBuf )
{
   return FALSE;
}
void TtxDecode_NotifyChannelChange( uint bufIdx, volatile EPGACQ_BUF * pThisVbiBuf )
{
}

// ----------------------------------------------------------------------------
// Struct with callback functions for shared memory server module
//
static const WINSHMSRV_CB vbiRecCb =
{
   VbiRec_CbTvEvent,
   VbiRec_CbStationSelected,
   VbiRec_CbEpgQuery,
   VbiRec_CbTunerGrant,
   VbiRec_CbAttachTv
};
#endif

Tcl_Obj * TranscodeToUtf8( T_EPG_ENCODING enc, const char * pPrefix, const char * pStr, const char * pPostfix )
{
   assert((pPrefix == NULL) && (pPostfix == NULL));  // not used by xawtv.c
   // note: UTF-8 encoding not required since the value is just passed through
   return Tcl_NewStringObj(pStr, -1);
}

// ---------------------------------------------------------------------------
// Initialize the Tcl/Tk interpreter and load our scripts
//
static int ui_init( int argc, char **argv )
{
   char * args;

   // set up the default locale to be standard "C" locale so parsing is performed correctly
   setlocale(LC_ALL, "");
   setlocale(LC_NUMERIC, "C");  // required for Tcl or parsing of floating point numbers fails

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

   #ifndef WIN32
   Tcl_SetVar(interp, "x11_appdef_path", X11_APP_DEFAULTS, TCL_GLOBAL_ONLY);
   Tcl_SetVar2Ex(interp, "is_unix", NULL, Tcl_NewIntObj(1), TCL_GLOBAL_ONLY);
   Tcl_SetVar2Ex(interp, "is_icccm_proto", NULL, Tcl_NewBooleanObj(IPC_ICCCM_ENABLED), TCL_GLOBAL_ONLY);
   #else
   Tcl_SetVar2Ex(interp, "is_unix", NULL, Tcl_NewIntObj(0), TCL_GLOBAL_ONLY);
   Tcl_SetVar2Ex(interp, "is_icccm_proto", NULL, Tcl_NewBooleanObj(0), TCL_GLOBAL_ONLY);
   #endif

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
   if (TCL_EVAL_CONST(interp, (char*)tcl_libs_tcl_static) != TCL_OK)
   {
      debug1("tcl_libs_tcl_static error: %s\n", Tcl_GetStringResult(interp));
      debugTclErr(interp, "tcl_libs_tcl_static");
   }
   if (TCL_EVAL_CONST(interp, (char*)tk_libs_tcl_static) != TCL_OK)
   {
      debug1("tk_libs_tcl_static error: %s\n", Tcl_GetStringResult(interp));
      debugTclErr(interp, "tk_libs_tcl_static");
   }
   #endif
   if (TCL_EVAL_CONST(interp, (char*)combobox_tcl_dynamic) != TCL_OK)
   {
      debug1("combobox_tcl_dynamic error: %s\n", Tcl_GetStringResult(interp));
      debugTclErr(interp, "combobox_tcl_dynamic");
   }

   #ifdef DISABLE_TCL_BGERR
   // switch off Tcl/Tk background error reports for release version
   sprintf(comm, "proc bgerror foo {}\n");
   eval_check(interp, comm);
   #endif

   #ifdef WIN32
   // create an asynchronous event source that allows to receive triggers from the EPG message receptor thread
   asyncThreadHandler = Tcl_AsyncCreate(VbiRec_AsyncThreadHandler, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_SendCmd", TclCb_SendTvCmd, (ClientData) NULL, NULL);
   #else
   exitAsyncHandler = Tcl_AsyncCreate(AsyncHandler_AppTerminate, NULL);
   Tcl_CreateCommand(interp, "XawtvConfigShmChannelChange", Xawtv_CbStationChange, (ClientData) NULL, NULL);
   #endif
   Tcl_CreateObjCommand(interp, "C_SendEpgOsd", TclCb_SendEpgOsd, (ClientData) NULL, NULL);

   if (TCL_EVAL_CONST(interp, (char*)vbirec_gui_tcl_static) != TCL_OK)
   {
      debug1("vbirec_gui_tcl_static error: %s\n", Tcl_GetStringResult(interp));
      debugTclErr(interp, "vbirec_gui_tcl_static");
   }

   Tcl_TraceVar(interp, "dumpttx_enable", TCL_TRACE_WRITES|TCL_GLOBAL_ONLY, TclCb_EnableDump, NULL);

   #if defined (WIN32) && ((TCL_MAJOR_VERSION != 8) || (TCL_MINOR_VERSION >= 5))
   Tcl_CreateCommand(interp, "C_WinHandleShutdown", TclCbWinHandleShutdown, (ClientData) NULL, NULL);
   eval_check(interp, "wm protocol . WM_SAVE_YOURSELF C_WinHandleShutdown\n");
   #endif

   Tcl_ResetResult(interp);
   return (TRUE);
}

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
                   "Usage: %s [options]\n"
                   "       -help       \t\t: this message\n"
                   "       -geometry <geometry>\t: window position\n"
                   "       -iconic     \t\t: iconify window\n"
#ifndef WIN32
                   "       -card <digit>       \t: index of TV card for acq (starting at 0)\n"
                   "       -protocol xawtv|iccc\t: protocol for communication with EPG app\n"
#endif
                   , argv0, reason, argvn, argv0);
#if 0
      /*balance brackets for syntax highlighting*/  )
#endif
#ifdef WIN32
   MessageBox(NULL, comm, "VbiRec usage", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
#endif

   exit(1);
}

// ---------------------------------------------------------------------------
// Parse command line options
//
static void ParseArgv( int argc, char * argv[] )
{
   int argIdx = 1;

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
#ifndef WIN32
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
         else if (!strcmp(argv[argIdx], "-protocol"))
         {
            if (argIdx + 1 < argc)
            {
               if (strcmp(argv[argIdx + 1], "xawtv") == 0)
                  epgIpcProtocol = EPG_IPC_XAWTV;
               else if (strcmp(argv[argIdx + 1], "iccc") == 0)
                  epgIpcProtocol = EPG_IPC_ICCCM;
               else
                  Usage(argv[0], argv[argIdx+1], "invalid protocol keyword");
               argIdx += 2;
            }
            else
               Usage(argv[0], argv[argIdx], "missing card index after");
         }
#endif // WIN32
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
         Usage(argv[0], argv[argIdx], "unexpected argument");
   }
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

   // initialize Tcl/Tk interpreter and compile all scripts
   SetArgv(&argc, &argv);
   #endif
   ParseArgv(argc, argv);

   // mark Tcl/Tk interpreter as uninitialized
   interp = NULL;
   memset(&ttxStats, 0, sizeof(ttxStats));
   memset(&cniStats, 0, sizeof(cniStats));

   should_exit = FALSE;
   ui_init(argc, argv);

   #ifdef WIN32
   if (WintvSharedMem_Init(FALSE))
   #endif
   {
      #ifdef WIN32
      #if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION <= 4)
      // set up callback to catch shutdown messages (requires tk83.dll patch!)
      Tk_RegisterMainDestructionHandler(WinApiDestructionHandler);
      #endif
      #ifndef __MINGW32__
      __try {
      #else
      SetUnhandledExceptionFilter(WinApiExceptionHandler);
      #endif

      WintvSharedMem_SetCallbacks(&vbiRecCb);
      if (WintvSharedMem_StartStop(TRUE, NULL))

      #else  // not WIN32
      signal(SIGINT, signal_handler);
      signal(SIGTERM, signal_handler);
      signal(SIGHUP, signal_handler);
      BtDriver_Init();
      eval_check(interp, "set xawtvcf {tunetv 1 follow 0 dopop 0 poptype 0 duration 7}");
      eval_check(interp, "proc CreateTuneTvButton {} {}\n");
      eval_check(interp, "proc RemoveTuneTvButton {} {}\n");
      Xawtv_Init(NULL, NULL);
      // pass driver parameters to the driver
      BtDriver_Configure(videoCardIndex, BTDRV_SOURCE_DVB, 0 /*prio*/);
      //TODO BtDriver_TuneDvbPid(230);
      if ( BtDriver_StartAcq() )
      #endif
      {
         #ifdef WIN32
         pVbiBuf = WintvSharedMem_GetVbiBuf();
         #endif
         if (pVbiBuf != NULL)
         {
            // skip first VBI frame, reset ttx decoder, then set reader idx to writer idx
            pVbiBuf->buf[0].chanChangeReq = pVbiBuf->buf[0].chanChangeCnf = 0;
            pVbiBuf->buf[0].reader_idx = pVbiBuf->buf[0].writer_idx;

            // pass the configuration variables to the ttx process via IPC
            epgPageStart = pVbiBuf->startPageNo[0] = 0x300;
            epgPageStop = pVbiBuf->stopPageNo[0] = 0x399;
            sprintf(comm, "%03X-%03X", epgPageStart, epgPageStop);

            // enable acquisition in the slave process/thread
            pVbiBuf->buf[0].chanChangeReq = pVbiBuf->buf[0].chanChangeCnf + 2;
            pVbiBuf->ttxEnabled = TRUE;
            pVbiBuf->buf[0].ttxHeader.op_mode = EPGACQ_TTX_HEAD_DEC;

            #ifdef WIN32
            VbiRec_CbStationSelected();
            #endif

            // trigger second event for the first time to install the timer
            SecTimerEvent(NULL);

            // set window title & disable resizing
            eval_check(interp, "wm title . {VBI recorder " TVSIM_VERSION_STR "}\nwm resizable . 0 0\n");
            if (startIconified)
               eval_check(interp, "wm iconify .");

            // wait until window is open and everything displayed
            while ( (Tk_GetNumMainWindows() > 0) && (should_exit == FALSE) &&
                    Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT) )
               ;

            // process GUI events & callbacks until the main window is closed
            while ((Tk_GetNumMainWindows() > 0) && (should_exit == FALSE))
            {
               Tcl_DoOneEvent(TCL_ALL_EVENTS);
            }

            if (clockHandler != NULL)
               Tcl_DeleteTimerHandler(clockHandler);

            Tcl_UntraceVar(interp, "dumpttx_enable", TCL_TRACE_WRITES|TCL_GLOBAL_ONLY, TclCb_EnableDump, NULL);
            if (fdTtxFile != -1)
               close(fdTtxFile);

            #ifndef WIN32
            BtDriver_StopAcq();
            #endif
         }
      }
      else
      #ifdef WIN32
      {  // Win32: failed to setup shared memory
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
      #else // !WIN32
      {  // failed to start VBI acquisition -> display error message
         const char * pErrStr;
         strcpy(comm, "tk_messageBox -type ok -icon error -parent . -message {Failed to start acquisition: ");
         pErrStr = BtDriver_GetLastError();
         if (pErrStr != NULL)
            strcat(comm, pErrStr);
         else
            strcat(comm, "(Internal error, please report. Try to restart the application.)\n");
         strcat(comm, "}\n");
         eval_check(interp, comm);
      }
      #endif

      #if defined(WIN32) && !defined(__MINGW32__)
      }
      __except (EXCEPTION_EXECUTE_HANDLER)
      {  // caught a fatal exception -> free IPC resources
         WinApiExceptionHandler(NULL);
      }
      #endif

      #ifdef WIN32
      WintvSharedMem_Exit();
      #else
      BtDriver_Exit();
      Xawtv_Destroy();
      #endif
   }

   exit(0);
   return (0);
}

