/*
 *  Nextview EPG block database
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
 *    Defines the C structures that keep the information of
 *    the acquired Nextview data blocks. All Nextview bit fields
 *    are immediately transformed into these C structures upon
 *    reception. Since these structures contain elements of
 *    variable length (and great maximum size) those elements
 *    are appended to the end of the base structure, and addressed
 *    by offsets to the start of the structure. Do always use
 *    the provided macros to access them.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgblock.h,v 1.29 2001/08/31 16:45:38 tom Exp tom $
 */

#ifndef __EPGBLOCK_H
#define __EPGBLOCK_H


// descriptors of PI,OI,NI,MI blocks
typedef struct
{
   uchar type;            // contains block type of according table (LI,TI)
   uchar id;              // index into that table
   //uchar eval;          // unused
} DESCRIPTOR;

// ---------------------------------------------------------------------------
//    BI Block
// ---------------------------------------------------------------------------

typedef struct {
   uint  app_id;
} BI_BLOCK;

#define EPG_ILLEGAL_APPID    0
#define EPG_DEFAULT_APPID    1

// ---------------------------------------------------------------------------
//    AI Block
// ---------------------------------------------------------------------------

typedef uint CNI_VAL;

typedef struct
{
   CNI_VAL cni;
   uint startNo;
   uint stopNo;
   uint stopNoSwo;
   uint addInfo;
   signed char lto;
   uchar dayCount;
   uchar alphabet;
   uchar nameLen;

   uint off_name;
} AI_NETWOP;

typedef struct
{
   uchar version;
   uchar version_swo;
   uchar netwopCount;
   uchar thisNetwop;
   uchar serviceNameLen;
   uint  niCount;
   uint  oiCount;
   uint  miCount;
   uint  niCountSwo;
   uint  oiCountSwo;
   uint  miCountSwo;

   uint off_serviceNameStr;
   uint off_netwops;
} AI_BLOCK;

#define AI_GET_NETWOPS(X)       ((AI_NETWOP *)((uchar *)(X)+(X)->off_netwops))
#define AI_GET_NETWOP_N(X,N)    (&((AI_NETWOP *)((uchar *)(X)+(X)->off_netwops))[N])
#define AI_GET_SERVICENAME(X)   ((uchar *)(X)+(X)->off_serviceNameStr)
#define AI_GET_STR_BY_OFF(X,O)  ((uchar *)(X)+(O))
#define AI_GET_NETWOP_NAME(X,N) ((uchar *)(X)+AI_GET_NETWOPS(X)[N].off_name)
#define AI_GET_CNI(X)           (AI_GET_NETWOP_N(X,(X)->thisNetwop)->cni)

// ---------------------------------------------------------------------------
//    PI Block
// ---------------------------------------------------------------------------

/*
   length of fields in bits

   name                   len  bitoff byte
   ---------------------- ---- ------ ----
   application_id          5     0
   block_size              11     
   checksum                8     0
   control_block_size      10    0
   datatype_id             6     2
   CA_mode                 2     0
   _copyright              1     2
   reserved_for_future_use 1     3
   block_no                16    4      5
   feature_flags           12    4      7
   netwop_no               8     0      9
   start_time              16    0     10
   start_date              16    0     12
   stop_time               16    0     14
   _pil                    20    0     16
   parental_rating         4     4     18
   editorial_rating        3     0     20
   no_themes               3     3     21
   no_sortcrit             3     6     21
   descriptor_looplength   6     1     22
*/

enum {
   EPG_STR_TYPE_TRANSP_SHORT,
   EPG_STR_TYPE_TRANSP_LONG,
   EPG_STR_TYPE_TTX_STR,
   EPG_STR_TYPE_TTX_RECT,
   EPG_STR_TYPE_TTX_PAGE
};

/*
typedef struct {
   uint type:6;
   uint id:6;
   uint eval:8;
} DESCRIPTOR_COMPRESSED;

typedef struct {
  uint block_no:                16;
  uint feature_flags:           12;
  uint netwop_no:               8;
  uint start_time:              16;
  uint start_date:              16;
  uint stop_time:               16;
  uint _pil:                    20;
  uint parental_rating:         4;
  uint editorial_rating:        3;
  uint no_themes:               3;
  uint no_sortcrit:             3;
  uint no_descriptors:          6;
  uint background_reuse:        1;
  uint background_ref:          16;
  uint title_length:            8;
  uint short_info_length:       8;
  uint long_info_type:          3;
  uint long_info_length:        10;
  uchar themes[7];
  uchar sortcrits[7];
  //DESCRIPTOR descriptors[63];
} PI_BLOCK_COMPRESSED;
*/

