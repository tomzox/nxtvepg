/*
 *  X11 and Win32 window manager hooks
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
 *    This module implements a window manager hook to make a toplevel
 *    window stay on top, i.e. unobscured by any other windows.
 *    Currently only used for reminder message windows.
 *
 *  Author: X11 code by Gerd Knorr (code copied from xawtv-3.87/x11/wmhooks.c)
 *          Win32 version and Tcl interface by Tom Zoerner
 *
 *  $Id: wmhooks.c,v 1.2 2003/09/28 18:28:22 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#ifndef WIN32
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#else
#include <windows.h>
#endif

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgui/epgmain.h"
#include "epgui/wmhooks.h"

#ifndef WIN32
static Atom _NET_SUPPORTED;
static Atom _NET_WM_STATE;
static Atom _NET_WM_STATE_STAYS_ON_TOP;
static Atom _NET_WM_STATE_ABOVE;
static Atom _WIN_SUPPORTING_WM_CHECK;
static Atom _WIN_PROTOCOLS;
static Atom _WIN_LAYER;

static void (*wm_stay_on_top)(Display *dpy, Window win, int state) = NULL;

/* ------------------------------------------------------------------------ */

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */

static void
netwm_set_state(Display *dpy, Window win, int operation, Atom state)
{
    XEvent e;

    memset(&e,0,sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.message_type = _NET_WM_STATE;
    e.xclient.display = dpy;
    e.xclient.window = win;
    e.xclient.format = 32;
    e.xclient.data.l[0] = operation;
    e.xclient.data.l[1] = state;

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
	       SubstructureRedirectMask, &e);
}

static void
netwm_stay_on_top(Display *dpy, Window win, int state)
{
    int op = state ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    netwm_set_state(dpy,win,op,_NET_WM_STATE_ABOVE);
}

static void
netwm_old_stay_on_top(Display *dpy, Window win, int state)
{
    int op = state ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    netwm_set_state(dpy,win,op,_NET_WM_STATE_STAYS_ON_TOP);
}

/* ------------------------------------------------------------------------ */

#define WIN_LAYER_NORMAL                 4
#define WIN_LAYER_ONTOP                  6

/* tested with icewm + WindowMaker */
static void
gnome_stay_on_top(Display *dpy, Window win, int state)
{
    XClientMessageEvent  xev;

    if (0 == win)
	return;

    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.window = win;
    xev.message_type = _WIN_LAYER;
    xev.format = 32;
    xev.data.l[0] = state ? WIN_LAYER_ONTOP : WIN_LAYER_NORMAL;
    XSendEvent(dpy,DefaultRootWindow(dpy),False,
	       SubstructureNotifyMask,(XEvent*)&xev);
    if (state)
	XRaiseWindow(dpy,win);
}

/* ------------------------------------------------------------------------ */

static int
wm_check_capability(Display *dpy, Window root, Atom list, Atom wanted)
{
    Atom            type;
    int             format;
    unsigned int    i;
    unsigned long   nitems, bytesafter;
    unsigned char   *args;
    unsigned long   *ldata;
    int             retval = -1;
    
    if (Success != XGetWindowProperty
	(dpy, root, list, 0, (65536 / sizeof(long)), False,
	 AnyPropertyType, &type, &format, &nitems, &bytesafter, &args))
	return -1;
    if (type != XA_ATOM)
	return -1;
    ldata = (unsigned long*)args;
    for (i = 0; i < nitems; i++) {
	if (ldata[i] == wanted)
	    retval = 0;
#ifndef DPRINTF_OFF
        {
            char *name;
            name = XGetAtomName(dpy,ldata[i]);
            dprintf1("wm cap: %s\n",name);
            XFree(name);
        }
#endif
    }
    XFree(ldata);
    return retval;
}

#define INIT_ATOM(dpy,atom) atom = XInternAtom(dpy,#atom,False)

