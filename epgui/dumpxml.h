/*
 *  Nextview GUI: Output of PI data in HTML and XML
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
 *  $Id: dumpxml.h,v 1.1 2003/02/26 21:59:35 tom Exp tom $
 */

#ifndef __DUMPXML_H
#define __DUMPXML_H


// ----------------------------------------------------------------------------
// Interface functions declaration

// Interface to main module
void EpgDumpXml_Init( void );
void EpgDumpXml_Destroy( void );

void EpgDumpXml_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp );

#endif  // __DUMPXML_H
