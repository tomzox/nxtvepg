/*
 *  Windows TV app communication - shared memory layout
 *
 *  Copyright (C) 1999-2008 T. Zoerner
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
 */

#ifndef __WINSHM_H
#define __WINSHM_H


// ---------------------------------------------------------------------------
// Structure for communication with TV application
// - the prefix "tv" notes that this variable is written by the TV app.
//   the prefix "epg" notes that this variable is written by the EPG app.
//
#define TVAPP_NAME_MAX_LEN     64   // max. TV app name string length (including terminating zero)
#define TVAPP_PATH_MAX_LEN    511   // max. TV app path string length (including 0)
#define TV_CHAN_NAME_MAX_LEN  128   // max. channel name string length (including 0)
#define EPG_QUERY_MAX_LEN    (2*1024) // max. query command length
#define EPG_DATA_BUF_SIZE   (64*1024) // size of EPG data buffer
#define EPG_CMD_MAX_LEN      (2*1024) // max. command length

typedef struct
{
   uint32_t  epgShmVersion;   // version number of SHM protocol
   uint32_t  epgShmSize;      // size of mapped shared memory
   uint32_t  tvFeatures;      // bit field with features supported by TV app
   uint8_t   tvAppName[TVAPP_NAME_MAX_LEN];  // TV application name and version string
   uint8_t   tvAppPath[TVAPP_PATH_MAX_LEN];  // TV application directory (INI & channel table)
   uint8_t   tvAppType;       // TV application type (see enum below; req. for INI parsing)

   uint8_t   tvAppAlive;      // TRUE while TV application is alive & attached to SHM
   uint8_t   epgAppAlive;     // non-zero while EPG GUI application is alive

   uint8_t   epgHasDriver;    // EPG app is attached to driver
   uint8_t   epgTvCardIdx;    // index of card currently in use by EPG app; 0xff if none
   uint8_t   tvReqTvCard;     // TV app requires the card with index given by the next element
   uint8_t   tvCardIdx;       // index of card required by TV app requests; 0xff if all
   // TODO: device
   // TODO: text encoding: Latin-1 or UTF-8

   uint8_t   tvGrantTuner;    // TV app sets tuner to EPG requested frequency
   uint8_t   reserved1[3];    // unused; set to 0
   uint32_t  epgReqFreq;      // TV tuner frequency requested by acq control; 0 if none
   uint32_t  epgReqNorm;      // TV tuner norm requested by acq control; only valid if TV freq is valid
   uint32_t  epgReqInput;     // TV input source requested by EPG user (0=tuner; 1,2,3=ext.inp., 4=none)

   uint8_t   tvCurIsTuner;    // TRUE if input source is a TV tuner
   uint8_t   reserved4[3];    // unused, set to 0
   uint32_t  tvCurNorm;       // 0=PAL, 1=SECAM, 2=NTSC
   uint32_t  tvCurFreq;       // tuner freq. from which VBI is currently captured

   uint8_t   tvChanName[TV_CHAN_NAME_MAX_LEN];  // name of currently tuned TV channel
   uint32_t  tvChanCni;       // network ID of current channel as found in VPS/PDC; set to 0 if unknown
   uint32_t  tvChanEpgPiCnt;  // number of programmes to deliver for current channel after station change
   uint32_t  tvStationIdx;    // value to be incremented for station changes

   uint8_t   tvEpgQuery[EPG_QUERY_MAX_LEN];  // database query sent from TV to EPG app
   uint32_t  tvEpgQueryLen;   // number of bytes in query buffer (including (last) terminating zero)
   uint32_t  tvEpgQueryIdx;   // value to be incremented for each new query

   uint32_t  epgStationIdx;   // running index of station EPG update, or zero
   uint8_t   epgDataRespType; // request to which this data is the response, see resp. types below
   uint8_t   reserved2[3];    // unused, set to 0
   uint32_t  epgDataIdx;      // index of query to which the data belongs, or zero
   uint32_t  epgDataLen;      // number of bytes in buffer
   uint32_t  epgDataOff;      // offset in case the data is transmitted in several parts
   uint8_t   epgData[EPG_DATA_BUF_SIZE];  // buffer for sending EPG data to TV app

   uint8_t   epgCommand[EPG_CMD_MAX_LEN];  // pass control cmd from EPG to TV app (e.g. channel switch)
   uint32_t  epgCmdArgc;      // number of arguments in 0-separated command string list
   uint32_t  epgCommandIdx;   // value to be incremented after command changes
   uint32_t  tvCommandIdx;    // last EPG cmd processed by TV app (EPG app must wait for this before sending next cmd)
   uint32_t  epgCmdArgLen;    // overall length of the argument string, including all 0-bytes

   uint8_t   reserved3[1024]; // unused, set to 0

   EPGACQ_BUF vbiBuf;

} TVAPP_COMM;

