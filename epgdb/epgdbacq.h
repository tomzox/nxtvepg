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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgdbacq.h,v 1.8 2000/06/24 18:03:13 tom Exp tom $
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
// internal ring buffer for EPG packets
//
// number of teletext packets that can be stored in ring buffer
// - Nextview maximum data rate is 5 pages per second (200ms min distance)
//   data rate usually is much lower though, around 1-2 pages per sec
// - room for 1-2 secs should be enought in most cases, i.e. 2*5*24=240
#define EPGACQ_BUF_COUNT  512

typedef struct
{
   uint    pageno;
   uint    sub;
   uchar   pkgno;
   uchar   data[40];
} VBI_LINE;

typedef struct
{
   bool       isEnabled;
   bool       isEpgScan;
   bool       isEpgPage;
   uchar      isMipPage;
   uint       epgPageNo;

   uint       mipPageNo;
   uint       dataPageCount;
   uint       vpsCni;
   uint       pdcCni;
   uint       ni;
   uchar      niRepCnt;

   uint       writer_idx;
   uint       reader_idx;
   VBI_LINE   line[EPGACQ_BUF_COUNT];

   ulong      ttxPkgCount;
   ulong      epgPkgCount;
   ulong      epgPagCount;

   #ifndef WIN32
   pid_t      vbiPid;
   pid_t      epgPid;
   #endif
} EPGACQ_BUF;


// ---------------------------------------------------------------------------
// Declaration of the service interface functions
//

// interface to the EPG acquisition control module
void EpgDbAcqInit( EPGACQ_BUF * pShm );
void EpgDbAcqStart( EPGDB_CONTEXT *dbc, uint pageNo, uint appId );
void EpgDbAcqStop( void );
void EpgDbAcqReset( EPGDB_CONTEXT *dbc, uint pageNo, uint appId );
void EpgDbAcqInitScan( void );
void EpgDbAcqGetScanResults( uint *pCni, bool *pNiWait, uint *pDataPageCnt );
uint EpgDbAcqGetMipPageNo( void );
void EpgDbAcqGetStatistics( ulong *pTtxPkgCount, ulong *pEpgPkgCount, ulong *pEpgPagCount );

// interface to the teletext packet decoder
bool EpgDbAcqAddPacket( uint pageNo, uint sub, uchar pkgno, const uchar * data );

// interface to the main event control - should be called every 40 ms in average
void EpgDbAcqProcessPackets( EPGDB_CONTEXT * const * pdbc );
bool EpgDbAcqCheckForPackets( void );


#endif  // __EPGDBACQ_H
