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
 *  $Id: tvchan.c,v 1.10 2020/06/17 19:59:13 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/btdrv.h"
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
   const char * prefix;    // prefix for the channel name
   uint    idxOffset;      // start index of a band namespace
   uint    wdmBaseIdx;     // start index in WDM channel table
} FREQ_TABLE;


// channel table for Western Europe
const FREQ_TABLE freqTableEurope[] =
{  // must be sorted by channel numbers
   { 48.25,   66.25, 7.0,   2,   4,  "E",   0,  1},   // CCIR I/III (2-4)
   {175.25,  228.25, 7.0,   5,  12,  "E",   0, 20},   // CCIR I/III (5-12)
   {471.25,  859.25, 8.0,  21,  69,   "",   0, 59},   // UHF (21-69)
   {105.25,  172.25, 7.0,  71,  80, "SE",  70, 10},   // CCIR SL/SH (S1-10)
   {231.25,  298.25, 7.0,  81,  90, "SE",  70, 28},   // CCIR SL/SH (S11-20)
   {303.25,  467.25, 8.0,  91, 111,  "S",  70, 38},   // CCIR H (S21-41)
   { 69.25,  101.25, 7.0, 112, 116, "S0", 111,  5},   // CCIR I/III (S42-46, Belgium only)
   {     0,       0, 0.0,   0,   0,   "",   0,  0}
};

// channel table for France
const FREQ_TABLE freqTableFrance[] =
{  // must be sorted by channel numbers
   { 47.75,   51.75, 8.0,   1,   1,  "K",   0,  1},   // K1
   { 55.75,   58.50, 8.0,   2,   2,  "K",   0,  2},   // K2
   { 60.50,   62.25, 8.0,   3,   3,  "K",   0,  3},   // K3
   { 63.75,   69.50, 8.0,   4,   4,  "K",   0,  4},   // K4
   {176.00,  220.00, 8.0,   5,  10,  "K",   0,  5},   // K5 - K10
   {471.25,  859.25, 8.0,  21,  69,   "",   0, 21},   // UHF (21-69)
#if 0
// XXX FIXME: there's an alternate, imcompatible channel table for cable
   {303.25,  451.25, 8.0,  71,  89,  "H",  70,  0},   // H01 - H19
   {116.75,  302.75,12.0, 102, 117,  "K",  91,  0},   // KB - KQ
#endif
   {     0,       0, 0.0,   0,   0,   "",   0,  0}
};

const FREQ_TABLE * const freqTableList[] =
{
   freqTableEurope,
   freqTableFrance
};

typedef enum
{
   FREQ_TAB_D_A_CH,
   FREQ_TAB_FRANCE,
   FREQ_TAB_COUNT
} TVCHAN_FREQ_TAB_IDX;

// index of the frequency table currently in use
static TVCHAN_FREQ_TAB_IDX freqTabIdx = 0;

// ---------------------------------------------------------------------------
// Converts channel name back to frequency
//
uint TvChannels_NameToFreq( const char * pName )
{
   const FREQ_TABLE *ft;
   char * pEnd;
   TVCHAN_FREQ_TAB_IDX tabIdx;
   uint  channel;

   for (tabIdx=0; tabIdx < FREQ_TAB_COUNT; tabIdx++)
   {
      ft = freqTableList[tabIdx];

      while (ft->firstChannel > 0)
      {
         if ((*ft->prefix == 0) || (strncmp(pName, ft->prefix, strlen(ft->prefix)) == 0))
         {
            channel = (uint) strtol(pName + strlen(ft->prefix), &pEnd, 10);
            if (*pEnd == 0)
            {
               channel += ft->idxOffset;
               if ((channel >= ft->firstChannel) && (channel <= ft->lastChannel))
               {
                  return (uint)(16.0 * (ft->freqStart + ft->freqOffset * (channel - ft->firstChannel)));
               }
            }
         }
         ft += 1;
      }
   }
   debug1("TvChannels-NameToFreq: unknown channel ID \"%s\"", pName);
   return 0;
}