#define PI_MAX_THEME_COUNT      7
#define PI_MAX_SORTCRIT_COUNT   7

typedef struct {
  uint   block_no;
  uchar  netwop_no;
  uint   feature_flags;
  time_t start_time;
  time_t stop_time;
  uint   pil;
  uchar  parental_rating;
  uchar  editorial_rating;
  uint   background_ref;
  uchar  background_reuse;
  uchar  long_info_type;

  uchar  no_themes;
  uchar  no_sortcrit;
  uchar  no_descriptors;
  uchar  themes[PI_MAX_THEME_COUNT];
  uchar  sortcrits[PI_MAX_SORTCRIT_COUNT];

  uint   off_title;
  uint   off_short_info;
  uint   off_long_info;
  uint   off_descriptors;
} PI_BLOCK;

#define PI_GET_TITLE(X)        ((uchar*)(X)+((X)->off_title))
#define PI_HAS_SHORT_INFO(X)   ((bool)((X)->off_short_info != 0))
#define PI_GET_SHORT_INFO(X)   ((uchar*)(X)+((X)->off_short_info))
#define PI_HAS_LONG_INFO(X)    ((bool)((X)->off_long_info != 0))
#define PI_GET_LONG_INFO(X)    ((uchar*)(X)+((X)->off_long_info))
#define PI_GET_STR_BY_OFF(X,O) ((uchar *)(X)+(O))
#define PI_GET_DESCRIPTORS(X)  ((DESCRIPTOR*)((uchar*)(X)+((X)->off_descriptors)))


// ---------------------------------------------------------------------------
//    NI Block
// ---------------------------------------------------------------------------

/*
typedef struct
{
   uint block_no         :16;
   uint header_size      :2;
   uint no_events        :4;
   uint msg_size         :3;
   uint no_descriptors   :6;
   uint header_length    :8;
   uint msg_attrib       :8;
} NI_BLOCK_COMPRESSED;
*/


#define EV_ATTRIB_KIND_REL_DATE     0x02  // relative date date_offset 
#define EV_ATTRIB_KIND_PROGNO_START 0x10  // first program prog_offset 
#define EV_ATTRIB_KIND_PROGNO_STOP  0x11  // last program prog_offset 
#define EV_ATTRIB_KIND_NETWOP       0x18  // network operator netwop_no 
#define EV_ATTRIB_KIND_THEME        0x20  // theme classes: 0x20-0x27
#define EV_ATTRIB_KIND_SORTCRIT     0x30  // sorting crit.: 0x30-0x37
#define EV_ATTRIB_KIND_EDITORIAL    0x40  // editorial rating editorial_rating 
#define EV_ATTRIB_KIND_PARENTAL     0x41  // parental rating parental_rating 
#define EV_ATTRIB_KIND_START_TIME   0x80  // start time time_code 
#define EV_ATTRIB_KIND_STOP_TIME    0x81  // stop time time_code 
#define EV_ATTRIB_KIND_FEATURES     0xC0  // features feature_flags 
#define EV_ATTRIB_KIND_LANGUAGE     0xC8  // language language 
#define EV_ATTRIB_KIND_SUBT_LANG    0xC9  // subtitle language subtitle_language others reserved for future expansion

typedef struct
{
   uchar kind;
   ulong data;
} EV_ATTRIB_DATA;

/*
typedef struct
{
   uint next_id          :16;
   uint next_type        :4;
   uint no_attribs       :4;
   uint str_len          :8;
   EV_ATTRIB_DATA unit[7];
} EVENT_ATTRIB_COMPRESSED;
*/

#define NEXT_TYPE_NI  1
#define NEXT_TYPE_OI  2

typedef struct
{
   uint  next_id;
   uchar next_type;
   uchar no_attribs;
   uint  off_evstr;
   EV_ATTRIB_DATA unit[7];
} EVENT_ATTRIB;

#define NI_MAX_EVENT_COUNT   16

typedef struct
{
   uint  block_no;
   uchar header_size;
   uchar msg_size;
   uchar no_events;
   uchar no_descriptors;
   uint  msg_attrib;

   uint  off_events;
   uint  off_header;
   uint  off_descriptors;
} NI_BLOCK;

