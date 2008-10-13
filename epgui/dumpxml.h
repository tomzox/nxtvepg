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
 *  $Id: dumpxml.h,v 1.6 2008/10/12 19:56:11 tom Exp tom $
 */

#ifndef __DUMPXML_H
#define __DUMPXML_H


typedef enum
{
   DUMP_XMLTV_ANY,
   DUMP_XMLTV_DTD_5_GMT,
   DUMP_XMLTV_DTD_5_LTZ,
   DUMP_XMLTV_DTD_6,
   DUMP_XMLTV_COUNT
} DUMP_XML_MODE;

// ----------------------------------------------------------------------------
// Interface functions declaration

void EpgDumpXml_HtmlWriteString( FILE *fp, const char * pText, sint strlen );
void EpgDumpXml_HtmlRemoveQuotes( const uchar * pStr, uchar * pBuf, uint maxOutLen );

void EpgDumpXml_Standalone( EPGDB_CONTEXT * pDbContext, FILTER_CONTEXT * fc,
                            FILE * fp, DUMP_XML_MODE dumpMode );

#endif  // __DUMPXML_H
