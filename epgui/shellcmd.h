/*
 *  Nextview GUI: Output of PI data in various formats
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

#ifndef __SHELLCMD_H
#define __SHELLCMD_H


// ----------------------------------------------------------------------------
// Interface functions declaration

void ShellCmd_Init( void );
void ShellCmd_Destroy( void );

uint ShellCmd_CtxMenuAddUserDef( Tcl_Interp *interp, const char * pMenu, bool addSeparator );
Tcl_Obj * PiOutput_ParseScript( Tcl_Interp *interp, Tcl_Obj * pCmdObj,
                                const PI_BLOCK * pPiBlock );
void PiOutput_ExecuteScript( Tcl_Interp *interp, Tcl_Obj * pCmdObj, const PI_BLOCK * pPiBlock );

#endif  // __SHELLCMD_H
