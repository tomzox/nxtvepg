/*
 *  TV application interaction simulator for nxtvepg
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *    This module is the main module of the small standalone tool "tvsim".
 *    It's a demonstration of how nxtvepg can interact with TV applications.
 *    Currently supported is a shared memory protocol for WIN32 (implemented
 *    in K!TV) a X11 atoms based protocol for Xawtv (UNIX)
 */

#define DEBUG_SWITCH DEBUG_SWITCH_TVSIM
//#define DPRINTF_OFF

#ifdef WIN32
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <locale.h>
#ifndef WIN32
#include <sys/types.h>
#include <sys/ioctl.h>
#endif

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "epgvbi/ttxdecode.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbmerge.h"
#include "epgctl/epgacqctl.h"
#include "epgui/xiccc.h"
#include "epgui/epgmain.h"
#include "epgui/cmdline.h"
#include "epgui/rcfile.h"
#include "epgui/wintvcfg.h"
#include "epgui/wintvui.h"
#include "epgtcl/dlg_hwcfg.h"
#include "tvsim/winshmclnt.h"
#include "tvsim/tvsim_gui.h"
#include "tvsim/tvsim_version.h"

#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <machine/ioctl_bt848.h>
#elif defined(__linux__)
#include <linux/videodev2.h>
#endif

// prior to 8.4 there's a SEGV when evaluating const scripts (Tcl tries to modify the string)
#if (TCL_MAJOR_VERSION > 8) || ((TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4))
# define TCL_EVAL_CONST(INTERP, SCRIPT) Tcl_EvalEx(INTERP, SCRIPT, -1, TCL_EVAL_GLOBAL)
#else
# define TCL_EVAL_CONST(INTERP, SCRIPT) Tcl_VarEval(INTERP, "uplevel #0 {", (char *) SCRIPT, "}", NULL)
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

Tcl_Encoding encIso88591 = NULL;

#ifndef WIN32
static Window toplevel_wid = None;

// variables used for XAWTV protocol
static Atom xawtv_station_atom = None;
static Atom xawtv_remote_atom = None;

static Tcl_AsyncHandler asyncIcccmHandler = NULL;
static XICCC_STATE xiccc;
#if 0
static Atom icccm_atom_manager = None;
#endif

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

static Tcl_AsyncHandler asyncThreadHandler = NULL;
static Tcl_TimerToken   popDownEvent       = NULL;
static bool             haveIdleHandler    = FALSE;

// Default TV card index - identifies which TV card is used by the simulator
// (note that in contrary to the other TV card parameters this value is not
// taken from the nxtvepg rc/ini file; can be overriden with -card option)
#define TVSIM_CARD_IDX   0
// input source index of the TV tuner with btdrv4win.c
#define TVSIM_INPUT_IDX  0

// command line options
static const char * rcfile = NULL;
static bool isDefaultRcfile = TRUE;
static bool withoutDevice = FALSE;
static uint videoCardIndex = TVSIM_CARD_IDX;
static bool startIconified = FALSE;
#ifndef WIN32
static EPG_IPC_PROTOCOL epgIpcProtocol = EPG_IPC_BOTH;
#endif

typedef struct
{
   char       * pNetName;
   uchar        net_idx;
   time_t       start_time;
   time_t       stop_time;
   uint         pil;
   uchar        prat;
   uchar        erat;
   uchar        erat_max;
   char       * pSound;
   bool         is_wide;
   bool         is_palplus;
   bool         is_digital;
   bool         is_encrypted;
   bool         is_live;
   bool         is_repeat;
   bool         is_subtitled;
   char       * pThemes[8];  // PI_MAX_THEME_COUNT
   char       * pTitle;
   char       * pDescription;
} EPG_PI;

#ifndef WIN32
/* same as ioctl(), but repeat if interrupted */
#define IOCTL(fd, cmd, data)                                            \
({ int __result; do __result = ioctl(fd, cmd, data);                    \
   while ((__result == -1L) && (errno == EINTR)); __result; })

static int video_fd = -1;
#endif


// forward declarations
static void TvSimu_ParseRemoteCmd( char * argStr, uint argc );

static void TvSimu_DisplayPiDescription( const char * pText )
{
   Tcl_Obj * objv[2];

   objv[0] = Tcl_NewStringObj("DisplayPiDescription", -1);
   objv[1] = Tcl_NewStringObj(pText, -1);
   Tcl_IncrRefCount(objv[0]);
   Tcl_IncrRefCount(objv[1]);

   if (Tcl_EvalObjv(interp, 2, objv, 0) != TCL_OK)
      debugTclErr(interp, "TvSimu-DisplayPiDescription");

   Tcl_DecrRefCount(objv[0]);
   Tcl_DecrRefCount(objv[1]);
}

