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
 *    Philips or Temic tuners on Win32 systems. It uses WinDriver from KRF-Tech,
 *    which is a generic driver that allows direct access to I/O functions from
 *    user space by providing a set of generic I/O functions in a kernel module.
 *
 *    Unfortunately WinDriver is not freeware, so it may not be included with
 *    the Nextview decoder and has to be downloaded separately. If you want to
 *    compile nxtvepg for Windows, you have to take the header files from the
 *    WinDriver package, since they may not be redistributed either.
 *
 *    The code in this module is heavily based upon the Linux bttv driver
 *    and has been adapted for WinDriver by someone who calls himself
 *    "Espresso". His programming style is quite "special" (in lack of
 *    a better, non insulting description); I tried to clean up and comment
 *    the code as good as possible.
 *
 *
 *  Authors:
 *
 *    BT8x8-Parts (from bttv driver for Linux)
 *      Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
 *                             & Marcus Metzler (mocm@thp.uni-koeln.de)
 *
 *    Tuner and I2C bus (from bttv driver for Linux)
 *      Copyright (C) 1997,1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 *    WinDriver adaption (from MultiDec)
 *      Copyright (C) 2000 Espresso <echter_espresso@hotmail.com>
 *
 *    VBI only adaption and nxtvepg integration
 *      Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: btdrv4win.c,v 1.8 2001/01/07 19:48:49 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/vbidecode.h"
#include "epgvbi/btdrv.h"

#include "epgvbi/windrvr.h"
#include "epgvbi/bt848.h"


// ----------------------------------------------------------------------------
// Declaration of internal variables

#define WINDRVR_MIN_VERSION   400

#define VBI_LINES_PER_FIELD   16
#define VBI_LINES_PER_FRAME  (VBI_LINES_PER_FIELD * 2)
#define VBI_LINE_SIZE         2048
#define VBI_DATA_SIZE        (VBI_LINE_SIZE * VBI_LINES_PER_FRAME)
#define VBI_SPL               2044

#define RISC_CODE_LENGTH     (512 * sizeof(DWORD))
typedef DWORD PHYS;

static WD_DMA Risc_dma;
static WD_DMA Vbi_dma;


#define I2C_DELAY 0
#define I2C_TIMING (0x7<<4)
#define I2C_COMMAND (I2C_TIMING | BT848_I2C_SCL | BT848_I2C_SDA)


static BYTE TunerDeviceWrite, TunerDeviceRead;


static HANDLE Bt_Device_Handle;
static BT8X8_HANDLE hBT8X8;

static char OrgDriverName[128];
static bool NT=FALSE;


static CRITICAL_SECTION m_cCrit;    //semaphore for I2C access

struct TTunerType
{
  const char * name;
  WORD thresh1;         // frequency range for UHF, VHF-L, VHF_H
  WORD thresh2;
  BYTE VHF_L;
  BYTE VHF_H;
  BYTE UHF;
  BYTE config;
  BYTE I2C;
  WORD IFPCoff;
};


static struct TTunerType Tuners[] =
{
   { "none",          0                ,0                ,0x00,0x00,0x00,0x00,0x00,  0},
   { "Temic PAL",     2244/*16*140.25*/,7412/*16*463.25*/,0x02,0x04,0x01,0x8e,0xc2,623},
   { "Philips Pal I", 2244/*16*140.25*/,7412/*16*463.25*/,0xa0,0x90,0x30,0x8e,0xc0,623},
   { "Philips NTSC",  2516/*16*157.25*/,7220/*16*451.25*/,0xA0,0x90,0x30,0x8e,0xc0,732},
   { "Philips SECAM", 2692/*16*168.25*/,7156/*16*447.25*/,0xA7,0x97,0x37,0x8e,0xc0,623},
   { "Philips PAL",   2692/*16*168.25*/,7156/*16*447.25*/,0xA0,0x90,0x30,0x8e,0xc0,623},
   { "Temic NTSC",    2516/*16*157.25*/,7412/*16*463.25*/,0x02,0x04,0x01,0x8e,0xc2,732},
   { "Temic PAL I",   2720/*16*170.00*/,7200/*16*450.00*/,0xa0,0x90,0x30,0x8e,0xc2,623},
};
#define TUNERS_COUNT (sizeof(Tuners) / sizeof(struct TTunerType))
#define INVALID_INPUT_SOURCE  4

static int TvCardIndex = 0;
static int TvCardCount = 0;
static int TunerType = 0;
static int ThreadPrio = 0;
static BOOL INIT_PLL = FALSE;
static int InputSource = INVALID_INPUT_SOURCE;
static HANDLE VBI_Event=NULL;
static BOOL StopVBI;
static BOOL Capture_Videotext;
static BOOL Initialized = FALSE;
static int LastFrequency = 0;

extern char comm[1000];

EPGACQ_BUF *pVbiBuf;
static EPGACQ_BUF vbiBuf;


// ----------------------------------------------------------------------------
// Helper function to set user-configured priority in IRQ and VBI threads
//
static void SetAcqPriority( HANDLE thr )
{
   int prio;

   switch (ThreadPrio)
   {
      default:
      case 0: prio = THREAD_PRIORITY_NORMAL; break;
      case 1: prio = THREAD_PRIORITY_ABOVE_NORMAL; break;
      // skipping HIGHEST by (arbitrary) choice
      case 2: prio = THREAD_PRIORITY_TIME_CRITICAL; break;
   }
   SetThreadPriority(thr, prio);
}

// ----------------------------------------------------------------------------
// Utility functions to address the memory mapped Bt8x8 I/O registers

void MaskDataByte (int Offset, BYTE d, BYTE m)
{
   BYTE a;
   BYTE b;
   a = BT8X8_ReadByte (hBT8X8, BT8X8_AD_BAR0, Offset);
   b = (a & ~(m)) | ((d) & (m));
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, Offset, b);
}


void MaskDataWord (int Offset, WORD d, WORD m)
{
   WORD a;
   WORD b;
   a = BT8X8_ReadWord (hBT8X8, BT8X8_AD_BAR0, Offset);
   b = (a & ~(m)) | ((d) & (m));
   BT8X8_WriteWord (hBT8X8, BT8X8_AD_BAR0, Offset, b);
}


void AndDataByte (int Offset, BYTE d)
{
   BYTE a;
   BYTE b;
   a = BT8X8_ReadByte (hBT8X8, BT8X8_AD_BAR0, Offset);
   b = a & d;
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, Offset, b);
}


void AndDataWord (int Offset, short d)
{
   WORD a;
   WORD b;
   a = BT8X8_ReadWord (hBT8X8, BT8X8_AD_BAR0, Offset);
   b = a & d;
   BT8X8_WriteWord (hBT8X8, BT8X8_AD_BAR0, Offset, b);
}


void OrDataByte (int Offset, BYTE d)
{
   BYTE a;
   BYTE b;
   a = BT8X8_ReadByte (hBT8X8, BT8X8_AD_BAR0, Offset);
   b = a | d;
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, Offset, b);
}


void OrDataWord (int Offset, unsigned short d)
{
   WORD a;
   WORD b;
   a = BT8X8_ReadWord (hBT8X8, BT8X8_AD_BAR0, Offset);
   b = a | d;
   BT8X8_WriteWord (hBT8X8, BT8X8_AD_BAR0, Offset, b);
}


