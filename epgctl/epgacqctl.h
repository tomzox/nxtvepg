/*
 *  Nextview acquisition control
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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgacqctl.h,v 1.43 2004/03/11 22:22:20 tom Exp tom $
 */

#ifndef __EPGACQCTL_H
#define __EPGACQCTL_H

#include "epgdb/epgstream.h"

extern EPGDB_CONTEXT * pAcqDbContext;

// ---------------------------------------------------------------------------
// describing the state of the acq
//

// internal state of the control module
typedef enum
{
   ACQSTATE_OFF,            // acq is disabled
   ACQSTATE_WAIT_BI,        // wait for BI block
   ACQSTATE_WAIT_AI,        // wait for BI block
   ACQSTATE_RUNNING         // acq is up & running
} EPGACQ_STATE;

// user-configured acquisition mode
typedef enum
{
   ACQMODE_FORCED_PASSIVE,  // forced passive
   ACQMODE_PASSIVE,         // do not touch /dev/video
   ACQMODE_NETWORK,         // connect to remote server
   ACQMODE_EXTERNAL,        // set input source, then passive
   ACQMODE_FOLLOW_UI,       // change acq db to follow browser
   ACQMODE_FOLLOW_MERGED,   // substate of follow-ui for merged db, equiv to cyclic-2
   ACQMODE_CYCLIC_2,        // cyclic: full only
   ACQMODE_CYCLIC_012,      // cyclic: now->near->all
   ACQMODE_CYCLIC_02,       // cyclic: now->all
   ACQMODE_CYCLIC_12,       // cyclic: near->all
   ACQMODE_COUNT
} EPGACQ_MODE;

#define ACQMODE_IS_PASSIVE(X) (((X) == ACQMODE_PASSIVE) || ((X) == ACQMODE_NETWORK))
#define ACQMODE_IS_CYCLIC(X)  ((X) >= ACQMODE_FOLLOW_MERGED)
#define ACQMODE_IS_FOLLOW(X)  (((X) == ACQMODE_FOLLOW_UI) || ((X) == ACQMODE_FOLLOW_MERGED) || ((X) == ACQMODE_NETWORK))

// acquisition phases 0,1,2 that make up one full cycle
typedef enum
{
   ACQMODE_PHASE_NOWNEXT,
   ACQMODE_PHASE_STREAM1,
   ACQMODE_PHASE_STREAM2,
   ACQMODE_PHASE_MONITOR,
   ACQMODE_PHASE_COUNT
} EPGACQ_PHASE;

// reason for entering forced passive mode
typedef enum
{
   ACQPASSIVE_NONE,             // not in forced passive mode (unused)
   ACQPASSIVE_NO_TUNER,         // input source is not TV tuner
   ACQPASSIVE_NO_FREQ,          // selected database has no tuner frequency
   ACQPASSIVE_NO_DB,            // no db yet or database file is missing
   ACQPASSIVE_ACCESS_DEVICE     // failed to tune channel (access /dev/video)
} EPGACQ_PASSIVE;


#define EPGACQCTL_SLICER_INTV    20   // check slicer after X secs
#define EPGACQCTL_DUMP_INTV      60   // interval between db dumps
#define EPGACQCTL_MODIF_INTV  (2*60)  // max. interval without reception

#define EPGACQCTL_STREAM1_UPD_INTV    (1*60*60)
#define EPGACQCTL_STREAM2_UPD_INTV   (14*60*60)

#define MIN_CYCLE_QUOTE     0.33
#define MIN_CYCLE_VARIANCE  0.25
#define MAX_CYCLE_VAR_DIFF  0.01

#define MAX_CYCLE_ACQ_REP_COUNT   1.1

#define NOWNEXT_TIMEOUT_AI_COUNT  5
#define NOWNEXT_TIMEOUT          (5*60)
#define STREAM1_TIMEOUT         (30*60)
#define STREAM2_TIMEOUT         (30*60)

// ---------------------------------------------------------------------------
// Structure to describe the state of acq to the user

// number of seconds without AI after which acq is considered "stalled"
#define ACQ_DESCR_STALLED_TIMEOUT  30

#define ACQ_COUNT_TO_PERCENT(C,T) (((T)>0) ? ((int)((double)(C) * 100.0 / (T))) : 100)

