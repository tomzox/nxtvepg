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
 *  $Id: epgdbacq.h,v 1.14 2001/06/10 08:08:37 tom Exp tom $
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
// Definition of callback functions from the 
//
typedef struct 
{
   bool (* pAiCallback)( const AI_BLOCK *pNewAi );
   bool (* pBiCallback)( const BI_BLOCK *pNewBi );
   void (* pChannelChange)( bool changeDb );
   void (* pStopped)( void );
} EPGDB_ACQ_CB;

// ---------------------------------------------------------------------------
// Declaration of the service interface functions
//

// interface to the EPG acquisition control module
void EpgDbAcqInit( void );
void EpgDbAcqStart( EPGDB_CONTEXT *dbc, uint pageNo, uint appId );
void EpgDbAcqStop( void );
void EpgDbAcqReset( EPGDB_CONTEXT *dbc, uint pageNo, uint appId );
void EpgDbAcqInitScan( void );
void EpgDbAcqSetCallbacks( const EPGDB_ACQ_CB * pCb );
void EpgDbAcqNotifyChannelChange( void );
void EpgDbAcqGetScanResults( uint *pCni, bool *pNiWait, uint *pDataPageCnt );
uint EpgDbAcqGetMipPageNo( void );
void EpgDbAcqGetStatistics( ulong *pTtxPkgCount, ulong *pEpgPkgCount, ulong *pEpgPagCount );
void EpgDbAcqResetVpsPdc( void );
bool EpgDbAcqGetCniAndPil( uint * pCni, uint *pPil );

// interface to the teletext packet decoder
bool EpgDbAcqAddPacket( uint pageNo, uint sub, uchar pkgno, const uchar * data );
void EpgDbAcqAddVpsData( const char * data );
void EpgDbAcqLostFrame( void );

// interface to the main event control - should be called every 1-2 secs in average
void EpgDbAcqProcessPackets( EPGDB_CONTEXT * const * pdbc );
bool EpgDbAcqCheckForPackets( void );


#endif  // __EPGDBACQ_H
