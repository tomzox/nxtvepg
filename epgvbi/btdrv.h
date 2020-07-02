/*
 *  VBI capture driver interface definition
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
 *    This header file defines the interface between Nextview EPG
 *    acquisition (including EPG scan) and the driver support module.
 *    Except for configuration functions, the interface is platform,
 *    driver and TV capture chip independent, i.e. the same functions
 *    are provided for UNIX (v4l and bktr drivers) and M$ Windows
 *    (DScaler driver and WDM)
 *
 *    Currently all UNIX platfors use the btdrv4linux implementation
 *    of the driver support module, M$ Windows uses btdrv4win. For
 *    other platforms there's a btdrv4dummy module (won't allow
 *    acquisition from a TV card, however you can still receive data
 *    from a daemon in the network).  Ports for additional platforms
 *    are always welcome.
 *
 *    Note: the prefix "BtDriver" has only historical reasons: at the
 *    time the driver was written there was only Linux bttv.  Even long
 *    after, only cards with the Brooktree family of capture chips were
 *    supported.
 *
 *    For an explanations of the functions see the C files.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: btdrv.h,v 1.51 2020/06/21 07:33:46 tom Exp tom $
 */

#ifndef __BTDRV_H
#define __BTDRV_H


// ---------------------------------------------------------------------------
// NetBSD/FreeBSD only: these structs are used by slave and master to keep record
// of the cards and their input channels.
//
// In NetBSD we need to keep the bktr video device open by the slave process
// all the time while acq is active.  Hence we can only scan the remaining
// cards; the currently used card is scanned by the slave when acq is started
// or when the card index is changed.

#if defined(__NetBSD__) || defined(__FreeBSD__)

# define MAX_CARDS    4  // max number of supported cards
# define MAX_INPUTS   4  // max number of supported inputs
# define MAX_BSD_CARD_NAME_LEN   20  // max. string length in struct

struct Input
{
  char name [MAX_BSD_CARD_NAME_LEN];  // name of the input
  int inputID;            // id within the bktr-device
  int isTuner;            // input is a tuner
  int isAvailable;        // the hardware has this input-type
};

struct Card
{
  char name[MAX_BSD_CARD_NAME_LEN];
  struct Input inputs[MAX_INPUTS]; // the inputs, bktr currently knows only 4
  int isAvailable;        // the card is installed
  int isBusy;             // the device is already open
  int inUse;              // device is used by the vbi slave
};
#endif  //__NetBSD__ || __FreeBSD__

typedef enum
{
   VBI_SLICER_AUTO,
   VBI_SLICER_TRIVIAL,
   VBI_SLICER_ZVBI,
   VBI_SLICER_COUNT
} VBI_SLICER_TYPE;

typedef enum
{
   VBI_CHANNEL_PRIO_UNSET,
   VBI_CHANNEL_PRIO_BACKGROUND,
   VBI_CHANNEL_PRIO_INTERACTIVE
} VBI_CHANNEL_PRIO_TYPE;

typedef enum
{
#ifdef WIN32
   BTDRV_SOURCE_WDM = 0,
   BTDRV_SOURCE_PCI = 1,
#else
   BTDRV_SOURCE_ANALOG = 0,
   BTDRV_SOURCE_DVB = 1,
#endif
   BTDRV_SOURCE_NONE = -1,
   BTDRV_SOURCE_UNDEF = -2
} BTDRV_SOURCE_TYPE;


// enable dump of all incoming TTX packets
#define DUMP_TTX_PACKETS         OFF

// ---------------------------------------------------------------------------
// number of teletext packets that can be stored in ring buffer
// - room for 1-2 secs should be enough in most cases, i.e. 2*5*24=240
// - Nextview maximum data rate is 5 pages per second (200ms min distance)
//   data rate usually is much lower though, around 1-2 pages per sec
// - for teletext capture the maximum rate is usually 25 pages/second
//   (i.e. pages are split across two VBI fields, unless it's a short page;
//   normally there's at most one page header per field due to the 200ms rule)
//   Hence we expect at most 2*25*30 = 1500 packets in 2 secs
//
#define TTXACQ_BUF_COUNT  1408

