/*
 *  Win32 VBI capture driver for the Conexant 23881 chip (aka Bt881)
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
 *    This module is a "user-space driver" for the Conexant 23881 chip.
 *    It's directly derived from the DScaler driver.
 *
 *
 *  Authors:
 *      Quoting DScaler:
 *      "This code is based on a version of dTV modified by Michael Eskin and
 *      others at Connexant.  Those parts are probably (c) Connexant 2002"
 *
 *  DScaler #Id: CX2388xCard.cpp,v 1.39 2003/01/29 18:24:49 adcockj Exp #
 *  DScaler #Id: CX2388xSource.cpp,v 1.42 2003/01/25 23:46:25 laurentg Exp #
 *  DScaler #Id: CX2388xProvider.cpp,v 1.3 2002/11/02 09:47:36 adcockj Exp #
 *
 *  $Id: cx2388x.c,v 1.11 2003/04/12 17:52:27 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

// debug features
#define DISABLE_PCI_REGISTER_DUMP

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/vbidecode.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"

#include "dsdrv/dsdrvlib.h"
#include "dsdrv/hwmem.h"           // XXX fixme: should be in dsdrvlib.h
#include "dsdrv/hwpci.h"           // XXX fixme: should be in dsdrvlib.h
#include "dsdrv/tvcard.h"
#include "dsdrv/wintuner.h"
#include "dsdrv/bt8x8_i2c.h"
#include "dsdrv/cx2388x_reg.h"
#include "dsdrv/cx2388x_typ.h"
#include "dsdrv/cx2388x.h"


// ----------------------------------------------------------------------------
// Declaration of internal variables
//
static HANDLE vbiThreadHandle = NULL;
static BOOL StopVbiThread;

#define VBI_LINE_SIZE            2048
#define VBI_LINES_PER_FIELD        16
#define VBI_LINES_PER_FRAME      (VBI_LINES_PER_FIELD * 2)
#define VBI_FRAME_CAPTURE_COUNT     5
#define VBI_FIELD_CAPTURE_COUNT  (VBI_FRAME_CAPTURE_COUNT * 2)

#define VBI_SPL                  2044
#define FSC_PAL                  4.43361875

#define RISC_CODE_LENGTH         4096
typedef DWORD PHYS;

static THwMem  m_RiscDMAMem;
static THwMem  m_VBIDMAMem[VBI_FRAME_CAPTURE_COUNT];
static PHYS    pRiscBasePhysical;

static DWORD m_I2CRegister;
static ULONG m_I2CSleepCycle;
static bool  m_I2CInitialized;
static bool  CardConflictDetected;


// ----------------------------------------------------------------------------
// Debug helper function: dump content of important PCI registers
//
#ifndef DISABLE_PCI_REGISTER_DUMP
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>

typedef struct DumpRegisterStruct
{
   const char * pName;
   uint         offset;
} DUMP_REG_SPEC;

#define DumpRegister(REG)  {#REG, (REG)}

static const DUMP_REG_SPEC dumpDwordRegs[] =
{
   DumpRegister(CX2388X_DEVICE_STATUS),
   DumpRegister(CX2388X_VIDEO_INPUT),
   DumpRegister(CX2388X_TEMPORAL_DEC),
   DumpRegister(CX2388X_AGC_BURST_DELAY),
   DumpRegister(CX2388X_BRIGHT_CONTRAST), 
   DumpRegister(CX2388X_UVSATURATION),    
   DumpRegister(CX2388X_HUE),             
   DumpRegister(CX2388X_WHITE_CRUSH),
   DumpRegister(CX2388X_PIXEL_CNT_NOTCH),
   DumpRegister(CX2388X_HORZ_DELAY_EVEN),
   DumpRegister(CX2388X_HORZ_DELAY_ODD),
   DumpRegister(CX2388X_VERT_DELAY_EVEN),
   DumpRegister(CX2388X_VERT_DELAY_ODD),
   DumpRegister(CX2388X_VDELAYCCIR_EVEN),
   DumpRegister(CX2388X_VDELAYCCIR_ODD),
   DumpRegister(CX2388X_HACTIVE_EVEN),
   DumpRegister(CX2388X_HACTIVE_ODD),
   DumpRegister(CX2388X_VACTIVE_EVEN),    
   DumpRegister(CX2388X_VACTIVE_ODD),     
   DumpRegister(CX2388X_HSCALE_EVEN),     
   DumpRegister(CX2388X_HSCALE_ODD),      
   DumpRegister(CX2388X_VSCALE_EVEN),     
   DumpRegister(CX2388X_VSCALE_ODD),      
   DumpRegister(CX2388X_FILTER_EVEN),     
   DumpRegister(CX2388X_FILTER_ODD),      
   DumpRegister(CX2388X_FORMAT_2HCOMB),
   DumpRegister(CX2388X_PLL),
   DumpRegister(CX2388X_PLL_ADJUST),
   DumpRegister(CX2388X_SAMPLERATECONV),  
   DumpRegister(CX2388X_SAMPLERATEFIFO),  
   DumpRegister(CX2388X_SUBCARRIERSTEP),  
   DumpRegister(CX2388X_SUBCARRIERSTEPDR),
   DumpRegister(CX2388X_CAPTURECONTROL),  
   DumpRegister(CX2388X_VIDEO_COLOR_FORMAT),
   DumpRegister(CX2388X_VBI_SIZE),
   DumpRegister(CX2388X_FIELD_CAP_CNT),
   DumpRegister(CX2388X_VIDY_GP_CNT),
   DumpRegister(CX2388X_VBI_GP_CNT),
   DumpRegister(CX2388X_VIP_CONFIG),
   DumpRegister(CX2388X_VIP_CONTBRGT),
   DumpRegister(CX2388X_VIP_HSCALE),
   DumpRegister(CX2388X_VIP_VSCALE),
   DumpRegister(CX2388X_VBOS),

   DumpRegister(CX2388X_DEV_CNTRL2),
   DumpRegister(CX2388X_VID_INTSTAT),
   DumpRegister(CX2388X_VID_DMA_CNTRL),
   DumpRegister(CX2388X_CAPTURECONTROL),
   DumpRegister(SRAM_CMDS_24 + 0x00),
   DumpRegister(SRAM_CMDS_24 + 0x04),
   DumpRegister(SRAM_CMDS_24 + 0x08),
   DumpRegister(SRAM_CMDS_24 + 0x0C),
   DumpRegister(SRAM_CMDS_24 + 0x10),
   DumpRegister(MO_DMA24_PTR2),
   DumpRegister(MO_DMA24_CNT1),
   DumpRegister(MO_DMA24_CNT2),

   DumpRegister(MO_GP0_IO),
   DumpRegister(MO_GP1_IO),   
   DumpRegister(MO_GP2_IO),
   DumpRegister(MO_GP3_IO),
   DumpRegister(MO_GPIO),
   DumpRegister(MO_GPOE),
};

static const DUMP_REG_SPEC dumpByteRegs[] =
{
   DumpRegister(0x320d01),
   DumpRegister(0x320d02),
   DumpRegister(0x320d03),
   DumpRegister(0x320d04),
   DumpRegister(0x320d2a),
   DumpRegister(0x320d2b),
};

static void Cx2388x_DumpRegisters( const char * pHeader )
{
   char    buf[256];
   uint    idx;
   int     fd;

   fd = open("cx23881-pci.log", O_WRONLY|O_CREAT|O_APPEND, 0666);
   if (fd >= 0)
   {
      write(fd, "\n\n", 2);
      write(fd, pHeader, strlen(pHeader));
      write(fd, "\n", 1);

      for (idx=0; idx < sizeof(dumpDwordRegs)/sizeof(dumpDwordRegs[0]); idx++)
      {
         sprintf(buf, "%s\t%08lx\n", dumpDwordRegs[idx].pName, ReadDword(dumpDwordRegs[idx].offset));
         write(fd, buf, strlen(buf));
      }

      for (idx=0; idx < sizeof(dumpByteRegs)/sizeof(dumpByteRegs[0]); idx++)
      {
         sprintf(buf, "%s\t%02x\n", dumpByteRegs[idx].pName, ReadByte(dumpByteRegs[idx].offset));
         write(fd, buf, strlen(buf));
      }
      close(fd);
   }
}
#endif

// ----------------------------------------------------------------------------
// Save/reload card state into/from file
// - save or restore everything that might be used by the "real" drivers
//
static void Cx2388x_ManageMyState( void )
{
    DWORD i;

    // save and restore the whole of the SRAM
    // as the drivers might well have stored code and fifo
    // buffers in there that we will overwrite
    for(i = 0x180040; i < 0x187FFF; i += 4)
    {
        ManageDword(i);
    }

    // save and restore the registers than we overwrite
    ManageDword(MO_DMA21_PTR2);
    ManageDword(MO_DMA21_CNT1);
    ManageDword(MO_DMA21_CNT2);

    ManageDword(MO_DMA24_PTR2);
    ManageDword(MO_DMA24_CNT1);
    ManageDword(MO_DMA24_CNT2);
    
    ManageDword(MO_DMA25_PTR2);
    ManageDword(MO_DMA25_CNT1);
    ManageDword(MO_DMA25_CNT2);

    ManageDword(MO_DMA26_PTR2);
    ManageDword(MO_DMA26_CNT1);
    ManageDword(MO_DMA26_CNT2);
}

// ----------------------------------------------------------------------------
//
static void Cx2388x_StartCapture( void )
{
   DWORD value1;
   DWORD value2;

   dprintf0("Cx2388x-StartCapture: starting acquisition\n");

   // RISC Controller Enable
   WriteDword(CX2388X_DEV_CNTRL2, 1<<5);

   // Clear Interrupt Status bits
   WriteDword(CX2388X_VID_INTSTAT, 0x0000000);

   value1 = ReadDword(CX2388X_VID_DMA_CNTRL) & 0xFFFFFF00;
   value2 = ReadDword(CX2388X_CAPTURECONTROL) & 0xFFFFFF00;

   // enable VBI RISC processing and FIFO
   value1 |= 0x88;
   value2 |= 0x18;

   WriteDword(CX2388X_VID_DMA_CNTRL, value1);
   WriteDword(CX2388X_CAPTURECONTROL, value2);

   // Clear Interrupt Status bits
   WriteDword(CX2388X_VID_INTSTAT, 0xFFFFFFFF);
}

// ----------------------------------------------------------------------------
// Halt the DMA control program (RISC)
//
static void Cx2388x_StopCapture( void )
{
   // Firstly stop the card from doing anything
   // so stop the risc controller
   WriteDword(CX2388X_DEV_CNTRL2, 0x00000000);

   // then stop capturing video and VBI
   MaskDataDword(CX2388X_VID_DMA_CNTRL, 0x00, 0xFF);
   MaskDataDword(CX2388X_CAPTURECONTROL, 0x00,0xFF);

   // set the RISC addresses to be NULL
   // so that nobody tries to run our RISC code later
   WriteDword(SRAM_CMDS_24 + 0x00, 0);

   dprintf0("Cx2388x-StopCapture: stopped acquisition\n");
}

// ----------------------------------------------------------------------------
// Program the PLL to a specific output frequency.
// - assume that we have a PLL pre dividor of 2
//
static double Cx2388x_SetPLL( double PLLFreq )
{
   DWORD RegValue = 0;
   int Prescaler = 2;
   double PLLValue;
   int PLLInt = 0;
   int PLLFraction;

   PLLValue = PLLFreq * 8.0 * (double)Prescaler / 28.63636;

   while ((PLLValue < 14.0) && (Prescaler < 5))
   {
      Prescaler += 1;
      PLLValue = PLLFreq * 8.0 * (double)Prescaler / 28.63636;
   }

   switch (Prescaler)
   {
      case 2:
         RegValue = 0 << 26;
         break;
      case 3:
         RegValue = 3 << 26;
         break;
      case 4:
         RegValue = 2 << 26;
         break;
      case 5:
         RegValue = 1 << 26;
         break;
      default:
         debug1("Invalid PLL Pre Scaler value %d", Prescaler);
         break;
   }

   PLLInt = (int)PLLValue;
   PLLFraction = (int)((PLLValue - (double)PLLInt) * (double)(1<<20) + 0.5);

   // Check for illegal PLL values
   if (PLLInt < 14 || PLLInt > 63)
   {
      debug1("Invalid PLL value %f MHz", PLLFreq);
      return 0.0;
   }

   // Set register int and fraction values
   RegValue |= PLLInt << 20;
   RegValue |= PLLFraction & 0xFFFFF;

   WriteDword(CX2388X_PLL , RegValue);

   return (28.63636 / (8.0 * (double)Prescaler)) * ((double)PLLInt + (double)PLLFraction / (double)(1 << 20));
}

// ----------------------------------------------------------------------------
// Sets up card to support size and format requested
//
static void Cx2388x_SetGeoSize( TVCARD * pTvCard, uint nInput, uint TVFormat )
{
   DWORD HTotal;
   double PLL;
   DWORD VideoInput;
   DWORD RegValue;
   DWORD FilterDefault;
   DWORD HCombDefault;

   // start with default values 
   // the only bit switched on is CFILT
   FilterDefault = (1 << 19);
   HCombDefault = 0x181f0008;

   PLL = Cx2388x_SetPLL(27.0);

   // Setup correct format
   VideoInput = ReadDword(CX2388X_VIDEO_INPUT);
   VideoInput &= 0xfffffff0;

   if (pTvCard->cfg->IsInputSVideo(pTvCard, nInput))
   {
      // set up with
      // Previous line remodulation - off
      // 3-d Comb filter - off
      // Comb Range - 00
      // Full Luma Range - on
      // PAL Invert Phase - off
      // Coring - off
      HCombDefault = 0x08;

      // switch off luma notch
      // Luma notch is 1 = off
      FilterDefault |= CX2388X_FILTER_LNOTCH;
      // turn on just the chroma Comb Filter
      FilterDefault |= 1 << 5;
      // Disable luma dec
      FilterDefault |= 1 << 12;
   }
   else
   {
      // set up with
      // Previous line remodulation - on
      // 3-d Comb filter - on
      // Comb Range - 1f
      // Full Luma Range - on
      // PAL Invert Phase - off
      // Coring - off
      HCombDefault = 0x181f0008;
   }

   if (TVFormat == VIDEO_MODE_PAL)
   {
      VideoInput |= VideoFormatPALBDGHI;
      HTotal = HLNotchFilter135PAL | 864;
      HCombDefault |= (1 << 26);
   }
   else
   {   /* SECAM */
      VideoInput |= VideoFormatSECAM;
      HTotal = HLNotchFilter135PAL | 864;
      // test for Laurent
      // other stuff that may be required
      // Comments from Laurent
      // Bits 12, 16, and 18 must be set to 1 for SECAM
      // It seems to work even for PAL with these bits
      // TODO : check that they must be set for all the video formats
      // QCIF HFilter
      FilterDefault |= (1<<11);
      // 29 Tap first chroma demod
      FilterDefault |= (1<<15);
      // Third Chroma Demod - on
      FilterDefault |= (1<<17);
   }

   WriteDword(CX2388X_VIDEO_INPUT, VideoInput);
   WriteDword(CX2388X_PIXEL_CNT_NOTCH, HTotal);
   WriteDword(CX2388X_FORMAT_2HCOMB, HCombDefault);
   WriteDword(CX2388X_FILTER_EVEN, FilterDefault);
   WriteDword(CX2388X_FILTER_ODD, FilterDefault);

   // set up subcarrier frequency
   RegValue = (DWORD)(((8.0 * FSC_PAL) / PLL) * (double)(1<<22));
   WriteDword( CX2388X_SUBCARRIERSTEP, RegValue & 0x7FFFFF );
   // Subcarrier frequency Dr, for SECAM only but lets
   // set it anyway
   RegValue = (DWORD)((8.0 * 4.406250 / PLL) * (double)(1<<22));
   WriteDword( CX2388X_SUBCARRIERSTEPDR, RegValue);

   // set up burst gate delay and AGC gate delay
   RegValue = (DWORD)(6.8 * PLL / 2.0 + 15.5);
   RegValue |= (DWORD)(6.5 * PLL / 2.0 + 21.5) << 8;
   WriteDword(CX2388X_AGC_BURST_DELAY, RegValue);
}

