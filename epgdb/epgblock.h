/*
 *  Nextview EPG block database
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
 *  Description:
 *
 *    Defines the C structures that keep the acquired EPG data.
 *    Since these structures contain elements of variable length
 *    (and great maximum size) those elements are appended to the
 *    end of the base structure, and addressed by offsets to the
 *    start of the structure. Do always use the provided macros
 *    to access them.
 */

#ifndef __EPGBLOCK_H
#define __EPGBLOCK_H


// ---------------------------------------------------------------------------
//    AI Block
// ---------------------------------------------------------------------------

typedef struct
{
   uint32_t  netCni;
   int8_t    lto;          // TODO: always 120 for XMLTV
   uint8_t   language;     // TODO: always 1 for XMLTV
   uint8_t   dayCount;

   uint16_t  off_name;
} AI_NETWOP;

typedef struct
{
   uint8_t   version;      // TODO always 0 for XMLTV
   uint8_t   netwopCount;

   uint16_t  off_serviceNameStr;
   uint16_t  off_netwops;

   uint16_t  reserved_0;  // padding to align following AI_NETWOP to 32-bit boundary
} AI_BLOCK;

#define AI_GET_NETWOPS(X)       ((const AI_NETWOP *)((uint16_t *)(X)+(X)->off_netwops/sizeof(uint16_t)))
#define AI_GET_NETWOP_N(X,N)    (&((const AI_NETWOP *)((uint16_t *)(X)+(X)->off_netwops/sizeof(uint16_t)))[N])
#define AI_GET_SERVICENAME(X)   ((const char *)(X)+(X)->off_serviceNameStr)
#define AI_GET_STR_BY_OFF(X,O)  ((const char *)(X)+(O))
#define AI_GET_NETWOP_NAME(X,N) ((const char *)(X)+AI_GET_NETWOPS(X)[N].off_name)
#define AI_GET_NET_CNI(N)       ((uint)((N)->netCni))
#define AI_GET_NET_CNI_N(X,N)   AI_GET_NET_CNI(AI_GET_NETWOP_N((X),(N)))


// ---------------------------------------------------------------------------
//    PI Block
// ---------------------------------------------------------------------------

#define PI_MAX_THEME_COUNT      7

#define PI_FEATURE_VIDEO_HD        0x400  // not in Nextview EPG
#define PI_FEATURE_VIDEO_BW        0x200  // not in Nextview EPG
#define PI_FEATURE_SUBTITLES       0x100
#define PI_FEATURE_REPEAT          0x080
#define PI_FEATURE_LIVE            0x040
#define PI_FEATURE_ENCRYPTED       0x020
//#define PI_FEATURE_DIGITAL        0x010
#define PI_FEATURE_PAL_PLUS        0x008
#define PI_FEATURE_FMT_WIDE        0x004
#define PI_FEATURE_SOUND_MASK      0x003
#define PI_FEATURE_SOUND_MONO      0x000
#define PI_FEATURE_SOUND_2CHAN     0x001
#define PI_FEATURE_SOUND_STEREO    0x002
#define PI_FEATURE_SOUND_SURROUND  0x003

// for merged PI: indices of source databases
typedef uint8_t EPGDB_MERGE_SRC;

typedef struct
{
  uint8_t   netwop_no;
  time32_t  start_time;
  time32_t  stop_time;
  uint32_t  pil;
  uint16_t  feature_flags;
  uint8_t   parental_rating;
  uint8_t   editorial_rating;

  uint8_t   no_themes;
  uint8_t   no_descriptors;              // used by merged db
  uint8_t   themes[PI_MAX_THEME_COUNT];

  uint16_t  off_title;
  uint16_t  off_desc_text;
  uint16_t  off_descriptors;
} PI_BLOCK;

#define PI_GET_TITLE(X)        ((const char*)(X)+((X)->off_title))
#define PI_HAS_DESC_TEXT(X)    ((bool)((X)->off_desc_text != 0))
#define PI_GET_DESC_TEXT(X)    ((const char*)(X)+((X)->off_desc_text))
#define PI_GET_STR_BY_OFF(X,O) ((const char*)(X)+(O))
#define PI_GET_DESCRIPTORS(X)  ((const EPGDB_MERGE_SRC*)((uchar*)(X)+((X)->off_descriptors)))


// ---------------------------------------------------------------------------
//    OI Block
// ---------------------------------------------------------------------------

typedef struct
{
   uint16_t  off_header;
   uint16_t  off_message;
} OI_BLOCK;

#define OI_GET_HEADER(X)       ((const char*)(X)+((X)->off_header))
#define OI_HAS_HEADER(X)       ((bool)((X)->off_header != 0))
#define OI_GET_MESSAGE(X)      ((const char*)(X)+((X)->off_message))
#define OI_HAS_MESSAGE(X)      ((bool)((X)->off_message != 0))


// ----------------------------------------------------------------------------
// EPG block types (internal redefinition; ordering is relevant!)

// this is the maximum number of netwops an AI block can carry
// (note: value 0xFF is reserved as invalid netwop index)
// TODO may be too low for Digital TV
#define MAX_NETWOP_COUNT    254

