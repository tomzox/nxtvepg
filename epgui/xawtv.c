/*
 *  Nextview decoder: xawtv remote control module
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
 *    This module implements integration with the xawtv Linux TV viewer.
 *    It provides services to read the .xawtv configuration file, detect
 *    external channel changes and transfer EPG info to xawtv.
 *
 *    It's serivces are used for the following purposes: 1. synchronize
 *    station names with the .xawtv configuration file, 2. read channel
 *    list from .xawtv for EPG scan, 3. tune a selected network's channel
 *    into xawtv, 4. receive channel change notifications from xawtv,
 *    5. transfer title information to xawtv.
 *
 *  Authors:
 *
 *     Parts of this code originate from Netscape (author unknown).
 *     Those parts have been adapted for xawtv-remote.c by Gerd Knorr
 *     (kraxel@bytesex.org). The toplevel query could be improved with
 *     methods copied from ssetroot of the tvtwm package by Mark
 *     Lillibridge, MIT Project Athena, and Tcl/Tk version 8.3.
 *     Parser patterns for .xawtv have been directly lifted from
 *     xawtv-3.41 by Gerd Knorr.
 *
 *     Additional code and adaptions to nxtvepg by Tom Zoerner
 *
 *  $Id: xawtv.c,v 1.20 2002/05/01 12:23:50 tom Exp tom $
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
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/tvchan.h"
#include "epgvbi/btdrv.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbacq.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgacqctl.h"
#include "epgui/epgmain.h"
#include "epgui/pilistbox.h"
#include "epgui/xawtv.h"


// default allocation size for frequency table (will grow if required)
#define CHAN_CLUSTER_SIZE  50

Atom xawtv_station_atom = None;
Atom xawtv_remote_atom = None;
Atom wm_class_atom = None;

Window xawtv_wid = None;
Window parent_wid = None;
Window root_wid = None;

Tcl_TimerToken popDownEvent = NULL;
Tcl_TimerToken pollVpsEvent = NULL;

// possible EPG info display types in xawtv
typedef enum
{
   POP_EXT,
   POP_VTX,
   POP_VTX2,
   POP_MSG,
   POP_COUNT
} POPTYPE;

// structure to hold user configuration (copy of global Tcl variables)
typedef struct
{
   bool     tunetv;
   bool     follow;
   bool     doPop;
   POPTYPE  popType;
   uint     duration;
} XAWTVCF;

static XAWTVCF xawtvcf = {1, 1, 1, POP_EXT, 7};

// forward declaration
static void Xawtv_StationSelected( ClientData clientData );
static bool Xawtv_QueryRemoteStation( Window wid, char * pBuffer, int bufLen );
static void Xawtv_PopDownNowNext( ClientData clientData );

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
// Check if an .xawtv config file exists
//
static bool Xawtv_Check( void )
{
   static bool initialized = FALSE;
   static bool enabled = FALSE;
   char * pHome, * pPath;

   if (initialized == FALSE)
   {
      pHome = getenv("HOME");
      if (pHome != NULL)
      {
         pPath = xmalloc(strlen(pHome) + 7 + 1);
         strcpy(pPath, pHome);
         strcat(pPath, "/.xawtv");

         enabled = (access(pPath, R_OK) == 0);

         xfree(pPath);
      }
      initialized = TRUE;
   }

   return enabled;
}

// ----------------------------------------------------------------------------
// Tcl interface to Xawtv_Check
//
static int Xawtv_Enabled(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_Tvapp_Enabled";
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      Tcl_SetResult(interp, (Xawtv_Check() ? "1" : "0"), TCL_STATIC);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Open the .xawtv configuration file
// - must be closed by caller!
//
static FILE * Xawtv_OpenRcFile( void )
{
   char * pHome, * pPath;
   FILE * fp;

   fp = NULL;
   pHome = getenv("HOME");
   if (pHome != NULL)
   {
      pPath = xmalloc(strlen(pHome) + 7 + 1);
      strcpy(pPath, pHome);
      strcat(pPath, "/.xawtv");

      fp = fopen(pPath, "r");

      xfree(pPath);
   }
   return fp;
}

// ----------------------------------------------------------------------------
// Extract all channels from $HOME/.xawtv
//
bool Xawtv_GetFreqTab( Tcl_Interp * interp, ulong ** pFreqTab, uint * pCount )
{
   ulong *freqTab;
   int freqCount, freqTabLen;
   char line[256], tag[64], value[192];
   ulong freq;
   FILE * fp;
   bool result = FALSE;

   fp = Xawtv_OpenRcFile();
   if (fp != NULL)
   {
      freqTabLen  = CHAN_CLUSTER_SIZE;
      freqTab     = xmalloc(freqTabLen * sizeof(ulong));
      freqCount   = 0;

      // read all lines of the file (ignoring section structure)
      while (fgets(line, 255, fp) != NULL)
      {
         // search for channel assignment lines, e.g. " channel = SE15"
         if (sscanf(line," %63[^= ] = %191[^\n]", tag, value) == 2)
         {
            if (strcmp(tag, "channel") == 0)
            {
               // convert channel name to frequency and append it to the list
               freq = TvChannels_NameToFreq(value);
               if (freq != 0)
               {
                  if (freqCount == freqTabLen)
                  {  // table is full -> allocate and copy
                     ulong * oldFreqTab = freqTab;
                     freqTab = xmalloc((freqTabLen + CHAN_CLUSTER_SIZE) * sizeof(ulong));
                     memcpy(freqTab, oldFreqTab, freqTabLen * sizeof(ulong));
                     freqTabLen += CHAN_CLUSTER_SIZE;
                     xfree(oldFreqTab);
                  }
                  dprintf2("CHANNEL \"%s\" -> FREQ %f\n", value, (double)freq/16.0);
                  freqTab[freqCount] = freq;
                  freqCount += 1;
               }
               else
                  debug1("Xawtv-GetFreqTab: parse error channel value \"%s\"", value);
            }
         }
      }
      fclose(fp);

      if (freqCount == 0)
      {  // no channel assignments found in the file -> warn the user and abort
         sprintf(comm, "tk_messageBox -type ok -icon error -message {No channel assignments found in ~/.xawtv\nPlease disable option 'Use .xawtv'}\n");
         eval_check(interp, comm);

         xfree(pFreqTab);
         pFreqTab = NULL;
      }

      *pFreqTab = freqTab;
      *pCount = freqCount;
      result = TRUE;
   }
   else
   {
      sprintf(comm, "tk_messageBox -type ok -icon error -message {Could not open file ~/.xawtv\nMake the file readable or disable option 'Use .xawtv'}\n");
      eval_check(interp, comm);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the .xawtv configuration file
// - the parsing pattern used here is lifted directly from xawtv-3.41
// - station names are defined on single lines in brackets, e.g. [ARD]
// - names containing '/' characters are assumed to be multi-network channels,
//   e.g. "[arte / kinderkanal]". Those are broken up around the separator and
//   each segment added separately to the output list
//
static int Xawtv_GetStationNames(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_Tvapp_GetStationNames";
   char line[256], section[100];
   char *ps, *pc, *pe, c;
   FILE * fp;
   int result = TCL_ERROR;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      fp = Xawtv_OpenRcFile();
      if (fp != NULL)
      {
         // read all lines of the file (ignoring section structure)
         while (fgets(line, 255, fp) != NULL)
         {
            // search for section headers, i.e. "[station]"
            if (sscanf(line,"[%99[^]]]", section) == 1)
            {
               if ( (strcmp(section, "global") != 0) &&
                    (strcmp(section, "defaults") != 0) &&
                    (strcmp(section, "launch") != 0) )
               {
                  Tcl_AppendElement(interp, section);

                  // split multi-channel names at separator '/' and append each segment separately
                  ps = section;
                  pe = strchr(ps, '/');
                  if (pe != NULL)
                  {
                     do
                     {
                        pc = pe;
                        // remove trailing white space
                        while ((pc > ps) && (*(pc - 1) == ' '))
                            pc--;
                        if (pc > ps)
                        {
                           c = *pc;
                           *pc = 0;
                           Tcl_AppendElement(interp, ps);
                           *pc = c;
                        }
                        ps = pe + 1;
                        // skip whitespace following the separator
                        while (*ps == ' ')
                            ps++;
                     }
                     while ((pe = strchr(ps, '/')) != NULL);

                     // add the segment after the last separator
                     while (*ps == ' ')
                        ps++;
                     if (*ps != 0)
                        Tcl_AppendElement(interp, ps);
                  }
               }
            }
         }
         fclose(fp);
      }
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Dummy X11 error event handler for debugging purposes
//
#ifdef DEBUG_SWITCH
static int Xawtv_X11ErrorHandler( ClientData clientData, XErrorEvent *errEventPtr )
{
   debug5("X11 error: type=%d serial=%lu err=%d req=%d res=0x%lX", errEventPtr->type, errEventPtr->serial, errEventPtr->error_code, errEventPtr->request_code, errEventPtr->resourceid);
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
   int i;

   for (i = 0; i < nitems; i += strlen(prop + i) + 1)
      dprintf1("%s ", prop + i);
   dprintf0("\n");
}
#else
# define DebugDumpProperty(X,Y)
#endif

// ---------------------------------------------------------------------------
// Check the class of a newly created window
// - to find xawtv we cannot check for the STATION property on the window,
//   because that one's not available right away when the window is created
//
static bool Xawtv_QueryClass( Display * dpy, Window wid )
{
   Tk_ErrorHandler errHandler;
   Atom   type;
   int    format;
   ulong  nitems, bytesafter;
   uchar  *args;
   bool   result = FALSE;

   // push an /dev/null handler onto the stack that catches any type of X11 error events
   errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

   if ( (XGetWindowProperty(dpy, wid, wm_class_atom, 0, 64, False, AnyPropertyType,
                            &type, &format, &nitems, &bytesafter, &args) == Success) && (args != NULL) )
   {
      dprintf1("Xawtv-Query: wm-class: %s\n", args);
      if (strcasecmp(args, "XAWTV") == 0)
      {
         xawtv_wid = wid;
         // parent is unknown, because the window is not managed by the wm yet
         parent_wid = None;
         result = TRUE;
      }
   }

   // remove the dummy error handler
   Tk_DeleteErrorHandler(errHandler);

   return result;
}

// ----------------------------------------------------------------------------
// Find the uppermost parent of the xawtv window beneath the root
// - direct child of the root window is needed to query dimensions
// - error handler must be installed by caller!
//
static bool Xawtv_QueryParent( Display * dpy, Window wid )
{
   Window root, root_ret, parent, *kids;
   uint   nkids;
   bool   result = FALSE;

   root = RootWindowOfScreen(DefaultScreenOfDisplay(dpy));
   while ( (wid != root) &&
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
// Event handler
// - this function is called for every incoming X event
// - filters out events from the xawtv main window
//
static int Xawtv_EventNotification( ClientData clientData, XEvent *eventPtr )
{
   bool result = FALSE;

   if ( (eventPtr->type == PropertyNotify) &&
        (eventPtr->xproperty.window == xawtv_wid) && (xawtv_wid != None) )
   {
      dprintf1("PropertyNotify event from window 0x%X\n", (int)eventPtr->xproperty.window);
      if (eventPtr->xproperty.atom == xawtv_station_atom)
      {
         if (eventPtr->xproperty.state == PropertyNewValue)
         {  // the station has changed
            AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);
         }
      }
      result = TRUE;
   }
   else if ( (eventPtr->type == CreateNotify) &&
             (eventPtr->xcreatewindow.parent == root_wid) && (root_wid != None) )
   {
      dprintf1("CreateNotify event from window 0x%X\n", (int)eventPtr->xcreatewindow.window);
      if ( (xawtv_wid == None) &&
           Xawtv_QueryClass(eventPtr->xcreatewindow.display, eventPtr->xcreatewindow.window) )
      {
         // remove the creation event notification
         XSelectInput(eventPtr->xcreatewindow.display, root_wid, 0);
         // install an event handler
         XSelectInput(eventPtr->xcreatewindow.display, xawtv_wid, StructureNotifyMask | PropertyChangeMask);
         // check if the tuner device is busy upon the first property event
         followTvState.newXawtvStarted = TRUE;
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
      XSelectInput(eventPtr->xdestroywindow.display, root_wid, SubstructureNotifyMask);
      // have the acq control update it's device state
      EpgAcqCtl_CheckDeviceAccess();
      // destroy the nxtvepg-controlled xawtv popup window
      if (xawtvcf.popType == POP_EXT)
         AddMainIdleEvent(Xawtv_PopDownNowNext, NULL, TRUE);
      result = TRUE;
   }
   else if ( (eventPtr->type == UnmapNotify) &&
             (eventPtr->xunmap.window == xawtv_wid) && (xawtv_wid != None) )
   {
      dprintf1("UnmapNotify event from window 0x%X\n", (int)eventPtr->xunmap.window);
      if (xawtvcf.popType == POP_EXT)
         AddMainIdleEvent(Xawtv_PopDownNowNext, NULL, TRUE);
      result = TRUE;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Search for the xawtv toplevel window
//
static bool Xawtv_FindWindow( Display * dpy, Atom atom )
{
   Window root, root2, parent, *kids, w;
   unsigned int nkids;
   bool result = FALSE;
   int n;
   Atom type;
   int format;
   ulong nitems, bytesafter;
   uchar *args = NULL;

   // get normal root window
   root = RootWindowOfScreen(DefaultScreenOfDisplay(dpy));

   if (XQueryTree(dpy, root, &root2, &parent, &kids, &nkids) != 0)
   {
      if (kids != NULL)
      {
         // process kids in opposite stacking order, i.e. topmost -> bottom
         for (n = nkids - 1; n >= 0; n--)
         {
            w = XmuClientWindow(dpy, kids[n]);

            args = NULL;
            if ( (XGetWindowProperty(dpy, w, atom, 0, (65536 / sizeof (long)), False, XA_STRING,
                                     &type, &format, &nitems, &bytesafter, &args) == Success) && (args != NULL) )
            {
               dprintf1("Found xawtv window 0x%08lx, STATION: ", w);
               DebugDumpProperty(args, nitems);
               XFree(args);

               xawtv_wid = w;
               parent_wid = kids[n];
               // check if the tuner device is busy upon the next property event
               followTvState.newXawtvStarted = TRUE;
               result = TRUE;
               break;   // NOTE: there might be more than window
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

// ---------------------------------------------------------------------------
// Display a popup window below xawtv
// - similar to SendCmd: first verify if the old window ID is still valid;
//   if not search for a new xawtv window
// - if the window manager's wrapper window (parent) is not known yet search it
// - query the window manager's wrapper window for the position and size
//
static void Xawtv_Popup( float rperc, const char *rtime, const char * ptitle )
{
   XWindowAttributes wat;
   Tk_ErrorHandler errHandler;
   Tk_Window tkwin;
   Display *dpy;
   int retry;

   tkwin = Tk_MainWindow(interp);
   if (tkwin != NULL)
   {
      dpy = Tk_Display(tkwin);
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
            }

            retry -= 1;
         }
         while ((xawtv_wid == None) && (retry >= 0));

         if ((xawtv_wid != None) && (wat.map_state == IsViewable))
         {
            sprintf(comm, "Create_XawtvPopup %d %d %d %d %f {%s} {%s}\n",
                          wat.x, wat.y, wat.width, wat.height, rperc, rtime, ptitle);
            eval_check(interp, comm);

            // make sure there's no popdown event scheduled
            RemoveMainIdleEvent(Xawtv_PopDownNowNext, NULL, FALSE);
         }

         // remove the dummy error handler
         Tk_DeleteErrorHandler(errHandler);
      }
      else
         debug0("No Tk display available");
   }
   else
      debug0("Tk main window not found");
}

// ---------------------------------------------------------------------------
// Send a command to xawtv via X11 property
//
static int Xawtv_SendCmd(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_Tvapp_SendCmd <command> [<args> [<...>]]";
   Tk_ErrorHandler errHandler;
   Tk_Window tkwin;
   Display *dpy;
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
      tkwin = Tk_MainWindow(interp);
      if (tkwin != NULL)
      {
         dpy = Tk_Display(tkwin);
         if (dpy != NULL)
         {
            // push an /dev/null handler onto the stack that catches any type of X11 error events
            errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

            // check if the cached window still exists
            if (xawtv_wid != None)
            {
               Atom type;
               int format;
               ulong nitems, bytesafter;
               uchar *args = NULL;

               XGetWindowProperty(dpy, xawtv_wid, xawtv_station_atom, 0, (65536 / sizeof (long)), False, XA_STRING,
                                  &type, &format, &nitems, &bytesafter, &args);
               if (args != NULL)
               {
                  dprintf1("SendCmd: xawtv alive, window 0x%08lx, STATION: ", xawtv_wid);
                  DebugDumpProperty(args, nitems);
                  XFree(args);
               }
               else
                  xawtv_wid = None;
            }

            // try to find a new xawtv window
            if (xawtv_wid == None)
            {
               if ( Xawtv_FindWindow(dpy, xawtv_station_atom) )
               {
                  dprintf1("Xawtv-SendCmd: Connect to xawtv 0x%lX\n", (ulong)xawtv_wid);
                  XSelectInput(dpy, xawtv_wid, StructureNotifyMask | PropertyChangeMask);
                  AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);
               }
            }

            if (xawtv_wid != None)
            {
               // sum up the total length of all parameters, including terminating 0-Bytes
               pass_dstr = xmalloc(sizeof(Tcl_DString) * argc);  // allocate one too many
               dprintf1("ctrl  0x%08lx: ", xawtv_wid);
               len = 0;
               for (idx = 1; idx < argc; idx++)
               {
                  tmp_dstr = pass_dstr + idx - 1;
                  // convert Tcl internal Unicode to Latin-1
                  Tcl_UtfToExternalDString(NULL, argv[idx], -1, tmp_dstr);
                  dprintf1("%s ", Tcl_DStringValue(tmp_dstr));
                  len += Tcl_DStringLength(tmp_dstr) + 1;
               }
               dprintf0("\n");

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

               // send the string to xawtv
               XChangeProperty(dpy, xawtv_wid, xawtv_remote_atom, XA_STRING, 8*sizeof(char), PropModeReplace, pass, len);

               xfree(pass);
            }
            else
            {  // xawtv window not found
               if (strcmp(argv[0], "C_Tvapp_SendCmd") == 0)
               {  // display warning only if called after user-interaction
                  sprintf(comm, "tk_messageBox -type ok -icon error -message \"xawtv is not running!\"\n");
                  eval_check(interp, comm);
               }
            }

            // remove the dummy error handler
            Tk_DeleteErrorHandler(errHandler);
         }
         else
            debug0("No Tk display available");
      }
      else
         debug0("Tk main window not found");

      result = TCL_OK;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Query the station name from the xawtv property
//
static bool Xawtv_QueryRemoteStation( Window wid, char * pBuffer, int bufLen )
{
   Tk_ErrorHandler errHandler;
   Tk_Window tkwin;
   Display *dpy;
   Atom type;
   int format, idx, argc;
   ulong nitems, bytesafter;
   uchar *args;
   bool result = FALSE;

   tkwin = Tk_MainWindow(interp);
   if (tkwin != NULL)
   {
      dpy = Tk_Display(tkwin);
      if (dpy != NULL)
      {
         // push an /dev/null handler onto the stack that catches any type of X11 error events
         errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);

         args = NULL;
         if ( (XGetWindowProperty(dpy, wid, xawtv_station_atom, 0, (65536 / sizeof (long)), False, XA_STRING,
                                  &type, &format, &nitems, &bytesafter, &args) == Success) && (args != NULL) )
         {
            // argument list is: frequency, channel, name
            for (idx=0, argc=0; idx < nitems; idx += strlen(args + idx) + 1, argc++)
            {
               if (argc == 2)
               {
                  dprintf2("Xawtv-Query: window 0x%lX: station name: %s\n", (ulong)wid, args + idx);
                  if (pBuffer != NULL)
                  {
                     strncpy(pBuffer, args + idx, bufLen);
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
   else
      debug0("Tk main window not found");

   return result;
}

// ----------------------------------------------------------------------------
// Convert station name to CNI
//
static uint Xawtv_StationNametoCni( const char * station )
{
   const AI_BLOCK *pAiBlock;
   const char * name;
   uchar cni_str[7], *cfgn;
   uchar netwop;
   uint cni;

   cni = 0;
   EpgDbLockDatabase(pUiDbContext, TRUE);
   pAiBlock = EpgDbGetAi(pUiDbContext);
   if (pAiBlock != NULL)
   {
      for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
      {
         name = AI_GET_NETWOP_NAME(pAiBlock, netwop);
         cni  = AI_GET_NETWOP_N(pAiBlock, netwop)->cni;
         sprintf(cni_str, "0x%04X", cni);
         cfgn = Tcl_GetVar2(interp, "cfnetnames", cni_str, TCL_GLOBAL_ONLY);

         if ( (cfgn != NULL) ?
              ((cfgn[0] == station[0]) && (strcmp(cfgn, station) == 0)) :
              ((name[0] == station[0]) && (strcmp(name, station) == 0)) )
         {
            break;
         }
      }

      // if no netwop found, return invalid value
      if (netwop >= pAiBlock->netwopCount)
         cni = 0;
   }
   EpgDbLockDatabase(pUiDbContext, FALSE);

   return cni;
}

// ----------------------------------------------------------------------------
// Determine TV program by PIL or current time
//
static const PI_BLOCK * Xawtv_SearchCurrentPi( uint cni, uint pil )
{
   FILTER_CONTEXT *fc;
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK *pPiBlock;
   time_t now;
   uchar netwop;
   
   pPiBlock = NULL;
   now = time(NULL);

   EpgDbLockDatabase(pUiDbContext, TRUE);
   pAiBlock = EpgDbGetAi(pUiDbContext);
   if (pAiBlock != NULL)
   {
      // convert the CNI parameter to a netwop index
      for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
         if (cni == AI_GET_NETWOP_N(pAiBlock, netwop)->cni)
            break;

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
            EpgDbFilterSetExpireTime(fc, now);
            EpgDbFilterEnable(fc, FILTER_NETWOP | FILTER_EXPIRE_TIME);

            pPiBlock = EpgDbSearchFirstPi(pUiDbContext, fc);
            if ((pPiBlock != NULL) && (pPiBlock->start_time > now))
            {  // found one, but it's in the future
               debug2("Xawtv-SearchCurrentPi: first PI on netwop %d is %d minutes in the future", netwop, (int)(pPiBlock->start_time - now));
               pPiBlock = NULL;
            }

            EpgDbFilterDestroyContext(fc);
         }
      }
   }
   EpgDbLockDatabase(pUiDbContext, FALSE);

   return pPiBlock;
}

// ----------------------------------------------------------------------------
// Timer handler to destroy the xawtv popup
//
static void Xawtv_PopDownNowNext( ClientData clientData )
{
   char *argv[10];

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
         eval_check(interp, "if {[string length [info commands .xawtv_epg]] > 0} {\n"
                            "   destroy .xawtv_epg\n"
                            "}\n");
         break;

      case POP_VTX:
      case POP_VTX2:
         argv[0] = "auto";
         argv[1] = "vtx";
         argv[2] = NULL;
         Xawtv_SendCmd(NULL, interp, 2, argv);
         break;

      case POP_MSG:
         // title message is removed automatically by xawtv
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
   char *argv[10];
   char start_str[10], stop_str[10], time_str[35];
   float percentage;
   time_t now;

   if (pPiBlock != NULL)
   {
      strftime(start_str, 10, "%H:%M", localtime(&pPiBlock->start_time));
      strftime(stop_str, 10, "%H:%M", localtime(&pPiBlock->stop_time));

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
            // generate popup window next to xawtv window
            sprintf(time_str, "%s-%s", start_str, stop_str);
            Xawtv_Popup(percentage, time_str, PI_GET_TITLE(pPiBlock));
            break;

         case POP_MSG:
            // send a command to xawtv to display the info in the xawtv window title
            sprintf(comm, "%s-%s %s", start_str, stop_str, PI_GET_TITLE(pPiBlock));
            argv[0] = "auto";
            argv[1] = "message";
            argv[2] = comm;
            argv[3] = NULL;
            Xawtv_SendCmd(NULL, interp, 3, argv);
            break;

         case POP_VTX:
            // send a command to xawtv to display an overlay window with the EPG info
            sprintf(comm, "%s-%s  %s", start_str, stop_str, PI_GET_TITLE(pPiBlock));
            argv[0] = "auto";
            argv[1] = "vtx";
            argv[2] = comm;
            argv[3] = NULL;
            Xawtv_SendCmd(NULL, interp, 3, argv);
            break;

         case POP_VTX2:
            // same as VTX, but 2 lines with additional percentage
            sprintf(time_str, "%s-%s (%d%%)", start_str, stop_str, (int)(percentage*100.0));
            sprintf(comm, "%s", PI_GET_TITLE(pPiBlock));
            argv[0] = "auto";
            argv[1] = "vtx";
            argv[2] = time_str;
            argv[3] = comm;
            argv[4] = NULL;
            Xawtv_SendCmd(NULL, interp, 4, argv);
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
            Xawtv_NowNext(pPiBlock);

         // jump with the cursor on the current programme
         if (xawtvcf.follow)
            PiListBox_GotoPi(pPiBlock);
      }
   }
   else
   {  // unsupported network or no appropriate PI found -> remove popup
      Xawtv_PopDownNowNext(NULL);
   }
}

// ----------------------------------------------------------------------------
// Poll VPS/PDC for channel changes
// - invoked (indirectly) by acq ctl when a new CNI or PIL is available
// - if CNI or PIL changed, the user configured action is launched
//
static void Xawtv_PollVpsPil( ClientData clientData )
{
   const EPGDB_ACQ_VPS_PDC * pVpsPdc;
   EPGACQ_DESCR acqState;

   if ((xawtvcf.follow || xawtvcf.doPop) && (followTvState.stationPoll == 0))
   {
      pVpsPdc = EpgAcqCtl_GetVpsPdc(VPSPDC_REQ_TVAPP);
      if (pVpsPdc != NULL)
      {
         if ( (followTvState.cni != pVpsPdc->cni) ||
              ((pVpsPdc->pil != followTvState.pil) && VPS_PIL_IS_VALID(pVpsPdc->pil)) )
         {
            followTvState.pil = pVpsPdc->pil;
            followTvState.cni = pVpsPdc->cni;
            dprintf5("Xawtv_PollVpsPil: %02d.%02d. %02d:%02d (0x%04X)\n", (pVpsPdc->pil >> 15) & 0x1F, (pVpsPdc->pil >> 11) & 0x0F, (pVpsPdc->pil >>  6) & 0x1F, (pVpsPdc->pil      ) & 0x3F, pVpsPdc->cni);

            EpgAcqCtl_DescribeAcqState(&acqState);
            // ignore channel changes on a server running on a different host
            if ((acqState.isNetAcq == FALSE) || acqState.isLocalServer)
            {
               // ignore the PIL change if the acquisition is running in active m
               // because it then must be using a different tuner card
               if ( (acqState.mode == ACQMODE_PASSIVE) ||
                    ((acqState.mode == ACQMODE_FORCED_PASSIVE) && (acqState.passiveReason == ACQPASSIVE_ACCESS_DEVICE)) )
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
   const EPGDB_ACQ_VPS_PDC * pVpsPdc;
   bool keepWaiting = FALSE;

   pollVpsEvent = NULL;
   assert(followTvState.stationPoll > 0);

   pVpsPdc = EpgAcqCtl_GetVpsPdc(VPSPDC_REQ_TVAPP);
   if ((pVpsPdc != NULL) && (pVpsPdc->cni != 0))
   {  // VPS data received - check if it's the expected CNI
      if ((pVpsPdc->cni == followTvState.stationCni) || (followTvState.stationPoll >= 360))
      {
         followTvState.cni = pVpsPdc->cni;
         followTvState.pil = pVpsPdc->pil;
         dprintf7("Xawtv-StationPollVpsPil: after %d ms: 0x%04X: PIL: %02d.%02d. %02d:%02d (0x%04X)\n", followTvState.stationPoll, pVpsPdc->cni, (pVpsPdc->pil >> 15) & 0x1F, (pVpsPdc->pil >> 11) & 0x0F, (pVpsPdc->pil >>  6) & 0x1F, (pVpsPdc->pil      ) & 0x3F, pVpsPdc->cni );
      }
      else
      {  // not the expected CNI -> keep waiting
         dprintf7("Xawtv-StationPollVpsPil: Waiting for 0x%04X, got 0x%04X: PIL: %02d.%02d. %02d:%02d (0x%04X)\n", followTvState.stationCni, pVpsPdc->cni, (pVpsPdc->pil >> 15) & 0x1F, (pVpsPdc->pil >> 11) & 0x0F, (pVpsPdc->pil >>  6) & 0x1F, (pVpsPdc->pil      ) & 0x3F, pVpsPdc->cni );
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
   uint cni;

   if (xawtv_wid != None)
   {
      // query xawtv property, i.e. which station was selected
      if ( Xawtv_QueryRemoteStation(xawtv_wid, station, sizeof(station)) )
      {
         if (pollVpsEvent != NULL)
         {
            Tcl_DeleteTimerHandler(pollVpsEvent);
            pollVpsEvent = NULL;
            followTvState.stationPoll = 0;
         }

         // there definately was a station change -> remove suppression of popup
         followTvState.lastCni = 0;

         // translate the station name to a CNI by searching the network table
         cni = Xawtv_StationNametoCni(station);
         if (cni != 0)
         {
            if (followTvState.newXawtvStarted)
            {  // have the acq control update it's device state
               followTvState.newXawtvStarted = FALSE;
               EpgAcqCtl_CheckDeviceAccess();
            }

            EpgAcqCtl_DescribeAcqState(&acqState);
            if ( (acqState.state != ACQDESCR_DISABLED) &&
                 ((acqState.isNetAcq == FALSE) || acqState.isLocalServer) &&
                 ( (acqState.mode == ACQMODE_PASSIVE) ||
                   ((acqState.mode == ACQMODE_FORCED_PASSIVE) && (acqState.passiveReason == ACQPASSIVE_ACCESS_DEVICE))) &&
                 ( (followTvState.cni != cni) ||
                   (followTvState.pil == INVALID_VPS_PIL) ) )
            {  // acq running -> wait for VPS/PDC to determine PIL (and confirm CNI)
               dprintf1("Xawtv-StationSelected: delay xawtv info for 0x%04X\n", cni);
               followTvState.stationCni = cni;
               followTvState.stationPoll = 120;

               // clear old VPS results, just like after a channel change
               EpgAcqCtl_ResetVpsPdc();
               pollVpsEvent = Tcl_CreateTimerHandler(120, Xawtv_StationTimer, NULL);
            }
            else
            {
               // acq not running or running on a different TV card -> don't wait for VPS
               dprintf1("Xawtv-StationSelected: popup xawtv info for 0x%04X\n", cni);
               if (followTvState.cni != cni)
                  followTvState.pil = INVALID_VPS_PIL;
               followTvState.cni = cni;
               Xawtv_FollowTvNetwork();
            }
         }
         else
         {  // unknown CNI -> remove existing popup
            // note that multi-network station may be identified via VPS shortly after
            dprintf1("Xawtv-StationSelected: unknown station: \"%s\"\n", station);
            Xawtv_PopDownNowNext(NULL);

            EpgAcqCtl_ResetVpsPdc();
         }
      }
      else
         debug1("Xawtv-StationSelected: no atom found on window 0x%X\n", (int)xawtv_wid);
   }

   if (pollVpsEvent == NULL)
      pollVpsEvent = Tcl_CreateTimerHandler(200, Xawtv_PollVpsPil, NULL);
}

// ----------------------------------------------------------------------------
// Read user configuration from Tcl global variables
// 
static int Xawtv_ReadConfig( Tcl_Interp *interp, XAWTVCF *pNewXawtvcf )
{
   int tunetv, follow, doPop, popType, duration;
   char * pTmpStr;
   char **cfArgv;
   int  cfArgc, idx;
   int result = TCL_ERROR;

   pTmpStr = Tcl_GetVar(interp, "xawtvcf", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      result = Tcl_SplitList(interp, pTmpStr, &cfArgc, &cfArgv);
      if (result == TCL_OK)
      {
         if ((cfArgc & 1) == 0)
         {
            // copy old values into "int" variables for Tcl conversion funcs
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
               else
               {
                  sprintf(comm, "C_Xawtv_ReadConfig: unknown config type: %s", cfArgv[idx]);
                  Tcl_SetResult(interp, comm, TCL_VOLATILE);
                  result = TCL_ERROR;
               }
            }

            if (popType >= POP_COUNT)
            {
               sprintf(comm, "C_Xawtv_ReadConfig: illegal popup type: %d (valid 0..%d)", popType, POP_COUNT-1);
               Tcl_SetResult(interp, comm, TCL_VOLATILE);
               result = TCL_ERROR;
            }

            if (result == TCL_OK)
            {
               if (pNewXawtvcf != NULL)
               {  // all went well -> copy new values into the config struct
                  pNewXawtvcf->tunetv   = tunetv;
                  pNewXawtvcf->follow   = follow;
                  pNewXawtvcf->doPop    = doPop;
                  pNewXawtvcf->popType  = popType;
                  pNewXawtvcf->duration = duration;
               }
               else
                  debug0("Xawtv-ReadConfig: illegal NULL param");
            }
         }
         else
         {
            sprintf(comm, "C_Xawtv_ReadConfig: odd number of list elements: %s", pTmpStr);
            Tcl_SetResult(interp, comm, TCL_VOLATILE);
            result = TCL_ERROR;
         }
         Tcl_Free((char *) cfArgv);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Read user configuration from Tcl global variables
// - called during init and after user config changes
// - loads config from rc/ini file
// - searches xawtv window and sets up X event mask
//
static int Xawtv_InitConfig(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   Tk_ErrorHandler errHandler;
   Tk_Window tkwin;
   Display *dpy;
   XAWTVCF newXawtvCf;
   bool isFirstCall = (bool)((int) ttp);
   int result = TCL_ERROR;

   if (pollVpsEvent != NULL)
   {
      Tcl_DeleteTimerHandler(pollVpsEvent);
      pollVpsEvent = NULL;
   }

   if ( Xawtv_Check() )
   {
      tkwin = Tk_MainWindow(interp);
      if (tkwin != NULL)
      {
         dpy = Tk_Display(tkwin);
         if (dpy != NULL)
         {
            // load config from rc/ini file
            result = Xawtv_ReadConfig(interp, &newXawtvCf);
            if (result == TCL_OK)
            {
               // remove existing popup by the old method
               if (isFirstCall == FALSE)
                  Xawtv_PopDownNowNext(NULL);

               // copy new config over the old
               xawtvcf = newXawtvCf;

               // create or remove the "Tune TV" button
               if ((xawtvcf.tunetv == FALSE) && (isFirstCall == FALSE))
                  eval_check(interp, "RemoveTuneTvButton\n");
               else if (xawtvcf.tunetv)
                  eval_check(interp, "CreateTuneTvButton\n");

               Tcl_SetVar(interp, "tvapp_name", "xawtv", TCL_GLOBAL_ONLY);

               if (xawtvcf.tunetv || xawtvcf.follow || xawtvcf.doPop)
               {  // xawtv window id required for communication

                  // push an /dev/null handler onto the stack that catches any type of X11 error events
                  errHandler = Tk_CreateErrorHandler(dpy, -1, -1, -1, (Tk_ErrorProc *) Xawtv_X11ErrorHandler, (ClientData) NULL);
                  // search for an xawtv window
                  if ( (xawtv_wid != None) || Xawtv_FindWindow(dpy, xawtv_station_atom) )
                  {
                     // install an event handler for property changes in xawtv
                     dprintf1("Install PropertyNotify for window 0x%X\n", (int)xawtv_wid);
                     XSelectInput(dpy, xawtv_wid, StructureNotifyMask | PropertyChangeMask);
                     AddMainIdleEvent(Xawtv_StationSelected, NULL, TRUE);
                     // no events required from the root window
                     XSelectInput(dpy, root_wid, 0);
                  }
                  else
                  {  // install event handler on the root window to check for xawtv being started
                     dprintf1("Install PropertyNotify for root window 0x%X\n", (int)root_wid);
                     XSelectInput(dpy, root_wid, SubstructureNotifyMask);
                  }

                  // remove the dummy error handler
                  Tk_DeleteErrorHandler(errHandler);
               }
               else
               {  // connection no longer required
                  if (xawtv_wid != None)
                     XSelectInput(dpy, xawtv_wid, 0);
                  if (root_wid != None)
                     XSelectInput(dpy, root_wid, 0);
                  xawtv_wid = None;
                  parent_wid = None;
               }

               if (xawtvcf.follow || xawtvcf.doPop)
               {  // create a timer to regularily poll for VPS/PDC
                  pollVpsEvent = Tcl_CreateTimerHandler(200, Xawtv_PollVpsPil, NULL);
               }
            }
         }
      }
   }
   return result;
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
}

// ----------------------------------------------------------------------------
// Initialize the module
//
void Xawtv_Init( void )
{
   Tk_Window tkwin;
   Display *dpy;
   bool  enabled = FALSE;

   // Create callback functions
   Tcl_CreateCommand(interp, "C_Tvapp_InitConfig", Xawtv_InitConfig, (ClientData) FALSE, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_Enabled", Xawtv_Enabled, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_GetStationNames", Xawtv_GetStationNames, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_SendCmd", Xawtv_SendCmd, (ClientData) NULL, NULL);

   // check for existance of .xawtv config file
   // if not present the module remains completely dead
   if ( Xawtv_Check() )
   {
      tkwin = Tk_MainWindow(interp);
      if (tkwin != NULL)
      {
         dpy = Tk_Display(tkwin);
         if (dpy != NULL)
         {
            xawtv_station_atom = XInternAtom(dpy, "_XAWTV_STATION", False);
            xawtv_remote_atom = XInternAtom(dpy, "_XAWTV_REMOTE", False);
            wm_class_atom = XInternAtom(dpy, "WM_CLASS", False);

            root_wid = RootWindowOfScreen(DefaultScreenOfDisplay(dpy));

            // read user config and initialize local vars
            Xawtv_InitConfig((ClientData) TRUE, interp, 0, NULL);

            Tk_CreateGenericHandler(Xawtv_EventNotification, NULL);

            enabled = TRUE;
         }
      }
      dprintf0("Xawtv-Init: done.\n");
   }

   // en-/disable TV app. entry in the "Configure" menu
   sprintf(comm, ".menubar.config entryconfigure \"TV app. interaction...\" -state %s\n",
                 (enabled ? "normal" : "disabled"));
   eval_check(interp, comm);
}

#endif  // not WIN32
