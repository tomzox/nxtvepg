/*
 *  Nextview EPG database raw dump
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
 *  $Id: dumpraw.h,v 1.11 2003/02/26 21:53:49 tom Exp tom $
 */

#ifndef __DUMPRAW_H
#define __DUMPRAW_H


// ---------------------------------------------------------------------------
// declaration of service interface functions
//

// interface to stream decoder
void EpgDumpRaw_IncomingBlock( const EPGDB_BLOCK_UNION * pUnion, BLOCK_TYPE type, uchar stream );
void EpgDumpRaw_IncomingUnknown( BLOCK_TYPE type, uint size, uchar stream );

void EpgDumpRaw_Init( void );
void EpgDumpRaw_Destroy( void );

#endif  // __DUMPRAW_H