#ifndef DPRINTF_OFF
// ---------------------------------------------------------------------------
// Break up a TAB separated PI Info string into C structure
// - the given string is modified
// - warning: strings are references into the text
//   hence only valid as long as the text is not freed
//
static bool TvSimu_ParsePiDescription( char * pText, EPG_PI * pi )
{
   char * pOrigText = pText;
   char * pEnd;
   struct tm t;
   int int_val, int_val2, nscan, scan_pos;
   uint oldMoD;
   uint idx;

   memset(pi, 0, sizeof(*pi));

   pEnd = strchr(pText, '\t');
   if (pEnd == NULL) goto error;
   pi->pNetName = pText;
   *pEnd = 0;
   pText = pEnd + 1;

   nscan = sscanf(pText, "%d\t%n", &int_val, &scan_pos);
   if (nscan < 1) goto error;
   pi->net_idx = int_val;
   pText += scan_pos;

   nscan = sscanf(pText, "%u-%u-%u\t%u:%u:00\t%n",
                         &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &scan_pos);
   if (nscan < 5) goto error;
   t.tm_year -= 1900;
   t.tm_mon -= 1;
   t.tm_isdst = -1;
   t.tm_sec = 0;
   oldMoD = t.tm_hour*60 + t.tm_min;
   pi->start_time = mktime(&t);
   pText += scan_pos;

   nscan = sscanf(pText, "%u:%u:00\t%n", &t.tm_hour, &t.tm_min, &scan_pos);
   if (nscan < 2) goto error;
   t.tm_isdst = -1;
   t.tm_sec = 0;
   if (oldMoD < t.tm_hour*60 + t.tm_min)
      t.tm_mday += 1;
   pi->stop_time = mktime(&t);
   pText += scan_pos;

   if (strncmp(pText, "\\N\t", 3) == 0)
   {
      pi->pil = INVALID_VPS_PIL;
      pText += 3;
   }
   else
   {
      nscan = sscanf(pText, "%04u-%02u-%02u %02u:%02u:00\t%n",
                            &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &scan_pos);
      if (nscan < 5) goto error;
      pi->pil = (t.tm_mday << 15) | (t.tm_mon << 11) | (t.tm_hour << 6) | t.tm_min;
      pText += scan_pos;
   }

   if (strncmp(pText, "\\N\t", 3) == 0)
   {
      pi->prat = 255;  // PI_PARENTAL_UNDEFINED
      pText += 3;
   }
   else
   {
      nscan = sscanf(pText, "%d\t%n", &int_val, &scan_pos);
      if (nscan < 1) goto error;
      pi->prat = int_val;
      pText += scan_pos;
   }

   if (strncmp(pText, "\\N\t\\N\t", 6) == 0)
   {
      pi->erat = 255;  // PI_EDITORIAL_UNDEFINED
      pText += 6;
   }
   else
   {
      nscan = sscanf(pText, "%d\t%d\t%n", &int_val, &int_val2, &scan_pos);
      if (nscan < 2) goto error;
      pi->erat = int_val;
      pi->erat_max = int_val2;
      pText += scan_pos;
   }

   pEnd = strchr(pText, '\t');
   if (pEnd == NULL) goto error;
   pi->pSound = pText;
   *pEnd = 0;
   pText = pEnd + 1;

   nscan = sscanf(pText, "%d\t%n", &int_val, &scan_pos);
   if (nscan < 1) goto error;
   pi->is_wide = int_val;
   pText += scan_pos;

   nscan = sscanf(pText, "%d\t%n", &int_val, &scan_pos);
   if (nscan < 1) goto error;
   pi->is_palplus = int_val;
   pText += scan_pos;

   nscan = sscanf(pText, "%d\t%n", &int_val, &scan_pos);
   if (nscan < 1) goto error;
   pi->is_encrypted = int_val;
   pText += scan_pos;

   nscan = sscanf(pText, "%d\t%n", &int_val, &scan_pos);
   if (nscan < 1) goto error;
   pi->is_live = int_val;
   pText += scan_pos;

   nscan = sscanf(pText, "%d\t%n", &int_val, &scan_pos);
   if (nscan < 1) goto error;
   pi->is_repeat = int_val;
   pText += scan_pos;

   nscan = sscanf(pText, "%d\t%n", &int_val, &scan_pos);
   if (nscan < 1) goto error;
   pi->is_subtitled = int_val;
   pText += scan_pos;

   for (idx = 0; idx < 8; idx++)  //PI_MAX_THEME_COUNT
   {
      pEnd = strchr(pText, '\t');
      if (pEnd == NULL) goto error;
      pi->pThemes[idx] = pText;
      *pEnd = 0;
      pText = pEnd + 1;
   }

   pEnd = strchr(pText, '\t');
   if (pEnd == NULL) goto error;
   pi->pTitle = pText;
   *pEnd = 0;
   pText = pEnd + 1;

   pEnd = strchr(pText, '\n');
   if (pEnd == NULL) goto error;
   pi->pDescription = pText;
   *pEnd = 0;
   pText = pEnd + 1;

   return TRUE;

error:
   idx = pText - pOrigText;
   // undo substitutions
   for ( ; pText >= pOrigText; pText--)
   {
      if (*pText == 0)
         *pText = '\t';
   }
   debug3("TvSimu-ParsePiDescription: parse error at offset %d: '%s' in %s", idx, pOrigText + idx, pOrigText);
   return FALSE;
}
#endif


// dummies for wintvui.c
#include "epgdb/epgdbfil.h"
#include "epgui/epgsetup.h"
#include "epgui/uictrl.h"
bool EpgSetup_AcquisitionMode( NETACQ_SET_MODE netAcqSetMode ) { return FALSE; }
void EpgAcqCtl_Stop( void ) {}
void UiControlMsg_AcqEvent( ACQ_EVENT acqEvent ) {}

#ifndef WIN32
// ---------------------------------------------------------------------------
// Get X11 display for the TV app
// - usually the same as the display of the nxtvepg GUI
// - can be modified via command line switch (e.g. multi-headed PC)
//
static Display * TvSim_GetTvDisplay( void )
{
   Tk_Window   tkwin;
   Display   * dpy = NULL;

   tkwin = Tk_MainWindow(interp);
   if (tkwin != NULL)
   {
      dpy = Tk_Display(tkwin);

      if (dpy == NULL)
         debug0("TvSim-GetTvDisplay: failed to determine display");
   }
   else
      debug0("TvSim-GetTvDisplay: failed to determine main window");

   return dpy;
}

// ---------------------------------------------------------------------------
// Query the station name from the xawtv property
// - when the remote EPG app wants up to execute a command, it assigns a command
//   string to the atom
// - note the command string is not cleared after it's processed; we rely on
//   event triggers by the X server to execute new commands
//
static int Xawtv_QueryRemoteCommand( Window wid, Atom property, char * pBuffer, int bufLen )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;
   Atom  type;
   int   format;
   ulong nitems, bytesafter;
   uchar * args;
   int idx;
   int result = 0;

   dpy = TvSim_GetTvDisplay();
   if (dpy != NULL)
   {
      // push an /dev/null handler onto the stack that catches any type of X11 error events
      errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) NULL, (ClientData) NULL);

      args = NULL;
      if ( (XGetWindowProperty(dpy, wid, property, 0, (65536 / sizeof (long)), False, XA_STRING,
                               &type, &format, &nitems, &bytesafter, &args) == Success) && (args != NULL) )
      {
         if (nitems >= bufLen)
            nitems = bufLen - 1;
         memcpy(pBuffer, args, nitems);
         pBuffer[bufLen - 1] = 0;
         XFree(args);

         dprintf1("Xawtv-QueryRemoteCommand: RECV %s", pBuffer);
         result = 0;
         for (idx=0; idx < nitems; idx++)
         {
            if (pBuffer[idx] == 0)
            {
               result += 1;

               if (idx + 1 < nitems)
                  dprintf1(" %s", pBuffer + idx + 1);
            }
         }
         dprintf1(" (#%d)\n", result);
      }
      else
         dprintf0("Xawtv-QueryRemoteCommand: failed to read property\n");

      // remove the dummy error handler
      Tk_DeleteErrorHandler(errHandler);
   }
   else
      debug0("No Tk display available");

   return result;
}

