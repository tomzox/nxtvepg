/*
 *  Win32 VBI capture driver for the Conexant 23881 chip (aka Bt881)
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
 *  $Id: cx2388x.h,v 1.1 2003/01/19 08:16:37 tom Exp tom $
 */

#ifndef __CX2388X_H
#define __CX2388X_H

// ---------------------------------------------------------------------------
// Interface declaration
//
void Cx2388x_GetInterface( TVCARD * pTvCard );

#endif  // __CX2388X_H