// ring buffer element, contains one teletext packet
typedef struct
{
   uint16_t  pageno;            // teletext magazine (0..7 << 8) and page number
   uint16_t  ctrl_lo;           // control bits
   uint8_t   ctrl_hi;           // control bits
   uint8_t   pkgno;             // teletext packet number (0..31)
   uint8_t   data[40];          // raw teletext data starting at offset 2 (i.e. after mag/pkgno)

   #if DUMP_TTX_PACKETS == ON
   uint32_t  frame;             // frame in which the packet was received (for debugging only)
   uint8_t   line;              // VBI line index in the frame in which the data was received
   uint8_t   reserved2[3];      // unused; undefined
   #endif
} VBI_LINE;

// ---------------------------------------------------------------------------
// Page header buffer and page number metrics

// number of buffers for teletext page headers
#define EPGACQ_ROLL_HEAD_COUNT 8

#define EPGACQ_PG_NO_RING_SIZE 25

// possible modes for teletext header capture
#define EPGACQ_TTX_HEAD_NONE   0        // disable header capture
#define EPGACQ_TTX_HEAD_DEC    1        // capture from pages with decimal numbers only
#define EPGACQ_TTX_HEAD_ALL    2        // capture all headers

typedef struct
{
   uint32_t  write_idx;         // Out: next write position
   uint32_t  write_ind;         // Out: incremented after each packet write (to indicate new data)
   uint32_t  fill_cnt;          // Out: number of valid packets (starting at 0)
   uint8_t   write_lock;        // In: writer lock during read
   uint8_t   op_mode;           // In: header capture mode; see defines above
   uint8_t   reserved_0[2];     // reserved for future use, always 0
   VBI_LINE  ring_buf[EPGACQ_ROLL_HEAD_COUNT];

   uint16_t  pg_no_ring[EPGACQ_PG_NO_RING_SIZE];
   int16_t   magPgDirection;    // Out: page sequence prediction: <0:down >0:up
   uint8_t   magPgResetReq;     // In: change value to request reset of page counters
   uint8_t   magPgResetCnf;     // Out: set to value of "req" after reset
   uint8_t   ring_wr_idx;       // Out: index of the last written value in ring buffer
   uint8_t   ring_val_cnt;      // Out: number of valid values in ring buffer (starting at 0)
   uint8_t   reserved_1[4];     // reserved for future use, always 0
} TTX_HEAD_BUF;

// ---------------------------------------------------------------------------
// Teletext acquisition statistics
//
typedef struct
{
   uint32_t  frameCount;        // number of VBI frames received
   uint32_t  vpsLineCount;      // number of VPS lines received
   uint32_t  ttxPkgCount;       // number of ttx packets received
   uint32_t  ttxPkgDrop;        // number of ttx packets dropped b/c Hamming errors
   uint32_t  ttxPkgRate;        // number of ttx packets per frame: running average (.16 bit fix-point)
   uint32_t  scanPkgCount;      // number of EPG ttx packets received
   uint32_t  scanPagCount;      // number of EPG ttx pages received
   uint32_t  ttxPkgGrab;        // number of ttx packets grabbed (for non-EPG)
   uint32_t  ttxPagGrab;        // number of ttx pages grabbed (for non-EPG)
   uint32_t  reserved_0[4];     // unused, always 0
} TTX_DEC_STATS;

// ---------------------------------------------------------------------------
// Channel identification (CNI) decoding state
//
typedef enum
{
   CNI_TYPE_VPS,                // VPS line 16, used in DE, AU, CH and CZ only
   CNI_TYPE_PDC,                // Packet 8/30/2, used in GB only
   CNI_TYPE_NI,                 // Packet 8/30/1, used mainly in F
   CNI_TYPE_COUNT,
   INVALID_CNI_TYPE = CNI_TYPE_COUNT
} CNI_TYPE;