// ----------------------------------------------------------------------------
// Allocate memory for DMA
// - The difference to normal malloc is that you have to know the
//   physical address in RAM and that the memory must not be swapped out.
//
static BOOL
Alloc_DMA (DWORD dwSize, WD_DMA * dma, int Option)
{
   BZERO (*dma);
   if (Option == DMA_KERNEL_BUFFER_ALLOC)
      dma->pUserAddr = NULL;
   else
      dma->pUserAddr = malloc (dwSize);
   dma->dwBytes = dwSize;
   dma->dwOptions = Option;
   WD_DMALock (hBT8X8->hWD, dma);
   if (dma->hDma == 0)
   {
      if (dma->dwOptions != DMA_KERNEL_BUFFER_ALLOC)
         free (dma->pUserAddr);
      dma->pUserAddr = NULL;
      return (FALSE);
   }
   return TRUE;
}


static void
Free_DMA (WD_DMA * dma)
{
   LPVOID *MemPtr = NULL;
   if (dma == NULL)
      return;
   if (dma->hDma != 0)
   {
      if (dma->dwOptions != DMA_KERNEL_BUFFER_ALLOC)
         MemPtr = dma->pUserAddr;
      WD_DMAUnlock (hBT8X8->hWD, dma);
      if (MemPtr != NULL)
         free (MemPtr);
      dma->pUserAddr = NULL;

   }
}


static PHYS
GetPhysicalAddress (WD_DMA * dma, LPBYTE pLinear, DWORD dwSizeWanted, DWORD * pdwSizeAvailable)
{
   long Offset;
   int i;
   long sum;

   PHYS a;
   Offset = pLinear - (LPBYTE) dma->pUserAddr;
   sum = 0;
   i = 0;
   while ((unsigned) i < dma->dwPages)
   {
      if (sum + dma->Page[i].dwBytes > (unsigned) Offset)
      {
         Offset -= sum;
         a = (PHYS) ((LPBYTE) dma->Page[i].pPhysicalAddr + Offset);
         if (pdwSizeAvailable != NULL)
            *pdwSizeAvailable = dma->Page[i].dwBytes - Offset;
         return (a);
      }
      sum += dma->Page[i].dwBytes;
      i++;

   }
   return (0);
}


static PHYS
RiscLogToPhys (DWORD * pLog)
{
   return ((PHYS)Risc_dma.Page[0].pPhysicalAddr + (pLog - (DWORD *)Risc_dma.pUserAddr) * sizeof(DWORD));
}


// ----------------------------------------------------------------------------
// Allocate the required DMA memory areas
// - one area is for the VBI buffer
// - one area is for the RISC code that is executed in the DMA controller
//
bool Init_Memory (void)
{
   if (!Alloc_DMA (RISC_CODE_LENGTH, &Risc_dma, DMA_KERNEL_BUFFER_ALLOC))
   {
      MessageBox(NULL, "failed to allocate 2kB continguous RAM for DMA RISC code", "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      return(FALSE);
   }

   if (!Alloc_DMA (VBI_DATA_SIZE, &Vbi_dma, DMA_KERNEL_BUFFER_ALLOC))
   {
      MessageBox(NULL, "failed to allocate 64kB continguous RAM for VBI buffer", "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      Free_DMA(&Risc_dma);
      return(FALSE);
   }

   return (TRUE);
}

// ----------------------------------------------------------------------------
// Generate RISC program to control DMA transfer
// - This program is executed by the DMA controller in the Booktree chip.
// - It controls which of the captured data is written whereto.
// - In our case it's very simple: all VBI lines are written into one
//   consecutive buffer (actually there's a gap of 4 bytes between lines)
//
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
// - returns TRUE if the selected source is the TV tuner
//   XXX currently index 0 is hardwired is TV tuner input
//
bool BtDriver_SetInputSource( int inputIdx, bool keepOpen, bool * pIsTuner )
{
   // 0= Video_Tuner,
   // 1= Video_Ext1,
   // 2= Video_Ext2,
   // 3= Video_Ext3
   // 4= INVALID_INPUT_SOURCE
   assert(inputIdx < 4);

   // remember the input source for later
   InputSource = inputIdx;

   if (Initialized && (inputIdx < 4))
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

   if (pIsTuner != NULL)
      *pIsTuner = (inputIdx == 0);

   return TRUE;
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
   return NULL;
}

// ---------------------------------------------------------------------------
// Check if the current source has a video signal
//
bool BtDriver_IsVideoPresent( void )
{
   if (Initialized)
   {
      return ((BT8X8_ReadByte (hBT8X8, BT8X8_AD_BAR0, BT848_DSTATUS) & (BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC)) == (BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC)) ? TRUE : FALSE;
   }
   else
      return FALSE;
}

// ---------------------------------------------------------------------------
// Start the PLL (required on some cards only)
//
static void InitPll( void )
{
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_PLL_F_LO, 0xf9);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_PLL_F_HI, 0xdc);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_PLL_XCI, 0x8e);
   Sleep (500);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_TGCTRL, 0x08);
}

// ----------------------------------------------------------------------------
// Reset the Bt8x8 and program all relevant registers with constants
//
bool Init_BT_HardWare( void )
{
   // software reset, sets all registers to reset default values
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_SRESET, 0);
   Sleep(50);

   // start address for the DMA RISC code
   BT8X8_WriteDword (hBT8X8, BT8X8_AD_BAR0, BT848_RISC_STRT_ADD, (PHYS)Risc_dma.Page[0].pPhysicalAddr + 2 * sizeof(PHYS));
   //BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_TDEC, 0x00);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_COLOR_CTL, BT848_COLOR_CTL_GAMMA);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_ADELAY, 0x7f);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_BDELAY, 0x72);
   // disable capturing
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_CAP_CTL, 0x00);
   // max length of a VBI line
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_VBI_PACK_SIZE, 0xff);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_VBI_PACK_DEL, 1);

   // vertical delay for image data in the even and odd fields
   // IMPORTANT!  This defines the end of VBI capturing, i.e. the number of max. captured lines!
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_E_VDELAY_LO, 0x20);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_O_VDELAY_LO, 0x20);

   BT8X8_WriteWord (hBT8X8, BT8X8_AD_BAR0, BT848_GPIO_DMA_CTL, BT848_GPIO_DMA_CTL_PKTP_32 |
                                                               BT848_GPIO_DMA_CTL_PLTP1_16 |
                                                               BT848_GPIO_DMA_CTL_PLTP23_16 |
                                                               BT848_GPIO_DMA_CTL_GPINTC |
                                                               BT848_GPIO_DMA_CTL_GPINTI);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_GPIO_REG_INP, 0x00);
   // input format (PAL, NTSC etc.) and input source
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_IFORM, BT848_IFORM_MUX1 | BT848_IFORM_XTBOTH | BT848_IFORM_PAL_BDGHI);

   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_CONTRAST_LO, 0xd8);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_BRIGHT, 0x10);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_E_VSCALE_HI, 0x20);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_O_VSCALE_HI, 0x20);

   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_ADC, BT848_ADC_RESERVED | BT848_ADC_CRUSH);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_E_CONTROL, BT848_CONTROL_LDEC);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_O_CONTROL, BT848_CONTROL_LDEC);

   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_E_SCLOOP, 0x00);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_O_SCLOOP, 0x00);

   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_E_CONTROL, 0x03);
   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_O_CONTROL, 0x03);

   // interrupt mask; reset the status before enabling the interrupts
   BT8X8_WriteDword (hBT8X8, BT8X8_AD_BAR0, BT848_INT_STAT, (DWORD) 0x0fffffffUL);
   BT8X8_WriteDword (hBT8X8, BT8X8_AD_BAR0, BT848_INT_MASK, (1 << 23) | BT848_INT_RISCI);

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
   BT8X8_WriteDword (hBT8X8, BT8X8_AD_BAR0, BT848_I2C, (bCtrl << 1) | bData);
   I2CBus_wait (I2C_DELAY);
}

