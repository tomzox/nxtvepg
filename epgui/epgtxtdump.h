/*
 *  Nextview block ASCII dump
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
 *  $Id: epgtxtdump.h,v 1.10 2001/04/04 18:47:12 tom Exp tom $
 */

#ifndef __EPGTXTDUMP_H
#define __EPGTXTDUMP_H


// ---------------------------------------------------------------------------
// declaration of service interface functions
//

// interface to GUI
void EpgTxtDump_Toggle( void );
void EpgTxtDump_Database( EPGDB_CONTEXT *pDbContext, FILE *fp,
                          bool do_pi, bool do_xi, bool do_ai, bool do_ni,
                          bool do_oi, bool do_mi, bool do_li, bool do_ti );

// interface to stream decoder
void EpgTxtDump_Block( const EPGDB_BLOCK_UNION * pUnion, BLOCK_TYPE type, uchar stream );
void EpgTxtDump_UnknownBlock( BLOCK_TYPE type, uint size, uchar stream );

#endif  // __EPGTXTDUMP_H