typedef enum
{
   BLOCK_TYPE_OI,
   BLOCK_TYPE_AI,
   BLOCK_TYPE_PI,
} BLOCK_TYPE;

typedef union
{
   AI_BLOCK   ai;
   PI_BLOCK   pi;
   OI_BLOCK   oi;
} EPGDB_BLOCK_UNION;

typedef struct EPGDB_BLOCK_STRUCT
{
   struct EPGDB_BLOCK_STRUCT *pNextBlock;        // next block in order of start time
   struct EPGDB_BLOCK_STRUCT *pPrevBlock;        // previous block in order of start time
   struct EPGDB_BLOCK_STRUCT *pNextNetwopBlock;  // next block of the same network in order of start time
   struct EPGDB_BLOCK_STRUCT *pPrevNetwopBlock;  // previous block of the same network in order of start time
   uint32_t     size;               // actual size of the union; may be greater than it's sizeof()
   uint8_t      version;            // AI version at the time of acquisition of this block
   uint16_t     parityErrCnt;       // parity error count for string segment
   time32_t     acqTimestamp;       // time when the block was received last
   BLOCK_TYPE   type;

   const EPGDB_BLOCK_UNION   blk;   // the actual data
} EPGDB_BLOCK;

#define BLK_UNION_OFF    ((uint)(sizeof(EPGDB_BLOCK) - sizeof(EPGDB_BLOCK_UNION)))

#define BLK_HEAD_PTR_OFF_MEM    ((uint)(4*sizeof(void*)))
#if defined (USE_32BIT_COMPAT)  // by default keep 64-bit incompatible so that old 64-bit DBs remain compatible
#define BLK_HEAD_PTR_OFF_FILE   ((uint)(4*sizeof(uint32_t)))
#else
#define BLK_HEAD_PTR_OFF_FILE   BLK_HEAD_PTR_OFF_MEM
#endif
#define BLK_HEAD_SIZE_DATA      (BLK_UNION_OFF - BLK_HEAD_PTR_OFF_MEM)
#define BLK_HEAD_SIZE_FILE      (BLK_UNION_OFF + BLK_HEAD_PTR_OFF_FILE - BLK_HEAD_PTR_OFF_MEM)
#define BLK_HEAD_SIZE_MEM       BLK_UNION_OFF
#define BLK_HEAD_PTR_FILE(P)    ((EPGDB_BLOCK*)((char*)(P) + BLK_HEAD_PTR_OFF_FILE - BLK_HEAD_PTR_OFF_MEM))

// ----------------------------------------------------------------------------
// Event callback for handling incoming PI in the GUI
//
typedef enum
{
   EPGDB_PI_INSERTED,
   EPGDB_PI_PRE_UPDATE,    // no longer sent
   EPGDB_PI_POST_UPDATE,   // no longer sent
   EPGDB_PI_REMOVED,
   EPGDB_PI_RESYNC
} EPGDB_PI_ACQ_EVENT;

struct EPGDB_CONTEXT_STRUCT;  // forward declaration only for following pointer reference

typedef bool (EPGDB_PI_ACQ_CB) ( const struct EPGDB_CONTEXT_STRUCT * usedDbc, EPGDB_PI_ACQ_EVENT event,
                                 const PI_BLOCK * pPiBlock, const PI_BLOCK * pObsolete );

// ----------------------------------------------------------------------------
// declaration of database context, which keeps lists of all blocks
//

typedef struct EPGDB_CONTEXT_STRUCT
{
   uint   provCni;                  // provider CNI
   uint   lockLevel;                // number of database locks on this context

   bool   merged;                   // Flag for merged db
   void   *pMergeContext;           // Pointer to merge parameters

   EPGDB_BLOCK *pAiBlock;
   EPGDB_BLOCK *pOiBlock;
   EPGDB_BLOCK *pFirstPi, *pLastPi;
   EPGDB_BLOCK *pObsoletePi;
   EPGDB_BLOCK *pFirstNetwopPi[MAX_NETWOP_COUNT];

   EPGDB_PI_ACQ_CB *pPiAcqCb;
} EPGDB_CONTEXT;

// ----------------------------------------------------------------------------
// Macros for provider database CNI

// max number of databases that can be merged into one
#define MAX_MERGED_DB_COUNT  63

// pseudo CNIs
#define MERGED_PROV_CNI        0x00FF

#define XMLTV_PROV_CNI_BASE    0x00010000
#define XMLTV_PROV_CNI_DELTA   0x00010000
#define XMLTV_NET_CNI_MASK     (XMLTV_PROV_CNI_DELTA - 1)

#define IS_PSEUDO_CNI(CNI)     ((CNI)==MERGED_PROV_CNI)

// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
EPGDB_BLOCK * EpgBlockCreate( BLOCK_TYPE type, uint size, time_t mtime );

bool EpgBlockCheckConsistancy( EPGDB_BLOCK * pBlock );

void EpgLtoInit( void );
sint EpgLtoGet( time_t when );

#endif // __EPGBLOCK_H
