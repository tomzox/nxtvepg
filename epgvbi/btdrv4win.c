/*
 *  Win32 VBI capture driver management
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
 *    This module manages M$ Windows drivers for all supported TV cards.
 *    It provides an abstraction layer with higher-level functions,
 *    e.g. to start/stop acquisition or change the channel.
 *
 *    It's based on DSdrv by Mathias Ellinger and John Adcock: a free open-source
 *    driver for PCI cards. The driver offers generic I/O functions to directly
 *    access the hardware (memory map PCI registers, allocate memory for DMA
 *    etc.)  Much of the code to control the specific cards was also taken from
 *    DScaler, although a large part (if not the most) originally came from the
 *    Linux bttv and saa7134 drivers (see also copyrights below).  Linux bttv
 *    was originally ported to Windows by "Espresso".
 *
 *
 *  Authors:
 *
 *    WinDriver adaption (from MultiDec)
 *      Copyright (C) 2000 Espresso (echter_espresso@hotmail.com)
 *
 *    WinDriver replaced with DSdrv Bt8x8 code (DScaler driver)
 *      March 2002 by E-Nek (e-nek@netcourrier.com)
 *
 *    WDM support
 *      February 2004 by Gérard Chevalier (gd_chevalier@hotmail.com)
 *
 *    The rest
 *      Tom Zoerner
 *
 *
 *  $Id: btdrv4win.c,v 1.46.1.14 2005/01/09 18:30:38 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
//#define DPRINTF_OFF
//#define WITHOUT_DSDRV

#include <windows.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"

#include "wdmdrv/wdmdrv.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/zvbidecoder.h"
#include "epgvbi/tvchan.h"

#include "dsdrv/dsdrvlib.h"
#include "dsdrv/tvcard.h"
#include "dsdrv/bt8x8.h"
#include "dsdrv/saa7134.h"
#include "dsdrv/cx2388x.h"
#include "dsdrv/wintuner.h"


// ----------------------------------------------------------------------------
// State of PCI cards

typedef struct
{
   WORD   VendorId;
   WORD   DeviceId;
   char * szName;
} TCaptureChip;

#define PCI_ID_BROOKTREE  0x109e
#define PCI_ID_PHILIPS    0x1131
#define PCI_ID_CONEXANT   0x14F1
#define PCI_ID_PSEUDO_WDM (('W'<<24)|('D'<<16)|('M'<<8))

static const TCaptureChip CaptureChips[] =
{
   { PCI_ID_BROOKTREE, 0x036e, "Brooktree Bt878"  },
   { PCI_ID_BROOKTREE, 0x036f, "Brooktree Bt878A" },
   { PCI_ID_BROOKTREE, 0x0350, "Brooktree Bt848"  },
   { PCI_ID_BROOKTREE, 0x0351, "Brooktree Bt849"  },
   { PCI_ID_PHILIPS,   0x7134, "Philips SAA7134" },
   { PCI_ID_PHILIPS,   0x7133, "Philips SAA7133" },
   { PCI_ID_PHILIPS,   0x7130, "Philips SAA7130" },
   //{ PCI_ID_PHILIPS,  0x7135, "Philips SAA7135" },
   { PCI_ID_CONEXANT,  0x8800, "Conexant CX23881 (Bt881)" }
};
#define CAPTURE_CHIP_COUNT (sizeof(CaptureChips) / sizeof(CaptureChips[0]))

typedef struct
{
   uint  chipIdx;
   uint  chipCardIdx;
   WORD  VendorId;
   WORD  DeviceId;
   DWORD dwSubSystemID;
   DWORD dwBusNumber;
   DWORD dwSlotNumber;
} PCI_SOURCE;

// ----------------------------------------------------------------------------
// State of WDM sources

#define WDM_DRV_DLL_PATH "wdmdrv\\VBIAcqWDMDrv.dll"

typedef struct
{
   int  devIdx;
} WDM_SOURCE;

static WDMDRV_CTL  WdmDrv;        // pointers to wdmdrv API functions
static HINSTANCE   WdmDrvHandle;  // handle to wdmdrv interface DLL
static vbi_raw_decoder WdmZvbiDec[2];
static LONGLONG    WdmLastTimestamp;
static long        WdmLastFrameNo;

// ----------------------------------------------------------------------------
// Shared variables between PCI and WDM sources

typedef enum
{
   BTDRV_SOURCE_WDM,
   BTDRV_SOURCE_PCI,
   BTDRV_SOURCE_COUNT
} BTDRV_SOURCE_TYPE;

typedef struct
{
   BTDRV_SOURCE_TYPE    type;
   union
   {
      PCI_SOURCE        pci;
      WDM_SOURCE        wdm;
   } u;
} BTDRV_SOURCE;

#define MAX_CARD_COUNT  (2 * 4)
#define CARD_COUNT_UNINITIALIZED  (MAX_CARD_COUNT + 1)
static BTDRV_SOURCE   btCardList[MAX_CARD_COUNT];
static uint           btCardCount;

#define INVALID_INPUT_SOURCE  0xff

static struct
{
   uint        sourceIdx;
   uint        cardId;
   uint        tunerType;
   uint        pllType;
   uint        wdmStop;
   uint        threadPrio;
   uint        inputSrc;
   uint        tunerFreq;
   uint        tunerNorm;
} btCfg;

static TVCARD cardif;
static BOOL pciDrvLoaded;
static BOOL wdmDrvLoaded;
static BOOL shmSlaveMode = FALSE;

// ----------------------------------------------------------------------------

volatile EPGACQ_BUF * pVbiBuf;
static EPGACQ_BUF vbiBuf;

static bool BtDriver_CountSources( bool showDrvErr );
static BOOL BtDriver_PciCardOpen( uint sourceIdx );

// ----------------------------------------------------------------------------
// Load WDM interface library dynamically
//
static bool BtDriver_WdmDllLoad( void )
{
   bool result = FALSE;

   WdmDrvHandle = LoadLibrary(WDM_DRV_DLL_PATH);
   if (WdmDrvHandle != NULL)
   {
      dprintf0("BtDriver-WdmDllLoad: LoadLibrary OK\n");

      WdmDrv.InitDrv = (void *) GetProcAddress(WdmDrvHandle, "InitDrv");
      WdmDrv.FreeDrv = (void *) GetProcAddress(WdmDrvHandle, "FreeDrv");
      WdmDrv.StartAcq = (void *) GetProcAddress(WdmDrvHandle, "StartAcq");
      WdmDrv.StopAcq = (void *) GetProcAddress(WdmDrvHandle, "StopAcq");
      WdmDrv.SetChannel = (void *) GetProcAddress(WdmDrvHandle, "SetChannel");
      WdmDrv.GetSignalStatus = (void *) GetProcAddress(WdmDrvHandle, "GetSignalStatus");
      WdmDrv.EnumDevices = (void *) GetProcAddress(WdmDrvHandle, "EnumDevices");
      WdmDrv.GetDeviceName = (void *) GetProcAddress(WdmDrvHandle, "GetDeviceName");
      WdmDrv.SelectDevice = (void *) GetProcAddress(WdmDrvHandle, "SelectDevice");
      WdmDrv.GetVBISettings = (void *) GetProcAddress(WdmDrvHandle, "GetVBISettings");
      WdmDrv.GetVideoInputName = (void *) GetProcAddress(WdmDrvHandle, "GetVideoInputName");
      WdmDrv.SelectVideoInput = (void *) GetProcAddress(WdmDrvHandle, "SelectVideoInput");
      WdmDrv.IsInputATuner = (void *) GetProcAddress(WdmDrvHandle, "IsInputATuner");
      WdmDrv.SetWSTPacketCallBack = (void *) GetProcAddress(WdmDrvHandle, "SetWSTPacketCallBack");

      if ( (WdmDrv.InitDrv != NULL) &&
           (WdmDrv.FreeDrv != NULL) &&
           (WdmDrv.StartAcq != NULL) &&
           (WdmDrv.StopAcq != NULL) &&
           (WdmDrv.SetChannel != NULL) &&
           (WdmDrv.GetSignalStatus != NULL) &&
           (WdmDrv.EnumDevices != NULL) &&
           (WdmDrv.GetDeviceName != NULL) &&
           (WdmDrv.SelectDevice != NULL) &&
           (WdmDrv.GetVBISettings != NULL) &&
           (WdmDrv.GetVideoInputName != NULL) &&
           (WdmDrv.SelectVideoInput != NULL) &&
           (WdmDrv.IsInputATuner != NULL) &&
           (WdmDrv.SetWSTPacketCallBack != NULL) )
      {
         dprintf0("BtDriver-WdmDllLoad: Got entry points OK\n");

         if ( WdmDrv.InitDrv() == S_OK )
         {
            dprintf0("BtDriver-WdmDllLoad: OK\n");
            result = TRUE;
         }
         else
            debug0("BtDriver-WdmDllLoad: failed to initialise WDM interface library");
      }
      else
         debug0("BtDriver-WdmDllLoad: find all WDM interface functions");

      if (result == FALSE)
      {
         FreeLibrary(WdmDrvHandle);
         WdmDrvHandle = NULL;
      }
   }
   else
       debug0("BtDriver-WdmDllLoad: Failed to load DLL" WDM_DRV_DLL_PATH);

   return result;
}

// ----------------------------------------------------------------------------
// CallBack function called by the WDM VBI Driver
// - this is just a wrapper to the ttx decoder
//
static BOOL __stdcall BtDriver_WdmVbiCallback( BYTE * pFieldBuffer, LONGLONG timestamp )
{
   BYTE * pLineData;
   uint line_idx;

   //debug0("VBI CB !");

   // convert timestamp into frame sequence number
   // XXX Tom : I changed 40 ms to 20 ms in the test and made it in integer arithmetic
   // Debug trace shows timestamp - WdmLastTimestamp ~ 200000
   // OLD : if (timestamp - WdmLastTimestamp > (1.5 * 1 / 25))
   if (timestamp - WdmLastTimestamp > (1.5 * 200000))
      WdmLastFrameNo += 2;
   else
      WdmLastFrameNo += 1;
   WdmLastTimestamp = timestamp;

   if (pVbiBuf->slicerType != VBI_SLICER_ZVBI)
   {
      if (VbiDecodeStartNewFrame(WdmLastFrameNo))
      {
         pLineData = pFieldBuffer;

         // decode all lines in the field
         for (line_idx = 0; line_idx <= WdmZvbiDec[0].count[0]; line_idx++)
         {
            //debug1("line: %d\n", line_idx);

            VbiDecodeLine(pLineData, line_idx, TRUE);

            pLineData += WdmZvbiDec[0].bytes_per_line;
         }
      }
   }
   else
   {
      static int field_idx = 0;  // XXX FIXME: must be passed as parameter to the callback
      field_idx = (field_idx + 1) & 1; // XXX FIXME cntd.

      ZvbiSliceAndProcess(&WdmZvbiDec[field_idx], pFieldBuffer, WdmLastFrameNo);
   }

   return TRUE;
}

// ----------------------------------------------------------------------------
// Slicer setup according to VBI format delivered by WDM source
//
static void BtDriver_WdmInitSlicer( void )
{
   KS_VBIINFOHEADER VBIParameters;
   HRESULT hres;
   uint services[2];

   hres = WdmDrv.GetVBISettings(&VBIParameters);
   if ( WDM_DRV_OK(hres) )
   {
      VbiDecodeSetSamplingRate(VBIParameters.SamplingFrequency, VBIParameters.StartLine);

      memset(&WdmZvbiDec[0], 0, sizeof(WdmZvbiDec[0]));
      WdmZvbiDec[0].sampling_rate    = VBIParameters.SamplingFrequency;
      WdmZvbiDec[0].bytes_per_line   = VBIParameters.StrideInBytes;
      WdmZvbiDec[0].offset           = (double)VBIParameters.ActualLineStartTime * 1E-8
                                          / VBIParameters.SamplingFrequency;
      WdmZvbiDec[0].interlaced       = FALSE;
      WdmZvbiDec[0].synchronous      = TRUE;
      WdmZvbiDec[0].sampling_format  = VBI_PIXFMT_YUV420;
      WdmZvbiDec[0].scanning         = 625;
      memcpy(&WdmZvbiDec[1], &WdmZvbiDec[0], sizeof(WdmZvbiDec[0]));

      WdmZvbiDec[0].start[0]         = VBIParameters.StartLine;
      WdmZvbiDec[0].count[0]         = VBIParameters.EndLine - VBIParameters.StartLine + 1;
      WdmZvbiDec[0].start[1]         = -1;
      WdmZvbiDec[0].count[1]         = 0;

      WdmZvbiDec[1].start[0]         = -1;
      WdmZvbiDec[1].count[0]         = 0;
      WdmZvbiDec[1].start[1]         = VBIParameters.StartLine + 312;
      WdmZvbiDec[1].count[1]         = VBIParameters.EndLine - VBIParameters.StartLine + 1;

      services[0] = vbi_raw_decoder_add_services(&WdmZvbiDec[0], VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS, 0);
      services[1] = vbi_raw_decoder_add_services(&WdmZvbiDec[1], VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS, 0);
      if ( ((services[0] & VBI_SLICED_TELETEXT_B) == 0) ||
           ((services[1] & VBI_SLICED_TELETEXT_B) == 0) )
      {
         MessageBox(NULL, "The selected capture source is appearently not able "
                          "to capture teletext. Will try anyways, but it might not work.",
                          "Nextview EPG driver warning",
                          MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      }

      WdmLastTimestamp = 0;
      WdmLastFrameNo   = 0;
   }
   else
      debug1("BtDriver-WdmInitSlicer: failed to retrieve VBI format: %ld", (long)hres);
}

// ----------------------------------------------------------------------------
// Map internal norm to WDM
//
static AnalogVideoStandard BtDriver_WdmMapNorm( int norm )
{
   AnalogVideoStandard WdmNorm;

   // Adapt norm value for WDM driver (uses DirectShow convention)
   switch (norm)
   {
      case VIDEO_MODE_PAL :
         WdmNorm = AnalogVideo_PAL_B;
         break;
      case VIDEO_MODE_SECAM :
         WdmNorm = AnalogVideo_SECAM_L;
         break;
      case VIDEO_MODE_NTSC :
         WdmNorm = AnalogVideo_NTSC_M;
         break;
      default:
         debug1("BtDriver-TuneChannel: unsupported TV norm %d", norm);
         WdmNorm = AnalogVideo_SECAM_L;
         break;
   }
   return WdmNorm;
}

// ----------------------------------------------------------------------------
// Helper function to set user-configured priority in IRQ and VBI threads
//
static int BtDriver_GetAcqPriority( int level )
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

   return prio;
}

// ---------------------------------------------------------------------------
// Get interface functions for a TV card
//
static bool BtDriver_PciCardGetInterface( TVCARD * pTvCard, DWORD VendorId, uint sourceIdx )
{
   bool result = TRUE;

   memset(pTvCard, 0, sizeof(* pTvCard));

   switch (VendorId)
   {
      case PCI_ID_BROOKTREE:
         Bt8x8_GetInterface(pTvCard);
         break;

      case PCI_ID_PHILIPS:
         SAA7134_GetInterface(pTvCard);
         break;

      case PCI_ID_CONEXANT:
         Cx2388x_GetInterface(pTvCard);
         break;

      default:
         result = FALSE;
         break;
   }

   // copy card parameters into the struct
   if ((btCardCount != CARD_COUNT_UNINITIALIZED) && (sourceIdx < btCardCount))
   {
      assert(btCardList[sourceIdx].type == BTDRV_SOURCE_PCI);
      assert(btCardList[sourceIdx].u.pci.VendorId == VendorId);

      pTvCard->params.BusNumber   = btCardList[sourceIdx].u.pci.dwBusNumber;
      pTvCard->params.SlotNumber  = btCardList[sourceIdx].u.pci.dwSlotNumber;
      pTvCard->params.VendorId    = btCardList[sourceIdx].u.pci.VendorId;
      pTvCard->params.DeviceId    = btCardList[sourceIdx].u.pci.DeviceId;
      pTvCard->params.SubSystemId = btCardList[sourceIdx].u.pci.dwSubSystemID;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Select the video input source
// - which input is tuner and which composite etc. is completely up to the
//   card manufacturer, but it seems that almost all use the 2,3,1,1 muxing
// - returns TRUE in *pIsTuner if the selected source is the TV tuner
//
static bool BtDriver_SetInputSource( uint inputIdx, uint norm, bool * pIsTuner )
{
   bool isTuner = FALSE;
   bool result = FALSE;
   HRESULT hres;

   // remember the input source for later
   btCfg.inputSrc = inputIdx;

   if (shmSlaveMode == FALSE)
   {
      if (pciDrvLoaded)
      {
#ifndef WITHOUT_DSDRV
         // XXX TODO norm switch
         result = cardif.cfg->SetVideoSource(&cardif, inputIdx);

         isTuner = cardif.cfg->IsInputATuner(&cardif, inputIdx);
#else
         result = isTuner = TRUE;
#endif
      }
      else if (wdmDrvLoaded)
      {
         // XXX TODO(TZ) norm
         hres = WdmDrv.SelectVideoInput(inputIdx);
         if ( WDM_DRV_OK(hres) )
         {
            // No need to check for failure when calling IsInputATuner as we just called
            // SelectVideoInput successfully
            hres = WdmDrv.IsInputATuner(inputIdx, &isTuner);
            result  = TRUE;
         }
         else
            debug2("BtDriver-SetInputSource: failed for input #%d: %ld", inputIdx, hres);
      }
   }
   else
   {  // slave mode -> set param in shared memory
      result = WintvSharedMem_SetInputSrc(inputIdx);
      isTuner = TRUE;  // XXX TODO(TZ)
   }

   if (pIsTuner != NULL)
      *pIsTuner = isTuner;

   return result;
}

// ---------------------------------------------------------------------------
// Return name for given input source index
// - has to be called repeatedly with incremented indices until NULL is returned
//
const char * BtDriver_GetInputName( uint sourceIdx, uint cardType, uint inputIdx )
{
   const char * pName = NULL;
   TVCARD tmpCardIf;
   uint   chipIdx;
   HRESULT hres;

   if ((btCardCount != CARD_COUNT_UNINITIALIZED) && (sourceIdx < btCardCount))
   {
      if (btCardList[sourceIdx].type == BTDRV_SOURCE_PCI)
      {
         chipIdx = btCardList[sourceIdx].u.pci.chipIdx;

         if (BtDriver_PciCardGetInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, CARD_COUNT_UNINITIALIZED) )
         {
            tmpCardIf.params.cardId = cardType;

            if (inputIdx < tmpCardIf.cfg->GetNumInputs(&tmpCardIf))
               pName = tmpCardIf.cfg->GetInputName(&tmpCardIf, inputIdx);
            else
               pName = NULL;
         }
      }
      else if (btCardList[sourceIdx].type == BTDRV_SOURCE_WDM)
      {
         if (WdmDrvHandle != NULL)
         {
            // XXX TODO: pass source index: btCardList[sourceIdx].u.wdm.devIdx
            hres = WdmDrv.GetVideoInputName(inputIdx, (char **)&pName);
            if ( WDM_DRV_OK(hres) == FALSE )
            {
               debug4("BtDriver-GetInputName: failed for input #%d on source #%d (WDM dev #%d): %ld", inputIdx, sourceIdx, btCardList[sourceIdx].u.wdm.devIdx, hres);
               pName = NULL;
            }
         }
         else
            fatal0("BtDriver-GetInputName: WDM interface DLL not attached");
      }
   }
   dprintf4("BtDriver-GetInputName: sourceIdx=%d, cardType=%d, inputIdx=%d: result=%s\n", sourceIdx, cardType, inputIdx, ((pName != NULL) ? pName : "NULL"));

   return pName;
}

// ---------------------------------------------------------------------------
// Auto-detect card type and return parameters from config table
// - card type may also be set manually, in which case only params are returned
//
bool BtDriver_QueryCardParams( uint sourceIdx, sint * pCardType, sint * pTunerType, sint * pPllType )
{
   TVCARD tmpCardIf;
   uint   chipIdx;
   bool   drvWasLoaded;
   uint   loadError;
   bool   result = FALSE;

   if ((pCardType != NULL) && (pTunerType != NULL) && (pPllType != NULL))
   {
      if ((btCardCount != CARD_COUNT_UNINITIALIZED) && (sourceIdx < btCardCount))
      {
         if (shmSlaveMode)
         {  // error
            debug0("BtDriver-QueryCardParams: driver is in slave mode");
            MessageBox(NULL, "Cannot query the TV card while connected to a TV application.\nTerminated the TV application and try again.", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         else if (pciDrvLoaded && (btCfg.sourceIdx != sourceIdx))
         {  // error
            debug2("BtDriver-QueryCardParams: acq running for different card %d instead req. %d", btCfg.sourceIdx, sourceIdx);
            MessageBox(NULL, "Acquisition is running for a different TV card.\nStop acquisition and try again.", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         else if (btCardList[sourceIdx].type != BTDRV_SOURCE_PCI)
         {  // do nothing: should not be called for WDM sources
            debug2("BtDriver-QueryCardParams: called for non-PCI source %d, card %d", btCardList[sourceIdx].type, sourceIdx);
         }
         else
         {
            drvWasLoaded = pciDrvLoaded;
            if (pciDrvLoaded == FALSE)
            {
               shmSlaveMode = (WintvSharedMem_ReqTvCardIdx(sourceIdx) == FALSE);
               if (shmSlaveMode == FALSE)
               {
                  loadError = DsDrvLoad();
                  if (loadError == HWDRV_LOAD_SUCCESS)
                  {
                     pciDrvLoaded = TRUE;

                     // scan the PCI bus for known cards
                     BtDriver_CountSources(TRUE);

                     if (BtDriver_PciCardOpen(sourceIdx) == FALSE)
                     {
                        DsDrvUnload();
                        pciDrvLoaded = FALSE;
                     }
                  }
                  else
                  {
                     MessageBox(NULL, DsDrvGetErrorMsg(loadError), "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
                  }
               }
            }

            if (pciDrvLoaded)
            {
               chipIdx = btCardList[sourceIdx].u.pci.chipIdx;
               if ( BtDriver_PciCardGetInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, sourceIdx) )
               {
#ifndef WITHOUT_DSDRV
                  if (*pCardType <= 0)
                     *pCardType  = tmpCardIf.cfg->AutoDetectCardType(&tmpCardIf);

                  if (*pCardType > 0)
                  {
                     *pTunerType = tmpCardIf.cfg->AutoDetectTuner(&tmpCardIf, *pCardType);
                     *pPllType   = tmpCardIf.cfg->GetPllType(&tmpCardIf, *pCardType);
                  }
                  else
                     *pTunerType = *pPllType = 0;
#endif
                  result = TRUE;
               }

               if (drvWasLoaded == FALSE)
               {
                  DsDrvUnload();
                  pciDrvLoaded = FALSE;
               }
            }

            if (drvWasLoaded == FALSE)
            {
               WintvSharedMem_FreeTvCard();
               shmSlaveMode = FALSE;
            }
         }
      }
      else
         debug2("BtDriver-QueryCardParams: PCI bus not scanned or invalid card idx %d >= count %d", sourceIdx, btCardCount);
   }
   else
      fatal3("BtDriver-QueryCardParams: illegal NULL ptr params %lx,%lx,%lx", (long)pCardType, (long)pTunerType, (long)pPllType);

   return result;
}

// ---------------------------------------------------------------------------
// Return name from card list for a given chip
// - only used for PCI sources to select card manufacturer and model
//
const char * BtDriver_GetCardNameFromList( uint sourceIdx, uint listIdx )
{
   const char * pName = NULL;
   TVCARD tmpCardIf;
   uint chipIdx;

   if ((btCardCount != CARD_COUNT_UNINITIALIZED) && (sourceIdx < btCardCount))
   {
      if (btCardList[sourceIdx].type == BTDRV_SOURCE_PCI)
      {
         chipIdx = btCardList[sourceIdx].u.pci.chipIdx;

         if ( BtDriver_PciCardGetInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, CARD_COUNT_UNINITIALIZED) )
         {
            pName = tmpCardIf.cfg->GetCardName(&tmpCardIf, listIdx);
         }
      }
      else if (btCardList[sourceIdx].type == BTDRV_SOURCE_WDM)
      {
         fatal0("BtDriver-GetCardNameFromList: called for WDM source");
      }
   }
   return pName;
}

// ---------------------------------------------------------------------------
// Return name & chip type for given TV card
// - if the driver was never loaded before the PCI bus is scanned now;
//   no error message displayed if the driver fails to load, but result set to FALSE
// - end of enumeration is indicated by a FALSE result or NULL name pointer
//
bool BtDriver_EnumCards( uint sourceIdx, uint cardType, uint * pChipType, const char ** pName, bool showDrvErr )
{
   static char cardName[50];
   TVCARD tmpCardIf;
   uint   chipIdx;
   uint   chipType;
   HRESULT hres;
   bool   result = FALSE;

   if ((pChipType != NULL) && (pName != NULL))
   {
      // note: only try to load driver for the first query
      if ( (sourceIdx == 0) && (btCardCount == CARD_COUNT_UNINITIALIZED) )
      {
         assert(pciDrvLoaded == FALSE);
         BtDriver_CountSources(showDrvErr);
      }

      // XXX TODO(TZ) might distinguish PCI scan from WDM scan (one should not make the other fail)
      if (btCardCount != CARD_COUNT_UNINITIALIZED)
      {
         if (sourceIdx < btCardCount)
         {
            if (btCardList[sourceIdx].type == BTDRV_SOURCE_PCI)
            {
               chipIdx  = btCardList[sourceIdx].u.pci.chipIdx;
               chipType = (CaptureChips[chipIdx].VendorId << 16) | CaptureChips[chipIdx].DeviceId;

               if ((cardType != 0) && (chipType == *pChipType))
               {
                  if ( BtDriver_PciCardGetInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, CARD_COUNT_UNINITIALIZED) )
                  {
                     *pName = tmpCardIf.cfg->GetCardName(&tmpCardIf, cardType);
                  }
                  else
                     *pName = "unknown chip type";
               }
               else
               {
                  sprintf(cardName, "unknown %s card", CaptureChips[chipIdx].szName);
                  *pName = cardName;
               }
               *pChipType = chipType;
            }
            else if ( (btCardList[sourceIdx].type == BTDRV_SOURCE_WDM) && (WdmDrvHandle != NULL) )
            {
               hres = WdmDrv.GetDeviceName(btCardList[sourceIdx].u.wdm.devIdx, (char **) pName);
               if ( WDM_DRV_OK(hres) )
               {
                  *pChipType = PCI_ID_PSEUDO_WDM;
               }
               else
                  *pName = NULL;
            }
            else
               *pName = NULL;
         }
         else
         {  // end of enumeration
            *pName = NULL;
         }
         result = TRUE;
      }
      else
      {  // failed to load driver for PCI scan -> just return names for already known cards
         if (*pChipType == PCI_ID_PSEUDO_WDM)
         {
            *pName = "unknown WDM source";
         }
         else if (*pChipType != 0)
         {
            if ( BtDriver_PciCardGetInterface(&tmpCardIf, *pChipType >> 16, CARD_COUNT_UNINITIALIZED) )
            {
               *pName = tmpCardIf.cfg->GetCardName(&tmpCardIf, cardType);
            }
            else
               *pName = "unknown card";

            // return 0 as chip type to indicate driver failure
            *pChipType = 0;
         }
         else
         {  // end of enumeration
            if (shmSlaveMode && showDrvErr && (sourceIdx == 0))
            {
               MessageBox(NULL, "Cannot scan PCI bus for TV cards while connected to a TV application.\nTerminated the TV application and try again.", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            }
            *pName = NULL;
         }

         result = TRUE;
      }
   }
   else
      fatal2("BtDriver-EnumCards: illegal NULL ptr params %lx,%lx", (long)pChipType, (long)pName);

   return result;
}

// ---------------------------------------------------------------------------
// Check if the current source has a video signal
//
bool BtDriver_IsVideoPresent( void )
{
   HRESULT wdmResult;
   bool result;

   if (pciDrvLoaded)
   {
#ifndef WITHOUT_DSDRV
      result = cardif.ctl->IsVideoPresent();
#else
      result = FALSE;
#endif
   }
   else if (wdmDrvLoaded)
   {
      wdmResult = WdmDrv.GetSignalStatus();

      if (WDM_DRV_OK(wdmResult))
         result = ((wdmResult & WDM_DRV_WARNING_NOSIGNAL) == 0);
      else
         result = TRUE;
      // note: upon failure return "TRUE" to be fail-safe (i.e. don't skip the channel in scan)
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
// Generate a list of available cards
//
static void BtDriver_WdmSourceCount( void )
{
   uint  wdmIdx;
   uint  wdmCount;
   HRESULT hres;

   if (WdmDrvHandle != NULL)
   {
      dprintf0("BtDriver-WdmSourceCount: entry\n");

      hres = WdmDrv.EnumDevices();
      if (WDM_DRV_OK(hres))
      {
         wdmCount = (uint) hres;
         dprintf2("BtDriver-WdmSourceCount: adding %d WDM sources (after %d PCI)\n", wdmCount, btCardCount);

         for (wdmIdx = 0; (wdmIdx < wdmCount) && (btCardCount < MAX_CARD_COUNT); wdmIdx++)
         {
            btCardList[btCardCount].type              = BTDRV_SOURCE_WDM;
            btCardList[btCardCount].u.wdm.devIdx      = wdmIdx;
            btCardCount += 1;
         }
         ifdebug1(wdmIdx < wdmCount, "BtDriver-WdmSourceCount: source list overflow: %d WDM sources omitted", wdmCount - wdmIdx);
      }
      else
         debug1("BtDriver-WdmSourceCount: WDM interface DLL returned error %ld\n", hres);
   }
   else
      dprintf0("BtDriver-WdmSourceCount: WDM sources omitted, DLL not available\n");
}

// ---------------------------------------------------------------------------
// Scan PCI Bus for known TV card capture chips
//
static void BtDriver_PciCardCount( void )
{
   uint  chipIdx, cardIdx;

   btCardCount = 0;
   for (chipIdx=0; (chipIdx < CAPTURE_CHIP_COUNT) && (btCardCount < MAX_CARD_COUNT); chipIdx++)
   {
      cardIdx = 0;
      while (btCardCount < MAX_CARD_COUNT)
      {
         if (DoesThisPCICardExist(CaptureChips[chipIdx].VendorId, CaptureChips[chipIdx].DeviceId,
                                  cardIdx,
                                  &btCardList[btCardCount].u.pci.dwSubSystemID,
                                  &btCardList[btCardCount].u.pci.dwBusNumber,
                                  &btCardList[btCardCount].u.pci.dwSlotNumber) == ERROR_SUCCESS)
         {
            dprintf4("PCI scan: found capture chip %s, ID=%lx, bus=%ld, slot=%ld\n", CaptureChips[chipIdx].szName, btCardList[btCardCount].u.pci.dwSubSystemID, btCardList[btCardCount].u.pci.dwBusNumber, btCardList[btCardCount].u.pci.dwSlotNumber);

            btCardList[btCardCount].type              = BTDRV_SOURCE_PCI;
            btCardList[btCardCount].u.pci.VendorId    = CaptureChips[chipIdx].VendorId;
            btCardList[btCardCount].u.pci.DeviceId    = CaptureChips[chipIdx].DeviceId;
            btCardList[btCardCount].u.pci.chipIdx     = chipIdx;
            btCardList[btCardCount].u.pci.chipCardIdx = cardIdx;
            btCardCount += 1;
         }
         else
         {  // no more cards with this chip -> next chip (outer loop)
            break;
         }
         cardIdx += 1;
      }
   }
   dprintf1("BtDriver-PciCardCount: found %d PCI cards\n", btCardCount);
}

// ---------------------------------------------------------------------------
// Generate a list of available cards
//
static bool BtDriver_CountSources( bool showDrvErr )
{
   uint  loadError;
   bool  result = FALSE;

   // if the scan was already done skip it
   if (btCardCount == CARD_COUNT_UNINITIALIZED)
   {
      dprintf0("BtDriver-CountSources: start\n");

      // note: don't reserve the card from TV app since a PCI scan does not cause conflicts
      if (pciDrvLoaded == FALSE)
      {
         loadError = DsDrvLoad();
         if (loadError == HWDRV_LOAD_SUCCESS)
         {
            // scan the PCI bus for known cards, but don't open any
            BtDriver_PciCardCount();
            DsDrvUnload();

            // append WDM sources to the list (note: only if PCI scan succeeded)
            BtDriver_WdmSourceCount();
            result = TRUE;
         }
         else if (showDrvErr)
         {
            MessageBox(NULL, DsDrvGetErrorMsg(loadError), "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
      }
      else
      {  // dsdrv already loaded -> just do the scan
         BtDriver_PciCardCount();
         BtDriver_WdmSourceCount();
         result = TRUE;
      }
   }
   else
      result = TRUE;

   return result;
}

// ---------------------------------------------------------------------------
// Open the driver device and allocate I/O resources
//
static BOOL BtDriver_PciCardOpen( uint sourceIdx )
{
   char msgbuf[200];
   int  ret;
   int  chipIdx, chipCardIdx;
   BOOL supportsAcpi;
   BOOL result = FALSE;

   if (sourceIdx < btCardCount)
   {
      chipIdx     = btCardList[sourceIdx].u.pci.chipIdx;
      chipCardIdx = btCardList[sourceIdx].u.pci.chipCardIdx;

      if ( BtDriver_PciCardGetInterface(&cardif, CaptureChips[chipIdx].VendorId, sourceIdx) )
      {
#ifndef WITHOUT_DSDRV
         supportsAcpi = cardif.cfg->SupportsAcpi(&cardif);

         ret = pciGetHardwareResources(CaptureChips[chipIdx].VendorId,
                                       CaptureChips[chipIdx].DeviceId,
                                       chipCardIdx,
                                       supportsAcpi,
                                       cardif.ctl->ResetChip);

         if (ret == ERROR_SUCCESS)
         {
            dprintf2("BtDriver-PciCardOpen: %s driver loaded, card #%d opened\n", CaptureChips[chipIdx].szName, sourceIdx);
            result = TRUE;
         }
         else if (ret == 3)
         {  // card found, but failed to open -> abort
            sprintf(msgbuf, "Capture card #%d (with %s chip) cannot be locked!", sourceIdx, CaptureChips[chipIdx].szName);
            MessageBox(NULL, msgbuf, "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
#else
         result = TRUE;
#endif
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Shut down the driver and free all resources
// - after this function is called, other processes can use the card
//
static void BtDriver_DsDrvUnload( void )
{
   if (pciDrvLoaded)
   {
#ifndef WITHOUT_DSDRV
      cardif.ctl->Close(&cardif);
#endif
      DsDrvUnload();

      // clear driver interface functions (debugging only)
      memset(&cardif.ctl, 0, sizeof(cardif.ctl));
      memset(&cardif.cfg, 0, sizeof(cardif.cfg));

      pciDrvLoaded = FALSE;
   }
   dprintf0("BtDriver-DsDrvUnload: driver unloaded\n");
}

// ----------------------------------------------------------------------------
// Boot the dsdrv driver, allocate resources and initialize all subsystems
//
static bool BtDriver_DsDrvLoad( void )
{
   DWORD loadError;
   bool result = FALSE;

   assert((shmSlaveMode == FALSE) && (wdmDrvLoaded == FALSE));

   loadError = DsDrvLoad();
   if (loadError == HWDRV_LOAD_SUCCESS)
   {
      pciDrvLoaded = TRUE;
      BtDriver_CountSources(FALSE);

      if ( (btCfg.sourceIdx < btCardCount) &&
           (btCardList[btCfg.sourceIdx].type == BTDRV_SOURCE_PCI) )
      {
         if ( BtDriver_PciCardOpen(btCfg.sourceIdx) )
         {
            cardif.params.cardId = btCfg.cardId;

#ifndef WITHOUT_DSDRV
            if ( cardif.ctl->Open(&cardif, btCfg.wdmStop) )
            {
               if ( cardif.ctl->Configure(btCfg.threadPrio, btCfg.pllType) )
               {
                  // initialize tuner module for the configured tuner type
                  Tuner_Init(btCfg.tunerType, &cardif);

                  if (btCfg.tunerFreq != 0)
                  {  // if freq already set, apply it now
                     Tuner_SetFrequency(btCfg.tunerType, btCfg.tunerFreq, btCfg.tunerNorm);
                  }
                  if (btCfg.inputSrc != INVALID_INPUT_SOURCE)
                  {  // if source already set, apply it now
                     BtDriver_SetInputSource(btCfg.inputSrc, btCfg.tunerNorm, NULL);
                  }
                  result = TRUE;
               }
               else
               {  // failed to initialize the card (alloc memory for DMA)
                  BtDriver_DsDrvUnload();
               }
            }
#else  // WITHOUT_DSDRV
            result = TRUE;
#endif
         }
         // else: user message already generated by driver
      }
      else
         ifdebug2((btCfg.sourceIdx >= btCardCount), "BtDriver-DsDrvLoad: illegal card index %d >= count %d", btCfg.sourceIdx, btCardCount);

      if ((result == FALSE) && (pciDrvLoaded))
      {
         DsDrvUnload();
         pciDrvLoaded = FALSE;
      }
   }
   else
   {  // failed to load the driver
      MessageBox(NULL, DsDrvGetErrorMsg(loadError), "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Shut down the WDM source and free all resources
//
static void BtDriver_WdmUnload( void )
{
   if (wdmDrvLoaded)
   {
      WdmDrv.FreeDrv();

      ZvbiSliceAndProcess(NULL, NULL, 0);
      vbi_raw_decoder_destroy(&WdmZvbiDec[0]);
      vbi_raw_decoder_destroy(&WdmZvbiDec[1]);

      wdmDrvLoaded = FALSE;
   }
   dprintf0("BtDriver-WdmUnload: driver unloaded\n");
}

// ----------------------------------------------------------------------------
// Activate WDM source
//
static bool BtDriver_WdmLoad( void )
{
   HRESULT wdmResult;
   bool result = FALSE;

   assert((shmSlaveMode == FALSE) && (pciDrvLoaded == FALSE));
   dprintf0("BtDriver-WdmLoad: entry\n");

   // XXX possibly attempt to load DLL again to obtain error code?
   if (WdmDrvHandle != NULL)
   {
      // XXX TODO norm must be passed (for multi-norm tuners) xx gerard : given in SetChannel
      wdmResult = WdmDrv.SelectDevice(btCardList[btCfg.sourceIdx].u.wdm.devIdx, 0);
      if ( WDM_DRV_OK(wdmResult) )
      {
         if ((wdmResult & WDM_DRV_WARNING_INUSE) == 0)
         {
            BtDriver_WdmInitSlicer();

            if ( WdmDrv.SetWSTPacketCallBack(BtDriver_WdmVbiCallback, TRUE) == S_OK )
            {
               wdmDrvLoaded = TRUE;
               result = TRUE;
            }
         }
         else
            MessageBox(NULL, "Failed to start Nextview acquisition: Capture device already in use", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      }
      // else: user message already generated by open function (XXX really?)
      dprintf1("BtDriver-WdmLoad: WDM result %ld\n", (long)wdmResult);

      if (result == FALSE)
      {
         BtDriver_WdmUnload();
      }
   }
   else
   {  // failed to load the DLL
      // XXX TODO print human readable error code
      //MessageBox(NULL, GetDriverErrorMsg(loadError), "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      result = FALSE;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Check if the parameters are valid for the given source
// - this function is used to warn the user abour parameter mismatch after
//   hardware or driver configuration changes
//
bool BtDriver_CheckCardParams( int sourceIdx, int chipId,
                               int cardType, int tunerType, int pll, int input )
{
   TVCARD tmpCardIf;
   bool   result = FALSE;

   if (btCardCount != CARD_COUNT_UNINITIALIZED)
   {
      if (sourceIdx < btCardCount)
      {
         if (btCardList[sourceIdx].type == BTDRV_SOURCE_PCI)
         {
            if (((btCardList[sourceIdx].u.pci.VendorId << 16) |
                  btCardList[sourceIdx].u.pci.DeviceId) == chipId)
            {
               if (BtDriver_PciCardGetInterface(&tmpCardIf, (chipId >> 16), CARD_COUNT_UNINITIALIZED) )
               {
                  result = (tmpCardIf.cfg->GetCardName(&tmpCardIf, cardType) != NULL) &&
                           (tmpCardIf.cfg->GetNumInputs(&tmpCardIf) > input) &&
                            (Tuner_GetName(tunerType) != NULL);
               }
            }
         }
         else if (btCardList[sourceIdx].type == BTDRV_SOURCE_WDM)
         {
            result = (chipId == PCI_ID_PSEUDO_WDM);
         }
      }
      else
         debug2("BtDriver-CheckCardParams: source index %d no longer valid (>= %d)", sourceIdx, btCardCount);
   }
   else
   {  // no PCI scan yet: just do rudimentary checks
      if (sourceIdx < CAPTURE_CHIP_COUNT)
      {
         if (chipId == PCI_ID_PSEUDO_WDM)
         {
            result = (WdmDrvHandle != NULL);
         }
         else if (chipId != 0)
         {
            if (BtDriver_PciCardGetInterface(&tmpCardIf, (chipId >> 16), CARD_COUNT_UNINITIALIZED) )
            {
               tmpCardIf.params.cardId = cardType;

               result = (tmpCardIf.cfg->GetCardName(&tmpCardIf, cardType) != NULL) &&
                        (tmpCardIf.cfg->GetNumInputs(&tmpCardIf) > input) &&
                        (Tuner_GetName(tunerType) != NULL);
            }
            else
               debug1("BtDriver-CheckCardParams: unknown PCI ID 0x%X", chipId);
         }
         // else: card not configured yet
      }
      else
         debug2("BtDriver-CheckCardParams: invalid source index %d (>= max %d)", sourceIdx, CAPTURE_CHIP_COUNT);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Set user-configurable hardware parameters
// - called at program start and after config change
// - Important: input source and tuner freq must be set afterwards
//
bool BtDriver_Configure( int sourceIdx, int prio, int chipType, int cardType,
                         int tunerType, int pllType, bool wdmStop )
{
   bool sourceChange;
   bool cardTypeChange;
   bool pllChange;
   bool tunerChange;
   bool prioChange;
   int  chipIdx;
   int  oldChipType;
   bool result = TRUE;

   dprintf7("BtDriver-Configure: source=%d prio=%d chipType=%d cardType=%d tunerType=%d pll=%d wdmStop=%d\n", sourceIdx, prio, chipType, cardType, tunerType, pllType, wdmStop);
   prio = BtDriver_GetAcqPriority(prio);

   if ( (btCardCount != CARD_COUNT_UNINITIALIZED) && (sourceIdx < btCardCount) )
   {  // check if the configuration data still matches the hardware
      if (btCardList[sourceIdx].type == BTDRV_SOURCE_PCI)
      {
         chipIdx     = btCardList[sourceIdx].u.pci.chipIdx;
         oldChipType = ((CaptureChips[chipIdx].VendorId << 16) | CaptureChips[chipIdx].DeviceId);
         if (chipType != oldChipType)
         {
            debug3("BtDriver-Configure: PCI chip type of source #%d changed from 0x%X to 0x%X", sourceIdx, oldChipType, chipType);
            cardType = tunerType = pllType = wdmStop = 0;
         }
      }
      else if (btCardList[sourceIdx].type == BTDRV_SOURCE_WDM)
      {
         ifdebug1(chipType != PCI_ID_PSEUDO_WDM, "BtDriver-Configure: Warning: passed chip type not WDM pseudo-ID: 0x%X", chipType);
         cardType = tunerType = pllType = wdmStop = 0;
      }
   }

   // check which values change
   sourceChange   = ((sourceIdx != btCfg.sourceIdx) ||
                     (wdmStop  != btCfg.wdmStop));
   cardTypeChange = (cardType  != btCfg.cardId);
   tunerChange    = (tunerType != btCfg.tunerType);
   pllChange      = (pllType   != btCfg.pllType);
   prioChange     = (prio      != btCfg.threadPrio);

   dprintf8("BtDriver-Configure: MODE: slave=%d pci=%d wdm=%d CHANGES: source:%d cardType:%d tuner:%d pll:%d prio:%d\n", shmSlaveMode, pciDrvLoaded, wdmDrvLoaded, sourceChange, cardTypeChange, tunerChange, pllChange, prioChange);

   // save the new values
   btCfg.sourceIdx  = sourceIdx;
   btCfg.threadPrio = prio;
   btCfg.cardId     = cardType;
   btCfg.tunerType  = tunerType;
   btCfg.pllType    = pllType;
   btCfg.wdmStop    = wdmStop;

   if (shmSlaveMode == FALSE)
   {
      if (pciDrvLoaded || wdmDrvLoaded)
      {  // acquisition already running -> must change parameters on the fly
         cardif.params.cardId = btCfg.cardId;

         if (sourceChange)
         {  // change of TV card (may include switch between DsDrv and WDM)
            BtDriver_StopAcq();

            if (BtDriver_StartAcq() == FALSE)
            {
               if (pVbiBuf != NULL)
                  pVbiBuf->hasFailed = TRUE;
               result = FALSE;
            }
         }
         else
         {  // same source: just update tuner type and PLL
            if (btCardList[sourceIdx].type == BTDRV_SOURCE_PCI)
            {
#ifndef WITHOUT_DSDRV
               if (tunerChange && (btCfg.tunerType != 0) && (btCfg.inputSrc == 0))
               {
                  Tuner_Init(btCfg.tunerType, &cardif);
               }

               if (prioChange || pllChange)
               {
                  cardif.ctl->Configure(btCfg.threadPrio, btCfg.pllType);
               }

               if (cardTypeChange)
               {
                  BtDriver_SetInputSource(btCfg.inputSrc, btCfg.tunerNorm, NULL);
               }
#endif  // WITHOUT_DSDRV
            }
            else if (btCardList[sourceIdx].type == BTDRV_SOURCE_WDM)
            {  // not applicable to WDM source
            }
         }
      }
      assert(!(pciDrvLoaded && wdmDrvLoaded));  // only one may be active at the same time
   }
   else
   {  // slave mode -> new card idx
      if (sourceChange)
      {
         BtDriver_StopAcq();

         if (BtDriver_StartAcq() == FALSE)
         {
            if (pVbiBuf != NULL)
               pVbiBuf->hasFailed = TRUE;
            result = FALSE;
         }
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Set slicer type
// - note: slicer type "automatic" not allowed here:
//   type must be decided by upper layers
//
void BtDriver_SelectSlicer( VBI_SLICER_TYPE slicerType )
{
   if ((slicerType != VBI_SLICER_AUTO) && (slicerType < VBI_SLICER_COUNT))
   {
      dprintf1("BtDriver-SelectSlicer: slicer %d\n", slicerType);

      if (pVbiBuf != NULL)
         pVbiBuf->slicerType = slicerType;
   }
   else
      debug1("BtDriver-SelectSlicer: invalid slicer type %d", slicerType);
}

// ----------------------------------------------------------------------------
// Change the tuner frequency
// - makes only sense if TV tuner is input source
//
bool BtDriver_TuneChannel( int inputIdx, uint freq, bool keepOpen, bool * pIsTuner )
{
   uint norm;
   bool result = FALSE;

   norm  = freq >> 24;
   freq &= 0xffffff;

   if (BtDriver_SetInputSource(inputIdx, norm, pIsTuner))
   {
      if (*pIsTuner && (freq != 0))
      {
         // remember frequency for later
         btCfg.tunerFreq = freq;
         btCfg.tunerNorm = norm;

         if (shmSlaveMode == FALSE)
         {
            if (pciDrvLoaded)
            {
#ifndef WITHOUT_DSDRV
               result = Tuner_SetFrequency(btCfg.tunerType, freq, norm);
#else
               result = TRUE;
#endif
            }
            else if (wdmDrvLoaded)
            {
               AnalogVideoStandard WdmNorm;
               uint country;
               sint channel;

               channel = TvChannels_FreqToWdmChannel(freq, norm, &country);
               if (channel != -1)
               {
                  WdmNorm = BtDriver_WdmMapNorm(norm);
                  result = WDM_DRV_OK( WdmDrv.SetChannel(channel, country, WdmNorm) );

                  if (result)
                     dprintf4("TUNE: freq=%.2f, norm=%d: WDM channel=%d, country=%d\n", (double)freq/16.0, norm, channel, country);
                  else
                     debug4("FAILED to tune: freq=%.2f, norm=%d: WDM channel=%d, country=%d", (double)freq/16.0, norm, channel, country);
               }
               else
                  debug2("BtDriver-TuneChannel: freq conversion failed for %.2f norm=%d", (double)freq/16.0, norm);
            }
            else
            {  // driver not loaded -> freq will be tuned upon acq start
               result = TRUE;
            }
         }
         else
         {  // even in slave mode the TV app may have granted the tuner to us
            result = WintvSharedMem_SetTunerFreq(freq);
         }
      }
      else
         result = TRUE;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get the current tuner frequency
//
bool BtDriver_QueryChannel( uint * pFreq, uint * pInput, bool * pIsTuner )
{
   HRESULT hres;
   bool result = FALSE;

   if ((pFreq != NULL) && (pInput != NULL) && (pIsTuner != NULL))
   {
      if (shmSlaveMode == FALSE)
      {
         *pFreq = btCfg.tunerFreq | (btCfg.tunerNorm << 24);
         *pInput = btCfg.inputSrc;
         if (pciDrvLoaded)
         {
            *pIsTuner = cardif.cfg->IsInputATuner(&cardif, btCfg.inputSrc);
         }
         else if (wdmDrvLoaded)
         {
            hres = WdmDrv.IsInputATuner(btCfg.inputSrc, pIsTuner);
            if (WDM_DRV_OK(hres) == FALSE)
            {
               debug1("BtDriver-QueryChannel: failed to query if WDM input #%d is tuner", btCfg.inputSrc);
               *pIsTuner = FALSE;
            }
         }
         else
            *pIsTuner = TRUE;

         result = TRUE;
      }
      else
      {
         *pFreq = WintvSharedMem_GetTunerFreq();
         *pInput = WintvSharedMem_GetInputSource();
         *pIsTuner = TRUE;  // XXX TODO

         result = (*pInput != EPG_REQ_INPUT_NONE) &&
                  (*pFreq != EPG_REQ_FREQ_NONE);
      }
   }
   else
      debug0("BtDriver-QueryChannel: invalid NULL ptr params");

   return result;
}

// ----------------------------------------------------------------------------
// Dummies - not used for Windows
//
void BtDriver_CloseDevice( void )
{
}

bool BtDriver_QueryChannelToken( void )
{
   return FALSE;
}

void BtDriver_SetChannelProfile( VBI_CHANNEL_PRIO_TYPE prio,
                                 int subPrio, int duration, int minDuration )
{
}

// ----------------------------------------------------------------------------
// Check if the driver has control over the TV channel selection
//
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
   return Tuner_GetName(idx);
}

// ---------------------------------------------------------------------------
// Query if acquisition is currently enabled and in which mode
// - return value pointers may be NULL if value not required
//
bool BtDriver_GetState( bool * pEnabled, bool * pHasDriver, uint * pCardIdx )
{
   if (pEnabled != NULL)
      *pEnabled = (shmSlaveMode | pciDrvLoaded | wdmDrvLoaded);
   if (pHasDriver != NULL)
      *pHasDriver = pciDrvLoaded | wdmDrvLoaded;
   if (pCardIdx != NULL)
      *pCardIdx = btCfg.sourceIdx;

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
   uint prevFreq, prevNorm, prevInput;

   // save current input settings into temporary variables
   prevFreq  = btCfg.tunerFreq;
   prevNorm  = btCfg.tunerNorm;
   prevInput = btCfg.inputSrc;

   BtDriver_StopAcq();

   // restore input params
   btCfg.tunerFreq = prevFreq;
   btCfg.tunerNorm = prevNorm;
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
   HRESULT wdmResult;
   bool result = FALSE;

   if ( (shmSlaveMode == FALSE) && (pciDrvLoaded == FALSE) && (wdmDrvLoaded == FALSE) )
   {
      // check if the configured card is currently free
      shmSlaveMode = (WintvSharedMem_ReqTvCardIdx(btCfg.sourceIdx) == FALSE);
      if (shmSlaveMode == FALSE)
      {
         if (pVbiBuf != NULL)
         {
            if ( (btCardCount == CARD_COUNT_UNINITIALIZED) ||
                 ( (btCfg.sourceIdx < btCardCount) &&
                   (btCardList[btCfg.sourceIdx].type == BTDRV_SOURCE_PCI) ))
            {
               // load driver & initialize driver for selected TV card
               if ( BtDriver_DsDrvLoad() )
               {
#ifndef WITHOUT_DSDRV
                  if ( cardif.ctl->StartAcqThread() )
                  {
                     pVbiBuf->hasFailed = FALSE;
                     result = TRUE;
                  }
                  else
                  {
                     BtDriver_DsDrvUnload();
                  }
#else
                  result = TRUE;
#endif
               }
               assert(pciDrvLoaded == result);
            }
            // note: no "else" because dsdrv might have been loaded for card scan
            if ( (btCfg.sourceIdx < btCardCount) &&
                 (btCardList[btCfg.sourceIdx].type == BTDRV_SOURCE_WDM) )
            {
               if ( BtDriver_WdmLoad() )
               {
                  wdmResult = WdmDrv.StartAcq();
                  if ( WDM_DRV_OK(wdmResult) )
                  {
                     pVbiBuf->hasFailed = FALSE;
                     result = TRUE;
                  }
                  else
                  {
                     // XXX display error message
                     BtDriver_WdmUnload();
                  }
                  dprintf2("BtDriver-StartAcq: WDM-result=%lu OK:%d", wdmResult, WDM_DRV_OK(wdmResult));
               }
               assert(wdmDrvLoaded == result);
            }

            if ( (btCardCount != CARD_COUNT_UNINITIALIZED) &&
                 (btCfg.sourceIdx >= btCardCount) )
            {
               char msgbuf[200];

               if (btCardCount == 0)
                  sprintf(msgbuf, "No supported TV capture PCI cards or WDM drivers found!");
               else
                  sprintf(msgbuf, "TV source #%d not found! (Found %d PCI cards and WDM drivers)", btCfg.sourceIdx, btCardCount);
               MessageBox(NULL, msgbuf, "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            }
         }

         if (result == FALSE)
            WintvSharedMem_FreeTvCard();
      }
      else
      {  // TV card is already used by TV app -> slave mode
         dprintf0("BtDriver-StartAcq: starting in slave mode\n");

         assert(pVbiBuf == &vbiBuf);
         pVbiBuf = WintvSharedMem_GetVbiBuf();
         if (pVbiBuf != NULL)
         {
            memcpy((char *)pVbiBuf, &vbiBuf, sizeof(*pVbiBuf));
            pVbiBuf->hasFailed = FALSE;
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
// - XXX TODO add recursion detection (in case we're called by exception handler)
//
void BtDriver_StopAcq( void )
{
   if (shmSlaveMode == FALSE)
   {
      if (pciDrvLoaded)
      {
#ifndef WITHOUT_DSDRV
         cardif.ctl->StopAcqThread();
#endif
         BtDriver_DsDrvUnload();
      }
      else if (wdmDrvLoaded)
      {
         WdmDrv.StopAcq();

         BtDriver_WdmUnload();
      }

      // notify connected TV app that card & driver are now free
      WintvSharedMem_FreeTvCard();
   }
   else
   {
      if (pVbiBuf != NULL)
      {
         dprintf0("BtDriver-StopAcq: stopping slave mode acq\n");

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
   btCfg.tunerNorm  = VIDEO_MODE_PAL;
   btCfg.inputSrc   = INVALID_INPUT_SOURCE;
}

// ---------------------------------------------------------------------------
// Query error description for last failed operation
// - currently unused because errors are already reported by the driver in WIN32
//
const char * BtDriver_GetLastError( void )
{
   return NULL;
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
   memset(&cardif, 0, sizeof(cardif));
   btCfg.tunerType = TUNER_ABSENT;
   btCfg.inputSrc  = INVALID_INPUT_SOURCE;
   btCardCount = CARD_COUNT_UNINITIALIZED;
   pciDrvLoaded = FALSE;
   wdmDrvLoaded = FALSE;

   WdmDrvHandle = NULL;
   BtDriver_WdmDllLoad();

   return TRUE;
}

// ---------------------------------------------------------------------------
// Clean up the driver module for exit
// - called once at program termination
//
void BtDriver_Exit( void )
{
   if (pciDrvLoaded || wdmDrvLoaded)
   {  // acq is still running - should never happen
      BtDriver_StopAcq();
   }
   if (WdmDrvHandle != NULL)
   {
      FreeLibrary(WdmDrvHandle);
      WdmDrvHandle = NULL;
   }
}

