/*
 *  Nextview EPG block database
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
 *  $Id: epgdbacq.h,v 1.20 2002/05/01 18:12:54 tom Exp tom $
 */

#ifndef __EPGDBACQ_H
#define __EPGDBACQ_H

// ---------------------------------------------------------------------------
// Global definitions

#define MIP_EPG_ID          0xE3

#define EPG_DEFAULT_PAGENO  0x1DF
#define EPG_ILLEGAL_PAGENO  0
#define VALID_EPG_PAGENO(X) ((((X)>>8)<8) && ((((X)&0xF0)>=0xA0) || (((X)&0x0F)>=0x0A)))

// ---------------------------------------------------------------------------
// VPS system codes, as defined in "VPS Richtlinie 8R2" from August 1995
//
// system code -> this network does not support PIL labeling (may be temporary)
#define VPS_PIL_CODE_SYSTEM ((0 << 15) | (15 << 11) | (31 << 6) | 63)
// empty code -> the current broadcast is just a filler and not worth recording
#define VPS_PIL_CODE_EMPTY  ((0 << 15) | (15 << 11) | (30 << 6) | 63)
// pause code -> the previous transmission is paused and will be continued later
#define VPS_PIL_CODE_PAUSE  ((0 << 15) | (15 << 11) | (29 << 6) | 63)
// macro to check if the PIL is not a control code
#define VPS_PIL_IS_VALID(PIL) ((((PIL) >> 6) & 0x1f) < 24)

#define INVALID_VPS_PIL     VPS_PIL_CODE_SYSTEM


// ---------------------------------------------------------------------------
// Declaration of the service interface functions
//

// interface to the EPG acquisition control module
void EpgDbAcqInit( void );
void EpgDbAcqStart( EPGDB_CONTEXT *dbc, EPGDB_QUEUE * pQueue, uint pageNo, uint appId );
void EpgDbAcqStop( void );
void EpgDbAcqReset( EPGDB_CONTEXT *dbc, EPGDB_QUEUE * pQueue, uint pageNo, uint appId );
void EpgDbAcqInitScan( void );
void EpgDbAcqNotifyChannelChange( void );
void EpgDbAcqGetScanResults( uint *pCni, bool *pNiWait, uint *pDataPageCnt );
uint EpgDbAcqGetMipPageNo( void );
void EpgDbAcqGetStatistics( uint32_t * pTtxPkgCount, uint32_t * pEpgPkgCount, uint32_t * pEpgPagCount );
bool EpgDbAcqGetCniAndPil( uint * pCni, uint *pPil );

// interface to the teletext packet decoder
void EpgDbAcqAddPacket( const uchar * data );
void EpgDbAcqAddVpsData( const uchar * data );
bool EpgDbAcqNewVbiFrame( uint frameSeqNo );

// interface to the main event control - should be called every 1-2 secs in average
bool EpgDbAcqProcessPackets( void );
bool EpgDbAcqCheckForPackets( bool * pStopped );


#endif  // __EPGDBACQ_H