// ----------------------------------------------------------------------------
// Initialize video input related settings
// - since we do not care for the video image, only defaults are used
//   (no need to support choma decoding, comb filters, scaling etc.)
//
static void Cx2388x_VideoInit( void )
{
   DWORD VBIPackets;

   VBIPackets = (DWORD)((VBI_SPL/4) * 27.0 / (8 * FSC_PAL));
   WriteDword(CX2388X_VBI_SIZE, (VBIPackets & 0x1ff) | (10<<11));

   WriteDword(CX2388X_VERT_DELAY_EVEN, 0x44);
   WriteDword(CX2388X_VERT_DELAY_ODD, 0x44);

   WriteDword(CX2388X_HACTIVE_EVEN, 720);
   WriteDword(CX2388X_HACTIVE_ODD, 720);

   WriteDword(CX2388X_VACTIVE_EVEN, 576);
   WriteDword(CX2388X_VACTIVE_ODD, 576);

   WriteDword(CX2388X_HORZ_DELAY_EVEN, 130);
   WriteDword(CX2388X_HORZ_DELAY_ODD, 130);

   WriteDword(CX2388X_HSCALE_EVEN, 0);
   WriteDword(CX2388X_HSCALE_ODD, 0);

   WriteDword(CX2388X_VSCALE_EVEN, 0);
   WriteDword(CX2388X_VSCALE_ODD, 0);
}