typedef enum
{
   ACQDESCR_NET_CONNECT,
   ACQDESCR_DISABLED,
   ACQDESCR_SCAN,
   ACQDESCR_STARTING,
   ACQDESCR_NO_RECEPTION,
   ACQDESCR_DEC_ERRORS,
   ACQDESCR_STALLED,
   ACQDESCR_RUNNING,
} ACQDESCR_STATE;

typedef struct
{
   ACQDESCR_STATE state;
   EPGACQ_MODE    mode;
   EPGACQ_PHASE   cyclePhase;
   EPGACQ_PASSIVE passiveReason;
   uint16_t       cniCount;
   uint32_t       dbCni;
   uint32_t       cycleCni;
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
   uint32_t  ttxPkgCount;
   uint32_t  epgPkgCount;
   uint32_t  epgPagCount;
} EPGDB_ACQ_TTX_STATS;

typedef struct
{
   time_t    lastAiTime;
   time_t    minAiDistance;
   time_t    maxAiDistance;
   uint32_t  sumAiDistance;
   uint32_t  aiCount;
} EPGDB_ACQ_AI_STATS;

typedef struct
{
   uint32_t  cni;
   uint32_t  pil;
} EPGDB_ACQ_VPS_PDC;

typedef struct
{
   time_t              acqStartTime;
   time_t              lastStatsUpdate;

   EPGDB_ACQ_AI_STATS  ai;
   EPGDB_ACQ_TTX_STATS ttx;
   EPG_STREAM_STATS    stream;

   EPGDB_HIST          hist[STATS_HIST_WIDTH];
   uint16_t            histIdx;
   uint8_t             resvd_0[6];       // for 64-bit alignment (Sun-Sparc)

   EPGDB_BLOCK_COUNT   count[2];
   EPGDB_VAR_HIST      varianceHist[2];
   uint32_t            nowNextMaxAcqRepCount;

   EPGDB_ACQ_VPS_PDC   vpsPdc;
   uint8_t             resvd_1[4];

} EPGDB_STATS;

typedef enum
{
   VPSPDC_REQ_TVAPP,
   VPSPDC_REQ_STATSWIN,
   VPSPDC_REQ_DAEMON,
   VPSPDC_REQ_COUNT
} VPSPDC_REQ_ID;

// ---------------------------------------------------------------------------
// Interface to main control module and user interface
//
void EpgAcqCtl_InitDaemon( void );
bool EpgAcqCtl_Start( void );
void EpgAcqCtl_Stop( void );
const char * EpgAcqCtl_GetLastError( void );
bool EpgAcqCtl_SelectMode( EPGACQ_MODE newAcqMode, EPGACQ_PHASE maxPhase,
                           uint cniCount, const uint * pCniTab );
bool EpgAcqCtl_SetInputSource( uint inputIdx, uint slicerType );
bool EpgAcqCtl_CheckDeviceAccess( void );
void EpgAcqCtl_DescribeAcqState( EPGACQ_DESCR * pAcqState );
void EpgAcqCtl_Suspend( bool suspend );

// interface to acq server and client
void EpgAcqCtl_AddNetVpsPdc( const EPGDB_ACQ_VPS_PDC * pVpsPdcUpd );
void EpgAcqCtl_UpdateNetProvList( uint cniCount, const uint * pCniList );

// has to be invoked about once a second by a timer when acq is running
void EpgAcqCtl_Idle( void );
bool EpgAcqCtl_ProcessPackets( void );
void EpgAcqCtl_ProcessBlocks( void );
bool EpgAcqCtl_ProcessVps( void );

// interface to statistics windows
const EPGDB_BLOCK_COUNT * EpgAcqCtl_GetDbStats( void );
const EPGDB_ACQ_VPS_PDC * EpgAcqCtl_GetVpsPdc( VPSPDC_REQ_ID clientId );
const EPGDB_STATS * EpgAcqCtl_GetAcqStats( void );
void EpgAcqCtl_EnableAcqStats( bool enable );
void EpgAcqCtl_ResetVpsPdc( void );
void EpgAcqCtl_EnableTimescales( bool enable, bool allProviders );
#ifdef __EPGTSCQUEUE_H
EPGDB_PI_TSC * EpgAcqCtl_GetTimescaleQueue( void );
#endif


#endif  // __EPGACQCTL_H
