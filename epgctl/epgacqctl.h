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
 *  $Id: epgacqctl.h,v 1.10 2000/07/08 18:34:22 tom Exp tom $
 */

#ifndef __EPGACQCTL_H
#define __EPGACQCTL_H


// ---------------------------------------------------------------------------
// describing the state of the acq and db
//
typedef enum
{
   // states while acq is running
   DBSTATE_NO_TTX,          // no teletext reception
   DBSTATE_WAIT_EPG,        // TTX, but no EPG on default page 0x1DF
   DBSTATE_WAIT_AI,         // EPG reception; wait for AI,BI
   DBSTATE_UPDATING,        // acq running, db ok
   // states while acq is stopped
   DBSTATE_EMPTY,           // acq not running, db empty
   DBSTATE_OK               // acq not running, db ok
} EPGDB_STATE;

// internal state of the control module
typedef enum
{
   ACQSTATE_OFF,            // acq is disabled
   ACQSTATE_WAIT_BI,        // wait for BI block
   ACQSTATE_WAIT_AI,        // wait for BI block
   ACQSTATE_RUNNING         // acq is up & running
} EPGACQ_STATE;

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
#define EPGACQCTL_MODIF_INTV   5*60   // max. interval without reception

// ---------------------------------------------------------------------------
// Structure to keep statistics

#define STATS_HIST_WIDTH 128

typedef struct
{
   ulong  acqStartTime;
   ulong  lastAiTime;
   uint   minAiDistance;
   uint   maxAiDistance;
   uint   aiCount;
   uchar  hist_expir[STATS_HIST_WIDTH];
   uchar  hist_s1cur[STATS_HIST_WIDTH];
   uchar  hist_s1old[STATS_HIST_WIDTH];
   uchar  hist_s2cur[STATS_HIST_WIDTH];
   uchar  hist_s2old[STATS_HIST_WIDTH];
   uchar  histIdx;

   EPGDB_BLOCK_COUNT count[2];  // evaluated after every AI block

   ulong  ttxPkgCount;       // copied here from epgdbacq module
   ulong  epgPkgCount;
   ulong  epgPagCount;

} EPGDB_STATS;


// ---------------------------------------------------------------------------
// Interface to main control module and user interface
bool EpgAcqCtl_Start( void );
void EpgAcqCtl_Stop( void );
int  EpgAcqCtl_Toggle( int newState );
bool EpgAcqCtl_OpenDb( int target, uint cni );
void EpgAcqCtl_CloseDb( int target );
bool EpgAcqCtl_StartScan( void );
void EpgAcqCtl_StopScan( void );

// Interface for notifications from acquisition
#ifdef __EPGBLOCK_H
void EpgAcqCtl_ChannelChange( void );
bool EpgAcqCtl_AiCallback( const AI_BLOCK *pNewAi );
bool EpgAcqCtl_BiCallback( const BI_BLOCK *pBi );
#endif

// has to be invoked about once a second by a timer when acq is running
void EpgAcqCtl_Idle( void );
void EpgAcqCtl_ProcessPackets( void );

// interface to statistics windows
const EPGDB_STATS * EpgAcqCtl_GetStatistics( void );


#endif  // __EPGACQCTL_H
