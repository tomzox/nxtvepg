/*
 *  Nextview EPG PI timescale queue
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 */

#ifndef __EPGTSCQUEUE_H
#define __EPGTSCQUEUE_H


// ----------------------------------------------------------------------------
// Declaration of PI timescale queue data types
//

// these flags describe each element in the timescale buffer
#define PI_TSC_MASK_IS_OLD_VERSION   0x01
#define PI_TSC_MASK_DAY_MASK         0x06
#define PI_TSC_MASK_HAS_DESC_TEXT    0x08
#define PI_TSC_MASK_IS_EXPIRED       0x10
#define PI_TSC_MASK_IS_MISSING       0x20
#define PI_TSC_MASK_IS_DEFECTIVE     0x40
#define PI_TSC_MASK_IS_LAST          0x80

#define PI_TSC_MASK_DAY_SHIFT        0x01

// this element describes a given range on a timescale
// it covers one or more PI (of the same network), if they have equal flags
typedef struct
{
   uint32_t  startOffMins;       // start time, relative to base time (as offset to save space)
                                 // ATTENTION: 16-bit not sufficient as time range in XMLTV input may
                                 // be larger than a year upon decoding errors, or for expired data
   uint32_t  durationMins;       // absolute difference between start and stop time in minutes
   uint8_t   netwop;             // index in the AI network table of the current provider
   uint8_t   flags;              // logical OR of flags as defined above
   uint8_t   blockIdx;           // index in the PI block tange table in the AI block (lowest index in case of concatenation)
   uint8_t   concatCount;        // number of PI which are covered by this range (required to recover all block indices)
} EPGDB_PI_TSC_ELEM;

// maximum length of the range array: usually 200-500 are entries required
#define PI_TSC_BUF_LEN_INITIAL            150

typedef struct EPGDB_PI_TSC_BUF_STRUCT
{
   struct EPGDB_PI_TSC_BUF_STRUCT  * pNext;   // pointer to next buffer in the queue
   struct EPGDB_PI_TSC_BUF_STRUCT  * pPrev;   // pointer to previous buffer in the queue

   uint32_t     provCni;                      // provider the data in the buffer belongs to
   bool         locked;                       // locked until pending EPG blocks are processed
   uint16_t     fillCount;                    // number of valid entries in pi array
   uint16_t     popIdx;                       // number of 'popped' entries
   time32_t     baseTime;                     // base for start and stop time offsets

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
void EpgTscQueue_AddAll( EPGDB_PI_TSC * pQueue, EPGDB_CONTEXT * dbc,
                         time_t now, time_t tsAcq );

// interface to GUI
const EPGDB_PI_TSC_ELEM * EpgTscQueue_PopElem( EPGDB_PI_TSC * tsc, time_t * pBaseTime );


#endif  // __EPGTSCQUEUE_H