static BOOL
I2C_GetLine ()
{
   return BT8X8_ReadDword (hBT8X8, BT8X8_AD_BAR0, BT848_I2C) & 1;
}

static BYTE
I2C_Read (BYTE nAddr)
{
   DWORD i;
   volatile DWORD stat;

   // clear status bit ; BT848_INT_RACK is ro

   BT8X8_WriteDword (hBT8X8, BT8X8_AD_BAR0, BT848_INT_STAT, BT848_INT_I2CDONE);
   BT8X8_WriteDword (hBT8X8, BT8X8_AD_BAR0, BT848_I2C, (nAddr << 24) | I2C_COMMAND);

   for (i = 0x7fffffff; i; i--)
   {
      stat = BT8X8_ReadDword (hBT8X8, BT8X8_AD_BAR0, BT848_INT_STAT);
      if (stat & BT848_INT_I2CDONE)
         break;
   }

   if (!i)
      return (BYTE) - 1;
   if (!(stat & BT848_INT_RACK))
      return (BYTE) - 2;

   return (BYTE) ((BT8X8_ReadDword (hBT8X8, BT8X8_AD_BAR0, BT848_I2C) >> 8) & 0xFF);
}

static BOOL
I2C_Write (BYTE nAddr, BYTE nData1, BYTE nData2, BOOL bSendBoth)
{
   DWORD i;
   DWORD data;
   DWORD stat;

   /* clear status bit; BT848_INT_RACK is ro */
   BT8X8_WriteDword (hBT8X8, BT8X8_AD_BAR0, BT848_INT_STAT, BT848_INT_I2CDONE);

   data = (nAddr << 24) | (nData1 << 16) | I2C_COMMAND;
   if (bSendBoth)
      data |= (nData2 << 8) | BT848_I2C_W3B;
   BT8X8_WriteDword (hBT8X8, BT8X8_AD_BAR0, BT848_I2C, data);

   for (i = 0x7fffffff; i; i--)
   {
      stat = BT8X8_ReadDword (hBT8X8, BT8X8_AD_BAR0, BT848_INT_STAT);
      if (stat & BT848_INT_I2CDONE)
         break;
   }

   if (!i)
      return FALSE;
   if (!(stat & BT848_INT_RACK))
      return FALSE;

   return TRUE;
}


static BOOL
I2CBus_Lock ()
{
   EnterCriticalSection (&m_cCrit);
   return TRUE;
}

static BOOL
I2CBus_Unlock ()
{
   LeaveCriticalSection (&m_cCrit);
   return TRUE;
}

static void
I2CBus_Start ()
{
   I2C_SetLine (0, 1);
   I2C_SetLine (1, 1);
   I2C_SetLine (1, 0);
   I2C_SetLine (0, 0);
}

static void
I2CBus_Stop ()
{
   I2C_SetLine (0, 0);
   I2C_SetLine (1, 0);
   I2C_SetLine (1, 1);
}

static void
I2CBus_One ()
{
   I2C_SetLine (0, 1);
   I2C_SetLine (1, 1);
   I2C_SetLine (0, 1);
}

static void
I2CBus_Zero ()
{
   I2C_SetLine (0, 0);
   I2C_SetLine (1, 0);
   I2C_SetLine (0, 0);
}

static BOOL
I2CBus_Ack ()
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
// Set TSA5522 synthesizer frequency
//
static BOOL Tuner_SetFrequency( int TunerTyp, int wFrequency )
{
   BYTE config;
   WORD div;
   BOOL bAck;

   if (TunerTyp < TUNERS_COUNT)
   {
      if (wFrequency < Tuners[TunerTyp].thresh1)
         config = Tuners[TunerTyp].VHF_L;
      else if (wFrequency < Tuners[TunerTyp].thresh2)
         config = Tuners[TunerTyp].VHF_H;
      else
         config = Tuners[TunerTyp].UHF;

      div = wFrequency + Tuners[TunerTyp].IFPCoff;

      div &= 0x7fff;
      I2CBus_Lock ();              // Lock/wait

      if (!I2CBus_Write ((BYTE) TunerDeviceWrite, (BYTE) ((div >> 8) & 0x7f), (BYTE) (div & 0xff), TRUE))
      {
         Sleep (1);
         if (!I2CBus_Write ((BYTE) TunerDeviceWrite, (BYTE) ((div >> 8) & 0x7f), (BYTE) (div & 0xff), TRUE))
         {
            Sleep (1);
            if (!I2CBus_Write ((BYTE) TunerDeviceWrite, (BYTE) ((div >> 8) & 0x7f), (BYTE) (div & 0xff), TRUE))
            {
               debug0("Tuner_SetFrequency: i2c write failed for word #1");
               I2CBus_Unlock ();   // Unlock

               return (FALSE);
            }
         }
      }
      if (!(bAck = I2CBus_Write (TunerDeviceWrite, Tuners[TunerTyp].config, config, TRUE)))
      {
         Sleep (1);
         if (!(bAck = I2CBus_Write (TunerDeviceWrite, Tuners[TunerTyp].config, config, TRUE)))
         {
            Sleep (1);
            if (!(bAck = I2CBus_Write (TunerDeviceWrite, Tuners[TunerTyp].config, config, TRUE)))
            {
               debug0("Tuner_SetFrequency: i2c write failed for word #2");
            }
         }
      }
      I2CBus_Unlock ();            // Unlock

      if (!bAck)
         return FALSE;
   }
   else
      debug1("Tuner_SetFrequency: illegal tuner idx %d", TunerTyp);

   return TRUE;
}

