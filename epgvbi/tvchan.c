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
 *    Updated from xawtv-3.21,
 *    Copyright (C) Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 *  Author: Tom Zoerner
 *
 *  $Id: tvchan.c,v 1.6 2001/04/17 19:58:09 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <stdlib.h>
#include <string.h>

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
   const uchar * prefix;   // prefix for the channel name
   uint    idxOffset;      // start index of a band namespace
} FREQ_TABLE;


// channel table for Western Europe
const FREQ_TABLE freqTableEurope[] =
{  // must be sorted by channel numbers
   { 48.25,   66.25, 7.0,   2,   4,  "E",   0},   // CCIR I/III (2-4)
   {175.25,  228.25, 7.0,   5,  12,  "E",   0},   // CCIR I/III (5-12)
   {471.25,  859.25, 8.0,  21,  69,   "",   0},   // UHF (21-69)
   {105.25,  172.25, 7.0,  71,  80, "SE",  70},   // CCIR SL/SH (S1-10)
   {231.25,  298.25, 7.0,  81,  90, "SE",  70},   // CCIR SL/SH (S11-20)
   {303.25,  467.25, 8.0,  91, 111,  "S",  70},   // CCIR H (S21-41)
   { 69.25,  101.25, 7.0, 112, 116, "S0", 111},   // CCIR I/III (S42-46, Belgium only)
   {     0,       0, 0.0,   0,   0,   "",   0}
};

// channel table for France
const FREQ_TABLE freqTableFrance[] =
{  // must be sorted by channel numbers
   { 47.75,   51.75, 0.0,   1,   1,  "K",   0},   // K1
   { 55.75,   58.50, 0.0,   2,   2,  "K",   0},   // K2
   { 60.50,   62.25, 0.0,   3,   3,  "K",   0},   // K3
   { 63.75,   69.50, 0.0,   4,   4,  "K",   0},   // K4
   {176.00,  220.00, 8.0,   5,  10,  "K",   0},   // K5 - K10
   {471.25,  859.25, 8.0,  21,  69,   "",   0},   // UHF (21-69)
   {303.25,  451.25, 8.0,  71,  89,  "H",  70},   // H01 - H19
   {116.75,  302.75,12.0, 102, 117,  "K",  91},   // KB - KQ
   {     0,       0, 0.0,   0,   0,   "",   0}
};

const FREQ_TABLE * const freqTableList[] =
{
   freqTableEurope,
   freqTableFrance
};


// index of the frequency table currently in use
static uint freqTabIdx = 0;

// ---------------------------------------------------------------------------
// Converts channel name back to frequency
//
ulong TvChannels_NameToFreq( const char * pName )
{
   const FREQ_TABLE *ft;
   char * pEnd;
   ulong channel;

   ft = freqTableList[freqTabIdx];
   while (ft->firstChannel > 0)
   {
      if ((*ft->prefix == 0) || (strncmp(pName, ft->prefix, strlen(ft->prefix)) == 0))
      {
         channel = strtol(pName + strlen(ft->prefix), &pEnd, 10);
         if (*pEnd == 0)
         {
            channel += ft->idxOffset;
            if ((channel >= ft->firstChannel) && (channel <= ft->lastChannel))
            {
               return (ulong)(16.0 * (ft->freqStart + ft->freqOffset * (channel - ft->firstChannel)));
            }
         }
      }
      ft += 1;
   }
   return 0;
}

// ---------------------------------------------------------------------------
// Builds the name for a given channel number
//
void TvChannels_GetName( uint channel, uchar * pName, uint maxNameLen )
{
   const uchar *prefix;
   const FREQ_TABLE *ft;
   uchar buf[20];

   prefix = "";
   ft = freqTableList[freqTabIdx];
   while (ft->firstChannel > 0)
   {
      if (channel <= ft->lastChannel)
      {
         if (channel >= ft->firstChannel)
         {
            prefix   = ft->prefix;
            channel -= ft->idxOffset;
         }
         else
            prefix = "?";
         break;
      }
      ft += 1;
   }

   sprintf(buf, "%s%d", prefix, channel);
   strncpy(pName, buf, maxNameLen);
}

// ---------------------------------------------------------------------------
// Get number of channels in table (for EPG scan progress bar)
//
int TvChannels_GetCount( void )
{
   const FREQ_TABLE *ft;
   int count;

   count = 0;
   ft = freqTableList[freqTabIdx];
   while (ft->firstChannel > 0)
   {
      count += ft->lastChannel - ft->firstChannel + 1;
      ft += 1;
   }
   return count;
}

// ---------------------------------------------------------------------------
// Get the next channel and frequency from the table
//
bool TvChannels_GetNext( uint *pChan, ulong *pFreq )
{
   const FREQ_TABLE *ft;

   ft = freqTableList[freqTabIdx];

   if (*pChan == 0)
      *pChan = ft->firstChannel;
   else
      *pChan = *pChan + 1;

   // search the channel in the channel bands table
   while (ft->firstChannel > 0)
   {
      // assume that the table is sorted by channel numbers
      if (*pChan <= ft->lastChannel)
      {
         // skip possible channel gap between bands
         if (*pChan < ft->firstChannel)
            *pChan = ft->firstChannel;

         // get the frequency of this channel
         *pFreq = (ulong) (16.0 * (ft->freqStart + (*pChan - ft->firstChannel) * ft->freqOffset));
         break;
      }
      ft += 1;
   }

   return (ft->firstChannel > 0);
}

// ---------------------------------------------------------------------------
// Select the channel table
//
void TvChannels_SelectFreqTable( uint tableIdx )
{
   if (tableIdx <= 1)
      freqTabIdx = tableIdx;
   else
      debug1("TvChannels-SelectFreqTable: illegal index %d - ignored", tableIdx);
}

