/*
 *  Nextview EPG: xawtv remote control module
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
 *    This module implements interaction with xawtv and derived TV viewing
 *    applications.  It detects xawtv channel changes and triggers popups
 *    with current programme information.  It can also send remote commands
 *    to the TV application (e.g. to switch the TV channel).
 *
 *    Communication is based on so-called "X atoms", i.e. variables which are
 *    stored in the X server; an event handler is installed both in xawtv and
 *    nxtvepg which is called whenever an atom's value is changed by the other
 *    application.
 *
 *  Author: Tom Zoerner
 *
 *     Parts of the code implementing the Xawtv protocol (root and toplevel
 *     window tree traversal) originates from Netscape (author unknown).
 *     Those parts have been adapted for xawtv-remote.c by Gerd Knorr
 *     (kraxel@bytesex.org)  Some functions have been derived from xawtv.
 *
 *  $Id: xawtv.c,v 1.57 2020/06/17 19:34:45 tom Exp tom $
 */

#ifdef WIN32
#error "This module is not intended for Win32 systems"
#else

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmu/WinUtil.h>	/* for XmuClientWindow() */

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/cni_tables.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgversion.h"
#include "epgui/epgmain.h"
#include "epgui/pibox.h"
#include "epgui/pidescr.h"
#include "epgui/uidump.h"
#include "epgui/dumptext.h"
#include "epgui/shellcmd.h"
#include "epgui/epgsetup.h"
#include "epgui/wintvui.h"
#include "epgui/xiccc.h"
#include "epgui/xawtv.h"


// ----------------------------------------------------------------------------
// Variables used for ICCCM protocol

// complete communication state
static XICCC_STATE xiccc;
// copy of the last setstation message of the connected TV app
static XICCC_MSG_SETSTATION xiccc_last_station;

// ----------------------------------------------------------------------------
// Variables used for Xawtv protocol

// this atom is set by xawtv: it contains the current channel's name
static Atom xawtv_station_atom = None;
// this atom is set by nxtvepg to send remote commands
static Atom xawtv_remote_atom = None;
// this atom is used to identify xawtv in the search across all toplevel windows
static Atom wm_class_atom = None;

// X11 display handle if TV app is on different display than the GUI
static Display * alternate_dpy = NULL;
static MAIN_REMOTE_CMD_HANDLER * pRemoteCmdHandler = NULL;

// window IDs (note: on the TV app's X11 server)
static Window xawtv_wid = None;
static Window parent_wid = None;
static Window root_wid = None;

static Tcl_TimerToken popDownEvent = NULL;
static Tcl_TimerToken pollVpsEvent = NULL;
static Tcl_TimerToken classBufferEvent = NULL;

// names to search for in WM_CLASS property
static const char * const pKnownTvAppNames[] =
{
  "XAWTV",
  "XAWDECODE",
  "XDTV",
  "ZAPPING",
  "TVTIME",
};
#define KNOWN_TVAPP_COUNT (sizeof(pKnownTvAppNames)/sizeof(pKnownTvAppNames[0]))

// possible EPG info display types in xawtv
typedef enum
{
   POP_EXT,
   POP_VTX,
   POP_VTX2,
   POP_MSG,
   POP_APP,
   POP_COUNT
} POPTYPE;

// structure to hold user configuration (copy of global Tcl variables)
typedef struct
{
   bool     xawtvProto;
   bool     xicccProto;
   bool     tunetv;
   bool     follow;
   bool     doPop;
   POPTYPE  popType;
   uint     duration;
} XAWTVCF;

static XAWTVCF xawtvcf = {1, 1, 1, 1, 1, POP_EXT, 7};

// forward declaration
static void Xawtv_ClassBufferTimer( ClientData clientData );
static void Xawtv_StationSelected( ClientData clientData );
static bool Xawtv_QueryRemoteStation( Window wid, char * pBuffer, int bufLen, int * pTvFreq );
static void Xawtv_PopDownNowNext( ClientData clientData );
static void Xawtv_TvAttach( ClientData clientData );
static void Xawtv_IcccServeRequest( ClientData clientData );
static Tcl_Obj * Xawtv_ReadConfigAppCmd( Tcl_Interp *interp );

// ----------------------------------------------------------------------------
// buffer to hold IDs of newly created toplevel windows until WM_CLASS query
//
typedef struct
{
   time_t    timestamp;
   Display * dpy;
   Window    wid;
} XAWTV_WID_BUF;

static struct
{
   uint      maxCount;
   uint      fillCount;
   XAWTV_WID_BUF * pBuf;
} xawtvWidScanBuf;

// ----------------------------------------------------------------------------
// define struct to hold state of CNI & PIL supervision
//
static struct
{
   uint      pil;
   uint      cni;
   bool      newXawtvStarted;
   uint      stationPoll;
   uint      stationCni;
   uint      lastCni;
   time_t    lastStartTime;
} followTvState = {INVALID_VPS_PIL, 0, FALSE, 0, 0, 0, 0};


// ----------------------------------------------------------------------------
// Dummy X11 error event handler for debugging purposes
//
#ifdef DEBUG_SWITCH
static int Xawtv_X11ErrorHandler( ClientData clientData, XErrorEvent *errEventPtr )
{
   char msgBuf[256];

   msgBuf[0] = 0;
   XGetErrorText(errEventPtr->display, errEventPtr->error_code, msgBuf, sizeof(msgBuf));

   debug7("X11 error: type=%d serial=%lu err=%d '%s' req=%d,%d res=0x%lX", errEventPtr->type, errEventPtr->serial, errEventPtr->error_code, msgBuf, errEventPtr->request_code, errEventPtr->minor_code, errEventPtr->resourceid);
   return 0;
}
#else
// no debugging -> just have Tk discard all error events
const Tk_ErrorProc * Xawtv_X11ErrorHandler = NULL;
#endif

// ----------------------------------------------------------------------------
#ifndef DPRINTF_OFF
static void DebugDumpProperty( const char * prop, ulong nitems )
{
   uint i;

   for (i = 0; i < nitems; i += strlen(prop + i) + 1)
      dprintf1("%s ", prop + i);
   dprintf0("\n");
}
#else
# define DebugDumpProperty(X,Y)
#endif


// ---------------------------------------------------------------------------
// Get X11 display for the TV app
// - usually the same as the display of the nxtvepg GUI
// - can be modified via command line switch (e.g. multi-headed PC)
//
static Display * Xawtv_GetTvDisplay( void )
{
   Tk_Window   tkwin;
   Display   * dpy;

   dpy = alternate_dpy;
   if (dpy == NULL)
   {  // same as GUI -> query main window for it's display handle
      tkwin = Tk_MainWindow(interp);
      if (tkwin != NULL)
      {
         dpy = Tk_Display(tkwin);
      }
   }
   return dpy;
}

// ---------------------------------------------------------------------------
// Set input select mask on root window for Xawtv protocol
// - take care not to remove mask required by other protocols
//
static void Xawtv_SetRootWindowEvMask( Display * dpy, Window root_wid, ulong ev_mask )
{
   if (root_wid != None)
   {
      if (xawtvcf.xicccProto)
         XSelectInput(dpy, root_wid, ev_mask | StructureNotifyMask);
      else
         XSelectInput(dpy, root_wid, ev_mask);
   }
}

