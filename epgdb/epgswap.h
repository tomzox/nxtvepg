/*
 *  Byte-swapping utilities for endian conversion
 *
 *  Copied from utils.h, which is part of the GNU C Library.
 *  Copyright (C) 1992, 1996, 1997, 2000 Free Software Foundation, Inc.
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
 *    Defines inline function to convert little endian 16- and 32-bit
 *    elements to big endian, and the other way around.  They are used
 *    when an endian wise not matching database is loaded or when
 *    connected to such an acquisition daemon.
 */

#ifndef __EPGSWAP_H
#define __EPGSWAP_H

static inline void swap16( void * pVal )
{
   uint val = * (ushort *) pVal;

   * (ushort *) pVal = ((((ushort)(val) & 0x00ffU) <<  8) |
                        (((ushort)(val) & 0xff00U) >>  8));
}

static inline void swap32( void * pVal )
{
   uint val = * (uint *) pVal;

   * (uint *) pVal = ((((uint)(val) & 0x000000ffU) << 24) |
                      (((uint)(val) & 0x0000ff00U) <<  8) |
                      (((uint)(val) & 0x00ff0000U) >>  8) |
                      (((uint)(val) & 0xff000000U) >> 24));
}

static inline void swap64( void * pVal )
{
   uint val1 = * ((uint *) pVal);
   uint val2 = * (((uint *) pVal) + 1);

   * ((uint *) pVal) = ((((uint)(val2) & 0x000000ffU) << 24) |
                        (((uint)(val2) & 0x0000ff00U) <<  8) |
                        (((uint)(val2) & 0x00ff0000U) >>  8) |
                        (((uint)(val2) & 0xff000000U) >> 24));

   * (((uint *) pVal) + 1) =
                       ((((uint)(val1) & 0x000000ffU) << 24) |
                        (((uint)(val1) & 0x0000ff00U) <<  8) |
                        (((uint)(val1) & 0x00ff0000U) >>  8) |
                        (((uint)(val1) & 0xff000000U) >> 24));
}

#endif // __EPGSWAP_H
