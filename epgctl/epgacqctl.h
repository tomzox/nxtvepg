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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgacqctl.h,v 1.17 2000/12/26 15:57:11 tom Exp tom $
 */

#ifndef __EPGACQCTL_H
#define __EPGACQCTL_H


// ---------------------------------------------------------------------------
// describing the state of the acq and db
//
typedef enum
{
   EPGDB_NOT_INIT,
   EPGDB_PROV_SCAN,
   EPGDB_PROV_WAIT,
   EPGDB_PROV_SEL,
   EPGDB_ACQ_NO_FREQ,
   EPGDB_ACQ_NO_TUNER,
   EPGDB_ACQ_ACCESS_DEVICE,
   EPGDB_ACQ_PASSIVE,
   EPGDB_ACQ_WAIT,
   EPGDB_ACQ_OTHER_PROV,
   EPGDB_EMPTY,
   EPGDB_PREFILTERED_EMPTY,
   EPGDB_OK
} EPGDB_STATE;

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
   ACQMODE_FOLLOW_UI,       // change acq db to follow browser
   ACQMODE_FOLLOW_MERGED,   // substate of follow-ui for merged db, equiv to cyclic-2
   ACQMODE_CYCLIC_2,        // cyclic: full only
   ACQMODE_CYCLIC_012,      // cyclic: now->near->all
   ACQMODE_CYCLIC_02,       // cyclic: now->all
   ACQMODE_CYCLIC_12,       // cyclic: near->all
   ACQMODE_COUNT
} EPGACQ_MODE;

#define ACQMODE_IS_CYCLIC(X) ((X) >= ACQMODE_FOLLOW_MERGED)

// acquisition phases 0,1,2 that make up one full cycle
typedef enum
{
   ACQMODE_PHASE_NOWNEXT,
   ACQMODE_PHASE_STREAM1,
   ACQMODE_PHASE_STREAM2,
   ACQMODE_PHASE_COUNT
} EPGACQ_PHASE;

// reason for entering forced passive mode
typedef enum
{
   ACQPASSIVE_NONE,             // not in forced passive mode (unused)
   ACQPASSIVE_NO_TUNER,         // input source is not TV tuner
   ACQPASSIVE_NO_FREQ,          // selected database has no tuner frequency
   ACQPASSIVE_ACCESS_DEVICE     // failed to tune channel (access /dev/video)
} EPGACQ_PASSIVE;

// result values for epg scan start function
typedef enum
{
   EPGSCAN_OK,                  // no error
   EPGSCAN_ACCESS_DEV_VIDEO,    // failed to set input source or tuner freq
   EPGSCAN_ACCESS_DEV_VBI,      // failed to start acq
   EPGSCAN_NO_TUNER,            // input source is not a TV tuner
   EPGSCAN_START_RESULT_COUNT
} EPGSCAN_START_RESULT;

// internal state during epg scan
typedef enum
{
   SCAN_STATE_OFF,
   SCAN_STATE_RESET,
   SCAN_STATE_WAIT,
   SCAN_STATE_WAIT_NI,
   SCAN_STATE_WAIT_DATA,
   SCAN_STATE_WAIT_EPG,
   SCAN_STATE_DONE
} EPGSCAN_STATE;


enum
{
   DB_TARGET_UI   = 0,
   DB_TARGET_ACQ  = 1
};

#define EPGACQCTL_DUMP_INTV      60   // interval between db dumps
#define EPGACQCTL_MODIF_INTV  (2*60)  // max. interval without reception

#define EPGACQCTL_NOWNEXT_UPD_INTV      (45*60)
#define EPGACQCTL_STREAM1_UPD_INTV    (1*60*60)
#define EPGACQCTL_STREAM2_UPD_INTV   (14*60*60)

#define MIN_CYCLE_QUOTE     0.33
#define MIN_CYCLE_VARIANCE  0.25
#define MAX_CYCLE_VAR_DIFF  0.01

// ---------------------------------------------------------------------------
// Structure to keep statistics about the current acq db
// - updated after every received AI block
// - can be retrieved by the GUI for the acq stats window

#define STATS_HIST_WIDTH     128
#define VARIANCE_HIST_COUNT    5

typedef struct
{
   time_t acqStartTime;
   time_t lastAiTime;
   time_t minAiDistance;
   time_t maxAiDistance;
   double avgAiDistance;
   uint   aiCount;
   uchar  hist_expir[STATS_HIST_WIDTH];
   uchar  hist_s1cur[STATS_HIST_WIDTH];
   uchar  hist_s1old[STATS_HIST_WIDTH];
   uchar  hist_s2cur[STATS_HIST_WIDTH];
   uchar  hist_s2old[STATS_HIST_WIDTH];
   uchar  histIdx;

   double varianceHist[VARIANCE_HIST_COUNT];  // ring buffer for variance
   uint   varianceHistCount;      // number of valid entries in the buffer
   uint   varianceHistIdx;        // last written index

   EPGDB_BLOCK_COUNT count[2];

   ulong  ttxPkgCount;       // copied here from epgdbacq module
   ulong  epgPkgCount;
   ulong  epgPagCount;

} EPGDB_STATS;


// ---------------------------------------------------------------------------
// Interface to main control module and user interface
//
bool EpgAcqCtl_Start( void );
void EpgAcqCtl_Stop( void );
int  EpgAcqCtl_Toggle( int newState );
EPGSCAN_START_RESULT EpgAcqCtl_StartScan( void );
void EpgAcqCtl_StopScan( void );
bool EpgAcqCtl_SelectMode( EPGACQ_MODE newAcqMode, uint cniCount, const uint * pCniTab );
bool EpgAcqCtl_SetInputSource( uint inputIdx );
bool EpgAcqCtl_UiProvChange( void );
EPGDB_STATE EpgAcqCtl_GetDbState( void );

// Interface for notifications from acquisition
#ifdef __EPGBLOCK_H
void EpgAcqCtl_ChannelChange( bool changeDb );
bool EpgAcqCtl_AiCallback( const AI_BLOCK *pNewAi );
bool EpgAcqCtl_BiCallback( const BI_BLOCK *pBi );
#endif

// has to be invoked about once a second by a timer when acq is running
void EpgAcqCtl_Idle( void );
void EpgAcqCtl_ProcessPackets( void );

// interface to statistics windows
const EPGDB_STATS * EpgAcqCtl_GetStatistics( void );


#endif  // __EPGACQCTL_H