// ---------------------------------------------------------------------------
// Check the class of a newly created window
// - to find xawtv we cannot check for the STATION property on the window,
//   because that one's not available right away when the window is created
// - error handler must be installed by caller!
//
static bool Xawtv_ClassQuery( Display * dpy, Window wid )
{
   Atom   type;
   int    format, argc;
   ulong  off;
   ulong  nitems, bytesafter;
   char   *args;
   char   *pAtomName;
   uint   tvIdx;
   bool   result = FALSE;

   if ( (XGetWindowProperty(dpy, wid, wm_class_atom, 0, 64, False, AnyPropertyType,
                            &type, &format, &nitems, &bytesafter, (uchar**)&args) == Success) &&
       (args != NULL) )
   {
      if (type == XA_STRING)
      {
         for (off=0, argc=0; (off < nitems) && !result; off += strlen(args + off) + 1, argc++)
         {
            dprintf2("Xawtv-ClassQuery: window:0x%X wm-class: '%s'\n", (int)wid, args + off);
            for (tvIdx=0; tvIdx < KNOWN_TVAPP_COUNT; tvIdx++)
            {
               if ( (nitems - off >= strlen(pKnownTvAppNames[tvIdx])) &&
                    (strcasecmp(args + off, pKnownTvAppNames[tvIdx]) == 0) )
               {
                  result = TRUE;
                  break;
               }
            }
         }
      }
      else if (type == XA_ATOM)
      {
         for (argc=0; (argc < nitems) && !result; argc++)
         {
            pAtomName = XGetAtomName(dpy, (Atom)(((long*)args)[argc]));
            if (pAtomName != NULL)
            {
               for (tvIdx=0; tvIdx < KNOWN_TVAPP_COUNT; tvIdx++)
               {
                  dprintf2("Xawtv-ClassQuery: window:0x%X wm-class atom: '%s'\n", (int)wid, pAtomName);
                  if (strcasecmp(pAtomName, pKnownTvAppNames[tvIdx]) == 0)
                  {
                     result = TRUE;
                     break;
                  }
               }
               XFree(pAtomName);
            }
         }
      }
      XFree(args);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Find the uppermost parent of the xawtv window beneath the root
// - direct child of the root window is needed to query dimensions
// - error handler must be installed by caller!
//
static bool Xawtv_QueryParent( Display * dpy, Window wid )
{
   Window root_ret, parent, *kids;
   uint   nkids;
   bool   result = FALSE;

   while ( (wid != root_wid) &&
           XQueryTree(dpy, wid, &root_ret, &parent, &kids, &nkids))
   {
      dprintf3("Xawtv-QueryParent: wid=0x%lX parent=0x%lx root=0x%lx\n", wid, parent, root_ret);
      if (kids != NULL)
         XFree(kids);

      if (parent == root_ret)
      {  // parent is the root window -> done.
         parent_wid = wid;
         result = TRUE;
         break;
      }
      wid = parent;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Append ID of a newly created window to the buffer
//
static void Xawtv_ClassBufferAppend( Display * dpy, Window wid )
{
   XAWTV_WID_BUF * pOld;
   uint  idx;

   for (idx = 0; idx < xawtvWidScanBuf.fillCount; idx++)
      if ( (xawtvWidScanBuf.pBuf[idx].wid == wid) &&
           (xawtvWidScanBuf.pBuf[idx].dpy == dpy) )
         break;
   if (idx < xawtvWidScanBuf.fillCount)
   {
      // window is already registered (or ID already reused) -> remove old instance
      // (don't overwrite, else timestamps would no longer be in increasing order)
      if (idx + 1 < xawtvWidScanBuf.fillCount)
      {
         memmove(&xawtvWidScanBuf.pBuf[idx], &xawtvWidScanBuf.pBuf[idx + 1],
                 sizeof(xawtvWidScanBuf.pBuf[0]) * (xawtvWidScanBuf.fillCount - (idx + 1)));
      }
      xawtvWidScanBuf.fillCount -= 1;
   }

   // grow buffer if necessary
   if (xawtvWidScanBuf.fillCount >= xawtvWidScanBuf.maxCount)
   { 
      pOld = xawtvWidScanBuf.pBuf;
      xawtvWidScanBuf.maxCount += 16;
      xawtvWidScanBuf.pBuf = xmalloc(xawtvWidScanBuf.maxCount * sizeof(xawtvWidScanBuf.pBuf[0]));

      if (pOld != NULL)
      {
         memcpy(xawtvWidScanBuf.pBuf, pOld, xawtvWidScanBuf.fillCount * sizeof(xawtvWidScanBuf.pBuf[0]));
         xfree(pOld);
      }
   }

   xawtvWidScanBuf.pBuf[xawtvWidScanBuf.fillCount].dpy = dpy;
   xawtvWidScanBuf.pBuf[xawtvWidScanBuf.fillCount].wid = wid;
   xawtvWidScanBuf.pBuf[xawtvWidScanBuf.fillCount].timestamp = time(NULL);
   xawtvWidScanBuf.fillCount += 1;
}

// ----------------------------------------------------------------------------
// Empty the window ID buffer
//
static void Xawtv_ClassBufferDestroy( void )
{
   if (xawtvWidScanBuf.pBuf != NULL)
   {
      xfree(xawtvWidScanBuf.pBuf);
      xawtvWidScanBuf.pBuf = NULL;
   }
   xawtvWidScanBuf.fillCount = 0;
   xawtvWidScanBuf.maxCount = 0;
}

// ----------------------------------------------------------------------------
// Check class of all windows in a queue to search for new Xawtv peer
//
static void Xawtv_ClassBufferProcess( void )
{
   Tk_ErrorHandler errHandler;
   Display * dpy;
   uint   idx;
   time_t now = time(NULL);

   dpy = Xawtv_GetTvDisplay();
   if (dpy != NULL)
   {
      // push an /dev/null handler onto the stack that catches any type of X11 error events
      errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

      for (idx = 0; idx < xawtvWidScanBuf.fillCount; idx++)
      {
         if (now >= xawtvWidScanBuf.pBuf[idx].timestamp + 2)
         {
            if ( Xawtv_ClassQuery(xawtvWidScanBuf.pBuf[idx].dpy, xawtvWidScanBuf.pBuf[idx].wid) )
            {
               dprintf1("Xawtv-ClassBufferProcess: found peer window:0x%X\n", (int)xawtvWidScanBuf.pBuf[idx].wid);

               xawtv_wid = xawtvWidScanBuf.pBuf[idx].wid;
               // discard the remaining IDs in the buffer
               Xawtv_ClassBufferDestroy();

               // parent is unknown, because the window may not managed by the wm yet
               parent_wid = None;
               // remove the creation event notification
               Xawtv_SetRootWindowEvMask(dpy, root_wid, 0);
               // install an event handler
               XSelectInput(dpy, xawtv_wid, StructureNotifyMask | PropertyChangeMask);
               // check if the tuner device is busy upon the first property event
               followTvState.newXawtvStarted = TRUE;
               AddMainIdleEvent(Xawtv_TvAttach, NULL, TRUE);
               // trigger initial query of station property
               AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);
               break;
            }
         }
         else
         {  // check this and the following windows later
            break;
         }
      }

      // remove processed windows from the queue
      if (idx < xawtvWidScanBuf.fillCount)
      {
         if (idx > 0)
         {
            memmove(&xawtvWidScanBuf.pBuf[0], &xawtvWidScanBuf.pBuf[idx],
                    sizeof(xawtvWidScanBuf.pBuf[0]) * (xawtvWidScanBuf.fillCount - idx));
            xawtvWidScanBuf.fillCount -= idx;
         }
         classBufferEvent = Tcl_CreateTimerHandler( (xawtvWidScanBuf.pBuf[0].timestamp + 2 - now) * 1000,
                                                    Xawtv_ClassBufferTimer, NULL);
      }
      else
         xawtvWidScanBuf.fillCount = 0;

      // remove the dummy error handler
      Tk_DeleteErrorHandler(errHandler);
   }
}

// ----------------------------------------------------------------------------
// Timer event handler to process queued toplevel window IDs
//
static void Xawtv_ClassBufferTimer( ClientData clientData )
{
   classBufferEvent = NULL;

   if (xawtv_wid == None)
   {
      if (xawtvWidScanBuf.fillCount != 0)
      {
         dprintf2("Xawtv-ClassWidCheck: checking class of %d new windows, first: 0x%X\n", xawtvWidScanBuf.fillCount, (int)xawtvWidScanBuf.pBuf[0].wid);
         Xawtv_ClassBufferProcess();
      }
   }
   else
   {
      Xawtv_ClassBufferDestroy();
   }
}

// ----------------------------------------------------------------------------
// Event handler, triggered by X event handler for new toplevel windows
// - install TCL timer to trigger query of the wm-class of new windows
// - query is delayed because apps may need some time until wm-class is set
//
static void Xawtv_ClassBufferEvent( ClientData clientData )
{
   if (classBufferEvent == NULL)
   {
      classBufferEvent = Tcl_CreateTimerHandler(3000, Xawtv_ClassBufferTimer, NULL);
   }
}

// ----------------------------------------------------------------------------
// X Event handler
// - this function is called for every incoming X event
// - filters out events from the xawtv main window
//
static int Xawtv_EventNotification( ClientData clientData, XEvent *eventPtr )
{
   bool needHandler;
   bool result = FALSE;

   if ( (xawtvcf.xicccProto) &&
        Xiccc_XEvent(eventPtr, &xiccc, &needHandler) )
   {
      if (needHandler)
      {
         AddMainIdleEvent(Xawtv_IcccServeRequest, NULL, TRUE);
      }
   }
   else if (xawtvcf.xawtvProto)
   {
      if ( (eventPtr->type == PropertyNotify) &&
           (eventPtr->xproperty.window == xawtv_wid) && (xawtv_wid != None) )
      {
         dprintf1("PropertyNotify event from window 0x%X\n", (int)eventPtr->xproperty.window);
         if (eventPtr->xproperty.atom == xawtv_station_atom)
         {
            if (eventPtr->xproperty.state == PropertyNewValue)
            {  // the station has changed
               xiccc_last_station.isNew = FALSE;
               AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);
            }
         }
         result = TRUE;
      }
      else if ( (eventPtr->type == CreateNotify) &&
                (eventPtr->xcreatewindow.parent == root_wid) && (root_wid != None) )
      {
         dprintf1("CreateNotify event from window 0x%X\n", (int)eventPtr->xcreatewindow.window);
         if (xawtv_wid == None)
         {
            Xawtv_ClassBufferAppend(eventPtr->xcreatewindow.display, eventPtr->xcreatewindow.window);
            AddMainIdleEvent(Xawtv_ClassBufferEvent, NULL, TRUE);
         }
         result = TRUE;
      }
      else if ( (eventPtr->type == DestroyNotify) &&
                (eventPtr->xdestroywindow.window == xawtv_wid) && (xawtv_wid != None) )
      {
         dprintf1("DestroyNotify event from window 0x%X\n", (int)eventPtr->xdestroywindow.window);
         // disconnect from the obsolete window
         xawtv_wid = None;
         parent_wid = None;
         // install a create notification handler to wait for a new xawtv window
         Xawtv_SetRootWindowEvMask(eventPtr->xdestroywindow.display, root_wid, SubstructureNotifyMask);
         // destroy the nxtvepg-controlled xawtv popup window
         AddMainIdleEvent(Xawtv_TvAttach, NULL, TRUE);
         result = TRUE;
      }
      else if ( (eventPtr->type == UnmapNotify) &&
                (eventPtr->xunmap.window == xawtv_wid) && (xawtv_wid != None) )
      {
         dprintf1("UnmapNotify event from window 0x%X\n", (int)eventPtr->xunmap.window);
         AddMainIdleEvent(Xawtv_PopDownNowNext, NULL, TRUE);
         result = TRUE;
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Search for the xawtv toplevel window
//
static bool Xawtv_FindWindow( Display * dpy, Atom atom )
{
   Window root2, parent, *kids, w;
   unsigned int nkids;
   bool result = FALSE;
   int n;
   Atom type;
   int format;
   ulong nitems, bytesafter;
   char *args = NULL;

   if (XQueryTree(dpy, root_wid, &root2, &parent, &kids, &nkids) != 0)
   {
      if (kids != NULL)
      {
         // process kids in opposite stacking order, i.e. topmost -> bottom
         for (n = nkids - 1; n >= 0; n--)
         {
            w = XmuClientWindow(dpy, kids[n]);

            args = NULL;
            if ( (XGetWindowProperty(dpy, w, atom, 0, (65536 / sizeof (long)), False, XA_STRING,
                                     &type, &format, &nitems, &bytesafter, (uchar**)&args) == Success) && (args != NULL) )
            {
               dprintf1("Found xawtv window 0x%08lx, STATION: ", w);
               DebugDumpProperty(args, nitems);
               XFree(args);

               xawtv_wid = w;
               parent_wid = kids[n];
               // check if the tuner device is busy upon the next property event
               followTvState.newXawtvStarted = TRUE;
               AddMainIdleEvent(Xawtv_TvAttach, NULL, TRUE);
               result = TRUE;
               break;   // NOTE: there might be more than window
            }
            else
            {  // station property not present -> debug only: read wm-class property
#ifndef DPRINTF_OFF
               Xawtv_ClassQuery(dpy, kids[n]);
#endif
            }
         }
         XFree(kids);
      }
      else
         debug1("root window has no children on display %s", DisplayString(dpy));
   }
   else
      debug1("XQueryTree failed on display %s", DisplayString(dpy));

   return result;
}

// ----------------------------------------------------------------------------
// Check if the cached Xawtv window still exists
// - X error handler must be installed by caller
// - if window has become invalid or none known yet, a new one is searched
//
static void Xawtv_CheckWindow( Display * dpy )
{
   Atom type;
   int format;
   ulong nitems, bytesafter;
   char *args = NULL;

   if (xawtv_wid != None)
   {
      XGetWindowProperty(dpy, xawtv_wid, xawtv_station_atom, 0, (65536 / sizeof (long)), False,
                         XA_STRING, &type, &format, &nitems, &bytesafter, (uchar**)&args);
      if (args != NULL)
      {
         dprintf1("Xawtv-CheckWindow: xawtv alive, window 0x%X, STATION: ", (int)xawtv_wid);
         DebugDumpProperty(args, nitems);
         XFree(args);
      }
      else
      {
         dprintf1("Xawtv-CheckWindow: xawtv window 0x%X invalid", (int)xawtv_wid);
         xawtv_wid = None;
         AddMainIdleEvent(Xawtv_TvAttach, NULL, TRUE);
      }
   }

   // try to find a new xawtv window
   if (xawtv_wid == None)
   {
      if ( Xawtv_FindWindow(dpy, xawtv_station_atom) )
      {
         dprintf1("Xawtv-CheckWindow: Connect to xawtv 0x%X\n", (int)xawtv_wid);
         XSelectInput(dpy, xawtv_wid, StructureNotifyMask | PropertyChangeMask);
         AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);
      }
   }
}

// ---------------------------------------------------------------------------
// Display a popup window below xawtv
// - similar to SendCmd: first verify if the old window ID is still valid;
//   if not search for a new xawtv window
// - if the window manager's wrapper window (parent) is not known yet search it
// - query the window manager's wrapper window for the position and size
//
static void Xawtv_Popup( float rperc, const char * pTimes, const char * pTitle )
{
   XWindowAttributes wat;
   Tk_ErrorHandler errHandler;
   Display *dpy;
   int retry;
   int idx;

   {
      dpy = Xawtv_GetTvDisplay();
      if (dpy != NULL)
      {
         // push an /dev/null handler onto the stack that catches any type of X11 error events
         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

         retry = 1;
         do
         {
            // try to find an xawtv window
            if (xawtv_wid == None)
            {
               if ( Xawtv_FindWindow(dpy, xawtv_station_atom) )
               {
                  dprintf1("Xawtv-Popup: Connect to xawtv 0x%lX\n", (ulong)xawtv_wid);
                  XSelectInput(dpy, xawtv_wid, StructureNotifyMask | PropertyChangeMask);
                  AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);
               }
            }
            else if (parent_wid == None)
            {  // parent window not yet known - search for it now
               // this branch is only reached when xawtv is started after nxtvepg
               if (Xawtv_QueryParent(dpy, xawtv_wid) == FALSE)
               {
                  xawtv_wid = None;
                  AddMainIdleEvent(Xawtv_TvAttach, NULL, TRUE);
               }
            }

            // determine the window position
            if ( (xawtv_wid != None) && (parent_wid != None) &&
                 XGetWindowAttributes(dpy, parent_wid, &wat) )
            {
               dprintf5("Geometry: parent 0x%lX: %d/%d %dx%d\n", parent_wid, wat.x,wat.y, wat.width,wat.height);
            }
            else
            {
               xawtv_wid = None;
               parent_wid = None;
               AddMainIdleEvent(Xawtv_TvAttach, NULL, TRUE);
            }

            retry -= 1;
         }
         while ((xawtv_wid == None) && (retry >= 0));

         if ((xawtv_wid != None) && (wat.map_state == IsViewable))
         {
            Tcl_Obj * objv[10];
            objv[0] = Tcl_NewStringObj("Create_XawtvPopup", -1);
            objv[1] = Tcl_NewStringObj(DisplayString(dpy), -1);
            objv[2] = Tcl_NewIntObj(xawtvcf.popType);
            objv[3] = Tcl_NewIntObj(wat.x);
            objv[4] = Tcl_NewIntObj(wat.y);
            objv[5] = Tcl_NewIntObj(wat.width);
            objv[6] = Tcl_NewIntObj(wat.height);
            objv[7] = Tcl_NewDoubleObj(rperc);
            objv[8] = TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, pTimes, NULL);
            objv[9] = TranscodeToUtf8(EPG_ENC_NXTVEPG, NULL, pTitle, NULL);

            for (idx = 0; idx < 10; idx++)
               Tcl_IncrRefCount(objv[idx]);
            if (Tcl_EvalObjv(interp, 10, objv, 0) != TCL_OK)
               debugTclErr(interp, "Xawtv-Popup");
            for (idx = 0; idx < 10; idx++)
               Tcl_DecrRefCount(objv[idx]);

            // make sure there's no popdown event scheduled
            RemoveMainIdleEvent(Xawtv_PopDownNowNext, NULL, FALSE);
         }

         // remove the dummy error handler
         Tk_DeleteErrorHandler(errHandler);
      }
      else
         debug0("No Tk display available");
   }
}

// ---------------------------------------------------------------------------
// Send a command to xawtv via X11 property
// - note the string parameter may contain multiple 0-separated sub-strings
//
bool Xawtv_SendCmdArgv( Tcl_Interp *interp, const char * pCmdStr, uint cmdLen )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;
   bool result = FALSE;

   dpy = Xawtv_GetTvDisplay();
   if (dpy != NULL)
   {
      // push an /dev/null handler onto the stack that catches any type of X11 error events
      errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

      if ( XICCC_HAS_PEER(&xiccc) )
      {
         result = Xiccc_SendQuery(&xiccc, pCmdStr, cmdLen, xiccc.atoms._NXTVEPG_REMOTE,
                                  xiccc.atoms._NXTVEPG_REMOTE_RESULT);
      }
      else if (xawtvcf.xawtvProto)
      {
         Xawtv_CheckWindow(dpy);

         if (xawtv_wid != None)
         {
            // send the string to xawtv
            XChangeProperty(dpy, xawtv_wid, xawtv_remote_atom, XA_STRING, 8*sizeof(char),
                            PropModeReplace, (uchar*)pCmdStr, cmdLen);
            result = TRUE;
         }
      }

      if (result == FALSE)
      {  // xawtv window not found
         if (interp != NULL)
         {  // display warning only if called after user-interaction
            sprintf(comm, "tk_messageBox -type ok -icon error -message \"No TV application is running!\"\n");
            eval_check(interp, comm);
            Tcl_ResetResult(interp);
         }
      }

      // remove the dummy error handler
      Tk_DeleteErrorHandler(errHandler);
   }
   else
      debug0("No Tk display available");

   return result;
}

// ---------------------------------------------------------------------------
// Query the station name from the xawtv property
//
static bool Xawtv_QueryRemoteStation( Window wid, char * pBuffer, int bufLen, int * pTvFreq )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;
   Atom type;
   int format, argc;
   ulong off;
   ulong nitems, bytesafter;
   char *args;
   bool result = FALSE;

   if (wid != None)
   {
      dpy = Xawtv_GetTvDisplay();
      if (dpy != NULL)
      {
         // push an /dev/null handler onto the stack that catches any type of X11 error events
         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

         args = NULL;
         if ( (XGetWindowProperty(dpy, wid, xawtv_station_atom, 0, (65536 / sizeof (long)), False, XA_STRING,
                                  &type, &format, &nitems, &bytesafter, (uchar**)&args) == Success) && (args != NULL) )
         {
            // argument list is: frequency, channel, name
            for (off=0, argc=0; off < nitems; off += strlen(args + off) + 1, argc++)
            {
               if (argc == 0)
               {
                  if (pTvFreq != NULL)
                  {
                     char * p;
                     p = strchr(args + off, ',');
                     if (p != NULL)
                        *p = '.';
                     *pTvFreq = (int)(strtod(args + off, NULL) * 16);
                  }
               }
               else if (argc == 2)
               {
                  if (pBuffer != NULL)
                  {
                     dprintf2("Xawtv-Query: window 0x%lX: station name: '%s'\n", (ulong)wid, args + off);
                     strncpy(pBuffer, args + off, bufLen);
                     pBuffer[bufLen - 1] = 0;
                  }
                  result = TRUE;
                  break;
               }
            }
            XFree(args);
         }

         // remove the dummy error handler
         Tk_DeleteErrorHandler(errHandler);
      }
      else
         debug0("No Tk display available");
   }

   return result;
}

