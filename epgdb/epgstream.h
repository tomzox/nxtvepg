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
 *  Author: Tom Zoerner
 *
 *  $Id: epgstream.h,v 1.17 2004/09/25 18:42:41 tom Exp tom $
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

typedef struct
{
   uint32_t  epgPageNo;         // EPG ttx page number
   uint32_t  epgPagCount;       // number of EPG ttx pages received
   uint32_t  epgPagMissing;     // number of missing pages
   uint32_t  epgPkgCount;       // number of EPG ttx packets received
   uint32_t  epgPkgMissing;     // number of missing packets
   uint32_t  epgBiCount;        // number of BI blocks
   uint32_t  epgAppId;          // EPG application ID from BI block
   uint32_t  epgBlkCount;       // number of blocks in stream (including BI)
   uint32_t  epgBlkErr;         // number of dropped blocks (decoding error, not drop due to missing ttx pkg)
   uint32_t  epgStrSum;         // number of string characters
   uint32_t  epgParErr;         // number of parity errors
   uint32_t  reserved_0[3];     // unused, always 0
} EPG_STREAM_STATS;

// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
void EpgStreamInit( EPGDB_QUEUE *pDbQueue, bool bWaitForBiAi, uint appId, uint pageNo );
void EpgStreamClear( void );
bool EpgStreamProcessPackets( void );
bool EpgStreamCheckSlicerQuality( void );
void EpgStreamGetStatistics( EPG_STREAM_STATS * pStats );


#endif // __EPGSTREAM_H