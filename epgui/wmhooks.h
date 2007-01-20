/*
 *  X11 and Win32 window manager hooks
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
 *  $Id: wmhooks.h,v 1.2 2006/12/22 15:23:37 tom Exp tom $
 */

#ifndef __WMHOOKS_H
#define __WMHOOKS_H


void WmHooks_Init( Tcl_Interp * interp );
void WmHooks_Destroy( void );


#endif  // __WMHOOKS_H
