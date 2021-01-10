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
 *
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgdbif.h,v 1.32 2007/01/20 21:32:28 tom Exp tom $
 */

#ifndef __EPGDBIF_H
#define __EPGDBIF_H


// ----------------------------------------------------------------------------
// Types
//
typedef enum
{
   STARTING_AT,
   RUNNING_AT
} EPGDB_TIME_SEARCH_MODE;

// database statistics
typedef struct
{
   uint32_t  day1[2];
   uint32_t  day2[2];
   uint32_t  day3[2];
   uint32_t  expiredSinceAcq;
   uint32_t  expired;
   uint32_t  defective;
} EPGDB_BLOCK_COUNT;

// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
void  EpgDbLockDatabase( EPGDB_CONTEXT * dbc, uchar enable );
bool  EpgDbIsLocked( const EPGDB_CONTEXT * dbc );

const AI_BLOCK * EpgDbGetAi( const EPGDB_CONTEXT * dbc );
const OI_BLOCK * EpgDbGetOi( const EPGDB_CONTEXT * dbc );

const PI_BLOCK * EpgDbGetFirstObsoletePi( const EPGDB_CONTEXT * dbc );
const PI_BLOCK * EpgDbGetNextObsoletePi( const EPGDB_CONTEXT * dbc, const PI_BLOCK * pPiBlock );
const PI_BLOCK * EpgDbSearchObsoletePi( const EPGDB_CONTEXT * dbc, uchar netwop_no, time_t start_time, time_t stop_time );
const PI_BLOCK * EpgDbSearchPiByPil( const EPGDB_CONTEXT * dbc, uchar netwop_no, uint pil );

#ifdef __EPGDBFIL_H
const PI_BLOCK * EpgDbSearchPi( const EPGDB_CONTEXT * dbc, time_t start_time, uchar netwop_no );
const PI_BLOCK * EpgDbSearchFirstPiAfter( const EPGDB_CONTEXT * dbc, time_t min_time, EPGDB_TIME_SEARCH_MODE startOrStop, const FILTER_CONTEXT *fc );
const PI_BLOCK * EpgDbSearchFirstPiBefore( const EPGDB_CONTEXT * dbc, time_t start_time, EPGDB_TIME_SEARCH_MODE startOrStop, const FILTER_CONTEXT *fc );
const PI_BLOCK * EpgDbSearchFirstPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc );
const PI_BLOCK * EpgDbSearchLastPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc );
const PI_BLOCK * EpgDbSearchNextPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock );
const PI_BLOCK * EpgDbSearchPrevPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock );
uint EpgDbCountPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock );
uint EpgDbCountPrevPi( const EPGDB_CONTEXT * dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock );
#endif

bool  EpgDbGetVpsTimestamp( struct tm * pVpsTime, uint pil, time_t startTime );
uint  EpgDbGetProgIdx( const EPGDB_CONTEXT * dbc, const PI_BLOCK * pPiBlock );
uint  EpgDbContextGetCni( const EPGDB_CONTEXT * dbc );
bool  EpgDbContextIsMerged( const EPGDB_CONTEXT * dbc );
time_t EpgDbGetAiUpdateTime( const EPGDB_CONTEXT * dbc );
time_t EpgDbGetPiUpdateTime( const PI_BLOCK * pPiBlock );
void EpgDbSetAiUpdateTime( const EPGDB_CONTEXT * dbc, time_t acqTimestamp );
void EpgDbSetPiAcqCallback( EPGDB_CONTEXT * dbc, EPGDB_PI_ACQ_CB * pCb );

bool EpgDbGetStat( const EPGDB_CONTEXT * dbc, EPGDB_BLOCK_COUNT * pCount, time_t now, time_t tsAcq );

#endif  // __EPGDBIF_H
