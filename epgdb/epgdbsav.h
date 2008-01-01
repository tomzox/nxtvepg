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
 *
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgdbsav.h,v 1.42 2007/12/30 23:49:56 tom Exp tom $
 */

#ifndef __EPGDBSAV_H
#define __EPGDBSAV_H


// ---------------------------------------------------------------------------
// definition of dump header struct

#define MAGIC_STR      "NEXTVIEW-DB by TOMZO"
#define MAGIC_STR_LEN  20

#define DUMP_COMPAT    EPG_VERSION_TO_INT(0,7,0)   // latest compatible

#define ENDIAN_MAGIC   0xAA55
#define WRONG_ENDIAN   (((ENDIAN_MAGIC>>8)&0xFF)|((ENDIAN_MAGIC&0xFF)<<8))

typedef enum
{
#ifndef WIN32                // note: first value is default name format for new databases
   DB_FMT_UNIX,
   DB_FMT_DOS,
#else
   DB_FMT_DOS,
   DB_FMT_UNIX,
#endif
   DB_FMT_COUNT
} DB_FMT_TYPE;

#ifdef WIN32
#define PATH_SEPARATOR       '\\'
#define PATH_SEPARATOR_STR   "\\"
#define PATH_ROOT            "\\"
#define DUMP_NAME_DEF_FMT    DB_FMT_DOS
#else
#define PATH_SEPARATOR       '/'
#define PATH_SEPARATOR_STR   "/"
#define PATH_ROOT            "/"
#define DUMP_NAME_TMP_SUFX   ".tmp"
#define DUMP_NAME_DEF_FMT    DB_FMT_UNIX
#endif

#define DUMP_NAME_EXP_DOS   "%*[nN]%*[xX]%*[tT]%*[vV]%04X.%*[eE]%*[pP]%*[gG]"
#define DUMP_NAME_FMT_DOS    "nxtv%04x.epg"
#define DUMP_NAME_PAT_DOS    "nxtv*.epg"
#define DUMP_NAME_LEN_DOS    (8+1+3)         // for sscanf() only - for malloc use MAX_LEN!

#define DUMP_NAME_EXP_UNIX  "%*[nN]%*[xX]%*[tT]%*[vV]%*[dD]%*[bB]-%04X"
#define DUMP_NAME_FMT_UNIX   "nxtvdb-%04x"
#define DUMP_NAME_PAT_UNIX   "nxtvdb-*."
#define DUMP_NAME_LEN_UNIX   (6+1+4)

#define DUMP_NAME_MAX_LEN    25   // = MAX(LEN_WIN32,LEN_UNIX) + DUMP_NAME_TMP_SUFX

#ifndef O_BINARY
#define O_BINARY       0          // for M$-Windows only
#endif

#define DUMP_ENCODING_ISO_8859_1  0
#define DUMP_ENCODING_UTF8        0xFFFFFFFF
#ifdef USE_UTF8
#define DUMP_ENCODING_SUP         DUMP_ENCODING_UTF8
#else
#define DUMP_ENCODING_SUP         DUMP_ENCODING_ISO_8859_1
#endif


typedef struct
{
   uchar     magic[MAGIC_STR_LEN];  // file header
   uint16_t  endianMagic;         // magic to recognize big/little endian
   uint32_t  compatVersion;       // version of oldest compatible software
   uint32_t  swVersion;           // version of this software

   time_t    lastPiDate;          // stop time of last PI in db
   time_t    firstPiDate;         // start time of first PI in db
   time_t    lastAiUpdate;        // time when the last AI block was received
   uint32_t  cni;                 // CNI of EPG provider
   uint32_t  pageNo;              // last used ttx page
   uint32_t  tunerFreq;           // tuner frequency of provider's channel
   uint32_t  appId;               // EPG application ID from BI block

   uint32_t  encoding;            // encoding: one of DUMP_ENCODING_*
   uchar     reserved[24];        // unused space for future use; set to 0
} EPGDBSAV_HEADER;


#define BLOCK_TYPE_DEFECT_PI  0x77

// max size is much larger than any block will ever become
// but should be small enough so it can safely be malloc'ed during reload
#define EPGDBSAV_MAX_BLOCK_SIZE  30000