// ----------------------------------------------------------------------------
// Send station reply or update to remote TV app via ICCCM
//
static void Xawtv_IcccReplyNullStation( void )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;
   XICCC_EV_QUEUE * pReq;

   if (xiccc.pSendStationQueue != NULL)
   {
      dpy = Xawtv_GetTvDisplay();
      if (dpy != NULL)
      {
         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

         while (xiccc.pSendStationQueue != NULL)
         {
            pReq = xiccc.pSendStationQueue;
            Xiccc_QueueUnlinkEvent(&xiccc.pSendStationQueue, pReq);

            dprintf1("Xawtv-IcccReplyStation: reply to wid 0x%X\n", (int)pReq->requestor);

            Xiccc_SendNullReply(&xiccc, pReq, xiccc.atoms._NXTVEPG_SETSTATION);

            if (pReq->msg.setstation.epgUpdate)
               Xiccc_QueueAddEvent(&xiccc.pPermStationQueue, pReq);
            else
               xfree(pReq);
         }

         Tk_DeleteErrorHandler(errHandler);
      }
      else
         debug0("Xawtv-IcccReplyStation: failed to get description");
   }
}

// ----------------------------------------------------------------------------
// Query database and convert PI to text for a station update
//
static char * Xawtv_IcccPiQuery( const PI_BLOCK *pPiBlock, uint count )
{
   PI_DESCR_BUF pbuf;
   FILTER_CONTEXT *fc;
   uint  idx;

   memset(&pbuf, 0, sizeof(pbuf));

   // create a search filter to return PI for the current station only
   fc = EpgDbFilterCreateContext();
   EpgDbFilterInitNetwop(fc);
   EpgDbFilterSetNetwop(fc, pPiBlock->netwop_no);
   EpgDbFilterEnable(fc, FILTER_NETWOP);

   for (idx = 0; (idx < count) && (pPiBlock != NULL); idx++)
   {
      if (EpgDumpText_Single(pUiDbContext, pPiBlock, &pbuf) == FALSE)
         break;

      pPiBlock = EpgDbSearchNextPi(pUiDbContext, fc, pPiBlock);
   }
   EpgDbFilterDestroyContext(fc);

   return pbuf.pStrBuf;
}

