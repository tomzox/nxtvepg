/*
 *  Nextview EPG block database search filters
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
 *  $Id: epgdbfil.h,v 1.26 2003/03/14 18:53:53 tom Exp tom $
 */

#ifndef __EPGDBFIL_H
#define __EPGDBFIL_H


// ----------------------------------------------------------------------------
// global definitions of filter setting parameters

#define FILTER_EXPIRE_TIME   0x0001
#define FILTER_NETWOP_PRE    0x0002
#define FILTER_NETWOP_PRE2 0x100000
#define FILTER_AIR_TIMES   0x010000
#define FILTER_NETWOP        0x0004
#define FILTER_THEMES        0x0008
#define FILTER_SORTCRIT      0x0010
#define FILTER_SERIES        0x0020
#define FILTER_SUBSTR        0x0040
//free                       0x0080
#define FILTER_PROGIDX       0x0100
#define FILTER_TIME_ONCE     0x0200
#define FILTER_TIME_DAILY    0x0400
#define FILTER_DURATION    0x020000
#define FILTER_PAR_RAT       0x0800
#define FILTER_EDIT_RAT      0x1000
#define FILTER_FEATURES      0x2000
#define FILTER_LANGUAGES     0x4000
#define FILTER_SUBTITLES     0x8000
#define FILTER_VPS_PDC     0x040000
#define FILTER_INVERT      0x080000
// sum of all filter bitmasks
#define FILTER_ALL         0x1FFFFF
// sum of permanent "pre"-filters
#define FILTER_PERM          (FILTER_EXPIRE_TIME | FILTER_AIR_TIMES | \
                              FILTER_NETWOP_PRE | FILTER_NETWOP_PRE2)

#define FEATURES_ALL         0x01FF

#define LI_DESCR_TYPE        7  //   EPGDBACQ_TYPE_LI
#define TI_DESCR_TYPE        8  //   EPGDBACQ_TYPE_TI

// ----------------------------------------------------------------------------
// temporary structure for processing NI stacks
//
#define NI_DATE_NONE      0
#define NI_DATE_RELDATE   1
#define NI_DATE_START     2
#define NI_DATE_STOP      4

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
   struct EPGDB_FILT_SUBSTR_struct * pNext;

   bool   scopeTitle;
   bool   scopeDesc;
   bool   strMatchCase;
   bool   strMatchFull;

   uchar  str[1];
} EPGDB_FILT_SUBSTR;

// ----------------------------------------------------------------------------
// definition of filter context structure
//
#define LI_DESCR_BUFFER_SIZE   (LI_MAX_DESC_COUNT/8)
#define TI_DESCR_BUFFER_SIZE   (TI_MAX_DESC_COUNT/8)
#define THEME_CLASS_COUNT       8
#define FEATURE_CLASS_COUNT     6

typedef struct
{
   uint   enabledFilters;
   uint   invertedFilters;

   uchar  firstProgIdx, lastProgIdx;
   time_t expireTime;
   time_t timeBegin, timeEnd;
   uint   duration_min;
   uint   duration_max;
   bool   netwopCacheUpdated;
   bool   netwopCache[MAX_NETWOP_COUNT];
   bool   netwopFilterField[MAX_NETWOP_COUNT];
   bool   netwopPreFilter1[MAX_NETWOP_COUNT];
   bool   netwopPreFilter2[MAX_NETWOP_COUNT];
   uint   netwopAirTimeStart[MAX_NETWOP_COUNT];
   uint   netwopAirTimeStop[MAX_NETWOP_COUNT];
   uchar  themeFilterField[256];
   uchar  seriesFilterMatrix[MAX_NETWOP_COUNT][128];
   uchar  usedThemeClasses;
   uchar  invertedThemeClasses;
   uchar  sortCritFilterField[256];
   uchar  usedSortCritClasses;
   uchar  invertedSortCritClasses;
   uchar  parentalRating;
   uchar  editorialRating;
   uint   featureFilterFlagField[FEATURE_CLASS_COUNT];
   uint   featureFilterMaskField[FEATURE_CLASS_COUNT];
   uchar  featureFilterCount;
   uchar  langDescrTable[MAX_NETWOP_COUNT][LI_DESCR_BUFFER_SIZE];
   uchar  subtDescrTable[MAX_NETWOP_COUNT][TI_DESCR_BUFFER_SIZE];
   uint   vps_pdc_mode;
   EPGDB_FILT_SUBSTR * pSubStrCtx;
} FILTER_CONTEXT;


#define EpgDbFilterGetExpireTime(FC) ((FC)->expireTime)

// ----------------------------------------------------------------------------
// global function declarations

FILTER_CONTEXT * EpgDbFilterCreateContext( void );
FILTER_CONTEXT * EpgDbFilterCopyContext( const FILTER_CONTEXT * fc );
void   EpgDbFilterDestroyContext( FILTER_CONTEXT * fc );

