/*
 *  VBI capture driver for the Booktree 848/849/878/879 family
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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: tvchan.h,v 1.1 2000/12/09 14:03:40 tom Exp tom $
 */

#ifndef __TVCHAN_H
#define __TVCHAN_H


// ---------------------------------------------------------------------------
// declaration of service interface functions
//
bool TvChannels_GetNext( uint *pChan, ulong *pFreq );
int  TvChannels_GetCount( void );


#endif  // __TVCHAN_H
