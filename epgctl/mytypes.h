/*
 *  General type definitions for the Nextview decoder
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
 *    Platform and compiler independent type definitions
 *    and global debug control.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: mytypes.h,v 1.15 2002/05/08 11:45:17 tom Exp tom $
 */

#ifndef __MYTYPES_H
#define __MYTYPES_H


// include declaration of exact-width types for inter-process communication
// (e.g. uint16_t must have exactly 16 bits - no more, no less - so the basic ANSI C
// integer types should not be used, because they are specified by minimum width only)
#ifndef _MSC_VER
#include <sys/types.h>
#ifndef WIN32
#include <inttypes.h>
#else
#include <stdint.h>
#endif
#else
// Microsoft Visual C doesn't fully comply to the C99 standard, so we define the types here
#include <windef.h>
typedef BYTE           uint8_t;
typedef WORD           uint16_t;
typedef DWORD          uint32_t;
#endif

// required basic types
typedef unsigned char  bool;     // >=1 bit unsigned
typedef   signed char  schar;    // 8 bit signed
typedef unsigned char  uchar;    // 8 bit unsigned
typedef   signed int   sint;     // >=32 bit signed
#ifdef WIN32
typedef unsigned short ushort;   // >=16 bit unsigned
typedef unsigned int   uint;     // >=32 bit unsigned
typedef unsigned long  ulong;    // >=32 bit unsigned
#endif
typedef   signed long  slong;    // >=32 bit signed

// boolean values
#define FALSE 0
#define TRUE  1

// definitions for compile-time pre-processor switches
#define OFF FALSE
#define ON  TRUE

// en-/disable debugging
#define HALT_ON_FAILED_ASSERTION OFF
#define DEBUG_GLOBAL_SWITCH      OFF
#define DEBUG_SWITCH_EPGDB       OFF
#define DEBUG_SWITCH_EPGCTL      OFF
#define DEBUG_SWITCH_EPGUI       OFF
#define DEBUG_SWITCH_VBI         OFF
#define DEBUG_SWITCH_TCL_BGERR   OFF
#define DEBUG_SWITCH_TVSIM       OFF
#define DEBUG_SWITCH_DSDRV       OFF

// enable memory leak detection for debugging
#define CHK_MALLOC               OFF

#endif // __MYTYPES_H
