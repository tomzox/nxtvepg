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
 *  $Id: hamming.c,v 1.8 2002/05/12 20:19:13 tom Exp tom $
 */

#define __HAMMING_C

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

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
// Unham a series of 8/4 encoded bytes in-place
// - aborts and returns FALSE upon errors
//
bool UnHam84Array( uchar * pin, uint byteCount )
{
   schar c1, c2;
   uchar * pout = pin;
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
// Resolve parity on an array in-place
// - errors are ignored, the character just replaced by a blank
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
         *(pout++) = ' ';
         errCount += 1;
      }
   }

   return errCount;
}

