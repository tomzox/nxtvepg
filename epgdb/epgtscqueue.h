/*
 *  Nextview EPG PI timescale queue
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
 *  $Id: epgtscqueue.h,v 1.6 2008/02/03 15:42:36 tom Exp tom $
 */

#ifndef __EPGTSCQUEUE_H
#define __EPGTSCQUEUE_H


// ----------------------------------------------------------------------------
// Declaration of PI timescale queue data types
//

// these flags describe each element in the timescale buffer
#define PI_TSC_MASK_IS_EXPIRED       0x01
#define PI_TSC_MASK_IS_MISSING       0x02
#define PI_TSC_MASK_IS_LAST          0x04
#define PI_TSC_MASK_IS_DEFECTIVE     0x08
#define PI_TSC_MASK_IS_CUR_VERSION   0x10
//#define PI_TSC_MASK_IS_STREAM_1      0x20
#define PI_TSC_MASK_HAS_SHORT_I      0x40
#define PI_TSC_MASK_HAS_LONG_I       0x80

// this element describes a given range on a timescale
// it covers one or more PI (of the same network), if they have equal flags
typedef struct
{
   uint16_t  startOffMins;       // start time, relative to base time (as offset to save 16 bit)
   uint16_t  durationMins;       // absolute difference between start and stop time in minutes
   uint8_t   netwop;             // index in the AI network table of the current provider
   uint8_t   flags;              // logical OR of flags as defined above
   uint8_t   blockIdx;           // index in the PI block tange table in the AI block (lowest index in case of concatenation)
   uint8_t   concatCount;        // number of PI which are covered by this range (required to recover all block indices)
} EPGDB_PI_TSC_ELEM;

// maximum length of the range array: usually 200-500 are entries required
#define PI_TSC_BUF_LEN_INITIAL            150

typedef struct EPGDB_PI_TSC_BUF_STRUCT
{
   uint32_t     provCni;                      // provider the data in the buffer belongs to
   bool         locked;                       // locked until pending EPG blocks are processed
   uint16_t     fillCount;                    // number of valid entries in pi array
   uint16_t     popIdx;                       // number of 'popped' entries
   uint8_t      resv_0[2];                    // unused
   time32_t     baseTime;                     // base for start and stop time offsets
   union
   {
      struct
      {
         struct EPGDB_PI_TSC_BUF_STRUCT  * pNext;   // pointer to next buffer in the queue
         struct EPGDB_PI_TSC_BUF_STRUCT  * pPrev;   // pointer to previous buffer in the queue
      } p;
#if defined (USE_32BIT_COMPAT)
      uint8_t   resv_1[2*8];
#endif
   } u;
   EPGDB_PI_TSC_ELEM  pi[1];                  // array with the actual data; size depends on mode
                                              // array must be last to allow partial transmission
} EPGDB_PI_TSC_BUF;

// macros to calculate sizes of a timescale buffer
#define PI_TSC_GET_BUF_COUNT()     PI_TSC_BUF_LEN_INITIAL
#define PI_TSC_GET_BUF_SIZE(C)     ((uint)( sizeof(EPGDB_PI_TSC_BUF) + \
                                          (sizeof(EPGDB_PI_TSC_ELEM) * ((C) - 1))))

// this type holds root of the queue
// other modules pass a pointer to this as a handle
typedef struct
{
   EPGDB_PI_TSC_BUF   * pHead;                // pointer to first buffer in the queue (newest data)
   EPGDB_PI_TSC_BUF   * pTail;                // pointer to last buffer in the queue (oldest data)
   uint          writeProvCni;                // provider CNI for additions
   uint          readProvCni;                 // provider CNI for pop
} EPGDB_PI_TSC;


// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
void EpgTscQueue_Init( EPGDB_PI_TSC * tsc );
void EpgTscQueue_Clear( EPGDB_PI_TSC * tsc );

void EpgTscQueue_SetProvCni( EPGDB_PI_TSC * pQueue, uint provCni );
void EpgTscQueue_AddAll( EPGDB_PI_TSC * tsc, EPGDB_CONTEXT * dbc );

// interface to GUI
const EPGDB_PI_TSC_ELEM * EpgTscQueue_PopElem( EPGDB_PI_TSC * tsc, time_t * pBaseTime );


#endif  // __EPGTSCQUEUE_H