// ---------------------------------------------------------------------------
// Auto-detect a tuner on the I2C bus
//
BOOL Init_Tuner( int TunerNr )
{
   unsigned char j;
   bool result = FALSE;

   if (TunerNr < TUNERS_COUNT)
   {
      InitializeCriticalSection (&m_cCrit);

      j = 0xc0;
      TunerDeviceRead = Tuners[TunerNr].I2C = j;
      TunerDeviceWrite = Tuners[TunerNr].I2C = j;

      while ((j <= 0xce) && (I2CBus_AddDevice ((BYTE) j) == FALSE))
      {
         j++;
         TunerDeviceRead = Tuners[TunerNr].I2C = j;
         TunerDeviceWrite = Tuners[TunerNr].I2C = j;
      }

      if (j <= 0xce)
      {
         dprintf1("Tuner I2C-Bus I/O 0x%02x", j);
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
// Open the driver device and allocate I/O resources
// - this and the following functions have been generated by the "Windriver wizard"
//
static int
BT8X8_Open (BT8X8_HANDLE * phBT8X8)
{
   WD_VERSION ver;
   WD_PCI_SCAN_CARDS pciScan;
   WD_PCI_CARD_INFO pciCardInfo;
   BT8X8_HANDLE hBT8X8;
   uint idx, tvCardIdx;
   int Ret = BT8X8_OPEN_RESULT_OK;

   hBT8X8 = (BT8X8_HANDLE) malloc(sizeof (BT8X8_STRUCT));
   if (hBT8X8 == NULL)
   {  // malloc failure
      Ret = BT8X8_OPEN_RESULT_MALLOC;
      goto Exit;
   }

   BZERO (*hBT8X8);

   hBT8X8->cardReg.hCard = 0;
   hBT8X8->hWD = WD_Open();

   // check if handle valid & version OK
   if (hBT8X8->hWD == INVALID_HANDLE_VALUE)
   {
      //Cannot open WinDriver device
      Ret = BT8X8_OPEN_RESULT_DRIVER;
      goto Exit;
   }


   // check if driver version matches include file
   BZERO(ver);
   WD_Version(hBT8X8->hWD, &ver);
   //if (ver.dwVer < WD_VER)
   if (ver.dwVer < WINDRVR_MIN_VERSION)
   {
      sprintf(comm, "WARNING: WinDriver version mismatch: found %d, expected at least %d (compiled with %d)\n"
                    "Please refer to README.txt for further information.",
                    ver.dwVer, WINDRVR_MIN_VERSION, WD_VER);
      MessageBox(NULL, comm, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      //Ret = BT8X8_OPEN_RESULT_VERSION;
      //goto Exit;
   }

   // scan for all cards on PCI bus
   BZERO (pciScan);
   WD_PciScanCards (hBT8X8->hWD, &pciScan);
   //debug1("PCI scan: found %d cards", pciScan.dwCards);
   TvCardCount = 0;
   tvCardIdx = 0xffff;
   for (idx=0; idx < pciScan.dwCards; idx++)
   {
      if (pciScan.cardId[idx].dwVendorId == PCI_VENDOR_ID_BROOKTREE)
      {
         switch (pciScan.cardId[idx].dwDeviceId)
         {
            case PCI_DEVICE_ID_BT848:
            case PCI_DEVICE_ID_BT849:
            case PCI_DEVICE_ID_BT878:
            case PCI_DEVICE_ID_BT879:
               if (TvCardCount == TvCardIndex)
                  tvCardIdx = idx;
               TvCardCount += 1;
               break;
         }
      }
   }
   if (tvCardIdx == 0xffff)
   {
      // error - Cannot find PCI card with the given index
      Ret = BT8X8_OPEN_RESULT_CARDIDX;
      goto Exit;
   }

   BZERO (pciCardInfo);
   pciCardInfo.pciSlot = pciScan.cardSlot[tvCardIdx];
   WD_PciGetCardInfo (hBT8X8->hWD, &pciCardInfo);
   hBT8X8->pciSlot = pciCardInfo.pciSlot;
   hBT8X8->cardReg.Card = pciCardInfo.Card;
   hBT8X8->fUseInt = TRUE;

   // make interrupt resource sharable
   for (idx = 0; idx < hBT8X8->cardReg.Card.dwItems; idx++)
   {
      WD_ITEMS *pItem = &hBT8X8->cardReg.Card.Item[idx];
      /*
      switch(pItem->item)
      {
         case ITEM_INTERRUPT:
            debug3("Card item #%d: IRQ %d (notSharable=%d)", idx, pItem->I.Int.dwInterrupt, pItem->fNotSharable);
            break;
         case ITEM_MEMORY:
            debug3("Card item #%d: MEMORY 0x%X, %d bytes", idx, pItem->I.Mem.dwPhysicalAddr, pItem->I.Mem.dwBytes);
            break;
         case ITEM_IO:
            debug3("Card item #%d: IO %d", idx, pItem->I.IO.dwAddr, pItem->I.IO.dwBytes);
            break;
         case ITEM_BUS:
            debug4("Card item #%d: BUS type=%d number=%d slot=%d", idx, pItem->I.Bus.dwBusType, pItem->I.Bus.dwBusNum, pItem->I.Bus.dwSlotFunc);
            break;
      }
      */
      if (pItem->item == ITEM_INTERRUPT)
         pItem->fNotSharable = FALSE;
   }

   hBT8X8->cardReg.fCheckLockOnly = FALSE;
   WD_CardRegister (hBT8X8->hWD, &hBT8X8->cardReg);
   if (hBT8X8->cardReg.hCard == 0)
   {
      Ret = BT8X8_OPEN_RESULT_REGISTER;
      goto Exit;
   }

   if (!BT8X8_DetectCardElements (hBT8X8))
   {
      Ret = BT8X8_OPEN_RESULT_ELEMS;
      goto Exit;
   }

   // Open finished OK
   *phBT8X8 = hBT8X8;
   return Ret;

 Exit:
   // Error during Open
   if (hBT8X8->cardReg.hCard)
      WD_CardUnregister (hBT8X8->hWD, &hBT8X8->cardReg);
   if (hBT8X8->hWD != INVALID_HANDLE_VALUE)
      WD_Close (hBT8X8->hWD);
   if (hBT8X8 != NULL)
      free (hBT8X8);
   return Ret;
}


static BOOL
BT8X8_DetectCardElements (BT8X8_HANDLE hBT8X8)
{
   DWORD i;
   DWORD ad_sp;

   BZERO (hBT8X8->Int);
   BZERO (hBT8X8->addrDesc);

   for (i = 0; i < hBT8X8->cardReg.Card.dwItems; i++)
   {
      WD_ITEMS *pItem = &hBT8X8->cardReg.Card.Item[i];

      switch (pItem->item)
      {
         case ITEM_MEMORY:
         case ITEM_IO:
            {
               DWORD dwBytes;
               DWORD dwPhysAddr;
               BOOL fIsMemory;
               if (pItem->item == ITEM_MEMORY)
               {
                  dwBytes = pItem->I.Mem.dwBytes;
                  dwPhysAddr = pItem->I.Mem.dwPhysicalAddr;
                  fIsMemory = TRUE;
               }
               else
               {
                  dwBytes = pItem->I.IO.dwBytes;
                  dwPhysAddr = pItem->I.IO.dwAddr;
                  fIsMemory = FALSE;
               }

               for (ad_sp = 0; ad_sp < BT8X8_ITEMS; ad_sp++)
               {
                  DWORD dwPCIAddr;
                  DWORD dwPCIReg;

                  if (BT8X8_IsAddrSpaceActive (hBT8X8, ad_sp))
                     continue;
                  if (ad_sp < BT8X8_AD_EPROM)
                     dwPCIReg = PCI_BAR0 + 4 * ad_sp;
                  else
                     dwPCIReg = PCI_ERBAR;
                  dwPCIAddr = BT8X8_ReadPCIReg (hBT8X8, dwPCIReg);
                  if (dwPCIAddr & 1)
                  {
                     if (fIsMemory)
                        continue;
                     dwPCIAddr &= ~(0x3);
                  }
                  else
                  {
                     if (!fIsMemory)
                        continue;
                     dwPCIAddr &= ~(0xf);
                  }
                  if (dwPCIAddr == dwPhysAddr)
                     break;
               }
               if (ad_sp < BT8X8_ITEMS)
               {
                  DWORD j;
                  hBT8X8->addrDesc[ad_sp].fActive = TRUE;
                  hBT8X8->addrDesc[ad_sp].index = i;
                  hBT8X8->addrDesc[ad_sp].fIsMemory = fIsMemory;
                  hBT8X8->addrDesc[ad_sp].dwMask = 0;
                  for (j = 1; j < dwBytes && j != 0x80000000; j *= 2)
                  {
                     hBT8X8->addrDesc[ad_sp].dwMask =
                        (hBT8X8->addrDesc[ad_sp].dwMask << 1) | 1;
                  }
               }
            }
            break;
         case ITEM_INTERRUPT:
            if (hBT8X8->Int.Int.hInterrupt)
               return FALSE;
            hBT8X8->Int.Int.hInterrupt = pItem->I.Int.hInterrupt;
            break;
      }
   }

   // check that all the items needed were found
   // check if interrupt found
   if (hBT8X8->fUseInt && !hBT8X8->Int.Int.hInterrupt)
   {
      return FALSE;
   }

   // check that at least one memory space was found
   for (i = 0; i < BT8X8_ITEMS; i++)
      if (BT8X8_IsAddrSpaceActive (hBT8X8, i))
         break;
   if (i == BT8X8_ITEMS)
      return FALSE;

   return TRUE;
}


static void
BT8X8_Close (BT8X8_HANDLE hBT8X8)
{

   BT8X8_WriteByte (hBT8X8, BT8X8_AD_BAR0, BT848_SRESET, 0);

   // disable interrupts
   if (BT8X8_IntIsEnabled (hBT8X8))
      BT8X8_IntDisable (hBT8X8);

   // unregister card
   if (hBT8X8->cardReg.hCard)
      WD_CardUnregister (hBT8X8->hWD, &hBT8X8->cardReg);

   // close WinDriver
   WD_Close (hBT8X8->hWD);
   free (hBT8X8);
}

// ---------------------------------------------------------------------------
// utility functions to call WinDriver I/O functions
//
static void
BT8X8_WritePCIReg (BT8X8_HANDLE hBT8X8, DWORD dwReg, DWORD dwData)
{
   WD_PCI_CONFIG_DUMP pciCnf;

   BZERO (pciCnf);
   pciCnf.pciSlot = hBT8X8->pciSlot;
   pciCnf.pBuffer = &dwData;
   pciCnf.dwOffset = dwReg;
   pciCnf.dwBytes = 4;
   pciCnf.fIsRead = FALSE;
   WD_PciConfigDump (hBT8X8->hWD, &pciCnf);
}

static DWORD
BT8X8_ReadPCIReg (BT8X8_HANDLE hBT8X8, DWORD dwReg)
{
   WD_PCI_CONFIG_DUMP pciCnf;
   DWORD dwVal;

   BZERO (pciCnf);
   pciCnf.pciSlot = hBT8X8->pciSlot;
   pciCnf.pBuffer = &dwVal;
   pciCnf.dwOffset = dwReg;
   pciCnf.dwBytes = 4;
   pciCnf.fIsRead = TRUE;
   WD_PciConfigDump (hBT8X8->hWD, &pciCnf);
   return dwVal;
}


static BOOL
BT8X8_IsAddrSpaceActive (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace)
{
   return hBT8X8->addrDesc[addrSpace].fActive;
}

// General read/write function
static void
BT8X8_ReadWriteBlock (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset, BOOL fRead, PVOID buf, DWORD dwBytes, BT8X8_MODE mode)
{
   WD_TRANSFER trans;
   BOOL fMem = hBT8X8->addrDesc[addrSpace].fIsMemory;
   // safty check: is the address range active
   if (!BT8X8_IsAddrSpaceActive (hBT8X8, addrSpace))
      return;
   BZERO (trans);
   if (fRead)
   {
      if (mode == BT8X8_MODE_BYTE)
         trans.cmdTrans = fMem ? RM_SBYTE : RP_SBYTE;
      else if (mode == BT8X8_MODE_WORD)
         trans.cmdTrans = fMem ? RM_SWORD : RP_SWORD;
      else if (mode == BT8X8_MODE_DWORD)
         trans.cmdTrans = fMem ? RM_SDWORD : RP_SDWORD;
   }
   else
   {
      if (mode == BT8X8_MODE_BYTE)
         trans.cmdTrans = fMem ? WM_SBYTE : WP_SBYTE;
      else if (mode == BT8X8_MODE_WORD)
         trans.cmdTrans = fMem ? WM_SWORD : WP_SWORD;
      else if (mode == BT8X8_MODE_DWORD)
         trans.cmdTrans = fMem ? WM_SDWORD : WP_SDWORD;
   }
   if (fMem)
      trans.dwPort = hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.Mem.dwTransAddr;
   else
      trans.dwPort = hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.IO.dwAddr;
   trans.dwPort += dwOffset;

   trans.fAutoinc = TRUE;
   trans.dwBytes = dwBytes;
   trans.dwOptions = 0;
   trans.Data.pBuffer = buf;
   WD_Transfer (hBT8X8->hWD, &trans);
}

static BYTE
BT8X8_ReadByte (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset)
{
   BYTE data;
   if (hBT8X8->addrDesc[addrSpace].fIsMemory)
   {
      PBYTE pData = (PBYTE) (hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.Mem.dwUserDirectAddr + dwOffset);
      data = *pData;            // read from the memory mapped range directly
   }
   else
      BT8X8_ReadWriteBlock (hBT8X8, addrSpace, dwOffset, TRUE, &data, sizeof (BYTE), BT8X8_MODE_BYTE);
   return data;
}

static WORD
BT8X8_ReadWord (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset)
{
   WORD data;
   if (hBT8X8->addrDesc[addrSpace].fIsMemory)
   {
      PWORD pData = (PWORD) (hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.Mem.dwUserDirectAddr + dwOffset);
      data = *pData;            // read from the memory mapped range directly

   }
   else
      BT8X8_ReadWriteBlock (hBT8X8, addrSpace, dwOffset, TRUE, &data, sizeof (WORD), BT8X8_MODE_WORD);
   return data;
}

static DWORD
BT8X8_ReadDword (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset)
{
   DWORD data;
   if (hBT8X8->addrDesc[addrSpace].fIsMemory)
   {
      PDWORD pData = (PDWORD) (hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.Mem.dwUserDirectAddr + dwOffset);
      data = *pData;            // read from the memory mapped range directly

   }
   else
      BT8X8_ReadWriteBlock (hBT8X8, addrSpace, dwOffset, TRUE, &data, sizeof (DWORD), BT8X8_MODE_DWORD);
   return data;
}

static void
BT8X8_WriteByte (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset, BYTE data)
{
   if (hBT8X8->addrDesc[addrSpace].fIsMemory)
   {
      PBYTE pData = (PBYTE) (hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.Mem.dwUserDirectAddr + dwOffset);
      *pData = data;            // write to the memory mapped range directly

   }
   else
      BT8X8_ReadWriteBlock (hBT8X8, addrSpace, dwOffset, FALSE, &data, sizeof (BYTE), BT8X8_MODE_BYTE);
}

static void
BT8X8_WriteWord (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset, WORD data)
{
   if (hBT8X8->addrDesc[addrSpace].fIsMemory)
   {
      PWORD pData = (PWORD) (hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.Mem.dwUserDirectAddr + dwOffset);
      *pData = data;            // write to the memory mapped range directly

   }
   else
      BT8X8_ReadWriteBlock (hBT8X8, addrSpace, dwOffset, FALSE, &data, sizeof (WORD), BT8X8_MODE_WORD);
}

static void
BT8X8_WriteDword (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset, DWORD data)
{
   if (hBT8X8->addrDesc[addrSpace].fIsMemory)
   {
      PDWORD pData = (PDWORD) (hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.Mem.dwUserDirectAddr + dwOffset);
      *pData = data;            // write to the memory mapped range directly

   }
   else
      BT8X8_ReadWriteBlock (hBT8X8, addrSpace, dwOffset, FALSE, &data, sizeof (DWORD), BT8X8_MODE_DWORD);
}

// ---------------------------------------------------------------------------
// Frame interrupt function
// - IRQ is invoked by the RISC code after the last VBI line in the even field
//
static DWORD WINAPI
BT8X8_IntThread (PVOID pData)
{
   BT8X8_HANDLE hBT8X8 = (BT8X8_HANDLE) pData;
   int status;

   SetAcqPriority(GetCurrentThread());

   for (;;)
   {
      WD_IntWait (hBT8X8->hWD, &hBT8X8->Int.Int);
      if (hBT8X8->Int.Int.fStopped)
         break;                 // WD_IntDisable() was called

      status = hBT8X8->Int.Trans[0].Data.Dword;
      if (status & BT848_INT_RISCI)
      {
         if (Capture_Videotext)
         {
            SetEvent (VBI_Event);
         }
      }
   }
   return 0;
}

static BOOL
BT8X8_IntEnable (BT8X8_HANDLE hBT8X8, BT8X8_INT_HANDLER funcIntHandler)
{
   ULONG threadId;
   BT8X8_ADDR addrSpace;

   if (!hBT8X8->fUseInt)
      return FALSE;
   // check if interrupt is already enabled
   if (hBT8X8->Int.hThread)
      return FALSE;
   BZERO (hBT8X8->Int.Trans);

   addrSpace = BT8X8_AD_BAR0;   // put the address space of the register here

   hBT8X8->Int.Trans[0].dwPort = hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.Mem.dwTransAddr + BT848_INT_STAT;
   hBT8X8->Int.Trans[0].cmdTrans = RM_DWORD;

   hBT8X8->Int.Trans[1].dwPort = hBT8X8->cardReg.Card.Item[hBT8X8->addrDesc[addrSpace].index].I.Mem.dwTransAddr + BT848_INT_STAT;
   hBT8X8->Int.Trans[1].cmdTrans = WM_DWORD;
   hBT8X8->Int.Trans[1].Data.Dword = (DWORD) 0x0fffffff;        // put the data to write to the control register here

   hBT8X8->Int.Int.dwCmds = 2;
   hBT8X8->Int.Int.Cmd = hBT8X8->Int.Trans;
   hBT8X8->Int.Int.dwOptions = INTERRUPT_CMD_COPY | INTERRUPT_LEVEL_SENSITIVE;

   WD_IntEnable (hBT8X8->hWD, &hBT8X8->Int.Int);
   // check if WD_IntEnable failed
   if (!hBT8X8->Int.Int.fEnableOk)
      return FALSE;

   // create interrupt handler thread
   hBT8X8->Int.hThread = CreateThread (0, 0x1000, BT8X8_IntThread, hBT8X8, 0, &threadId);

   return TRUE;
}


static void
BT8X8_IntDisable (BT8X8_HANDLE hBT8X8)
{
   if (!hBT8X8->fUseInt)
      return;
   if (!hBT8X8->Int.hThread)
      return;
   WD_IntDisable (hBT8X8->hWD, &hBT8X8->Int.Int);
   WaitForSingleObject (hBT8X8->Int.hThread, INFINITE);
   hBT8X8->Int.hThread = NULL;
}

static BOOL
BT8X8_IntIsEnabled (BT8X8_HANDLE hBT8X8)
{
   if (!hBT8X8->fUseInt)
      return FALSE;
   if (!hBT8X8->Int.hThread)
      return FALSE;
   return TRUE;
}



/****************************************************************************
*
*    FUNCTION: InstallDriver( IN SC_HANDLE, IN LPCTSTR, IN LPCTSTR)
*
*    PURPOSE: Creates a driver service.
*
****************************************************************************/
static BOOL
InstallDriver (IN SC_HANDLE SchSCManager, IN LPCTSTR DriverName, IN LPCTSTR ServiceExe)
{
   SC_HANDLE schService;

   //
   // NOTE: This creates an entry for a standalone driver. If this
   //       is modified for use with a driver that requires a Tag,
   //       Group, and/or Dependencies, it may be necessary to
   //       query the registry for existing driver information
   //       (in order to determine a unique Tag, etc.).
   //

   schService = CreateService (SchSCManager,    // SCManager database
                                DriverName,     // name of service
                                DriverName,     // name to display
                                SERVICE_ALL_ACCESS,     // desired access
                                SERVICE_KERNEL_DRIVER,  // service type
                                SERVICE_DEMAND_START,   // start type
                                SERVICE_ERROR_NORMAL,   // error control type
                                ServiceExe,     // service's binary
                                NULL,   // no load ordering group
                                NULL,   // no tag identifier
                                NULL,   // no dependencies
                                NULL,   // LocalSystem account
                                NULL    // no password
      );
   if (schService == NULL)
      return FALSE;

   CloseServiceHandle (schService);

   return TRUE;
}


/****************************************************************************
*
*    FUNCTION: StartDriver( IN SC_HANDLE, IN LPCTSTR)
*
*    PURPOSE: Starts the driver service.
*
****************************************************************************/
static BOOL
StartDriver (IN SC_HANDLE SchSCManager, IN LPCTSTR DriverName)
{
   SC_HANDLE schService;
   BOOL ret;

   schService = OpenService (SchSCManager,
                             DriverName,
                             SERVICE_ALL_ACCESS
      );
   if (schService == NULL)
      return FALSE;

   ret = StartService (schService, 0, NULL);

   if ((ret == FALSE) && (GetLastError () == ERROR_SERVICE_ALREADY_RUNNING))
      ret = TRUE;;

   CloseServiceHandle (schService);

   return ret;
}


/****************************************************************************
*
*    FUNCTION: StopDriver( IN SC_HANDLE, IN LPCTSTR)
*
*    PURPOSE: Has the configuration manager stop the driver (unload it)
*
****************************************************************************/
static BOOL
StopDriver (IN SC_HANDLE SchSCManager, IN LPCTSTR DriverName)
{
   SC_HANDLE schService;
   BOOL ret;
   SERVICE_STATUS serviceStatus;

   schService = OpenService (SchSCManager, DriverName, SERVICE_ALL_ACCESS);
   if (schService == NULL)
      return FALSE;

   ret = ControlService (schService, SERVICE_CONTROL_STOP, &serviceStatus);

   CloseServiceHandle (schService);

   return ret;
}


/****************************************************************************
*
*    FUNCTION: RemoveDriver( IN SC_HANDLE, IN LPCTSTR)
*
*    PURPOSE: Deletes the driver service.
*
****************************************************************************/
static BOOL
RemoveDriver (IN SC_HANDLE SchSCManager, IN LPCTSTR DriverName)
{
   SC_HANDLE schService;
   BOOL ret;
   return TRUE;

   schService = OpenService (SchSCManager,
                             DriverName,
                             SERVICE_ALL_ACCESS
      );

   if (schService == NULL)
      return FALSE;

   ret = DeleteService (schService);

   CloseServiceHandle (schService);

   return ret;
}



/****************************************************************************
*
*    FUNCTION: OpenDevice( IN LPCTSTR, HANDLE *)
*
*    PURPOSE: Opens the device and returns a handle if desired.
*
****************************************************************************/
static BOOL
OpenDevice (IN LPCTSTR DriverName, HANDLE * lphDevice)
{
   TCHAR completeDeviceName[64];
   HANDLE hDevice;

   //
   // Create a \\.\XXX device name that CreateFile can use
   //
   // NOTE: We're making an assumption here that the driver
   //       has created a symbolic link using it's own name
   //       (i.e. if the driver has the name "XXX" we assume
   //       that it used IoCreateSymbolicLink to create a
   //       symbolic link "\DosDevices\XXX". Usually, there
   //       is this understanding between related apps/drivers.
   //
   //       An application might also peruse the DEVICEMAP
   //       section of the registry, or use the QueryDosDevice
   //       API to enumerate the existing symbolic links in the
   //       system.
   //

   wsprintf (completeDeviceName, TEXT ("\\\\.\\%s"), DriverName);

   hDevice = CreateFile (completeDeviceName,
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL
      );
   if (hDevice == ((HANDLE) - 1))
      return FALSE;

   // If user wants handle, give it to them.  Otherwise, just close it.
   if (lphDevice)
      *lphDevice = hDevice;
   else
      CloseHandle (hDevice);

   return TRUE;
}


/****************************************************************************
*
*    FUNCTION: UnloadDeviceDriver( const TCHAR *)
*
*    PURPOSE: Stops the driver and has the configuration manager unload it.
*
****************************************************************************/
static BOOL
UnloadDeviceDriver (const TCHAR * Name, BOOL DRemove)
{
   SC_HANDLE schSCManager;

   schSCManager = OpenSCManager (NULL,  // machine (NULL == local)
                                  NULL,         // database (NULL == default)
                                  SC_MANAGER_ALL_ACCESS         // access required
      );

   StopDriver (schSCManager, Name);
   if (DRemove == TRUE)
      RemoveDriver (schSCManager, Name);

   CloseServiceHandle (schSCManager);

   return TRUE;
}


/****************************************************************************
*
*    FUNCTION: LoadDeviceDriver( const TCHAR, const TCHAR, HANDLE *)
*
*    PURPOSE: Registers a driver with the system configuration manager
*        and then loads it.
*
****************************************************************************/
static BOOL
LoadDeviceDriver (const TCHAR * Name, const TCHAR * Path, HANDLE * lphDevice, BOOL Install)
{
   SC_HANDLE schSCManager;
   BOOL okay;

   schSCManager = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

   // Ignore success of installation: it may already be installed.
   if (Install == TRUE)
      InstallDriver (schSCManager, Name, Path);

   // Ignore success of start: it may already be started.
   okay = StartDriver (schSCManager, Name);

   CloseServiceHandle (schSCManager);

   return okay;
}



// ----------------------------------------------------------------------------
// Start and initialize the windriver
//
static bool Init_WinDriver( void )
{
   OSVERSIONINFO osvi;
   char Path[255];
   int ret;
   long RegRet;
   HKEY hKey;
   bool result;

   NT = FALSE;
   result = TRUE;

   osvi.dwOSVersionInfoSize = sizeof (osvi);
   if (GetVersionEx (&osvi) == TRUE)
   {
      if (osvi.dwPlatformId == VER_PLATFORM_WIN32_NT)
      {
         NT = TRUE;
      }
   }

   // Hack (by Espresso)
   // Ich benutze eine Windriver 4.00 Evaluation Version ( Gutes Teil ))
   // Eigentlich nur 30 Tage Laufzeit aber ein Löschen folgender Registry-Werte
   // verlängert die Laufzeit :-)
   // Diese Version von Windriver kann im Internet von //www.krftech.com gesaugt werden
   // Zur neuen Übersetzung wird die Windrvr.h benötigt ( Ist in der Evaluation enthalten )

   if (NT == TRUE)
      RegRet = RegOpenKey (HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Network", &hKey);
   else
      RegRet = RegOpenKey (HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Network", &hKey);
   if (RegRet == ERROR_SUCCESS)
   {
      // ( Windriver 4.00 )
      RegDeleteValue(hKey,"DriverFigIA");
      RegDeleteValue(hKey,"DriverFigLA");
      RegDeleteValue(hKey,"DriverFigUA");
      // ( Windriver 4.31 )
      RegDeleteValue(hKey,"DriverFigId");
      RegDeleteValue(hKey,"DriverFigLd");
      RegDeleteValue(hKey,"DriverFigUd");

      RegCloseKey (hKey);
   }

   if (NT == TRUE)
   {
      GetCurrentDirectory( sizeof(Path), Path );
      strcat(Path,"\\WINDRVR.SYS");
      //strcpy (Path, "SYSTEM32\\DRIVERS\\WINDRVR.SYS");

      if (OrgDriverName[0] != 0x00)
      {
         UnloadDeviceDriver ((const char *) OrgDriverName, FALSE);
         Sleep (500);
      }

      if (LoadDeviceDriver ("WinDriver", Path, &Bt_Device_Handle, TRUE) == FALSE)
      {
         MessageBox(NULL, "Failed to load NT device driver WinDrvr.sys\n"
                          "Have you copied the driver file into the nxtvepg working directory?\n"
                          "Please refer to README.txt for further information.",
                    "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         result = FALSE;
      }
   }
   else
   {
      Bt_Device_Handle = CreateFile ("\\\\.\\WinDrvr.VXD", 0, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, 0);
      if (Bt_Device_Handle == INVALID_HANDLE_VALUE)
      {
         MessageBox(NULL, "Failed to load Win95/98 device driver WinDrvr.vxd"
                          "Have you copied the driver file into the nxtvepg working directory?\n"
                          "Please refer to README.txt for further information.",
                    "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         result = FALSE;
      }
   }

   if (result != FALSE)
   {
      ret = BT8X8_Open (&hBT8X8);
      if (ret == BT8X8_OPEN_RESULT_OK)
      {
         dprintf0("BT878 gefunden, VendorID=0x109e, DeviceID=0x036e");
      }
      else if (ret == BT8X8_OPEN_RESULT_DRIVER)
      {
         MessageBox(NULL, "Failed to start the device driver for Bt8x8.\n"
                          "Do you have permission to start a driver?\n"
                          "Please refer to README.txt for further information.",
                    "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         result = FALSE;
      }
      else if (ret == BT8X8_OPEN_RESULT_REGISTER)
      {
         MessageBox(NULL, "Bt8x8 card cannot be locked - already in use?\n"
                          "Please refer to README.txt for further information.",
                    "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         result = FALSE;
      }
      else
      {
         sprintf(comm, "Bt8x8 card #%d not found (found %d cards)", TvCardIndex, TvCardCount);
         MessageBox(NULL, comm, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         result = FALSE;
      }

      if (result != FALSE)
      {
         if (BT8X8_IntEnable (hBT8X8, NULL) == FALSE)
         {
            MessageBox(NULL, "Failed to enable Interrupt for Bt8x8 card.\n"
                             "Please refer to README.txt for further information.",
                       "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            result = FALSE;
         }
      }

      if ( (result == FALSE) && NT )
      {
         UnloadDeviceDriver ("WinDriver", TRUE);
         Sleep (500);
         if (OrgDriverName[0] != 0x00)
            LoadDeviceDriver ((const char *) OrgDriverName, Path, &Bt_Device_Handle, FALSE);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Shut down the driver and free all resources
//
static void BtDriver_Unload( void )
{
   if (Initialized)
   {
      CloseHandle (VBI_Event);
      VBI_Event = NULL;

      BT8X8_Close (hBT8X8);
      if (NT == TRUE)
      {
         UnloadDeviceDriver ("WinDriver", TRUE);
         if (OrgDriverName[0] != 0x00)
            LoadDeviceDriver ((const char *) OrgDriverName, "", &Bt_Device_Handle, FALSE);
      }

      Free_DMA(&Risc_dma);
      Free_DMA(&Vbi_dma);

      LastFrequency = 0;
      InputSource = INVALID_INPUT_SOURCE;
      Initialized = FALSE;
   }
}

// ----------------------------------------------------------------------------
// Boot the driver, allocate resources and initialize all subsystems
//
static bool BtDriver_Load( void )
{
   bool result;

   Capture_Videotext = FALSE;

   result = Init_WinDriver();
   if (result != FALSE)
   {
      Risc_dma.hDma = Vbi_dma.hDma = 0;
      if ( Init_Memory() )
      {
         // must be set to TRUE before the set funcs are called
         Initialized = TRUE;

         if (VBI_Event == NULL)
            VBI_Event = CreateEvent(NULL, FALSE, FALSE, NULL);
         ResetEvent(VBI_Event);

         // initialize all bt848 registers
         Init_BT_HardWare();

         if (INIT_PLL == TRUE)
         {
            InitPll();
         }

         // auto-detect the tuner on the I2C bus
         Init_Tuner(TunerType);

         if (LastFrequency != 0)
         {  // if freq already set, apply it now
            Tuner_SetFrequency(TunerType, LastFrequency);
         }
         if (InputSource != INVALID_INPUT_SOURCE)
         {  // if source already set, apply it now
            BtDriver_SetInputSource(InputSource, FALSE, NULL);
         }

         result = TRUE;
      }
      else
      {  // driver boot failed - abort
         BtDriver_Unload();
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Set user-configurable hardware parameters
// - called at program start and after config change
// - Important: input source and tuner freq must be set afterwards
//
void BtDriver_Configure( int cardIndex, int tunerType, int pll, int prio )
{
   int oldCardIdx = TvCardIndex;
   bool pllChange, tunerChange;

   tunerChange = (tunerType != TunerType);
   TunerType = tunerType;
   ThreadPrio = prio;
   pllChange = (pll && !INIT_PLL);
   INIT_PLL = pll;
   TvCardIndex = cardIndex;

   if (Initialized)
   {  // acquisition already running -> must change parameters on the fly

      if (oldCardIdx != cardIndex)
      {  // change of TV card -> unload and reload driver
         if (Capture_Videotext)
         {
            BtDriver_StopAcq();
            BtDriver_StartAcq();
         }
         else
         {  // acq not running, but driver loaded (this mode is currently not used)
            BtDriver_Unload();
            // load the driver with the new params
            BtDriver_Load();
         }
      }
      else
      {  // same card index: just update tuner type and PLL
         if (tunerChange && (TunerType != 0) && (InputSource == 0))
         {
            Init_Tuner(TunerType);
         }
         if (pllChange)
         {
            InitPll();
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Change the tuner frequency
// - makes only sense if TV tuner is input source
//
bool BtDriver_TuneChannel( ulong freq, bool keepOpen )
{
   // remember frequency for later
   LastFrequency = (uint) freq;

   if (Initialized)
   {
      return Tuner_SetFrequency(TunerType, freq);
   }
   else
   {  // driver not loaded -> freq will be tuned upon acq start
      return TRUE;
   }
}

// ----------------------------------------------------------------------------
// Get the current tuner frequency
//
ulong BtDriver_QueryChannel( void )
{
   return LastFrequency;
}

// ----------------------------------------------------------------------------
// Dummies - not used for Windows
//
void BtDriver_CloseDevice( void )
{
}

bool BtDriver_CheckDevice( void )
{
   return TRUE;
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
static void BtDriver_VbiThread( void )
{
   int row;
   BYTE *pVBI;

   SetAcqPriority(GetCurrentThread());

   for (;;)
   {
      WaitForSingleObject (VBI_Event, INFINITE);
      ResetEvent (VBI_Event);

      if (StopVBI == TRUE)
         return;

      if (Capture_Videotext == TRUE)
      {
         pVBI = (LPBYTE) Vbi_dma.pUserAddr;

         for (row = 0; row < VBI_LINES_PER_FRAME; row++, pVBI += VBI_LINE_SIZE)
         {
            VbiDecodeLine(pVBI, row, pVbiBuf->isEpgScan);
         }
         memset(Vbi_dma.pUserAddr, 0xa5, VBI_DATA_SIZE);
      }
   }
}

// ---------------------------------------------------------------------------
// Start acquisition
// - the driver is automatically loaded and initialized
//
bool BtDriver_StartAcq( void )
{
   DWORD LinkThreadID;

   if (Initialized == FALSE)
   {
      if (BtDriver_Load())
      {
         StopVBI = FALSE;
         Capture_Videotext = TRUE;
         ResetEvent(VBI_Event);
         CloseHandle(CreateThread((LPSECURITY_ATTRIBUTES) NULL, (DWORD) 0, (LPTHREAD_START_ROUTINE) BtDriver_VbiThread, NULL, (DWORD) 0, (LPDWORD) & LinkThreadID));

         Set_Capture(TRUE);

         return TRUE;
      }
   }
   return FALSE;
}

// ---------------------------------------------------------------------------
// Stop acquisition
// - the driver is automatically stopped and removed
//
void BtDriver_StopAcq( void )
{
   if (Initialized)
   {
      Set_Capture(FALSE);

      Capture_Videotext = FALSE;
      StopVBI = TRUE;
      SetEvent (VBI_Event);
      Sleep(20);
      StopVBI = TRUE;
      SetEvent(VBI_Event);

      BtDriver_Unload();
   }
}

// ---------------------------------------------------------------------------
// Initialize the driver module
// - called once at program start
//
bool BtDriver_Init( void )
{
   memset(&vbiBuf, 0, sizeof(vbiBuf));
   pVbiBuf = &vbiBuf;

   return TRUE;
}

// ---------------------------------------------------------------------------
// Clean up the driver module for exit
// - called once at program termination
//
void BtDriver_Exit( void )
{
   if (Initialized)
   {  // acq is still running - should never happen
      BtDriver_StopAcq();
   }
}

