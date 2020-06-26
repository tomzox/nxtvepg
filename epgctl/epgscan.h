/*
 *  Scan TV channels for Nextview EPG content providers
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
 *  $Id: epgscan.h,v 1.12 2002/09/14 18:13:19 tom Exp tom $
 */

#ifndef __EPGSCAN_H
#define __EPGSCAN_H


// result values for epg scan start function
typedef enum
{
   EPGSCAN_OK,                  // no error
   EPGSCAN_INTERNAL,            // internal error
   EPGSCAN_ACCESS_DEV_VIDEO,    // failed to set input source or tuner freq
   EPGSCAN_ACCESS_DEV_VBI,      // failed to start acq
   EPGSCAN_NO_TUNER,            // input source is not a TV tuner
   EPGSCAN_START_RESULT_COUNT
} EPGSCAN_START_RESULT;

// internal state during epg scan
typedef enum
{
   SCAN_STATE_OFF,
   SCAN_STATE_RESET,
   SCAN_STATE_WAIT_SIGNAL,
   SCAN_STATE_WAIT_ANY,
   SCAN_STATE_WAIT_NI,
   SCAN_STATE_DONE
} EPGSCAN_STATE;

// callback function which is used by the scan handler to give user feedback
typedef void (EPGSCAN_MSGCB) ( const char * pMsg, bool bold );
typedef void (EPGSCAN_DELCB) ( uint cni );

// ---------------------------------------------------------------------------
// Interface to main control module and user interface
//
EPGSCAN_START_RESULT EpgScan_Start( int inputSource, bool doSlow, bool useXawtv,
                                    char * chnNames, EPGACQ_TUNER_PAR *freqTab,
                                    uint freqCount, uint * pRescheduleMs,
                                    EPGSCAN_MSGCB * pMsgCallback );
uint EpgScan_EvHandler( void );
void EpgScan_Stop( void );
void EpgScan_SetSpeed( bool doFast );
bool EpgScan_IsActive( void );
double EpgScan_GetProgressPercentage( void );

#endif  // __EPGSCAN_H
