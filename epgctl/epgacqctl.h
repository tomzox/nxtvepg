/*
 *  Acquisition main control
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgacqctl.h,v 1.49 2020/06/17 19:31:21 tom Exp tom $
 */

#ifndef __EPGACQCTL_H
#define __EPGACQCTL_H

#include "epgdb/ttxgrab.h"
#include "epgvbi/btdrv.h"

// ---------------------------------------------------------------------------
// describing the state of the acq
//

// user-configured acquisition mode
typedef enum
{
   ACQMODE_PASSIVE,         // do not touch /dev/video
   ACQMODE_NETWORK,         // connect to remote server
   ACQMODE_CYCLIC_2,        // cyclic: full only
   ACQMODE_CYCLIC_02,       // cyclic: now->all
   ACQMODE_COUNT
} EPGACQ_MODE;

// acquisition phases 0,1,2 that make up one full cycle
typedef enum
{
   ACQMODE_PHASE_NOWNEXT,
   ACQMODE_PHASE_FULL,
   ACQMODE_PHASE_MONITOR,
   ACQMODE_PHASE_COUNT
} EPGACQ_PHASE;

// reason for entering forced passive mode
typedef enum
{
   ACQPASSIVE_NONE,             // not in forced passive mode (unused)
   ACQPASSIVE_NO_TUNER,         // input source is not TV tuner
   ACQPASSIVE_ACCESS_DEVICE     // failed to tune channel (access /dev/video)
} EPGACQ_PASSIVE;

// ---------------------------------------------------------------------------
// Structure to describe the state of acq to the user

#define ACQ_COUNT_TO_PERCENT(C,T) (((T)>0) ? ((int)(((double)(C) * 100.0 + (T)/2) / (T))) : 100)

typedef enum
{
   ACQDESCR_NET_CONNECT,
   ACQDESCR_DISABLED,
   ACQDESCR_SCAN,
   ACQDESCR_STARTING,
   ACQDESCR_TTX_PG_SEQ_SCAN,
   ACQDESCR_NO_RECEPTION,
   ACQDESCR_IDLE,
   ACQDESCR_RUNNING,
} ACQDESCR_STATE;

typedef struct
{
   ACQDESCR_STATE ttxGrabState;
   EPGACQ_MODE    mode;
   EPGACQ_PHASE   cyclePhase;
   EPGACQ_PASSIVE passiveReason;
   uint16_t       ttxSrcCount;
   int16_t        ttxGrabIdx;    // TODO add ttxGrabCount
   uint16_t       ttxGrabDone;
   bool           isNetAcq;
   bool           isLocalServer;
} EPGACQ_DESCR;

// ---------------------------------------------------------------------------
// Structure to keep statistics about the current acq db
// - updated after every received AI block
// - can be retrieved by the GUI for the acq stats window

#define STATS_HIST_WIDTH     128
#define VARIANCE_HIST_COUNT    5

typedef struct
{
   uint8_t   expir;
   uint8_t   s1cur;
   uint8_t   s1old;
   uint8_t   s2cur;
   uint8_t   s2old;
} EPGDB_HIST;

typedef struct
{
   double    buf[VARIANCE_HIST_COUNT];   // ring buffer for variance
   uint16_t  count;                      // number of valid entries in the buffer
   uint16_t  lastIdx;                    // last written index
   uint32_t  resv_align;                 // for 64-bit alignment (Sun-Sparc)
} EPGDB_VAR_HIST;

typedef struct
{
   uint8_t   cniType;
   uint32_t  cni;
   uint32_t  pil;
} EPG_ACQ_VPS_PDC;

typedef struct
{
   time32_t            acqStartTime;

   EPGDB_HIST          hist[STATS_HIST_WIDTH];
   uint16_t            histIdx;
   uint8_t             resvd_0[6];      // for 64-bit alignment

   EPGDB_BLOCK_COUNT   count;
   EPGDB_VAR_HIST      varianceHist;
   uint32_t            nowMaxAcqRepCount;
   uint32_t            nowMaxAcqNetCount;
} EPG_NXTV_ACQ_STATS;

#define EPG_TTX_STATS_NAMLEN 32

// TODO (1) add srcCount
// TODO (2) add TTX_GRAB_STATS holding merge/average of all sources
typedef struct
{
   time32_t            acqStartTime;
   char                srcName[EPG_TTX_STATS_NAMLEN];
   int32_t             srcIdx;
   TTX_GRAB_STATS      pkgStats;
} EPG_TTX_GRAB_STATS;

// TODO (3) add TTX_DEC_STATS holding merge/average of all sources
typedef struct
{
   time32_t            lastStatsUpdate;
   TTX_DEC_STATS       ttx_dec;           // specific to srcIdx
   EPG_TTX_GRAB_STATS  ttx_grab;
   uint32_t            ttx_duration;
   EPG_NXTV_ACQ_STATS  nxtv;  // TODO port to teletext
} EPG_ACQ_STATS;

typedef enum
{
   VPSPDC_REQ_POLL,
   VPSPDC_REQ_TVAPP,
   VPSPDC_REQ_STATSWIN,
   VPSPDC_REQ_DAEMON,
   VPSPDC_REQ_COUNT
} VPSPDC_REQ_ID;

// ---------------------------------------------------------------------------
// Interface to main control module and user interface
//
void EpgAcqCtl_Init( void );
void EpgAcqCtl_Destroy( bool isEmergency );
bool EpgAcqCtl_Start( void );
void EpgAcqCtl_Stop( void );
const char * EpgAcqCtl_GetLastError( void );
bool EpgAcqCtl_SelectMode( EPGACQ_MODE newAcqMode, EPGACQ_PHASE maxPhase,
                           uint ttxSrcCount, const char * pTtxNames,
                           const EPGACQ_TUNER_PAR * pTtxFreqs );
bool EpgAcqCtl_SetInputSource( uint inputIdx, uint slicerType );
bool EpgAcqCtl_CheckDeviceAccess( void );
void EpgAcqCtl_DescribeAcqState( EPGACQ_DESCR * pAcqState );
void EpgAcqCtl_GetAcqModeStr( const EPGACQ_DESCR * pAcqState,
                              const char ** ppModeStr, const char ** ppPasvStr );
void EpgAcqCtl_Suspend( bool suspend );
bool EpgAcqCtl_IsActive( void );

// interface to sub-modules
bool EpgAcqCtl_TuneProvider( const EPGACQ_TUNER_PAR * par, EPGACQ_PASSIVE * pMode );

// timer events used during acquisition
bool EpgAcqCtl_ProcessPackets( void );
void EpgAcqCtl_ProcessBlocks( void );
bool EpgAcqCtl_ProcessVps( void );

// interface for status, statistics and timescales display
bool EpgAcqCtl_GetAcqStats( EPG_ACQ_STATS * pAcqStats );
void EpgAcqCtl_EnableAcqStats( bool enable );
bool EpgAcqCtl_GetVpsPdc( EPG_ACQ_VPS_PDC * pVpsPdc, VPSPDC_REQ_ID clientId, bool force );
void EpgAcqCtl_ResetVpsPdc( void );

#endif  // __EPGACQCTL_H
