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
 *  $Id: epgacqctl.h,v 1.27 2001/05/31 17:08:07 tom Exp tom $
 */

#ifndef __EPGACQCTL_H
#define __EPGACQCTL_H


extern EPGDB_CONTEXT * pAcqDbContext;

// ---------------------------------------------------------------------------
// describing the state of the acq and db
//
typedef enum
{
   EPGDB_NOT_INIT,
   EPGDB_WAIT_SCAN,
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
   ACQMODE_EXTERNAL,        // set input source, then passive
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
   ACQPASSIVE_NO_DB,            // no db yet or database file is missing
   ACQPASSIVE_ACCESS_DEVICE     // failed to tune channel (access /dev/video)
} EPGACQ_PASSIVE;


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

#define NOWNEXT_TIMEOUT_AI_COUNT  5

// ---------------------------------------------------------------------------
// Structure to keep statistics about the current acq db
// - updated after every received AI block
// - can be retrieved by the GUI for the acq stats window

#define STATS_HIST_WIDTH     128
#define VARIANCE_HIST_COUNT    5

typedef struct
{
   uchar  expir;
   uchar  s1cur;
   uchar  s1old;
   uchar  s2cur;
   uchar  s2old;
} EPGDB_HIST;

typedef struct
{
   time_t acqStartTime;
   time_t lastAiTime;
   time_t minAiDistance;
   time_t maxAiDistance;
   double avgAiDistance;
   uint   aiCount;
   EPGDB_HIST hist[STATS_HIST_WIDTH];
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
// Structure to describe the state of acq to the user

// number of seconds without AI after which acq is considered "stalled"
#define ACQ_DESCR_STALLED_TIMEOUT  30

#define ACQ_COUNT_TO_PERCENT(C,T) (((T)>0) ? ((int)((double)(C) * 100.0 / (T))) : 100)

typedef enum
{
   ACQDESCR_DISABLED,
   ACQDESCR_SCAN,
   ACQDESCR_STARTING,
   ACQDESCR_NO_RECEPTION,
   ACQDESCR_STALLED,
   ACQDESCR_RUNNING,
} ACQDESCR_STATE;

typedef struct
{
   ACQDESCR_STATE state;
   EPGACQ_MODE    mode;
   EPGACQ_PHASE   cyclePhase;
   EPGACQ_PASSIVE passiveReason;
   uint           dbCni;
   uint           cycleCni;

} EPGACQ_DESCR;

// ---------------------------------------------------------------------------
// Interface to main control module and user interface
//
bool EpgAcqCtl_Start( void );
void EpgAcqCtl_Stop( void );
int  EpgAcqCtl_Toggle( int newState );
bool EpgAcqCtl_SelectMode( EPGACQ_MODE newAcqMode, uint cniCount, const uint * pCniTab );
bool EpgAcqCtl_SetInputSource( uint inputIdx );
bool EpgAcqCtl_UiProvChange( void );
bool EpgAcqCtl_CheckDeviceAccess( void );
EPGDB_STATE EpgAcqCtl_GetDbState( uint cni );
void EpgAcqCtl_DescribeAcqState( EPGACQ_DESCR * pAcqState );
void EpgAcqCtl_ToggleAcqForScan( bool enable );

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
