/*
 *  Decoding of teletext and VPS packets
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
 *  $Id: ttxdecode.h,v 1.32 2009/05/02 19:30:34 tom Exp tom $
 */

#ifndef __TTXDECODE_H
#define __TTXDECODE_H

// ---------------------------------------------------------------------------
// Global definitions

#define MIP_EPG_ID          0xE3

#define EPG_DEFAULT_PAGENO  0x1DF
#define EPG_ILLEGAL_PAGENO  0
#define VALID_EPG_PAGENO(X) ((((X)>>8)<8) && ((((X)&0xF0)>=0xA0) || (((X)&0x0F)>=0x0A)))

// constants for calculation of ttx pkg/page running average
#define TTX_PKG_RATE_FIXP   16   // number of binary digits in fix point arithmetic
#define TTX_PKG_RATE_FACT   4    // ln2 of forgetting factor, i.e. new values count as 1/(2^FACT)

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

// interface to the EPG acquisition control module and EPG scan
void TtxDecode_StartEpgAcq( uint epgPageNo, bool isEpgScan );
void TtxDecode_StopEpgAcq( void );
void TtxDecode_StartTtxAcq( bool enableScan, uint startPageNo, uint stopPageNo );
void TtxDecode_StopTtxAcq( void );
void TtxDecode_InitScan( void );
void TtxDecode_GetScanResults( uint *pCni, bool *pNiWait, uint *pDataPageCnt, uchar *pDispText, uint textMaxLen );
uint TtxDecode_GetMipPageNo( void );
uint TtxDecode_GetDateTime( sint * pLto );
bool TtxDecode_GetPageHeader( char * pBuf, uint * pPgNum, uint pkgOff );
bool TtxDecode_GetMagStats( uint * pMagBuf, sint * pPgDirection, bool reset );
bool TtxDecode_CheckForPackets( bool * pStopped );
#ifdef __BTDRV_H
const VBI_LINE * TtxDecode_GetPacket( uint pkgOff );
void TtxDecoder_ReleasePackets( void );
bool TtxDecode_GetCniAndPil( uint * pCni, uint * pPil, CNI_TYPE * pCniType,
                             uint pCniInd[3], uint pPilInd[3],
                             volatile EPGACQ_BUF * pThisVbiBuf );
void TtxDecode_GetStatistics( TTX_DEC_STATS * pStats, time_t * pStatsStart );
void TtxDecode_NotifyChannelChange( volatile EPGACQ_BUF * pThisVbiBuf );
#endif

// interface to the teletext packet decoder
void TtxDecode_AddPacket( const uchar * data, uint line );
void TtxDecode_AddVpsData( const uchar * data );
bool TtxDecode_NewVbiFrame( uint frameSeqNo );

#endif  // __TTXDECODE_H
