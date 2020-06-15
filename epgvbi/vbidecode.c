/*
 *  Decode raw VBI lines to Teletext byte array or VPS CNI
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
 *    This module takes an array of VBI lines from each frame of
 *    the video image and decodes them according to the
 *    Enhanced Teletext specification (see ETS 300 706, available at
 *    http://www.etsi.org/). Result is 40 data bytes for each line.
 *
 *    The teletext scanner in this module is based on vbidecode.cc
 *    which is (was) part of the bttv driver package for Linux.
 *    Copyright (C) 1996,97 Ralph Metzler  (rjkm@thp.uni-koeln.de)
 *                        & Marcus Metzler (mocm@thp.uni-koeln.de)
 *
 *  Author: Tom Zoerner
 *
 *  $Id: vbidecode.c,v 1.39 2020/06/15 09:57:45 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/hamming.h"
#include "epgvbi/vbidecode.h"


// use fixpoint arithmetic for scanning steps
#define FPSHIFT 16
#define FPFAC (1<<FPSHIFT)

// use standard frequency for Bt848 and PAL
// (no Nextview exists in any NTSC countries)
#define BTTV_VT_RATE            35468950L
#define BTTV_PEAK_SEARCH_OFF    50
#define BTTV_START_LINE         7

// the SAA7134 chip has a different offset and sampling rate than the Bt8x8 chips
#define SAA7134_VT_RATE         (6750000L * 4)
#define SAA7134_PEAK_SEARCH_OFF 0
#define SAA7134_START_LINE      7

#define VT_RATE_CONV            6937500L
#define VPS_RATE_CONV           4995627L
#define AGC_START_OFF           120
#define AGC_LENGTH              300
#define PEAK_SEARCH_LEN         AGC_LENGTH

#define PEAK_SEARCH_OFF         0

static uint vtstep  = ((uint)((double)FPFAC * BTTV_VT_RATE / VT_RATE_CONV));
static uint vpsstep = ((uint)((double)FPFAC * BTTV_VT_RATE / VPS_RATE_CONV));
static uint vbiStartLine = BTTV_START_LINE;


//#define DUMP_VBI_LINE
//#define DUMP_VPS_LINE
#if defined(DUMP_VBI_LINE) || defined(DUMP_VPS_LINE)
// ---------------------------------------------------------------------------
// Debugging: dump the line as input to gnuplot
//
static void VbiDecodeDumpLine( int line, const uchar * lbuf, int thresh, int off )
{
   static int count = 0;
   uint idx;

   if (count++ < 100)
   {
      printf("# LINE %d - THRESH %d - OFF %d\n", line, thresh, off);
      for (idx=0; idx < 2048; idx++)
         printf("%d %d\n", idx, (int)*(lbuf++));
   }
}
#endif

// ---------------------------------------------------------------------------
// Set the raw sampling rate for VBI data
// - may differ between different hardware
//
void VbiDecodeSetSamplingRate( ulong sampling_rate, uint startLine )
{
   if (sampling_rate == 0)
   {  // set default sampling rate if driver doesn't support VBI format query
#ifdef SAA7134_0_2_2
      sampling_rate = SAA7134_VT_RATE;
      vbiStartLine  = SAA7134_START_LINE;
#else
      sampling_rate = BTTV_VT_RATE;
      vbiStartLine  = BTTV_START_LINE;
#endif
   }
   else
      vbiStartLine  = startLine;

   vtstep  = (uint) ((double)FPFAC * sampling_rate / VT_RATE_CONV);
   vpsstep = (uint) ((double)FPFAC * sampling_rate / VPS_RATE_CONV);

   dprintf4("startline=%d, rate=%ld -> vtstep=%.2f, vpsstep=%.2f\n", startLine, sampling_rate, (double)vtstep/FPFAC, (double)vpsstep/FPFAC);
}

// ---------------------------------------------------------------------------
// Start of VBI data for a new frame
// - just pass the call through to the ttx page decoder state machine
//
bool VbiDecodeStartNewFrame( uint frameSeqNo )
{
   return TtxDecode_NewVbiFrame(frameSeqNo);
}

// ---------------------------------------------------------------------------
// Get one byte from the analog VBI data line
//
static uchar vtscan(const uchar * lbuf, ulong * spos, int off)
{
   int j;
   uchar theByte;

   theByte = 0;
   for (j = 7; j >= 0; j--, *spos += vtstep)
   {
      theByte |= ((lbuf[*spos >> FPSHIFT] + off) & 0x80) >> j;
   }

   return theByte;
}

// ---------------------------------------------------------------------------
// Get one byte from the analog VPS data line
// - VPS uses a lower bit rate than teletext
//
static uchar vps_scan(const uchar * lbuf, ulong * spos, int off)
{
   int j;
   uchar theByte;

   theByte = 0;
   for (j = 7; j >= 0; j--, *spos += vpsstep)
   {
      theByte |= ((lbuf[*spos >> FPSHIFT] + off) & 0x80) >> j;
   }

   return theByte;
}

// ---------------------------------------------------------------------------
// Low level decoder of raw VBI data 
//
void VbiDecodeLine(const uchar * lbuf, int line, bool doVps)
{
   uchar data[45];
   int i, p;
   int thresh, off, min = 255, max = 0;
   ulong spos, dpos;

   line += vbiStartLine;

   /* automatic gain control */
   for (i = AGC_START_OFF; i < AGC_START_OFF + AGC_LENGTH; i++)
   {
      if (lbuf[i] < min)
         min = lbuf[i];
      if (lbuf[i] > max)
         max = lbuf[i];
   }
   thresh = (max + min) / 2;
   off = 128 - thresh;

   // search for first 1 bit (VT always starts with 55 55 27)
   p = PEAK_SEARCH_OFF;
   while ((lbuf[p] < thresh) && (p < PEAK_SEARCH_OFF + PEAK_SEARCH_LEN))
      p++;
   // search for maximum of 1st peak
   while ((lbuf[p + 1] >= lbuf[p]) && (p < PEAK_SEARCH_OFF + PEAK_SEARCH_LEN))
      p++;
   spos = dpos = (p << FPSHIFT);

