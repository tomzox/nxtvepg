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
 *  Description:
 *
 *    Platform and compiler independent type definitions
 *    and other global definitions.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: mytypes.h,v 1.3 2000/06/01 19:45:08 tom Exp tom $
 */

#include <sys/types.h>

#ifndef __MYTYPES_H
#define __MYTYPES_H
typedef unsigned char uchar;
typedef   signed int  sint;
typedef   signed long slong;
typedef unsigned char bool;
#ifdef WIN32
typedef unsigned int  uint;
typedef unsigned long ulong;
#endif

#define FALSE 0
#define TRUE  1

// definitions for compile-time pre-processor switches
#define OFF FALSE
#define ON  TRUE

// en-/disable debugging
#define HALT_ON_FAILED_ASSERTION OFF
#define DEBUG_GLOBAL_SWITCH      OFF
#define DEBUG_SWITCH_EPGDB       ON
#define DEBUG_SWITCH_EPGCTL      ON
#define DEBUG_SWITCH_EPGUI       ON

#endif // __MYTYPES_H
