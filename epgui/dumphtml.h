/*
 *  Nextview GUI: Output of programme data in XML
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
 *  $Id: dumphtml.h,v 1.1 2003/02/26 21:59:31 tom Exp tom $
 */

#ifndef __DUMPHTML_H
#define __DUMPHTML_H


// ----------------------------------------------------------------------------
// Interface functions declaration

// Interface to main module
void EpgDumpHtml_Init( void );
void EpgDumpHtml_Destroy( void );

void EpgDumpHtml_WriteString( FILE *fp, const char * pText );
void EpgDumpHtml_RemoveQuotes( const uchar * pStr, uchar * pBuf, uint maxOutLen );


#endif  // __DUMPHTML_H