#define NI_GET_HEADER(X)       ((uchar*)(X)+((X)->off_header))
#define NI_HAS_HEADER(X)       ((bool)(((X)->off_header) != 0))
#define NI_GET_EVENTS(X)       ((EVENT_ATTRIB*)((uchar*)(X)+((X)->off_events)))
#define NI_GET_EVENT_STR(X,Y)  ((uchar*)(X)+((Y)->off_evstr))
#define NI_GET_DESCRIPTORS(X)  ((DESCRIPTOR*)((uchar*)(X)+((X)->off_descriptors)))

// ---------------------------------------------------------------------------
//    MI Block
// ---------------------------------------------------------------------------

/*
typedef struct
{
   uint block_no         :16;
   uint no_descriptors   :6;
   uint msg_length       :10;
} MI_BLOCK_COMPRESSED;
*/

typedef struct
{
   uint  block_no;
   uchar no_descriptors;

   uint  off_message;
   uint  off_descriptors;
} MI_BLOCK;

#define MI_GET_MESSAGE(X)      ((uchar*)(X)+((X)->off_message))
#define MI_HAS_MESSAGE(X)      ((bool)((X)->off_message != 0))
#define MI_GET_DESCRIPTORS(X)  ((DESCRIPTOR*)((uchar*)(X)+((X)->off_descriptors)))


// ---------------------------------------------------------------------------
//    OI Block
// ---------------------------------------------------------------------------

/*
typedef struct
{
   uint block_no         :16;
   uint msg_attrib       :8;
   uint header_size      :3;
   uint msg_size         :3;
   uint no_descriptors   :6;
   uint msg_length       :10;
   uint header_length    :8;
} OI_BLOCK_COMPRESSED;
*/

#define MSG_ATTRIB_VAL_USE_SHORT_INFO   0

typedef struct
{
   uint  block_no;
   uchar msg_attrib;
   uchar header_size;
   uchar msg_size;
   uchar no_descriptors;

   uint  off_header;
   uint  off_message;
   uint  off_descriptors;
} OI_BLOCK;

#define OI_GET_HEADER(X)       ((uchar*)(X)+((X)->off_header))
#define OI_HAS_HEADER(X)       ((bool)((X)->off_header != 0))
#define OI_GET_MESSAGE(X)      ((uchar*)(X)+((X)->off_message))
#define OI_HAS_MESSAGE(X)      ((bool)((X)->off_message != 0))
#define OI_GET_DESCRIPTORS(X)  ((DESCRIPTOR*)((uchar*)(X)+((X)->off_descriptors)))


// ---------------------------------------------------------------------------
//    LI Block
// ---------------------------------------------------------------------------

#define LI_MAX_DESC_COUNT   64
#define LI_MAX_LANG_COUNT   16

typedef struct
{
   uchar id;
   uchar lang_count;
   uchar lang[3][LI_MAX_LANG_COUNT];
} LI_DESC;

typedef struct
{
   uint  block_no;
   uchar netwop_no;
   uchar desc_no;

   uint  off_desc;
} LI_BLOCK;

#define LI_GET_DESC(X)       ((LI_DESC*)((uchar*)(X)+((X)->off_desc)))

// ---------------------------------------------------------------------------
//    TI Block
// ---------------------------------------------------------------------------

#define TI_MAX_DESC_COUNT   64
#define TI_MAX_LANG_COUNT   16

typedef struct
{
   uchar lang[3];
   uint  page;
   uint  subpage;
} TI_SUBT;

typedef struct
{
   uchar   id;
   uchar   subt_count;
   TI_SUBT subt[TI_MAX_LANG_COUNT];
} TI_DESC;

typedef struct
{
   uint  block_no;
   uchar netwop_no;
   uchar desc_no;

   uint  off_desc;
} TI_BLOCK;

#define TI_GET_DESC(X)       ((TI_DESC*)((uchar*)(X)+((X)->off_desc)))

// ----------------------------------------------------------------------------
// EPG block types (internal redefinition; ordering is relevant!)

// this is the maximum number of netwops an AI block can carry
#define MAX_NETWOP_COUNT    80

typedef enum
{
   BLOCK_TYPE_NI
  ,BLOCK_TYPE_OI
  ,BLOCK_TYPE_MI
  ,BLOCK_TYPE_LI
  ,BLOCK_TYPE_TI
#define BLOCK_TYPE_GENERIC_COUNT  BLOCK_TYPE_BI
  ,BLOCK_TYPE_BI
  ,BLOCK_TYPE_AI
  ,BLOCK_TYPE_PI
#define BLOCK_TYPE_COUNT          (BLOCK_TYPE_PI + 1)
#define BLOCK_TYPE_INVALID        0xff
} BLOCK_TYPE;

typedef struct
{
   uint       block_no;
   uchar      netwop_no;
   // Rest des Blocks folgt hier
} GENERIC_BLK;

