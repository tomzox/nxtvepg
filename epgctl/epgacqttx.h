/*
 *  Teletext EPG acquisition control
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
 *  $Id: epgacqttx.h,v 1.1 2006/11/25 22:09:28 tom Exp tom $
 */

#ifndef __EPGACQTTX_H
#define __EPGACQTTX_H

void EpgAcqTtx_Init( void );
void EpgAcqTtx_Destroy( void );
bool EpgAcqTtx_Start( EPGACQ_MODE mode, EPGACQ_PHASE cyclePhase );
void EpgAcqTtx_Stop( void );
void EpgAcqTtx_Suspend( void );
void EpgAcqTtx_SetParams( uint ttxSrcCount, const char * pTtxNames, const EPGACQ_TUNER_PAR * pTtxFreqs );
bool EpgAcqTtx_CompareParams( uint ttxSrcCount, const char * pTtxNames, const EPGACQ_TUNER_PAR * pTtxFreqs );

bool EpgAcqTtx_ProcessPackets( bool * pCheckSlicer );
bool EpgAcqTtx_MonitorSources( void );
void EpgAcqTtx_ChannelChange( void );
void EpgAcqTtx_InitCycle( bool isTtxSrc, EPGACQ_PHASE phase );

bool EpgAcqTtx_GetAcqStats( EPG_TTX_GRAB_STATS * pTtxGrabStats );
void EpgAcqTtx_DescribeAcqState( EPGACQ_DESCR * pAcqState );

#endif  // __EPGACQTTX_H