void   EpgDbFilterEnable( FILTER_CONTEXT *fc, uint searchFilter );
void   EpgDbFilterDisable( FILTER_CONTEXT *fc, uint searchFilter );
void   EpgDbFilterInvert( FILTER_CONTEXT *fc, uint mask, uchar themeClass, uchar sortCritClass );

void   EpgDbFilterInitNetwop( FILTER_CONTEXT *fc );
void   EpgDbFilterSetNetwop( FILTER_CONTEXT *fc, uchar netwopNo );
void   EpgDbFilterInitNetwopPreFilter( FILTER_CONTEXT *fc );
void   EpgDbFilterSetNetwopPreFilter( FILTER_CONTEXT *fc, uchar netwopNo );
void   EpgDbFilterInitNetwopPreFilter2( FILTER_CONTEXT *fc );
void   EpgDbFilterSetNetwopPreFilter2( FILTER_CONTEXT *fc, uchar netwopNo );
void   EpgDbFilterInitAirTimesFilter( FILTER_CONTEXT *fc );
void   EpgDbFilterSetAirTimesFilter( FILTER_CONTEXT *fc, uchar netwopNo, uint startMoD, uint stopMoD );
void   EpgDbFilterSetExpireTime( FILTER_CONTEXT *fc, ulong newExpireTime );
void   EpgDbFilterSetDateTimeBegin( FILTER_CONTEXT *fc, ulong newTimeBegin );
void   EpgDbFilterSetDateTimeEnd( FILTER_CONTEXT *fc, ulong newTimeEnd );
void   EpgDbFilterSetMinMaxDuration( FILTER_CONTEXT *fc, uint dur_min, uint dur_max );
uchar  EpgDbFilterInitThemes( FILTER_CONTEXT *fc, uchar themeClassBitField );
void   EpgDbFilterSetThemes( FILTER_CONTEXT *fc, uchar firstTheme, uchar lastTheme, uchar themeClassBitField );
void   EpgDbFilterInitSeries( FILTER_CONTEXT *fc );
void   EpgDbFilterSetSeries( FILTER_CONTEXT *fc, uchar netwop, uchar series, bool enable );
uchar  EpgDbFilterInitSortCrit( FILTER_CONTEXT *fc, uchar sortCritClassBitField );
void   EpgDbFilterSetSortCrit( FILTER_CONTEXT *fc, uchar firstSortCrit, uchar lastSortCrit, uchar sortCritClassBitField );
void   EpgDbFilterSetParentalRating( FILTER_CONTEXT *fc, uchar parentalRating );
void   EpgDbFilterSetEditorialRating( FILTER_CONTEXT *fc, uchar editorialRating );
void   EpgDbFilterSetFeatureFlags( FILTER_CONTEXT *fc, uchar index, uint flags, uint mask );
void   EpgDbFilterSetNoFeatures( FILTER_CONTEXT *fc, uchar noFeatures );
uchar  EpgDbFilterGetNoFeatures( FILTER_CONTEXT *fc );
void   EpgDbFilterInitLangDescr( FILTER_CONTEXT *fc );
void   EpgDbFilterSetLangDescr( const EPGDB_CONTEXT *dbc, FILTER_CONTEXT *fc, const uchar *lg );
void   EpgDbFilterInitSubtDescr( FILTER_CONTEXT *fc );
void   EpgDbFilterSetSubtDescr( const EPGDB_CONTEXT *dbc, FILTER_CONTEXT *fc, const uchar *lg );
void   EpgDbFilterSetProgIdx( FILTER_CONTEXT *fc, uchar firstProgIdx, uchar lastProgIdx );
void   EpgDbFilterSetVpsPdcMode( FILTER_CONTEXT *fc, uint mode );
void   EpgDbFilterSetSubStr( FILTER_CONTEXT *fc, const uchar *pStr,
                             bool scopeTitle, bool scopeDesc, bool matchCase, bool matchFull );

void   EpgDbFilterInitNi( FILTER_CONTEXT *fc, NI_FILTER_STATE *pNiState );
void   EpgDbFilterApplyNi( const EPGDB_CONTEXT *dbc, FILTER_CONTEXT *fc, NI_FILTER_STATE *pNiState, uchar kind, ulong data );
void   EpgDbFilterFinishNi( FILTER_CONTEXT *fc, NI_FILTER_STATE *pNiState );

bool   EpgDbFilterMatches( const EPGDB_CONTEXT *dbc, FILTER_CONTEXT *fc, const PI_BLOCK * pi );

void EpgDbFilterGetNetwopFilter( FILTER_CONTEXT *fc, uchar * pNetFilter, uint count );


#endif  // __EPGDBFIL_H
