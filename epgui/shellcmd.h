/*
 *  Nextview GUI: Output of PI data in various formats
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
 *  $Id: shellcmd.h,v 1.1 2003/02/26 22:00:04 tom Exp tom $
 */

#ifndef __SHELLCMD_H
#define __SHELLCMD_H


// ----------------------------------------------------------------------------
// Interface functions declaration

void ShellCmd_Init( void );
void ShellCmd_Destroy( void );

void ShellCmd_CtxMenuAddUserDef( const char * pMenu, bool addSeparator );

#endif  // __SHELLCMD_H
