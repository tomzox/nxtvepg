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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: vbidecode.c,v 1.23 2000/12/09 16:50:11 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbacq.h"
#include "epgvbi/hamming.h"
#include "epgvbi/vbidecode.h"

// use fixpoint arithmetic for scanning steps
#define FPSHIFT 16
#define FPFAC (1<<FPSHIFT)

// use standard frequency for Bt848 and PAL
// (no Nextview exists in any NTSC countries)
#define VTSTEP  ((int)((35.468950/6.9375)*FPFAC+0.5))
#define VPSSTEP ((int)(7.1 * FPFAC + 0.5))


// ---------------------------------------------------------------------------
// Decode teletext packet header
// - all packets are forwarded to the ring buffer
//
static void VbiDecodePacket(const uchar * data)
{
   sint tmp1, tmp2, tmp3;
   uchar mag, pkgno;
   uint page;
   uint sub;

   if (UnHam84Byte(data, &tmp1))
   {
      mag = tmp1 & 7;
      pkgno = (tmp1 >> 3) & 0x1f;

      if (pkgno == 0)
      {  // this is a page header - start of a new page
         if (UnHam84Byte(data + 2, &tmp1) &&
             UnHam84Byte(data + 4, &tmp2) &&
             UnHam84Byte(data + 6, &tmp3))
         {
            page = tmp1 | (mag << 8);
            sub = (tmp2 | (tmp3 << 8)) & 0x3f7f;
            //printf("**** page=%03x.%04X\n", page, sub);

            EpgDbAcqAddPacket(page, sub, 0, data + 2);
         }
         //else debug0("page number or subcode hamming error - skipping page");
      }
      else
      {
         EpgDbAcqAddPacket((uint) mag << 8, 0, pkgno, data + 2);
         //printf("**** pkgno=%d\n", pkgno);
      }
   }
   //else debug0("packet header decoding error - skipping");
}

// ---------------------------------------------------------------------------
// Get one byte from the analog VBI data line
//
static uchar vtscan(const uchar * lbuf, ulong * spos, int off)
{
   int j;
   uchar theByte;

   theByte = 0;
   for (j = 7; j >= 0; j--, *spos += VTSTEP)
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
   for (j = 7; j >= 0; j--, *spos += VPSSTEP)
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

   /* automatic gain control */
   for (i = 120; i < 450; i++)
   {
      if (lbuf[i] < min)
         min = lbuf[i];
      if (lbuf[i] > max)
         max = lbuf[i];
   }
   thresh = (max + min) / 2;
   off = 128 - thresh;

   // search for first 1 bit (VT always starts with 55 55 27)
   p = 50;
   while ((lbuf[p] < thresh) && (p < 350))
      p++;
   // search for maximum of 1st peak
   while ((lbuf[p + 1] >= lbuf[p]) && (p < 350))
      p++;
   spos = dpos = (p << FPSHIFT);

   /* ignore first bit for now */
   data[0] = vtscan(lbuf, &spos, off);

   if ((data[0] & 0xfe) == 0x54)
   {
      data[1] = vtscan(lbuf, &spos, off);
      switch (data[1])
      {
         case 0x75:             /* missed first two 1-bits, TZ991230++ */
            //printf("****** step back by 2\n");
            spos -= 2 * VTSTEP;
            data[1] = 0xd5;
         case 0xd5:             /* oops, missed first 1-bit: backup 2 bits */
            //printf("****** step back by 1\n");
            spos -= 2 * VTSTEP;
            data[1] = 0x55;
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
                  VbiDecodePacket(data + 3);
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
   else if ((line == 9) && doVps)
   {
      if ( (vps_scan(lbuf, &dpos, off) == 0x55) &&       // VPS run in
           (vps_scan(lbuf, &dpos, off) == 0x55) &&
           (vps_scan(lbuf, &dpos, off) == 0x51) &&       // VPS start code
           (vps_scan(lbuf, &dpos, off) == 0x99))
      {
         for (i = 3; i <= 14; i++)
         {
            int bit, j;
            data[i] = 0;
            for (j = 0; j < 8; j++, dpos += VPSSTEP * 2)
            {  // decode bi-phase data bit: 1='10', 0='01'

               bit = (lbuf[dpos >> FPSHIFT] + off) & 0x80;
               if (bit == ((lbuf[(dpos + VPSSTEP) >> FPSHIFT] + off) & 0x80))
                  break;  // bit error

               data[i] |= bit >> j;
            }
            if (j < 8)
               break;  // was error

         }

         if (i > 14)
         {
            uint cni = ((data[13] & 0x3) << 10) | ((data[14] & 0xc0) << 2) |
            ((data[11] & 0xc0)) | (data[14] & 0x3f);
            if ((cni != 0) && ((cni & 0xfff) != 0xfff))
            {
               EpgDbAcqAddVpsCode(cni);
            }
            //printf("VPS line %d: CNI=0x%04x\n", line, cni);
         }
      }
      //else
      //{dpos = spos; printf("VPS line %d=%02x %02x %02x %02x\n", line, vps_scan(lbuf, &dpos, off), vps_scan(lbuf, &dpos, off), vps_scan(lbuf, &dpos, off), vps_scan(lbuf, &dpos, off));}
   }
   //else
   //printf("****** line=%d  [0]=%x != 0x54\n", line, data[0]);
}

