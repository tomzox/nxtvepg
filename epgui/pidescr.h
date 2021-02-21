/*
 *  Nextview GUI: PI description text output
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *  Description: see C source file.
 */

#ifndef __PIDESCR_H
#define __PIDESCR_H


// ----------------------------------------------------------------------------
// Table to implement isalnum() for all latin fonts
//
extern const schar alphaNumTab[256];

#define ALNUM_NONE    0
#define ALNUM_DIGIT   1
#define ALNUM_UCHAR   2
#define ALNUM_LCHAR  -1
#define ALNUM_NATION  4

typedef struct
{
   FILE         * fp;
   char         * pStrBuf;
   uint         size;
   uint         off;

} PI_DESCR_BUF;

// ----------------------------------------------------------------------------
// Interface functions declaration

typedef void (PiDescr_AppendInfoTextCb_Type) ( void *fp, const char * pDesc, bool addSeparator );
void PiDescription_BufAppend( PI_DESCR_BUF * pBuf, const char * pStr, sint len );

// Interface to PI listbox
void PiDescription_AppendDescriptionText( const PI_BLOCK * pPiBlock,
                                          PiDescr_AppendInfoTextCb_Type AppendInfoTextCb,
                                          void *fp, bool isMerged );
void PiDescription_AppendCompressedThemes( const PI_BLOCK *pPiBlock, char * outstr, uint maxlen );
void PiDescription_AppendFeatureList( const PI_BLOCK *pPiBlock, char * outstr );

// Interface to filter module (series title lists)
const char * PiDescription_RemoveSeriesIndex( const char * pTitle, char * outbuf, uint maxLen );
const char * PiDescription_DictifyTitle( const char * pTitle, uchar lang, char * outbuf, uint maxLen );

#endif  // __PIDESCR_H