// default time after which PI which are no longer part of the stream are discarded
// (note: PI which still have a valid block no are not discarded until acq is started)
#define EPGDBSAV_DEFAULT_EXPIRE_TIME  (4*60*60)

// result codes for reload and peek (ordered by increasing user relevance)
typedef enum
{
   EPGDB_RELOAD_OK,            // no error
   EPGDB_RELOAD_INTERNAL,      // unexpected error
   EPGDB_RELOAD_CORRUPT,       // unexpected content
   EPGDB_RELOAD_WRONG_MAGIC,   // magic not found or file too short
   EPGDB_RELOAD_ACCESS,        // file open failed
   EPGDB_RELOAD_ENDIAN,        // big/little endian conflict
   EPGDB_RELOAD_ENCODING,      // incompatible encoding
   EPGDB_RELOAD_INT_WIDTH,     // incompatible integer width
   EPGDB_RELOAD_VERSION,       // incompatible version
   EPGDB_RELOAD_MERGE,         // invalid merge config
   EPGDB_RELOAD_EXIST,         // file does not exist
   EPGDB_RELOAD_XML_CNI,       // XMLTV CNI but path unknown
   EPGDB_RELOAD_XML_MASK = 0x40000000 // XMLTV specific error
} EPGDB_RELOAD_RESULT;

// macro to compare severity of reload errors
// (to be used if multiple errors occur in a loop across all databases)
#define RELOAD_ERR_WORSE(X,Y)  ((X)>(Y))

// ---------------------------------------------------------------------------
// header format of database written by 32-bit and 64-bit version of nxtvepg
// - included for error detection only
//
typedef struct
{
   uchar     magic[MAGIC_STR_LEN];
   uint16_t  endianMagic;
   uint32_t  compatVersion;
   uint32_t  swVersion;

   uint32_t  lastPiDate;
   uint32_t  firstPiDate;
   uint32_t  lastAiUpdate;
   uint32_t  cni;
   uint32_t  pageNo;
   uint32_t  tunerFreq;
   uint32_t  appId;

   uint32_t  encoding;
   uchar     reserved[24];
} EPGDBSAV_HEADER_TIME_T_32;

typedef struct
{
   uchar     magic[MAGIC_STR_LEN];
   uint16_t  endianMagic;
   uint32_t  compatVersion;
   uint32_t  swVersion;

   uint32_t  lastPiDate;        // not used, so use 2*32 as placeholder for 64
   uint32_t  __lastPiDate_2;
   uint32_t  firstPiDate;
   uint32_t  __firstPiDate_2;
   uint32_t  lastAiUpdate;
   uint32_t  __lastAiUpdate_2;
   uint32_t  cni;
   uint32_t  pageNo;
   uint32_t  tunerFreq;
   uint32_t  appId;

   uint32_t  encoding;
   uchar     reserved[24];
} EPGDBSAV_HEADER_TIME_T_64;

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
EPGDB_CONTEXT * EpgDbReload( uint cni, EPGDB_RELOAD_RESULT * pResult, time_t * pMtime );
EPGDB_CONTEXT * EpgDbLoadDemo( const char * pDemoDatabase, EPGDB_RELOAD_RESULT * pResult, time_t * pMtime );
void EpgDbReloadScan( void (*pCb)(uint cni, const char * pPath, sint mtime) );
void EpgDbSavSetPiExpireDelay( time_t expireDelayPi );
bool EpgDbSavSetupDir( const char * pDirPath, bool isDemoMode );
bool EpgDbDumpCheckFileHeader( const char * pFilename );
bool EpgDbDumpGetDirAndCniFromArg( char * pArg, const char ** ppDirPath, uint * pCni );

EPGDB_CONTEXT * EpgDbPeek( uint cni, EPGDB_RELOAD_RESULT * pResult, time_t * pMtime );
bool EpgDbDumpUpdateHeader( uint cni, uint freq );
uint EpgDbReadFreqFromDefective( uint cni );
time_t EpgReadAiUpdateTime( uint cni );
uint EpgDbRemoveDatabaseFile( uint cni );

#endif  // __EPGDBSAV_H
