/*
 *  Nextview EPG block database dump & reload
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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgdbsav.h,v 1.10.1.1 2000/11/07 20:59:28 tom Exp $
 */

#ifndef __EPGDBSAV_H
#define __EPGDBSAV_H


// ---------------------------------------------------------------------------
// definition of dump header struct

#define MAGIC_STR      "NEXTVIEW-DB by TOMZO\n"
#define MAGIC_STR_LEN  20

#define DUMP_VERSION   0x00000303 // current version 0.3.3
#define DUMP_COMPAT    0x0000010a // last compatible version

#ifdef WIN32
#define DUMP_NAME_FMT  "NXTV%04X.EPG"
#define DUMP_NAME_PAT  "NXTV*.EPG"
#define DUMP_NAME_LEN  (8+1+3)
#else
#define DUMP_NAME_FMT  "nxtvdb-%04x"
#define DUMP_NAME_TMP  ".tmp"
#define DUMP_NAME_LEN  (6+1+4)
#endif
#define DUMP_NAME_MAX  25

#ifndef O_BINARY
#define O_BINARY       0          // for M$-Windows only
#endif


typedef struct
{
   uchar   magic[MAGIC_STR_LEN];  // file header
   ulong   dumpVersion;           // version of this software

   ulong   lastPiDate;            // stop time of last PI in db
   uint    cni;                   // CNI of EPG provider
   uint    pageNo;                // last used ttx page
   ulong   tunerFreq;             // tuner frequency of provider's channel

   uchar   reserved[46];          // unused space for future use; set to 0
} EPGDBSAV_HEADER;


#define RELOAD_ANY_CNI   0

typedef struct
{
   uint         pageNo;
   ulong        tunerFreq;
   EPGDB_BLOCK  *pBiBlock;
   EPGDB_BLOCK  *pAiBlock;
} EPGDBSAV_PEEK;


#define BLOCK_TYPE_DEFECT_PI  0x77


// ---------------------------------------------------------------------------
// declaration of service interface functions

bool EpgDbDump( EPGDB_CONTEXT *pDbContext );
bool EpgDbReload( EPGDB_CONTEXT *pDbContext, uint cni );
uint EpgDbReloadScan( int index );

const EPGDBSAV_PEEK * EpgDbPeek( uint cni );
void EpgDbPeekDestroy( const EPGDBSAV_PEEK *pPeek );
bool EpgDbDumpUpdateHeader( const EPGDB_CONTEXT *pDbContext, uint cni, ulong freq );


#endif  // __EPGDBSAV_H