// ----------------------------------------------------------------------------
// Initialize all PCI registers
//
static void Cx2388x_ResetHardware( DWORD m_BusNumber, DWORD m_SlotNumber )
{
   PCI_COMMON_CONFIG PCI_Config;
   int  i;
   int  j;

   // try and switch on the card using the PCI Command value
   // this is to try and solve problems when a driver hasn't been
   // loaded for the card, which may be necessary when you have
   // multiple cx2388x cards
   if (HwPci_GetPCIConfig(&PCI_Config, m_BusNumber, m_SlotNumber))
   {
      // switch on allow master and respond to memory requests
      if ((PCI_Config.Command & 0x06) != 0x06)
      {
         debug1(" CX2388x PCI Command was %d", PCI_Config.Command);
         PCI_Config.Command |= 0x06;
         HwPci_SetPCIConfig(&PCI_Config, m_BusNumber, m_SlotNumber);
      }
   }

   // Firstly stop the card from doing anything
   // so stop the risc controller
   WriteDword(CX2388X_DEV_CNTRL2, 0x00000000);
   // then stop any DMA transfers
   WriteDword(MO_VID_DMACNTRL, 0x00000000);
   WriteDword(MO_AUD_DMACNTRL, 0x00000000);
   WriteDword(MO_TS_DMACNTRL, 0x00000000);
   WriteDword(MO_VIP_DMACNTRL, 0x00000000);
   WriteDword(MO_GPHST_DMACNTRL, 0x00000000);

   // secondly stop any interupts from happening
   // if we change something and let an
   // interupt happen than the driver might try and
   // do something bad
   WriteDword( CX2388X_PCI_INTMSK, 0x00000000 );
   WriteDword( CX2388X_VID_INTMSK, 0x00000000 );
   WriteDword( CX2388X_AUD_INTMSK, 0x00000000 );
   WriteDword( CX2388X_TS_INTMSK, 0x00000000 );
   WriteDword( CX2388X_VIP_INTMSK, 0x00000000 );
   WriteDword( CX2388X_GPHST_INTMSK, 0x00000000 );

   WriteDword( CX2388X_VID_INTSTAT, 0xFFFFFFFF ); // Clear PIV int
   WriteDword( CX2388X_PCI_INTSTAT, 0xFFFFFFFF ); // Clear PCI int
   WriteDword( MO_INT1_STAT, 0xFFFFFFFF );        // Clear RISC int

   // clear field capture counter
   WriteDword( CX2388X_FIELD_CAP_CNT, 0 );


   // wait a bit so that everything has cleared through
   Sleep(200);

   // Clear out the SRAM Channel Management data structures
   // for all 12 devices
   for (i=1; i<=12; ++i)
   {
      DWORD dwaddr = 0x180000+i*0x40;
      for (j=0; j<5; ++j)
      {
         WriteDword(dwaddr+(j*4),0);
      }
   }

   // Reset the chip (note sure about this)
   //WriteDword( 0x310304, 0x1 );
   //::Sleep(500);

   /////////////////////////////////////////////////////////////////
   // Setup SRAM tables
   /////////////////////////////////////////////////////////////////

   // first check that everything we want to fit in SRAM
   // actually does fit, I'd hope this gets picked up in debug
   if (SRAM_NEXT > SRAM_MAX)
   {
      debug0("Too much to fit in SRAM");
   }

   /////////////////////////////////////////////////////////////////
   // Setup for video channel 21
   /////////////////////////////////////////////////////////////////

   // Instruction Queue Base
   WriteDword(SRAM_CMDS_21 + 0x0c, SRAM_INSTRUCTION_QUEUE_VIDEO);

   // Instruction Queue Size is in DWORDs
   WriteDword(SRAM_CMDS_21 + 0x10, (SRAM_INSTRUCTION_QUEUE_SIZE / 4));

   // Cluster table base
   WriteDword(SRAM_CMDS_21 + 0x04, SRAM_CLUSTER_TABLE_VIDEO);

   // Cluster table size is in QWORDS
   WriteDword(SRAM_CMDS_21 + 0x08, SRAM_CLUSTER_TABLE_VIDEO_SIZE / 8);

   // Fill in cluster buffer entries
   for (i = 0; i < SRAM_VIDEO_BUFFERS; i++)
   {
       WriteDword( SRAM_CLUSTER_TABLE_VIDEO + (i * 0x10),
                   SRAM_FIFO_VIDEO_BUFFERS + (i * SRAM_FIFO_VIDEO_BUFFER_SIZE) );
   }

   // Copy the cluster buffer info to the DMAC

   // Set the DMA Cluster Table Address
   WriteDword(MO_DMA21_PTR2, SRAM_CLUSTER_TABLE_VIDEO);

   // Set the DMA buffer limit size in qwords
   WriteDword(MO_DMA21_CNT1, SRAM_FIFO_VIDEO_BUFFER_SIZE / 8);

   // Set the DMA Cluster Table Size in qwords
   WriteDword(MO_DMA21_CNT2, SRAM_CLUSTER_TABLE_VIDEO_SIZE / 8);

   /////////////////////////////////////////////////////////////////
   // Setup for VBI channel 24
   /////////////////////////////////////////////////////////////////

   // Instruction Queue Base
   WriteDword(SRAM_CMDS_24 + 0x0c, SRAM_INSTRUCTION_QUEUE_VBI);

   // Instruction Queue Size is in DWORDs
   WriteDword(SRAM_CMDS_24 + 0x10, (SRAM_INSTRUCTION_QUEUE_SIZE / 4));

   // Cluster table base
   WriteDword(SRAM_CMDS_24 + 0x04, SRAM_CLUSTER_TABLE_VBI);

   // Cluster table size is in QWORDS
   WriteDword(SRAM_CMDS_24 + 0x08, (SRAM_CLUSTER_TABLE_VBI_SIZE / 8));

   // Fill in cluster buffer entries
   for (i = 0; i < SRAM_VBI_BUFFERS; ++i)
   {
       WriteDword( SRAM_CLUSTER_TABLE_VBI + (i * 0x10),
                   SRAM_FIFO_VBI_BUFFERS + (i * SRAM_FIFO_VBI_BUFFER_SIZE) );
   }

   // Copy the cluster buffer info to the DMAC

   // Set the DMA Cluster Table Address
   WriteDword(MO_DMA24_PTR2, SRAM_CLUSTER_TABLE_VBI);

   // Set the DMA buffer limit size in qwords
   WriteDword(MO_DMA24_CNT1, SRAM_FIFO_VBI_BUFFER_SIZE / 8);

   // Set the DMA Cluster Table Size in qwords
   WriteDword(MO_DMA24_CNT2, (SRAM_CLUSTER_TABLE_VBI_SIZE / 8));

   /////////////////////////////////////////////////////////////////
   // Other one off settings for the chip
   /////////////////////////////////////////////////////////////////

   // set format to YUY2
   MaskDataDword(CX2388X_VIDEO_COLOR_FORMAT, 0x00000044, 0x000000FF);

   // Test from Mike Asbury's regtool init code
   WriteDword( MO_PDMA_STHRSH, 0x0807 ); // Fifo source Threshhold
   WriteDword( MO_PDMA_DTHRSH, 0x0807 ); // Fifo Threshhold

   WriteDword( CX2388X_VID_INTSTAT, 0xFFFFFFFF ); // Clear PIV int
   WriteDword( CX2388X_PCI_INTSTAT, 0xFFFFFFFF ); // Clear PCI int
   WriteDword( MO_INT1_STAT, 0xFFFFFFFF );   // Clear RISC int

   //
   // Fixes for flashing suggested by Ben Felts
   //
   // 1.  Set bits 16:9 of register 0xE4310208 to 0x00.
   //     The default value is 0x03803C0F, which becomes 0x0380000F with this change.
   WriteDword( CX2388X_AGC_SYNC_TIP1, 0x0380000F );

   //2.  Set bits 27:26 of register 0xE4310200 to 0x0.  The default value is
   //    0x0CE00555, which becomes 0x00E00555 with this change.
   WriteDword( CX2388X_AGC_BACK_VBI, 0x00E00555 );
}

