/*
 *  Interaction with X11 TV-apps via Inter-Client Communication Conventions
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
 *    This module implements interaction with TV applications based on the
 *    Inter-Client Communication Conventions (an X Consortium Standard).
 *    The method used is peer-to-peer communication by means of selections.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: xiccc.c,v 1.3 2014/04/23 21:05:15 tom Exp tom $
 */

#ifdef WIN32
#error "This module is not intended for Win32 systems"
#else

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgui/xiccc.h"


// ---------------------------------------------------------------------------
// Remove all events in a queue
//
void Xiccc_QueueFree( XICCC_EV_QUEUE ** pHead )
{
   XICCC_EV_QUEUE *pWalk, *pNext;

   if (pHead != NULL)
   {
      pWalk = *pHead;
      while (pWalk != NULL)
      {
         pNext = pWalk->pNext;
         xfree(pWalk);
         pWalk = pNext;
      }
      *pHead = NULL;
   }
   else
      fatal0("Xiccc-QueueFree: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Unlink a specific event from the queue
// - memory must be freed or event queued in other queue by caller
//
void Xiccc_QueueUnlinkEvent( XICCC_EV_QUEUE ** pHead, XICCC_EV_QUEUE * pReq )
{
   XICCC_EV_QUEUE *pWalk, *pPrev;

   if ((pHead != NULL) && (pReq != NULL))
   {
      pWalk = *pHead;
      pPrev = NULL;
      while (pWalk != NULL)
      {
         if (pWalk == pReq)
         {
            if (pPrev != NULL)
               pPrev->pNext = pWalk->pNext;
            else
               *pHead = pWalk->pNext;

            break;
         }
         else
         {
            pPrev = pWalk;
            pWalk = pWalk->pNext;
         }
      }

      // request must be actually queued
      assert(pWalk != NULL);
   }
   else
      fatal0("Xiccc-Xiccc_QueueRemoveEvent: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Remove all events of a given requestor from the queue
//
void Xiccc_QueueRemoveRequest( XICCC_EV_QUEUE ** pHead, Window requestor )
{
   XICCC_EV_QUEUE *pWalk, *pPrev, *pTemp;

   if (pHead != NULL)
   {
      pWalk = *pHead;
      pPrev = NULL;
      while (pWalk != NULL)
      {
         if (pWalk->requestor == requestor)
         {
            if (pPrev != NULL)
               pPrev->pNext = pWalk->pNext;
            else
               *pHead = pWalk->pNext;

            pTemp = pWalk;
            pWalk = pWalk->pNext;

            xfree(pTemp);
         }
         else
         {
            pPrev = pWalk;
            pWalk = pWalk->pNext;
         }
      }
   }
   else
      fatal0("Xiccc-QueueRemoveRequest: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Add an event to the queue
// - new events are inserted at the head of the queue
//
void Xiccc_QueueAddEvent( XICCC_EV_QUEUE ** pHead, XICCC_EV_QUEUE * pNew )
{
   if ((pHead != NULL) && (pNew != NULL))
   {
      if (pNew->requestor != None)
      {
         // remove older events from the same source
         Xiccc_QueueRemoveRequest(pHead, pNew->requestor);

         pNew->pNext = *pHead;
         *pHead = pNew;
      }
      else
         debug0("Xiccc-QueueAddEvent: null window ID - not queued");
   }
   else
      fatal2("Xiccc-QueueAddEvent: illegal NULL ptr param: 0x%lX,0x%lX", (long)pHead, (long)pNew);
}

// ---------------------------------------------------------------------------
// Claim ownership of the selection
//
static void Xiccc_ClaimManagementStep2( XICCC_STATE * pXi, Time timestamp )
{
   Window owner_wid;
   XEvent ev;
   /*Status st;*/

   assert(timestamp != CurrentTime);
   assert(pXi->manager_wid != None);

   owner_wid = XGetSelectionOwner(pXi->dpy, pXi->manager_atom);
   if ( (pXi->manager_state == XICCC_WAIT_FORCED_CLAIM) || (owner_wid == None) )
   {
      XSetSelectionOwner(pXi->dpy, pXi->manager_atom, pXi->manager_wid, timestamp);
      owner_wid = XGetSelectionOwner(pXi->dpy, pXi->manager_atom);
      if (owner_wid == pXi->manager_wid)
      {
         dprintf2("Xiccc-ClaimManagement: 0wned selection 0x%X on wid 0x%X\n", (int)pXi->manager_atom, (int)pXi->manager_wid);
         pXi->manager_state = XICCC_READY;
         pXi->mgmt_start_time = timestamp;

         dprintf2("Xiccc-ClaimManagement: send MANAGER (0x%X) msg to root wid 0x%X\n", (int)pXi->atoms.MANAGER, (int)pXi->root_wid);
         memset(&ev,0,sizeof(ev));
         ev.xclient.type = ClientMessage;
         ev.xclient.display = pXi->dpy;
         ev.xclient.window = pXi->root_wid;
         ev.xclient.message_type = pXi->atoms.MANAGER;
         ev.xclient.format = 32;

         ev.xclient.data.l[0] = timestamp;
         ev.xclient.data.l[1] = pXi->manager_atom;
         ev.xclient.data.l[2] = pXi->manager_wid;

         /*st =*/ XSendEvent(pXi->dpy, pXi->root_wid, False, StructureNotifyMask, &ev);
      }
      else
      {
         debug1("Xiccc-ClaimManagement: failed to own selection: owner is 0x%X", (int)owner_wid);
         if (pXi->other_manager_wid != None)
         {
            pXi->other_manager_wid = owner_wid;
            XSelectInput(pXi->dpy, pXi->other_manager_wid, StructureNotifyMask);
            pXi->manager_state = XICCC_OTHER_OWNER;
         }
         else
            pXi->manager_state = XICCC_PASSIVE;
      }
   }
   else
   {
      dprintf1("Xiccc-ClaimManagement: selection already has an owner: 0x%X\n", (int)owner_wid);
      pXi->manager_state = XICCC_OTHER_OWNER;
      pXi->other_manager_wid = owner_wid;
      if (pXi->other_manager_wid != None)
      {
         // select for destruct event on manager
         XSelectInput(pXi->dpy, pXi->other_manager_wid, StructureNotifyMask);
      }
   }
}

// ---------------------------------------------------------------------------
// Release ownership of the selection voluntarily
//
static void Xiccc_ReleaseManagementStep2( XICCC_STATE * pXi, Time timestamp )
{
   Window owner_wid;

   owner_wid = XGetSelectionOwner(pXi->dpy, pXi->manager_atom);
   if (owner_wid == pXi->manager_wid)
   {
      // this will send a SelectionClear event to the previous owner
      XSetSelectionOwner(pXi->dpy, pXi->manager_atom, None, timestamp);
   }
   pXi->manager_state = XICCC_PASSIVE;
}

// ---------------------------------------------------------------------------
// Process events triggered by X11 messages
// - should be called from application main event loop with X11 error handler
//
void Xiccc_HandleInternalEvent( XICCC_STATE * pXi )
{
   if (pXi->events & XICCC_FOCUS_CLAIM)
   {
      pXi->events &= ~XICCC_FOCUS_CLAIM;
      if ( (pXi->manager_state == XICCC_WAIT_FORCED_CLAIM) ||
           (pXi->manager_state == XICCC_WAIT_CLAIM) )
      {
         Xiccc_ClaimManagementStep2(pXi, pXi->last_time);
         if (pXi->manager_state == XICCC_READY)
         {
            pXi->events |= XICCC_GOT_MGMT;
         }
      }
   }
   if (pXi->events & XICCC_FOCUS_CLEAR)
   {
      pXi->events &= ~XICCC_FOCUS_CLEAR;
      Xiccc_ReleaseManagementStep2(pXi, pXi->last_time);
      pXi->events |= XICCC_LOST_MGMT;
   }
   if (pXi->events & XICCC_FOCUS_LOST)
   {
      pXi->events &= ~XICCC_FOCUS_LOST;
      if (pXi->manager_state == XICCC_OTHER_OWNER)
      {
         // check again for new owner & install event handler to wait for release
         Xiccc_ClaimManagementStep2(pXi, pXi->last_time);
         if (pXi->manager_state != XICCC_READY)
         {
            pXi->events |= XICCC_LOST_MGMT;
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Handler for X events on manager or toplevel window
//
bool Xiccc_XEvent( XEvent *eventPtr, XICCC_STATE * pXi, bool * pNeedHandler )
{
   XICCC_EV_QUEUE * pReq;
   bool result = FALSE;

   *pNeedHandler = FALSE;

   if ( (eventPtr->type == PropertyNotify) &&
        (eventPtr->xproperty.window == pXi->manager_wid) &&
        (eventPtr->xproperty.atom == pXi->manager_atom) )
   {
      // property event on manager atom: only used during start-up and release
      if ( (pXi->manager_state == XICCC_WAIT_FORCED_CLAIM) ||
           (pXi->manager_state == XICCC_WAIT_CLAIM) )
      {
         pXi->last_time = eventPtr->xproperty.time;
         pXi->events |= XICCC_FOCUS_CLAIM;
         *pNeedHandler = TRUE;
      }
      else if (pXi->manager_state == XICCC_WAIT_CLEAR)
      {
         pXi->last_time = eventPtr->xproperty.time;
         pXi->events |= XICCC_FOCUS_CLEAR;
         *pNeedHandler = TRUE;
      }
      // else: can safely be ignored in all other states
      result = TRUE;
   }
   else if ( (eventPtr->type == SelectionRequest) &&
             (eventPtr->xselectionrequest.owner == pXi->manager_wid) &&
             (eventPtr->xselectionrequest.selection == pXi->manager_atom) )
   {
      dprintf3("SelectionRequest event from req wid 0x%X on target 0x%X property 0x%X\n", (int)eventPtr->xselectionrequest.requestor, (int)eventPtr->xselectionrequest.target, (int)eventPtr->xselectionrequest.property);
      // selection request, i.e. message from peer arrived
      if ( (pXi->manager_state == XICCC_READY) /*&&
           (pXi->mgmt_start_time <= eventPtr->xselectionrequest.time)*/ )
      {
         pReq = xmalloc(sizeof(*pReq));
         memset(pReq, 0, sizeof(*pReq));
         pReq->requestor = eventPtr->xselectionrequest.requestor;
         pReq->property = eventPtr->xselectionrequest.property;
         pReq->timestamp = eventPtr->xselectionrequest.time;
         pReq->msg.setstation.isNew = TRUE;

         if (eventPtr->xselectionrequest.target == pXi->atoms._NXTVEPG_SETSTATION)
         {
            Xiccc_QueueAddEvent(&pXi->pNewStationQueue, pReq);
            pXi->events |= XICCC_SETSTATION_REQ;
            *pNeedHandler = TRUE;
         }
         else if (eventPtr->xselectionrequest.target == pXi->atoms._NXTVEPG_REMOTE)
         {
            Xiccc_QueueAddEvent(&pXi->pRemoteCmdQueue, pReq);
            pXi->events |= XICCC_REMOTE_REQ;
            *pNeedHandler = TRUE;
         }
         else
         {
            debug2("Xawtv-IcccmEvent: unknown target 0x%X (wid 0x%X)", (int)eventPtr->xselectionrequest.target, (int)eventPtr->xselectionrequest.requestor);
            xfree(pReq);
         }
      }
      result = TRUE;
   }
   else if ( (eventPtr->type == SelectionNotify) &&
             (eventPtr->xselection.requestor == pXi->manager_wid) &&
             (eventPtr->xselection.selection == pXi->remote_manager_atom) )
   {
      // reply to one of our messages to the peer
      dprintf3("SelectionNotify event for window 0x%X on target 0x%X property 0x%X\n", (int)eventPtr->xselection.requestor, (int)eventPtr->xselection.target, (int)eventPtr->xselection.property);

      // silently ignore failed or rejceted queries
      if (eventPtr->xselection.property != None)
      {
         if (eventPtr->xselection.target == pXi->atoms._NXTVEPG_SETSTATION)
         {
            pXi->events |= XICCC_SETSTATION_REPLY;
            *pNeedHandler = TRUE;
         }
         else if (eventPtr->xselection.target == pXi->atoms._NXTVEPG_REMOTE)
         {
            pXi->events |= XICCC_REMOTE_REPLY;
            *pNeedHandler = TRUE;
         }
         else
            debug2("SelectionNotify event from wid 0x%X unknown: 0x%X", (int)eventPtr->xselection.requestor, (int)eventPtr->xselection.target);
      }
      result = TRUE;
   }
   else if ( (eventPtr->type == SelectionClear) &&
             (eventPtr->xselectionclear.selection == pXi->manager_atom) )
   {
      // someone else claimed ownership forcibly
      dprintf3("SelectionClear event for window 0x%X on target 0x%X property 0x%X\n", (int)eventPtr->xselection.requestor, (int)eventPtr->xselection.target, (int)eventPtr->xselection.property);
      if ( (eventPtr->xselectionclear.window != pXi->manager_wid) &&
           (pXi->manager_state != XICCC_PASSIVE) )
      {
         pXi->manager_state = XICCC_OTHER_OWNER;
         pXi->other_manager_wid = eventPtr->xselectionclear.window;

         pXi->last_time = eventPtr->xselectionclear.time;
         pXi->events |= XICCC_FOCUS_LOST;
         *pNeedHandler = TRUE;
      }
      result = TRUE;
   }
   else if ( (eventPtr->type == DestroyNotify) &&
             (pXi->manager_state == XICCC_OTHER_OWNER) &&
             (eventPtr->xdestroywindow.window == pXi->other_manager_wid) )
   {
      dprintf1("DestroyNotify event from window 0x%X (concurrent manager)\n", (int)eventPtr->xdestroywindow.window);
      // immediately reset window ID to prevent further references
      pXi->other_manager_wid = None;

      // dummy property change to generate an event
      XChangeProperty(pXi->dpy, pXi->manager_wid, pXi->manager_atom,
                      XA_STRING, 8*sizeof(char), PropModeAppend, NULL, 0);
      pXi->manager_state = XICCC_WAIT_CLAIM;

      result = TRUE;
   }
   else if ( (eventPtr->type == DestroyNotify) &&
             (eventPtr->xdestroywindow.window == pXi->remote_manager_wid) &&
             (pXi->remote_manager_wid != None) )
   {
      dprintf1("DestroyNotify event from window 0x%X (remote manager)\n", (int)eventPtr->xdestroywindow.window);
      // immediately reset window ID to prevent further references
      pXi->remote_manager_wid = None;

      pXi->events |= XICCC_LOST_PEER;
      *pNeedHandler = TRUE;
      result = TRUE;
   }
   else if ( (eventPtr->type == ClientMessage) &&
             (eventPtr->xclient.message_type == pXi->atoms.MANAGER) &&
             (eventPtr->xclient.window == pXi->root_wid) &&
             (eventPtr->xclient.data.l[1] == pXi->remote_manager_atom) )
   {
      dprintf3("ClientMessage on window 0x%X: new manager for selection 0x%lX: wid 0x%lX\n", (int)eventPtr->xclient.window, eventPtr->xclient.data.l[1], eventPtr->xclient.data.l[2]);

      if (pXi->remote_manager_wid != eventPtr->xclient.data.l[2])
      {
         // XXX release old remote peer?
         pXi->remote_manager_wid = eventPtr->xclient.data.l[2];
         // select for destroy events on the remote window
         // XXX TODO should be done inside error handler
         XSelectInput(pXi->dpy, pXi->remote_manager_wid, StructureNotifyMask);

         pXi->events |= XICCC_NEW_PEER;
         *pNeedHandler = TRUE;
      }
      result = TRUE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Assemble a 0-separated string vector from the given arguments
// - memory is allocated for ARGV, must be freed by the caller
//
void Xiccc_BuildArgv( char ** ppBuild, uint * pArgLen, ... )
{
   va_list argl;
   const char * pStr;
   char * pArgStr;

   // first iteration across param list to sum up lengths
   *pArgLen = 0;
   va_start(argl, pArgLen);
   while ( (pStr = va_arg(argl, const char *)) != NULL )
   {
      *pArgLen += strlen(pStr) + 1;
   }
   va_end(argl);

   *ppBuild = xmalloc(*pArgLen);

   // second iteration to concatenate strings (with zeros inbetween)
   pArgStr = *ppBuild;
   va_start(argl, pArgLen);
   while ( (pStr = va_arg(argl, const char *)) != NULL )
   {
      strcpy(pArgStr, pStr);
      pArgStr += strlen(pStr) + 1;
   }
   va_end(argl);
}

// ---------------------------------------------------------------------------
// Break an atom's 0-separated character array into an ARGV
// - memory is allocated for ARGV, must be freed by the caller
//
bool Xiccc_SplitArgv( Display * dpy, Window wid, Atom property, char *** ppArgv, uint * pArgc )
{
   Atom  type;
   int   format;
   ulong nitems, bytesafter;
   uchar * args;
   uchar * pStr;
   uint idx;
   uint argc;
   bool result = FALSE;

   if ((ppArgv != NULL) && (pArgc != NULL) && (dpy != NULL))
   {
      *ppArgv = NULL;
      *pArgc = 0;

      args = NULL;
      if ( (XGetWindowProperty(dpy, wid, property, 0, (65536 / sizeof(long)), False, XA_STRING,
                               &type, &format, &nitems, &bytesafter, &args) == Success) && (args != NULL) )
      {
         // count number of separators in the string
         argc = 0;
         for (idx = 0; idx < nitems; idx++)
            if (args[idx] == 0)
               argc++;
         // check for missing zero at arg end: 0 is implied
         if ((nitems > 0) && (args[nitems - 1] != 0))
         {
            debug3("Xiccc-SplitArgv: missing terminating zero: wid 0x%X, property 0x%X, arg %.20s...", (int)wid, (int)property, args);
            argc += 1;
         }

         *ppArgv = xmalloc(argc * sizeof(char *) + nitems + 1);

         // copy sub-strings
         pStr = (char *) ((*ppArgv) + argc);
         memcpy(pStr, args, nitems);
         pStr[nitems] = 0;

         for (idx = 0; idx < argc; idx++)
         {
            (*ppArgv)[idx] = pStr;
            pStr += strlen(pStr) + 1;
         }
         *pArgc = argc;

         XFree(args);

         dprintf6("Xiccc-SplitArgv: wid 0x%X prop 0x%X: argc=%d argv: %s, %s, %s, ...\n", (int)wid, (int)property, argc, ((argc > 0)?(*ppArgv)[0]:""), ((argc > 1)?(*ppArgv)[1]:""), ((argc > 2)?(*ppArgv)[2]:""));
         result = TRUE;
      }
      else
         debug2("Xiccc-SplitArgv: failed to read property: wid 0x%X, property 0x%X", (int)wid, (int)property);
   }
   else
      fatal0("Xiccc-SplitArgv: illegal NULL ptr param");

   return result;
}

// ---------------------------------------------------------------------------
// Parse setstation message
//
bool Xiccc_ParseMsgSetstation( Display * dpy, XICCC_EV_QUEUE * pReq, Atom target )
{
   XICCC_MSG_SETSTATION * pMsg;
   char ** pArgv;
   uint argc;
   int  value, valEndOff;
   bool result = FALSE;

   if (Xiccc_SplitArgv(dpy, pReq->requestor, target, &pArgv, &argc))
   {
      if (argc >= SETSTATION_PARM_COUNT)
      {
         pMsg = &pReq->msg.setstation;

         strncpy(pMsg->stationName, pArgv[SETSTATION_PARM_CHNAME], sizeof(pMsg->stationName));
         pMsg->stationName[sizeof(pMsg->stationName) - 1] = 0;

         if ( (sscanf(pArgv[SETSTATION_PARM_FREQ], "%i%n", &value, &valEndOff) >= 1) &&
              (pArgv[SETSTATION_PARM_FREQ][valEndOff ] == 0) )
            pMsg->freq = value;
         else
            pMsg->freq = 0;

         strncpy(pMsg->channelName, pArgv[SETSTATION_PARM_CHN], sizeof(pMsg->channelName));
         pMsg->channelName[sizeof(pMsg->channelName) - 1] = 0;

         if ( (sscanf(pArgv[SETSTATION_PARM_VPS_PDC_CNI], "%i%n", &value, &valEndOff) >= 1) &&
              (pArgv[SETSTATION_PARM_VPS_PDC_CNI][valEndOff ] == 0) )
            pMsg->cni = value;
         else
            pMsg->cni = 0;

         // note: EPG PI format is currently unused

         if ( (sscanf(pArgv[SETSTATION_PARM_EPG_PI_CNT], "%i%n", &value, &valEndOff) >= 1) &&
              (pArgv[SETSTATION_PARM_EPG_PI_CNT][valEndOff ] == 0) )
            pMsg->epgPiCount = value;
         else
            pMsg->epgPiCount = 1;

         if ( (sscanf(pArgv[SETSTATION_PARM_EPG_UPDATE], "%i%n", &value, &valEndOff) >= 1) &&
              (pArgv[SETSTATION_PARM_EPG_UPDATE][valEndOff ] == 0) )
            pMsg->epgUpdate = value;
         else
            pMsg->epgUpdate = 0;

         result = TRUE;
      }
      xfree(pArgv);
   }
   return result;
}

// ---------------------------------------------------------------------------
// Reply with empty message to peer - used in error cases
//
bool Xiccc_SendNullReply( XICCC_STATE * pXi, XICCC_EV_QUEUE * pReq, Atom target )
{
   XEvent ev;
   /*Status st;*/

   if ((pXi != NULL) && (pReq != NULL))
   {
      dprintf2("Xiccc-SendNullReply: send empty reply to wid 0x%X target 0x%X\n", (int)pReq->requestor, (int)target);

      memset(&ev,0,sizeof(ev));
      ev.xselection.type = SelectionNotify;
      ev.xselection.display = pXi->dpy;
      ev.xselection.requestor = pReq->requestor;
      ev.xselection.selection = pXi->manager_atom;
      ev.xselection.target = target;
      ev.xselection.property = None;
      ev.xselection.time = pReq->timestamp;

      /*st =*/ XSendEvent(pXi->dpy, pReq->requestor, False, NoEventMask, &ev);
   }
   else
      fatal0("Xiccc-SendNullReply: illegal NULL ptr param");

   return TRUE;
}

// ---------------------------------------------------------------------------
// Reply to a message from a peer
// - assign the given result to property and send select notification event
// - note: the peer is not necessarily identical to the remote manager
//
bool Xiccc_SendReply( XICCC_STATE * pXi, const char * pStr, int strLen,
                      XICCC_EV_QUEUE * pReq, Atom target )
{
   Atom property;
   XEvent ev;
   /*Status st;*/

   if ((pXi != NULL) && (pStr != NULL) && (pReq != NULL))
   {
      dprintf4("Xiccc-SendReply: send reply to wid 0x%X target 0x%X property 0x%X: %.40s...\n", (int)pReq->requestor, (int)target, (int)pReq->property, pStr);

      // so called "obsolete clients" may not send result propert -> assign to target atom
      if (pReq->property != None)
         property = pReq->property;
      else
         property = target;

      if (strLen == -1)
         strLen = strlen(pStr);

      XChangeProperty(pXi->dpy, pReq->requestor, property, XA_STRING, 8*sizeof(char),
                      PropModeReplace, pStr, strLen);

      memset(&ev,0,sizeof(ev));
      ev.xselection.type = SelectionNotify;
      ev.xselection.display = pXi->dpy;
      ev.xselection.requestor = pReq->requestor;
      ev.xselection.selection = pXi->manager_atom;
      ev.xselection.target = target;
      ev.xselection.property = property;
      ev.xselection.time = pReq->timestamp;

      /*st =*/ XSendEvent(pXi->dpy, pReq->requestor, False, NoEventMask, &ev);
   }
   else
      fatal0("Xiccc-SendReply: illegal NULL ptr param");

   return TRUE;
}

// ----------------------------------------------------------------------------
// Send query message to peer
// - XXX FIXME should pass non-zero timestamp to be able to identify reply
//
bool Xiccc_SendQuery( XICCC_STATE * pXi, const char * pCmd, sint cmdLen,
                      Atom target, Atom property )
{
   XEvent ev;
   /*Status st;*/
   bool result = FALSE;

   dprintf3("Xiccc-SendQuery: wid 0x%X target 0x%X: property %s\n", (int)pXi->remote_manager_wid, (int)target, pCmd);

   if (pXi->remote_manager_wid != None)
   {
      if (cmdLen == -1)
         cmdLen = strlen(pCmd);

      XChangeProperty(pXi->dpy, pXi->manager_wid, target, XA_STRING, 8, PropModeReplace,
                      pCmd, cmdLen);

      memset(&ev,0,sizeof(ev));
      ev.xselectionrequest.type = SelectionRequest;
      ev.xselectionrequest.display = pXi->dpy;
      ev.xselectionrequest.owner = pXi->remote_manager_wid;
      ev.xselectionrequest.requestor = pXi->manager_wid;
      ev.xselectionrequest.selection = pXi->remote_manager_atom;
      ev.xselectionrequest.target = target;
      ev.xselectionrequest.property = property;
      ev.xselectionrequest.time = CurrentTime;

      /*st =*/ XSendEvent(pXi->dpy, pXi->remote_manager_wid, False, NoEventMask, &ev);

      result = TRUE;
   }
   else
      debug0("Xiccc-SendQuery: no peer known");

   return result;
}

// ---------------------------------------------------------------------------
// Start procedure to voluntarily release ownership
// - caller should install X error handler
//
bool Xiccc_SearchPeer( XICCC_STATE * pXi )
{
   bool  result = FALSE;

   do
   {
      // check if the selection manager already exists
      pXi->remote_manager_wid = XGetSelectionOwner(pXi->dpy, pXi->remote_manager_atom);
      if (pXi->remote_manager_wid != None)
      {
         dprintf2("Xicccm-SearchPeer: found remote manager wid 0x%X for target 0x%X\n", (int)pXi->remote_manager_wid, (int)pXi->remote_manager_atom);
         // select for destroy events
         XSelectInput(pXi->dpy, pXi->remote_manager_wid, StructureNotifyMask);
         pXi->events |= XICCC_NEW_PEER;
      }
      else
      {
         dprintf2("Xicccm-SearchPeer: no remote manager for target 0x%X, wait for message on root 0x%X\n", (int)pXi->remote_manager_atom, (int)pXi->root_wid);
         // to be notified when selection is created
         // commentd out: this is done once and for all during start-up
         //XSelectInput(pXi->dpy, pXi->root_wid, StructureNotifyMask);
      }
   }
   while (pXi->remote_manager_wid != XGetSelectionOwner(pXi->dpy, pXi->remote_manager_atom));

   return result;
}

// ---------------------------------------------------------------------------
// Start procedure to voluntarily release ownership
//
bool Xiccc_ReleaseManagement( XICCC_STATE * pXi )
{
   bool  result = FALSE;

   if (pXi->manager_state != XICCC_PASSIVE)
   {
      // dummy property change to generate an event
      XChangeProperty(pXi->dpy, pXi->manager_wid, pXi->manager_atom,
                      XA_STRING, 8*sizeof(char), PropModeAppend, NULL, 0);

      // next step is invoked after property change event
      pXi->manager_state = XICCC_WAIT_CLEAR;
      result = TRUE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Start procedure to become owner of the selection (i.e. manager)
//
bool Xiccc_ClaimManagement( XICCC_STATE * pXi, bool force )
{
   bool  result = FALSE;

   if (pXi->manager_state == XICCC_PASSIVE)
   {
      // dummy property change to generate an event
      XChangeProperty(pXi->dpy, pXi->manager_wid, pXi->manager_atom,
                      XA_STRING, 8*sizeof(char), PropModeAppend, NULL, 0);

      // next step is invoked after property change event
      pXi->manager_state = (force ? XICCC_WAIT_FORCED_CLAIM : XICCC_WAIT_CLAIM);
      result = TRUE;
   }
   else
      debug1("Xiccc-ClaimManagement: not in passive state: %d", (int)pXi->manager_state);

   return result;
}

// ---------------------------------------------------------------------------
// Free module resources
//
void Xiccc_Destroy( XICCC_STATE * pXi )
{
   Xiccc_QueueFree(&pXi->pNewStationQueue);
   Xiccc_QueueFree(&pXi->pSendStationQueue);
   Xiccc_QueueFree(&pXi->pPermStationQueue);
   Xiccc_QueueFree(&pXi->pRemoteCmdQueue);

   if (pXi->manager_wid != None)
   {
      XDestroyWindow(pXi->dpy, pXi->manager_wid);
   }
   else
      dprintf0("Xiccc-Destroy: manager wid already destroyed");
}

// ---------------------------------------------------------------------------
// Initialize the communication state and allocate static resources
// - called once during start-up
// - used both by all peers, i.e. both selection managers and clients
//
bool Xiccc_Initialize( XICCC_STATE * pXi, Display * dpy,
                       bool isEpg, const char * pIdArgv, uint idLen )
{
   bool  result = FALSE;

   if (dpy != NULL)
   {
      memset(pXi, 0, sizeof(*pXi));
      pXi->root_wid = RootWindowOfScreen(DefaultScreenOfDisplay(dpy));
      pXi->dpy = dpy;

      pXi->atoms._NXTVEPG_SELECTION_MANAGER = XInternAtom(dpy, "_NXTVEPG_SELECTION_MANAGER", False);
      pXi->atoms._NXTVEPG_TV_CLIENT_MANAGER = XInternAtom(dpy, "_NXTVEPG_TV_CLIENT_MANAGER", False);
      pXi->atoms._NXTVEPG_SETSTATION = XInternAtom(dpy, "_NXTVEPG_SETSTATION", False);
      pXi->atoms._NXTVEPG_SETSTATION_RESULT = XInternAtom(dpy, "_NXTVEPG_SETSTATION_RESULT", False);
      pXi->atoms._NXTVEPG_REMOTE = XInternAtom(dpy, "_NXTVEPG_REMOTE", False);
      pXi->atoms._NXTVEPG_REMOTE_RESULT = XInternAtom(dpy, "_NXTVEPG_REMOTE_RESULT", False);
      pXi->atoms.MANAGER = XInternAtom(dpy, "MANAGER", False);

      pXi->manager_wid = XCreateSimpleWindow(dpy, pXi->root_wid, -1, -1, 1, 1, 0, 0, 0);
      if (pXi->manager_wid != None)
      {
         dprintf1("Xicccm-Initialize: created selection manager window 0x%X\n", (int)pXi->manager_wid);

         // to receive property events on manager selection atom during start-up
         XSelectInput(pXi->dpy, pXi->manager_wid, PropertyChangeMask);

         // to receive client message when a new remote manager is created
         XSelectInput(pXi->dpy, pXi->root_wid, StructureNotifyMask);

         if (isEpg)
         {
            pXi->manager_atom = pXi->atoms._NXTVEPG_SELECTION_MANAGER;
            pXi->remote_manager_atom = pXi->atoms._NXTVEPG_TV_CLIENT_MANAGER;
         }
         else
         {
            pXi->manager_atom = pXi->atoms._NXTVEPG_TV_CLIENT_MANAGER;
            pXi->remote_manager_atom = pXi->atoms._NXTVEPG_SELECTION_MANAGER;
         }

         // protocol version and client info
         XChangeProperty(dpy, pXi->manager_wid, pXi->manager_atom,
                         XA_STRING, 8*sizeof(char), PropModeReplace, pIdArgv, idLen);

         pXi->manager_state = XICCC_PASSIVE;

         result = TRUE;
      }
   }
   else
      fatal0("Xicccm-Initialize: illegal NULL ptr param");

   return result;
}

#endif  // not WIN32