// ----------------------------------------------------------------------------
// Send station reply or update to remote TV app via ICCCM
//
static void Xawtv_IcccReplyStation( const PI_BLOCK *pPiBlock, bool isStationChange )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;
   XICCC_EV_QUEUE * pReq;
   char * pPiDescr;

   if ( (xiccc.pPermStationQueue != NULL) ||
        (xiccc.pSendStationQueue != NULL) ||
        (XICCC_IS_MANAGER(&xiccc)) )
   {
      if (pPiBlock != NULL)
      {
         dpy = Xawtv_GetTvDisplay();
         if (dpy != NULL)
         {
            errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

            pReq = xiccc.pPermStationQueue;
            while (pReq != NULL)
            {
               dprintf2("Xawtv-IcccReplyStation: update wid 0x%X: PI '%s'\n", (int)pReq->requestor, PI_GET_TITLE(pPiBlock));

               pPiDescr = Xawtv_IcccPiQuery(pPiBlock, pReq->msg.setstation.epgPiCount);
               if (pPiDescr != NULL)
               {
                  Xiccc_SendReply(&xiccc, pPiDescr, -1, pReq, xiccc.atoms._NXTVEPG_SETSTATION);
                  xfree(pPiDescr);
               }
               pReq = pReq->pNext;
            }

            while (xiccc.pSendStationQueue != NULL)
            {
               pReq = xiccc.pSendStationQueue;
               Xiccc_QueueUnlinkEvent(&xiccc.pSendStationQueue, pReq);

               dprintf2("Xawtv-IcccReplyStation: reply to wid 0x%X: PI '%s'\n", (int)pReq->requestor, PI_GET_TITLE(pPiBlock));

               pPiDescr = Xawtv_IcccPiQuery(pPiBlock, pReq->msg.setstation.epgPiCount);
               if (pPiDescr != NULL)
               {
                  Xiccc_SendReply(&xiccc, pPiDescr, -1, pReq, xiccc.atoms._NXTVEPG_SETSTATION);
                  xfree(pPiDescr);
               }

               if (pReq->msg.setstation.epgUpdate)
                  Xiccc_QueueAddEvent(&xiccc.pPermStationQueue, pReq);
               else
                  xfree(pReq);
            }

            Tk_DeleteErrorHandler(errHandler);
         }
         else
            debug0("Xawtv-IcccReplyStation: failed to get description");
      }
   }
}

