/*
 *  Nextview EPG database merging
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
 *  $Id: epgdbmerge.h,v 1.7 2001/09/02 16:28:04 tom Exp tom $
 */

#ifndef __EPGDBMERGE_H
#define __EPGDBMERGE_H


// max number of databases that can be merged into one
#define MAX_MERGED_DB_COUNT  10

// descriptor type for dbIdx info which is inserted into merged PI blocks
#define MERGE_DESCR_TYPE     0x3c

// list of PI attributes that can be configured separately
typedef enum
{
   MERGE_TYPE_TITLE,
   MERGE_TYPE_DESCR,
   MERGE_TYPE_THEMES,
   MERGE_TYPE_SERIES,
   MERGE_TYPE_SORTCRIT,
   MERGE_TYPE_EDITORIAL,
   MERGE_TYPE_PARENTAL,
   MERGE_TYPE_SOUND,
   MERGE_TYPE_FORMAT,
   MERGE_TYPE_REPEAT,
   MERGE_TYPE_SUBT,
   MERGE_TYPE_OTHERFEAT,
   MERGE_TYPE_VPS,
   MERGE_TYPE_COUNT
} MERGE_ATTRIB_TYPE;

typedef uchar MERGE_ATTRIB_VECTOR[MAX_MERGED_DB_COUNT];
typedef MERGE_ATTRIB_VECTOR MERGE_ATTRIB_MATRIX[MERGE_TYPE_COUNT];
typedef MERGE_ATTRIB_VECTOR *MERGE_ATTRIB_VECTOR_PTR;

typedef uchar EPGDB_MERGE_MAP[MAX_NETWOP_COUNT];

typedef struct
{
   uint            dbCount;
   uint            acqIdx;
   EPGDB_CONTEXT * pDbContext   [MAX_MERGED_DB_COUNT];
   uint            cnis         [MAX_MERGED_DB_COUNT];
   EPGDB_MERGE_MAP netwopMap    [MAX_MERGED_DB_COUNT];
   EPGDB_MERGE_MAP revNetwopMap [MAX_MERGED_DB_COUNT];

   MERGE_ATTRIB_VECTOR max[MERGE_TYPE_COUNT];
} EPGDB_MERGE_CONTEXT;


// ----------------------------------------------------------------------------
// Declaration of interface functions
//
void EpgDbMergeInsertPi( EPGDB_MERGE_CONTEXT * dbmc, EPGDB_BLOCK * pNewBlock );
void EpgDbMergeAiBlocks( EPGDB_CONTEXT * dbc, uint netwopCount, uint * pNetwopList );
void EpgDbMergeAllPiBlocks( EPGDB_CONTEXT * dbc );
void EpgDbMerge_ResetPiVersion( EPGDB_CONTEXT * dbc, uint dbIdx );


#endif  // __EPGDBMERGE_H