// ----------------------------------------------------------------------------
//
static ULONG Cx2388x_GetTickCount( void )
{
   ULONGLONG ticks;
   ULONGLONG frequency;

   QueryPerformanceFrequency((PLARGE_INTEGER)&frequency);
   QueryPerformanceCounter((PLARGE_INTEGER)&ticks);
   ticks = (ticks & 0xFFFFFFFF00000000) / frequency * 10000000 +
           (ticks & 0xFFFFFFFF) * 10000000 / frequency;
   return (ULONG)(ticks / 10000);
}

static void Cx2388x_I2cInitialize( void )
{
   volatile DWORD i;
   DWORD elapsed;
   DWORD start;

   WriteDword(CX2388X_I2C, 1);
   m_I2CRegister = ReadDword(CX2388X_I2C);

   m_I2CSleepCycle = 10000L;
   elapsed = 0L;
   // get a stable reading
   while (elapsed < 5)
   {
      m_I2CSleepCycle *= 10;
      start = Cx2388x_GetTickCount();
      for (i = m_I2CSleepCycle; i > 0; i--)
         ;
      elapsed = Cx2388x_GetTickCount() - start;
   }
   // calculate how many cycles a 50kHZ is (half I2C bus cycle)
   m_I2CSleepCycle = m_I2CSleepCycle / elapsed * 1000L / 50000L;

   m_I2CInitialized = TRUE;
}