// ---------------------------------------------------------------------------
// Query the station name from an ICCCM client
// - expected format: PROTO/version, station name, frequency, channel, VPS/PDC CNI,
//   return EPG format, return EPG PI#
//
static bool Xawtv_IcccQueryStation( char * pBuffer, int bufLen, int * pTvFreq )
{
   bool result = FALSE;

   if (xiccc_last_station.isNew)
   {
      // first (i.e. latest) valid request in queue -> return this station name
      strncpy(pBuffer, xiccc_last_station.stationName, bufLen);
      if (bufLen > 0)
         pBuffer[bufLen - 1] = 0;
      *pTvFreq = xiccc_last_station.freq;

      result = TRUE;
   }
   else
   {
      *pTvFreq = 0;
      result = FALSE;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Handle messages sent from peers & other communication events
//
static void Xawtv_IcccServeRequest( ClientData clientData )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;
   XICCC_EV_QUEUE * pReq;
   bool first;

   if (xawtvcf.xicccProto)
   {
      dpy = Xawtv_GetTvDisplay();

      if (dpy != NULL)
      {
         // push an /dev/null handler onto the stack because remote window might no longer exist
         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

         // internal event handler must be called first - may generate application level events
         if ( IS_XICCC_INTERNAL_EVENT(xiccc.events) )
         {
            Xiccc_HandleInternalEvent(&xiccc);
         }

         if (xiccc.events & XICCC_LOST_PEER)
         {
            xiccc.events &= ~XICCC_LOST_PEER;
            Xawtv_TvAttach(NULL);
         }
         if (xiccc.events & XICCC_NEW_PEER)
         {
            xiccc.events &= ~XICCC_NEW_PEER;
            Xawtv_TvAttach(NULL);
         }
         if (xiccc.events & XICCC_GOT_MGMT)
         {
            xiccc.events &= ~XICCC_GOT_MGMT;
         }
         if (xiccc.events & XICCC_LOST_MGMT)
         {
            xiccc.events &= ~XICCC_LOST_MGMT;
         }

         if (xiccc.events & XICCC_SETSTATION_REQ)
         {
            xiccc.events &= ~XICCC_SETSTATION_REQ;

            first = TRUE;
            while (xiccc.pNewStationQueue != NULL)
            {
               pReq = xiccc.pNewStationQueue;
               Xiccc_QueueUnlinkEvent(&xiccc.pNewStationQueue, pReq);

               if (Xiccc_ParseMsgSetstation(dpy, pReq, xiccc.atoms._NXTVEPG_SETSTATION) )
               {
                  Xiccc_QueueRemoveRequest(&xiccc.pPermStationQueue, pReq->requestor);
                  Xiccc_QueueAddEvent(&xiccc.pSendStationQueue, pReq);
                  if ( first )
                  {
                     xiccc_last_station = pReq->msg.setstation;
                     first = FALSE;
                  }
               }
               else
               {  // discard invalid requests
                  xfree(pReq);
               }
            }
            Xawtv_StationSelected(NULL);
         }

         if (xiccc.events & XICCC_REMOTE_REQ)
         {
            char ** pArgv;
            char * pResult;
            uint  argc;

            xiccc.events &= ~XICCC_REMOTE_REQ;

            while (xiccc.pRemoteCmdQueue != NULL)
            {
               pReq = xiccc.pRemoteCmdQueue;
               Xiccc_QueueUnlinkEvent(&xiccc.pRemoteCmdQueue, pReq);

               pResult = NULL;
               if (Xiccc_SplitArgv(dpy, pReq->requestor,
                                   xiccc.atoms._NXTVEPG_REMOTE, &pArgv, &argc))
               {
                  if ((argc > 0) && (pRemoteCmdHandler != NULL))
                  {
                     pResult = pRemoteCmdHandler(pArgv, argc);
                  }
                  xfree(pArgv);
               }

               if (pResult != NULL)
               {
                  Xiccc_SendReply(&xiccc, pResult, -1, pReq, xiccc.atoms._NXTVEPG_REMOTE);
                  xfree(pResult);
               }
               else
                  Xiccc_SendReply(&xiccc, "", 0, pReq, xiccc.atoms._NXTVEPG_REMOTE);

               xfree(pReq);
            }
         }

         // remove the dummy error handler
         Tk_DeleteErrorHandler(errHandler);
      }
   }
}

// ----------------------------------------------------------------------------
// Compare given name with all network names
// - returns 0 if the given name doesn't match any known networks
//
static uint Xawtv_MapName2Cni( const char * station )
{
   const AI_BLOCK *pAiBlock;
   const char * pNetName;
   uchar netwop;
   uint cni;

   cni = 0;

   EpgDbLockDatabase(pUiDbContext, TRUE);
   pAiBlock = EpgDbGetAi(pUiDbContext);
   if (pAiBlock != NULL)
   {
      for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
      {
         // get user-configured name for this network (fall-back to AI name, if none)
         pNetName = EpgSetup_GetNetName(pAiBlock, netwop, NULL);

         if ((pNetName[0] == station[0]) && (strcmp(pNetName, station) == 0))
         {
            cni  = AI_GET_NET_CNI_N(pAiBlock, netwop);
            break;
         }
      }
   }
   EpgDbLockDatabase(pUiDbContext, FALSE);

   return cni;
}

// ----------------------------------------------------------------------------
// Determine TV program by CNI and PIL or current time
// - CNI may have been derived by name (i.e. mapped by name to CNI in AI)
//   or from live TV; in the latter case it's been converted to PDC
//
static const PI_BLOCK * Xawtv_SearchCurrentPi( uint cni, uint pil )
{
   FILTER_CONTEXT *fc;
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK *pPiBlock;
   const AI_NETWOP *pNetwop;
   time_t now;
   uchar netwop;
   
   assert(EpgDbIsLocked(pUiDbContext));
   pPiBlock = NULL;
   now = time(NULL);

   pAiBlock = EpgDbGetAi(pUiDbContext);
   if ((pAiBlock != NULL) && (cni != 0))
   {
      // convert the CNI parameter to a netwop index
      pNetwop = AI_GET_NETWOPS(pAiBlock);
      for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++, pNetwop++ ) 
         if (cni == AI_GET_NET_CNI_N(pAiBlock, netwop))
            break;

      // if not found: try 2nd time with conversion to PDC
      if (netwop >= pAiBlock->netwopCount)
      {
         pNetwop = AI_GET_NETWOPS(pAiBlock);
         for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++, pNetwop++ ) 
            if ( IS_NXTV_CNI(AI_GET_NET_CNI(pNetwop)) &&
                 (cni == CniConvertUnknownToPdc(AI_GET_NET_CNI(pNetwop))) )
               break;
      }

      if (netwop < pAiBlock->netwopCount)
      {
         if (VPS_PIL_IS_VALID(pil))
         {
            // search the running title by its PIL
            pPiBlock = EpgDbSearchPiByPil(pUiDbContext, netwop, pil);
         }

         if (pPiBlock == NULL)
         {
            // search the running title by its running time

            fc = EpgDbFilterCreateContext();
            // filter for the given network and start time >= now
            EpgDbFilterInitNetwop(fc);
            EpgDbFilterSetNetwop(fc, netwop);
            EpgDbFilterEnable(fc, FILTER_NETWOP);

            pPiBlock = EpgDbSearchFirstPiAfter(pUiDbContext, now, RUNNING_AT, fc);

            if ((pPiBlock != NULL) && (pPiBlock->start_time > now))
            {  // found one, but it's in the future
               debug2("Xawtv-SearchCurrentPi: first PI on netwop %d is %d minutes in the future", netwop, (int)(pPiBlock->start_time - now));
               pPiBlock = NULL;
            }

            EpgDbFilterDestroyContext(fc);
         }
      }
   }

   return pPiBlock;
}

// ----------------------------------------------------------------------------
// Timer handler to destroy the xawtv popup
//
static void Xawtv_PopDownNowNext( ClientData clientData )
{
   if (popDownEvent != NULL)
   {  // if not called by the timer remove the timer event
      dprintf0("Xawtv-PopDownNowNext: remove timer handler\n");
      Tcl_DeleteTimerHandler(popDownEvent);
      popDownEvent = NULL;
   }
   dprintf0("Xawtv-PopDownNowNext: destroy the popup\n");

   switch (xawtvcf.popType)
   {
      case POP_EXT:
      case POP_VTX:
      case POP_VTX2:
         eval_check(interp, "catch {destroy .xawtv_epg}");
         break;

      case POP_APP:
      case POP_MSG:
         // title message is removed automatically by remote application
         break;

      default:
         SHOULD_NOT_BE_REACHED;
         break;
   }
}

static void Xawtv_PopDownNowNextTimer( ClientData clientData )
{
   popDownEvent = NULL;
   AddMainIdleEvent(Xawtv_PopDownNowNext, NULL, TRUE);
}

// ----------------------------------------------------------------------------
// Display title and running time info for the given PI
// - the method of display can be configured by the user
// - the display is removed after a user-configured time span by a timer handler
//   (except for the "message" method, for which xawtv has an internal timer)
//
static void Xawtv_NowNext( const PI_BLOCK *pPiBlock )
{
   char start_str[10], stop_str[10], time_str[35];
   float percentage;
   uint  cmdLen;
   time_t now;
   time_t start_time;
   time_t stop_time;
   Tcl_Obj * pCmdObj;

   if (pPiBlock != NULL)
   {
      start_time = pPiBlock->start_time;
      strftime(start_str, 10, "%H:%M", localtime(&start_time));

      stop_time = pPiBlock->stop_time;
      strftime(stop_str, 10, "%H:%M", localtime(&stop_time));

      now = time(NULL);
      if ( (now < pPiBlock->start_time) ||
           (pPiBlock->start_time == pPiBlock->stop_time) )
         percentage = 0.0;
      else if (now < pPiBlock->stop_time)
         percentage = (double)(now - pPiBlock->start_time) / (pPiBlock->stop_time - pPiBlock->start_time);
      else
         percentage = 1.0;

      // remove previous popdown timer
      if (popDownEvent != NULL)
         Tcl_DeleteTimerHandler(popDownEvent);
      popDownEvent = NULL;

      switch (xawtvcf.popType)
      {
         case POP_EXT:
         case POP_VTX:
         case POP_VTX2:
            // generate popup window next to xawtv window
            sprintf(time_str, "%s - %s", start_str, stop_str);
            Xawtv_Popup(percentage, time_str, PI_GET_TITLE(pPiBlock));
            break;

         case POP_MSG:
            // send a command to xawtv to display the info in the xawtv window title
            cmdLen = sprintf(comm, "message%c%s-%s %s%c", 0,
                             start_str, stop_str, PI_GET_TITLE(pPiBlock), 0);
            Xawtv_SendCmdArgv(NULL, comm, cmdLen);
            break;

         case POP_APP:
            pCmdObj = Xawtv_ReadConfigAppCmd(interp);
            if (pCmdObj != NULL)
            {
               PiOutput_ExecuteScript(interp, pCmdObj, pPiBlock);
            }
            break;

         default:
            SHOULD_NOT_BE_REACHED;
            break;
      }

      if ((xawtvcf.popType != POP_MSG) && (xawtvcf.duration > 0))
      {
         // install a timer handler to destroy the popup
         popDownEvent = Tcl_CreateTimerHandler(1000 * xawtvcf.duration, Xawtv_PopDownNowNextTimer, NULL);
      }
   }
}

// ----------------------------------------------------------------------------
// Display EPG OSD for the given PI block
// - currently only used for debug purposes (by vbirec)
//
void Xawtv_SendEpgOsd( const PI_BLOCK *pPiBlock )
{
   if (xawtv_wid != None)
      Xawtv_NowNext(pPiBlock);

   Xawtv_IcccReplyStation(pPiBlock, TRUE);
}

// ----------------------------------------------------------------------------
// React upon new CNI and/or PIL
// - CNI may have been acquired either through xawtv property or VPS/PDC
//   PIL changes can be detected via VPS/PDC only (on networks which support it)
// - first search for a matching PI in the database
//   if not found, just remove a possible old popup
// - action depends on user configuration: can be popup and/or cursor movement
//
static void Xawtv_FollowTvNetwork( void )
{
   const PI_BLOCK *pPiBlock;
   
   EpgDbLockDatabase(pUiDbContext, TRUE);
   pPiBlock = Xawtv_SearchCurrentPi(followTvState.cni, followTvState.pil);
   if (pPiBlock != NULL)
   {
      if ( (followTvState.cni != followTvState.lastCni) ||
           (pPiBlock->start_time != followTvState.lastStartTime) )
      {
         // remember PI to suppress double-popups
         followTvState.lastCni       = followTvState.cni;
         followTvState.lastStartTime = pPiBlock->start_time;

         // display the programme information
         if (xawtvcf.doPop)
         {
            if (xawtv_wid != None)
               Xawtv_NowNext(pPiBlock);
         }
         Xawtv_IcccReplyStation(pPiBlock, TRUE);

         // jump with the cursor on the current programme
         if (xawtvcf.follow)
            PiBox_GotoPi(pPiBlock);
      }
      else
         Xawtv_IcccReplyStation(pPiBlock, FALSE);
   }
   else
   {  // unsupported network or no appropriate PI found -> remove popup
      Xawtv_PopDownNowNext(NULL);
      Xawtv_IcccReplyNullStation();
   }
   EpgDbLockDatabase(pUiDbContext, FALSE);
}

