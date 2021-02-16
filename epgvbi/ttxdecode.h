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
 *  $Id: ttxdecode.h,v 1.33 2020/06/17 19:57:29 tom Exp tom $
 */

#ifndef __TTXDECODE_H
#define __TTXDECODE_H

// ---------------------------------------------------------------------------
// Global definitions

#define MIP_EPG_ID          0xE3

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
void TtxDecode_StartTtxAcq( bool enableScan, uint startPageNo, uint stopPageNo );
void TtxDecode_StopTtxAcq( void );
void TtxDecode_StartScan( void );
void TtxDecode_StopScan( void );
void TtxDecode_GetScanResults( uint bufIdx, uint *pCni, bool *pNiWait, char *pDispText, uint textMaxLen );
uint TtxDecode_GetDateTime( uint bufIdx, sint * pLto );
bool TtxDecode_GetPageHeader( uint bufIdx, uchar * pBuf, uint * pPgNum, uint pkgOff );
bool TtxDecode_GetMagStats( uint bufIdx, uint * pMagBuf, sint * pPgDirection, bool reset );
bool TtxDecode_CheckForPackets( bool * pStopped );
#ifdef __BTDRV_H
const VBI_LINE * TtxDecode_GetPacket( uint bufIdx, uint pkgOff );
void TtxDecoder_ReleasePackets( void );
bool TtxDecode_GetCniAndPil( uint bufIdx, uint * pCni, uint * pPil, CNI_TYPE * pCniType,
                             uint pCniInd[3], uint pPilInd[3],
                             volatile EPGACQ_BUF * pThisVbiBuf );
void TtxDecode_GetStatistics( uint bufIdx, TTX_DEC_STATS * pStats, time_t * pStatsStart );
void TtxDecode_NotifyChannelChange( uint bufIdx, volatile EPGACQ_BUF * pThisVbiBuf );
#endif

// interface to the teletext packet decoder
void TtxDecode_AddPacket( uint bufIdx, const uchar * data, uint line );
void TtxDecode_AddVpsData( uint bufIdx, const uchar * data );
bool TtxDecode_NewVbiFrame( uint bufIdx, uint frameSeqNo );

#endif  // __TTXDECODE_H
