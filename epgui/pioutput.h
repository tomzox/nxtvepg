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
 *  $Id: pioutput.h,v 1.20 2020/06/17 19:32:20 tom Exp tom $
 */

#ifndef __PIOUTPUT_H
#define __PIOUTPUT_H


// ----------------------------------------------------------------------------
// Definition of PI listbox column types - must match keyword list in source
//
typedef enum
{
   PIBOX_COL_DAY,
   PIBOX_COL_DAY_MONTH,
   PIBOX_COL_DAY_MONTH_YEAR,
   PIBOX_COL_DESCR,
   PIBOX_COL_DURATION,
   PIBOX_COL_ED_RATING,
   PIBOX_COL_FORMAT,
   PIBOX_COL_LIVE_REPEAT,
   PIBOX_COL_NETNAME,
   PIBOX_COL_PAR_RATING,
   PIBOX_COL_PIL,
   PIBOX_COL_SOUND,
   PIBOX_COL_SUBTITLES,
   PIBOX_COL_THEME,
   PIBOX_COL_TIME,
   PIBOX_COL_TITLE,
   PIBOX_COL_WEEKDAY,
   PIBOX_COL_WEEKCOL,
   PIBOX_COL_REMINDER,
   PIBOX_COL_USER_DEF,
   PIBOX_COL_INVALID
} PIBOX_COL_TYPES;

// cache for PI listbox column configuration
typedef struct
{
   PIBOX_COL_TYPES  type;
   uint             width;
   bool             skipNewline;
   Tcl_Obj        * pDefObj;
} PIBOX_COL_CFG;


// ----------------------------------------------------------------------------
// Interface functions declaration

// Interface to main module
void PiOutput_Init( void );
void PiOutput_Destroy( void );

// interface to PI listboxes
void PiOutput_PiListboxInsert( const PI_BLOCK *pPiBlock, uint textrow );
uint PiOutput_PiNetBoxInsert( const PI_BLOCK * pPiBlock, uint colIdx, sint textRow );
void PiOutput_DescriptionTextUpdate( const PI_BLOCK * pPiBlock, bool keepView );
void PiOutput_DescriptionTextClear( void );

// Interface to HTML dump
#ifdef _TCL
PIBOX_COL_TYPES PiOutput_GetPiColumnType( Tcl_Obj * pKeyObj );
uint PiOutput_MatchUserCol( const PI_BLOCK * pPiBlock, PIBOX_COL_TYPES * pType, Tcl_Obj * pMarkObj,
                            char * pOutBuffer, uint maxLen, int * pCharLen,
                            Tcl_Obj ** ppImageObj, Tcl_Obj ** ppFmtObj );
uint PiOutput_PrintColumnItem( const PI_BLOCK * pPiBlock, PIBOX_COL_TYPES type,
                               char * pOutBuffer, uint maxLen, int * pCharLen );
const PIBOX_COL_CFG * PiOutput_CfgColumnsCache( uint colCount, Tcl_Obj ** pColObjv );
void PiOutput_CfgColumnsClear( const PIBOX_COL_CFG * pColTab, uint colCount );
#endif

#endif  // __PIOUTPUT_H
