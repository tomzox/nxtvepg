/*
 *  Windows TV app communication - shared memory layout
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
 *    This header file defines the layout of the shared memory which can
 *    be used for communication between a TV application and nxtvepg.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: winshm.h,v 1.2 2002/05/04 18:21:39 tom Exp tom $
 */

#ifndef __WINSHM_H
#define __WINSHM_H

// ---------------------------------------------------------------------------
// Structure for communication with TV application
// - the prefix "tv" notes that this variable is written by the TV app.
//   the prefix "epg" notes that this variable is written by the EPG app.
//
#define TVAPP_NAME_MAX_LEN  32   // max. TV app name string length (including terminating zero)
#define CHAN_NAME_MAX_LEN   64   // max. channel name string length (including 0)
#define EPG_TITLE_MAX_LEN   44   // max. program title length (including 0)
#define EPG_CMD_MAX_LEN    256   // max. command length

typedef struct
{
   uint32_t  epgShmVersion;   // version number of SHM protocol
   uint32_t  tvFeatures;      // bit field with features supported by TV app
   uint8_t   tvAppName[TVAPP_NAME_MAX_LEN];  // TV application name and version string

   uint8_t   tvAppAlive;      // TRUE while TV application is alive & attached to SHM
   uint8_t   epgAppAlive;     // TRUE while EPG application is alive

   uint8_t   epgHasDriver;    // EPG app is attached to driver
   uint8_t   epgTvCardIdx;    // index of card currently in use by EPG app; 0xff if none
   uint8_t   tvReqTvCard;     // TV app requires the card with index given by the next element
   uint8_t   tvCardIdx;       // index of card required by TV app requests; 0xff if none; 0xfe if all

   uint8_t   tvGrantTuner;    // TV app sets tuner to EPG requested frequency
   uint8_t   reserved1;       // unused; set to 0
   uint32_t  epgReqFreq;      // TV tuner frequency requested by acq control; 0 if none
   uint32_t  epgReqInput;     // TV input source requested by EPG user (0=tuner; 1,2,3=ext.inp., 4=none)
   uint32_t  tvCurFreq;       // tuner freq. from which VBI is currently captured
   uint32_t  tvCurInput;      // input source from which video is received (0=tuner, ...)

   uint8_t   tvChanName[CHAN_NAME_MAX_LEN];  // name of currently tuned TV channel
   uint32_t  tvChanCni;       // network ID of current channel as found in VPS/PDC; set to 0 if unknown
   uint32_t  tvChanNameIdx;   // value to be incremented after name changes
   uint8_t   epgProgTitle[EPG_TITLE_MAX_LEN];  // name of currently tuned TV channel
   uint32_t  epgStartTime;    // start time of current program (seconds since 1.1.1970 0:00am UTC)
   uint32_t  epgStopTime;     // stop time of current program
   uint8_t   epgPdcThemeCount;// number of valid elements in the following array
   uint8_t   epgPdcThemes[7]; // PDC theme descriptors as defined in in ETS 300 231
   uint32_t  epgProgInfoIdx;  // value to be incremented after EPG title & times update

   uint8_t   epgCommand[EPG_CMD_MAX_LEN];  // pass ctonrol cmd from EPG to TV app (e.g. channel switch)
   uint32_t  epgCmdArgc;      // number of arguments in 0-separated command string list
   uint32_t  epgCommandIdx;   // value to be incremented after command changes
   uint32_t  tvCommandIdx;    // last EPG cmd processed by TV app (EPG app must wait for this before sending next cmd)
   uint8_t   reserved2[256];  // unused, set to 0

   EPGACQ_BUF vbiBuf;

} TVAPP_COMM;

// names of resources for inter-process communication
#define EPG_SHM_NAME          "nxtvepg_shm"
#define SHM_MUTEX_NAME        "nxtvepg_shm_mutex"
#define EPG_MUTEX_NAME        "nxtvepg_epg_mutex"
#define EPG_SHM_EVENT_NAME    "nxtvepg_epg_event"
#define TV_MUTEX_NAME         "nxtvepg_tv_mutex"
#define TV_SHM_EVENT_NAME     "nxtvepg_tv_event"

#define TVAPP_CARD_REQ_ALL    0xFF    // TV app requests all cards, i.e. force EPG to unload driver
#define EPG_REQ_INPUT_NONE    0xFF    // EPG has no need to set an input source (e.g. acq. disabled)
#define EPG_REQ_FREQ_NONE     0       // EPG has no need for tuner (e.g. non-tuner input source)

#define TVAPP_FEAT_TTX_FWD    0x0001  // forwards teletext packets
#define TVAPP_FEAT_VPS_FWD    0x0002  // forwards VPS CNI & PIL
#define TVAPP_FEAT_TUNER      0x0004  // grants tuner to EPG app when paused
#define TVAPP_FEAT_REQ_CNAME  0x0008  // requests name of current channel
#define TVAPP_FEAT_CMD_TUNE   0x0010  // allows channel change by EPG App
#define TVAPP_FEAT_CMD_MUTE   0x0020  // allows audio mute by EPG app
#define TVAPP_FEAT_ALL_000701 (TVAPP_FEAT_TTX_FWD|TVAPP_FEAT_VPS_FWD|TVAPP_FEAT_TUNER|TVAPP_FEAT_REQ_CNAME|TVAPP_FEAT_CMD_TUNE|TVAPP_FEAT_CMD_MUTE)

#define EPG_SHM_VERSION   0xFF020000  // protocol version id

#endif  // __WINSHM_H