// ----------------------------------------------------------------------------
// Poll VPS/PDC for channel changes
// - invoked (indirectly) by acq ctl when a new CNI or PIL is available
// - if CNI or PIL changed, the user configured action is launched
//
static void Xawtv_PollVpsPil( ClientData clientData )
{
   EPG_ACQ_VPS_PDC vpsPdc;
   EPGACQ_DESCR acqState;

   if ((xawtvcf.follow || xawtvcf.doPop) && (followTvState.stationPoll == 0))
   {
      if ( EpgAcqCtl_GetVpsPdc(&vpsPdc, VPSPDC_REQ_TVAPP, FALSE) &&
           WintvUi_CheckAirTimes(vpsPdc.cni) )
      {
         if ( (followTvState.cni != vpsPdc.cni) ||
              ((vpsPdc.pil != followTvState.pil) && VPS_PIL_IS_VALID(vpsPdc.pil)) )
         {
            followTvState.pil = vpsPdc.pil;
            followTvState.cni = vpsPdc.cni;
            dprintf5("Xawtv_PollVpsPil: %02d.%02d. %02d:%02d (0x%04X)\n", (vpsPdc.pil >> 15) & 0x1F, (vpsPdc.pil >> 11) & 0x0F, (vpsPdc.pil >>  6) & 0x1F, (vpsPdc.pil      ) & 0x3F, vpsPdc.cni);

            EpgAcqCtl_DescribeAcqState(&acqState);
            // ignore channel changes on a server running on a different host
            if ((acqState.isNetAcq == FALSE) || acqState.isLocalServer)
            {
               // ignore the PIL change if the acquisition is running in active mode
               // because it then must be using a different tuner card
               if ( (acqState.mode == ACQMODE_PASSIVE) ||
                    (acqState.passiveReason == ACQPASSIVE_ACCESS_DEVICE) )
               {
                  Xawtv_FollowTvNetwork();
               }
            }
         }
      }
   }
   pollVpsEvent = Tcl_CreateTimerHandler(200, Xawtv_PollVpsPil, NULL);
}

// ----------------------------------------------------------------------------
// Idle handler to invoke Follow-TV from timer handler
//
static void Xawtv_FollowTvHandler( ClientData clientData )
{
   Xawtv_FollowTvNetwork();
   followTvState.stationCni  = 0;
   followTvState.stationPoll = 0;
}

// ----------------------------------------------------------------------------
// Timer handler for VPS polling after property notification
//
static void Xawtv_StationTimer( ClientData clientData )
{
   EPG_ACQ_VPS_PDC vpsPdc;
   bool keepWaiting = FALSE;

   pollVpsEvent = NULL;
   assert(followTvState.stationPoll > 0);

   if ( EpgAcqCtl_GetVpsPdc(&vpsPdc, VPSPDC_REQ_TVAPP, FALSE) && (vpsPdc.cni != 0) &&
        WintvUi_CheckAirTimes(vpsPdc.cni) )
   {  // VPS data received - check if it's the expected CNI
      if ( (vpsPdc.cni == followTvState.stationCni) || (followTvState.stationCni == 0) ||
           (followTvState.stationPoll >= 360) )
      {
         followTvState.cni = vpsPdc.cni;
         followTvState.pil = vpsPdc.pil;
         dprintf7("Xawtv-StationPollVpsPil: after %d ms: 0x%04X: PIL: %02d.%02d. %02d:%02d (0x%04X)\n", followTvState.stationPoll, vpsPdc.cni, (vpsPdc.pil >> 15) & 0x1F, (vpsPdc.pil >> 11) & 0x0F, (vpsPdc.pil >>  6) & 0x1F, (vpsPdc.pil      ) & 0x3F, vpsPdc.cni );
      }
      else
      {  // not the expected CNI -> keep waiting
         dprintf7("Xawtv-StationPollVpsPil: Waiting for 0x%04X, got 0x%04X: PIL: %02d.%02d. %02d:%02d (0x%04X)\n", followTvState.stationCni, vpsPdc.cni, (vpsPdc.pil >> 15) & 0x1F, (vpsPdc.pil >> 11) & 0x0F, (vpsPdc.pil >>  6) & 0x1F, (vpsPdc.pil      ) & 0x3F, vpsPdc.cni );
         keepWaiting = TRUE;
      }
   }
   else
   {  // no VPS reception -> wait some more, until limit is reached
      if (followTvState.stationPoll >= 360)
      {
         dprintf0("Xawtv-StationPollVpsPil: no PIL received\n");
         followTvState.cni = followTvState.stationCni;
         followTvState.pil = INVALID_VPS_PIL;
      }
      else
         keepWaiting = TRUE;
   }

   if (keepWaiting == FALSE)
   {  // dispatch the popup from the main loop
      AddMainIdleEvent(Xawtv_FollowTvHandler, NULL, TRUE);

      pollVpsEvent = Tcl_CreateTimerHandler(200, Xawtv_PollVpsPil, NULL);
   }
   else
   {  // keep waiting
      pollVpsEvent = Tcl_CreateTimerHandler(120, Xawtv_StationTimer, NULL);
      followTvState.stationPoll += 120;
   }
}

// ----------------------------------------------------------------------------
// Notification about channel change by xawtv
// - invoked after a property notification event
//
static void Xawtv_StationSelected( ClientData clientData )
{
   EPGACQ_DESCR acqState;
   char station[50];
   sint tvCurFreq;
   uint cni;
   char * p;

   {
      // query xawtv property, i.e. which station was selected
      if ( Xawtv_IcccQueryStation(station, sizeof(station), &tvCurFreq) ||
           Xawtv_QueryRemoteStation(xawtv_wid, station, sizeof(station), &tvCurFreq) )
      {
         if (pollVpsEvent != NULL)
         {
            Tcl_DeleteTimerHandler(pollVpsEvent);
            pollVpsEvent = NULL;
            followTvState.stationPoll = 0;
         }
         // for vbirec only (Tcl function is empty in nxtvepg)
         // (dirty hack: we're calling a C function, but via Tcl to avoid a dependency)
         while ((p = strchr(station, '{')) != NULL)
            *p = '(';
         while ((p = strchr(station, '}')) != NULL)
            *p = ')';
         sprintf(comm, "XawtvConfigShmChannelChange {%s} %d\n", station, tvCurFreq);
         eval_check(interp, comm);

         // there definately was a station change -> remove suppression of popup
         followTvState.lastCni = 0;

         // translate the station name to a CNI by searching the network table
         cni = WintvUi_StationNameToCni(station, Xawtv_MapName2Cni);

         if (followTvState.newXawtvStarted)
         {  // have the acq control update it's device state
            followTvState.newXawtvStarted = FALSE;
            EpgAcqCtl_CheckDeviceAccess();
         }

         EpgAcqCtl_DescribeAcqState(&acqState);
         if ( (acqState.ttxGrabState != ACQDESCR_DISABLED) &&
              ((acqState.isNetAcq == FALSE) || acqState.isLocalServer) &&
              ( (acqState.mode == ACQMODE_PASSIVE) ||
                (acqState.passiveReason == ACQPASSIVE_ACCESS_DEVICE)) &&
              ( (followTvState.cni != cni) ||
                (followTvState.pil == INVALID_VPS_PIL) ) )
         {  // acq running -> wait for VPS/PDC to determine PIL (and confirm CNI)
            dprintf1("Xawtv-StationSelected: delay xawtv info for CNI 0x%04X\n", cni);
            followTvState.stationCni = cni;
            followTvState.stationPoll = 120;

            // clear old VPS results, just like after a channel change
            EpgAcqCtl_ResetVpsPdc();
            pollVpsEvent = Tcl_CreateTimerHandler(120, Xawtv_StationTimer, NULL);
         }
         else
         {
            // acq not running or running on a different TV card -> don't wait for VPS
            dprintf1("Xawtv-StationSelected: popup xawtv info for CNI 0x%04X\n", cni);
            if (followTvState.cni != cni)
               followTvState.pil = INVALID_VPS_PIL;
            followTvState.cni = cni;
            Xawtv_FollowTvNetwork();
         }
      }
      else
         debug1("Xawtv-StationSelected: no atom found on window 0x%X", (int)xawtv_wid);
   }

   if (pollVpsEvent == NULL)
      pollVpsEvent = Tcl_CreateTimerHandler(200, Xawtv_PollVpsPil, NULL);
}

// ----------------------------------------------------------------------------
// Notification about creation or destruction of xawtv window
//
static void Xawtv_TvAttach( ClientData clientData )
{
   if ( (xawtv_wid != None) || XICCC_HAS_PEER(&xiccc) )
   {
      eval_check(interp, "XawtvConfigShmAttach 1\n");

      // note: device access is checked upon first property event
      // to give the other app enough time to open the video device
   }
   else
   {
      AddMainIdleEvent(Xawtv_PopDownNowNext, NULL, TRUE);
      eval_check(interp, "XawtvConfigShmAttach 0\n");

      // have the acq control update it's device state
      EpgAcqCtl_CheckDeviceAccess();
   }

   xiccc_last_station.isNew = FALSE;
}

