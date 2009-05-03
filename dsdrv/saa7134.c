/*
 *  Win32 VBI capture driver for the SAA7134 chip
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
 *    This module is a "user-space driver" for the Philips SAA7134 chip.
 *    It's directly derived from DScaler and Linux saa7134 drivers.
 *
 *
 *  Authors:
 *      Copyright (c) 2002 Atsushi Nakagawa.  All rights reserved.
 *
 *  DScaler #Id: SAA7134Card.cpp,v 1.46 2005/03/24 17:57:58 adcockj Exp #
 *  DScaler #Id: SAA7134Source.cpp,v 1.100 2006/09/24 14:14:44 robmuller Exp #
 *  DScaler #Id: SAA7134Provider.cpp,v 1.10 2002/12/24 08:22:14 atnak Exp #
 *
 *  $Id: saa7134.c,v 1.26 2009/04/19 18:16:25 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF
#define DISABLE_PCI_REGISTER_DUMP
#define ENABLE_CAPTURING

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
#include "dsdrv/tvcard.h"
#include "dsdrv/hwmem.h"           // XXX fixme: should be in dsdrvlib.h
#include "dsdrv/wintuner.h"
#include "dsdrv/saa7134_reg.h"
#include "dsdrv/saa7134_i2c.h"
#include "dsdrv/saa7134_typ.h"
#include "dsdrv/saa7134.h"


// ----------------------------------------------------------------------------
// Declaration of internal variables
//
static HANDLE vbiThreadHandle = NULL;
static BOOL StopVbiThread;

#define VBI_SCALE               0x200
#define VBI_VSTART             (7 - 3)
#define VBI_VSTOP             (22 - 3)
#define VBI_LINE_SIZE            2048
#define VBI_LINES_PER_FIELD        16
#define VBI_FRAME_CAPTURE_COUNT  (1+1)  // only 2 "tasks" supported by hardware
#define VBI_FIELD_CAPTURE_COUNT  (VBI_FRAME_CAPTURE_COUNT * 2)

#define VIDEO_VSTART               24
#define VIDEO_VSTOP                25
#define VIDEO_LINES_PER_FIELD       2
#define VIDEO_LINE_SIZE          2048
#define VIDEO_BASE_OFFSET        (VBI_LINE_SIZE * VBI_LINES_PER_FIELD * 2)

#define DMA_PAGE_SIZE            4096
#define VBI_PAGES_PER_TASK       ((VBI_LINE_SIZE * VBI_LINES_PER_FIELD * 2) / DMA_PAGE_SIZE)
#define VIDEO_PAGES_PER_TASK     ((VIDEO_LINE_SIZE * VIDEO_LINES_PER_FIELD * 2) / DMA_PAGE_SIZE)
#define DMA_PAGES_PER_TASK       (VBI_PAGES_PER_TASK + VIDEO_PAGES_PER_TASK)

#define VBI_FIELD_MARKER1        0xeffe0aef
#define VBI_FIELD_MARKER2        0xfe0beffe

static BYTE    m_PreparedRegions = 0x00;
static BOOL    CardConflictDetected;

static THwMem  PageTableDmaMem[VBI_FRAME_CAPTURE_COUNT];
static THwMem  VbiDmaMem[VBI_FRAME_CAPTURE_COUNT];


#define DMA_CHANNEL_VIDEO_A  0
#define DMA_CHANNEL_VIDEO_B  1
#define DMA_CHANNEL_VBI_A    2
#define DMA_CHANNEL_VBI_B    3
#define DMA_CHANNEL_COUNT    4

// buffer to keep DMA control register values to check for external modifications
static DWORD DmaControlBuf[DMA_CHANNEL_COUNT];

// Bit masks used in TFieldID
enum
{
   FIELDID_FRAMESHIFT      = 1,
   FIELDID_FIELDMASK       = 0x01,
   FIELDID_SECONDFIELD     = FIELDID_FIELDMASK
};
typedef char TFieldID;

static vbi_raw_decoder zvbi_rd[2];


// ----------------------------------------------------------------------------
// Debug helper function: dump content of all PCI registers
//
#ifndef DISABLE_PCI_REGISTER_DUMP
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
static void SAA7134_DumpRegisters( void )
{
   char    HexString[256];
   DWORD   Data;
   uint    idx;
   uint    off;
   int     fd;

   fd = open("pci.log", O_WRONLY|O_CREAT|O_APPEND, 0666);
   if (fd >= 0)
   {
      for (idx = 0x000; idx < 0x400; idx += 16)
      {
         off = sprintf(HexString, "%03X:", idx);

         Data = ReadDword(idx);
         off += sprintf(HexString + off, " %02x%02x %02x%02x",
             (int)(Data & 0xFF), (int)((Data >> 8) & 0xFF), (int)((Data >> 16) & 0xFF), (int)((Data >> 24) & 0xFF));

         Data = ReadDword(idx + 4);
         off += sprintf(HexString + off, " %02x%02x %02x%02x",
             (int)(Data & 0xFF), (int)((Data >> 8) & 0xFF), (int)((Data >> 16) & 0xFF), (int)((Data >> 24) & 0xFF));

         Data = ReadDword(idx + 8);
         off += sprintf(HexString + off, "|%02x%02x %02x%02x",
             (int)(Data & 0xFF), (int)((Data >> 8) & 0xFF), (int)((Data >> 16) & 0xFF), (int)((Data >> 24) & 0xFF));

         Data = ReadDword(idx + 12);
         off += sprintf(HexString + off, " %02x%02x %02x%02x\n",
             (int)(Data & 0xFF), (int)((Data >> 8) & 0xFF), (int)((Data >> 16) & 0xFF), (int)((Data >> 24) & 0xFF));

         write(fd, HexString, strlen(HexString));
      }
      close(fd);
   }
}
#endif

// ----------------------------------------------------------------------------
// Save/reload card state into/from file
// - save or restore everything that might be used by the "real" drivers
//
static void SAA7134_ManageMyState( TVCARD * pTvCard )
{
    int i;

    ManageByte(SAA7134_INCR_DELAY);
    ManageByte(SAA7134_ANALOG_IN_CTRL1);
    ManageByte(SAA7134_ANALOG_IN_CTRL2);
    ManageByte(SAA7134_ANALOG_IN_CTRL3);
    ManageByte(SAA7134_ANALOG_IN_CTRL4);

    ManageByte(SAA7134_HSYNC_START);
    ManageByte(SAA7134_HSYNC_STOP);
    ManageByte(SAA7134_SYNC_CTRL);
    ManageByte(SAA7134_LUMA_CTRL);
    ManageByte(SAA7134_DEC_LUMA_BRIGHT);
    ManageByte(SAA7134_DEC_LUMA_CONTRAST);
    ManageByte(SAA7134_DEC_CHROMA_SATURATION);
    ManageByte(SAA7134_DEC_CHROMA_HUE);
    ManageByte(SAA7134_CHROMA_CTRL1);
    ManageByte(SAA7134_CHROMA_GAIN_CTRL);
    ManageByte(SAA7134_CHROMA_CTRL2);
    ManageByte(SAA7134_MODE_DELAY_CTRL);
    ManageByte(SAA7134_ANALOG_ADC);
    ManageByte(SAA7134_VGATE_START);
    ManageByte(SAA7134_VGATE_STOP);
    ManageByte(SAA7134_MISC_VGATE_MSB);
    ManageByte(SAA7134_RAW_DATA_GAIN);
    ManageByte(SAA7134_RAW_DATA_OFFSET);
    ManageByte(SAA7134_STATUS_VIDEO);
    ManageByte(SAA7134_STATUS_VIDEO_HIBYTE);

    for (i = 0; i < 16; i++)
    {
        ManageByte(SAA7134_GREEN_PATH(i));
        ManageByte(SAA7134_BLUE_PATH(i));
        ManageByte(SAA7134_RED_PATH(i));
    }

    ManageByte(SAA7134_START_GREEN);
    ManageByte(SAA7134_START_BLUE);
    ManageByte(SAA7134_START_RED);

    for (i = SAA7134_TASK_A_MASK; i < 0x077; i++)
    {
        ManageByte(i);
    }

    for (i = SAA7134_TASK_B_MASK; i < 0x0B7; i++)
    {
        ManageByte(i);
    }

    ManageByte(SAA7134_OFMT_VIDEO_A);
    ManageByte(SAA7134_OFMT_DATA_A);
    ManageByte(SAA7134_OFMT_VIDEO_B);
    ManageByte(SAA7134_OFMT_DATA_B);
    ManageByte(SAA7134_ALPHA_NOCLIP);
    ManageByte(SAA7134_ALPHA_CLIP);
    ManageByte(SAA7134_UV_PIXEL);
    ManageByte(SAA7134_CLIP_RED);
    ManageByte(SAA7134_CLIP_GREEN);
    ManageByte(SAA7134_CLIP_BLUE);

    #if 0  // We don't really need to save these
    for (i = 0; i < 16; i++)
    {
        ManageByte(SAA7134_CLIP_H_ACTIVE(i));
        ManageByte(SAA7134_CLIP_H_NOIDEA(i));
        ManageByte(SAA7134_CLIP_H_POS(i));
        ManageByte(SAA7134_CLIP_H_POS_HIBYTE(i));
        ManageByte(SAA7134_CLIP_V_ACTIVE(i));
        ManageByte(SAA7134_CLIP_V_NOIDEA(i));
        ManageByte(SAA7134_CLIP_V_POS(i));
        ManageByte(SAA7134_CLIP_V_POS_HIBYTE(i));
    }
    #endif

    ManageByte(SAA7134_DCXO_IDENT_CTRL);
    ManageByte(SAA7134_DEMODULATOR);
    ManageByte(SAA7134_AGC_GAIN_SELECT);
    ManageDword(SAA7134_CARRIER1_FREQ);
    ManageDword(SAA7134_CARRIER2_FREQ);
    ManageDword(SAA7134_NUM_SAMPLES);
    ManageByte(SAA7134_AUDIO_FORMAT_CTRL);
    ManageByte(SAA7134_MONITOR_SELECT);
    ManageByte(SAA7134_FM_DEEMPHASIS);
    ManageByte(SAA7134_FM_DEMATRIX);
    ManageByte(SAA7134_CHANNEL1_LEVEL);
    ManageByte(SAA7134_CHANNEL2_LEVEL);
    ManageByte(SAA7134_NICAM_CONFIG);
    ManageByte(SAA7134_NICAM_LEVEL_ADJUST);
    ManageByte(SAA7134_STEREO_DAC_OUTPUT_SELECT);
    ManageByte(SAA7134_I2S_OUTPUT_FORMAT);
    ManageByte(SAA7134_I2S_OUTPUT_SELECT);
    ManageByte(SAA7134_I2S_OUTPUT_LEVEL);
    ManageByte(SAA7134_DSP_OUTPUT_SELECT);

    // This stops unmuting on exit
    // ManageByte(SAA7134_AUDIO_MUTE_CTRL);

    ManageByte(SAA7134_SIF_SAMPLE_FREQ);

    // Managing this causes problems for cards that
    // use audio line for muting
    //ManageByte(SAA7134_ANALOG_IO_SELECT);

    ManageDword(SAA7134_AUDIO_CLOCK);
    ManageByte(SAA7134_AUDIO_PLL_CTRL);
    ManageDword(SAA7134_AUDIO_CLOCKS_PER_FIELD);

    ManageByte(SAA7134_VIDEO_PORT_CTRL0);
    ManageByte(SAA7134_VIDEO_PORT_CTRL1);
    ManageByte(SAA7134_VIDEO_PORT_CTRL2);
    ManageByte(SAA7134_VIDEO_PORT_CTRL3);
    ManageByte(SAA7134_VIDEO_PORT_CTRL4);
    ManageByte(SAA7134_VIDEO_PORT_CTRL5);
    ManageByte(SAA7134_VIDEO_PORT_CTRL6);
    ManageByte(SAA7134_VIDEO_PORT_CTRL7);
    ManageByte(SAA7134_VIDEO_PORT_CTRL8);

    ManageByte(SAA7134_TS_PARALLEL);
    ManageByte(SAA7134_TS_PARALLEL_SERIAL);
    ManageByte(SAA7134_TS_SERIAL0);
    ManageByte(SAA7134_TS_SERIAL1);
    ManageByte(SAA7134_TS_DMA0);
    ManageByte(SAA7134_TS_DMA1);
    ManageByte(SAA7134_TS_DMA2);

    ManageByte(SAA7134_I2S_AUDIO_OUTPUT);
    ManageByte(SAA7134_SPECIAL_MODE);

#if 0
    if ( (pTvCard->params.DeviceId == 0x7133) ||
         (pTvCard->params.DeviceId == 0x7135) )
    {
        ManageDword(SAA7133_ANALOG_IO_SELECT);
    }
#endif

    // do these ones last
    #if 0  // It is probably safer if we leave DMA and IRQ stuff zeroed when we're done.
    ManageWord(SAA7134_SOURCE_TIMING);
    //ManageByte(SAA7134_REGION_ENABLE);  // NO!

    for (i = 0; i < 7; i++)
    {
        ManageDword(SAA7134_RS_BA1(i));
        ManageDword(SAA7134_RS_BA2(i));
        ManageDword(SAA7134_RS_PITCH(i));
        ManageDword(SAA7134_RS_CONTROL(i));
    }

    ManageDword(SAA7134_FIFO_SIZE);
    ManageDword(SAA7134_THRESHOULD);

    // NOTE: the following are restored by DScaler, but NOT BY ME
    // since we don't know if the other app is still alive - might CRASH the system!

    //ManageDword(SAA7134_MAIN_CTRL);

    //ManageDword(SAA7134_IRQ1);
    //ManageDword(SAA7134_IRQ2);
    #endif
}

// ----------------------------------------------------------------------------
// Free DMA buffers and unlock them for DMA
//
static void SAA7134_FreeDmaMemory( void )
{
   uint idx;

   for (idx = 0; idx < VBI_FRAME_CAPTURE_COUNT; idx++)
   {
      HwMem_FreeContigMemory(&PageTableDmaMem[idx]);
      HwMem_FreeUserMemory(&VbiDmaMem[idx]);
   }
}

// ----------------------------------------------------------------------------
// Allocate buffers for DMA
// - allocate 2 VBI data buffers: each can hold one frame's lines;
//   also holds one dummy video line per field at the end: without capturing video,
//   tasks are toggled after every field, i.e. we'd get only 1 field per task so
//   we could buffer only two tasks instead of 4 otherwise
// - allocate two segments which are used as "page tables", i.e. they hold pointers
//   to each "page" of one associated VBI data buffer; this is required because on
//   the physical layer the VBI buffers may be non-continguous (the page table
//   buffers are allocated by a different function which makes sure the buffer
//   is not segmented)
// - since the page table is limited to 1024 pointers, the DMA channel address
//   space is limited to 4MB
//
static bool SAA7134_AllocDmaMemory( void )
{
   BYTE * pUser;
   uint   idx;

   memset(PageTableDmaMem, 0, sizeof(PageTableDmaMem));
   memset(VbiDmaMem, 0, sizeof(VbiDmaMem));

   for (idx = 0; idx < VBI_FRAME_CAPTURE_COUNT; idx++)
   {
      // allocate one page for a table with DMA_PAGES_PER_TASK pointers
      if (HwMem_AllocContigMemory(&PageTableDmaMem[idx], DMA_PAGE_SIZE) == FALSE)
      {
         MessageBox(NULL, "Failed to allocate DMA page table memory: driver abort", "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         SAA7134_FreeDmaMemory();
         return FALSE;
      }
      pUser = HwMem_GetUserPointer(&PageTableDmaMem[idx]);
      memset(pUser, 0, 4096);

      // allocate space for VBI buffer
      if (HwMem_AllocUserMemory(&VbiDmaMem[idx], DMA_PAGES_PER_TASK * DMA_PAGE_SIZE) == FALSE)
      {
         MessageBox(NULL, "Failed to allocate VBI Memory for DMA: driver abort", "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         SAA7134_FreeDmaMemory();
         return FALSE;
      }
   }

   return TRUE;
}

// ----------------------------------------------------------------------------
// Check if the page table setting was modified by a different app
// - this is meant as heuristic check to detect hardware access conflicts
//
static bool CheckDmaConflict( void )
{
   DWORD DmaCtl;
   uint  idx;

   for (idx = 0; idx < DMA_CHANNEL_COUNT; idx++)
   {
      DmaCtl = ReadDword(SAA7134_RS_CONTROL(idx));
      if (DmaCtl != DmaControlBuf[idx])
      {
         debug3("CheckDmaConflict: value of RS-CONTROL channel %d changed from 0x%lX to 0x%lX", idx, DmaControlBuf[idx], DmaCtl);
         CardConflictDetected = TRUE;
         break;
      }
   }
   return CardConflictDetected;
}

// ----------------------------------------------------------------------------
// Pass pointer to the page table to the card
// - setting up both VBI and dummy video
//
static void SetPageTable( uint frameIdx )
{
   DWORD * pVBIPageTableLinear;
   DWORD   pVBIPageTablePhysical;
   DWORD   Control;
   uint    vbiDmaChanIdx;
   uint    videoDmaChanIdx;

   vbiDmaChanIdx   = (frameIdx == 0) ? DMA_CHANNEL_VBI_A : DMA_CHANNEL_VBI_B;
   videoDmaChanIdx = (frameIdx == 0) ? DMA_CHANNEL_VIDEO_A : DMA_CHANNEL_VIDEO_B;

   pVBIPageTableLinear   = HwMem_GetUserPointer(&PageTableDmaMem[frameIdx]);
   pVBIPageTablePhysical = HwMem_TranslateToPhysical(&PageTableDmaMem[frameIdx], pVBIPageTableLinear, 4096, NULL);
   
   // ME must be enabled for page tables to work
   Control = SAA7134_RS_CONTROL_BURST_MAX |
             SAA7134_RS_CONTROL_ME |
             (pVBIPageTablePhysical / DMA_PAGE_SIZE);

   WriteDword(SAA7134_RS_CONTROL(vbiDmaChanIdx), Control);

   // Number bytes to offset into every page per field start
   // (note: which field is written first is determined by the task condition bits)

   // odd field (top)
   WriteDword(SAA7134_RS_BA1(vbiDmaChanIdx), 0);
   // even field
   WriteDword(SAA7134_RS_BA2(vbiDmaChanIdx), VBI_LINES_PER_FIELD * VBI_LINE_SIZE);

   // Number of bytes to spend per line
   WriteDword(SAA7134_RS_PITCH(vbiDmaChanIdx), VBI_LINE_SIZE);

   // settings for the dummy video line(s)
   // - video data is placed into the VBI buffer, behind both VBI fields (interlaced if more than 1 line)
   // - using the same page table as VBI
   WriteDword(SAA7134_RS_CONTROL(videoDmaChanIdx), Control);
   WriteDword(SAA7134_RS_BA1(videoDmaChanIdx), VIDEO_BASE_OFFSET + 0);
   WriteDword(SAA7134_RS_BA2(videoDmaChanIdx), VIDEO_BASE_OFFSET + VIDEO_LINE_SIZE);
   WriteDword(SAA7134_RS_PITCH(videoDmaChanIdx), VIDEO_LINE_SIZE * 2);

   // initialize buffer for conflict detection
   for (vbiDmaChanIdx = 0; vbiDmaChanIdx < DMA_CHANNEL_COUNT; vbiDmaChanIdx++)
   {
      DmaControlBuf[vbiDmaChanIdx] = ReadDword(SAA7134_RS_CONTROL(vbiDmaChanIdx));
   }
}

// ----------------------------------------------------------------------------
// Create a "page table" for the given frame's VBI data buffer
// - more comments see the allocate function
//
static bool CreatePageTable( uint frameIdx )
{
   const DWORD PAGE_MASK = (DMA_PAGE_SIZE - 1);
   DWORD * pPageTable;
   DWORD   pPhysical;
   BYTE  * pUser;
   DWORD   GotBytes;
   uint    pageIdx;

   pPageTable = HwMem_GetUserPointer(&PageTableDmaMem[frameIdx]);
   pUser      = HwMem_GetUserPointer(&VbiDmaMem[frameIdx]);

   // This routine should get #lines pages in their page boundries
   for (pageIdx = 0; pageIdx < DMA_PAGES_PER_TASK; pageIdx++)
   {
      pPhysical = HwMem_TranslateToPhysical(&VbiDmaMem[frameIdx], pUser, DMA_PAGE_SIZE, &GotBytes);

      if ((pPhysical == 0) || ((pPhysical & PAGE_MASK) != 0) || (GotBytes < DMA_PAGE_SIZE))
      {
         SHOULD_NOT_BE_REACHED;
         break;
      }

      *(pPageTable++) = pPhysical;
      pUser += DMA_PAGE_SIZE;
   }

   return (pageIdx >= DMA_PAGES_PER_TASK);
}

// ---------------------------------------------------------------------------
// Configure the card for DMA
//
static void SetupDMAMemory( void )
{
   DWORD Control;
   uint  frameIdx;

   // Turn off all DMA
   m_PreparedRegions = 0;
   WriteByte(SAA7134_REGION_ENABLE, 0);
   WriteDword(SAA7134_IRQ1, 0UL);
   MaskDataDword( SAA7134_MAIN_CTRL, 0UL, SAA7134_MAIN_CTRL_TE0 | SAA7134_MAIN_CTRL_TE1 |
                                          SAA7134_MAIN_CTRL_TE2 | SAA7134_MAIN_CTRL_TE3 );

   // generate the page tables and pass the pointers to the card
   for (frameIdx = 0; frameIdx < VBI_FRAME_CAPTURE_COUNT; frameIdx++)
   {
      if (CreatePageTable(frameIdx))
      {
         SetPageTable(frameIdx);
      }
   }

   // Enable DMA channels in both tasks
   Control = SAA7134_MAIN_CTRL_TE0 | SAA7134_MAIN_CTRL_TE1 |
             SAA7134_MAIN_CTRL_TE2 | SAA7134_MAIN_CTRL_TE3;
   MaskDataDword(SAA7134_MAIN_CTRL, Control, Control);

   // do not enable regions yet - there's a separate "start capture" function
   m_PreparedRegions = SAA7134_REGION_ENABLE_VID_ENA | SAA7134_REGION_ENABLE_VID_ENB |
                       SAA7134_REGION_ENABLE_VBI_ENA | SAA7134_REGION_ENABLE_VBI_ENB;

   // Don't turn on interrupts because we don't have an ISR!
   //IRQs = SAA7134_IRQ1_INTE_RA0_4 | SAA7134_IRQ1_INTE_RA0_5 |
   //       SAA7134_IRQ1_INTE_RA0_6 | SAA7134_IRQ1_INTE_RA0_7;
   // MaskDataDword(SAA7134_IRQ1, IRQs, IRQs);
}

// ---------------------------------------------------------------------------
// Set slicer parameters
//
static void SetTaskVbiGeometry( uint TaskMask )
{
   WriteWord(SAA7134_VBI_H_START(TaskMask), 0);  // PAL & SECAM only
   WriteWord(SAA7134_VBI_H_STOP(TaskMask),  719);
   WriteWord(SAA7134_VBI_V_START(TaskMask), VBI_VSTART);
   WriteWord(SAA7134_VBI_V_STOP(TaskMask),  VBI_VSTOP);

   WriteWord(SAA7134_VBI_H_SCALE_INC(TaskMask), VBI_SCALE);

   WriteWord(SAA7134_VBI_V_LEN(TaskMask), VBI_LINES_PER_FIELD);
   WriteWord(SAA7134_VBI_H_LEN(TaskMask), VBI_LINE_SIZE);

   // settings for video
   // we capture 1 line of video, else tasks are toggling after every field
   WriteWord(SAA7134_VIDEO_H_START(TaskMask),  0);
   WriteWord(SAA7134_VIDEO_H_STOP(TaskMask), 719);
   WriteWord(SAA7134_VIDEO_V_START(TaskMask), VIDEO_VSTART);
   WriteWord(SAA7134_VIDEO_V_STOP(TaskMask),  VIDEO_VSTOP);
   WriteWord(SAA7134_VIDEO_PIXELS(TaskMask), 384);
   WriteWord(SAA7134_VIDEO_LINES(TaskMask),   VIDEO_LINES_PER_FIELD);
   WriteWord(SAA7134_H_SCALE_INC(TaskMask), 0x0780);
   WriteByte(SAA7134_V_PHASE_OFFSET0(TaskMask), 0);
   WriteByte(SAA7134_V_PHASE_OFFSET1(TaskMask), 0);
   WriteByte(SAA7134_V_PHASE_OFFSET2(TaskMask), 0);
   WriteByte(SAA7134_V_PHASE_OFFSET3(TaskMask), 0);
}

// ---------------------------------------------------------------------------
// Enable capturing
//
static void StartCapture( void )
{
   BYTE Region;

   Region = SAA7134_REGION_ENABLE_VID_ENA | SAA7134_REGION_ENABLE_VID_ENB |
            SAA7134_REGION_ENABLE_VBI_ENA | SAA7134_REGION_ENABLE_VBI_ENB;

#ifdef ENABLE_CAPTURING
   MaskDataByte(SAA7134_REGION_ENABLE, Region, m_PreparedRegions);
#endif
}

// ---------------------------------------------------------------------------
// Disable capturing
//
static void StopCapture( void )
{
   WriteByte(SAA7134_REGION_ENABLE, 0x00);
}

// ---------------------------------------------------------------------------
// Typical settings for video decoder (according to saa7134 manual v0.1)
//
static void SetTypicalSettings( void )
{
   WriteByte(SAA7134_INCR_DELAY,               0x08);
   WriteByte(SAA7134_ANALOG_IN_CTRL1,          0xC0);
   WriteByte(SAA7134_ANALOG_IN_CTRL2,          0x00);
   WriteByte(SAA7134_ANALOG_IN_CTRL3,          0x90);
   WriteByte(SAA7134_ANALOG_IN_CTRL4,          0x90);
   WriteByte(SAA7134_HSYNC_START,              0xEB);
   WriteByte(SAA7134_HSYNC_STOP,               0xE0);

   WriteByte(SAA7134_SYNC_CTRL,                0x18);  // PAL
   //WriteByte(SAA7134_SYNC_CTRL,              0x58);  // SECAM

   WriteByte(SAA7134_LUMA_CTRL,                0x40);  // PAL
   //WriteByte(SAA7134_LUMA_CTRL,              0x1b);  // SECAM
   WriteByte(SAA7134_DEC_LUMA_BRIGHT,          0x80);  // bright 128
   WriteByte(SAA7134_DEC_LUMA_CONTRAST,        0x44);  // contrast 68
   // Saturation: Auto=64, PAL=61, SECAM=64, NTSC=67
   WriteByte(SAA7134_DEC_CHROMA_SATURATION,    0x40);  // saturation 64 (auto)
   WriteByte(SAA7134_DEC_CHROMA_HUE,           0x00);  // hue 0
   WriteByte(SAA7134_CHROMA_CTRL1,             0x81);  // PAL, SECAM: d1
   WriteByte(SAA7134_CHROMA_GAIN_CTRL,         0x2a);  // PAL, SECAM: 80
   WriteByte(SAA7134_CHROMA_CTRL2,             0x06);  // PAL, SECAM: 00
   WriteByte(SAA7134_MODE_DELAY_CTRL,          0x00);
   WriteByte(SAA7134_ANALOG_ADC,               0x01);
   WriteByte(SAA7134_VGATE_START,              0x11);
   WriteByte(SAA7134_VGATE_STOP,               0xFE);
   WriteByte(SAA7134_MISC_VGATE_MSB,           0x1C);  // PAL+SECAM
   WriteByte(SAA7134_RAW_DATA_GAIN,            0x40);
   WriteByte(SAA7134_RAW_DATA_OFFSET,          0x80);
}

// ---------------------------------------------------------------------------
// Setup parameters for tasks A and B
//
static void SetupTasks( void )
{
   // 0x00 = YUV2
   WriteByte(SAA7134_OFMT_VIDEO_A, 0x00);
   WriteByte(SAA7134_OFMT_VIDEO_B, 0x00);

   // 0x06 = raw VBI
   WriteByte(SAA7134_OFMT_DATA_A, 0x06);
   WriteByte(SAA7134_OFMT_DATA_B, 0x06);

   // Let task A get odd then even field,
   // followed by task B getting odd then even field.
   WriteByte(SAA7134_TASK_CONDITIONS(SAA7134_TASK_A_MASK), 0x0E);
   WriteByte(SAA7134_TASK_CONDITIONS(SAA7134_TASK_B_MASK), 0x0E);

   // handle two fields per task & don't skip any fields
   WriteByte(SAA7134_FIELD_HANDLING(SAA7134_TASK_A_MASK), 0x02);
   WriteByte(SAA7134_FIELD_HANDLING(SAA7134_TASK_B_MASK), 0x02);

   WriteByte(SAA7134_V_FILTER(SAA7134_TASK_A_MASK), 0x00);
   WriteByte(SAA7134_V_FILTER(SAA7134_TASK_B_MASK), 0x00);

   WriteByte(SAA7134_LUMA_CONTRAST(SAA7134_TASK_A_MASK), 0x40);
   WriteByte(SAA7134_LUMA_CONTRAST(SAA7134_TASK_B_MASK), 0x40);

   WriteByte(SAA7134_CHROMA_SATURATION(SAA7134_TASK_A_MASK), 0x40);
   WriteByte(SAA7134_CHROMA_SATURATION(SAA7134_TASK_B_MASK), 0x40);

   WriteByte(SAA7134_LUMA_BRIGHT(SAA7134_TASK_A_MASK), 0x80);
   WriteByte(SAA7134_LUMA_BRIGHT(SAA7134_TASK_B_MASK), 0x80);

   WriteWord(SAA7134_H_PHASE_OFF_LUMA(SAA7134_TASK_A_MASK), 0x00);
   WriteWord(SAA7134_H_PHASE_OFF_LUMA(SAA7134_TASK_B_MASK), 0x00);
   WriteWord(SAA7134_H_PHASE_OFF_CHROMA(SAA7134_TASK_A_MASK), 0x00);
   WriteWord(SAA7134_H_PHASE_OFF_CHROMA(SAA7134_TASK_B_MASK), 0x00);

   WriteByte(SAA7134_VBI_PHASE_OFFSET_LUMA(SAA7134_TASK_A_MASK), 0x00);
   WriteByte(SAA7134_VBI_PHASE_OFFSET_LUMA(SAA7134_TASK_B_MASK), 0x00);
   WriteByte(SAA7134_VBI_PHASE_OFFSET_CHROMA(SAA7134_TASK_A_MASK), 0x00);
   WriteByte(SAA7134_VBI_PHASE_OFFSET_CHROMA(SAA7134_TASK_B_MASK), 0x00);

   WriteByte(SAA7134_DATA_PATH(SAA7134_TASK_A_MASK), 0x01);
   WriteByte(SAA7134_DATA_PATH(SAA7134_TASK_B_MASK), 0x01);

   // deinterlace y offsets ?? no idea what these are
   // Odds: 0x00 default
   // Evens: 0x00 default + yscale(1024) / 0x20;
   // ---
   // We can tweak these to change the top line to even (by giving
   // even an offset of 0x20) but it doesn't change VBI
   WriteByte(SAA7134_V_PHASE_OFFSET0(SAA7134_TASK_A_MASK), 0x00); // Odd
   WriteByte(SAA7134_V_PHASE_OFFSET1(SAA7134_TASK_A_MASK), 0x00); // Even
   WriteByte(SAA7134_V_PHASE_OFFSET2(SAA7134_TASK_A_MASK), 0x00); // Odd
   WriteByte(SAA7134_V_PHASE_OFFSET3(SAA7134_TASK_A_MASK), 0x00); // Even

   WriteByte(SAA7134_V_PHASE_OFFSET0(SAA7134_TASK_B_MASK), 0x00); // Odd
   WriteByte(SAA7134_V_PHASE_OFFSET1(SAA7134_TASK_B_MASK), 0x00); // Even
   WriteByte(SAA7134_V_PHASE_OFFSET2(SAA7134_TASK_B_MASK), 0x00); // Odd
   WriteByte(SAA7134_V_PHASE_OFFSET3(SAA7134_TASK_B_MASK), 0x00); // Even
}

// ---------------------------------------------------------------------------
// Initialize all registers
//
static void ResetHardware( void )
{
   WORD  Status;
   uint  idx;

   // Disable all interrupts
   WriteDword(SAA7134_IRQ1, 0UL);
   WriteDword(SAA7134_IRQ2, 0UL);

   // Disable all DMA
   WriteDword(SAA7134_MAIN_CTRL,
       SAA7134_MAIN_CTRL_VPLLE |
       SAA7134_MAIN_CTRL_APLLE |
       SAA7134_MAIN_CTRL_EXOSC |
       SAA7134_MAIN_CTRL_EVFE1 |
       SAA7134_MAIN_CTRL_EVFE2 |
       SAA7134_MAIN_CTRL_ESFE  |
       SAA7134_MAIN_CTRL_EBADC |
       SAA7134_MAIN_CTRL_EBDAC);

   // Disable all regions
   WriteByte(SAA7134_REGION_ENABLE, 0x00);

   // Soft reset
   WriteByte(SAA7134_REGION_ENABLE, 0x00);
   WriteByte(SAA7134_REGION_ENABLE, SAA7134_REGION_ENABLE_SWRST);
   WriteByte(SAA7134_REGION_ENABLE, 0x00);

   for (idx=0; idx < 20; idx++)
   {
      Status = ReadWord(SAA7134_SCALER_STATUS);
      if ((Status & SAA7134_SCALER_STATUS_WASRST) == 0)
         break;
      Sleep(2);
   }
   ifdebug0(idx >= 20, "ResetHardware: WASRST not cleared after 20*2 ms");

   WriteWord(SAA7134_SOURCE_TIMING, SAA7134_SOURCE_TIMING_DVED | 1);
   //WriteWord(SAA7134_SOURCE_TIMING, 0);

   WriteByte(SAA7134_START_GREEN, 0x00);
   WriteByte(SAA7134_START_BLUE, 0x00);
   WriteByte(SAA7134_START_RED, 0x00);

   for (idx=0; idx < 0x0F; idx++)
   {
      WriteByte(SAA7134_GREEN_PATH(idx), (idx+1)<<4);
      WriteByte(SAA7134_BLUE_PATH(idx), (idx+1)<<4);
      WriteByte(SAA7134_RED_PATH(idx), (idx+1)<<4);
   }
   WriteByte(SAA7134_GREEN_PATH(0x0F), 0xFF);
   WriteByte(SAA7134_BLUE_PATH(0x0F), 0xFF);
   WriteByte(SAA7134_RED_PATH(0x0F), 0xFF);

   // RAM FIFO config ???
   WriteDword(SAA7134_FIFO_SIZE, 0x08070503);
   WriteDword(SAA7134_THRESHOULD, 0x02020202);

   // enable peripheral devices
   WriteByte(SAA7134_SPECIAL_MODE, 0x01);

   SetTypicalSettings();
   SetupTasks();
}

// ---------------------------------------------------------------------------
// Write a "magic" marker into the video DMA buffer
// - when the magic is overwritten by the capture chip we know that the
//   associated VBI field is complete, (video lines are written before VBI
//   lines because they arrive later)
//
static void WriteFieldMarker( uint fieldIdx )
{
   volatile BYTE  * pVideoField;
   volatile ulong * pMarker;

   pVideoField  = HwMem_GetUserPointer(&VbiDmaMem[fieldIdx / 2]);
   pVideoField += VIDEO_BASE_OFFSET;
   if ((fieldIdx % 2) != 0)
      pVideoField += VIDEO_LINE_SIZE;

   pMarker = (ulong *) pVideoField;
   *(pMarker++) = VBI_FIELD_MARKER1;
   *(pMarker++) = VBI_FIELD_MARKER2;
}

// ---------------------------------------------------------------------------
// Search for a field with new data
// - check all video fields if the magic has been overwritten
// - start checking with the field after the last processed one, because this
//   is expected to be written to next (buffers are arranged in a cycle)
// - if another than the start field is written to, we must re-synchronize;
//   this should happen only during start-up and after buffer overruns
//
static uint CheckFieldMarkers( uint startIdx, bool * pOverflow )
{
   volatile BYTE  * pVideoField;
   volatile ulong * pMarker;
   bool  lost_sync;
   bool  saw_unchanged;
   uint  walkIdx;
   uint  retIdx;
   uint  count;

   walkIdx = startIdx % VBI_FIELD_CAPTURE_COUNT;
   retIdx = walkIdx;
   lost_sync = FALSE;
   saw_unchanged = FALSE;

   for (count=0; count < VBI_FIELD_CAPTURE_COUNT; count++)
   {
      pVideoField  = HwMem_GetUserPointer(&VbiDmaMem[walkIdx / 2]);
      pVideoField += VIDEO_BASE_OFFSET;
      if ((walkIdx % 2) != 0)
         pVideoField += VIDEO_LINE_SIZE;

      pMarker = (ulong *) pVideoField;
      if ( (*(pMarker++) == VBI_FIELD_MARKER1) &&
           (*(pMarker++) == VBI_FIELD_MARKER2) )
      {  // magic intact -> no new data in this field
         if (saw_unchanged == FALSE)
            retIdx = walkIdx;
         saw_unchanged = TRUE;
         //dprintf2("Marker unchanged in field #%d (start was %d)\n", walkIdx, startIdx);
      }
      else
      {  // magic was overwritten
         //dprintf2("Marker changed in field #%d (start was %d)\n", walkIdx, startIdx);
         if (saw_unchanged)
         {
            debug2("SAA7134 Check-FieldMarkers: lost sync: new data in field %d after start field %d", walkIdx, startIdx);
            lost_sync = TRUE;
            retIdx = (walkIdx + 1) % VBI_FIELD_CAPTURE_COUNT;
         }
      }
      walkIdx = (walkIdx + 1) % VBI_FIELD_CAPTURE_COUNT;
   }

   ifdebug0((saw_unchanged == FALSE), "SAA7134 Check-FieldMarkers: buffer overflow (all fileds have new data)");
   if (pOverflow != NULL)
      *pOverflow = ((saw_unchanged == FALSE) || lost_sync);

   return retIdx;
}

// ---------------------------------------------------------------------------
// VBI Driver Thread
//
static DWORD WINAPI SAA7134_VbiThread( LPVOID dummy )
{
   BYTE * pVbiLine;
   bool  overflow;
   bool  acceptFrame;
   uint  curIdx;
   uint  oldIdx;

   VbiDecodeSetSamplingRate(6750000 * 4, VBI_VSTART + 3);

   memset(&zvbi_rd[0], 0, sizeof(zvbi_rd[0]));
   zvbi_rd[0].sampling_rate    = 6750000 * 4;
   zvbi_rd[0].bytes_per_line   = VBI_LINE_SIZE;
   zvbi_rd[0].interlaced       = FALSE;
   zvbi_rd[0].synchronous      = TRUE;
   zvbi_rd[0].sampling_format  = VBI_PIXFMT_YUV420;
   zvbi_rd[0].scanning         = 625;
   memcpy(&zvbi_rd[1], &zvbi_rd[0], sizeof(zvbi_rd[1]));
   zvbi_rd[0].start[0]         = 7;
   zvbi_rd[0].count[0]         = VBI_LINES_PER_FIELD;
   zvbi_rd[0].start[1]         = -1;
   zvbi_rd[0].count[1]         = 0;
   zvbi_rd[1].start[0]         = -1;
   zvbi_rd[1].count[0]         = 0;
   zvbi_rd[1].start[1]         = 319;
   zvbi_rd[1].count[1]         = VBI_LINES_PER_FIELD;
   vbi_raw_decoder_add_services(&zvbi_rd[0], VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS, 1);
   vbi_raw_decoder_add_services(&zvbi_rd[1], VBI_SLICED_TELETEXT_B, 1);

   for (oldIdx = 0; oldIdx < VBI_FIELD_CAPTURE_COUNT; oldIdx++)
      WriteFieldMarker(oldIdx);
   oldIdx = 0;

   StartCapture();

   for (;;)
   {
      if (StopVbiThread == TRUE)
         break;

      if (pVbiBuf != NULL)
      {
         curIdx = CheckFieldMarkers(oldIdx, &overflow);
         if (overflow)
         {  // buffer overflow or sync lost -> discard all fields
            for (oldIdx = 0; oldIdx < VBI_FIELD_CAPTURE_COUNT; oldIdx++)
               WriteFieldMarker(oldIdx);
            oldIdx = curIdx;
            Sleep(1);
         }
         else if (oldIdx != curIdx)
         {
            do
            {
               //dprintf1("processing field %d\n", oldIdx);
               WriteFieldMarker(oldIdx);

               pVbiLine = HwMem_GetUserPointer(&VbiDmaMem[oldIdx / 2]);
               if (oldIdx % 2)
                  pVbiLine += VBI_LINES_PER_FIELD * VBI_LINE_SIZE;

               // notify teletext decoder about start of new field; since we don't have a
               // field counter (no capture interrupt) we always pass 0 as sequence number
               if (pVbiBuf->slicerType != VBI_SLICER_ZVBI)
               {
                  acceptFrame = VbiDecodeStartNewFrame(0);
                  if (acceptFrame)
                  {
                     uint  row;

                     for (row = 0; row < VBI_LINES_PER_FIELD; row++, pVbiLine += VBI_LINE_SIZE)
                     {
                        VbiDecodeLine(pVbiLine, row, TRUE);
                     }
                  }
               }
               else
               {
                  acceptFrame = ZvbiSliceAndProcess(&zvbi_rd[oldIdx % 2], pVbiLine, 0);
               }

               if (acceptFrame == FALSE)
               {  // channel change -> discard all previously captured data in the buffer
                  for (oldIdx = 0; oldIdx < VBI_FIELD_CAPTURE_COUNT; oldIdx++)
                     WriteFieldMarker(oldIdx);
                  oldIdx = curIdx;
                  break;
               }
               oldIdx = (oldIdx + 1) % VBI_FIELD_CAPTURE_COUNT;

            } while (oldIdx != curIdx);
         }
         else
         {  // no new data arrived
            // abort if we're no longer exclusive users of the hardware
            if (CheckDmaConflict())
            {
               pVbiBuf->hasFailed = TRUE;
               break;
            }
            Sleep(5);
         }
      }
      else
      {  // acq ctl is currently not interested in VBI data
         Sleep(10);
      }
   }
   StopCapture();

   ZvbiSliceAndProcess(NULL, NULL, 0);
   vbi_raw_decoder_destroy(&zvbi_rd[0]);
   vbi_raw_decoder_destroy(&zvbi_rd[1]);

   return 0;  // dummy
}

// ---------------------------------------------------------------------------
//                    I N T E R F A C E   F U N C T I O N S
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Check if the current source has a video signal
//
static bool SAA7134_IsVideoPresent( void )
{
   WORD CheckMask = SAA7134_STATUS_VIDEO_HLCK;

   return ((ReadWord(SAA7134_STATUS_VIDEO) & CheckMask) == 0);
}

// ---------------------------------------------------------------------------
// Start acquisition
// - start the VBI thread
// - start capturing
//
static bool SAA7134_StartAcqThread( void )
{
   DWORD LinkThreadID;
   bool result;

   StopVbiThread = FALSE;

   vbiThreadHandle = CreateThread(NULL, 0, SAA7134_VbiThread, NULL, 0, &LinkThreadID);

   result = (vbiThreadHandle != NULL);
   ifdebug1(!result, "SAA7134-StartAcqThread: failed to create VBI thread: %ld", GetLastError());

#ifndef DISABLE_PCI_REGISTER_DUMP
   Sleep(500);
   SAA7134_DumpRegisters();
#endif

   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition
// - disable capturing
// - stop the VBI thread
//
static void SAA7134_StopAcqThread( void )
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
   StopCapture();
}

// ----------------------------------------------------------------------------
// Initialize PCI card
//
static void SAA7134_ResetChip( DWORD m_BusNumber, DWORD m_SlotNumber )
{
    // nothing needs to be done here for SAA7134 cards
}

// ---------------------------------------------------------------------------
// Set parameters
//
static bool SAA7134_Configure( uint threadPrio, uint pllType )
{
   if (vbiThreadHandle != NULL)
   {
      if (SetThreadPriority(vbiThreadHandle, threadPrio) == 0)
         debug2("SAA7134-Configure: SetThreadPriority(%d) returned %ld", threadPrio, GetLastError());
   }
   return TRUE;
}

// ---------------------------------------------------------------------------
// Allocate resources and initialize PCI registers
//
static bool SAA7134_Open( TVCARD * pTvCard, bool wdmStop )
{
   BYTE  CapCtl;
   DWORD DmaCtl;
   bool  result;

   CardConflictDetected = FALSE;

   // check if capturing is already enabled
   DmaCtl = ReadDword(SAA7134_MAIN_CTRL) &
       ( SAA7134_MAIN_CTRL_TE6 | SAA7134_MAIN_CTRL_TE5 | SAA7134_MAIN_CTRL_TE4 |
         SAA7134_MAIN_CTRL_TE3 | SAA7134_MAIN_CTRL_TE2 | SAA7134_MAIN_CTRL_TE1 |
         SAA7134_MAIN_CTRL_TE0 );
   CapCtl = ReadByte(SAA7134_REGION_ENABLE);
   if (DmaCtl && CapCtl)
   {
      debug2("SAA7134-Open: capturing already enabled (Region=0x%x, DMA=0x%lx)", CapCtl, DmaCtl);

      MessageBox(NULL, "Capturing is already enabled in the TV card!\n"
                       "Probably another video application is running,\n"
                       "however nxtvepg requires exclusive access.\n"
                       "Aborting data acquisition.",
                       "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      CardConflictDetected = TRUE;
      result = FALSE;
   }
   else
   {
      // Allocate memory for DMA
      result = SAA7134_AllocDmaMemory();

      if (result != FALSE)
      {
         // Save state of PCI registers
         HwPci_InitStateBuf();
         SAA7134_ManageMyState(pTvCard);

         // Initialize PCI registers
         ResetHardware();

         SetupDMAMemory();

         SetTaskVbiGeometry(SAA7134_TASK_A_MASK);
         SetTaskVbiGeometry(SAA7134_TASK_B_MASK);
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Free I/O resources and close the driver device
//
static void SAA7134_Close( TVCARD * pTvCard )
{
   // skip reset if conflict was detected earlier to avoid crashing the other app
   if (CardConflictDetected == FALSE)
   {
      // reset GPIO ports (mute audio)
      pTvCard->cfg->SetVideoSource(pTvCard, -1);

      // reset PCI registers
      WriteByte(SAA7134_REGION_ENABLE, 0x00);
      WriteDword(SAA7134_IRQ1, 0UL);
      WriteDword(SAA7134_IRQ2, 0UL);
      MaskDataDword(SAA7134_MAIN_CTRL, 0UL,
          SAA7134_MAIN_CTRL_TE6 |
          SAA7134_MAIN_CTRL_TE5 |
          SAA7134_MAIN_CTRL_TE4 |
          SAA7134_MAIN_CTRL_TE3 |
          SAA7134_MAIN_CTRL_TE2 |
          SAA7134_MAIN_CTRL_TE1 |
          SAA7134_MAIN_CTRL_TE0);

      // sofware reset
      WriteByte(SAA7134_REGION_ENABLE, 0x00);
      WriteByte(SAA7134_REGION_ENABLE, SAA7134_REGION_ENABLE_SWRST);
      WriteByte(SAA7134_REGION_ENABLE, 0x00);

      // reset PCI registers to original state
      HwPci_RestoreState();
      SAA7134_ManageMyState(pTvCard);
   }

   SAA7134_FreeDmaMemory();
}

// ---------------------------------------------------------------------------
// Fill structure with interface functions
//
static const TVCARD_CTL SAA7134_CardCtl =
{
   SAA7134_IsVideoPresent,
   SAA7134_StartAcqThread,
   SAA7134_StopAcqThread,
   SAA7134_Configure,
   SAA7134_ResetChip,
   SAA7134_Close,
   SAA7134_Open,
};


void SAA7134_GetInterface( TVCARD * pTvCard )
{
   if (pTvCard != NULL)
   {
      memset(pTvCard, 0, sizeof(*pTvCard));

      pTvCard->ctl = &SAA7134_CardCtl;

      SAA7134_I2cGetInterface(pTvCard);
      SAA7134Typ_GetInterface(pTvCard);
   }
   else
      fatal0("SAA7134-GetInterface: NULL ptr param");
}

