/*
 *  Teletext decoder
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
 *  $Id: vbidecode.h,v 1.16 2002/04/29 19:03:49 tom Exp tom $
 */

#ifndef __VBIDECODE_H
#define __VBIDECODE_H


// ---------------------------------------------------------------------------
// Declaration of service interface functions
//
void VbiDecodeLine( const uchar *lbuf, int line, bool doVps );
bool VbiDecodeStartNewFrame( uint frameSeqNo );


#endif // __VBIDECODE_H
