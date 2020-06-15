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
 *  $Id: dumptext.h,v 1.7 2005/01/01 18:18:23 tom Exp tom $
 */

#ifndef __DUMPTEXT_H
#define __DUMPTEXT_H


typedef enum
{
   DUMP_TEXT_PDC,
   DUMP_TEXT_AI,
   DUMP_TEXT_PI,
   DUMP_TEXT_COUNT
} DUMP_TEXT_MODE;

// ---------------------------------------------------------------------------
// declaration of service interface functions
//

void EpgDumpText_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp, DUMP_TEXT_MODE mode );

void EpgDumpText_Destroy( void );
void EpgDumpText_Init( void );


#endif  // __DUMPTEXT_H
