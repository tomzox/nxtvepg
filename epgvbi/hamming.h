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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: hamming.h,v 1.9 2004/02/14 19:17:12 tom Exp tom $
 */

#ifndef __HAMMING_H
#define __HAMMING_H

#ifdef __HAMMING_C
# define EXT
#else
# define EXT extern
#endif

// ----------------------------------------------------------------------------
// Hamming 8/4 decoding table
// - single errors are ignored
// - 0xff = double-error
//
EXT const uchar unhamtab[256]
#ifdef __HAMMING_C
= {
   0x01, 0xff, 0x01, 0x01, 0xff, 0x00, 0x01, 0xff,
   0xff, 0x02, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x07,
   0xff, 0x00, 0x01, 0xff, 0x00, 0x00, 0xff, 0x00,
   0x06, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x03, 0xff,
   0xff, 0x0c, 0x01, 0xff, 0x04, 0xff, 0xff, 0x07,
   0x06, 0xff, 0xff, 0x07, 0xff, 0x07, 0x07, 0x07,
   0x06, 0xff, 0xff, 0x05, 0xff, 0x00, 0x0d, 0xff,
   0x06, 0x06, 0x06, 0xff, 0x06, 0xff, 0xff, 0x07,
   0xff, 0x02, 0x01, 0xff, 0x04, 0xff, 0xff, 0x09,
   0x02, 0x02, 0xff, 0x02, 0xff, 0x02, 0x03, 0xff,
   0x08, 0xff, 0xff, 0x05, 0xff, 0x00, 0x03, 0xff,
   0xff, 0x02, 0x03, 0xff, 0x03, 0xff, 0x03, 0x03,
   0x04, 0xff, 0xff, 0x05, 0x04, 0x04, 0x04, 0xff,
   0xff, 0x02, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x07,
   0xff, 0x05, 0x05, 0x05, 0x04, 0xff, 0xff, 0x05,
   0x06, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x03, 0xff,
   0xff, 0x0c, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x09,
   0x0a, 0xff, 0xff, 0x0b, 0x0a, 0x0a, 0x0a, 0xff,
   0x08, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x0d, 0xff,
   0xff, 0x0b, 0x0b, 0x0b, 0x0a, 0xff, 0xff, 0x0b,
   0x0c, 0x0c, 0xff, 0x0c, 0xff, 0x0c, 0x0d, 0xff,
   0xff, 0x0c, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x07,
   0xff, 0x0c, 0x0d, 0xff, 0x0d, 0xff, 0x0d, 0x0d,
   0x06, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x0d, 0xff,
   0x08, 0xff, 0xff, 0x09, 0xff, 0x09, 0x09, 0x09,
   0xff, 0x02, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x09,
   0x08, 0x08, 0x08, 0xff, 0x08, 0xff, 0xff, 0x09,
   0x08, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x03, 0xff,
   0xff, 0x0c, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x09,
   0x0f, 0xff, 0x0f, 0x0f, 0xff, 0x0e, 0x0f, 0xff,
   0x08, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x0d, 0xff,
   0xff, 0x0e, 0x0f, 0xff, 0x0e, 0x0e, 0xff, 0x0e
}
#endif  //__HAMMING_C
;