// ---------------------------------------------------------------------------
// Builds the name for a given channel number
//
void TvChannels_GetName( uint channel, char * pName, uint maxNameLen )
{
   const char *prefix;
   const FREQ_TABLE *ft;
   char buf[20];

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

#ifdef WIN32
// ---------------------------------------------------------------------------
// Returns index into M$ WDM channel table for a given frequency
// - norm is used as a hint which country table to use for mapping
//   XXX FIXME: could try both tables and use the channel which matches the given frequency best
// - also returns country code for the used channel table; note M$ also differs
//   between cable and broadcast tables, but we always indicate cable since
//   broadcast is just a subset
// - return -1 in case of error (i.e. freq outside of all bands)
//
int TvChannels_FreqToWdmChannel( uint ifreq, uint norm, uint * pCountry )
{
   const FREQ_TABLE *ft;
   double dfreq;
   int    wdmIdx = -1;

   if (norm == VIDEO_MODE_SECAM)
   {
      ft = freqTableList[FREQ_TAB_FRANCE];
      *pCountry = 33;
   }
   else // (norm == VIDEO_MODE_PAL)
   {
      ft = freqTableList[FREQ_TAB_D_A_CH];
      *pCountry = 49;
   }
   assert((ifreq >> 24) == 0);  // no norm must be encoded in upper bits
   dfreq = (double)ifreq / 16.0;

   // search the channel in the channel bands table
   while (ft->firstChannel > 0)
   {
      // assume that the table is sorted by channel numbers
      if ( (dfreq >= ft->freqStart - ft->freqOffset/2) &&
           (dfreq <= ft->freqMax) )
      {
         wdmIdx = ft->wdmBaseIdx
                + (int)((dfreq - ft->freqStart + ft->freqOffset/2) / ft->freqOffset );

         if ( ((wdmIdx == 2) || (wdmIdx == 3)) && (*pCountry == 49) )
         {  // skip "E2A" (49.75) which is not covered by our table
            wdmIdx += 1;
         }
         break;
      }
      ft += 1;
   }

   return wdmIdx;
}
#endif // WIN32

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
bool TvChannels_GetNext( uint *pChan, uint *pFreq )
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
         *pFreq = (uint) (16.0 * (ft->freqStart + (*pChan - ft->firstChannel) * ft->freqOffset));
         if (freqTabIdx == FREQ_TAB_FRANCE)
            *pFreq |= (VIDEO_MODE_SECAM << 24);
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
   if (tableIdx < FREQ_TAB_COUNT)
      freqTabIdx = tableIdx;
   else
      debug1("TvChannels-SelectFreqTable: illegal index %d - ignored", tableIdx);
}

#if 0
// ---------------------------------------------------------------------------
// Debugging only: test WDM channel conversion functions
//
int main(int argc, char **argv)
{
   char * pEnd;
   char buf[10];
   uint ftableIdx;
   uint chnIdx;
   sint wdmChannel;
   uint freq;
   uint norm;
   uint country;
   double ffreq;

   if (argc == 2)
   {
      ffreq = strtod(argv[1], &pEnd);
      if (*pEnd == 0)
      {
         freq = ffreq * 16;
         norm = VIDEO_MODE_SECAM;
         wdmChannel = TvChannels_FreqToWdmChannel(freq, norm, &country);

         printf("%.2f -> channel #%d, country %d\n", (freq & 0xffffff)/16.0, wdmChannel, country);
      }
      else
         fprintf(stderr, "Parse error: not a number: %s\n", argv[1]);
   }
   else
   {
      for (ftableIdx = 0; ftableIdx < FREQ_TAB_COUNT; ftableIdx++)
      {
         TvChannels_SelectFreqTable(ftableIdx);

         chnIdx = 0;
         while (TvChannels_GetNext(&chnIdx, &freq))
         {
            TvChannels_GetName(chnIdx, buf, sizeof(buf));

            //freq += 7*16.16*rand()/(RAND_MAX+1.0) - 7*16/2;
            wdmChannel = TvChannels_FreqToWdmChannel(freq & 0xffffff, freq >> 24, &country);
            if (wdmChannel != -1)
               printf("%d: %s %.2f -> channel #%d, country %d\n", chnIdx, buf, (freq & 0xffffff)/16.0, wdmChannel, country);
            else
               printf("%d: %s %.2f -> ERROR\n", chnIdx, buf, (freq & 0xffffff)/16.0);
         }
      }
   }

   return 0;
}
#endif

