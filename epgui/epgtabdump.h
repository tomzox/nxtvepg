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
 *  $Id: epgtabdump.h,v 1.2 2002/10/20 17:35:02 tom Exp tom $
 */

#ifndef __EPGTABDUMP_H
#define __EPGTABDUMP_H


typedef enum
{
   EPGTAB_DUMP_AI,
   EPGTAB_DUMP_PI,
   EPGTAB_DUMP_PDC,
   EPGTAB_DUMP_XML,
   EPGTAB_DUMP_COUNT,
   EPGTAB_DUMP_NONE = EPGTAB_DUMP_COUNT
} EPGTAB_DUMP_MODE;


// ---------------------------------------------------------------------------
// declaration of service interface functions
//

EPGTAB_DUMP_MODE EpgTabDump_GetMode( const char * pModeStr );
void EpgTabDump_Database( EPGDB_CONTEXT * pDbContext, FILE * fp, EPGTAB_DUMP_MODE mode );

#endif  // __EPGTABDUMP_H