// ----------------------------------------------------------------------------
// odd-parity decoding table: bit 7 set <-> parity error
//
EXT const uchar parityTab[256]
#ifdef __HAMMING_C
= {
   0x80, 0x01, 0x02, 0x83, 0x04, 0x85, 0x86, 0x07,
   0x08, 0x89, 0x8a, 0x0b, 0x8c, 0x0d, 0x0e, 0x8f,
   0x10, 0x91, 0x92, 0x13, 0x94, 0x15, 0x16, 0x97,
   0x98, 0x19, 0x1a, 0x9b, 0x1c, 0x9d, 0x9e, 0x1f,
   0x20, 0xa1, 0xa2, 0x23, 0xa4, 0x25, 0x26, 0xa7,
   0xa8, 0x29, 0x2a, 0xab, 0x2c, 0xad, 0xae, 0x2f,
   0xb0, 0x31, 0x32, 0xb3, 0x34, 0xb5, 0xb6, 0x37,
   0x38, 0xb9, 0xba, 0x3b, 0xbc, 0x3d, 0x3e, 0xbf,
   0x40, 0xc1, 0xc2, 0x43, 0xc4, 0x45, 0x46, 0xc7,
   0xc8, 0x49, 0x4a, 0xcb, 0x4c, 0xcd, 0xce, 0x4f,
   0xd0, 0x51, 0x52, 0xd3, 0x54, 0xd5, 0xd6, 0x57,
   0x58, 0xd9, 0xda, 0x5b, 0xdc, 0x5d, 0x5e, 0xdf,
   0xe0, 0x61, 0x62, 0xe3, 0x64, 0xe5, 0xe6, 0x67,
   0x68, 0xe9, 0xea, 0x6b, 0xec, 0x6d, 0x6e, 0xef,
   0x70, 0xf1, 0xf2, 0x73, 0xf4, 0x75, 0x76, 0xf7,
   0xf8, 0x79, 0x7a, 0xfb, 0x7c, 0xfd, 0xfe, 0x7f,
   0x00, 0x81, 0x82, 0x03, 0x84, 0x05, 0x06, 0x87,
   0x88, 0x09, 0x0a, 0x8b, 0x0c, 0x8d, 0x8e, 0x0f,
   0x90, 0x11, 0x12, 0x93, 0x14, 0x95, 0x96, 0x17,
   0x18, 0x99, 0x9a, 0x1b, 0x9c, 0x1d, 0x1e, 0x9f,
   0xa0, 0x21, 0x22, 0xa3, 0x24, 0xa5, 0xa6, 0x27,
   0x28, 0xa9, 0xaa, 0x2b, 0xac, 0x2d, 0x2e, 0xaf,
   0x30, 0xb1, 0xb2, 0x33, 0xb4, 0x35, 0x36, 0xb7,
   0xb8, 0x39, 0x3a, 0xbb, 0x3c, 0xbd, 0xbe, 0x3f,
   0xc0, 0x41, 0x42, 0xc3, 0x44, 0xc5, 0xc6, 0x47,
   0x48, 0xc9, 0xca, 0x4b, 0xcc, 0x4d, 0x4e, 0xcf,
   0x50, 0xd1, 0xd2, 0x53, 0xd4, 0x55, 0x56, 0xd7,
   0xd8, 0x59, 0x5a, 0xdb, 0x5c, 0xdd, 0xde, 0x5f,
   0x60, 0xe1, 0xe2, 0x63, 0xe4, 0x65, 0x66, 0xe7,
   0xe8, 0x69, 0x6a, 0xeb, 0x6c, 0xed, 0xee, 0x6f,
   0xf0, 0x71, 0x72, 0xf3, 0x74, 0xf5, 0xf6, 0x77,
   0x78, 0xf9, 0xfa, 0x7b, 0xfc, 0x7d, 0x7e, 0xff,
}
#endif  //__HAMMING_C
;

// ----------------------------------------------------------------------------
// Table to reverse order of bits in one "nibble" (i.e. 4-bit integer)
// - used to decode P8/30/2 (PDC) which has different bit order than teletext
//
EXT const uchar reverse4Bits[16]
#ifdef __HAMMING_C
= {
   0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e,
   0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f
}
#endif  //__HAMMING_C
;

// ----------------------------------------------------------------------------
// Table to count number of bits in a byte
// - used to calculate bit distance between two byte values (after XOR)
// - i.e. used to count number of differing/erronous bits in a value
//
EXT const uchar byteBitDistTable[256]
#ifdef __HAMMING_C
= {
   0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
   1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
   1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
   2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
   1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
   2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
   2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
   3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
   1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
   2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
   2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
   3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
   2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
   3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
   3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
   4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
}
#endif  //__HAMMING_C
;

#define ByteBitDistance(X,Y) (byteBitDistTable[(uchar)((X)^(Y))])

// ----------------------------------------------------------------------------
// declaration of service interface functions
//
#define UnHam84Nibble(P,V) (( *(V) = (schar)unhamtab[*((uchar *)(P))] ) >= 0 )
#define UnHam84Byte(P,V)   (( *(V) = ((sint)unhamtab[*((uchar *)(P))] | ((sint)unhamtab[*(((uchar *)(P))+1)] << 4)) ) >= 0 )

bool UnHam84Array( uchar *pin, uint byteCount );
ushort UnHamParityArray( const uchar *pin, uchar *pout, uint byteCount );

#undef EXT

#endif  // __HAMMING_H
