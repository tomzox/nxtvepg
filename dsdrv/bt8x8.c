/*
 *  Win32 VBI capture driver for the Bt8x8 chip family
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
 *    This module is a "user-space driver" for the Brooktree Bt8x8 chips.
 *
 *    The code in this module is heavily based upon the Linux bttv driver
 *    and has been adapted for WinDriver by someone who calls himself
 *    "Espresso".  I stripped it down for VBI decoding only.  1.5 years later
 *    WinDriver was replaced with the free DSdrv BT8x8 driver by E-Nek.
 *
 *
 *  Author:
 *
 *    BT8x8-Parts (from bttv driver for Linux)
 *      Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
 *                             & Marcus Metzler (mocm@thp.uni-koeln.de)
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
 *  $Id: bt8x8.c,v 1.11 2004/03/22 17:35:32 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/vbidecode.h"
#include "epgvbi/zvbidecoder.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"

#include "dsdrv/dsdrvlib.h"
#include "dsdrv/hwmem.h"
#include "dsdrv/tvcard.h"
#include "dsdrv/bt8x8_reg.h"
#include "dsdrv/bt8x8_i2c.h"
#include "dsdrv/bt8x8_typ.h"
#include "dsdrv/bt8x8.h"


// ----------------------------------------------------------------------------
// Declaration of internal variables
//
#define VBI_LINES_PER_FIELD      16
#define VBI_LINES_PER_FRAME     (VBI_LINES_PER_FIELD * 2)
#define VBI_LINE_SIZE            2048
#define VBI_DATA_SIZE           (VBI_LINE_SIZE * VBI_LINES_PER_FRAME)
#define VBI_DMA_PAGE_SIZE        4096
#define VBI_SPL                  2044

#define VBI_FRAME_CAPTURE_COUNT   5
#define VBI_FIELD_CAPTURE_COUNT  (VBI_FRAME_CAPTURE_COUNT * 2)


#define RISC_CODE_LENGTH         4096  // currently apx. 36 DWORDs per field, total 1488
typedef DWORD PHYS;

static HANDLE vbiThreadHandle = NULL;
static BOOL StopVbiThread;

static THwMem  m_RiscDMAMem;
static THwMem  m_VBIDMAMem[VBI_FRAME_CAPTURE_COUNT];
static PHYS    pRiscBasePhysical;
static uint    BytesPerRISCField = 1;
static BOOL    CardConflictDetected;

static DWORD m_I2CRegister;
static ULONG m_I2CSleepCycle;
static BOOL  m_I2CInitialized;

static vbi_raw_decoder zvbi_rd;

// ---------------------------------------------------------------------------
// Bt8x8 I2C bus access
//
static ULONG Bt8x8_I2cGetTickCount( void )
{
   ULONGLONG ticks;
   ULONGLONG frequency;

   QueryPerformanceFrequency((PLARGE_INTEGER)&frequency);
   QueryPerformanceCounter((PLARGE_INTEGER)&ticks);
   ticks = (ticks & 0xFFFFFFFF00000000) / frequency * 10000000 +
           (ticks & 0xFFFFFFFF) * 10000000 / frequency;
   return (ULONG)(ticks / 10000);
}

static void Bt8x8_I2cInitialize( void )
{
   DWORD start;
   DWORD elapsed;
   volatile DWORD i;

   WriteDword(BT848_I2C, 1);
   m_I2CRegister = ReadDword(BT848_I2C);

   m_I2CSleepCycle = 10000L;
   elapsed = 0L;
   // get a stable reading
   while (elapsed < 5)
   {
      m_I2CSleepCycle *= 10;
      start = Bt8x8_I2cGetTickCount();
      for (i = m_I2CSleepCycle; i > 0; i--)
         ;
      elapsed = Bt8x8_I2cGetTickCount() - start;
   }
   // calculate how many cycles a 50kHZ is (half I2C bus cycle)
   m_I2CSleepCycle = m_I2CSleepCycle / elapsed * 1000L / 50000L;
   
   m_I2CInitialized = TRUE;

   dprintf2("BT848-I2cInitialize: elapsed ticks=%ld sleep cycle=%ld\n", elapsed, m_I2CSleepCycle);
}

static void Bt8x8_I2cSleep( void )
{
   volatile DWORD i;

   for (i = m_I2CSleepCycle; i > 0; i--)
      ;
}

static void Bt8x8_SetSDA( bool value )
{
   if (m_I2CInitialized == FALSE)
      Bt8x8_I2cInitialize();

   if (value)
   {
      dprintf0((m_I2CRegister & BT848_I2C_SDA) ? "BT848 SetSDA - d^\n" : "BT848 SetSDA - d/\n");
      m_I2CRegister |= BT848_I2C_SDA;
   }
   else
   {
      dprintf0((m_I2CRegister & BT848_I2C_SDA) ? "BT848 SetSDA - d\\\n" : "BT848 SetSDA - d_\n");
      m_I2CRegister &= ~BT848_I2C_SDA;
   }
   WriteDword(BT848_I2C, m_I2CRegister);
}

static void Bt8x8_SetSCL( bool value )
{
   if (m_I2CInitialized == FALSE)
      Bt8x8_I2cInitialize();

   if (value)
   {
      dprintf0((m_I2CRegister & BT848_I2C_SCL) ? "BT848 SetSCL - c^\n" : "BT848 SetSCL - c/\n");
      m_I2CRegister |= BT848_I2C_SCL;
   }
   else
   {
      dprintf0((m_I2CRegister & BT848_I2C_SCL) ? "BT848 SetSCL - c\\\n" : "BT848 SetSCL - c_\n");
      m_I2CRegister &= ~BT848_I2C_SCL;
   }
   WriteDword(BT848_I2C, m_I2CRegister);
}

static bool Bt8x8_GetSDA( void )
{
   bool state;

   if (m_I2CInitialized == FALSE)
      Bt8x8_I2cInitialize();

   state = ReadDword(BT848_I2C) & BT848_I2C_SDA ? TRUE : FALSE;
   dprintf0(state ? "BT848 GetSDA - d^\n" : "BT848 GetSDA - d_\n");
   return state;
}

static bool Bt8x8_GetSCL( void )
{
   bool state;

   if (m_I2CInitialized == FALSE)
      Bt8x8_I2cInitialize();

   state = ReadDword(BT848_I2C) & BT848_I2C_SCL ? TRUE : FALSE;
   dprintf0(state ? "BT848 GetSCL - c^\n" : "BT848 GetSCL - c_\n");
   return state;
}

// ----------------------------------------------------------------------------
// Free DMA buffers and unlock them for DMA
//
static void Bt8x8_FreeDmaMemory( void )
{
   uint  idx;

   HwMem_FreeContigMemory(&m_RiscDMAMem);

   for (idx=0; idx < VBI_FRAME_CAPTURE_COUNT; idx++)
   {
      HwMem_FreeUserMemory(&m_VBIDMAMem[idx]);
   }
}

// ----------------------------------------------------------------------------
// Allocate buffers for DMA
// - allocate VBI data buffer for each buffered frame: each can hold 2 fields' lines
// - allocate one area is for the RISC code that is executed in the DMA controller
//
static BOOL Bt8x8_AllocDmaMemory( void )
{
   uint  idx;

   memset(&m_RiscDMAMem, 0, sizeof(m_RiscDMAMem));
   memset(m_VBIDMAMem, 0, sizeof(m_VBIDMAMem));

   if (HwMem_AllocContigMemory(&m_RiscDMAMem, RISC_CODE_LENGTH) == FALSE)
   {
      MessageBox(NULL, "Failed to allocate RISC code memory: driver abort", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      Bt8x8_FreeDmaMemory();
      return FALSE;
   }

   for (idx=0; idx < VBI_FRAME_CAPTURE_COUNT; idx++)
   {
      if (HwMem_AllocUserMemory(&m_VBIDMAMem[idx], VBI_LINE_SIZE * VBI_LINES_PER_FIELD * 2) == FALSE)
      {
         MessageBox(NULL, "VBI Memory for DMA not Allocated", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         Bt8x8_FreeDmaMemory();
         return FALSE;
      }
   }

   return TRUE;
}

// ----------------------------------------------------------------------------
// Generate RISC program to control DMA transfer
// - This program is executed by the DMA controller in the Brooktree chip.
// - It controls which of the captured data is written whereto.
// - In our case it's very simple: all VBI lines are written into one
//   consecutive buffer (actually there's a gap of 4 bytes between lines)
//
static bool CreateRiscCode( void )
{
   DWORD * pRiscCode;
   BYTE  * pVbiUser;
   PHYS    pVbiPhysical;
   uint  nField;
   uint  nLine;
   DWORD GotBytesPerLine;

   pRiscCode         = HwMem_GetUserPointer(&m_RiscDMAMem);
   pRiscBasePhysical = HwMem_TranslateToPhysical(&m_RiscDMAMem, pRiscCode, RISC_CODE_LENGTH, NULL);

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
         *(pRiscCode++) = (DWORD) (BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_VRE);
      }
      *(pRiscCode++) = 0;

      *(pRiscCode++) = (DWORD) (BT848_RISC_SYNC | BT848_FIFO_STATUS_FM1);
      *(pRiscCode++) = 0;

      pVbiUser = HwMem_GetUserPointer(&m_VBIDMAMem[nField / 2]);

      if (nField & 1)
         pVbiUser += VBI_LINES_PER_FIELD * VBI_LINE_SIZE;

      for (nLine = 0; nLine < VBI_LINES_PER_FIELD; nLine++)
      {
         pVbiPhysical = HwMem_TranslateToPhysical(&m_VBIDMAMem[nField / 2], pVbiUser, VBI_SPL, &GotBytesPerLine);
         if ((pVbiPhysical == 0) || (VBI_SPL > GotBytesPerLine))
         {
            SHOULD_NOT_BE_REACHED;
            return FALSE;
         }
         *(pRiscCode++) = BT848_RISC_WRITE | BT848_RISC_SOL | BT848_RISC_EOL | VBI_SPL;
         *(pRiscCode++) = pVbiPhysical;
         pVbiUser += VBI_LINE_SIZE;
      }
   }

   BytesPerRISCField = ((long)pRiscCode - (long)HwMem_GetUserPointer(&m_RiscDMAMem)) / VBI_FIELD_CAPTURE_COUNT;

   *(pRiscCode++) = BT848_RISC_JUMP;
   *(pRiscCode++) = pRiscBasePhysical;

   // start address for the DMA RISC code
   WriteDword(BT848_RISC_STRT_ADD, pRiscBasePhysical);

   return TRUE;
}

// ---------------------------------------------------------------------------
// Determine which frame has been completed last
// - returns frame index: 0 to 4 (the VBI buffer contains 5 frames)
//
static int Bt8x8_GetRISCPosAsInt( uint * pErrCode )
{
   PHYS  CurrentRiscPos;
   int   CurrentPos = VBI_FIELD_CAPTURE_COUNT;
   int   CurrentFrame;

   // read the RISC program counter, i.e. pointer into the RISC code
   CurrentRiscPos = ReadDword(BT848_RISC_COUNT);

   if ( (CurrentRiscPos < pRiscBasePhysical) ||
        (CurrentRiscPos > pRiscBasePhysical + RISC_CODE_LENGTH) )
   {
      // illegal position -> check if another app is running
      CurrentRiscPos = ReadDword(BT848_RISC_STRT_ADD);

      if (CurrentRiscPos != pRiscBasePhysical)
      {
         debug2("Bt8x8-GetRISCPosAsInt: RISC start addr changed: %lX != %lX", CurrentRiscPos, pRiscBasePhysical);
         *pErrCode = 2;
      }
      else
         *pErrCode = 1;

      CurrentFrame = 0;
   }
   else
   {
      CurrentPos = (CurrentRiscPos - pRiscBasePhysical) / BytesPerRISCField;

      if (CurrentPos < VBI_FIELD_CAPTURE_COUNT)
      {
         *pErrCode = 0;

         // the current position lies in the field which is currently being filled
         // calculate the index of the previous (i.e. completed) frame
         if (CurrentPos < 2)
            CurrentFrame = ((CurrentPos + VBI_FIELD_CAPTURE_COUNT) - 2) / 2;
         else
            CurrentFrame = (CurrentPos - 2) / 2;
      }
      else
      {  // RISC counter currently behind the last field (not necessarily an error)
         *pErrCode = 1;
         CurrentFrame = 0;
      }
   }

   return CurrentFrame;
}

// ---------------------------------------------------------------------------
// En- or disable VBI capturing
// - when enabling, set up the RISC program and also enable DMA transfer
//
static BOOL Bt8x8_SetCapture(BOOL enable)
{
   // disable capturing while the RISC program is changed to avoid a crash
   MaskDataByte(BT848_CAP_CTL, 0, 0x0f);

   if (enable)
   {
      // set up a RISC program loop that controls DMA from the Bt8x8 to RAM
      if (CreateRiscCode())
      {
         // enable capturing VBI in even and odd field; video is not captured
         MaskDataByte(BT848_CAP_CTL, BT848_CAP_CTL_CAPTURE_VBI_EVEN | BT848_CAP_CTL_CAPTURE_VBI_ODD, 0x0f);

         // enable DMA
         OrDataWord (BT848_GPIO_DMA_CTL, 3);
      }
   }
   else
   {
      // disable DMA
      AndDataWord (BT848_GPIO_DMA_CTL, ~3);

      // clear address for the DMA RISC code
      // (to prevent another app from using it and doing DMA to random memory pages)
      if ( (CardConflictDetected == FALSE) &&
           (ReadDword(BT848_RISC_STRT_ADD) == pRiscBasePhysical) )
      {
         WriteDword(BT848_RISC_STRT_ADD, 0);
      }
   }

   return TRUE;
}

// ---------------------------------------------------------------------------
// Start the PLL (required on some cards only)
//
static void Bt8x8_InitPll( int type )
{
   uint  idx;

   if (type == 0)
   {
      WriteByte (BT848_TGCTRL, BT848_TGCTRL_TGCKI_NOPLL);
      WriteByte (BT848_PLL_XCI, 0x00);
   }
   else
   {
      if (type == 1)
      {
         WriteByte (BT848_PLL_F_LO, 0xf9);
         WriteByte (BT848_PLL_F_HI, 0xdc);
         WriteByte (BT848_PLL_XCI, 0x8E);
      }
      else
      {
         WriteByte (BT848_PLL_F_LO, 0x39);
         WriteByte (BT848_PLL_F_HI, 0xB0);
         WriteByte (BT848_PLL_XCI, 0x89);
      }

      for (idx = 0; idx < 100; idx++)
      {
         if (ReadByte (BT848_DSTATUS) & BT848_DSTATUS_CSEL)
         {
            WriteByte (BT848_DSTATUS, 0x00);
         }
         else
         {
            WriteByte (BT848_TGCTRL, BT848_TGCTRL_TGCKI_PLL);
            break;
         }
         Sleep (10);
      }

      // these settings do not work with my cards
      //WriteByte (BT848_WC_UP, 0xcf);
      //WriteByte (BT848_VTOTAL_LO, 0x00);
      //WriteByte (BT848_VTOTAL_HI, 0x00);
      //WriteByte (BT848_DVSIF, 0x00);
   }
}

// ----------------------------------------------------------------------------
// Save/reload PCI register state into/from RAM array on host
// - save or restore every register which is written to by nxtvepg,
//   except for DMA, interrupt and capture control
//
static void Bt8x8_ManageMyState( void )
{
   ManageByte(BT848_IFORM);
   ManageByte(BT848_FCNTR);
   ManageByte(BT848_PLL_F_LO);
   ManageByte(BT848_PLL_F_HI);
   ManageByte(BT848_PLL_XCI);
   ManageByte(BT848_TGCTRL);
   ManageByte(BT848_TDEC);
   ManageByte(BT848_E_CROP);
   ManageByte(BT848_O_CROP);
   ManageByte(BT848_E_VDELAY_LO);
   ManageByte(BT848_O_VDELAY_LO);
   ManageByte(BT848_E_VACTIVE_LO);
   ManageByte(BT848_O_VACTIVE_LO);
   ManageByte(BT848_E_HDELAY_LO);
   ManageByte(BT848_O_HDELAY_LO);
   ManageByte(BT848_E_HACTIVE_LO);
   ManageByte(BT848_O_HACTIVE_LO);
   ManageByte(BT848_E_HSCALE_HI);
   ManageByte(BT848_O_HSCALE_HI);
   ManageByte(BT848_E_HSCALE_LO);
   ManageByte(BT848_O_HSCALE_LO);
   ManageByte(BT848_BRIGHT);
   ManageByte(BT848_E_CONTROL);
   ManageByte(BT848_O_CONTROL);
   ManageByte(BT848_CONTRAST_LO);
   ManageByte(BT848_SAT_U_LO);
   ManageByte(BT848_SAT_V_LO);
   ManageByte(BT848_HUE);
   ManageByte(BT848_E_SCLOOP);
   ManageByte(BT848_O_SCLOOP);
   ManageByte(BT848_WC_UP);
   ManageByte(BT848_VTOTAL_LO);
   ManageByte(BT848_VTOTAL_HI);
   ManageByte(BT848_DVSIF);
   ManageByte(BT848_OFORM);
   ManageByte(BT848_E_VSCALE_HI);
   ManageByte(BT848_O_VSCALE_HI);
   ManageByte(BT848_E_VSCALE_LO);
   ManageByte(BT848_O_VSCALE_LO);
   ManageByte(BT848_ADC);
   ManageByte(BT848_E_VTC);
   ManageByte(BT848_O_VTC);
   ManageByte(BT848_COLOR_FMT);
   ManageByte(BT848_COLOR_CTL);
   ManageByte(BT848_VBI_PACK_SIZE);
   ManageByte(BT848_VBI_PACK_DEL);
   ManageByte(BT848_ADELAY);
   ManageByte(BT848_BDELAY);
   ManageByte(BT848_GPIO_REG_INP);
   ManageWord(BT848_GPIO_DMA_CTL);
   ManageDword(BT848_GPIO_OUT_EN);
   ManageDword(BT848_GPIO_OUT_EN_HIBYTE);
   ManageDword(BT848_GPIO_DATA);
}

// ----------------------------------------------------------------------------
// Reset the Bt8x8 and program all relevant registers with constants
//
static bool Bt8x8_ResetHardware( void )
{
   // software reset, sets all registers to reset default values
   WriteByte (BT848_SRESET, 0);
   Sleep(50);

   WriteByte (BT848_TDEC, 0x00);
   WriteByte (BT848_COLOR_CTL, BT848_COLOR_CTL_GAMMA);
   WriteByte (BT848_ADELAY, 0x7f);
   WriteByte (BT848_BDELAY, 0x72);
   // disable capturing
   WriteByte (BT848_CAP_CTL, 0x00);
   // max length of a VBI line
   WriteByte (BT848_VBI_PACK_SIZE, 0xff);
   WriteByte (BT848_VBI_PACK_DEL, 1);

   // vertical delay for image data in the even and odd fields
   // IMPORTANT!  This defines the end of VBI capturing, i.e. the number of max. captured lines!
   WriteByte (BT848_E_VDELAY_LO, 0x20);
   WriteByte (BT848_O_VDELAY_LO, 0x20);

   WriteWord (BT848_GPIO_DMA_CTL, BT848_GPIO_DMA_CTL_PKTP_32 |
                                  BT848_GPIO_DMA_CTL_PLTP1_16 |
                                  BT848_GPIO_DMA_CTL_PLTP23_16 |
                                  BT848_GPIO_DMA_CTL_GPINTC |
                                  BT848_GPIO_DMA_CTL_GPINTI);
   WriteByte (BT848_GPIO_REG_INP, 0x00);
   // input format (PAL, NTSC etc.) and input source
   WriteByte (BT848_IFORM, BT848_IFORM_MUX1 | BT848_IFORM_XTBOTH | BT848_IFORM_PAL_BDGHI);

   WriteByte (BT848_CONTRAST_LO, 0xd8);
   WriteByte (BT848_BRIGHT, 0x10);
   WriteByte (BT848_E_VSCALE_HI, 0x20);
   WriteByte (BT848_O_VSCALE_HI, 0x20);

   WriteByte (BT848_ADC, BT848_ADC_RESERVED | BT848_ADC_CRUSH);
   WriteByte (BT848_E_CONTROL, BT848_CONTROL_LDEC);
   WriteByte (BT848_O_CONTROL, BT848_CONTROL_LDEC);

   WriteByte (BT848_E_SCLOOP, 0x00);
   WriteByte (BT848_O_SCLOOP, 0x00);

   // interrupt mask; reset the status before enabling the interrupts
   WriteDword (BT848_INT_STAT, (DWORD) 0x0fffffffUL);
   WriteDword (BT848_INT_MASK, (1 << 23) | BT848_INT_RISCI);

   return (TRUE);
}

// ---------------------------------------------------------------------------
// VBI Driver Thread
//
static DWORD WINAPI Bt8x8_VbiThread( LPVOID dummy )
{
   int  OldFrame;
   int  CurFrame;
   uint riscError;
   bool acceptFrame;
   BYTE *pVBI;

   //SetAcqPriority(GetCurrentThread(), btCfg.threadPrio);

   // pass parameters to VBI slicer
   VbiDecodeSetSamplingRate(0, 0);

   memset(&zvbi_rd, 0, sizeof(zvbi_rd));
   zvbi_rd.sampling_rate    = 35468950L;
   zvbi_rd.bytes_per_line   = VBI_LINE_SIZE;
   zvbi_rd.start[0]         = 7;
   zvbi_rd.count[0]         = VBI_LINES_PER_FIELD;
   zvbi_rd.start[1]         = 319;
   zvbi_rd.count[1]         = VBI_LINES_PER_FIELD;
   zvbi_rd.interlaced       = FALSE;
   zvbi_rd.synchronous      = TRUE;
   zvbi_rd.sampling_format  = VBI_PIXFMT_YUV420;
   zvbi_rd.scanning         = 625;
   vbi_raw_decoder_add_services(&zvbi_rd, VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS, 1);

   Bt8x8_SetCapture(TRUE);

   riscError = 0;
   do
   {
      Sleep(1);
      OldFrame = Bt8x8_GetRISCPosAsInt(&riscError);
   }
   while ((riscError != 0) && (StopVbiThread == FALSE));

   for (;;)
   {
      if (StopVbiThread == TRUE)
         break;

      if (pVbiBuf != NULL)
      {
         CurFrame = Bt8x8_GetRISCPosAsInt(&riscError);
         if (riscError != 0)
         {  // invalid frame reading
            if (riscError == 2)
            {  // another TV app was started -> abort acq immediately
               CardConflictDetected = TRUE;
               pVbiBuf->hasFailed = TRUE;
               break;
            }
            else
               Sleep(1);
         }
         else if (CurFrame != OldFrame)
         {
            do
            {
               OldFrame = (OldFrame + 1) % VBI_FRAME_CAPTURE_COUNT;
               pVBI = HwMem_GetUserPointer(&m_VBIDMAMem[OldFrame]);

               // notify teletext decoder about start of new frame; since we don't have a
               // frame counter (no frame interrupt) we always pass 0 as frame sequence number
               if (pVbiBuf->slicerType != VBI_SLICER_ZVBI)
               {
                  acceptFrame = VbiDecodeStartNewFrame(0);
                  if (acceptFrame)
                  {
                     int  row;

                     for (row = 0; row < VBI_LINES_PER_FRAME; row++, pVBI += VBI_LINE_SIZE)
                     {
                        VbiDecodeLine(pVBI, row, TRUE);
                     }
                  }
               }
               else
               {
                  acceptFrame = ZvbiSliceAndProcess(&zvbi_rd, pVBI, 0);
               }

               if (acceptFrame == FALSE)
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
   Bt8x8_SetCapture(FALSE);

   ZvbiSliceAndProcess(NULL, NULL, 0);
   vbi_raw_decoder_destroy(&zvbi_rd);

   return 0;  // dummy
}

// ---------------------------------------------------------------------------
//                    I N T E R F A C E   F U N C T I O N S
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Check if the current source has a video signal
//
static bool Bt8x8_IsVideoPresent( void )
{
   return ((ReadByte (BT848_DSTATUS) & (BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC)) ==
                                       (BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC));
}

// ---------------------------------------------------------------------------
// Start acquisition
//
static bool Bt8x8_StartAcqThread( void )
{
   DWORD LinkThreadID;
   bool result;

   StopVbiThread = FALSE;
   vbiThreadHandle = CreateThread(NULL, 0, Bt8x8_VbiThread, NULL, 0, &LinkThreadID);

   result = (vbiThreadHandle != NULL);
   ifdebug1(!result, "Bt8x8-StartAcqThread: failed to create VBI thread: %ld", GetLastError());

   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition
//
static void Bt8x8_StopAcqThread( void )
{
   if (vbiThreadHandle != NULL)
   {
      StopVbiThread = TRUE;
      WaitForSingleObject(vbiThreadHandle, 200);
      CloseHandle(vbiThreadHandle);
      vbiThreadHandle = NULL;
   }

   // the VBI thread should have stopped capturing
   // just to be safe, do it again here
   Bt8x8_SetCapture(FALSE);
}

// ---------------------------------------------------------------------------
// Set parameters
//
static bool Bt8x8_Configure( uint threadPrio, uint pllType )
{
   Bt8x8_InitPll(pllType);

   if (vbiThreadHandle != NULL)
   {
      if (SetThreadPriority(vbiThreadHandle, threadPrio) == 0)
         debug2("Bt8x8-Configure: SetThreadPriority(%d) returned %ld", threadPrio, GetLastError());
   }

   return TRUE;
}

// ---------------------------------------------------------------------------
// Free I/O resources and close the driver device
//
static void Bt8x8_Close( TVCARD * pTvCard )
{
   // skip reset if conflict was detected earlier to avoid crashing the other app
   if (CardConflictDetected == FALSE)
   {
      WriteByte(BT848_SRESET, 0);

      // reset PCI registers to original state
      HwPci_RestoreState();
      Bt8x8_ManageMyState();
   }

   Bt8x8_FreeDmaMemory();
}

// ---------------------------------------------------------------------------
// Allocate resources and initialize PCI registers
//
static bool Bt8x8_Open( TVCARD * pTvCard, bool wdmStop )
{
   WORD DmaCtl;
   BYTE CapCtl;
   bool result;

   m_I2CInitialized = FALSE;
   CardConflictDetected = FALSE;

   // check if capturing is already enabled
   DmaCtl = ReadWord(BT848_GPIO_DMA_CTL);
   CapCtl = ReadByte(BT848_CAP_CTL);
   if ((DmaCtl & 3) && (CapCtl & 0x0f))
   {
      MessageBox(NULL, "Capturing is already enabled in the TV card!\n"
                       "Probably another video application is running,\n"
                       "however nxtvepg requires exclusive access.\n"
                       "Aborting data acquisition.",
                       "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      CardConflictDetected = TRUE;
      result = FALSE;
   }
   else
   {
      result = Bt8x8_AllocDmaMemory();
      if (result)
      {
         HwPci_InitStateBuf();
         Bt8x8_ManageMyState();

         // initialize all PCI registers
         Bt8x8_ResetHardware();
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Fill structure with interface functions
//
static const I2C_LINE_BUS Bt8x8_I2cBus =
{
   Bt8x8_I2cSleep,
   Bt8x8_SetSDA,
   Bt8x8_SetSCL,
   Bt8x8_GetSDA,
   Bt8x8_GetSCL,
};

static const TVCARD_CTL Bt8x8_CardCtl =
{
   Bt8x8_IsVideoPresent,
   Bt8x8_StartAcqThread,
   Bt8x8_StopAcqThread,
   Bt8x8_Configure,
   Bt8x8_Close,
   Bt8x8_Open,
};


void Bt8x8_GetInterface( TVCARD * pTvCard )
{
   if (pTvCard != NULL)
   {
      memset(pTvCard, 0, sizeof(*pTvCard));

      pTvCard->ctl        = &Bt8x8_CardCtl;
      pTvCard->i2cLineBus = &Bt8x8_I2cBus;

      Bt8x8I2c_GetInterface(pTvCard);
      Bt8x8Typ_GetInterface(pTvCard);
   }
   else
      fatal0("Bt8x8-GetInterface: NULL ptr param");
}

