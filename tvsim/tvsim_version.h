/*
 *  Software version of the TV application interaction debugging tools
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *    Defines the software version in various formats.
 */

#ifndef __TVSIM_VERSION_H
#define __TVSIM_VERSION_H


#define TVSIM_VERSION_MAJOR   3   // major revision
#define TVSIM_VERSION_MINOR   1   // minor revision

#define TVSIM_VERSION_STR     "3.1"


// RCS id to be included to the object code for ident(1)
#define TVSIM_VERSION_RCS_ID  "$Compiledate: " __DATE__ " " __TIME__" $";

// version in integer format for internal purposes
#define TVSIM_VERSION_TO_INT(MAJ,MIN) (((MAJ)<<8) | (MIN))
#define TVSIM_VERSION_NO      EPG_VERSION_TO_INT(TVSIM_VERSION_MAJOR, TVSIM_VERSION_MINOR)


#endif  // __TVSIM_VERSION_H