#define PDC_TEXT_LEN   20       // max. length of status display text without terminating zero

typedef struct
{
   uint8_t   haveCni;           // CNI available
   uint8_t   havePil;           // PIL available
   uint32_t  outCni;            // latest confirmed CNI - reset when fetched
   uint32_t  outPil;            // latest confirmed PIL - reset when fetched
   uint32_t  outCniInd;         // Incremented with each new CNI value
   uint32_t  outPilInd;         // Incremented with each new PIL value
   uint32_t  outTextInd;        // Incremented with each new text

   uint32_t  lastCni;           // last received CNI - copied to outCni after X repetitions
   uint32_t  cniRepCount;       // reception counter - reset upon CNI change
   uint32_t  lastPil;           // last received PIL - copied to outPil after X repetitions
   uint32_t  pilRepCount;       // reception counter - reset upon PIL change

   uint8_t   haveText;
   uint8_t   outText[PDC_TEXT_LEN+1];     // status display (e.g. channel name and program title)
   uint8_t   lastChar[PDC_TEXT_LEN];      // last received characters
   uint8_t   charRepCount[PDC_TEXT_LEN];  // reception counter for each character
} CNI_ACQ_STATE;

typedef struct
{
   uint8_t   haveTime;
   uint8_t   timeRepCount;
   uint8_t   reserved[2];
   int32_t   lto;
   int32_t   lastLto;
   uint32_t  timeVal;
   uint32_t  lastTimeVal;
} TTX_TIME_BUF;


// ---------------------------------------------------------------------------
// Channel parameters

typedef enum
{
  EPGACQ_TUNER_NORM_PAL = 0,     // analog norm IDs fixed for used in RC file
  EPGACQ_TUNER_NORM_NTSC = 1,
  EPGACQ_TUNER_NORM_SECAM = 2,
  EPGACQ_TUNER_NORM_DVB,
  EPGACQ_TUNER_EXTERNAL,         // external input (no tuning)
  EPGACQ_TUNER_NORM_COUNT
} EPGACQ_TUNER_NORM;

typedef struct
{
   EPGACQ_TUNER_NORM norm;
   long              freq;
   int               modulation;
   long              symbolRate;
   int               ttxPid;
} EPGACQ_TUNER_PAR;

// tolerance when comparing DVB frequencies,
// as driver query may not return exactly the tuned value
#define EPGACQ_TUNER_DVB_FREQ_TOL 50000

// ---------------------------------------------------------------------------
// Structure which is put into shared memory
// - used to pass parameters and commands from the master to the acq slave.
//   Those elements are marked with "In:" in the comments below
// - used to pass results from the acq slave to the master, e.g. EPG teletext
//   packets, CNIs and PILs and various statistics.
//   Those elements are marked with "Out:" in the comments below
//
typedef struct
{
   uint8_t   scanEnabled;       // In:  en-/disable capturing for channel scan
   uint8_t   ttxEnabled;        // In:  en-/disable teletext grabber
   uint8_t   reserved0;         // --:  unused; set to 0
   uint8_t   reserved1;         // --:  unused; set to 0
   uint32_t  startPageNo;       // In:  first teletext page number (range 000-7FF)
   uint32_t  stopPageNo;        // In:  last captured teletext page number

   uint8_t   hasFailed;         // Out: TRUE when acq was aborted due to error
   uint8_t   reserved2[3];      // --:  unused; set to 0
 
   uint32_t  chanChangeReq;     // In:  channel change request, i.e. reset of ttx decoder
   uint32_t  chanChangeCnf;     // Out: channel change execution confirmation

   CNI_ACQ_STATE cnis[CNI_TYPE_COUNT];  // Out: CNIs and PILs

   uint32_t  writer_idx;        // Out: current writer slot in ring buffer
   uint32_t  reader_idx;        // In/Out: current reader slot in ring buffer
   VBI_LINE  line[TTXACQ_BUF_COUNT];  // Out: teletext packets on requested pages

   TTX_DEC_STATS  ttxStats;     // Out: teletext decoder statistics
   TTX_HEAD_BUF   ttxHeader;    // Out: rolling buffer of teletext headers
   TTX_TIME_BUF   ttxTime;      // Out: teletext time

   #ifndef WIN32
   bool      vbiSlaveRunning;   // --:  TRUE while slave thread is running
   int       failureErrno;

   uchar     cardIndex;
   bool      cardIsDvb;
   int       dvbPid;
   uint      slicerType;
   # if defined(__NetBSD__) || defined(__FreeBSD__)
   uchar     inputIndex;
   struct Card tv_cards[MAX_CARDS];
   # endif
   uint      slaveChnSwitch;
   bool      slaveChnToken;
   bool      slaveChnTokenGrant;
   bool      slaveVbiProxy;
   int       chnProfValid;
   int       chnSubPrio;
   int       chnMinDuration;
   int       chnExpDuration;
   int       chnPrio;
   #else  // WIN32
   uint32_t  slicerType;

   uchar     reserved3[256];    // reserved for future additions; set to 0
   #endif
} EPGACQ_BUF;

