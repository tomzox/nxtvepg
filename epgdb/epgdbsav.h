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
 *  $Id: epgdbsav.h,v 1.24 2001/02/06 19:06:02 tom Exp tom $
 */

#ifndef __EPGDBSAV_H
#define __EPGDBSAV_H


// ---------------------------------------------------------------------------
// definition of dump header struct

#define MAGIC_STR      "NEXTVIEW-DB by TOMZO\n"
#define MAGIC_STR_LEN  20

#define DUMP_COMPAT    EPG_VERSION_TO_INT(0,4,3)   // latest compatible

#define ENDIAN_MAGIC   0xAA55
#define WRONG_ENDIAN   (((ENDIAN_MAGIC>>8)&0xFF)|((ENDIAN_MAGIC&0xFF)<<8))

#ifdef WIN32
#define PATH_SEPARATOR '\\'
#define PATH_ROOT      "\\"
#define DUMP_NAME_FMT  "NXTV%04X.EPG"
#define DUMP_NAME_PAT  "NXTV*.EPG"
#define DUMP_NAME_LEN  (8+1+3)
#else
#define PATH_SEPARATOR '/'
#define PATH_ROOT      "/"
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
   ushort  endianMagic;           // magic to recognize big/little endian
   ulong   compatVersion;         // version of oldest compatible software
   ulong   swVersion;             // version of this software

   time_t  lastPiDate;            // stop time of last PI in db
   time_t  firstPiDate;           // start time of first PI in db
   time_t  lastAiUpdate;          // time when the last AI block was received
   uint    cni;                   // CNI of EPG provider
   uint    pageNo;                // last used ttx page
   ulong   tunerFreq;             // tuner frequency of provider's channel
   uint    appId;                 // EPG application ID from BI block

   uchar   reserved[28];          // unused space for future use; set to 0
} EPGDBSAV_HEADER;


#define RELOAD_ANY_CNI   0

typedef struct
{
   uint         pageNo;
   ulong        tunerFreq;
   uint         appId;
   time_t       lastAiUpdate;
   time_t       firstPiDate;
   time_t       lastPiDate;
   EPGDB_BLOCK  *pBiBlock;
   EPGDB_BLOCK  *pAiBlock;
   EPGDB_BLOCK  *pOiBlock;
} EPGDBSAV_PEEK;


#define BLOCK_TYPE_DEFECT_PI  0x77

// max size is much larger than any block will ever become
// but should be small enough so it can safely be malloc'ed during reload
#define EPGDBSAV_MAX_BLOCK_SIZE  65536

// result codes for reload and peek (ordered by increasing user relevance)
typedef enum
{
   EPGDB_RELOAD_OK,            // no error
   EPGDB_RELOAD_INTERNAL,      // unexpected error
   EPGDB_RELOAD_CORRUPT,       // unexpected content
   EPGDB_RELOAD_WRONG_MAGIC,   // magic not found or file too short
   EPGDB_RELOAD_ACCESS,        // file open failed
   EPGDB_RELOAD_ENDIAN,        // big/little endian conflict
   EPGDB_RELOAD_VERSION,       // incompatible version
   EPGDB_RELOAD_EXIST          // file does not exist
} EPGDB_RELOAD_RESULT;

// macro to compare severity of reload errors
// (to be used if multiple errors occur in a loop across all databases)
#define RELOAD_ERR_WORSE(X,Y)  ((X)>(Y))

// ---------------------------------------------------------------------------
// header format of previous versions, included for error detection
// - else for previous versions' databases an "endian mismatch" would be reported
//
#define OBSOLETE_DUMP_MAX_VERSION   0x00000304 // start version
#define OBSOLETE_DUMP_MIN_VERSION   0x0000010a // max version

typedef struct
{
   uchar   magic[MAGIC_STR_LEN];  // file header
   ulong   dumpVersion;           // version of this software

   ulong   forbidden;             // different meanings between versions
   uint    cni;                   // CNI of EPG provider
   uint    pageNo;                // last used ttx page
   ulong   tunerFreq;             // tuner frequency of provider's channel

   uchar   reserved[46];          // unused space for future use; set to 0
} OBSOLETE_EPGDBSAV_HEADER;

// ---------------------------------------------------------------------------
// declaration of service interface functions

bool EpgDbDump( EPGDB_CONTEXT * pDbContext );
EPGDB_CONTEXT * EpgDbReload( uint cni, EPGDB_RELOAD_RESULT * pError );
uint EpgDbReloadScan( int index );
bool EpgDbDumpCreateDir( const char * pDirPath );
void EpgDbDumpGetDirAndCniFromArg( char * pArg, const char ** ppDirPath, uint * pCni );

const EPGDBSAV_PEEK * EpgDbPeek( uint cni, EPGDB_RELOAD_RESULT * pResult );
void EpgDbPeekDestroy( const EPGDBSAV_PEEK *pPeek );
bool EpgDbDumpUpdateHeader( uint cni, ulong freq );


#endif  // __EPGDBSAV_H
