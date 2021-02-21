/*
 *  Nextview browser: M$ Windows TV application remote control module
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
 *  Description: see C source file.
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
