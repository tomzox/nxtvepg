/*
 *  Nextview EPG block database interface
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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgdbif.h,v 1.10 2000/10/15 17:41:04 tom Exp tom $
 */

#ifndef __EPGDBIF_H
#define __EPGDBIF_H


// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
void  EpgDbLockDatabase( EPGDB_CONTEXT * dbc, uchar enable );
bool  EpgDbIsLocked( const EPGDB_CONTEXT * dbc );

const BI_BLOCK * EpgDbGetBi( const EPGDB_CONTEXT * dbc );
const AI_BLOCK * EpgDbGetAi( const EPGDB_CONTEXT * dbc );
const NI_BLOCK * EpgDbGetNi( const EPGDB_CONTEXT * dbc, uint block_no );
const OI_BLOCK * EpgDbGetOi( const EPGDB_CONTEXT * dbc, uint block_no );
const MI_BLOCK * EpgDbGetMi( const EPGDB_CONTEXT * dbc, uint block_no );
const LI_BLOCK * EpgDbGetLi( const EPGDB_CONTEXT * dbc, uint block_no, uchar netwop_no );
const TI_BLOCK * EpgDbGetTi( const EPGDB_CONTEXT * dbc, uint block_no, uchar netwop_no );
const PI_BLOCK * EpgDbGetPi( const EPGDB_CONTEXT * dbc, uint block_no, uchar netwop_no );
const PI_BLOCK * EpgDbGetFirstObsoletePi( const EPGDB_CONTEXT * dbc );
const PI_BLOCK * EpgDbGetNextObsoletePi( const EPGDB_CONTEXT * dbc, const PI_BLOCK * pPiBlock );
const PI_BLOCK * EpgDbSearchObsoletePi( const EPGDB_CONTEXT * dbc, uchar netwop_no, ulong start_time, ulong stop_time );

#ifdef __EPGDBFIL_H
const PI_BLOCK * EpgDbSearchPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc, uint block_no, uchar netwop_no );
const PI_BLOCK * EpgDbSearchPiExact( const EPGDB_CONTEXT * dbc, uint block_no, uchar netwop_no, ulong start_time );
const PI_BLOCK * EpgDbSearchFirstPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc );
const PI_BLOCK * EpgDbSearchLastPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc );
const PI_BLOCK * EpgDbSearchNextPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock );
const PI_BLOCK * EpgDbSearchPrevPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock );
#endif

uint  EpgDbGetProgIdx( const EPGDB_CONTEXT * dbc, uint block_no, uchar netwop );
uchar EpgDbGetStream( const void * pBlock );
uchar EpgDbGetVersion( const void * pBlock );
bool  EpgDbGetStat( const EPGDB_CONTEXT * dbc, EPGDB_BLOCK_COUNT * pCount );
ulong EpgDbGetPiBlockCount( uint startNo, uint stopNo );
ulong EpgDbGetPiBlockIndex( uint startNo, uint blockNo );
uint  EpgDbContextGetCni( const EPGDB_CONTEXT * dbc );


#endif  // __EPGDBIF_H