// ---------------------------------------------------------------------------
// Tcl callback to send a remote command to a connected TV application
//
static int Xawtv_SendCmd( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_SendCmd <command> [<args> [<...>]]";
   Tcl_DString *pass_dstr, *tmp_dstr;
   char * pass;
   int idx, len;
   int result;

   if (objc < 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      // sum up the total length of all parameters, including terminating 0-Bytes
      pass_dstr = xmalloc(sizeof(Tcl_DString) * objc);  // allocate one too many
      dprintf0("Xawtv-SendCmd (via Tcl): ");
      len = 0;
      for (idx = 1; idx < objc; idx++)
      {
         tmp_dstr = pass_dstr + idx - 1;
         // convert Tcl internal Unicode to Latin-1
         Tcl_UtfToExternalDString(NULL, Tcl_GetString(objv[idx]), -1, tmp_dstr);
         dprintf1("%s, ", Tcl_DStringValue(tmp_dstr));
         len += Tcl_DStringLength(tmp_dstr) + 1;
      }
      dprintf0("\n");

      // concatenate the parameters into one char-array, separated by 0-Bytes
      pass = xmalloc(len);
      len = 0;
      pass[0] = 0;
      for (idx = 1; idx < objc; idx++)
      {
         tmp_dstr = pass_dstr + idx - 1;
         strcpy(pass + len, Tcl_DStringValue(tmp_dstr));
         len += strlen(pass + len) + 1;
         Tcl_DStringFree(tmp_dstr);
      }
      xfree(pass_dstr);

      Xawtv_SendCmdArgv(interp, pass, len);

      xfree(pass);
      result = TCL_OK;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Tcl callback to re-send EPG OSD info to connected TV application
//
static int Xawtv_ShowEpg( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_ShowEpg";
   int result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Query which TV app we're connected to, if any
//
static int Xawtv_IsConnected( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   bool connected;

   if ( XICCC_HAS_PEER(&xiccc) || (xawtv_wid != None) )
      connected = 1;
   else
      connected = 0;

   Tcl_SetObjResult(interp, Tcl_NewBooleanObj(connected));
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Query which TV app we're connected to, if any
//
static int Xawtv_QueryTvapp( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;
   Atom   type;
   int    format;
   ulong  nitems, bytesafter;
   char  *args;

   dpy = Xawtv_GetTvDisplay();
   if (dpy != NULL)
   {
      if ( XICCC_HAS_PEER(&xiccc) )
      {
         char ** pArgv;
         uint  argc;

         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

         if (Xiccc_SplitArgv(dpy, xiccc.remote_manager_wid,
                             xiccc.remote_manager_atom, &pArgv, &argc))
         {
            Tcl_SetObjResult(interp, TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, pArgv[MANAGER_PARM_APPNAME], NULL));
            xfree(pArgv);
         }
         else
         {  // failed to read name: still return a name since we are connected anyways
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown", -1));
         }
         Tk_DeleteErrorHandler(errHandler);
      }
      else if (xawtv_wid != None)
      {
         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

         if ( (XGetWindowProperty(dpy, xawtv_wid, wm_class_atom, 0, 64, False, AnyPropertyType,
                                  &type, &format, &nitems, &bytesafter, (uchar**)&args) == Success) && (args != NULL) )
         {
            if (type == XA_STRING)
            {
               // make null-terminated copy of the string
               if (nitems >= TCL_COMM_BUF_SIZE)
                  nitems = TCL_COMM_BUF_SIZE - 1;
               strncpy(comm, args, nitems);
               comm[nitems] = 0;
               dprintf1("Xawtv-QueryTvapp: wm-class: %s\n", comm);

               Tcl_SetObjResult(interp, TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, comm, NULL));
            }
            else if (type == XA_ATOM)
            {
               char * pAtomName = XGetAtomName(dpy, (Atom)(((long*)args)[0]));
               if (pAtomName != NULL)
               {
                  Tcl_SetObjResult(interp, TranscodeToUtf8(EPG_ENC_SYSTEM, NULL, pAtomName, NULL));
                  XFree(pAtomName);
               }
            }
            XFree(args);
         }
         Tk_DeleteErrorHandler(errHandler);
      }
      else
         dprintf0("Xawtv-QueryTvapp: not connected\n");
   }
   else
      debug0("Xawtv-QueryTvapp: failed to get display");

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Query window ID of peer
// - returns 0 if not connected or connected via XICCC protocol
//
int Xawtv_GetXawtvWid( void )
{
   return xawtv_wid;
}

// ----------------------------------------------------------------------------
// Read user-configured command for external OSD application
// 
static Tcl_Obj * Xawtv_ReadConfigAppCmd( Tcl_Interp *interp )
{
   Tcl_Obj  * pVarObj;
   Tcl_Obj ** pCfObjv;
   int  cfCount, idx;
   Tcl_Obj * result = NULL;

   pVarObj = Tcl_GetVar2Ex(interp, "xawtvcf", NULL, TCL_GLOBAL_ONLY);
   if (pVarObj != NULL)
   {
      if (Tcl_ListObjGetElements(interp, pVarObj, &cfCount, &pCfObjv) == TCL_OK)
      {
         // parse config list; format is pairs of keyword and value
         for (idx=0; idx + 1 < cfCount; idx += 2)
         {
            if (strcmp("appcmd", Tcl_GetString(pCfObjv[idx])) == 0)
            {
               result = pCfObjv[idx + 1];
               break;
            }
         }
      }
      else
         debugTclErr(interp, "Xawtv-ReadConfigAppCmd: Parse error on xawtvcf");
   }
   else
      dprintf0("Xawtv-ReadConfigAppCmd: xawtvcf variable not defined\n");

   return result;
}

// ----------------------------------------------------------------------------
// Read user configuration from Tcl global variables
// 
static int Xawtv_ReadConfig( Tcl_Interp *interp, XAWTVCF *pNewXawtvcf )
{
   int tunetv, follow, doPop, popType, duration;
   int xawtvProto, xicccProto;
   const char * pTmpStr;
   CONST84 char ** cfArgv;
   int  cfArgc, idx;
   int result = TCL_ERROR;

   memset(pNewXawtvcf, 0, sizeof(*pNewXawtvcf));  // compiler dummy

   pTmpStr = Tcl_GetVar(interp, "xawtvcf", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      result = Tcl_SplitList(interp, pTmpStr, &cfArgc, &cfArgv);
      if (result == TCL_OK)
      {
         if ((cfArgc & 1) == 0)
         {
            // copy old values into "int" variables for Tcl conversion funcs
            xawtvProto = xawtvcf.xawtvProto;
            xicccProto = xawtvcf.xicccProto;
            tunetv   = xawtvcf.tunetv;
            follow   = xawtvcf.follow;
            doPop    = xawtvcf.doPop;
            popType  = xawtvcf.popType;
            duration = xawtvcf.duration;

            // parse config list; format is pairs of keyword and value
            for (idx=0; (idx + 1 < cfArgc) && (result == TCL_OK); idx += 2)
            {
               if (strcmp("tunetv", cfArgv[idx]) == 0)
                  result = Tcl_GetBoolean(interp, cfArgv[idx + 1], &tunetv);
               else if (strcmp("follow", cfArgv[idx]) == 0)
                  result = Tcl_GetBoolean(interp, cfArgv[idx + 1], &follow);
               else if (strcmp("dopop", cfArgv[idx]) == 0)
                  result = Tcl_GetBoolean(interp, cfArgv[idx + 1], &doPop);
               else if (strcmp("poptype", cfArgv[idx]) == 0)
                  result = Tcl_GetInt(interp, cfArgv[idx + 1], &popType);
               else if (strcmp("duration", cfArgv[idx]) == 0)
                  result = Tcl_GetInt(interp, cfArgv[idx + 1], &duration);
               else if (strcmp("xawtvProto", cfArgv[idx]) == 0)
                  result = Tcl_GetInt(interp, cfArgv[idx + 1], &xawtvProto);
               else if (strcmp("xicccProto", cfArgv[idx]) == 0)
                  result = Tcl_GetInt(interp, cfArgv[idx + 1], &xicccProto);
               else if (strcmp("appcmd", cfArgv[idx]) == 0)
               {  // not cached, i.e. read dynamically
                  //result = Tcl_GetString(interp, cfArgv[idx + 1]);
               }
               else
               {
                  debug1("C_Xawtv_ReadConfig: unknown config type: %s", cfArgv[idx]);
                  result = TCL_ERROR;
               }
            }

            if (popType >= POP_COUNT)
            {
               debug2("C_Xawtv_ReadConfig: illegal popup type: %d (valid 0..%d)", popType, POP_COUNT-1);
               result = TCL_ERROR;
            }

            if (result == TCL_OK)
            {
               if (pNewXawtvcf != NULL)
               {  // all went well -> copy new values into the config struct
                  pNewXawtvcf->xawtvProto = xawtvProto;
                  pNewXawtvcf->xicccProto = xicccProto;
                  pNewXawtvcf->tunetv   = tunetv;
                  pNewXawtvcf->follow   = follow;
                  pNewXawtvcf->doPop    = doPop;
                  pNewXawtvcf->popType  = popType;
                  pNewXawtvcf->duration = duration;
               }
               else
                  fatal0("Xawtv-ReadConfig: illegal NULL param");
            }
         }
         else
         {
            debug1("C_Xawtv_ReadConfig: odd number of list elements: %s", pTmpStr);
            result = TCL_ERROR;
         }
         Tcl_Free((char *) cfArgv);
      }
      else
         debugTclErr(interp, "Parse error on xawtvcf");
   }
   else
      dprintf0("Xawtv-ReadConfig: xawtvcf variable not defined\n");

   return result;
}

// ----------------------------------------------------------------------------
// Handler to trigger search for xawtv window in idle after start-up
// - not done during init to speed-up the startup precedure
//
static void Xawtv_InitDelayHandler( ClientData clientData )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;

   // check if a xawtv handle is needed ant not available yet
   if ( (xawtvcf.xawtvProto) &&
        (xawtvcf.tunetv || xawtvcf.follow || xawtvcf.doPop) &&
        (xawtv_wid == None) )
   {
      dpy = Xawtv_GetTvDisplay();
      if (dpy != NULL)
      {
         // push an /dev/null handler onto the stack that catches any type of X11 error events
         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);
         if ( Xawtv_FindWindow(dpy, xawtv_station_atom) )
         {
            // install an event handler for property changes in xawtv
            dprintf1("Install PropertyNotify for window 0x%X\n", (int)xawtv_wid);
            XSelectInput(dpy, xawtv_wid, StructureNotifyMask | PropertyChangeMask);
            AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);

            // no events required from the root window
            Xawtv_SetRootWindowEvMask(dpy, root_wid, 0);
         }
         else
         {  // install event handler on the root window to check for xawtv being started
            dprintf1("Install SubstructureNotify for root window 0x%X\n", (int)root_wid);
            Xawtv_SetRootWindowEvMask(dpy, root_wid, SubstructureNotifyMask);
         }
         // remove the dummy error handler
         Tk_DeleteErrorHandler(errHandler);
      }
   }
}

