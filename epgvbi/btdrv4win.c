/*
 *  Win32 VBI capture driver for the Booktree 848/849/878/879 family
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
 *    This module is a "user-space driver" for the Booktree Bt8x8 chips and
 *    Philips or Temic tuners on Win32 systems. It uses DSdrv by Mathias Ellinger
 *    and John Adcock - a free open-source driver for the BT8x8 chipset. The
 *    driver offers generic I/O functions to directly access the BT8x8 chip.
 *    This module provides an abstraction layer with higher-level functions,
 *    e.g. to tune a given TV channel or start/stop acquisition.
 *
 *    The code in this module is heavily based upon the Linux bttv driver
 *    and has been adapted for WinDriver by someone who calls himself
 *    "Espresso".  I stripped it down for VBI decoding only.  1.5 years later
 *    WinDriver was replaced with the free DSdrv BT8x8 driver by E-Nek.
 *
 *
 *  Authors:
 *
 *    BT8x8-Parts (from bttv driver for Linux)
 *      Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
 *                             & Marcus Metzler (mocm@thp.uni-koeln.de)
 *
 *    Tuner and I2C bus (from bttv driver for Linux)
 *      Copyright (C) 1997 Markus Schroeder (schroedm@uni-duesseldorf.de)
 *      Copyright (C) 1997,1998 Gerd Knorr (kraxel@goldbach.in-berlin.de)
 *
 *    WinDriver adaption (from MultiDec)
 *      Copyright (C) 2000 Espresso (echter_espresso@hotmail.com)
 *
 *    VBI only adaption and nxtvepg integration
 *      Tom Zoerner
 *
 *    WinDriver replaced with DSdrv (DScaler driver)
 *      March 2002 by E-Nek (e-nek@netcourrier.com)
 *
 *  $Id: btdrv4win.c,v 1.23 2002/05/30 13:57:22 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <malloc.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/vbidecode.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"

#include "dsdrv/dsdrvlib.h"
#include "epgvbi/bt848.h"


// ----------------------------------------------------------------------------
// Declaration of internal variables

#define VBI_LINES_PER_FIELD   16
#define VBI_LINES_PER_FRAME  (VBI_LINES_PER_FIELD * 2)
#define VBI_LINE_SIZE         2048
#define VBI_DATA_SIZE        (VBI_LINE_SIZE * VBI_LINES_PER_FRAME)
#define VBI_SPL               2044

#define VBI_FRAME_CAPTURE_COUNT   5
#define VBI_FIELD_CAPTURE_COUNT  (VBI_FRAME_CAPTURE_COUNT * 2)


#define RISC_CODE_LENGTH     (512 * sizeof(DWORD))
typedef DWORD PHYS;

#define I2C_DELAY 0
#define I2C_TIMING (0x7<<4)
#define I2C_COMMAND (I2C_TIMING | BT848_I2C_SCL | BT848_I2C_SDA)


static BYTE TunerDeviceI2C;
static CRITICAL_SECTION m_cCrit;    //semaphore for I2C access

struct TTunerType
{
   uchar  *name;
   uchar  vendor;
   uchar  type;

   double thresh1;       // frequency range for UHF, VHF-L, VHF_H
   double thresh2;
   uchar  VHF_L;
   uchar  VHF_H;
   uchar  UHF;
   uchar  config;
   ushort IFPCoff;
};

// Tuner type table, copied from bttv tuner driver
static const struct TTunerType Tuners[TUNER_COUNT] =
{  // Note: order must be identical to enum in header file!
   { "none", NoTuner, NOTUNER,
     0,0,0x00,0x00,0x00,0x00,0x00},

   { "Temic PAL (4002 FH5)", TEMIC, PAL,
     16*140.25,16*463.25,0x02,0x04,0x01,0x8e,623},
   { "Philips PAL_I", Philips, PAL_I,
     16*140.25,16*463.25,0xa0,0x90,0x30,0x8e,623},
   { "Philips NTSC", Philips, NTSC,
     16*157.25,16*451.25,0xA0,0x90,0x30,0x8e,732},

   { "Philips SECAM", Philips, SECAM,
     16*168.25,16*447.25,0xA7,0x97,0x37,0x8e,623},
   { "Philips FY5 PAL", Philips, PAL,
     16*168.25,16*447.25,0xA0,0x90,0x30,0x8e,623},
   { "Temic NTSC (4032 FY5)", TEMIC, NTSC,
     16*157.25,16*463.25,0x02,0x04,0x01,0x8e,732},
   { "Temic PAL_I (4062 FY5)", TEMIC, PAL_I,
     16*170.00,16*450.00,0x02,0x04,0x01,0x8e,623},

   { "Temic NTSC (4036 FY5)", TEMIC, NTSC,
     16*157.25,16*463.25,0xa0,0x90,0x30,0x8e,732},
   { "Alps HSBH1", TEMIC, NTSC,
     16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},
   { "Alps TSBE1",TEMIC,PAL,
     16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},
   { "Alps TSBB5", Alps, PAL_I, /* tested (UK UHF) with Modtec MM205 */
     16*133.25,16*351.25,0x01,0x02,0x08,0x8e,632},

   { "Alps TSBE5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
     16*133.25,16*351.25,0x01,0x02,0x08,0x8e,622},
   { "Alps TSBC5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
     16*133.25,16*351.25,0x01,0x02,0x08,0x8e,608},
   { "Temic PAL_I (4006FH5)", TEMIC, PAL_I,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "Alps TSCH6",Alps,NTSC,
     16*137.25,16*385.25,0x14,0x12,0x11,0x8e,732},

   { "Temic PAL_DK (4016 FY5)",TEMIC,PAL,
     16*136.25,16*456.25,0xa0,0x90,0x30,0x8e,623},
   { "Philips NTSC_M (MK2)",Philips,NTSC,
     16*160.00,16*454.00,0xa0,0x90,0x30,0x8e,732},
   { "Temic PAL_I (4066 FY5)", TEMIC, PAL_I,
     16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},
   { "Temic PAL* auto (4006 FN5)", TEMIC, PAL,
     16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},

   { "Temic PAL (4009 FR5)", TEMIC, PAL,
     16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
   { "Temic NTSC (4039 FR5)", TEMIC, NTSC,
     16*158.00, 16*453.00, 0xa0,0x90,0x30,0x8e,732},
   { "Temic PAL/SECAM multi (4046 FM5)", TEMIC, PAL,
     16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},
   { "Philips PAL_DK", Philips, PAL,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},

   { "Philips PAL/SECAM multi (FQ1216ME)", Philips, PAL,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "LG PAL_I+FM (TAPC-I001D)", LGINNOTEK, PAL_I,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "LG PAL_I (TAPC-I701D)", LGINNOTEK, PAL_I,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "LG NTSC+FM (TPI8NSR01F)", LGINNOTEK, NTSC,
     16*210.00,16*497.00,0xa0,0x90,0x30,0x8e,732},

   { "LG PAL_BG+FM (TPI8PSB01D)", LGINNOTEK, PAL,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "LG PAL_BG (TPI8PSB11D)", LGINNOTEK, PAL,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "Temic PAL* auto + FM (4009 FN5)", TEMIC, PAL,
     16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
   { "SHARP NTSC_JP (2U5JF5540)", SHARP, NTSC, /* 940=16*58.75 NTSC@Japan */
     16*137.25,16*317.25,0x01,0x02,0x08,0x8e,940},

   { "Samsung PAL TCPM9091PD27", Samsung, PAL,  /* from sourceforge v3tv */
     16*169,16*464,0xA0,0x90,0x30,0x8e,623},
   { "MT2032 universal", Microtune,PAL|NTSC,
     0,0,0,0,0,0,0},
   { "Temic PAL_BG (4106 FH5)", TEMIC, PAL,
     16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
   { "Temic PAL_DK (4012 FY5)", TEMIC, PAL,
     16*140.25, 16*463.25, 0x02,0x04,0x01,0x8e,623},
   { "Temic NTSC (4136 FY5)", TEMIC, NTSC,
     16*158.00, 16*453.00, 0xa0,0x90,0x30,0x8e,732},
};

#define TUNERS_COUNT (sizeof(Tuners) / sizeof(struct TTunerType))
//#if (TUNER_COUNT != TUNERS_COUNT)
//#error "tuner table length not equal enum"
//#endif

typedef struct
{
   WORD   VendorId;
   WORD   DeviceId;
   char * szName;
} TBT848Chip;

static const TBT848Chip BT848Chips[] =
{
   { 0x109e, 0x036e, "BT878" },
   { 0x109e, 0x036f, "BT878A"},
   { 0x109e, 0x0350, "BT848" },
   { 0x109e, 0x0351, "BT849" }
};
#define TV_CHIP_COUNT (sizeof(BT848Chips) / sizeof(BT848Chips[0]))

typedef struct
{
   uint  chipIdx;
   uint  chipCardIdx;
   DWORD dwSubSystemID;
   DWORD dwBusNumber;
   DWORD dwSlotNumber;
} TVCARD_ID;

#define MAX_CARD_COUNT  4
#define CARD_COUNT_UNINITIALIZED  (MAX_CARD_COUNT + 1)
static TVCARD_ID btCardList[MAX_CARD_COUNT];
static uint      btCardCount;

#define INVALID_INPUT_SOURCE  4

static struct
{
   TUNER_TYPE  tunerType;
   uint        pllType;
   uint        threadPrio;
   uint        cardIdx;
   uint        inputSrc;
   uint        tunerFreq;
} btCfg;

static HANDLE vbiThreadHandle = NULL;
static BOOL StopVbiThread;
static BOOL btDrvLoaded;
static BOOL shmSlaveMode = FALSE;

volatile EPGACQ_BUF * pVbiBuf;
static EPGACQ_BUF vbiBuf;

PMemStruct Risc_dma;
PMemStruct Vbi_dma[VBI_FRAME_CAPTURE_COUNT];

PHYS RiscBasePhysical;
BYTE *pVBILines[VBI_FRAME_CAPTURE_COUNT] = { NULL,NULL,NULL,NULL,NULL };
long BytesPerRISCField=1;


// ----------------------------------------------------------------------------
// Helper function to set user-configured priority in IRQ and VBI threads
//
static void SetAcqPriority( HANDLE thr, int level )
{
   int prio;

   switch (level)
   {
      default:
      case 0: prio = THREAD_PRIORITY_NORMAL; break;
      case 1: prio = THREAD_PRIORITY_ABOVE_NORMAL; break;
      // skipping HIGHEST by (arbitrary) choice
      case 2: prio = THREAD_PRIORITY_TIME_CRITICAL; break;
   }
   if (SetThreadPriority(thr, prio) == 0)
      debug1("SetAcqPriority: SetThreadPriority returned %ld", GetLastError());
}

// ----------------------------------------------------------------------------
// Utility functions to address the memory mapped Bt8x8 I/O registers

static void MaskDataByte (int Offset, BYTE d, BYTE m)
{
   BYTE a;
   BYTE b;
   a = BT8X8_ReadByte (Offset);
   b = (a & ~(m)) | ((d) & (m));
   BT8X8_WriteByte (Offset, b);
}


#if 0  // unused code
static void MaskDataWord (int Offset, WORD d, WORD m)
{
   WORD a;
   WORD b;
   a = BT8X8_ReadWord (Offset);
   b = (a & ~(m)) | ((d) & (m));
   BT8X8_WriteWord (Offset, b);
}
#endif


static void AndDataByte (int Offset, BYTE d)
{
   BYTE a;
   BYTE b;
   a = BT8X8_ReadByte (Offset);
   b = a & d;
   BT8X8_WriteByte (Offset, b);
}


static void AndDataWord (int Offset, short d)
{
   WORD a;
   WORD b;
   a = BT8X8_ReadWord (Offset);
   b = a & d;
   BT8X8_WriteWord (Offset, b);
}


static void OrDataByte (int Offset, BYTE d)
{
   BYTE a;
   BYTE b;
   a = BT8X8_ReadByte (Offset);
   b = a | d;
   BT8X8_WriteByte (Offset, b);
}


static void OrDataWord (int Offset, unsigned short d)
{
   WORD a;
   WORD b;
   a = BT8X8_ReadWord (Offset);
   b = a | d;
   BT8X8_WriteWord (Offset, b);
}



// ----------------------------------------------------------------------------
// Allocate memory for DMA
// - The difference to normal malloc is that you have to know the
//   physical address in RAM and that the memory must not be swapped out.
//
static BOOL Alloc_DMA(DWORD dwSize, PMemStruct * ppDma, int Option)
{
   *ppDma = NULL;

   memoryAlloc(dwSize, Option, ppDma);

   return (*ppDma != NULL);
}


static void Free_DMA(PMemStruct * ppDma)
{
   memoryFree(*ppDma);
}


static PHYS GetPhysicalAddress(PMemStruct pMem, LPBYTE pLinear, DWORD dwSizeWanted, DWORD * pdwSizeAvailable)
{
   PPageStruct pPages = (PPageStruct)(pMem + 1);
   DWORD Offset;
   DWORD i;
   DWORD sum;
   DWORD pRetVal = 0;

   Offset = (DWORD)pLinear - (DWORD)pMem->dwUser;
   sum = 0;
   i = 0;
   while (i < pMem->dwPages)
   {
      if (sum + pPages[i].dwSize > (unsigned)Offset)
      {
         Offset -= sum;
         pRetVal = pPages[i].dwPhysical + Offset;
         if ( pdwSizeAvailable != NULL )
         {
            *pdwSizeAvailable = pPages[i].dwSize - Offset;
         }
         break;
      }
      sum += pPages[i].dwSize;
      i++;
   }
   if(pRetVal == 0)
   {
      sum++;
   }
   if ( pdwSizeAvailable != NULL )
   {
      if (*pdwSizeAvailable < dwSizeWanted)
      {
         sum++;
      }
   }

   return pRetVal;
}


static void * GetFirstFullPage( PMemStruct pMem )
{
   PPageStruct pPages = (PPageStruct)(pMem + 1);
   DWORD pRetVal;

   pRetVal = (DWORD)pMem->dwUser;

   if(pPages[0].dwSize != 4096)
   {
      pRetVal += pPages[0].dwSize;
   }

   return (void *) pRetVal;
}

// ----------------------------------------------------------------------------
// Allocate the required DMA memory areas
// - one area is for the VBI buffer
// - one area is for the RISC code that is executed in the DMA controller
//
static BOOL BT8X8_MemoryInit(void)
{
   int i;

   if (!Alloc_DMA(83968, &Risc_dma, ALLOC_MEMORY_CONTIG))
   {
      MessageBox(NULL, "Risc Memory (83 KB Contiguous) not Allocated", "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      return FALSE;
   }

   RiscBasePhysical = GetPhysicalAddress(Risc_dma, (BYTE *)Risc_dma->dwUser, 83968, NULL);

   for (i = 0; i < VBI_FRAME_CAPTURE_COUNT; i++)
   {
      // JA 02/01/2001
      // Allocate some extra memory so that we can skip
      // start of buffer that is not page aligned
      if (!Alloc_DMA(2048 * 19 * 2 + 4095, &Vbi_dma[i], 0))
      {
         MessageBox(NULL, "VBI Memory for DMA not Allocated", "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         return FALSE;
      }
      pVBILines[i] = (BYTE *) GetFirstFullPage(Vbi_dma[i]);
   }
   return TRUE;
}

// ----------------------------------------------------------------------------
// Generate RISC program to control DMA transfer
// - This program is executed by the DMA controller in the Booktree chip.
// - It controls which of the captured data is written whereto.
// - In our case it's very simple: all VBI lines are written into one
//   consecutive buffer (actually there's a gap of 4 bytes between lines)
//
#if 0
static void MakeVbiTable ( void )
{
   int idx;
   DWORD *po = Risc_dma.pUserAddr;
   PHYS pPhysVBI = (PHYS) Vbi_dma.Page[0].pPhysicalAddr;

   // Sync to end of even field
   *(po++) = (DWORD) (BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_VRE);
   *(po++) = 0;

   // enable packed mode
   *(po++) = (BT848_RISC_SYNC | BT848_FIFO_STATUS_FM1);
   *(po++) = 0;

   for (idx = 0; idx < VBI_LINES_PER_FIELD; idx++)
   {  // read 16 lines of VBI into the host memory
      *(po++) = BT848_RISC_WRITE | BT848_RISC_SOL | BT848_RISC_EOL | VBI_SPL;
      *(po++) = pPhysVBI;
      pPhysVBI += VBI_LINE_SIZE;
   }

   // Sync to end of odd field
   *(po++) = (DWORD) (BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_VRO);
   *(po++) = 0;

   // enable packed mode
   *(po++) = (BT848_RISC_SYNC | BT848_FIFO_STATUS_FM1);
   *(po++) = 0;

   for (idx = VBI_LINES_PER_FIELD; idx < VBI_LINES_PER_FRAME; idx++)
   {  // read 16 lines of VBI into the host memory
      *(po++) = BT848_RISC_WRITE | BT848_RISC_SOL | BT848_RISC_EOL | VBI_SPL;
      *(po++) = pPhysVBI;
      pPhysVBI += VBI_LINE_SIZE;
   }

   // jump back to the start of the loop and raise the RISCI interrupt
   *(po++) = BT848_RISC_JUMP | (0xf0<<16) | BT848_RISC_IRQ;
   *(po++) = RiscLogToPhys (Risc_dma.pUserAddr);
}


static void MakeVbiTable ( void )
{
   int idx;
   DWORD *po, GotBytesPerLine;
   PHYS pPhysical;
   LPBYTE pUser;

   po = (DWORD *)Risc_dma->dwUser;
   pUser = pVBILines[0];

   // Sync to end of even field
   *(po++) = (DWORD) (BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_VRE);
   *(po++) = 0;

   // enable packed mode
   *(po++) = (BT848_RISC_SYNC | BT848_FIFO_STATUS_FM1);
   *(po++) = 0;

   for (idx = 0; idx < VBI_LINES_PER_FIELD; idx++)
   {
      pPhysical = GetPhysicalAddress(Vbi_dma[0], pUser, VBI_SPL, &GotBytesPerLine);
      if(pPhysical == 0 || VBI_SPL > GotBytesPerLine)
         return;

      // read 16 lines of VBI into the host memory
      *(po++) = BT848_RISC_WRITE | BT848_RISC_SOL | BT848_RISC_EOL | VBI_SPL;
      *(po++) = pPhysical;
      pUser += VBI_LINE_SIZE;
   }

  pUser += VBI_LINES_PER_FRAME * VBI_LINE_SIZE;

   // Sync to end of odd field
   *(po++) = (DWORD) (BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_VRO);
   *(po++) = 0;

   // enable packed mode
   *(po++) = (BT848_RISC_SYNC | BT848_FIFO_STATUS_FM1);
   *(po++) = 0;

   for (idx = VBI_LINES_PER_FIELD; idx < VBI_LINES_PER_FRAME; idx++)
   {
      pPhysical = GetPhysicalAddress(Vbi_dma[1], pUser, VBI_SPL, &GotBytesPerLine);
      if(pPhysical == 0 || VBI_SPL > GotBytesPerLine)
         return;
      // read 16 lines of VBI into the host memory
      *(po++) = BT848_RISC_WRITE | BT848_RISC_SOL | BT848_RISC_EOL | VBI_SPL;
      *(po++) = pPhysical;
      pUser += VBI_LINE_SIZE;
   }

   // jump back to the start of the loop and raise the RISCI interrupt
   *(po++) = BT848_RISC_JUMP;
   *(po++) = RiscBasePhysical;
}
#endif

static void MakeVbiTable( void )
{
   DWORD *pRiscCode;
   int nField;
   int nLine;
   LPBYTE pUser;
   PHYS pPhysical;
   DWORD GotBytesPerLine;
   //DWORD BytesPerLine = 0;

   pRiscCode = (DWORD *)Risc_dma->dwUser;

   // create the RISC code for 10 fields
   // the first one (0) is even, last one (9) is odd
   for (nField = 0; nField < VBI_FIELD_CAPTURE_COUNT; nField++)
   {
      // First we sync onto either the odd or even field
      if (nField & 1)
      {
         *(pRiscCode++) = (DWORD) (BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_VRO);
      }
      else
      {
         *(pRiscCode++) = (DWORD) (BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_VRE  | ((0xF1 + nField / 2) << 16));
      }
      *(pRiscCode++) = 0;

      *(pRiscCode++) = (DWORD) (BT848_RISC_SYNC | BT848_FIFO_STATUS_FM1);
      *(pRiscCode++) = 0;

      pUser = pVBILines[nField / 2];

      if (nField & 1)
         pUser += VBI_LINES_PER_FIELD * VBI_LINE_SIZE;

      for (nLine = 0; nLine < VBI_LINES_PER_FIELD; nLine++)
      {
         pPhysical = GetPhysicalAddress(Vbi_dma[nField / 2], pUser, VBI_SPL, &GotBytesPerLine);
         if (pPhysical == 0 || VBI_SPL > GotBytesPerLine)
            return;
         *(pRiscCode++) = BT848_RISC_WRITE | BT848_RISC_SOL | BT848_RISC_EOL | VBI_SPL;
         *(pRiscCode++) = pPhysical;
         pUser += VBI_LINE_SIZE;
      }
   }

   BytesPerRISCField = ((long)pRiscCode - (long)Risc_dma->dwUser) / VBI_FIELD_CAPTURE_COUNT;
   *(pRiscCode++) = BT848_RISC_JUMP;
   *(pRiscCode++) = RiscBasePhysical;
}


// ---------------------------------------------------------------------------
// Determine which frame has been completed last
// - returns frame index: 0 to 4 (the VBI buffer contains 5 frames)
//
static int BT8X8_GetRISCPosAsInt( void )
{
   DWORD CurrentRiscPos;
   int   CurrentPos = VBI_FIELD_CAPTURE_COUNT;
   int   CurrentFrame;

   while (CurrentPos >= VBI_FIELD_CAPTURE_COUNT)
   {
      // read the RISC program counter, i.e. pointer into the RISC code
      CurrentRiscPos = BT8X8_ReadDword(BT848_RISC_COUNT);
      CurrentPos = (CurrentRiscPos - RiscBasePhysical) / BytesPerRISCField;
   }

   // the current position lies in the field which is currently being filled
   // calculate the index of the previous (i.e. completed) frame
   if (CurrentPos < 2)
      CurrentFrame = ((CurrentPos + VBI_FIELD_CAPTURE_COUNT) - 2) / 2;
   else
      CurrentFrame = (CurrentPos - 2) / 2;

   return CurrentFrame;
}


// ---------------------------------------------------------------------------
// En- or disable VBI capturing
// - when enabling, set up the RISC program and also enable DMA transfer
//
static BOOL Set_Capture(BOOL enable)
{
   // disable capturing while the RISC program is changed to avoid a crash
   MaskDataByte (BT848_CAP_CTL, 0, 0x0f);

   if (enable)
   {
      // set up a RISC program loop that controls DMA from the Bt8x8 to RAM
      MakeVbiTable();

      // enable capturing VBI in even and odd field; video is not captured
      MaskDataByte(BT848_CAP_CTL, BT848_CAP_CTL_CAPTURE_VBI_EVEN | BT848_CAP_CTL_CAPTURE_VBI_ODD, 0x0f);

      // enable DMA
      OrDataWord (BT848_GPIO_DMA_CTL, 3);
   }
   else
   {
      // disable DMA
      AndDataWord (BT848_GPIO_DMA_CTL, ~3);
   }

   return TRUE;
}

// ---------------------------------------------------------------------------
// Select the video input source
// - which input is tuner and which composite etc. is completely up to the
//   card manufacturer, but it seems that almost all use the 2,3,1,1 muxing
// - returns TRUE in *pIsTuner if the selected source is the TV tuner
//   XXX currently index 0 is hardwired as TV tuner input
//
bool BtDriver_SetInputSource( int inputIdx, bool keepOpen, bool * pIsTuner )
{
   bool result = FALSE;

   // 0= Video_Tuner,
   // 1= Video_Ext1,
   // 2= Video_Ext2,
   // 3= Video_Ext3
   // 4= INVALID_INPUT_SOURCE

   if (inputIdx < 4)
   {
      // remember the input source for later
      btCfg.inputSrc = inputIdx;

      if (shmSlaveMode == FALSE)
      {
         if (btDrvLoaded)
         {
            AndDataByte (BT848_IFORM, ~(3 << 5));

            switch (inputIdx)
            {
               case 0:
               case 1:
               case 2:  // configure composite input
                  AndDataByte (BT848_E_CONTROL, ~BT848_CONTROL_COMP);
                  AndDataByte (BT848_O_CONTROL, ~BT848_CONTROL_COMP);
                  break;

               case 3:  // configure s-video input
                  OrDataByte (BT848_E_CONTROL, BT848_CONTROL_COMP);
                  OrDataByte (BT848_O_CONTROL, BT848_CONTROL_COMP);
                  break;
            }

            MaskDataByte (BT848_IFORM, (BYTE)(((inputIdx + 2) & 3) << 5), (3 << 5));
         }
         result = TRUE;
      }
      else
      {  // slave mode -> set param in shared memory
         result = WintvSharedMem_SetInputSrc(inputIdx);
      }
   }
   else
      debug1("BtDriver-SetInputSource: invalid input source %d", inputIdx);

   if (pIsTuner != NULL)
      *pIsTuner = (inputIdx == 0);

   return result;
}

// ---------------------------------------------------------------------------
// Return name for given input source index
// - has to be called repeatedly with incremented indices until NULL is returned
//
const char * BtDriver_GetInputName( uint cardIdx, uint inputIdx )
{
   switch (inputIdx)
   {
      case 0:  return "Tuner"; break;
      case 1:  return "Composite 1"; break;
      case 2:  return "Composite 2"; break;
      case 3:  return "S-Video (Y/C)"; break;
      default: return NULL;
   }
}

// ---------------------------------------------------------------------------
// Return name for given TV card
//
const char * BtDriver_GetCardName( uint cardIdx )
{
   #define MAX_CARD_NAME_LEN 32
   static char name[MAX_CARD_NAME_LEN + 1];
   const char * result = NULL;
   uint chipIdx;

   if (btCardCount == CARD_COUNT_UNINITIALIZED)
   {  // no PCI scan done yet -> return numerical indices for the max. number of cards
      if (cardIdx < MAX_CARD_COUNT)
      {
         sprintf(name, "%d", cardIdx);
         result = name;
      }
   }
   else
   {
      if (cardIdx < btCardCount)
      {
         chipIdx = btCardList[cardIdx].chipIdx;
         sprintf(name, "%d (%s)", cardIdx, BT848Chips[chipIdx].szName);
         result = name;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Check if the current source has a video signal
//
bool BtDriver_IsVideoPresent( void )
{
   bool result;

   if (btDrvLoaded)
   {
      result = ((BT8X8_ReadByte (BT848_DSTATUS) & (BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC)) == (BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC)) ? TRUE : FALSE;
   }
   else if (shmSlaveMode)
   {  // this operation is currently not supported by the TV app interaction protocol
      // but it's not really needed since normally we'll be using the TV app channel table for a scan
      result = TRUE;
   }
   else
      result = FALSE;

   return result;
}

// ---------------------------------------------------------------------------
// Start the PLL (required on some cards only)
//
static void InitPll( int type )
{
   int i;

   if (type == 0)
   {
      BT8X8_WriteByte (BT848_TGCTRL, BT848_TGCTRL_TGCKI_NOPLL);
      BT8X8_WriteByte (BT848_PLL_XCI, 0x00);
   }
   else
   {
      if (type == 1)
      {
         BT8X8_WriteByte (BT848_PLL_F_LO, 0xf9);
         BT8X8_WriteByte (BT848_PLL_F_HI, 0xdc);
         BT8X8_WriteByte (BT848_PLL_XCI, 0x8E);
      }
      else
      {
         BT8X8_WriteByte (BT848_PLL_F_LO, 0x39);
         BT8X8_WriteByte (BT848_PLL_F_HI, 0xB0);
         BT8X8_WriteByte (BT848_PLL_XCI, 0x89);
      }

      for (i = 0; i < 100; i++)
      {
         if (BT8X8_ReadByte (BT848_DSTATUS) & BT848_DSTATUS_CSEL)
         {
            BT8X8_WriteByte (BT848_DSTATUS, 0x00);
         }
         else
         {
            BT8X8_WriteByte (BT848_TGCTRL, BT848_TGCTRL_TGCKI_PLL);
            break;
         }
         Sleep (10);
      }

      // these settings do not work with my cards
      //BT8X8_WriteByte (BT848_WC_UP, 0xcf);
      //BT8X8_WriteByte (BT848_VTOTAL_LO, 0x00);
      //BT8X8_WriteByte (BT848_VTOTAL_HI, 0x00);
      //BT8X8_WriteByte (BT848_DVSIF, 0x00);
   }
}

// ----------------------------------------------------------------------------
// Reset the Bt8x8 and program all relevant registers with constants
//
static bool Init_BT_HardWare( void )
{
   // software reset, sets all registers to reset default values
   BT8X8_WriteByte (BT848_SRESET, 0);
   Sleep(50);

   // start address for the DMA RISC code
   BT8X8_WriteDword (BT848_RISC_STRT_ADD, RiscBasePhysical);
   BT8X8_WriteByte (BT848_TDEC, 0x00);
   BT8X8_WriteByte (BT848_COLOR_CTL, BT848_COLOR_CTL_GAMMA);
   BT8X8_WriteByte (BT848_ADELAY, 0x7f);
   BT8X8_WriteByte (BT848_BDELAY, 0x72);
   // disable capturing
   BT8X8_WriteByte (BT848_CAP_CTL, 0x00);
   // max length of a VBI line
   BT8X8_WriteByte (BT848_VBI_PACK_SIZE, 0xff);
   BT8X8_WriteByte (BT848_VBI_PACK_DEL, 1);

   // vertical delay for image data in the even and odd fields
   // IMPORTANT!  This defines the end of VBI capturing, i.e. the number of max. captured lines!
   BT8X8_WriteByte (BT848_E_VDELAY_LO, 0x20);
   BT8X8_WriteByte (BT848_O_VDELAY_LO, 0x20);

   BT8X8_WriteWord (BT848_GPIO_DMA_CTL, BT848_GPIO_DMA_CTL_PKTP_32 |
                                                               BT848_GPIO_DMA_CTL_PLTP1_16 |
                                                               BT848_GPIO_DMA_CTL_PLTP23_16 |
                                                               BT848_GPIO_DMA_CTL_GPINTC |
                                                               BT848_GPIO_DMA_CTL_GPINTI);
   BT8X8_WriteByte (BT848_GPIO_REG_INP, 0x00);
   // input format (PAL, NTSC etc.) and input source
   BT8X8_WriteByte (BT848_IFORM, BT848_IFORM_MUX1 | BT848_IFORM_XTBOTH | BT848_IFORM_PAL_BDGHI);

   BT8X8_WriteByte (BT848_CONTRAST_LO, 0xd8);
   BT8X8_WriteByte (BT848_BRIGHT, 0x10);
   BT8X8_WriteByte (BT848_E_VSCALE_HI, 0x20);
   BT8X8_WriteByte (BT848_O_VSCALE_HI, 0x20);

   BT8X8_WriteByte (BT848_ADC, BT848_ADC_RESERVED | BT848_ADC_CRUSH);
   BT8X8_WriteByte (BT848_E_CONTROL, BT848_CONTROL_LDEC);
   BT8X8_WriteByte (BT848_O_CONTROL, BT848_CONTROL_LDEC);

   BT8X8_WriteByte (BT848_E_SCLOOP, 0x00);
   BT8X8_WriteByte (BT848_O_SCLOOP, 0x00);

   // interrupt mask; reset the status before enabling the interrupts
   BT8X8_WriteDword (BT848_INT_STAT, (DWORD) 0x0fffffffUL);
   BT8X8_WriteDword (BT848_INT_MASK, (1 << 23) | BT848_INT_RISCI);

   return (TRUE);
}

// ---------------------------------------------------------------------------
// I2C bus access
// - does not use the hardware I2C capabilities of the Bt8x8
// - instead every bit of the protocol is implemented in sofware
//
static void
I2CBus_wait (int us)
{
   if (us > 0)
   {
      Sleep (us);
      return;
   }
   Sleep (0);
   Sleep (0);
   Sleep (0);
   Sleep (0);
   Sleep (0);
}

static void
I2C_SetLine (BOOL bCtrl, BOOL bData)
{
   BT8X8_WriteDword ( BT848_I2C, (bCtrl << 1) | bData);
   I2CBus_wait (I2C_DELAY);
}

static BOOL
I2C_GetLine ( void )
{
   return BT8X8_ReadDword (BT848_I2C) & 1;
}

#if 0  // unused code
static BYTE
I2C_Read (BYTE nAddr)
{
   DWORD i;
   volatile DWORD stat;

   // clear status bit ; BT848_INT_RACK is ro

   BT8X8_WriteDword (BT848_INT_STAT, BT848_INT_I2CDONE);
   BT8X8_WriteDword (BT848_I2C, (nAddr << 24) | I2C_COMMAND);

   for (i = 0x7fffffff; i; i--)
   {
      stat = BT8X8_ReadDword (BT848_INT_STAT);
      if (stat & BT848_INT_I2CDONE)
         break;
   }

   if (!i)
      return (BYTE) - 1;
   if (!(stat & BT848_INT_RACK))
      return (BYTE) - 2;

   return (BYTE) ((BT8X8_ReadDword (BT848_I2C) >> 8) & 0xFF);
}

static BOOL
I2C_Write (BYTE nAddr, BYTE nData1, BYTE nData2, BOOL bSendBoth)
{
   DWORD i;
   DWORD data;
   DWORD stat;

   /* clear status bit; BT848_INT_RACK is ro */
   BT8X8_WriteDword (BT848_INT_STAT, BT848_INT_I2CDONE);

   data = (nAddr << 24) | (nData1 << 16) | I2C_COMMAND;
   if (bSendBoth)
      data |= (nData2 << 8) | BT848_I2C_W3B;
   BT8X8_WriteDword (BT848_I2C, data);

   for (i = 0x7fffffff; i; i--)
   {
      stat = BT8X8_ReadDword (BT848_INT_STAT);
      if (stat & BT848_INT_I2CDONE)
         break;
   }

   if (!i)
      return FALSE;
   if (!(stat & BT848_INT_RACK))
      return FALSE;

   return TRUE;
}
#endif


static BOOL
I2CBus_Lock ( void )
{
   EnterCriticalSection (&m_cCrit);
   return TRUE;
}

static BOOL
I2CBus_Unlock ( void )
{
   LeaveCriticalSection (&m_cCrit);
   return TRUE;
}

static void
I2CBus_Start ( void )
{
   I2C_SetLine (0, 1);
   I2C_SetLine (1, 1);
   I2C_SetLine (1, 0);
   I2C_SetLine (0, 0);
}

static void
I2CBus_Stop ( void )
{
   I2C_SetLine (0, 0);
   I2C_SetLine (1, 0);
   I2C_SetLine (1, 1);
}

static void
I2CBus_One ( void )
{
   I2C_SetLine (0, 1);
   I2C_SetLine (1, 1);
   I2C_SetLine (0, 1);
}

static void
I2CBus_Zero ( void )
{
   I2C_SetLine (0, 0);
   I2C_SetLine (1, 0);
   I2C_SetLine (0, 0);
}

static BOOL
I2CBus_Ack ( void )
{
   BOOL bAck;

   I2C_SetLine (0, 1);
   I2C_SetLine (1, 1);
   bAck = !I2C_GetLine ();
   I2C_SetLine (0, 1);
   return bAck;
}

static BOOL
I2CBus_SendByte (BYTE nData, int nWaitForAck)
{
   I2C_SetLine (0, 0);
   nData & 0x80 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x40 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x20 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x10 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x08 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x04 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x02 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x01 ? I2CBus_One () : I2CBus_Zero ();
   if (nWaitForAck)
      I2CBus_wait (nWaitForAck);
   return I2CBus_Ack ();
}

#if 0  // unused code
static BYTE
I2CBus_ReadByte (BOOL bLast)
{
   int i;
   BYTE bData = 0;

   I2C_SetLine (0, 1);
   for (i = 7; i >= 0; i--)
   {
      I2C_SetLine (1, 1);
      if (I2C_GetLine ())
         bData |= (1 << i);
      I2C_SetLine (0, 1);
   }

   bLast ? I2CBus_One () : I2CBus_Zero ();
   return bData;
}

static BYTE
I2CBus_Read (BYTE nAddr)
{
   BYTE bData;

   I2CBus_Start ();
   I2CBus_SendByte (nAddr, 0);
   bData = I2CBus_ReadByte (TRUE);
   I2CBus_Stop ();
   return bData;
}
#endif

static BOOL
I2CBus_Write (BYTE nAddr, BYTE nData1, BYTE nData2, BOOL bSendBoth)
{
   BOOL bAck;

   I2CBus_Start ();
   I2CBus_SendByte (nAddr, 0);
   bAck = I2CBus_SendByte (nData1, 0);
   if (bSendBoth)
      bAck = I2CBus_SendByte (nData2, 0);
   I2CBus_Stop ();
   return bAck;
}


static BOOL
I2CBus_AddDevice (BYTE I2C_Port)
{
   BOOL bAck;

   // Test whether device exists
   I2CBus_Lock ();
   I2CBus_Start ();
   bAck = I2CBus_SendByte (I2C_Port, 0);
   I2CBus_Stop ();
   I2CBus_Unlock ();
   if (bAck)
      return TRUE;
   else
      return FALSE;
}

// ---------------------------------------------------------------------------
// Set TV tuner synthesizer frequency
//
static BOOL Tuner_SetFrequency( int type, int wFrequency )
{
   const struct TTunerType *pTun;
   double dFrequency;
   BYTE config;
   WORD div;
   BOOL bAck;

   if ((type < TUNERS_COUNT) && (type != TUNER_NONE))
   {
      pTun = Tuners + type;
      dFrequency = (double) wFrequency;

      if (dFrequency < pTun->thresh1)
         config = pTun->VHF_L;
      else if (dFrequency < pTun->thresh2)
         config = pTun->VHF_H;
      else
         config = pTun->UHF;

      // tv norm specification for multi-norm tuners
      switch (type)
      {
         case TUNER_TEMIC_4046FM5:
            config |= TEMIC_SET_PAL_BG;
            break;
         case PHILIPS_SET_PAL_BGDK:
            config |= PHILIPS_SET_PAL_BGDK;
            break;
         default:
            break;
      }

      div = (WORD)dFrequency + pTun->IFPCoff;

      div &= 0x7fff;
      I2CBus_Lock ();              // Lock/wait

      if (!I2CBus_Write ((BYTE) TunerDeviceI2C, (BYTE) ((div >> 8) & 0x7f), (BYTE) (div & 0xff), TRUE))
      {
         Sleep (1);
         if (!I2CBus_Write ((BYTE) TunerDeviceI2C, (BYTE) ((div >> 8) & 0x7f), (BYTE) (div & 0xff), TRUE))
         {
            Sleep (1);
            if (!I2CBus_Write ((BYTE) TunerDeviceI2C, (BYTE) ((div >> 8) & 0x7f), (BYTE) (div & 0xff), TRUE))
            {
               debug0("Tuner_SetFrequency: i2c write failed for word #1");
               I2CBus_Unlock ();   // Unlock

               return (FALSE);
            }
         }
      }
      if (!(bAck = I2CBus_Write (TunerDeviceI2C, pTun->config, config, TRUE)))
      {
         Sleep (1);
         if (!(bAck = I2CBus_Write (TunerDeviceI2C, pTun->config, config, TRUE)))
         {
            Sleep (1);
            if (!(bAck = I2CBus_Write (TunerDeviceI2C, pTun->config, config, TRUE)))
            {
               debug0("Tuner_SetFrequency: i2c write failed for word #2");
            }
         }
      }
      I2CBus_Unlock ();            // Unlock

      if (!bAck)
         return FALSE;
   }
   else if (type != TUNER_NONE)
      debug1("Tuner_SetFrequency: illegal tuner idx %d", type);

   return TRUE;
}

// ---------------------------------------------------------------------------
// Auto-detect a tuner on the I2C bus
//
static BOOL Init_Tuner( int TunerNr )
{
   unsigned char j;
   bool result = FALSE;

   if (TunerNr < TUNERS_COUNT)
   {
      InitializeCriticalSection (&m_cCrit);

      j = 0xc0;
      TunerDeviceI2C = j;

      while ((j <= 0xce) && (I2CBus_AddDevice ((BYTE) j) == FALSE))
      {
         j++;
         TunerDeviceI2C = j;
      }

      if (j <= 0xce)
      {
         result = TRUE;
      }
      else
         MessageBox(NULL, "Warning: no tuner found on Bt8x8 I2C bus\nIn address range 0xc0 - 0xce", "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
   }
   else
      debug1("Init_Tuner: illegal tuner idx %d", TunerNr);

   return result;
}

// ---------------------------------------------------------------------------
// Search a tuner in the list by given parameters
// - used to identify a manually configured tuner when importing a TV app INI file
//
uint BtDriver_MatchTunerByParams( uint thresh1, uint thresh2,
                                         uchar VHF_L, uchar VHF_H, uchar UHF,
                                         uchar config, ushort IFPCoff )
{
   const struct TTunerType * pTuner;
   uint idx;

   pTuner = Tuners;
   for (idx=0; idx < TUNERS_COUNT; idx++)
   {
      if ( (thresh1 == pTuner->thresh1) &&
           (thresh2 == pTuner->thresh2) &&
           (VHF_L == pTuner->VHF_L) &&
           (VHF_H == pTuner->VHF_H) &&
           (UHF == pTuner->UHF) &&
           (config == pTuner->config) &&
           (IFPCoff == pTuner->IFPCoff) )
      {
         return idx;
      }
      pTuner += 1;
   }
   return 0;
}


// ---------------------------------------------------------------------------
// Free I/O resources and close the driver device
//
static void BT8X8_Close( void )
{
   int idx;

   BT8X8_WriteByte(BT848_SRESET, 0);

   Free_DMA(&Risc_dma);
   for (idx=0; idx < VBI_FRAME_CAPTURE_COUNT; idx++)
      Free_DMA(&Vbi_dma[idx]);
}

// ---------------------------------------------------------------------------
// Generate a list of available cards
//
static void BT8X8_CountCards( void )
{
   uint  chipIdx, cardIdx;

   // if the scan was already done skip it
   if (btCardCount == CARD_COUNT_UNINITIALIZED)
   {
      btCardCount = 0;
      for (chipIdx=0; (chipIdx < TV_CHIP_COUNT) && (btCardCount < MAX_CARD_COUNT); chipIdx++)
      {
         cardIdx = 0;
         while (btCardCount < MAX_CARD_COUNT)
         {
            if (DoesThisPCICardExist(BT848Chips[chipIdx].VendorId, BT848Chips[chipIdx].DeviceId,
                                     cardIdx,
                                     &btCardList[btCardCount].dwSubSystemID,
                                     &btCardList[btCardCount].dwBusNumber,
                                     &btCardList[btCardCount].dwSlotNumber) == ERROR_SUCCESS)
            {
               dprintf4("PCI scan: found Booktree chip %s, ID=%lx, bus=%ld, slot=%ld\n", BT848Chips[chipIdx].szName, btCardList[btCardCount].dwSubSystemID, btCardList[btCardCount].dwBusNumber, btCardList[btCardCount].dwSlotNumber);
               btCardList[btCardCount].chipIdx     = chipIdx;
               btCardList[btCardCount].chipCardIdx = cardIdx;
               btCardCount += 1;
            }
            else
            {  // no more cards with this chip -> next chip (outer loop)
               break;
            }
            cardIdx += 1;
         }
      }
      dprintf1("BT8X8-CountCards: found %d cards", btCardCount);
   }
}

// ---------------------------------------------------------------------------
// Open the driver device and allocate I/O resources
//
static BOOL BT8X8_Open( void )
{
   char msgbuf[200];
   int  ret;
   int  chipIdx, chipCardIdx;
   BOOL result = FALSE;

   if (btCfg.cardIdx < btCardCount)
   {
      chipIdx     = btCardList[btCfg.cardIdx].chipIdx;
      chipCardIdx = btCardList[btCfg.cardIdx].chipCardIdx;

      ret = pciGetHardwareResources(BT848Chips[chipIdx].VendorId, BT848Chips[chipIdx].DeviceId, chipCardIdx);

      if (ret == ERROR_SUCCESS)
      {
         result = TRUE;
      }
      else if (ret == 3)
      {  // card found, but failed to open -> abort
         sprintf(msgbuf, "PCI-Card #%d (with %s chip) cannot be locked!", btCfg.cardIdx, BT848Chips[chipIdx].szName);
         MessageBox(NULL, msgbuf, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      }
   }
   else
   {
      sprintf(msgbuf, "Bt8x8 TV card #%d not found! (Found %d Bt8x8 TV cards on PCI bus)", btCfg.cardIdx, btCardCount);
      MessageBox(NULL, msgbuf, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Shut down the driver and free all resources
// - after this function is called, other processes can use the card
//
static void BtDriver_Unload( void )
{
   if (btDrvLoaded)
   {
      BT8X8_Close ();
      UnloadDriver();

      dprintf0("BtDriver-Unload: Bt8x8 driver unloaded\n");
      btDrvLoaded = FALSE;
   }
}

// ----------------------------------------------------------------------------
// Boot the driver, allocate resources and initialize all subsystems
//
static bool BtDriver_Load( void )
{
#ifndef WITHOUT_TVCARD
   const uchar * errmsg;
   DWORD loadError;
   bool result;

   assert(shmSlaveMode == FALSE);

   loadError = LoadDriver();
   if (loadError == HWDRV_LOAD_SUCCESS)
   {
      BT8X8_CountCards();

      result = BT8X8_Open();
      if (result != FALSE)
      {
         if ( BT8X8_MemoryInit() )
         {
            // must be set to TRUE before the set funcs are called
            dprintf0("BtDriver-Load: Bt8x8 driver successfully loaded\n");
            btDrvLoaded = TRUE;

            // initialize all bt848 registers
            Init_BT_HardWare();

            InitPll(btCfg.pllType);

            // auto-detect the tuner on the I2C bus
            Init_Tuner(btCfg.tunerType);

            if (btCfg.tunerFreq != 0)
            {  // if freq already set, apply it now
               Tuner_SetFrequency(btCfg.tunerType, btCfg.tunerFreq);
            }
            if (btCfg.inputSrc != INVALID_INPUT_SOURCE)
            {  // if source already set, apply it now
               BtDriver_SetInputSource(btCfg.inputSrc, FALSE, NULL);
            }

            result = TRUE;
         }
         else
         {  // driver boot failed - abort
            BtDriver_Unload();
         }
      }
      // else: user message already generated by open function

      if (result == FALSE)
         UnloadDriver();
   }
   else
   {  // failed to load the driver
      switch (loadError)
      {
         case HWDRV_LOAD_NOPERM:
            errmsg = "Failed to load the Bt8x8 driver: access denied.\n"
                     "On WinNT and Win2K you need admin permissions\n"
                     "to start the driver.  See README.txt for more info.";
            break;
         case HWDRV_LOAD_MISSING:
            errmsg = "Failed to load the Bt8x8 driver: could not start the service.\n"
                     "The the driver files dsdrv4.sys and dsdrv4.vxd may not be\n"
                     "in the nxtvepg directory. See README.txt for more info.";
            break;
         case HWDRV_LOAD_START:
            errmsg = "Failed to load the Bt8x8 driver: could not start the service.\n"
                     "See README.txt for more info.";
            break;
         case HWDRV_LOAD_INSTALL:
            errmsg = "Failed to load the Bt8x8 driver: could not install\n"
                     "the service.  See README.txt for more info.";
            break;
         case HWDRV_LOAD_CREATE:
            errmsg = "Failed to load the Bt8x8 driver: Another application may\n"
                     "already be using the driver. See README.txt for more info.\n";
            break;
         case HWDRV_LOAD_REMOTE_DRIVE:
            errmsg = "Failed to load the Bt8x8 driver: Cannot install the driver\n"
                     "on a network drive. See README.txt for more info.\n";
            break;
         case HWDRV_LOAD_VERSION:
            errmsg = "Failed to load the Bt8x8 driver: it's is an incompatible\n"
                     "version. See README.txt for more info.\n";
            break;
         case HWDRV_LOAD_OTHER:
         default:
            errmsg = "Failed to load the Bt8x8 driver.\n"
                     "See README.txt for more info.";
            break;
      }
      MessageBox(NULL, errmsg, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      result = FALSE;
   }

   return result;

#else  // WITHOUT_TVCARD
   assert(shmSlaveMode == FALSE);
   btDrvLoaded = FALSE;
   return TRUE;
#endif
}

// ----------------------------------------------------------------------------
// Set user-configurable hardware parameters
// - called at program start and after config change
// - Important: input source and tuner freq must be set afterwards
//
bool BtDriver_Configure( int cardIndex, int tunerType, int pllType, int prio )
{
   bool cardChange;
   bool pllChange;
   bool tunerChange;
   bool prioChange;
   bool result = TRUE;

   // check which values change
   cardChange  = (cardIndex != btCfg.cardIdx);
   tunerChange = (tunerType != btCfg.tunerType);
   pllChange   = (pllType   != btCfg.pllType);
   prioChange  = (prio      != btCfg.threadPrio);

   // save the new values
   btCfg.tunerType  = tunerType;
   btCfg.threadPrio = prio;
   btCfg.pllType    = pllType;
   btCfg.cardIdx    = cardIndex;

   if (shmSlaveMode == FALSE)
   {
      if (btDrvLoaded)
      {  // acquisition already running -> must change parameters on the fly

         if (cardChange)
         {  // change of TV card -> unload and reload driver
#ifndef WITHOUT_TVCARD
            BtDriver_StopAcq();
            if (BtDriver_StartAcq() == FALSE)
            {
               if (pVbiBuf != NULL)
                  pVbiBuf->hasFailed = TRUE;
               result = FALSE;
            }
#endif
         }
         else
         {  // same card index: just update tuner type and PLL
            if (tunerChange && (btCfg.tunerType != 0) && (btCfg.inputSrc == 0))
            {
               Init_Tuner(btCfg.tunerType);
            }
            if (pllChange)
            {
               InitPll(btCfg.pllType);
            }
         }

         if (prioChange && (vbiThreadHandle != NULL))
         {
            SetAcqPriority(vbiThreadHandle, btCfg.threadPrio);
         }
      }
   }
   else
   {  // slave mode -> new card idx
      if (cardChange)
      {
         // XXX TODO: may need to change from slave mode to normal OTOWA
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Change the tuner frequency
// - makes only sense if TV tuner is input source
//
bool BtDriver_TuneChannel( uint freq, bool keepOpen )
{
   bool result = FALSE;

   // remember frequency for later
   btCfg.tunerFreq = freq;

   if (shmSlaveMode == FALSE)
   {
      if (btDrvLoaded)
      {
         result = Tuner_SetFrequency(btCfg.tunerType, freq);
      }
      else
      {  // driver not loaded -> freq will be tuned upon acq start
         result = TRUE;
      }
   }
   else
   {  // even in slave mode the TV app may have granted the tuner tu us
      result = WintvSharedMem_SetTunerFreq(freq);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get the current tuner frequency
//
uint BtDriver_QueryChannel( void )
{
   uint freq = 0;

   if (shmSlaveMode == FALSE)
   {
      freq = btCfg.tunerFreq;
   }
   else
   {
      freq = WintvSharedMem_GetTunerFreq();
   }

   return freq;
}

// ----------------------------------------------------------------------------
// Dummies - not used for Windows
//
void BtDriver_CloseDevice( void )
{
}

bool BtDriver_CheckDevice( void )
{
   return (shmSlaveMode == FALSE);
}

// ----------------------------------------------------------------------------
// Retrieve identifier strings for supported tuner types
// - called by user interface
//
const char * BtDriver_GetTunerName( uint idx )
{
   if (idx < TUNERS_COUNT)
      return Tuners[idx].name;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// VBI Driver Thread
//
static DWORD WINAPI BtDriver_VbiThread( LPVOID dummy )
{
   int  OldFrame;
   int  CurFrame;
   int  row;
   BYTE *pVBI;

   SetAcqPriority(GetCurrentThread(), btCfg.threadPrio);

   Set_Capture(TRUE);
   OldFrame = BT8X8_GetRISCPosAsInt();

   for (;;)
   {
      if (StopVbiThread == TRUE)
         break;

      if (pVbiBuf != NULL)
      {
         CurFrame = BT8X8_GetRISCPosAsInt();
         if (CurFrame != OldFrame)
         {
            do
            {
               OldFrame = (OldFrame + 1) % VBI_FRAME_CAPTURE_COUNT;
               pVBI = (LPBYTE) pVBILines[OldFrame];

               // notify teletext decoder about start of new frame; since we don't have a
               // frame counter (no frame interrupt) we always pass 0 as frame sequence number
               if ( VbiDecodeStartNewFrame(0) )
               {
                  for (row = 0; row < VBI_LINES_PER_FRAME; row++, pVBI += VBI_LINE_SIZE)
                  {
                     VbiDecodeLine(pVBI, row, pVbiBuf->doVpsPdc);
                  }
               }
               else
               {  // discard all VBI frames in the buffer
                  OldFrame = CurFrame;
                  break;
               }
            } while (OldFrame != CurFrame);
         }
         else
            Sleep(5);
      }
      else
      {  // acq ctl is currently not interested in VBI data
         Sleep(10);
      }
   }
   Set_Capture(FALSE);

   return 0;  // dummy
}

// ---------------------------------------------------------------------------
// Query if acquisition is currently enabled and in which mode
// - return value pointers may be NULL if value not required
//
bool BtDriver_GetState( bool * pEnabled, bool * pHasDriver, uint * pCardIdx )
{
   if (pEnabled != NULL)
      *pEnabled = (shmSlaveMode | btDrvLoaded);
   if (pHasDriver != NULL)
      *pHasDriver = btDrvLoaded;
   if (pCardIdx != NULL)
      *pCardIdx = btCfg.cardIdx;

   return TRUE;
}

// ---------------------------------------------------------------------------
// Stop and start driver -> toggle slave mode
// - called when a TV application attaches or detaches
// - should not be used to change parameters - used Configure instead
//
bool BtDriver_Restart( void )
{
   bool result;
   uint prevFreq, prevInput;

   // save current input settings into temporary variables
   prevFreq  = btCfg.tunerFreq;
   prevInput = btCfg.inputSrc;

   BtDriver_StopAcq();

   // restore input params
   btCfg.tunerFreq = prevFreq;
   btCfg.inputSrc  = prevInput;

   // start acquisition
   result = BtDriver_StartAcq();

   // inform acq control if acq is switched off now
   if (pVbiBuf != NULL)
      pVbiBuf->hasFailed = !result;

   return result;
}

// ---------------------------------------------------------------------------
// Start acquisition
// - the driver is automatically loaded and initialized
//
bool BtDriver_StartAcq( void )
{
   DWORD LinkThreadID;
   bool result = FALSE;

   if ( (shmSlaveMode == FALSE) && (btDrvLoaded == FALSE) )
   {
      // check if the configured card is currently free
      shmSlaveMode = (WintvSharedMem_ReqTvCardIdx(btCfg.cardIdx) == FALSE);
      if (shmSlaveMode == FALSE)
      {
         if (BtDriver_Load())
         {
            StopVbiThread = FALSE;
#ifndef WITHOUT_TVCARD
            vbiThreadHandle = CreateThread(NULL, 0, BtDriver_VbiThread, NULL, 0, &LinkThreadID);
            if (vbiThreadHandle != NULL)
            {
               result = TRUE;
            }
            else
            {
               debug1("BtDriver-StartAcq: failed to create VBI thread: %ld", GetLastError());
               UnloadDriver();
            }
#else
            result = TRUE;
#endif
         }

         if (result == FALSE)
            WintvSharedMem_FreeTvCard();
      }
      else
      {  // TV card is already used by TV app -> slave mode
         dprintf0("BtDriver-StartAcq: starting in slave mode");

         assert(pVbiBuf == &vbiBuf);
         pVbiBuf = WintvSharedMem_GetVbiBuf();
         if (pVbiBuf != NULL)
         {
            memcpy((char *)pVbiBuf, &vbiBuf, sizeof(*pVbiBuf));
            result = TRUE;
         }
         else
            WintvSharedMem_FreeTvCard();
      }
   }
   else
   {  // acq already active - should never happen
      debug0("BtDriver-StartAcq: driver already loaded");
      result = TRUE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition
// - the driver is automatically stopped and removed
//
void BtDriver_StopAcq( void )
{
   if (shmSlaveMode == FALSE)
   {
      if (btDrvLoaded)
      {
         if (vbiThreadHandle != NULL)
         {
            StopVbiThread = TRUE;
            WaitForSingleObject(vbiThreadHandle, 200);
            CloseHandle(vbiThreadHandle);
            vbiThreadHandle = NULL;
         }
         Set_Capture(FALSE);     // now stop the capture safely

         BtDriver_Unload();

         // notify connected TV app that card & driver are now free
         WintvSharedMem_FreeTvCard();
      }
   }
   else
   {
      if (pVbiBuf != NULL)
      {
         dprintf0("BtDriver-StopAcq: stopping slave mode acq");

         // clear requests in shared memory
         WintvSharedMem_FreeTvCard();

         // switch back to the internal VBI buffer
         assert(pVbiBuf != &vbiBuf);
         memcpy(&vbiBuf, (char *)pVbiBuf, sizeof(vbiBuf));
         pVbiBuf = &vbiBuf;
      }
      else
         debug0("BtDriver-StopAcq: shared memory not allocated");

      shmSlaveMode = FALSE;
   }

   btCfg.tunerFreq  = 0;
   btCfg.inputSrc   = INVALID_INPUT_SOURCE;
}

// ---------------------------------------------------------------------------
// Initialize the driver module
// - called once at program start
//
bool BtDriver_Init( void )
{
   memset(&vbiBuf, 0, sizeof(vbiBuf));
   pVbiBuf = &vbiBuf;

   memset(&btCfg, 0, sizeof(btCfg));
   btCfg.tunerType = TUNER_NONE;
   btCfg.inputSrc  = INVALID_INPUT_SOURCE;
   btCardCount = CARD_COUNT_UNINITIALIZED;

   return TRUE;
}

// ---------------------------------------------------------------------------
// Clean up the driver module for exit
// - called once at program termination
//
void BtDriver_Exit( void )
{
   if (btDrvLoaded)
   {  // acq is still running - should never happen
      BtDriver_StopAcq();
   }
}

