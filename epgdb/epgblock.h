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
 *  $Id: epgblock.h,v 1.42 2002/05/11 15:43:36 tom Exp tom $
 */

#ifndef __EPGBLOCK_H
#define __EPGBLOCK_H


// descriptors of PI,OI,NI,MI blocks
typedef struct
{
   uint8_t   type;           // contains block type of according table (LI,TI)
   uint8_t   id;             // index into that table
   //uint8_t  eval;          // unused
} DESCRIPTOR;

// ---------------------------------------------------------------------------
//    BI Block
// ---------------------------------------------------------------------------

typedef struct {
   uint32_t  app_id;
} BI_BLOCK;

#define EPG_ILLEGAL_APPID    0
#define EPG_DEFAULT_APPID    1

// ---------------------------------------------------------------------------
//    AI Block
// ---------------------------------------------------------------------------

typedef struct
{
   uint16_t  cni;
   uint16_t  startNo;
   uint16_t  stopNo;
   uint16_t  stopNoSwo;
   uint16_t  addInfo;
   int8_t    lto;
   uint8_t   dayCount;
   uint8_t   alphabet;
   uint8_t   reserved_1;

   uint16_t  off_name;
} AI_NETWOP;

typedef struct
{
   uint8_t   version;
   uint8_t   version_swo;
   uint8_t   netwopCount;
   uint8_t   thisNetwop;
   uint16_t  niCount;
   uint16_t  oiCount;
   uint16_t  miCount;
   uint16_t  niCountSwo;
   uint16_t  oiCountSwo;
   uint16_t  miCountSwo;

   uint16_t  off_serviceNameStr;
   uint16_t  off_netwops;
} AI_BLOCK;

#define AI_GET_NETWOPS(X)       ((const AI_NETWOP *)((uchar *)(X)+(X)->off_netwops))
#define AI_GET_NETWOP_N(X,N)    (&((const AI_NETWOP *)((uchar *)(X)+(X)->off_netwops))[N])
#define AI_GET_SERVICENAME(X)   ((const uchar *)(X)+(X)->off_serviceNameStr)
#define AI_GET_STR_BY_OFF(X,O)  ((const uchar *)(X)+(O))
#define AI_GET_NETWOP_NAME(X,N) ((const uchar *)(X)+AI_GET_NETWOPS(X)[N].off_name)
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

typedef struct
{
  uint16_t  block_no;
  uint8_t   netwop_no;
  bool      block_no_in_ai;
  time_t    start_time;
  time_t    stop_time;
  uint32_t  pil;
  uint32_t  series_code;
  uint16_t  feature_flags;
  uint16_t  background_ref;
  uint8_t   parental_rating;
  uint8_t   editorial_rating;
  uint8_t   background_reuse;

  uint8_t   no_themes;
  uint8_t   no_sortcrit;
  uint8_t   no_descriptors;
  uint8_t   themes[PI_MAX_THEME_COUNT];
  uint8_t   sortcrits[PI_MAX_SORTCRIT_COUNT];

  uint16_t  off_title;
  uint16_t  off_short_info;
  uint16_t  off_long_info;
  uint16_t  off_descriptors;
} PI_BLOCK;

#define PI_GET_TITLE(X)        ((const uchar*)(X)+((X)->off_title))
#define PI_HAS_SHORT_INFO(X)   ((bool)((X)->off_short_info != 0))
#define PI_GET_SHORT_INFO(X)   ((const uchar*)(X)+((X)->off_short_info))
#define PI_HAS_LONG_INFO(X)    ((bool)((X)->off_long_info != 0))
#define PI_GET_LONG_INFO(X)    ((const uchar*)(X)+((X)->off_long_info))
#define PI_GET_STR_BY_OFF(X,O) ((const uchar*)(X)+(O))
#define PI_GET_DESCRIPTORS(X)  ((const DESCRIPTOR*)((uchar*)(X)+((X)->off_descriptors)))


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
   uint8_t  kind;
   uint32_t data;
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

#define NI_MAX_EVENT_COUNT   15
#define NI_MAX_ATTRIB_COUNT  15

typedef struct
{
   uint16_t  next_id;
   uint8_t   next_type;
   uint8_t   no_attribs;
   uint16_t  off_evstr;
   EV_ATTRIB_DATA unit[NI_MAX_ATTRIB_COUNT];
} EVENT_ATTRIB;

typedef struct
{
   uint16_t  block_no;
   uint8_t   header_size;
   uint8_t   msg_size;
   uint8_t   no_events;
   uint8_t   no_descriptors;
   uint16_t  msg_attrib;

   uint16_t  off_events;
   uint16_t  off_header;
   uint16_t  off_descriptors;
} NI_BLOCK;

#define NI_GET_HEADER(X)       ((const uchar*)(X)+((X)->off_header))
#define NI_HAS_HEADER(X)       ((bool)(((X)->off_header) != 0))
#define NI_GET_EVENTS(X)       ((const EVENT_ATTRIB*)((uchar*)(X)+((X)->off_events)))
#define NI_GET_EVENT_STR(X,Y)  ((const uchar*)(X)+((Y)->off_evstr))
#define NI_GET_DESCRIPTORS(X)  ((const DESCRIPTOR*)((uchar*)(X)+((X)->off_descriptors)))

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
   uint16_t  block_no;
   uint8_t   no_descriptors;

   uint16_t  off_message;
   uint16_t  off_descriptors;
} MI_BLOCK;

#define MI_GET_MESSAGE(X)      ((const uchar*)(X)+((X)->off_message))
#define MI_HAS_MESSAGE(X)      ((bool)((X)->off_message != 0))
#define MI_GET_DESCRIPTORS(X)  ((const DESCRIPTOR*)((uchar*)(X)+((X)->off_descriptors)))


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
   uint16_t  block_no;
   uint8_t   msg_attrib;
   uint8_t   header_size;
   uint8_t   msg_size;
   uint8_t   no_descriptors;

   uint16_t  off_header;
   uint16_t  off_message;
   uint16_t  off_descriptors;
} OI_BLOCK;

