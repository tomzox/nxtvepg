/*
 *  Nextview GUI: Export of programme data
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
 *  $Id: uidump.h,v 1.2 2007/01/21 14:09:23 tom Exp tom $
 */

#ifndef __UIDUMP_H
#define __UIDUMP_H


// ----------------------------------------------------------------------------
// Interface functions declaration

void EpgDump_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp, int dumpMode, int dumpSubMode );
void EpgDump_Destroy( void );
void EpgDump_Init( void );

#endif  // __UIDUMP_H