// ----------------------------------------------------------------------------
// Process events triggered by EPG application
//
static void TvSimu_IdleHandler( ClientData clientData )
{
   char buf[1000];
   int argc;

   argc = Xawtv_QueryRemoteCommand(toplevel_wid, xawtv_remote_atom, buf, sizeof(buf));
   if (argc > 0)
   {
      TvSimu_ParseRemoteCmd(buf, argc);
   }
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

// ----------------------------------------------------------------------------
// ICCM protocol
//

static void TvSimu_IcccmAttach( bool attach )
{
   // update the connection indicator in the GUI
   sprintf(comm, "ConnectEpg %d\n", attach);
   eval_check(interp, comm);
}

static void TvSimu_IcccmIdleHandler( ClientData clientData )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;
   Atom  type;
   int   format;
   ulong nitems, bytesafter;
   uchar * args;

   dprintf1("TvSimu-IcccmIdleHandler: event mask 0x%X\n", xiccc.events);

   // internal event handler must be called first - may generate application level events
   if ( IS_XICCC_INTERNAL_EVENT(xiccc.events) )
   {
      Xiccc_HandleInternalEvent(&xiccc);
   }

   if (xiccc.events & XICCC_LOST_PEER)
   {
      xiccc.events &= ~XICCC_LOST_PEER;
      TvSimu_IcccmAttach(FALSE);
   }
   if (xiccc.events & XICCC_NEW_PEER)
   {
      xiccc.events &= ~XICCC_NEW_PEER;
      TvSimu_IcccmAttach(TRUE);
   }
   if (xiccc.events & XICCC_GOT_MGMT)
   {
      xiccc.events &= ~XICCC_GOT_MGMT;
   }
   if (xiccc.events & XICCC_LOST_MGMT)
   {
      xiccc.events &= ~XICCC_LOST_MGMT;
   }

   if ( (xiccc.events & XICCC_SETSTATION_REPLY) && (xiccc.remote_manager_wid != None) )
   {
      xiccc.events &= ~XICCC_SETSTATION_REPLY;

      dpy = TvSim_GetTvDisplay();
      if (dpy != NULL)
      {
         // push an /dev/null handler onto the stack that catches any type of X11 error events
         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) NULL, (ClientData) NULL);

         args = NULL;
         if ( (XGetWindowProperty(dpy, xiccc.manager_wid, xiccc.atoms._NXTVEPG_SETSTATION_RESULT, 0, (65536 / sizeof (long)), False, XA_STRING,
                                  &type, &format, &nitems, &bytesafter, &args) == Success) && (args != NULL) )
         {
            if (popDownEvent != NULL)
               Tcl_DeleteTimerHandler(popDownEvent);
            popDownEvent = NULL;

            TvSimu_DisplayPiDescription((char*)args);

#ifndef DPRINTF_OFF
            if (args[0] != 0)
            {
               EPG_PI epg_pi;
               if (TvSimu_ParsePiDescription((char*)args, &epg_pi))
                  dprintf2("TvSimuMsg-IcccmIdleHandler: RECV NETNAME:%s TITLE:%s\n", epg_pi.pNetName, epg_pi.pTitle);
            }
#endif
            XFree(args);
         }
         else
            dprintf2("TvSimu-IcccmIdleHandler: failed to read property 0x%X on wid 0x%X\n", (int)xiccc.atoms._NXTVEPG_SETSTATION_RESULT, (int)xiccc.manager_wid);

         // remove the dummy error handler
         Tk_DeleteErrorHandler(errHandler);
      }
      else
         debug0("No Tk display available");
   }

   if (xiccc.events & XICCC_REMOTE_REQ)
   {
      XICCC_EV_QUEUE * pReq;
      char buf[4000];
      int argc;

      xiccc.events &= ~XICCC_REMOTE_REQ;

      dpy = TvSim_GetTvDisplay();
      if (dpy != NULL)
      {
         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) NULL, (ClientData) NULL);

         while (xiccc.pRemoteCmdQueue != NULL)
         {
            pReq = xiccc.pRemoteCmdQueue;
            Xiccc_QueueUnlinkEvent(&xiccc.pRemoteCmdQueue, pReq);

            argc = Xawtv_QueryRemoteCommand(pReq->requestor, xiccc.atoms._NXTVEPG_REMOTE, buf, sizeof(buf));
            if (argc > 0)
            {
               TvSimu_ParseRemoteCmd(buf, argc);

               Xiccc_SendReply(&xiccc, "OK", -1, pReq, xiccc.atoms._NXTVEPG_SETSTATION);
            }
            xfree(pReq);
         }
         Tk_DeleteErrorHandler(errHandler);
      }
   }
}

static int TvSimu_AsyncIcccmHandler( ClientData clientData, Tcl_Interp *interp, int code )
{
   Tcl_DoWhenIdle(TvSimu_IcccmIdleHandler, NULL);
   return code;
}