// ----------------------------------------------------------------------------
// Read user configuration from Tcl global variables
// - called during init and after user config changes
// - loads config from rc/ini file
// - searches xawtv window and sets up X event mask
//
static int Xawtv_InitConfig( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   Tk_ErrorHandler errHandler;
   Display *dpy;
   XAWTVCF newXawtvCf;
   bool old_xicccProto;
   bool isFirstCall = PVOID2INT(ttp);

   if (pollVpsEvent != NULL)
   {
      Tcl_DeleteTimerHandler(pollVpsEvent);
      pollVpsEvent = NULL;
   }

   // remove existing popup by the old method
   if (isFirstCall == FALSE)
      Xawtv_PopDownNowNext(NULL);

   // load config from rc/ini file
   if (Xawtv_ReadConfig(interp, &newXawtvCf) == TCL_OK)
   {
      // copy new config over the old
      old_xicccProto = xawtvcf.xicccProto;
      xawtvcf = newXawtvCf;

      // create or remove the "Tune TV" button
      if (xawtvcf.tunetv == FALSE)
         eval_check(interp, "RemoveTuneTvButton\n");
      else
         eval_check(interp, "CreateTuneTvButton\n");

      {
         dpy = Xawtv_GetTvDisplay();
         if (dpy != NULL)
         {
            if ( (xawtvcf.xawtvProto) &&
                 (xawtvcf.tunetv || xawtvcf.follow || xawtvcf.doPop) )
            {  // xawtv window id required for communication

               // search for an xawtv window
               if (xawtv_wid != None)
               {
                  // push an /dev/null handler onto the stack that catches any type of X11 error events
                  errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

                  // install an event handler for property changes in xawtv
                  dprintf1("Xawtv-InitConfig: Install PropertyNotify for window 0x%X\n", (int)xawtv_wid);
                  XSelectInput(dpy, xawtv_wid, StructureNotifyMask | PropertyChangeMask);
                  AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);
                  // no events required from the root window
                  Xawtv_SetRootWindowEvMask(dpy, root_wid, 0);

                  // remove the dummy error handler
                  Tk_DeleteErrorHandler(errHandler);
               }
               else
               {  // search for an existing xawtv window immediately afterwards
                  AddMainIdleEvent(Xawtv_InitDelayHandler, NULL, TRUE);
               }
            }
            else
            {  // connection no longer required
               if (xawtv_wid != None)
               {
                  dprintf1("Xawtv-InitConfig: remove event mask on window 0x%X\n", (int)xawtv_wid);
                  //XSelectInput(dpy, xawtv_wid, 0);
                  AddMainIdleEvent(Xawtv_TvAttach, NULL, TRUE);
               }
               if (root_wid != None)
                  Xawtv_SetRootWindowEvMask(dpy, root_wid, 0);
               xawtv_wid = None;
               parent_wid = None;
            }

            if (xawtvcf.xicccProto)
            {
               char * pIdArgv;
               uint   idLen;

               errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

               // free resources (required before calling init)
               if (old_xicccProto)
                  Xiccc_Destroy(&xiccc);

               Xiccc_BuildArgv(&pIdArgv, &idLen, ICCCM_PROTO_VSTR, "nxtvepg", EPG_VERSION_STR, "", NULL);
               Xiccc_Initialize(&xiccc, dpy, TRUE, pIdArgv, idLen);
               xfree(pIdArgv);

               Xiccc_ClaimManagement(&xiccc, FALSE);
               xiccc_last_station.isNew = FALSE;

               Xiccc_SearchPeer(&xiccc);
               Tk_DeleteErrorHandler(errHandler);

               if (xiccc.events != 0)
               {
                  AddMainIdleEvent(Xawtv_IcccServeRequest, NULL, TRUE);
               }
            }
            else
            {
               Xiccc_Destroy(&xiccc);
            }
         }
      }

      if (xawtvcf.follow || xawtvcf.doPop)
      {  // create a timer to regularily poll for VPS/PDC
         pollVpsEvent = Tcl_CreateTimerHandler(200, Xawtv_PollVpsPil, NULL);
      }
   }
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Shut the module down
//
void Xawtv_Destroy( void )
{
   if (pollVpsEvent != NULL)
   {
      Tcl_DeleteTimerHandler(pollVpsEvent);
      pollVpsEvent = NULL;
   }
   if (popDownEvent != NULL)
   {
      Tcl_DeleteTimerHandler(popDownEvent);
      popDownEvent = NULL;
   }

   if (xawtvcf.xicccProto)
   {
      Xiccc_Destroy(&xiccc);
   }

   if (classBufferEvent != NULL)
   {
      Tcl_DeleteTimerHandler(classBufferEvent);
      classBufferEvent = NULL;
   }
   Xawtv_ClassBufferDestroy();
}

// ----------------------------------------------------------------------------
// Initialize the module
// - optionally an alternate display can be specified for the TV app window
//
void Xawtv_Init( char * pTvX11Display, MAIN_REMOTE_CMD_HANDLER * pRemCmdCb )
{
   Tk_Window   tkwin;
   Tk_Window   tvwin;
   Display   * dpy;

   // Create callback functions
   Tcl_CreateObjCommand(interp, "C_Tvapp_InitConfig", Xawtv_InitConfig, (ClientData) FALSE, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_SendCmd", Xawtv_SendCmd, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_ShowEpg", Xawtv_ShowEpg, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_QueryTvapp", Xawtv_QueryTvapp, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_IsConnected", Xawtv_IsConnected, (ClientData) NULL, NULL);

   pRemoteCmdHandler = pRemCmdCb;

   if (pTvX11Display != NULL)
   {
      Tcl_ResetResult(interp);
      tkwin = Tk_MainWindow(interp);
      if (tkwin != NULL)
      {  // create a dummy window to force Tk to open the 2nd X11 display for the TV app
         tvwin = Tk_CreateWindow(interp, tkwin, ".tv_screen_dummy", pTvX11Display);
         if (tvwin != NULL)
         {
            alternate_dpy = Tk_Display(tvwin);
            if (alternate_dpy != NULL)
            {
               dprintf1("Using alternate display '%s' for TV app\n", DisplayString(alternate_dpy));
               root_wid = RootWindowOfScreen(Tk_Screen(tvwin));
            }
         }
         else
         {  // failed to open the display, e.g. due to access permissions -> display message to user
            fprintf(stderr, "Failed to open TV app display '%s': %s\n", pTvX11Display, Tcl_GetStringResult(interp));
         }
      }
      else
         debug1("Xawtv-Init: failed to query main window id: %s\n", Tcl_GetStringResult(interp));

      Tcl_ResetResult(interp);
   }

   {
      dpy = Xawtv_GetTvDisplay();
      if (dpy != NULL)
      {
         // create atoms for communication with TV client
         xawtv_station_atom = XInternAtom(dpy, "_XAWTV_STATION", False);
         xawtv_remote_atom = XInternAtom(dpy, "_XAWTV_REMOTE", False);
         wm_class_atom = XInternAtom(dpy, "WM_CLASS", False);

         if (root_wid == None)
            root_wid = RootWindowOfScreen(DefaultScreenOfDisplay(dpy));

         Tk_CreateGenericHandler(Xawtv_EventNotification, NULL);
      }
   }

   // clear window ID buffer
   memset(&xawtvWidScanBuf, 0, sizeof(xawtvWidScanBuf));

   // read user config and initialize local vars
   Xawtv_InitConfig((ClientData) TRUE, interp, 0, NULL);

   dprintf0("Xawtv-Init: done.\n");
}

#endif  // not WIN32
