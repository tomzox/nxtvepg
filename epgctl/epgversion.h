/*
 *  Software version of the Nextview decoder
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
 *  $Id: epgversion.h,v 1.12 2001/06/05 19:04:37 tom Exp tom $
 */


#ifndef __EPGVERSION_H
#define __EPGVERSION_H


#define EPG_VERSION_MAJOR   0   // major revision
#define EPG_VERSION_MINOR   5   // minor revision
#define EPG_VERSION_PL      1   // bugfix revision / patch level

#ifndef WIN32
#define EPG_VERSION_STR     "0.5.1"
#else
#define EPG_VERSION_STR     "1.7-win"
#endif


// RCS id to be included to the object code for ident(1)
#define EPG_VERSION_RCS_ID  "$Id: epgversion.h,v 1.12 2001/06/05 19:04:37 tom Exp tom $" "$Compiledate: " __DATE__ " " __TIME__" $";

// version in integer format for internal purposes
#define EPG_VERSION_TO_INT(MAJ,MIN,PL) (((MAJ)<<16) | ((MIN)<<8) | (PL))
#define EPG_VERSION_NO      EPG_VERSION_TO_INT(EPG_VERSION_MAJOR, EPG_VERSION_MINOR, EPG_VERSION_PL)


#endif  // __EPGVERSION_H
