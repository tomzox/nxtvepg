/*
 *  Export Nextview database as "TAB-separated" text file
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
 *  $Id: dumptext.h,v 1.5 2004/08/29 21:48:55 tom Exp tom $
 */

#ifndef __DUMPTEXT_H
#define __DUMPTEXT_H


typedef enum
{
   EPGTAB_DUMP_AI,
   EPGTAB_DUMP_PI,
   EPGTAB_DUMP_PDC,
   EPGTAB_DUMP_XML,
   EPGTAB_DUMP_DEBUG,
   EPGTAB_CLOCK_SET,
   EPGTAB_CLOCK_PRINT,
   EPGTAB_DUMP_COUNT,
   EPGTAB_DUMP_NONE = EPGTAB_DUMP_COUNT
} EPGTAB_DUMP_MODE;

#define IS_CLOCK_MODE(X) (((X)==EPGTAB_CLOCK_SET)||((X)==EPGTAB_CLOCK_PRINT))

// ---------------------------------------------------------------------------
// declaration of service interface functions
//

EPGTAB_DUMP_MODE EpgDumpText_GetMode( const char * pModeStr );
void EpgDumpText_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp, EPGTAB_DUMP_MODE mode );

void EpgDumpText_Destroy( void );
void EpgDumpText_Init( void );


#endif  // __DUMPTEXT_H