// names of resources for inter-process communication
#define EPG_SHM_NAME          "nxtvepg_shm"
#define SHM_MUTEX_NAME        "nxtvepg_shm_mutex"
#define EPG_ACQ_MUTEX_NAME     "nxtvepg_epg_acq_mutex"
#define EPG_GUI_MUTEX_NAME     "nxtvepg_epg_gui_mutex"
#define EPG_ACQ_SHM_EVENT_NAME "nxtvepg_epg_acq_event"
#define EPG_GUI_SHM_EVENT_NAME "nxtvepg_epg_gui_event"
#define TV_MUTEX_NAME         "nxtvepg_tv_mutex"
#define TV_SHM_EVENT_NAME     "nxtvepg_tv_event"

#define TVAPP_CARD_REQ_ALL    0xFF    // TV app requests all cards, i.e. force EPG to unload driver
#define EPG_REQ_INPUT_NONE    0xFF    // EPG has no need to set an input source (e.g. acq. disabled)
#define EPG_REQ_FREQ_NONE     0       // EPG has no need for tuner (e.g. non-tuner input source)

#define EPG_APP_GUI           (1<<0)  // mask bit: indicates EPG GUI application is alive
#define EPG_APP_DAEMON        (1<<1)  // mask bit: indicates EPG daemon is alive

#define EPG_DATA_RESP_CHN     0x00    // response to channel change
#define EPG_DATA_RESP_QUERY   0x01    // response to general query

#define TVAPP_FEAT_TTX_FWD    0x0001  // forwards teletext packets
#define TVAPP_FEAT_VPS_FWD    0x0002  // forwards VPS CNI & PIL
#define TVAPP_FEAT_TUNER      0x0004  // grants tuner to EPG app when paused
#define TVAPP_FEAT_REQ_CNAME  0x0008  // requests name of current channel
#define TVAPP_FEAT_CMD_TUNE   0x0010  // allows channel change by EPG App
#define TVAPP_FEAT_CMD_MUTE   0x0020  // allows audio mute by EPG app
#define TVAPP_FEAT_VCR        0x0040  // supports "record" command
#define TVAPP_FEAT_ALL_000701 (TVAPP_FEAT_TTX_FWD|TVAPP_FEAT_VPS_FWD|TVAPP_FEAT_TUNER|TVAPP_FEAT_REQ_CNAME|TVAPP_FEAT_CMD_TUNE|TVAPP_FEAT_CMD_MUTE)
#define TVAPP_FEAT_REMIND     0x0080  // supports "remind" command (OSD display of reminder msg)

#define EPG_SHM_VERSION       0xFF030004  // protocol version id
#define EPG_SHM_VERSION_MAJOR(V)   (((V) >> 16) & 0xFF)
#define EPG_SHM_VERSION_MINOR(V)   (((V) >>  8) & 0xFF)
#define EPG_SHM_VERSION_PATLEV(V)  (((V)      ) & 0xFF)

// List of TV apps for which INI file and channel table loading is supported
// - not all of these neccessarily do support EPG interaction too
// - when a TV app's format of INI or channel table changes, a new identifier must
//   be assigned and nxtvepg must be extended to parse the new format; until a new
//   ID has been assigned, TVAPP_NONE must be used.
//
#ifdef WIN32  // for UNIX this enum is defined in rcfile.h
typedef enum
{
   TVAPP_NONE,
   TVAPP_DSCALER,
   TVAPP_KTV,
   TVAPP_MULTIDEC,
   TVAPP_MORETV,
   TVAPP_FREETV,
   TVAPP_COUNT
} TVAPP_NAME;
#endif

#endif  // __WINSHM_H
