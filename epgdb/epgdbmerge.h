/*
 *  Nextview EPG database merging
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

#ifndef __EPGDBMERGE_H
#define __EPGDBMERGE_H


// separator character between description texts: ASCII "form feed"
#define EPG_DB_MERGE_DESC_TEXT_SEP  12

// list of PI attributes that can be configured separately
typedef enum
{
   MERGE_TYPE_TITLE,
   MERGE_TYPE_DESCR,
   MERGE_TYPE_THEMES,
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
   EPGDB_CONTEXT * pDbContext;
   uint            provCni;
   EPGDB_MERGE_MAP netwopMap;
   EPGDB_MERGE_MAP revNetwopMap;
} EPGDB_MERGE_PROV_CTX;

typedef struct
{
   uint            dbCount;

   EPGDB_MERGE_PROV_CTX prov[MAX_MERGED_DB_COUNT];
   MERGE_ATTRIB_VECTOR max[MERGE_TYPE_COUNT];
} EPGDB_MERGE_CONTEXT;


// ----------------------------------------------------------------------------
// Declaration of interface functions
//
void EpgDbMergeInsertPi( EPGDB_CONTEXT * pDbContext, EPGDB_BLOCK * pNewBlock );
void EpgDbMergeUpdateNetworks( EPGDB_CONTEXT * pDbContext, uint provCount, const uint * pProvCni );
void EpgDbMergeAiBlocks( EPGDB_CONTEXT * dbc, uint netwopCount, const uint * pNetwopList );
void EpgDbMergeAllPiBlocks( EPGDB_CONTEXT * dbc );
void EpgDbMerge_ResetPiVersion( EPGDB_CONTEXT * dbc, uint dbIdx );


#endif  // __EPGDBMERGE_H
