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
 *  $Id: pioutput.h,v 1.8 2002/08/24 13:58:16 tom Exp tom $
 */

#ifndef __PIOUTPUT_H
#define __PIOUTPUT_H


// Definition of PI listbox column types
typedef enum
{
   PIBOX_COL_NETNAME,
   PIBOX_COL_TIME,
   PIBOX_COL_WEEKDAY,
   PIBOX_COL_DAY,
   PIBOX_COL_DAY_MONTH,
   PIBOX_COL_DAY_MONTH_YEAR,
   PIBOX_COL_TITLE,
   PIBOX_COL_DESCR,
   PIBOX_COL_PIL,
   PIBOX_COL_THEME,
   PIBOX_COL_SOUND,
   PIBOX_COL_FORMAT,
   PIBOX_COL_ED_RATING,
   PIBOX_COL_PAR_RATING,
   PIBOX_COL_LIVE_REPEAT,
   PIBOX_COL_SUBTITLES,
   PIBOX_COL_COUNT

} PIBOX_COL_TYPES;


typedef void (PiOutput_AppendInfoTextCb_Type) ( void *fp, const char * pShortInfo, bool insertSeparator, const char * pLongInfo );

// Interface to filter module (series title lists)
const char * PiOutput_DictifyTitle( const char * pTitle, uchar lang, char * outbuf, uint maxLen );
void PiOutput_CtxMenuAddUserDef( const char * pMenu, bool addSeparator );

// Interface to menus
void PiOutput_CacheThemesMaxLen( void );
void PiOutput_SetNetnameColumnWidth( bool isInitial );

// Interface to PI listbox and HTML dump
int  PiOutput_PrintColumnItem( const PI_BLOCK * pPiBlock, PIBOX_COL_TYPES type, char * outstr );
void PiOutput_AppendShortAndLongInfoText( const PI_BLOCK *pPiBlock,
                                          PiOutput_AppendInfoTextCb_Type AppendInfoTextCb,
                                          void *fp, bool isMerged );
void PiOutput_AppendCompressedThemes( const PI_BLOCK *pPiBlock, char * outstr, uint maxlen );
void PiOutput_AppendFeatureList( const PI_BLOCK *pPiBlock, char * outstr );

// Interface to main module
void PiOutput_Create( void );
void PiOutput_Destroy( void );


#endif  // __PIOUTPUT_H
