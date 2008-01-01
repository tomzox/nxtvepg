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
 *  Description: see C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgacqnxt.h,v 1.1 2006/11/25 22:09:01 tom Exp tom $
 */

#ifndef __EPGACQNXT_H
#define __EPGACQNXT_H

// ---------------------------------------------------------------------------
// Function interface
//
void EpgAcqNxtv_Init( void );
void EpgAcqNxtv_Destroy( void );
bool EpgAcqNxtv_Start( EPGACQ_MODE mode, EPGACQ_PHASE cyclePhase, uint cni );
void EpgAcqNxtv_Stop( void );
void EpgAcqNxtv_Suspend( void );

bool EpgAcqNxtv_ProcessPackets( bool * pCheckSlicer );
void EpgAcqNxtv_ProcessBlocks( bool * pAdvance );
bool EpgAcqNxtv_MonitorSource( void );
void EpgAcqNxtv_ChannelChange( bool changeDb );

void EpgAcqNxtv_EnableTimescales( bool enable );
EPGDB_PI_TSC * EpgAcqNxtv_GetTimescaleQueue( void );
EPGDB_CONTEXT * EpgAcqNxtv_GetDbContext( void );
bool EpgAcqNxtv_GetAcqStats( EPG_NXTV_ACQ_STATS * pNxtvAcqStats );
void EpgAcqNxtv_DescribeAcqState( EPGACQ_DESCR * pAcqState );

#endif  // __EPGACQNXT_H