typedef union
{
   BI_BLOCK   bi;
   AI_BLOCK   ai;
   PI_BLOCK   pi;
   NI_BLOCK   ni;
   MI_BLOCK   mi;
   OI_BLOCK   oi;
   LI_BLOCK   li;
   TI_BLOCK   ti;
   GENERIC_BLK all;
} EPGDB_BLOCK_UNION;

typedef struct EPGDB_BLOCK_STRUCT
{
   struct EPGDB_BLOCK_STRUCT *pNextBlock;        // naechster Block in Verkettung nach Startzeit
   struct EPGDB_BLOCK_STRUCT *pPrevBlock;        // vorheriger Block in Verkettung nach Startzeit
   struct EPGDB_BLOCK_STRUCT *pNextNetwopBlock;  // naechster Block desselben Netwops
   struct EPGDB_BLOCK_STRUCT *pPrevNetwopBlock;  // vorheriger Block desselben Netwops
   uint         size;               // tatsaechliche Groesse der Union
   uchar        version;            // AI-Version zum Zeitpkt. der Acq.
   uchar        stream;             // Stream aus dem Block akquiriert wurde
   uchar        mergeCount;         // count of databases from which this block was merged
   uchar        origChkSum;
   ushort       origBlkLen;
   ushort       parityErrCnt;       // parity error count for string segment
   time_t       acqTimestamp;       // time when the block was added to the database
   uint         acqRepCount;        // reception count with same version and size
   BLOCK_TYPE   type;

   const EPGDB_BLOCK_UNION   blk;   // die eigentlichen Daten
} EPGDB_BLOCK;

#define BLK_UNION_OFF    (sizeof(EPGDB_BLOCK) - sizeof(EPGDB_BLOCK_UNION))

// ----------------------------------------------------------------------------
// declaration of database context, which keeps lists of all blocks
//
typedef struct EPGDB_CONTEXT_STRUCT
{
   struct EPGDB_CONTEXT_STRUCT *pNext;

   uint   refCount;                 // context reference counter
   uint   lockLevel;                // number of locks on this context
   bool   modified;                 // modified by acquisition
   time_t lastAiUpdate;             // timestamp of last AI reception

   bool   merged;                   // Flag for merged db
   void   *pMergeContext;           // Pointer to merge parameters

   uint   pageNo;                   // Teletext page for acq
   ulong  tunerFreq;                // Frequency for acq
   uint   appId;                    // BI block ID

   EPGDB_BLOCK *pAiBlock;
   EPGDB_BLOCK *pFirstPi, *pLastPi;
   EPGDB_BLOCK *pObsoletePi;
   EPGDB_BLOCK *pFirstNetwopPi[MAX_NETWOP_COUNT];
   EPGDB_BLOCK *pFirstGenericBlock[BLOCK_TYPE_GENERIC_COUNT];
} EPGDB_CONTEXT;

typedef struct
{
   ulong  ai;
   ulong  curVersion;
   ulong  allVersions;
   ulong  expired;
   ulong  defective;
   ulong  sinceAcq;
   double variance;
   double avgAcqRepCount;
} EPGDB_BLOCK_COUNT;

// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
EPGDB_BLOCK * EpgBlockConvertPi(const uchar *pCtrl, uint ctrlLen, uint strLen);
EPGDB_BLOCK * EpgBlockConvertAi(const uchar *pCtrl, uint ctrlLen, uint strLen);
EPGDB_BLOCK * EpgBlockConvertOi(const uchar *pCtrl, uint ctrlLen, uint strLen);
EPGDB_BLOCK * EpgBlockConvertNi(const uchar *pCtrl, uint ctrlLen, uint strLen);
EPGDB_BLOCK * EpgBlockConvertMi(const uchar *pCtrl, uint ctrlLen, uint strLen);
EPGDB_BLOCK * EpgBlockConvertLi(const uchar *pCtrl, uint ctrlLen, uint strLen);
EPGDB_BLOCK * EpgBlockConvertTi(const uchar *pCtrl, uint ctrlLen, uint strLen);
EPGDB_BLOCK * EpgBlockConvertBi(const uchar *pCtrl, uint ctrlLen);
EPGDB_BLOCK * EpgBlockCreate( uchar type, uint size );

uint EpgBlockBcdToMoD( uint BCD );
void EpgBlockSetAlphabets( const AI_BLOCK *pAiBlock );
void EpgLtoInit( void );
sint EpgLtoGet( time_t when );

#endif // __EPGBLOCK_H
