/*
 *  Tuner channel to frequency translation
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
 *  Description:
 *
 *    This module allows to loop over all available TV frequencies.
 *    Currently only the European channel division is supported,
 *    but that's enough since Nextview is not transmitted outside
 *    of Europe.
 *
 *    The tables in this modules were extracted from xtvscreen 0.5.22,
 *    which is (was) part of the bttv driver package for Linux.
 *    Copyright (C) 1996,97 Ralph Metzler  (rjkm@thp.uni-koeln.de)
 *                        & Marcus Metzler (mocm@thp.uni-koeln.de)
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: tvchan.c,v 1.1 2000/12/09 14:03:38 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/tvchan.h"


// ---------------------------------------------------------------------------
// Channel frequency table (Europe only)
//
typedef struct
{
   double  freqStart;      // base freq of first channel in this band
   double  freqMax;        // max. freq of last channel, including fine-tuning
   double  freqOffset;     // freq offset between two channels in this band
   uint    firstChannel;
   uint    lastChannel;
} FREQ_TABLE;

// channel table for Europe
#define FIRST_CHANNEL   2
const FREQ_TABLE freqTable[] =
{  // must be sorted by channel numbers
   { 48.25,   66.25, 7.0,   2,   4},
   {175.25,  228.25, 7.0,   5,  12},
   {471.25,  859.25, 8.0,  21,  69},
   {112.25,  172.25, 7.0,  72,  80},
   {231.25,  298.25, 7.0,  81,  90},
   {303.25,  451.25, 8.0,  91, 109},
   {455.25,  471.25, 8.0, 110, 112},
   {859.25, 1175.25, 8.0, 161, 200}
};
#define FREQ_TABLE_COUNT  (sizeof(freqTable)/sizeof(FREQ_TABLE))


// ---------------------------------------------------------------------------
// Get number of channels in table (for EPG scan progress bar)
//
int TvChannels_GetCount( void )
{
   int band, count;

   count = 0;
   for (band=0; band < FREQ_TABLE_COUNT; band++)
   {
      count += freqTable[band].lastChannel - freqTable[band].firstChannel + 1;
   }
   return count;
}

// ---------------------------------------------------------------------------
// Get the next channel and frequency from the table
//
bool TvChannels_GetNext( uint *pChan, ulong *pFreq )
{
   int band;

   if (*pChan < FIRST_CHANNEL)
      *pChan = FIRST_CHANNEL;
   else
      *pChan = *pChan + 1;

   for (band=0; band < FREQ_TABLE_COUNT; band++)
   {
      // assume that the table is sorted by channel numbers
      if (*pChan < freqTable[band].lastChannel)
      {
         // skip possible channel gap between bands
         if (*pChan < freqTable[band].firstChannel)
            *pChan = freqTable[band].firstChannel;

         // get the frequency of this channel
         *pFreq = (ulong) (16.0 * (freqTable[band].freqStart +
                                  (*pChan - freqTable[band].firstChannel) * freqTable[band].freqOffset));
         break;
      }
   }

   return (band < FREQ_TABLE_COUNT);
}

