/*
 *  Nextview GUI: Output of PI data in various formats
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
 *  $Id: pioutput.h,v 1.12 2002/12/08 19:25:34 tom Exp tom $
 */

#ifndef __PIOUTPUT_H
#define __PIOUTPUT_H


// ----------------------------------------------------------------------------
// Interface functions declaration

typedef void (PiOutput_AppendInfoTextCb_Type) ( void *fp, const char * pDesc, bool addSeparator );

// Interface to filter module (series title lists)
const char * PiOutput_DictifyTitle( const char * pTitle, uchar lang, char * outbuf, uint maxLen );
void PiOutput_CtxMenuAddUserDef( const char * pMenu, bool addSeparator );

// Interface to PI listbox
void PiOutput_PiListboxInsert( const PI_BLOCK *pPiBlock, uint textrow );
void PiOutput_AppendShortAndLongInfoText( const PI_BLOCK *pPiBlock,
                                          PiOutput_AppendInfoTextCb_Type AppendInfoTextCb,
                                          void *fp, bool isMerged );
void PiOutput_AppendCompressedThemes( const PI_BLOCK *pPiBlock, char * outstr, uint maxlen );
void PiOutput_AppendFeatureList( const PI_BLOCK *pPiBlock, char * outstr );

// Interface to main module
void PiOutput_Create( void );
void PiOutput_Destroy( void );
void PiOutput_DumpDatabaseXml( EPGDB_CONTEXT * pDbContext, FILE * fp );

#endif  // __PIOUTPUT_H