#ifdef DUMP_VBI_LINE
   VbiDecodeDumpLine(line, lbuf, thresh, p);
#endif

   /* ignore first bit for now */
   data[0] = vtscan(lbuf, &spos, off);

   if ((data[0] & 0xfe) == 0x54)
   {
      data[1] = vtscan(lbuf, &spos, off);
      switch (data[1])
      {
         case 0x75:             /* missed first two 1-bits, TZ991230++ */
            //printf("****** step back by 2\n");
            spos -= 2 * vtstep;
            data[1] = 0xd5;
            /* fall-through */
         case 0xd5:             /* oops, missed first 1-bit: backup 2 bits */
            //printf("****** step back by 1\n");
            spos -= 2 * vtstep;
            data[1] = 0x55;
            /* fall-through */
         case 0x55:
            data[2] = vtscan(lbuf, &spos, off);
            switch (data[2])
            {
               case 0xd8:       /* this shows up on some channels?!?!? */
                  //for (i=3; i<45; i++) 
                  //  data[i]=vtscan(lbuf, &spos, off);
                  //return;
               case 0x27:
                  for (i = 3; i < 45; i++)
                     data[i] = vtscan(lbuf, &spos, off);
                  TtxDecode_AddPacket(data + 3, line);
                  break;
               default:
                  //printf("****** line=%d  [2]=%x != 0x27 && 0xd8\n", line, data[2]);
                  break;
            }
            break;
         default:
            //printf("****** line=%d  [1]=%x != 0x55 && 0xd5\n", line, data[1]);
            break;
      }
   }
   else if ((line == 16) && doVps)
   {
#ifdef DUMP_VPS_LINE
      VbiDecodeDumpLine(line, lbuf, thresh, p);
#endif
      if ( (vps_scan(lbuf, &dpos, off) == 0x55) &&       // VPS run in
           (vps_scan(lbuf, &dpos, off) == 0x55) &&
           (vps_scan(lbuf, &dpos, off) == 0x51) &&       // VPS start code
           (vps_scan(lbuf, &dpos, off) == 0x99))
      {
         for (i = 3; i <= 14; i++)
         {
            int bit, j;
            data[i] = 0;
            for (j = 0; j < 8; j++, dpos += vpsstep * 2)
            {  // decode bi-phase data bit: 1='10', 0='01'

               bit = (lbuf[dpos >> FPSHIFT] + off) & 0x80;
               if (bit == ((lbuf[(dpos + vpsstep) >> FPSHIFT] + off) & 0x80))
                  break;  // bit error

               data[i] |= bit >> j;
            }
            if (j < 8)
               break;  // was error
         }

         if (i > 14)
         {
            TtxDecode_AddVpsData(data + 3);
         }
      }
      //else
      //{dpos = spos; printf("VPS line %d=%02x %02x %02x %02x\n", line, vps_scan(lbuf, &dpos, off), vps_scan(lbuf, &dpos, off), vps_scan(lbuf, &dpos, off), vps_scan(lbuf, &dpos, off));}
   }
   //else
   //printf("****** line=%d  [0]=%x != 0x54\n", line, data[0]);
}

