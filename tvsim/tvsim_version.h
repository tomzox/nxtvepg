/*
 *  Software version of the TV application interaction debugging tools
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: tvsim_version.h,v 1.4 2003/02/08 14:28:24 tom Exp tom $
 */

#ifndef __TVSIM_VERSION_H
#define __TVSIM_VERSION_H


#define TVSIM_VERSION_MAJOR   1   // major revision
#define TVSIM_VERSION_MINOR  11   // minor revision

#define TVSIM_VERSION_STR     "1.11"


// RCS id to be included to the object code for ident(1)
#define TVSIM_VERSION_RCS_ID  "$Id: tvsim_version.h,v 1.4 2003/02/08 14:28:24 tom Exp tom $" "$Compiledate: " __DATE__ " " __TIME__" $";

// version in integer format for internal purposes
#define TVSIM_VERSION_TO_INT(MAJ,MIN) (((MAJ)<<8) | (MIN))
#define TVSIM_VERSION_NO      EPG_VERSION_TO_INT(TVSIM_VERSION_MAJOR, TVSIM_VERSION_MINOR)


#endif  // __TVSIM_VERSION_H