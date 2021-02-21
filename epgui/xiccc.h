/*
 *  Interaction with X11 TV-apps via Inter-Client Communication Conventions
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
 *  Description: see C source file.
 */

#ifndef _XICCC_H
#define _XICCC_H

#ifndef WIN32

// ----------------------------------------------------------------------------
// Protocol constants and types
//
#define ICCCM_PROTO_VSTR      "NETAIP/1.0"

typedef enum
{
   MANAGER_PARM_PROTO,
   MANAGER_PARM_APPNAME,
   MANAGER_PARM_APPVERS,
   MANAGER_PARM_FEATURES,
   //MANAGER_PARM_DEVICE,
   //MANAGER_PARM_HOST,
   MANAGER_PARM_COUNT
} MANAGER_PARMS;

typedef enum
{
   SETSTATION_PARM_PROTO,
   SETSTATION_PARM_CHNAME,
   SETSTATION_PARM_FREQ,
   SETSTATION_PARM_CHN,
   SETSTATION_PARM_VPS_PDC_CNI,
   SETSTATION_PARM_EPG_FORMAT,
   SETSTATION_PARM_EPG_PI_CNT,
   SETSTATION_PARM_EPG_UPDATE,
   SETSTATION_PARM_COUNT
} SETSTATION_PARMS;

typedef struct
{
   bool         isNew;
   char         stationName[50];
   uint         freq;
   char         channelName[15];
   uint         cni;
   uint         epgPiFormat;
   uint         epgPiCount;
   uint         epgUpdate;
} XICCC_MSG_SETSTATION;

typedef union
{
   XICCC_MSG_SETSTATION setstation;
} XICCC_MSGS;

// ----------------------------------------------------------------------------
// Type definitions
//
typedef enum
{
   XICCC_PASSIVE,
   XICCC_WAIT_CLAIM,
   XICCC_WAIT_FORCED_CLAIM,
   XICCC_WAIT_CLEAR,
   XICCC_OTHER_OWNER,
   XICCC_READY
} XICCC_MGMT;

typedef enum
{
   XICCC_NEW_PEER          = 1<<0,
   XICCC_LOST_PEER         = 1<<1,
   XICCC_GOT_MGMT          = 1<<2,
   XICCC_LOST_MGMT         = 1<<3,
   XICCC_SETSTATION_REQ    = 1<<4,
   XICCC_SETSTATION_REPLY  = 1<<5,
   XICCC_REMOTE_REQ        = 1<<6,
   XICCC_REMOTE_REPLY      = 1<<7,

   // internal events - not to be processed by application event handler
   XICCC_FOCUS_LOST        = 1<<16,
   XICCC_FOCUS_CLAIM       = 1<<17,
   XICCC_FOCUS_CLEAR       = 1<<18,
   XICCC_NONE              = 0

} XICCC_EVENTS;

#define IS_XICCC_INTERNAL_EVENT(X)  ((X) & (0xffff << 16))

// Message queue
typedef struct XICCC_EV_QUEUE_struct
{
   struct XICCC_EV_QUEUE_struct * pNext;
   Window       requestor;
   Atom         property;
   Time         timestamp;
   XICCC_MSGS   msg;
} XICCC_EV_QUEUE;

typedef struct
{
   // this atom identifies the selection owner
   Atom _NXTVEPG_SELECTION_MANAGER;
   // this atom identifies the TV app. selection owner (target for channel changes)
   Atom _NXTVEPG_TV_CLIENT_MANAGER;
   // these atoms are request targets and result properties
   Atom _NXTVEPG_SETSTATION;
   Atom _NXTVEPG_SETSTATION_RESULT;
   Atom _NXTVEPG_REMOTE;
   Atom _NXTVEPG_REMOTE_RESULT;
   // used in announcement client message
   Atom MANAGER;
} XICCC_ATOMS;

typedef struct
{
   XICCC_ATOMS  atoms;
   XICCC_MGMT   manager_state;
   Window       manager_wid;
   Atom         manager_atom;
   Window       remote_manager_wid;
   Atom         remote_manager_atom;
   Window       other_manager_wid;
   Window       root_wid;
   Display    * dpy;
   Time         mgmt_start_time;
   Time         last_time;

   XICCC_EVENTS events;
   XICCC_EV_QUEUE * pNewStationQueue;
   XICCC_EV_QUEUE * pSendStationQueue;
   XICCC_EV_QUEUE * pPermStationQueue;
   XICCC_EV_QUEUE * pRemoteCmdQueue;
} XICCC_STATE;

#define XICCC_IS_MANAGER(X)  ( ((X)->manager_state == XICCC_READY) && \
                               ((X)->remote_manager_wid != None) )
#define XICCC_HAS_PEER(X)      ((X)->remote_manager_wid != None)

// ----------------------------------------------------------------------------
// Interface
//
bool Xiccc_Initialize( XICCC_STATE * pXi, Display * dpy,
                       bool isEpg, const char * pIdArgv, uint idLen );
void Xiccc_Destroy( XICCC_STATE * pXi );
bool Xiccc_ClaimManagement( XICCC_STATE * pXi, bool force );
bool Xiccc_ReleaseManagement( XICCC_STATE * pXi );
bool Xiccc_SearchPeer( XICCC_STATE * pXi );
bool Xiccc_XEvent( XEvent *eventPtr, XICCC_STATE * pXi, bool * pNeedHandler );
void Xiccc_HandleInternalEvent( XICCC_STATE * pXi );
bool Xiccc_SendNullReply( XICCC_STATE * pXi, XICCC_EV_QUEUE * pReq, Atom target );
bool Xiccc_SendReply( XICCC_STATE * pXi, const char * pStr, int strLen,
                      XICCC_EV_QUEUE * pReq, Atom target );
bool Xiccc_SendQuery( XICCC_STATE * pXi, const char * pCmd, sint cmdLen,
                      Atom target, Atom property );
bool Xiccc_SplitArgv( Display * dpy, Window wid, Atom property, char *** ppArgv, uint * pArgc );
void Xiccc_BuildArgv( char ** ppBuild, uint * pArgLen, ... );
bool Xiccc_ParseMsgSetstation( Display * dpy, XICCC_EV_QUEUE * pReq, Atom target );

void Xiccc_QueueUnlinkEvent( XICCC_EV_QUEUE ** pHead, XICCC_EV_QUEUE * pReq );
void Xiccc_QueueRemoveRequest( XICCC_EV_QUEUE ** pHead, Window requestor );
void Xiccc_QueueAddEvent( XICCC_EV_QUEUE ** pHead, XICCC_EV_QUEUE * pNew );
void Xiccc_QueueFree( XICCC_EV_QUEUE ** pHead );

#endif  // WIN32
#endif  // _XICCC_H
