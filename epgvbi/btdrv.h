/*
 *  VBI capture driver for the Booktree 848/849/878/879 family
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
 *  $Id: btdrv.h,v 1.4 2000/12/26 15:51:26 tom Exp tom $
 */

#ifndef __BTDRV_H
#define __BTDRV_H


// ---------------------------------------------------------------------------
// Buffer for communication between slave and master process
// - configuration parameters and state
// - ring buffer for EPG packets

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
   uchar      cardIndex;
} EPGACQ_BUF;


extern EPGACQ_BUF *pVbiBuf;

// ---------------------------------------------------------------------------
// declaration of service interface functions
//

// interface to acquisition control and EPG scan
bool BtDriver_Init( void );
void BtDriver_Exit( void );
bool BtDriver_StartAcq( void );
void BtDriver_StopAcq( void );
bool BtDriver_IsVideoPresent( void );
bool BtDriver_TuneChannel( ulong freq, bool keepOpen );
bool BtDriver_SetInputSource( int inputIdx, bool keepOpen, bool * pIsTuner );

#ifndef WIN32
void BtDriver_CheckParent( void );
void BtDriver_CloseDevice( void );
bool BtDriver_CheckDevice( void );
#endif

// interface to GUI
const char * BtDriver_GetCardName( uint cardIdx );
const char * BtDriver_GetTunerName( uint tunerIdx );
const char * BtDriver_GetInputName( uint cardIdx, uint inputIdx );
void BtDriver_Configure( int cardIndex, int tunerType, int pll, int prio );


#endif  // __BTDRV_H
