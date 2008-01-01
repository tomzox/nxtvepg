/*
 *  Teletext page grabber
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
 *  Description: see C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: ttxgrab.h,v 1.3 2007/03/10 19:31:32 tom Exp tom $
 */

#ifndef __TTXGRAB_H
#define __TTXGRAB_H


// ----------------------------------------------------------------------------
// Declaration of the acquisition statistics structure
//
typedef struct
{
   uint32_t  ttxPageStartNo;    // TTX page start and stop number
   uint32_t  ttxPageStopNo;
   uint32_t  ttxPagCount;       // number of ttx pages received
   uint32_t  ttxPkgCount;       // number of EPG ttx packets received
   uint32_t  ttxPkgStrSum;      // number of characters in all packets
   uint32_t  ttxPkgParErr;      // number of parity errors
   uint32_t  reserved_0[8];     // unused, always 0
} TTX_GRAB_STATS;

// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
#ifdef USE_TTX_GRABBER
bool TtxGrab_Start( uint startPage, uint stopPage, bool enableOutput );
void TtxGrab_Stop( void );
bool TtxGrab_ProcessPackets( void );
bool TtxGrab_CheckSlicerQuality( void );
void TtxGrab_GetStatistics( TTX_GRAB_STATS * pStats );
void TtxGrab_PostProcess( const char * pName, bool reset );
bool TtxGrab_CheckPostProcess( char ** ppXmlName );
void TtxGrab_GetPageStats( bool * pInRange, bool * pRangeDone, bool * pSourceLock );

void TtxGrab_Init( void );
void TtxGrab_Exit( void );
void TtxGrab_SetConfig( uint expireMin, const char * pPerlPath, bool keepTtxInp );
#endif // USE_TTX_GRABBER

#endif // __TTXGRAB_H
