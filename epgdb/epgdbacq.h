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
 *  $Id: epgdbacq.h,v 1.12 2001/02/25 16:00:45 tom Exp tom $
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
// Declaration of the service interface functions
//

// interface to the EPG acquisition control module
void EpgDbAcqInit( void );
void EpgDbAcqStart( EPGDB_CONTEXT *dbc, uint pageNo, uint appId );
void EpgDbAcqStop( void );
void EpgDbAcqReset( EPGDB_CONTEXT *dbc, uint pageNo, uint appId );
void EpgDbAcqInitScan( void );
void EpgDbAcqGetScanResults( uint *pCni, bool *pNiWait, uint *pDataPageCnt );
uint EpgDbAcqGetMipPageNo( void );
void EpgDbAcqGetStatistics( ulong *pTtxPkgCount, ulong *pEpgPkgCount, ulong *pEpgPagCount );
void EpgDbAcqEnableVpsPdc( bool enable );

// interface to the teletext packet decoder
bool EpgDbAcqAddPacket( uint pageNo, uint sub, uchar pkgno, const uchar * data );
void EpgDbAcqAddVpsCode( uint cni );
void EpgDbAcqLostFrame( void );

// interface to the main event control - should be called every 40 ms in average
void EpgDbAcqProcessPackets( EPGDB_CONTEXT * const * pdbc );
bool EpgDbAcqCheckForPackets( void );


#endif  // __EPGDBACQ_H
