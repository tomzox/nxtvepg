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
 *  $Id: btdrv.h,v 1.18 2002/05/04 18:21:05 tom Exp tom $
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

#ifdef __NetBSD__

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
#endif  //__NetBSD__

// ---------------------------------------------------------------------------
// number of teletext packets that can be stored in ring buffer
// - Nextview maximum data rate is 5 pages per second (200ms min distance)
//   data rate usually is much lower though, around 1-2 pages per sec
// - room for 1-2 secs should be enough in most cases, i.e. 2*5*24=240
#define EPGACQ_BUF_COUNT  512

// ring buffer element, contains one teletext packet
typedef struct
{
   uint16_t  pageno;            // teletext magazine (0..7 << 8) and page number
   uint16_t  sub;               // teletext sub-page number (BCD encoded)
   uint8_t   pkgno;             // teletext packet number (0..31)
   uint8_t   reserved;          // unused; set to 0
   uint8_t   data[40];          // raw teletext data starting at offset 2 (i.e. after mag/pkgno)
} VBI_LINE;

// ---------------------------------------------------------------------------
// Teletext decoding statistics
//
typedef struct
{
   uint32_t  frameCount;        // number of VBI frames received
   uint32_t  ttxPkgCount;       // number of ttx packets received
   uint32_t  ttxPkgDrop;        // number of ttx packets dropped b/c Hamming errors
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

typedef struct
{
   uint8_t   haveCni;           // CNI available
   uint8_t   havePil;           // PIL available
   uint32_t  outCni;            // latest confirmed CNI - reset when fetched
   uint32_t  outPil;            // latest confirmed PIL - reset when fetched

   uint32_t  lastCni;           // last recevied CNI - copied to outCni after X repetitions
   uint32_t  cniRepCount;       // reception counter - reset upon CNI change
   uint32_t  lastPil;           // last received PIL - copied to outPil after X repetitions
   uint32_t  pilRepCount;       // reception counter - reset upon PIL change
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
   uint8_t   doVpsPdc;          // In:  en-/disable VPS/PDC decoder
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
   #ifndef USE_THREADS
   bool      freeDevice;        // In:  TRUE when acq is stopped
   #endif

   bool      doQueryFreq;
   ulong     vbiQueryFreq;

   uchar     cardIndex;
   # ifdef __NetBSD__
   uchar     inputIndex;
   struct Card tv_cards[MAX_CARDS];
   # endif
   #else
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
bool BtDriver_IsVideoPresent( void );
bool BtDriver_TuneChannel( ulong freq, bool keepOpen );
ulong BtDriver_QueryChannel( void );
bool BtDriver_SetInputSource( int inputIdx, bool keepOpen, bool * pIsTuner );

#ifndef WIN32
void BtDriver_CheckParent( void );
int BtDriver_GetDeviceOwnerPid( void );
#else
bool BtDriver_Restart( void );
#endif
bool BtDriver_CheckDevice( void );
void BtDriver_CloseDevice( void );
#ifdef __NetBSD__
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