// ----------------------------------------------------------------------------
// X event handler
// - this function is called for every incoming X event
// - filters out events on the pseudo xawtv main window
//
static int Xawtv_EventNotification( ClientData clientData, XEvent *eventPtr )
{
   bool needHandler;
   bool result = FALSE;

   if (IPC_XAWTV_ENABLED)
   {
      if ( (eventPtr->type == PropertyNotify) &&
           (eventPtr->xproperty.window == toplevel_wid) && (toplevel_wid != None) )
      {
         dprintf1("PropertyNotify event from window 0x%X\n", (int)eventPtr->xproperty.window);
         if (eventPtr->xproperty.atom == xawtv_remote_atom)
         {
            if (eventPtr->xproperty.state == PropertyNewValue)
            {  // the remote command text has changed
               // install event at top of Tcl event loop
               Tcl_AsyncMark(asyncThreadHandler);
            }
         }
         result = TRUE;
      }
   }
   if (IPC_ICCCM_ENABLED)
   {
      if ( Xiccc_XEvent(eventPtr, &xiccc, &needHandler) )
      {
         if (needHandler)
         {
            Tcl_AsyncMark(asyncIcccmHandler);
         }
         result = TRUE;
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Update X atom with name and frequency of currently tuned station
// - should be called after every channel change
// - remote EPG app can use this to detect channel changes and should reply with EPG info
//
static void TvSim_XawtvSetStation( int freq, const char *channel, const char *name )
{
   Display   * dpy;
   int  len;
   char line[80];

   if (toplevel_wid != None)
   {
      dpy = TvSim_GetTvDisplay();
      if (dpy != NULL)
      {
         len  = sprintf(line, "%.3f", (float)freq/16)+1;
         len += sprintf(line+len, "%s", channel ? channel : "?") +1;
         len += sprintf(line+len, "%s", name    ? name    : "?") +1;

         XChangeProperty(dpy, toplevel_wid, xawtv_station_atom,
                         XA_STRING, 8, PropModeReplace,
                         (uchar*)line, len);
      }
      else
         debug0("TvSim-XawtvSetStation: display not defined");
   }
   else
      debug0("TvSim-XawtvSetStation: main window not defined");
}

// ----------------------------------------------------------------------------
// Find the parent of the main window which holds the WM_CLASS variable
// - required because Xawtv atoms must placed onto the same window
//
static Window Xawtv_QueryParent( Display * dpy, Window wid )
{
   Window root_wid, root_ret, parent, *kids;
   Atom   wm_class_atom;
   uint   nkids;
   uchar *args;
   Atom type;
   int format;
   ulong nitems, bytesafter;
   Window result = None;

   root_wid = RootWindowOfScreen(DefaultScreenOfDisplay(dpy));
   wm_class_atom = XInternAtom(dpy, "WM_CLASS", False);
   kids = NULL;

   while ( (wid != root_wid) &&
           XQueryTree(dpy, wid, &root_ret, &parent, &kids, &nkids))
   {
      dprintf3("Xawtv-QueryParent: wid=0x%lX parent=0x%lx root=0x%lx\n", wid, parent, root_ret);
      if (kids != NULL)
         XFree(kids);

      args = NULL;
      if ( (XGetWindowProperty(dpy, wid, wm_class_atom, 0, 64, False, AnyPropertyType,
                               &type, &format, &nitems, &bytesafter, &args) == Success) && (args != NULL) )
      {
         dprintf2("Xawtv-QueryParent: wid=0x%lX class=%s\n", wid, args);
         XFree(args);

         result = wid;
         break;
      }
      // continue search with parent's parent
      wid = parent;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Create Xawtv pseudo window for communication with EPG app
// - Xawtv is simulating by setting the wm class atom
// - assigns 2 atoms for communication: "xawtv station" to notify EPG app about
//   channel changes, and "xawtv remote" to accept remote commands
//
static void TvSim_XawtvCreateAtoms( void )
{
   Tk_ErrorHandler errHandler;
   Tk_Window  mainWindow;
   Display   * dpy;

   mainWindow = Tk_MainWindow(interp);
   if (mainWindow != NULL)
   {
      dpy = TvSim_GetTvDisplay();
      if (dpy != NULL)
      {
         toplevel_wid = Xawtv_QueryParent(dpy, Tk_WindowId(mainWindow));
         if (toplevel_wid != None)
         {
            Tk_CreateGenericHandler(Xawtv_EventNotification, NULL);

            // create atoms for communication with TV client
            if (IPC_XAWTV_ENABLED)
            {
               xawtv_station_atom = XInternAtom(dpy, "_XAWTV_STATION", False);
               xawtv_remote_atom = XInternAtom(dpy, "_XAWTV_REMOTE", False);

               TvSim_XawtvSetStation(0, "1", "ARD");
            }
            if (IPC_ICCCM_ENABLED)
            {
               char * pIdArgv;
               uint   idLen;

               errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) NULL, (ClientData) NULL);

               Xiccc_BuildArgv(&pIdArgv, &idLen, ICCCM_PROTO_VSTR, "tvsim", TVSIM_VERSION_STR, "", NULL);
               Xiccc_Initialize(&xiccc, dpy, FALSE, pIdArgv, idLen);
               xfree(pIdArgv);

               Xiccc_ClaimManagement(&xiccc, FALSE);
               Xiccc_SearchPeer(&xiccc);

               Tk_DeleteErrorHandler(errHandler);

               if (xiccc.events != 0)
                  Tcl_DoWhenIdle(TvSimu_IcccmIdleHandler, NULL);
            }
         }
         else
            debug0("TvSim-XawtvCreateAtoms: no toplevel window");
      }
      else
         debug0("TvSim-XawtvCreateAtoms: no display");
   }
   else
      debug0("TvSim-XawtvCreateAtoms: no main win");
}

// ---------------------------------------------------------------------------
// Replacement and dummy functions for channel switching
// - nxtvepg's btdrv4linux is not used because VBI decoding is not required here
//
bool BtDriver_Init( void )
{
   return TRUE;
}

bool BtDriver_StartAcq( void )
{
   char devName[32];
#if !defined(__NetBSD__) && !defined(__FreeBSD__)
   static const char * pLastDevPath = NULL;
   const char * pDevPath;
   uint try;

   for (try = 0; try < 3; try++)
   {
      if (pLastDevPath != NULL)
         pDevPath = pLastDevPath;
      else if (try <= 1)
         pDevPath = "/dev";
      else
         pDevPath = "/dev/v4l";

      sprintf(devName, "%s/video%u", pDevPath, videoCardIndex);

      if (access(devName, R_OK) == 0)
      {
         pLastDevPath = pDevPath;
         break;
      }
   }
   dprintf1("BtDriver-StartAcq: set device path %s\n", pDevPath);

#else  // NetBSD
   sprintf(devName, "/dev/tuner%u", videoCardIndex);
#endif

   video_fd = open(devName, O_RDONLY);
   if (video_fd == -1)
   {
      if (errno == EBUSY)
         fprintf(stderr, "video input device %s is busy (-> close all video apps)\n", devName);
      else
         fprintf(stderr, "Failed to open %s: %s\n", devName, strerror(errno));
   }

   return (video_fd >= 0);
}

void BtDriver_StopAcq( void )
{
   if (video_fd != -1)
   {
      close(video_fd);
      video_fd = -1;
   }
}


bool BtDriver_Configure( int sourceIdx, int drvType, int prio)
{
   return TRUE;
}

void BtDriver_SelectSlicer( VBI_SLICER_TYPE slicerType )
{
}

void BtDriver_Exit( void )
{
}

// ---------------------------------------------------------------------------
// Set the input channel and tune a given frequency and norm
// - input source is only set upon the first call when the device is kept open
//   also note that the isTuner flag is only set upon the first call
// - note: assumes that VBI device is opened before
//
bool BtDriver_TuneChannel( int inputIdx, const EPGACQ_TUNER_PAR * pFreqPar, bool dummy, bool * pIsTuner )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   bool result = FALSE;

   *pIsTuner = TRUE;

   if (video_fd != -1)
   {
      struct v4l2_frequency vfreq2;
      struct v4l2_input v4l2_desc_in;
      uint32_t v4l2_inp;
      v4l2_std_id vstd2;

      v4l2_inp = inputIdx;
      if (IOCTL(video_fd, VIDIOC_S_INPUT, &v4l2_inp) == 0)
      {
         switch (pFreqPar->norm)
         {
            case EPGACQ_TUNER_NORM_PAL:
            default:
               vstd2 = V4L2_STD_PAL;
               break;
            case EPGACQ_TUNER_NORM_NTSC:
               vstd2 = V4L2_STD_NTSC;
               break;
            case EPGACQ_TUNER_NORM_SECAM:
               vstd2 = V4L2_STD_SECAM;
               break;
         }
         if (IOCTL(video_fd, VIDIOC_S_STD, &vstd2) == 0)
         {
            memset(&v4l2_desc_in, 0, sizeof(v4l2_desc_in));
            v4l2_desc_in.index = inputIdx;
            if (IOCTL(video_fd, VIDIOC_ENUMINPUT, &v4l2_desc_in) == 0)
            {
               *pIsTuner = ((v4l2_desc_in.type & V4L2_INPUT_TYPE_TUNER) != 0);

               if (*pIsTuner)
               {
                  memset(&vfreq2, 0, sizeof(vfreq2));
                  if (IOCTL(video_fd, VIDIOC_G_FREQUENCY, &vfreq2) == 0)
                  {
                     vfreq2.frequency = pFreqPar->freq;
                     if (IOCTL(video_fd, VIDIOC_S_FREQUENCY, &vfreq2) == 0)
                     {
                        dprintf1("BtDriver-TuneChannel: set to %.2f\n", (double)pFreqPar->freq/16);

                        result = TRUE;
                     }
                     else
                        fprintf(stderr, "Failed to set tuner frequency (ioctl VIDIOC_S_FREQUENCY): %s\n", strerror(errno));
                  }
                  else
                     fprintf(stderr, "Failed to query tuner frequency (ioctl VIDIOC_G_FREQUENCY): %s\n", strerror(errno));
               }
               else
                  result = TRUE;
            }
            else
               debug2("BtDriver-SetInputSource: v4l2 VIDIOC_ENUMINPUT #%d error: %s", inputIdx, strerror(errno));
         }
         else
            fprintf(stderr, "Failed to set input norm (ioctl VIDIOC_S_STD): %s\n", strerror(errno));
      }
      else
         fprintf(stderr, "Failed to set video input source (ioctl VIDIOC_S_INPUT): %s\n", strerror(errno));
   }

#else // __NetBSD__ || __FreeBSD__
   bool result = FALSE;

   //if (BtDriver_SetInputSource(inputIdx, pFreqPar->norm, pIsTuner))
   {
      if ( (*pIsTuner) && (pFreqPar->freq != 0) )
      {
         if (video_fd != -1)
         {
            // mute audio
            int mute_arg = AUDIO_MUTE;
            if (ioctl (video_fd, BT848_SAUDIO, &mute_arg) == 0)
            {
               dprintf0("Muted tuner audio.\n");
            }
            else
               fprintf(stderr, "muting audio (ioctl AUDIO_MUTE): %s\n", strerror(errno));

            // Set the tuner frequency
            if(ioctl(video_fd, TVTUNER_SETFREQ, &pFreqPar->freq) == 0)
            {
               dprintf1("Vbi-TuneChannel: set to %.2f\n", (double)freq/16);

               result = TRUE;
            }
            else
               fprintf(stderr, "setting tuner frequency (ioctl TVTUNER_SETFREQ): %s\n", strerror(errno));
         }
      }
   }
#endif // __NetBSD__ || __FreeBSD__
   return result;
}


#else // WIN32
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
      MessageBox(NULL, buf, "TV App Simulator", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);

      free(buf);
   }
}
#endif // COMPILE_UNUSED_CODE

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
      BtDriver_Exit();
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
      if (argv0[len] == '\\')
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
#endif  // WIN32