extern volatile EPGACQ_BUF *pVbiBuf;

// ---------------------------------------------------------------------------
// declaration of service interface functions
//

// interface to acquisition control and EPG scan
bool BtDriver_Init( void );
void BtDriver_Exit( void );
bool BtDriver_StartAcq( void );
void BtDriver_StopAcq( void );
const char * BtDriver_GetLastError( void );
bool BtDriver_IsVideoPresent( void );
bool BtDriver_QueryChannel( EPGACQ_TUNER_PAR * pFreqPar, uint * pInput, bool * pIsTuner );
bool BtDriver_TuneChannel( int inputIdx, const EPGACQ_TUNER_PAR * pFreqPar, bool keepOpen, bool * pIsTuner );

#ifndef WIN32
int BtDriver_GetDeviceOwnerPid( void );
void BtDriver_TuneDvbPid( int pid );
#else
bool BtDriver_Restart( void );
bool BtDriver_GetState( bool * pEnabled, bool * pHasDriver, uint * pCardIdx );
#endif
void BtDriver_SetChannelProfile( VBI_CHANNEL_PRIO_TYPE prio, int subPrio, int duration, int minDuration );
bool BtDriver_QueryChannelToken( void );
bool BtDriver_CheckDevice( void );
void BtDriver_CloseDevice( void );

// configuration interface
#if defined(__NetBSD__) || defined(__FreeBSD__)
void BtDriver_ScanDevices( bool isMasterProcess );
#endif
const char * BtDriver_GetInputName( uint cardIdx, uint cardType, uint drvType, uint inputIdx );
bool BtDriver_Configure( int sourceIdx, int drvType, int prio, int chipType, int cardType,
                         int tunerType, int pllType, bool wdmStop );
void BtDriver_SelectSlicer( VBI_SLICER_TYPE slicerType );
BTDRV_SOURCE_TYPE BtDriver_GetDefaultDrvType( void );
#ifndef WIN32
const char * BtDriver_GetCardName( uint cardIdx, bool dvb );
#else
const char * BtDriver_GetCardNameFromList( uint cardIdx, uint listIdx );
const char * BtDriver_GetTunerName( uint tunerIdx );
bool BtDriver_EnumCards( uint drvType, uint cardIdx, uint cardType,
                         uint * pChipType, const char ** pName, bool showDrvErr );
bool BtDriver_QueryCardParams( uint cardIdx, sint * pCardType, sint * pTunerType, sint * pPllType );
bool BtDriver_CheckCardParams( uint drvType, uint cardIdx, uint chipId, uint cardType, uint tunerType, uint pll, uint input );
#endif


#endif  // __BTDRV_H
