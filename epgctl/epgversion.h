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
 *  $Id: epgversion.h,v 1.58 2004/09/04 21:22:16 tom Exp tom $
 */


#ifndef __EPGVERSION_H
#define __EPGVERSION_H


#define NXTVEPG_URL         "http://nxtvepg.sourceforge.net/"
#define NXTVEPG_MAILTO      "tomzo@nefkom.net"

#define EPG_VERSION_MAJOR   2   // major revision
#define EPG_VERSION_MINOR   7   // minor revision
#define EPG_VERSION_PL   0xB1   // bugfix revision / patch level

#define EPG_VERSION_STR     "2.7.3"


// RCS id to be included to the object code for ident(1)
#define EPG_VERSION_RCS_ID  "$Id: epgversion.h,v 1.58 2004/09/04 21:22:16 tom Exp tom $" "$Compiledate: " __DATE__ " " __TIME__" $";

// version in integer format for internal purposes
#define EPG_VERSION_TO_INT(MAJ,MIN,PL) (((MAJ)<<16) | ((MIN)<<8) | (PL))
#define EPG_VERSION_NO      EPG_VERSION_TO_INT(EPG_VERSION_MAJOR, EPG_VERSION_MINOR, EPG_VERSION_PL)


#endif  // __EPGVERSION_H
