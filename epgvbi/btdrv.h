/*
 *  VBI capture driver for the Booktree 848/849/878/879 family
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
 *  $Id: btdrv.h,v 1.26 2002/11/17 18:12:34 tom Exp tom $
 */

#ifndef __BTDRV_H
#define __BTDRV_H


// ---------------------------------------------------------------------------
// These structs are used by slave and master to keep record of the cards
// and their input channels.
// In NetBSD we need to keep the bktr video device open by the slave process
// all the time while acq is active.  Hence we can only scan the remaining
// cards; the currently used card is scanned by the slave when acq is started
// or when the card index is changed.

#if defined(__NetBSD__) || defined(__FreeBSD__)

# define MAX_CARDS    4  // max number of supported cards
# define MAX_INPUTS   4  // max number of supported inputs

struct Input
{
  char name [20];         // name of the input
  int inputID;            // id within the bktr-device
  int isTuner;            // input is a tuner
  int isAvailable;        // the hardware has this input-type
};

struct Card
{
  char name[20];
  struct Input inputs[MAX_INPUTS]; // the inputs, bktr currently knows only 4
  int isAvailable;        // the card is installed
  int isBusy;             // the device is already open
  int inUse;              // device is used by the vbi slave
};
#endif  //__NetBSD__ || __FreeBSD__

#ifdef linux
# include "linux/videodev.h"
#else
# ifndef VIDEO_MODE_SECAM
#  define VIDEO_MODE_PAL          0   // from videodev.h
#  define VIDEO_MODE_NTSC         1
#  define VIDEO_MODE_SECAM        2
# endif
#endif

// ---------------------------------------------------------------------------
// number of teletext packets that can be stored in ring buffer
// - Nextview maximum data rate is 5 pages per second (200ms min distance)
//   data rate usually is much lower though, around 1-2 pages per sec
// - room for 1-2 secs should be enough in most cases, i.e. 2*5*24=240
#define EPGACQ_BUF_COUNT  512

// ring buffer element, contains one teletext packet
typedef struct
{
   #if DUMP_TTX_PACKETS == ON
   uint32_t  frame;             // frame in which the packet was received (for debugging only)
   uint8_t   line;              // VBI line index in the frame in which the data was received
   uint8_t   reserved2[3];      // unused; undefined
   #endif

   uint16_t  pageno;            // teletext magazine (0..7 << 8) and page number
   uint16_t  sub;               // teletext sub-page number (BCD encoded)
   uint8_t   pkgno;             // teletext packet number (0..31)
   uint8_t   reserved;          // unused; undefined
   uint8_t   data[40];          // raw teletext data starting at offset 2 (i.e. after mag/pkgno)
} VBI_LINE;

// ---------------------------------------------------------------------------
// Teletext decoding statistics
//
typedef struct
{
   uint32_t  frameCount;        // number of VBI frames received
   uint32_t  vpsLineCount;      // number of VPS lines received
   uint32_t  ttxPkgCount;       // number of ttx packets received
   uint32_t  ttxPkgDrop;        // number of ttx packets dropped b/c Hamming errors
   uint32_t  ttxPkgRate;        // number of ttx packets per frame: running average (.16 bit fix-point)
   uint32_t  epgPkgCount;       // number of EPG ttx packets received
   uint32_t  epgPagCount;       // number of EPG ttx pages received
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

   uint32_t  lastCni;           // last received CNI - copied to outCni after X repetitions
   uint32_t  cniRepCount;       // reception counter - reset upon CNI change
   uint32_t  lastPil;           // last received PIL - copied to outPil after X repetitions
   uint32_t  pilRepCount;       // reception counter - reset upon PIL change

   uint8_t   haveText;
   uint8_t   outText[PDC_TEXT_LEN+1];     // status display (e.g. channel name and program title)
   uint8_t   lastChar[PDC_TEXT_LEN];      // last received characters
   uint8_t   charRepCount[PDC_TEXT_LEN];  // reception counter for each character
} CNI_ACQ_STATE;

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
   uint8_t   isEnabled;         // In:  en-/disable EPG teletext packet forward
   uint8_t   isEpgScan;         // In:  en-/disable EPG syntax scan an all potential EPG pages
   uint8_t   obsolete1;         // --:  obsolete (former doVps, set to TRUE)
   uint8_t   reserved1;         // --:  unused; set to 0
   uint32_t  epgPageNo;         // In:  EPG teletext page number

   uint8_t   hasFailed;         // Out: TRUE when acq was aborted due to error
   uint8_t   reserved2[3];      // --:  unused; set to 0
 
   uint32_t  chanChangeReq;     // In:  channel change request, i.e. reset of ttx decoder
   uint32_t  chanChangeCnf;     // Out: channel change execution confirmation

   uint32_t  mipPageNo;         // Out: EPG page no as listed in MIP
   uint32_t  dataPageCount;     // Out: number of TTX pages with EPG syntax

   CNI_ACQ_STATE cnis[CNI_TYPE_COUNT];  // Out: CNIs and PILs

   uint32_t  writer_idx;        // Out: current writer slot in ring buffer
   uint32_t  reader_idx;        // In/Out: current reader slot in ring buffer
   VBI_LINE  line[EPGACQ_BUF_COUNT];  // Out: teletext packets on EPG page
   VBI_LINE  lastHeader;        // Out: last teletext header (on any page)

   TTX_DEC_STATS  ttxStats;     // Out: teletext decoder statistics

   #ifndef WIN32
   pid_t     vbiPid;
   pid_t     epgPid;
   int       failureErrno;
   # ifndef USE_THREADS
   bool      freeDevice;        // In:  TRUE when acq is stopped
   # endif

   bool      doQueryFreq;
   uint      vbiQueryFreq;

   uchar     cardIndex;
   # if defined(__NetBSD__) || defined(__FreeBSD__)
   uchar     inputIndex;
   struct Card tv_cards[MAX_CARDS];
   # endif
   #else  // WIN32
   uchar     reserved3[128];    // reserved for future additions; set to 0
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
uint BtDriver_QueryChannel( void );
bool BtDriver_TuneChannel( int inputIdx, uint freq, bool keepOpen, bool * pIsTuner );

#ifndef WIN32
int BtDriver_GetDeviceOwnerPid( void );
#else
bool BtDriver_Restart( void );
#endif
bool BtDriver_CheckDevice( void );
void BtDriver_CloseDevice( void );
#if defined(__NetBSD__) || defined(__FreeBSD__)
void BtDriver_ScanDevices( bool isMasterProcess );
#endif

// interface to GUI
const char * BtDriver_GetCardName( uint cardIdx );
const char * BtDriver_GetTunerName( uint tunerIdx );
const char * BtDriver_GetInputName( uint cardIdx, uint inputIdx );
bool BtDriver_Configure( int cardIndex, int tunerType, int pllType, int prio );
#ifdef WIN32
uint BtDriver_MatchTunerByParams( uint thresh1, uint thresh2,
                                  uchar VHF_L, uchar VHF_H, uchar UHF,
                                  uchar config, ushort IFPCoff );
bool BtDriver_GetState( bool * pEnabled, bool * pHasDriver, uint * pCardIdx );
#endif


#endif  // __BTDRV_H