static void Cx2388x_I2cSleep( void )
{
   volatile DWORD i;

   for (i = m_I2CSleepCycle; i > 0; i--)
      ;
}

static void Cx2388x_SetSDA(bool value)
{
   if (m_I2CInitialized == FALSE)
   {
      Cx2388x_I2cInitialize();
   }
   if (value)
   {
      m_I2CRegister |= CX2388X_I2C_SDA;
   }
   else
   {
      m_I2CRegister &= ~CX2388X_I2C_SDA;
   }
   WriteDword(CX2388X_I2C, m_I2CRegister);
}

static void Cx2388x_SetSCL(bool value)
{
   if (m_I2CInitialized == FALSE)
   {
      Cx2388x_I2cInitialize();
   }
   if (value)
   {
      m_I2CRegister |= CX2388X_I2C_SCL;
   }
   else
   {
      m_I2CRegister &= ~CX2388X_I2C_SCL;
   }
   WriteDword(CX2388X_I2C, m_I2CRegister);
}

static bool Cx2388x_GetSDA( void )
{
   if (m_I2CInitialized == FALSE)
   {
      Cx2388x_I2cInitialize();
   }
   return ((ReadDword(CX2388X_I2C) & CX2388X_I2C_SDA) != 0);
}

static bool Cx2388x_GetSCL( void )
{
   if (m_I2CInitialized == FALSE)
   {
      Cx2388x_I2cInitialize();
   }
   return ((ReadDword(CX2388X_I2C) & CX2388X_I2C_SCL) != 0);
}

