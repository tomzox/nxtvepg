/*
 *  Build a system error message
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
 *  $Id: syserrmsg.h,v 1.1 2002/11/10 19:49:31 tom Exp tom $
 */

#ifndef __SYSERRMSG_H
#define __SYSERRMSG_H

void SystemErrorMessage_Set( char ** ppErrorText, int errCode, const char * pText, ... );

#endif  // __SYSERRMSG_H