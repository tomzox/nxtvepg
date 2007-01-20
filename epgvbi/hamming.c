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
 *  Author: Tom Zoerner
 *
 *  $Id: hamming.c,v 1.12 2006/07/02 12:03:35 tom Exp $
 */

#define __HAMMING_C

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"     // for define DUMP_TTX_PACKETS
#include "epgvbi/hamming.h"


// ----------------------------------------------------------------------------
// Decode one nibble
//
/*
uchar UnHam84Nibble( const uchar * d )
{
   schar c = (schar) unhamtab[*d];

   if (c >= 0)
      return c;

   hamErr++;
   return 0;
}
*/

// ----------------------------------------------------------------------------
// Decode 2 nibbles to one byte
//
/*
uchar UnHam84Byte( const uchar * d )
{
   schar c1, c2;
  
   c1 = (schar) unhamtab[*(d++)];
   if (c1 >= 0)
   {
      c2 = (schar) unhamtab[*d];
      if (c2 >= 0)
      {
         return ((c2<<4) | c1);
      }
   }

   hamErr++;
   return 0;
}
*/

// ----------------------------------------------------------------------------
// Decode 4 nibbles to one 16-bit word
//
/*
uint UnHam84Word( const uchar * d )
{
   schar c1,c2,c3,c4;
  
   if ( ((c1 = (schar) unhamtab[*(d++)]) >= 0) &&
        ((c2 = (schar) unhamtab[*(d++)]) >= 0) &&
        ((c3 = (schar) unhamtab[*(d++)]) >= 0) &&
        ((c4 = (schar) unhamtab[* d   ]) >= 0) )
   {
      return (c4<<12) | (c3<<8) | (c2<<4) | c1;
   }
   else
   {
      hamErr++;
      return 0;
   }
}
*/

// ----------------------------------------------------------------------------
// Decode a series of Hamming-8/4 encoded bytes
// - aborts and returns FALSE upon non-recoverable errors
//
bool UnHam84Array( uchar * pin, uchar * pout, uint byteCount )
{
   schar c1, c2;
   bool result = TRUE;

   for (; byteCount > 0; byteCount--)
   {
      if ( ((c1 = (schar) unhamtab[*(pin++)]) >= 0) &&
           ((c2 = (schar) unhamtab[*(pin++)]) >= 0) )
      {
         *(pout++) = (c2<<4) | c1;
      }
      else
      {
        result = FALSE;
        break;
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Decode a parity-encoded string
// - characters with parity errors are replaced with a blank char (0xA0)
// - returns number of parity errors in the string
//
ushort UnHamParityArray( const uchar *pin, uchar *pout, uint byteCount )
{
   ushort errCount;
   schar c1;

   errCount = 0;
   for (; byteCount > 0; byteCount--)
   {
      c1 = (schar)parityTab[*(pin++)];
      if (c1 >= 0)
      {
         *(pout++) = c1;
      }
      else
      {
         *(pout++) = 0xA0;  // Latin-1 space character
         errCount += 1;
      }
   }

   return errCount;
}

// ----------------------------------------------------------------------------
// Print data of a teletext data line
//
#if DUMP_TTX_PACKETS == ON
void DebugDumpTeletextPkg( const uchar * pHead, const uchar * pData, uint frameSeqNo,
                           uint lineNo, uint pkgNo, uint pageNo, uint subPageNo, bool isEpgPage )
{
   uchar   tmparr[46];
   uchar * pTmp;
   ushort  errCount;
   uint    byteCount;
   schar   c1;

   // undo parity bit and mask out special characters
   pTmp = tmparr;
   errCount = 0;
   for (byteCount = 42; byteCount > 0; byteCount--)
   {
      c1 = (schar)parityTab[*(pData++)];
      if ( (c1 >= 0) &&
           ((c1 > 0x20) && (c1 < 0x7f)) )
      {
         *(pTmp++) = c1;
      }
      else
      {
         *(pTmp++) = 0xA0;  // Latin-1 space character
         errCount += 1;
      }
   }

   if (pkgNo != 0)
   {
      tmparr[40] = 0;
      printf("%s: frame %d line %3d pkg %2d: '%s' (%d)\n",
              pHead, frameSeqNo, lineNo, pkgNo, &tmparr[0], isEpgPage);
   }
   else
   {
      tmparr[40-8] = 0;
      printf("%s: frame %d line %3d page %03X.%04X pkg %02d: '%s' (%d)\n",
              pHead, frameSeqNo, lineNo, pageNo, subPageNo, pkgNo, &tmparr[8], isEpgPage);
   }
}
#endif  // DUMP_TTX_PACKETS == ON