// ----------------------------------------------------------------------------
// Free DMA buffers and unlock them for DMA
//
static void Cx2388x_FreeDmaMemory( void )
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
static BOOL Cx2388x_AllocDmaMemory( void )
{
   uint  idx;

   memset(&m_RiscDMAMem, 0, sizeof(m_RiscDMAMem));
   memset(m_VBIDMAMem, 0, sizeof(m_VBIDMAMem));

   if (HwMem_AllocContigMemory(&m_RiscDMAMem, RISC_CODE_LENGTH) == FALSE)
   {
      MessageBox(NULL, "Failed to allocate RISC code memory: driver abort", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      Cx2388x_FreeDmaMemory();
      return FALSE;
   }

   for (idx=0; idx < VBI_FRAME_CAPTURE_COUNT; idx++)
   {
      if (HwMem_AllocUserMemory(&m_VBIDMAMem[idx], VBI_LINE_SIZE * VBI_LINES_PER_FIELD * 2) == FALSE)
      {
         MessageBox(NULL, "VBI Memory for DMA not Allocated", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         Cx2388x_FreeDmaMemory();
         return FALSE;
      }
   }

   return TRUE;
}

// ----------------------------------------------------------------------------
// Create DMA control code
//
static bool Cx2388x_CreateRiscCode( void )
{
   DWORD * pRiscCode;
   BYTE  * pVbiUser;
   PHYS    pVbiPhysical;
   uint  nField;
   uint  nLine;
   DWORD GotBytesPerLine;
   DWORD Instruction;

   // we create the RISC code for 10 fields
   // the first one (0) is even
   // last one (9) is odd

   // attempt to do VBI
   // for this chip I think we need a seperate RISC program for VBI

   pRiscCode         = HwMem_GetUserPointer(&m_RiscDMAMem);
   pRiscBasePhysical = HwMem_TranslateToPhysical(&m_RiscDMAMem, pRiscCode, RISC_CODE_LENGTH, NULL);

   for (nField = 0; nField < VBI_FIELD_CAPTURE_COUNT; nField++)
   {
      // First we sync onto either the odd or even field
      if (nField & 1)
         Instruction = RISC_RESYNC_EVEN;
      else
         Instruction = RISC_RESYNC_ODD;

      // maintain counter that we use to tell us where we are in the RISC code
      if (nField == 0)
         Instruction |= RISC_CNT_RESET;
      else
         Instruction |= RISC_CNT_INC;

      *(pRiscCode++) = Instruction;

      // skip the first line so that line numbers tie up with those for the Bt848
      *(pRiscCode++) = RISC_SKIP | RISC_SOL | RISC_EOL | VBI_SPL;

      pVbiUser = HwMem_GetUserPointer(&m_VBIDMAMem[nField / 2]);
      if ((nField & 1) == 1)
      {
         pVbiUser += VBI_LINES_PER_FIELD * VBI_LINE_SIZE;
      }

      for (nLine = 0; nLine < VBI_LINES_PER_FIELD; nLine++)
      {
         pVbiPhysical = HwMem_TranslateToPhysical(&m_VBIDMAMem[nField / 2], pVbiUser, VBI_SPL, &GotBytesPerLine);
         if ((pVbiPhysical == 0) || (VBI_SPL > GotBytesPerLine))
         {
            SHOULD_NOT_BE_REACHED;
            return FALSE;
         }
         *(pRiscCode++) = RISC_WRITE | RISC_SOL | RISC_EOL | VBI_SPL;
         *(pRiscCode++) = pVbiPhysical;
         pVbiUser += VBI_LINE_SIZE;
      }
   }

   // close the loop: jump back to start
   *(pRiscCode++) = RISC_JUMP;
   *(pRiscCode++) = pRiscBasePhysical;

   // check that we don't write RISC code beyond the allocated memory
   assert(RISC_CODE_LENGTH >= ((long)pRiscCode - (long)HwMem_GetUserPointer(&m_RiscDMAMem)));

   WriteDword(SRAM_CMDS_24 + 0x00, pRiscBasePhysical);

   // mark all addresses as PCI memory resident by clearing bit 31 (ISRP)
   AndDataDword(SRAM_CMDS_24 + 0x10, 0x7fffffff);

   return TRUE;
}

// ---------------------------------------------------------------------------
// Query index of the frame currently captured by the hardware
//
static uint Cx2388x_GetRiscPos( void )
{
   uint CurrentFrame;
   uint CurrentPos;
   
   CurrentPos = ReadDword(CX2388X_VBI_GP_CNT);
   if (CurrentPos < VBI_FIELD_CAPTURE_COUNT)
   {
      // the current position lies in the field which is currently being filled
      // calculate the index of the previous (i.e. completed) frame
      if (CurrentPos < 2)
         CurrentFrame = ((CurrentPos + VBI_FIELD_CAPTURE_COUNT) - 2) / 2;
      else
         CurrentFrame = (CurrentPos - 2) / 2;
   }
   else
      CurrentFrame = VBI_FRAME_CAPTURE_COUNT;

   return CurrentFrame;
}

// ---------------------------------------------------------------------------
// VBI Driver Thread
//
static DWORD WINAPI Cx2388x_VbiThread( LPVOID dummy )
{
   BYTE *pVBI;
   uint  OldFrame;
   uint  CurFrame;
   uint  row;

   dprintf0("Cx2388x-VbiThread: VBI thread started\n");

   //SetAcqPriority(GetCurrentThread(), btCfg.threadPrio);
   VbiDecodeSetSamplingRate(27000000L, 7);

   Cx2388x_StartCapture();

   OldFrame = Cx2388x_GetRiscPos();

   for (;;)
   {
      if (StopVbiThread == TRUE)
         break;

      if (pVbiBuf != NULL)
      {
         CurFrame = Cx2388x_GetRiscPos();
         if ((CurFrame != OldFrame) && (CurFrame < VBI_FRAME_CAPTURE_COUNT))
         {
            if (ReadDword(SRAM_CMDS_24 + 0x00) != pRiscBasePhysical)
            {  // another TV app was started -> abort acq immediately
               debug2("Cx2388x-VbiThread: RISC start addr changed: %lX != %lX", ReadDword(SRAM_CMDS_24 + 0x00), pRiscBasePhysical);
               CardConflictDetected = TRUE;
               pVbiBuf->hasFailed = TRUE;
               break;
            }
            //dprintf2("Cx2388x-VbiThread: processing frame #%d (prev. %d)\n", CurFrame, OldFrame);

            do
            {
               OldFrame = (OldFrame + 1) % VBI_FRAME_CAPTURE_COUNT;
               pVBI = HwMem_GetUserPointer(&m_VBIDMAMem[OldFrame]);

               // notify teletext decoder about start of new frame; since we don't have a
               // frame counter (no frame interrupt) we always pass 0 as frame sequence number
               if ( VbiDecodeStartNewFrame(0) )
               {
                  for (row = 0; row < VBI_LINES_PER_FRAME; row++, pVBI += VBI_LINE_SIZE)
                  {
                     VbiDecodeLine(pVBI, row, TRUE);
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
   Cx2388x_StopCapture();

   dprintf0("Cx2388x-VbiThread: VBI thread stopped\n");

   return 0;  // dummy
}


// ---------------------------------------------------------------------------
//                    I N T E R F A C E   F U N C T I O N S
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Check if the current source has a video signal
//
static bool Cx2388x_IsVideoPresent( void )
{
   DWORD dwval = ReadDword(CX2388X_DEVICE_STATUS);

   return ((dwval & CX2388X_DEVICE_STATUS_HLOCK) == CX2388X_DEVICE_STATUS_HLOCK);
}

// ---------------------------------------------------------------------------
// Stop acquisition
// - disable capturing
// - stop the VBI thread
//
static void Cx2388x_StopAcqThread( void )
{
#ifndef DISABLE_PCI_REGISTER_DUMP
   Cx2388x_DumpRegisters("CX23881 card close");
#endif

   if (vbiThreadHandle != NULL)
   {
      StopVbiThread = TRUE;
      WaitForSingleObject(vbiThreadHandle, 200);
      CloseHandle(vbiThreadHandle);
      vbiThreadHandle = NULL;
   }

   Sleep(100);

   // the VBI thread should have stopped capturing
   // just to be safe, do it again here
   Cx2388x_StopCapture();
}

// ---------------------------------------------------------------------------
// Start acquisition
// - start the VBI thread
// - start capturing
//
static bool Cx2388x_StartAcqThread( void )
{
   DWORD LinkThreadID;
   bool result;

   StopVbiThread = FALSE;

   vbiThreadHandle = CreateThread(NULL, 0, Cx2388x_VbiThread, NULL, 0, &LinkThreadID);

   result = (vbiThreadHandle != NULL);
   ifdebug1(!result, "Cx2388x-StartAcqThread: failed to create VBI thread: %ld", GetLastError());

   return result;
}

// ---------------------------------------------------------------------------
// Set parameters
//
static bool Cx2388x_Configure( uint threadPrio, uint pllType )
{
   dprintf2("Cx2388x-Configure: prio=%d, pll=%d\n", threadPrio, pllType);

   if (vbiThreadHandle != NULL)
   {
      if (SetThreadPriority(vbiThreadHandle, threadPrio) == 0)
         debug2("Cx2388x-Configure: SetThreadPriority(%d) returned %ld", threadPrio, GetLastError());
   }

   return TRUE;
}

// ---------------------------------------------------------------------------
// Free I/O resources and close the driver device
//
static void Cx2388x_Close( void )
{
   dprintf0("Cx2388x-Close: closing card\n");

   // sofware reset

   // reset PCI registers to original state
   if (CardConflictDetected == FALSE)
   {
      HwPci_RestoreState();
      Cx2388x_ManageMyState();
   }

   Cx2388x_FreeDmaMemory();
}

// ---------------------------------------------------------------------------
// Allocate resources and initialize PCI registers
//
static bool Cx2388x_Open( TVCARD * pTvCard )
{
   DWORD RiscEn;
   DWORD DmaCtl;
   bool result = FALSE;

   m_I2CInitialized = FALSE;
   CardConflictDetected = FALSE;

   if (pTvCard != NULL)
   {
#ifndef DISABLE_PCI_REGISTER_DUMP
      Cx2388x_DumpRegisters("CX23881 card open");
#endif
      RiscEn = ReadDword(CX2388X_DEV_CNTRL2);
      DmaCtl = ReadDword(CX2388X_VID_DMA_CNTRL);
      if ((RiscEn & (1 << 5)) && (DmaCtl & 0xff))
      {
         debug2("Cx2388x-Open: capturing already enabled (RISC=%lX, DMA=%lX)", RiscEn, DmaCtl);

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
         // Allocate memory for DMA
         result = Cx2388x_AllocDmaMemory();
         if (result)
         {
            dprintf4("Cx2388x-Open: initializing card type %d (vendor=%lX dev=%lX sub=%lX)\n", pTvCard->params.cardId, pTvCard->params.VendorId, pTvCard->params.DeviceId, pTvCard->params.SubSystemId);

            Cx2388x_StopCapture();

            // Save state of PCI registers
            HwPci_InitStateBuf();
            Cx2388x_ManageMyState();

            // Initialize PCI registers
            Cx2388x_ResetHardware(pTvCard->params.BusNumber, pTvCard->params.SlotNumber);
            Cx2388x_VideoInit();

            // Set up DMA in the card
            Cx2388x_CreateRiscCode();

            // XXX TODO should be called upon input or format changes
            Cx2388x_SetGeoSize(pTvCard, 0, VIDEO_MODE_PAL);
         }
      }
   }
   else
      fatal0("Cx2388x-Open: illegal NULL ptr param");

   return result;
}

// ---------------------------------------------------------------------------
// Fill structure with interface functions
//
static const I2C_LINE_BUS Cx2388x_I2cLineBus =
{
   Cx2388x_I2cSleep,
   Cx2388x_SetSDA,
   Cx2388x_SetSCL,
   Cx2388x_GetSDA,
   Cx2388x_GetSCL,
};

static const TVCARD_CTL Cx2388x_CardCtl =
{
   Cx2388x_IsVideoPresent,
   Cx2388x_StartAcqThread,
   Cx2388x_StopAcqThread,
   Cx2388x_Configure,
   Cx2388x_Close,
   Cx2388x_Open,
};


void Cx2388x_GetInterface( TVCARD * pTvCard )
{
   if (pTvCard != NULL)
   {
      memset(pTvCard, 0, sizeof(*pTvCard));

      pTvCard->ctl        = &Cx2388x_CardCtl;
      pTvCard->i2cLineBus = &Cx2388x_I2cLineBus;

      Bt8x8I2c_GetInterface(pTvCard);
      Cx2388xTyp_GetInterface(pTvCard);
   }
   else
      fatal0("Cx2388x-GetInterface: NULL ptr param");
}