static void
wm_detect(Display *dpy)
{
    Window root = DefaultRootWindow(dpy);

    INIT_ATOM(dpy, _NET_SUPPORTED);
    INIT_ATOM(dpy, _NET_WM_STATE);
    INIT_ATOM(dpy, _NET_WM_STATE_STAYS_ON_TOP);
    INIT_ATOM(dpy, _NET_WM_STATE_ABOVE);
    INIT_ATOM(dpy, _WIN_SUPPORTING_WM_CHECK);
    INIT_ATOM(dpy, _WIN_PROTOCOLS);
    INIT_ATOM(dpy, _WIN_LAYER);

    /* netwm checks */
    if (NULL == wm_stay_on_top &&
	0 == wm_check_capability(dpy,root,_NET_SUPPORTED,
				 _NET_WM_STATE_ABOVE)) {
        dprintf0("wmhooks: netwm state above\n");
	wm_stay_on_top = netwm_stay_on_top;
    } 
    if (NULL == wm_stay_on_top &&
	0 == wm_check_capability(dpy,root,_NET_SUPPORTED,
				 _NET_WM_STATE_STAYS_ON_TOP)) {
        dprintf0("wmhooks: netwm state stays_on_top\n");
	wm_stay_on_top = netwm_old_stay_on_top;
    }

    /* gnome checks */
    if (NULL == wm_stay_on_top &&
	0 == wm_check_capability(dpy,root,_WIN_PROTOCOLS,_WIN_LAYER)) {
        dprintf0("wmhooks: gnome layer\n");
	wm_stay_on_top = gnome_stay_on_top;
    }
}

// ---------------------------------------------------------------------------
// X11 Tcl interface for stay on top hook
//
static int WmHooks_StayOnTop(ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[])
{
   const char * const pUsage = "Usage: C_Wm_StayOnTop <toplevel>";
   Tcl_Obj  * pId;
   Display  * dpy;
   Tk_Window  tkwin;
   int  wid;
   int  result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (wm_stay_on_top != NULL)
      {
         // query window ID of the window manager's frame window (if any)
         sprintf(comm, "wm frame %s\n", argv[1]);
         if (Tcl_EvalEx(interp, comm, -1, 0) == TCL_OK)
         {
            pId = Tcl_GetObjResult(interp);
            if (pId != NULL)
            {
               if (Tcl_GetIntFromObj(interp, pId, &wid) == TCL_OK)
               {
                  dprintf2("WmHooks_StayOnTop: window %s ID=0x%X\n", argv[1], wid);
                  if (wid != 0)
                  {
                     tkwin = Tk_MainWindow(interp);
                     if (tkwin != NULL)
                     {
                        dpy = Tk_Display(tkwin);
                        if (dpy != NULL)
                        {
                           wm_stay_on_top(dpy, (Window)wid, 1);
                        }
                     }
                     else
                        debug0("WmHooks-StayOnTop: failed to determine main window");
                  }
                  else
                     debug0("WmHooks-StayOnTop: got invalid ID 0 for window frame");
               }
               else
                  debug1("WmHooks-StayOnTop: frame ID has invalid format '%s'", Tcl_GetStringResult(interp));
            }
            else
               debug0("WmHooks-StayOnTop: Tcl error: frame ID result missing");
         }
         else
            debugTclErr(interp, "WmHooks-StayOnTop");
      }
      else
         dprintf0("WmHooks-StayOnTop: no handler available (unsupported WM)\n");

      result = TCL_OK;
   }
   return result;
}

#else //  WIN32

// ---------------------------------------------------------------------------
// WIN32 Tcl interface for stay on top hook
//
static int WmHooks_StayOnTop(ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[])
{
   const char * const pUsage = "Usage: C_Wm_StayOnTop <toplevel>";
   Tcl_Obj * pId;
   int   hWnd;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      sprintf(comm, "wm frame %s\n", argv[1]);
      if (Tcl_EvalEx(interp, comm, -1, 0) == TCL_OK)
      {
         pId = Tcl_GetObjResult(interp);
         if (pId != NULL)
         {
            if (Tcl_GetIntFromObj(interp, pId, &hWnd) == TCL_OK)
            {
               SetWindowPos((HWND)hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                            SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE);
            }
            else
               debug1("WinStayOnTop: frame ID has invalid format '%s'", Tcl_GetStringResult(interp));
         }
         else
            debug0("WinStayOnTop: Tcl error: frame ID result missing");
      }
      else
         debugTclErr(interp, "WinStayOnTop");

      result = TCL_OK;
   }
   return result;
}
#endif  // WIN32

// ----------------------------------------------------------------------------
// Shut the module down
//
void WmHooks_Destroy( void )
{
}

// ----------------------------------------------------------------------------
// Initialize the module
//
void WmHooks_Init( Tcl_Interp * interp )
{
#ifndef WIN32
   Tk_Window   tkwin;
   Display   * dpy;

   tkwin = Tk_MainWindow(interp);
   if (tkwin != NULL)
   {
      dpy = Tk_Display(tkwin);
      if (dpy != NULL)
      {
         wm_detect(dpy);
      }
   }
#endif

   Tcl_CreateCommand(interp, "C_Wm_StayOnTop", WmHooks_StayOnTop, (ClientData) NULL, NULL);
}