// ---------------------------------------------------------------------------
// Helper function to convert text content into UTF-8
// - copied from epgmain.c
//
Tcl_Obj * TranscodeToUtf8( T_EPG_ENCODING enc,
                           const char * pPrefix, const char * pStr, const char * pPostfix )
{
   Tcl_Obj * pObj;
   Tcl_DString dstr;

   assert(pStr != NULL);

   switch (enc)
   {
      case EPG_ENC_UTF8:
         Tcl_DStringInit(&dstr);
         Tcl_DStringAppend(&dstr, pStr, -1);
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

// ----------------------------------------------------------------------------
// Dummy for rcfile.c
// - (not needed because tvsim doesn't write rc file anyways)
// 
void CmdLine_AddRcFilePostfix( const char * pPostfix )
{
}

#ifndef WIN32
// ---------------------------------------------------------------------------
// Helper function for allocating a new string for a concatenation of strings
//
static char * CmdLine_ConcatPaths( const char * p1, const char * p2, const char * p3 )
{
   char * pStr;

   if (p3 != NULL)
   {
      pStr = (char*) xmalloc(strlen(p1) + 1 + strlen(p2) + 1 + strlen(p3) + 1);
      sprintf(pStr, "%s/%s/%s", p1, p2, p3);
   }
   else
   {
      pStr = (char*) xmalloc(strlen(p1) + 1 + strlen(p2) + 1);
      sprintf(pStr, "%s/%s", p1, p2);
   }
   return pStr;
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
                   "Usage: %s [options]\n"
                   "       -help       \t\t: this message\n"
                   "       -geometry <geometry>\t: window position\n"
                   "       -iconic     \t\t: iconify window\n"
                   "       -rcfile <path>      \t: path and file name of setup file\n"
                   "       -card <digit>       \t: index of TV card for acq (starting at 0)\n"
#ifndef WIN32
                   "       -protocol xawtv|iccc\t: protocol for communication with EPG app\n"
#endif
                   , argv0, reason, argvn, argv0);
#if 0
      /*balance brackets for syntax highlighting*/  )
#endif
#ifdef WIN32
   MessageBox(NULL, comm, "TV interaction simulator usage", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
#endif

   exit(1);
}

// ---------------------------------------------------------------------------
// Parse command line options
//
static void ParseArgv( int argc, char * argv[] )
{
   int argIdx = 1;

#ifdef WIN32
   SetWorkingDirectoryFromExe(argv[0]);
#endif

#ifndef WIN32
   // code copied from epgui/cmdline.c

   char * pEnvHome = getenv("HOME");

   char * pEnvConfig = getenv("XDG_CONFIG_HOME");
   if (pEnvConfig == NULL)
   {
      if (pEnvHome != NULL)
         rcfile = CmdLine_ConcatPaths(pEnvHome, ".config", "nxtvepg/nxtvepgrc");
      else
         rcfile = xstrdup("nxtvepgrc");
   }
   else
      rcfile = CmdLine_ConcatPaths(pEnvConfig, "nxtvepg/nxtvepgrc", NULL);
#else
   rcfile = xstrdup("nxtvepg.ini");
#endif

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
               xfree((void *) rcfile);
               rcfile = xstrdup(argv[argIdx + 1]);
               isDefaultRcfile = FALSE;
               argIdx += 2;
            }
            else
               Usage(argv[0], argv[argIdx], "missing file name after");
         }
         else if (!strcmp(argv[argIdx], "-noacq"))
         {
            withoutDevice = TRUE;
            argIdx += 1;
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
#ifndef WIN32
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

// ----------------------------------------------------------------------------
// Set the hardware config params
// - the parameters must be loaded before from the nxtvepg rc/ini file,
//   except for the TV card index, which is set by a command line switch only
//
static bool SetHardwareConfig( uint cardIdx )
{
   const RCFILE * pRc = RcFile_Query();
   uint drvType, prio;
#ifdef WIN32
   //uint slicer, input;
#endif
   bool result;

   drvType = pRc->tvcard.drv_type;
   //cardIdx = pRc->tvcard.card_idx;
   prio    = pRc->tvcard.acq_prio;
#ifdef WIN32
   //input   = pRc->tvcard.input;
   //slicer  = pRc->tvcard.slicer_type;

   // pass the hardware config params to the driver
   if (drvType == BTDRV_SOURCE_WDM)
   {
      result = BtDriver_Configure(cardIdx, drvType, prio);
   }
   else
   {
      dprintf2("SetHardwareConfig: card #%d not configured (have %d)\n", cardIdx, pRc->tvcard.winsrc_count);

      MessageBox(NULL, "Failed to load TV card configuration from nxtvepg INI file. "
                 "Configure this card first in nxtvepg's TV card input configuration dialog.",
                 "TV App. Interaction Simulator", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      result = FALSE;
   }
#else
   result = BtDriver_Configure(cardIdx, drvType, prio);
#endif
   return result;
}

// ---------------------------------------------------------------------------
// Parses and executes a command received by the EPG app
//
static void TvSimu_ParseRemoteCmd( char * argStr, uint argc )
{
   char * pArg2;

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
   else if (strcmp(argStr, "message") == 0)
   {
      if (argc == 2)
      {
         if (popDownEvent != NULL)
            Tcl_DeleteTimerHandler(popDownEvent);
         popDownEvent = NULL;

         sprintf(comm, "DisplayPiDescriptionSimple {%s} {} {}", pArg2);
         eval_check(interp, comm);
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

   sprintf(comm, "ClearPiDescription");
   eval_check(interp, comm);
}

#ifdef WIN32
// ---------------------------------------------------------------------------
// Dummy functions to satisfy the BT driver and EPG decoder module
//
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"
bool WintvSharedMem_ReqTvCardIdx( uint cardIdx, bool * pEpgHasDriver ) { return TRUE; }
void WintvSharedMem_FreeTvCard( void ) {}
bool WintvSharedMem_SetInputSrc( uint inputIdx ) { return FALSE; }
bool WintvSharedMem_SetTunerFreq( uint freq, uint norm ) { return FALSE; }
bool WintvSharedMem_GetTunerFreq( uint * pFreq, bool * pIsTuner ) { return FALSE; }
uint WintvSharedMem_GetInputSource( void ) { return 0; }
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
   uint  norm;
   bool  isTuner;

   if (WinSharedMemClient_GetInpFreq(&inputSrc, &freq, &norm))
   {
      if ( (inputSrc != EPG_REQ_INPUT_NONE) &&
           (freq != EPG_REQ_FREQ_NONE) )
      {
         EPGACQ_TUNER_PAR freqPar;
         freqPar.freq = freq;
         freqPar.norm = norm;

         // set the TV input source (tuner, composite, S-video) and frequency
         if ( BtDriver_TuneChannel(inputSrc, &freqPar, FALSE, &isTuner) )
         {
            BtDriver_SelectSlicer(VBI_SLICER_ZVBI);

            // inform EPG app that the frequency has been set (note: this function is a
            // variant of _SetStation which does not request EPG info for the new channel)
            WinSharedMemClient_SetInputFreq(isTuner, freq, norm);
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Shm message handler: EPG peer sent command vector
// - a command vector is a concatenation of null terminated strings
//   + the overall length including zeros is given as arglen
//   + the number of 0-terminated arguments is passed as first argument;
//     use of this parameter is DEPRECIATED: use length instead (fail-safety)
// - the first string in the vectors holds the command name;
//   the following ones optional arguments (depending on the command)
//
static void TvSimuMsg_HandleEpgCmd( void )
{
   uint argc;
   uint arglen;
   char argStr[EPG_CMD_MAX_LEN];

   if (WinSharedMemClient_GetCmdArgv(&argc, &arglen, argStr, sizeof(argStr)))
   {
      // XXX TODO should use arglen instead of argc
      if (argc >= 1)
      {
         TvSimu_ParseRemoteCmd(argStr, argc);
      }
   }
}

// ----------------------------------------------------------------------------
// Shm message handler: EPG peer replied with program title information
// - display the information in the GUI
//
static void TvSimuMsg_UpdateProgInfo( void )
{
   EPG_PI epg_pi;
   char * pBuffer;

   pBuffer = WinSharedMemClient_GetProgInfo();
   if (pBuffer != NULL)
   {
      // remove the pop-down timer (which would clear the EPG display if no answer arrives in time)
      if (popDownEvent != NULL)
         Tcl_DeleteTimerHandler(popDownEvent);
      popDownEvent = NULL;

      TvSimu_DisplayPiDescription(pBuffer);

#ifndef DPRINTF_OFF
      if ( (pBuffer[0] != 0) &&
           TvSimu_ParsePiDescription(pBuffer, &epg_pi) )
      {
         dprintf2("TvSimuMsg-UpdateProgInfo: %s: %s\n", epg_pi.pNetName, epg_pi.pTitle);
      }
#endif

      free(pBuffer);
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

         case SHM_EVENT_STATION_INFO:
            TvSimuMsg_UpdateProgInfo();
            break;

         case SHM_EVENT_EPG_INFO:
            // TODO
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
#endif  // WIN32

// ----------------------------------------------------------------------------
// Tcl/Tk callback for toggle of "Grant tuner to EPG" button
//
static int TclCb_GrantTuner( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
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
#ifdef WIN32
         WinSharedMemClient_GrantTuner(doGrant);

         // check for an already pending request & execute it
         if (doGrant)
            TvSimuMsg_ReqTuner();
#endif
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Tcl/Tk callback function for channel selection
//
static int TclCb_TuneChan( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   const char * const pUsage = "C_TuneChan <idx> <name>";
   const TVAPP_CHAN_TAB * pChanTab;
   char  * pErrMsg;
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
         pErrMsg = NULL;
         pChanTab = WintvCfg_GetFreqTab(&pErrMsg);
         if ((pChanTab != NULL) && (pChanTab->chanCount > 0))
         {
            if ((freqIdx < pChanTab->chanCount) && (freqIdx >= 0))
            {
               // Change the tuner frequency via the BT8x8 driver
               BtDriver_TuneChannel(TVSIM_INPUT_IDX, &pChanTab->pFreqTab[freqIdx], FALSE, &isTuner);
               BtDriver_SelectSlicer(VBI_SLICER_ZVBI);

               #ifdef WIN32
               {
                  Tcl_Obj  * pVarObj;
                  int  piCount;
                  pVarObj = Tcl_GetVar2Ex(interp, "epg_pi_count", NULL, TCL_GLOBAL_ONLY);
                  if ( (pVarObj == NULL) ||
                       (Tcl_GetIntFromObj(interp, pVarObj, &piCount) != TCL_OK) )
                  {
                     piCount = 1;
                  }

                  // Request EPG info & update frequency info
                  // - norms are coded into the high byte of the frequency dword (0=PAL-BG, 1=NTSC, 2=SECAM)
                  // - input source is hard-wired to TV tuner in this simulation
                  // - channel ID is hard-wired to 0 too (see comments in the winshmclnt.c)
                  WinSharedMemClient_SetStation(argv[2], -1, 0,
                                                isTuner, pChanTab->pFreqTab[freqIdx].freq,
                                                pChanTab->pFreqTab[freqIdx].norm, piCount);
               }
               #else
               // Xawtv: provide station name and frequency (FIXME channel code omitted)
               if (IPC_XAWTV_ENABLED)
               {
                  TvSim_XawtvSetStation(pChanTab->pFreqTab[freqIdx].freq, "-", argv[2]);
               }
               if (IPC_ICCCM_ENABLED)
               {
                  CONST84 char * pPiCountStr;
                  char * pArgv;
                  char   freq_buf[15];
                  uint   arg_len;

                  if (xiccc.manager_wid != None)
                  {
                     pPiCountStr = Tcl_GetVar(interp, "epg_pi_count", TCL_GLOBAL_ONLY);
                     if (pPiCountStr == NULL)
                        pPiCountStr = "1";

                     sprintf(freq_buf, "%ld", pChanTab->pFreqTab[freqIdx].freq);

                     Xiccc_BuildArgv(&pArgv, &arg_len, ICCCM_PROTO_VSTR, argv[2], freq_buf, "-", "", "Text/Tabular", pPiCountStr, "1", NULL);
                     Xiccc_SendQuery(&xiccc, pArgv, arg_len, xiccc.atoms._NXTVEPG_SETSTATION,
                                     xiccc.atoms._NXTVEPG_SETSTATION_RESULT);
                     xfree(pArgv);
                  }
               }
               #endif

               // Install a timer handler to clear the title display in case EPG app fails to answer
               if (popDownEvent != NULL)
                  Tcl_DeleteTimerHandler(popDownEvent);
               popDownEvent = Tcl_CreateTimerHandler(420, TvSimu_TitleChangeTimer, NULL);
            }
            else
            {  // ignore: might be a non-tuner input
               //Tcl_SetResult(interp, "C_TuneChan: invalid channel index", TCL_STATIC);
               //result = TCL_ERROR;
            }
         }
         else
         {
            sprintf(comm, "tk_messageBox -type ok -icon error -message {"
                          "Could not tune the channel: %s "
                          "(Note the \"Test\" button in the TV app. interaction dialog.)}",
                          ((pErrMsg != NULL) ? pErrMsg : "Please check your TV application settings."));
            eval_check(interp, comm);

            if (pErrMsg != NULL)
               xfree(pErrMsg);
         }
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Return the current time as UNIX "epoch"
// - replacement for Tcl's [clock seconds] which requires an overly complicated
//   library script since 8.5
//
static int ClockSeconds( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ClockSeconds";
   int  result;

   if (objc != 1)
   {  // parameter count or format is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      Tcl_SetObjResult(interp, Tcl_NewIntObj(time(NULL)));
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Format time in the given format
// - workaround for Tcl library on Windows: current locale is not used for weekdays etc.
//
static int ClockFormat( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ClockFormat <time> <format>";
   long reqTimeVal;
   time_t reqTime;
   int  result;

   if ( (objc != 1+2) ||
        (Tcl_GetLongFromObj(interp, objv[1], &reqTimeVal) != TCL_OK) ||
        (Tcl_GetString(objv[2]) == NULL) )
   {  // parameter count or format is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      reqTime = (time_t) reqTimeVal;
      if (strftime(comm, sizeof(comm) - 1, Tcl_GetString(objv[2]), localtime(&reqTime)) == 0)
      {  // error
         comm[0] = 0;
      }
      Tcl_SetObjResult(interp, TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, comm, NULL));
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Scan time and date strings in ISO format and return epoch value
// - format: 2008-09-07 01:25:00
// - workaround for Tcl library on Windows: current locale is not used for weekdays etc.
//
static int ClockScanIso( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ClockScanIso <date> <time>";
   const char * pDate;
   const char * pTime;
   struct tm t;
   time_t epoch;
   int  result;

   if ( (objc != 1+2) ||
        (Tcl_GetString(objv[1]) == NULL) ||
        (Tcl_GetString(objv[2]) == NULL) )
   {  // parameter count or format is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pDate = Tcl_GetString(objv[1]);
      pTime = Tcl_GetString(objv[2]);
      memset(&t, 0, sizeof(t));

      if ((sscanf(pDate, "%u-%u-%u",
                         &t.tm_year, &t.tm_mon, &t.tm_mday) == 3) &&
          (sscanf(pTime, "%u:%u:00", &t.tm_hour, &t.tm_min) == 2))
      {
         t.tm_year -= 1900;
         t.tm_mon -= 1;
         t.tm_isdst = -1;
         t.tm_sec = 0;

         epoch = mktime(&t);
      }
      else
      {
         debug2("C_ClockScanIso: format error '%s' or '%s'", pDate, pTime);
         epoch = 0;
      }
      Tcl_SetObjResult(interp, Tcl_NewIntObj( epoch ));
      result = TCL_OK;
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
   #ifdef WIN32
   Tcl_SetVar(interp, "argv0", argv[0], TCL_GLOBAL_ONLY);
   #else
   if (IPC_XAWTV_ENABLED)
   {
      // WM_CLASS property for toplevel is derived from argv0
      // changing WM_CLASS later via Tk_SetClass() does not work, so we need this hack
      Tcl_SetVar(interp, "argv0", "Xawtv", TCL_GLOBAL_ONLY);
   }
   else
      Tcl_SetVar(interp, "argv0", argv[0], TCL_GLOBAL_ONLY);
   #endif

   Tcl_SetVar(interp, "tcl_library", TCL_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "tk_library", TK_LIBRARY_PATH, TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "tcl_interactive", "0", TCL_GLOBAL_ONLY);
   Tcl_SetVar(interp, "TVSIM_VERSION", TVSIM_VERSION_STR, TCL_GLOBAL_ONLY);

   #ifndef WIN32
   Tcl_SetVar(interp, "x11_appdef_path", X11_APP_DEFAULTS, TCL_GLOBAL_ONLY);
   Tcl_SetVar2Ex(interp, "is_unix", NULL, Tcl_NewIntObj(1), TCL_GLOBAL_ONLY);
   #else
   Tcl_SetVar2Ex(interp, "is_unix", NULL, Tcl_NewIntObj(0), TCL_GLOBAL_ONLY);
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

   #ifdef DISABLE_TCL_BGERR
   // switch off Tcl/Tk background error reports for release version
   sprintf(comm, "proc bgerror foo {}\n");
   eval_check(interp, comm);
   #endif

   // create an asynchronous event source that allows to receive triggers from the EPG message receptor thread
   asyncThreadHandler = Tcl_AsyncCreate(TvSimu_AsyncThreadHandler, NULL);
   #ifdef WIN32
   Tcl_SetVar2Ex(interp, "is_xiccc_proto", NULL, Tcl_NewBooleanObj(0), TCL_GLOBAL_ONLY);
   #else
   asyncIcccmHandler = Tcl_AsyncCreate(TvSimu_AsyncIcccmHandler, NULL);
   Tcl_SetVar2Ex(interp, "is_xiccc_proto", NULL, Tcl_NewBooleanObj(IPC_ICCCM_ENABLED), TCL_GLOBAL_ONLY);
   #endif

   Tcl_CreateCommand(interp, "C_GrantTuner", TclCb_GrantTuner, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_TuneChan", TclCb_TuneChan, (ClientData) NULL, NULL);

   Tcl_CreateObjCommand(interp, "C_ClockSeconds", ClockSeconds, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_ClockFormat", ClockFormat, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_ClockScanIso", ClockScanIso, (ClientData) NULL, NULL);

   if (TCL_EVAL_CONST(interp, (char*)tvsim_gui_tcl_static) != TCL_OK)
   {
      debug1("tvsim_gui_tcl_static error: %s\n", Tcl_GetStringResult(interp));
      debugTclErr(interp, "tvsim_gui_tcl_static");
   }

   sprintf(comm, "Tvsim_LoadRcFile {%s}\n", rcfile);
   eval_check(interp, comm);

   #if defined (WIN32) && ((TCL_MAJOR_VERSION != 8) || (TCL_MINOR_VERSION >= 5))
   Tcl_CreateCommand(interp, "C_WinHandleShutdown", TclCbWinHandleShutdown, (ClientData) NULL, NULL);
   eval_check(interp, "wm protocol . WM_SAVE_YOURSELF C_WinHandleShutdown\n");
   #endif

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
   char * pErrMsg = NULL;
   #ifdef WIN32
   WINSHMCLNT_EVENT  attachEvent;
   int argc;
   char ** argv;

   // initialize Tcl/Tk interpreter and compile all scripts
   SetArgv(&argc, &argv);
   #endif
   ParseArgv(argc, argv);

   RcFile_Init();
   RcFile_Load(rcfile, !isDefaultRcfile, &pErrMsg);
   if (pErrMsg != NULL)
      xfree(pErrMsg);

   // mark Tcl/Tk interpreter as uninitialized
   interp = NULL;

   ui_init(argc, argv);
   encIso88591 = Tcl_GetEncoding(interp, "iso8859-1");
   WintvUi_Init();

   BtDriver_Init();
   #ifdef WIN32
   if (WinSharedMemClient_Init(&tvSimuInfo, videoCardIndex, &attachEvent))
   #endif
   {
      #ifdef WIN32
      #if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION <= 4)
      // set up callback to catch shutdown messages (requires tk83.dll patch, see README.tcl83)
      Tk_RegisterMainDestructionHandler(WinApiDestructionHandler);
      #endif
      #ifndef __MINGW32__
      __try {
      #else
      SetUnhandledExceptionFilter(WinApiExceptionHandler);
      #endif
      #endif

      // pass bt8x8 driver parameters to the driver
      if (withoutDevice || SetHardwareConfig(videoCardIndex))
      {
         // fill channel listbox with names from TV app channel table
         // a warning is issued if the table is empty (used during startup)
         sprintf(comm, "LoadChanTable");
         eval_check(interp, comm);

         if (withoutDevice || BtDriver_StartAcq())
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

            #ifndef WIN32
            TvSim_XawtvCreateAtoms();
            #else
            // report attach failures during initialization
            if (attachEvent == SHM_EVENT_ATTACH)
               TvSimuMsg_Attach(TRUE);
            else if (attachEvent == SHM_EVENT_ATTACH_ERROR)
               TvSimuMsg_BackgroundError();
            #endif

            // process GUI events & callbacks until the main window is closed
            while (Tk_GetNumMainWindows() > 0)
            {
               Tcl_DoOneEvent(TCL_ALL_EVENTS);
            }

            BtDriver_StopAcq();
            #ifndef WIN32
            Xiccc_Destroy(&xiccc);
            #endif
         }
         else
         {
            debug0("Fatal: failed to start acq - quitting now");

            #ifdef WIN32
            // this might have been the cause for failure, so display the message
            if (attachEvent == SHM_EVENT_ATTACH_ERROR)
               TvSimuMsg_BackgroundError();
            #endif
         }
      }

      #if defined(WIN32) && !defined(__MINGW32__)
      }
      __except (EXCEPTION_EXECUTE_HANDLER)
      {  // caught a fatal exception -> stop the driver to prevent system crash ("blue screen")
         WinApiExceptionHandler(NULL);
      }
      #endif

      BtDriver_Exit();
      #ifdef WIN32
      WinSharedMemClient_Exit();
      #endif
   }
   #ifdef WIN32
   else
   {  // Fatal: failed to set up IPC resources
      TvSimuMsg_BackgroundError();
   }
   #endif

   WintvUi_Destroy();
   WintvCfg_Destroy();
   RcFile_Destroy();
   Tcl_FreeEncoding(encIso88591);
   encIso88591 = NULL;

   #if CHK_MALLOC == ON
   #ifdef WIN32
   xfree(argv);
   #endif
   xfree((void *) rcfile);
   // check for allocated memory that was not freed
   chk_memleakage();
   #endif

   exit(0);
   return(0);
}

