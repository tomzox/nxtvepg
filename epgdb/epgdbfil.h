/*
 *  Nextview EPG block database search filters
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

#ifndef __EPGDBFIL_H
#define __EPGDBFIL_H


// ----------------------------------------------------------------------------
// filter context parameters
//
#define FILTER_EXPIRE_TIME    0x00000001
#define FILTER_NETWOP_PRE     0x00000002
#define FILTER_NETWOP_PRE2    0x00000004
#define FILTER_AIR_TIMES      0x00000008

#define FILTER_NETWOP         0x00000010
#define FILTER_THEMES         0x00000020
//#define FILTER_SORTCRIT     0x00000040
//#define FILTER_SERIES       0x00000080
#define FILTER_SUBSTR         0x00000100
#define FILTER_CUSTOM         0x00000200
#define FILTER_PROGIDX        0x00000400
#define FILTER_TIME_ONCE      0x00000800
#define FILTER_TIME_DAILY     0x00001000
#define FILTER_TIME_WEEKLY    0x00002000
#define FILTER_TIME_MONTHLY   0x00004000
#define FILTER_DURATION       0x00008000
#define FILTER_PAR_RAT        0x00010000
#define FILTER_EDIT_RAT       0x00020000
#define FILTER_FEATURES       0x00040000
//#define FILTER_LANGUAGES    0x00080000
//#define FILTER_SUBTITLES    0x00100000
#define FILTER_VPS_PDC        0x00200000
#define FILTER_INVERT         0x00400000
// sum of all filter bitmasks
#define FILTER_ALL            0x007FFFFF
// sum of permanent "pre"-filters
#define FILTER_PERM          (FILTER_EXPIRE_TIME | FILTER_AIR_TIMES | \
                              FILTER_NETWOP_PRE | FILTER_NETWOP_PRE2)
#define FILTER_NONPERM       (FILTER_ALL & ~FILTER_PERM)

#define FILTER_TIME_ALL      (FILTER_TIME_ONCE | FILTER_TIME_DAILY | \
                              FILTER_TIME_WEEKLY | FILTER_TIME_MONTHLY)

#define FEATURES_ALL           0x01FF

#define LI_DESCR_TYPE        7  //   EPGDBACQ_TYPE_LI
#define TI_DESCR_TYPE        8  //   EPGDBACQ_TYPE_TI

#define FILTER_FORK_NULL     0
#define FILTER_FORK_OR       1
#define FILTER_FORK_AND      2

// ----------------------------------------------------------------------------
// temporary structure for processing NI stacks
//
#define NI_DATE_NONE         0
#define NI_DATE_RELDATE   0x01
#define NI_DATE_WEEKLY    0x02
#define NI_DATE_MONTHLY   0x04
#define NI_DATE_START     0x08
#define NI_DATE_STOP      0x10

typedef struct
{
   uchar   flags;
   uchar   reldate;
   uint    startMoD;
   uint    stopMoD;
} NI_FILTER_STATE;

// ----------------------------------------------------------------------------
// search context for substring filter - can be linked list for ORed searches

typedef struct EPGDB_FILT_SUBSTR_struct
{
   struct EPGDB_FILT_SUBSTR_struct * pNext;  // must be first in struct
   uint   elem_size;

   bool   scopeTitle;
   bool   scopeDesc;
   bool   strMatchCase;
   bool   strMatchFull;

   char   str[1];
} EPGDB_FILT_SUBSTR;

typedef struct EPGDB_FILT_SUBCTX_GENERIC_struct
{
   struct EPGDB_FILT_SUBCTX_GENERIC_struct * pNext;
   uint   elem_size;
} EPGDB_FILT_SUBCTX_GENERIC;

// ----------------------------------------------------------------------------
// callback function to implement reminder match in user-level

typedef bool CUSTOM_FILTER_MATCH ( const EPGDB_CONTEXT * usedDbc,
                                   const PI_BLOCK * pPiBlock, void * pArg );
typedef void CUSTOM_FILTER_FREE  ( void * pArg );

// ----------------------------------------------------------------------------
// definition of filter context structure
//
#define THEME_CLASS_COUNT       8
#define FEATURE_CLASS_COUNT     6

typedef struct FILTER_CTX_ACT_struct
{
   struct FILTER_CTX_ACT_struct * pNext;
   uint      elem_size;

   uint      enabledFilters;
   uint      invertedFilters;
   uchar     forkCombMode;
   sint      forkTag;
   uint      netwopCount;   // size of dynamically allocated netwopFilterField

   uchar     firstProgIdx, lastProgIdx;
   time_t    timeBegin, timeEnd;
   uint      timeDayOffset;
   uint      duration_min;
   uint      duration_max;
   bool    * pNetwopFilterField;
   uchar     themeFilterField[256];
   uchar     usedThemeClasses;
   uchar     invertedThemeClasses;
   uchar     parentalRating;
   uchar     editorialRating;
   uint      featureFilterFlagField[FEATURE_CLASS_COUNT];
   uint      featureFilterMaskField[FEATURE_CLASS_COUNT];
   uchar     featureFilterCount;
   uint      vps_pdc_mode;

   EPGDB_FILT_SUBSTR    * pSubStrCtx;
   void                 * pCustomArg;
   CUSTOM_FILTER_MATCH  * pCustomFilterFunc;
   CUSTOM_FILTER_FREE   * pCustomDestroyFunc;

} FILTER_CTX_ACT;

typedef struct
{
   uint   enabledPreFilters;
   uint   netwopCount;   // size of following dynamically allocated arrays
   bool * pNetwopPreFilter1;
   bool * pNetwopPreFilter2;
   uint * pNetwopAirTimeStart;
   uint * pNetwopAirTimeStop;
   time_t expireTime;

   FILTER_CTX_ACT    act;
   FILTER_CTX_ACT  * pFocus;

} FILTER_CONTEXT;


#define EpgDbFilterGetExpireTime(FC) ((FC)->expireTime)
#define EpgDbFilterIsEnabled(FC,TYP) (((FC)->pFocus->enabledFilters & (TYP)) != 0)
#define EpgDbFilterIsForked(FC)      ((FC)->act.pNext != NULL)
#define EpgDbFilterIsThemeFiltered(FC,IDX) (((FC)->pFocus->themeFilterField[(IDX)&0xff] & (FC)->pFocus->usedThemeClasses) != 0)

// ----------------------------------------------------------------------------
// global function declarations

FILTER_CONTEXT * EpgDbFilterCreateContext( void );
FILTER_CONTEXT * EpgDbFilterCopyContext( const FILTER_CONTEXT * fc );
void   EpgDbFilterDestroyContext( FILTER_CONTEXT * fc );

void   EpgDbFilterFork( FILTER_CONTEXT * fc, uint combMode, sint tag );
void   EpgDbFilterCloseFork( FILTER_CONTEXT * fc );
void   EpgDbFilterDestroyFork( FILTER_CONTEXT * fc, sint tag );
void   EpgDbFilterDestroyAllForks( FILTER_CONTEXT * fc );

void   EpgDbPreFilterEnable( FILTER_CONTEXT *fc, uint mask );
void   EpgDbPreFilterDisable( FILTER_CONTEXT *fc, uint mask );
void   EpgDbFilterEnable( FILTER_CONTEXT *fc, uint mask );
void   EpgDbFilterDisable( FILTER_CONTEXT *fc, uint mask );
void   EpgDbFilterInvert( FILTER_CONTEXT *fc, uint mask, uchar themeClass );

void   EpgDbFilterInitNetwop( FILTER_CONTEXT *fc, uint netwopCount );
void   EpgDbFilterSetNetwop( FILTER_CONTEXT *fc, uint netwopNo );
void   EpgDbFilterInitNetwopPreFilter( FILTER_CONTEXT *fc, uint netwopCount );
void   EpgDbFilterSetNetwopPreFilter( FILTER_CONTEXT *fc, uint netwopNo );
void   EpgDbFilterInitNetwopPreFilter2( FILTER_CONTEXT *fc, uint netwopCount );
void   EpgDbFilterSetNetwopPreFilter2( FILTER_CONTEXT *fc, uint netwopNo );
void   EpgDbFilterInitAirTimesFilter( FILTER_CONTEXT *fc, uint netwopCount );
void   EpgDbFilterSetAirTimesFilter( FILTER_CONTEXT *fc, uint netwopNo, uint startMoD, uint stopMoD );
void   EpgDbFilterSetExpireTime( FILTER_CONTEXT *fc, ulong newExpireTime );
void   EpgDbFilterSetDateTimeBegin( FILTER_CONTEXT *fc, ulong newTimeBegin );
void   EpgDbFilterSetDateTimeEnd( FILTER_CONTEXT *fc, ulong newTimeEnd );
void   EpgDbFilterSetMinMaxDuration( FILTER_CONTEXT *fc, uint dur_min, uint dur_max );
uchar  EpgDbFilterInitThemes( FILTER_CONTEXT *fc, uchar themeClassBitField );
void   EpgDbFilterSetThemes( FILTER_CONTEXT *fc, uchar firstTheme, uchar lastTheme, uchar themeClassBitField );
void   EpgDbFilterSetParentalRating( FILTER_CONTEXT *fc, uchar parentalRating );
void   EpgDbFilterSetEditorialRating( FILTER_CONTEXT *fc, uchar editorialRating );
void   EpgDbFilterSetFeatureFlags( FILTER_CONTEXT *fc, uchar index, uint flags, uint mask );
void   EpgDbFilterSetNoFeatures( FILTER_CONTEXT *fc, uchar noFeatures );
uchar  EpgDbFilterGetNoFeatures( FILTER_CONTEXT *fc );
void   EpgDbFilterSetProgIdx( FILTER_CONTEXT *fc, uchar firstProgIdx, uchar lastProgIdx );
void   EpgDbFilterSetVpsPdcMode( FILTER_CONTEXT *fc, uint mode );
void   EpgDbFilterSetSubStr( FILTER_CONTEXT *fc, const char *pStr,
                             bool scopeTitle, bool scopeDesc, bool matchCase, bool matchFull );
void   EpgDbFilterSetCustom( FILTER_CONTEXT *fc, CUSTOM_FILTER_MATCH * pMatchCb,
                             CUSTOM_FILTER_FREE * pFreeCb, void * pArg );

void   EpgDbFilterFinishNi( FILTER_CONTEXT *fc, NI_FILTER_STATE *pNiState );

bool   EpgDbFilterMatches( const EPGDB_CONTEXT *dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pi );

uchar * EpgDbFilterGetNetwopFilter( FILTER_CONTEXT *fc, uint count );


#endif  // __EPGDBFIL_H
