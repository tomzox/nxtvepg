/*
 *  Hamming and Parity decoder
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
 *    Decodes Hamming and Parity error protection.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: hamming.c,v 1.2 2000/06/01 19:42:30 tom Exp tom $
 */

#define __HAMMING_C

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/hamming.h"


// ---------------------------------------------------------------------------
// Decode 2 Hamming nibbles to 1 byte
// XXX Warning: not multi-thread safe
//
uchar UnHam84Byte( const uchar *d )
{
   uchar c1, c2;
  
   c1 = unhamtab[d[0]];
   c2 = unhamtab[d[1]];

   if ((c1|c2) & 0x40)
   {  // double error - reject data bits
      //if (!hamErr)
      //   fprintf(stderr, "##### hamming error #####\n");
      hamErr++;
      return 0;
   }
   else
      return (c2<<4)|(0x0f&c1);
}

// ---------------------------------------------------------------------------
// Decode 4 Hamming nibbles to 1 16-bit integer
// XXX Warning: not multi-thread safe
//
uint UnHam84Word( const uchar *d )
{
   uchar c1,c2,c3,c4;
  
   c1 = unhamtab[d[0]];
   c2 = unhamtab[d[1]];
   c3 = unhamtab[d[2]];
   c4 = unhamtab[d[3]];

   if ((c1|c2|c3|c4) & 0x40)
   {  // double error - reject data bits
      //if (!hamErr)
      //   fprintf(stderr, "##### hamming error #####\n");
      hamErr++;
      return 0;
   }
   else
      return (c4<<12)|((c3&0x0f)<<8)|((c2&0x0f)<<4)|(c1&0x0f);
}

// ---------------------------------------------------------------------------
// Decode 1 Hamming nibble to 1 byte
// XXX Warning: not multi-thread safe
//
uchar UnHam84Nibble( const uchar *d )
{
   uchar c = unhamtab[*d];

   if (c & 0x40)
   { // double error - reject data bits
      //if (!hamErr)
      //   fprintf(stderr, "##### hamming error #####\n");
      hamErr++;
      return 0;
   }
   else
      return c & 0x0f;
}

// ---------------------------------------------------------------------------
// unham a series of 8/4 Hamming encoded bytes in-place
// XXX Warning: not multi-thread safe
//
void UnHam84Array( uchar *pin, uint byteCount )
{
   uchar c1, c2;
   uchar *pout = pin;

   for (; byteCount>0; byteCount--)
   {
      c1 = unhamtab[pin[0]];
      c2 = unhamtab[pin[1]];

      if ((c1|c2) & 0x40)
      {  // double error - reject data bits
         //if (!hamErr)
         //   fprintf(stderr, "##### hamming error #####\n");
        hamErr++;
      }
      else
         *pout = (c2<<4)|(0x0f&c1);

      pin  += 2;
      pout += 1;
   }
}

// ---------------------------------------------------------------------------
// resolve parity on an array in-place - errors are ignored
//
void UnHamParityArray( const uchar *pin, uchar *pout, uint byteCount )
{
   for (; byteCount > 0; byteCount--)
   {
     *pout = parityTab[*pin] & 0x7f;
     //if (parityTab[*pin] & 0x80)
     //   fprintf(stderr, "Parity=%x error\n",*pin);
     pin  += 1;
     pout += 1;
   }
}