#define OI_GET_HEADER(X)       ((const uchar*)(X)+((X)->off_header))
#define OI_HAS_HEADER(X)       ((bool)((X)->off_header != 0))
#define OI_GET_MESSAGE(X)      ((const uchar*)(X)+((X)->off_message))
#define OI_HAS_MESSAGE(X)      ((bool)((X)->off_message != 0))
#define OI_GET_DESCRIPTORS(X)  ((const DESCRIPTOR*)((uchar*)(X)+((X)->off_descriptors)))


// ---------------------------------------------------------------------------
//    LI Block
// ---------------------------------------------------------------------------

#define LI_MAX_DESC_COUNT   64
#define LI_MAX_LANG_COUNT   16

typedef struct
{
   uint8_t  id;
   uint8_t  lang_count;
   uint8_t  lang[LI_MAX_LANG_COUNT][3];
} LI_DESC;

typedef struct
{
   uint16_t  block_no;
   uint8_t   netwop_no;
   uint8_t   desc_no;

   uint16_t  off_desc;
} LI_BLOCK;

#define LI_GET_DESC(X)       ((const LI_DESC*)((uchar*)(X)+((X)->off_desc)))

// ---------------------------------------------------------------------------
//    TI Block
// ---------------------------------------------------------------------------

#define TI_MAX_DESC_COUNT   64
#define TI_MAX_LANG_COUNT   16

typedef struct
{
   uint8_t   lang[3];
   uint16_t  page;
   uint16_t  subpage;
} TI_SUBT;

typedef struct
{
   uint8_t   id;
   uint8_t   subt_count;
   TI_SUBT   subt[TI_MAX_LANG_COUNT];
} TI_DESC;

typedef struct
{
   uint16_t  block_no;
   uint8_t   netwop_no;
   uint8_t   desc_no;

   uint16_t  off_desc;
} TI_BLOCK;

#define TI_GET_DESC(X)       ((const TI_DESC*)((uchar*)(X)+((X)->off_desc)))

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
   uint16_t  block_no;
   uint8_t   netwop_no;
   // more data following here, depending on the actual block type
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
   struct EPGDB_BLOCK_STRUCT *pNextBlock;        // next block in order of start time
   struct EPGDB_BLOCK_STRUCT *pPrevBlock;        // previous block in order of start time
   struct EPGDB_BLOCK_STRUCT *pNextNetwopBlock;  // next block of the same network in order of start time
   struct EPGDB_BLOCK_STRUCT *pPrevNetwopBlock;  // previous block of the same network in order of start time
   uint32_t     size;               // actual size of the union; may be greater than it's sizeof()
   uint8_t      version;            // AI version at the time of acquisition of this block
   uint8_t      stream;             // stream in which the block was received
   uint8_t      origChkSum;         // check sum over 708 encoded block
   uint8_t      reserved_1;
   uint16_t     origBlkLen;         // length of 708 encoded block
   uint16_t     parityErrCnt;       // parity error count for string segment
   time_t       updTimestamp;       // time when the block content changed last
   time_t       acqTimestamp;       // time when the block was received last
   uint16_t     acqRepCount;        // reception count with same version and size
   BLOCK_TYPE   type;

   const EPGDB_BLOCK_UNION   blk;   // the actual data
} EPGDB_BLOCK;

#define BLK_UNION_OFF    (sizeof(EPGDB_BLOCK) - sizeof(EPGDB_BLOCK_UNION))

// ----------------------------------------------------------------------------
// declaration of database context, which keeps lists of all blocks
//

typedef struct EPGDB_CONTEXT_STRUCT
{
   uint   lockLevel;                // number of database locks on this context
   bool   modified;                 // if TRUE, db was modified by acquisition

   bool   merged;                   // Flag for merged db
   void   *pMergeContext;           // Pointer to merge parameters

   uint   pageNo;                   // Teletext page for acq
   uint   tunerFreq;                // Frequency for acq
   uint   appId;                    // BI block ID

   EPGDB_BLOCK *pAiBlock;
   EPGDB_BLOCK *pFirstPi, *pLastPi;
   EPGDB_BLOCK *pObsoletePi;
   EPGDB_BLOCK *pFirstNetwopPi[MAX_NETWOP_COUNT];
   EPGDB_BLOCK *pFirstGenericBlock[BLOCK_TYPE_GENERIC_COUNT];
} EPGDB_CONTEXT;

typedef struct
{
   uint32_t  ai;
   uint32_t  curVersion;
   uint32_t  allVersions;
   uint32_t  expired;
   uint32_t  defective;
   uint32_t  sinceAcq;
   double    variance;
   double    avgAcqRepCount;
} EPGDB_BLOCK_COUNT;

// max number of databases that can be merged into one
#define MAX_MERGED_DB_COUNT  10

// ----------------------------------------------------------------------------
// Declaration of queue for acquisition
//
typedef struct
{
   uint            blockCount;
   EPGDB_BLOCK   * pFirstBlock;
   EPGDB_BLOCK   * pLastBlock;
} EPGDB_QUEUE;

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

bool EpgBlockCheckConsistancy( EPGDB_BLOCK * pBlock );
bool EpgBlockSwapEndian( EPGDB_BLOCK * pBlock );

uint EpgBlockBcdToMoD( uint BCD );
void EpgBlockSetAlphabets( const AI_BLOCK *pAiBlock );
void EpgLtoInit( void );
sint EpgLtoGet( time_t when );

#endif // __EPGBLOCK_H
