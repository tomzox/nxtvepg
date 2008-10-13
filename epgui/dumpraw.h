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
 *  $Id: dumpraw.h,v 1.14 2008/10/12 19:55:39 tom Exp tom $
 */

#ifndef __DUMPRAW_H
#define __DUMPRAW_H


// ---------------------------------------------------------------------------
// declaration of service interface functions
//
#include "epgdb/epgdbfil.h"

// interface to stream decoder
void EpgDumpRaw_IncomingBlock( const EPGDB_BLOCK_UNION * pUnion, BLOCK_TYPE type, uchar stream );
void EpgDumpRaw_IncomingUnknown( BLOCK_TYPE type, uint size, uchar stream );

// interface to main module (command line)
void EpgDumpRaw_Standalone( EPGDB_CONTEXT * pDbContext, FILTER_CONTEXT * fc, FILE * fp );
void EpgDumpRaw_Toggle( void );

void EpgDumpRaw_Database( EPGDB_CONTEXT *pDbContext, FILTER_CONTEXT * fc, FILE *fp,
                          bool do_pi, bool do_xi, bool do_ai, bool do_ni,
                          bool do_oi, bool do_mi, bool do_li, bool do_ti );

#endif  // __DUMPRAW_H
