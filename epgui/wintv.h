/*
 *  Nextview browser: M$ Windows TV application remote control module
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
 *  $Id: wintv.h,v 1.5 2004/06/19 19:07:10 tom Exp tom $
 */

#ifndef __WINTV_H
#define __WINTV_H


// ----------------------------------------------------------------------------
// Initialization
//
void Wintv_Init( bool enable );
void Wintv_Destroy( void );

void Wintv_SendCmdArgv(Tcl_Interp *interp, const char * pCmdStr, uint strLen );


#endif  // __WINTV_H
