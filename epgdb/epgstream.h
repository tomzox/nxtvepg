/*
 *  Nextview stream decoder
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
 *  $Id: epgstream.h,v 1.9 2000/06/24 18:04:04 tom Exp tom $
 */

#ifndef __EPGSTREAM_H
#define __EPGSTREAM_H


// ----------------------------------------------------------------------------
// Declaration of the internal stream decoder status
//

#define NXTV_STREAM_NO_1    0
#define NXTV_STREAM_NO_2    1
#define NXTV_NO_STREAM      2

// definition of block types according to ETS 300 707 (Nextview spec.)
// - please note that internally a different type definition is used (see epgblock.h)
//   (this is because of ordering and the gaps in the following enumeration)
enum
{
   EPGDBACQ_TYPE_BI = 0x00
  ,EPGDBACQ_TYPE_AI = 0x01
  ,EPGDBACQ_TYPE_PI = 0x02
  ,EPGDBACQ_TYPE_NI = 0x03
  ,EPGDBACQ_TYPE_OI = 0x04
  ,EPGDBACQ_TYPE_MI = 0x05
  ,EPGDBACQ_TYPE_UI = 0x06
  ,EPGDBACQ_TYPE_LI = 0x07
  ,EPGDBACQ_TYPE_TI = 0x08
  ,EPGDBACQ_TYPE_CI = 0x3E
  ,EPGDBACQ_TYPE_HI = 0x3F
};

// ----------------------------------------------------------------------------
// declaration of private decoder status
//
// max block len (12 bits) - see ETS 300 708
// note that this does not include the 4 header bytes with length and appId
#define NXTV_BLOCK_MAXLEN 2048

typedef struct
{
   uchar ci, pkgCount, lastPkg;
   uchar appID;
   uint  blockLen, recvLen;
   bool  haveBlock;
   bool  haveHeader;
   uchar headerFragment[3];
   uchar blockBuf[NXTV_BLOCK_MAXLEN + 4];
} NXTV_STREAM;

// max. number of pages that are allowed for EPG transmission: mFd & mdF: m[0..7],d[0..9]
#define NXTV_VALID_PAGE_PER_MAG (1 + 10 + 10)
#define NXTV_VALID_PAGE_COUNT   (8 * NXTV_VALID_PAGE_PER_MAG)

typedef struct
{
   uchar pkgCount;
   uchar lastPkg;
   uchar okCount;
} PAGE_SCAN_STATE;


// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
void EpgStreamInit( bool bWaitForAiBi, uint appId );
void EpgStreamEnableAllTypes( void );
bool EpgStreamNewPage( uint sub );
void EpgStreamDecodePacket( uchar packNo, const uchar * dat );

EPGDB_BLOCK * EpgStreamGetNextBlock( void );
EPGDB_BLOCK * EpgStreamGetBlockByType( uchar type );
void          EpgStreamClearScratchBuffer( void );

void EpgStreamSyntaxScanInit( void );
void EpgStreamSyntaxScanHeader( uint page, uint sub );
bool EpgStreamSyntaxScanPacket( uchar mag, uchar packNo, const uchar * dat );


#endif // __EPGSTREAM_H
