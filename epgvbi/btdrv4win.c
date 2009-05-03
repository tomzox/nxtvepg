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
 *  $Id: btdrv4win.c,v 1.61 2009/04/19 18:20:44 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF
//#define WITHOUT_DSDRV
//#define WDM_DUMMY_DRV

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
#include "epgvbi/syserrmsg.h"

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

#define WDM_DRV_DLL_PATH "VBIAcqWDMDrv.dll"

static WDMDRV_CTL  WdmDrv;        // pointers to wdmdrv API functions
static HINSTANCE   WdmDrvHandle;  // handle to wdmdrv interface DLL
static uint        WdmSrcCount;   // number of known WDM sources
static vbi_raw_decoder WdmZvbiDec[2];
static LONGLONG    WdmLastTimestamp;
static uint        WdmLastFrameNo;
static uint        WdmSkipFields;

#ifdef WDM_DUMMY_DRV
extern WDMDRV_CTL WdmDummyIf;
#endif

// ----------------------------------------------------------------------------
// Shared variables between PCI and WDM sources

#define MAX_CARD_COUNT  4
#define CARD_COUNT_UNINITIALIZED  (~0u)
static PCI_SOURCE     btCardList[MAX_CARD_COUNT];
static uint           btCardCount;

#define INVALID_INPUT_SOURCE  0xff

static struct
{
   BTDRV_SOURCE_TYPE drvType;
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

static void BtDriver_WdmDllFree( void );
static void BtDriver_WdmCountSources( bool showDrvErr );
static bool BtDriver_DsDrvCountSources( bool showDrvErr );
static BOOL BtDriver_PciCardOpen( uint sourceIdx );

// ----------------------------------------------------------------------------
// Load WDM interface library dynamically
//
static bool BtDriver_WdmDllLoad( bool showDrvErr )
{
   bool result = FALSE;

#ifndef WDM_DUMMY_DRV
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
   {
      char * pErrMsg = NULL;
      DWORD errCode = GetLastError();

      debug1("BtDriver-WdmDllLoad: Failed to load DLL " WDM_DRV_DLL_PATH " (%ld)", errCode);
      if (showDrvErr)
      {
         if (errCode == ERROR_FILE_NOT_FOUND)
         {
            MessageBox(NULL, "WDM interface library '" WDM_DRV_DLL_PATH "' not found.\n"
                             "Without this DLL file you can only use the internal\n"
                             "'dsdrv' driver for your TV card.",
                             "nxtvepg WDM problem", MB_ICONEXCLAMATION | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         else if (errCode == ERROR_MOD_NOT_FOUND)
         {
            MessageBox(NULL, "Failed to load WDM interface library '" WDM_DRV_DLL_PATH "'\n"
                             "probably because DLL 'MSVCR70D.dll' was not found in the path.\n"
                             "Without loading the WDM DLL you can only use the internal\n"
                             "'dsdrv' driver for your TV card.",
                             "nxtvepg WDM problem", MB_ICONEXCLAMATION | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         else
         {
            SystemErrorMessage_Set(&pErrMsg, errCode, "Failed to load WDM interface library '" WDM_DRV_DLL_PATH "': ", NULL);
            MessageBox(NULL, pErrMsg, "nxtvepg WDM problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            xfree(pErrMsg);
         }
      }
   }

#else  // WDM_DUMMY_DRV
   {
      WdmDrv = WdmDummyIf;
      if ( WdmDrv.InitDrv() == S_OK )
      {
         dprintf0("BtDriver-WdmDllLoad: Dummy interface loaded\n");
         WdmDrvHandle = LoadLibrary("kernel32.dll");
         result = TRUE;
      }
   }
#endif  // WDM_DUMMY_DRV

   return result;
}

// ----------------------------------------------------------------------------
// CallBack function called by the WDM VBI Driver
// - this is just a wrapper to the ttx decoder
//
static BOOL __stdcall BtDriver_WdmVbiCallback( BYTE * pFieldBuffer, int FrameType, LONGLONG timestamp )
{
   BYTE * pLineData;
   uint FirstField, LastField;
   uint maxFieldDelta;
   uint field_idx;
   uint line_count;
   uint line_idx;

   switch (FrameType)
   {
      case PROGRESSIVE_FRAME:
         FirstField = 0;
         LastField = 1;
         maxFieldDelta = 400000 *1.5;  // 25 frames per second
         break;
      case FIELD_ONE:
         FirstField = LastField = 0;
         maxFieldDelta = 200000 *1.5;  // 2*25 fields per second
         break;
      case FIELD_TWO:
         FirstField = LastField = 1;
         maxFieldDelta = 200000 *1.5;
         break;
      default:
         FirstField = LastField = 0;
         maxFieldDelta = 0;
         break;
   }

   if (WdmSkipFields > 0)
   {
      dprintf1("BtDriver-WdmVbiCallback: field-skip %d remaining\n", WdmSkipFields);
      pLineData = pFieldBuffer;
      for (field_idx = FirstField; field_idx <= LastField; field_idx++)
      {
#if 0
         memset(pLineData, 0, WdmZvbiDec[field_idx].bytes_per_line *
                              WdmZvbiDec[field_idx].count[field_idx]);
#endif
         pLineData +=         WdmZvbiDec[field_idx].bytes_per_line *
                              WdmZvbiDec[field_idx].count[field_idx];

         if (WdmSkipFields > 0)
            WdmSkipFields -= 1;
      }
      return TRUE;
   }

   // convert timestamp into frame sequence number
   //printf("VBI: %ld (D=%ld), %d, %d\n", (long)timestamp, (long)(timestamp - WdmLastTimestamp), FrameType, WdmLastFrameNo); /* XXX */
#if 0 // Hauppauge driver provides bogus timestamps
   if (timestamp - WdmLastTimestamp > maxFieldDelta)
      WdmLastFrameNo += 2;
   else
      WdmLastFrameNo += 1;
#endif
   WdmLastTimestamp = timestamp;

   if (pVbiBuf != NULL)
   {
      if (pVbiBuf->slicerType != VBI_SLICER_ZVBI)
      {
         if (VbiDecodeStartNewFrame(WdmLastFrameNo))
         {
            pLineData = pFieldBuffer;

            for (field_idx = FirstField; field_idx <= LastField; field_idx++)
            {
               line_count = WdmZvbiDec[field_idx].count[field_idx];

               // decode all lines in the field
               for (line_idx = 0; line_idx < line_count; line_idx++)
               {
                  //debug1("line: %d\n", line_idx);

                  VbiDecodeLine(pLineData, line_idx, TRUE);

                  pLineData += WdmZvbiDec[field_idx].bytes_per_line;
               }
            }
         }
      }
      else
      {
         pLineData = pFieldBuffer;

         for (field_idx = FirstField; field_idx <= LastField; field_idx++)
         {
            ZvbiSliceAndProcess(&WdmZvbiDec[field_idx], pLineData, WdmLastFrameNo);

            line_count = WdmZvbiDec[field_idx].count[field_idx];
            pLineData += line_count * WdmZvbiDec[field_idx].bytes_per_line;
         }
      }
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
      dprintf7("WdmVBISettings: lines:%ld-%ld freq:%ld, min/max/actual line start times:%ld/%ld/%ld, end time:%ld\n", VBIParameters.StartLine, VBIParameters.EndLine, VBIParameters.SamplingFrequency, VBIParameters.MinLineStartTime, VBIParameters.MaxLineStartTime, VBIParameters.ActualLineStartTime, VBIParameters.ActualLineEndTime);
      dprintf4("                video std:%ld, sample count:%ld, stride:%ld bytes, buf size:%ld\n", VBIParameters.VideoStandard, VBIParameters.SamplesPerLine, VBIParameters.StrideInBytes, VBIParameters.BufferSize);

      VbiDecodeSetSamplingRate(VBIParameters.SamplingFrequency,
                               VBIParameters.StartLine);

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
      services[1] = vbi_raw_decoder_add_services(&WdmZvbiDec[1], VBI_SLICED_TELETEXT_B, 0);
      if ( ((services[0] & VBI_SLICED_TELETEXT_B) == 0) ||
           ((services[1] & VBI_SLICED_TELETEXT_B) == 0) )
      {
         MessageBox(NULL, "The selected capture source is appearently not able "
                          "to capture teletext. Will try anyways, but it might not work.",
                          "nxtvepg driver warning",
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
      assert(btCardList[sourceIdx].VendorId == VendorId);

      pTvCard->params.BusNumber   = btCardList[sourceIdx].dwBusNumber;
      pTvCard->params.SlotNumber  = btCardList[sourceIdx].dwSlotNumber;
      pTvCard->params.VendorId    = btCardList[sourceIdx].VendorId;
      pTvCard->params.DeviceId    = btCardList[sourceIdx].DeviceId;
      pTvCard->params.SubSystemId = btCardList[sourceIdx].dwSubSystemID;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Free resources allocated in card drivers
//
static void BtDriver_PciCardsRelease( void )
{
   TVCARD tmpCardIf;

   Bt8x8_GetInterface(&tmpCardIf);
   tmpCardIf.cfg->FreeCardList();

   SAA7134_GetInterface(&tmpCardIf);
   tmpCardIf.cfg->FreeCardList();

   Cx2388x_GetInterface(&tmpCardIf);
   tmpCardIf.cfg->FreeCardList();
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
const char * BtDriver_GetInputName( uint sourceIdx, uint cardType, uint drvType, uint inputIdx )
{
   const char * pName = NULL;
   TVCARD tmpCardIf;
   uint   chipIdx;
   HRESULT hres;

   if (drvType == BTDRV_SOURCE_PCI)
   {
      if ((btCardCount != CARD_COUNT_UNINITIALIZED) && (sourceIdx < btCardCount))
      {
         chipIdx = btCardList[sourceIdx].chipIdx;

         if (BtDriver_PciCardGetInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, CARD_COUNT_UNINITIALIZED) )
         {
            tmpCardIf.params.cardId = cardType;

            if (inputIdx < tmpCardIf.cfg->GetNumInputs(&tmpCardIf))
               pName = tmpCardIf.cfg->GetInputName(&tmpCardIf, inputIdx);
            else
               pName = NULL;
         }
      }
      dprintf4("BtDriver-GetInputName: PCI sourceIdx=%d, cardType=%d, inputIdx=%d: result=%s\n", sourceIdx, cardType, inputIdx, ((pName != NULL) ? pName : "NULL"));
   }
   else if (drvType == BTDRV_SOURCE_WDM)
   {
      // note: only try to load driver for the first query
      if ( (inputIdx == 0) && (WdmSrcCount == CARD_COUNT_UNINITIALIZED) )
      {
         BtDriver_WdmCountSources(FALSE);
      }

      if ((WdmSrcCount != CARD_COUNT_UNINITIALIZED) && (sourceIdx < WdmSrcCount) &&
          (WdmDrvHandle != NULL))
      {
         if (wdmDrvLoaded == FALSE)
         {
            hres = WdmDrv.SelectDevice(sourceIdx, 0);
            if ( WDM_DRV_OK(hres) )
            {
               hres = WdmDrv.GetVideoInputName(inputIdx, (char **)&pName);
            }
            else
            {
               debug3("BtDriver-GetInputName: failed to select source #%d (WDM dev #%d): %ld", sourceIdx, sourceIdx, hres);
               hres = S_OK;
               pName = NULL;
            }
         }
         else
         {  // this branch should never be reached - since the query function
            // does not allow to specify a device acq must be stopped before so
            // that we can temporarily select the given device
            if (sourceIdx == btCfg.sourceIdx)
            {
               hres = WdmDrv.GetVideoInputName(inputIdx, (char **)&pName);
            }
            else
            {
               debug2("BtDriver-GetInputName: cannot query source %d while acq is enabled for %d", sourceIdx, btCfg.sourceIdx);
               hres = S_OK;
               pName = NULL;
            }
         }
         if ( WDM_DRV_OK(hres) == FALSE )
         {
            ifdebug4(inputIdx == 0, "BtDriver-GetInputName: failed for input #%d on source #%d (WDM dev #%d): %ld", inputIdx, sourceIdx, sourceIdx, hres);
            pName = NULL;
         }
      }
      dprintf3("BtDriver-GetInputName: WDM sourceIdx=%d, inputIdx=%d: result=%s\n", sourceIdx, inputIdx, ((pName != NULL) ? pName : "NULL"));
   }
   else
      debug4("BtDriver-GetInputName: invalid driver type %d: sourceIdx=%d, cardType=%d, inputIdx=%d", drvType, sourceIdx, cardType, inputIdx);

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
            MessageBox(NULL, "Cannot query the TV card while connected to a TV application.\nTerminated the TV application and try again.", "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         else if (pciDrvLoaded && (btCfg.sourceIdx != sourceIdx))
         {  // error
            debug2("BtDriver-QueryCardParams: acq running for different card %d instead req. %d", btCfg.sourceIdx, sourceIdx);
            MessageBox(NULL, "Acquisition is running for a different TV card.\nStop acquisition and try again.", "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         else
         {
            drvWasLoaded = pciDrvLoaded;
            if (pciDrvLoaded == FALSE)
            {
               shmSlaveMode = (WintvSharedMem_ReqTvCardIdx(sourceIdx, NULL) == FALSE);
               if (shmSlaveMode == FALSE)
               {
                  loadError = DsDrvLoad();
                  if (loadError == HWDRV_LOAD_SUCCESS)
                  {
                     pciDrvLoaded = TRUE;

                     // scan the PCI bus for known cards
                     BtDriver_DsDrvCountSources(TRUE);

                     if (BtDriver_PciCardOpen(sourceIdx) == FALSE)
                     {
                        DsDrvUnload();
                        pciDrvLoaded = FALSE;
                     }
                  }
                  else
                  {
                     MessageBox(NULL, DsDrvGetErrorMsg(loadError), "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
                  }
               }
            }

            if (pciDrvLoaded)
            {
               chipIdx = btCardList[sourceIdx].chipIdx;
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
#endif
                  {
                     *pTunerType = *pPllType = 0;
                  }
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
      chipIdx = btCardList[sourceIdx].chipIdx;

      if ( BtDriver_PciCardGetInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, CARD_COUNT_UNINITIALIZED) )
      {
         pName = tmpCardIf.cfg->GetCardName(&tmpCardIf, listIdx);
      }
   }
   return pName;
}

// ---------------------------------------------------------------------------
// Determine default driver type upon first start
// - prefer WDM if DLL is available
//
BTDRV_SOURCE_TYPE BtDriver_GetDefaultDrvType( void )
{
   BTDRV_SOURCE_TYPE result;

   if (BtDriver_WdmDllLoad(FALSE))
   {
      BtDriver_WdmDllFree();
      result = BTDRV_SOURCE_WDM;
   }
   else
   {
      result = BTDRV_SOURCE_PCI;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Return name & chip type for given TV card
// - if the driver was never loaded before the PCI bus is scanned now;
//   no error message displayed if the driver fails to load, but result set to FALSE
// - end of enumeration is indicated by a FALSE result or NULL name pointer
//
bool BtDriver_EnumCards( uint drvType, uint sourceIdx, uint cardType,
                         uint * pChipType, const char ** pName, bool showDrvErr )
{
   static char cardName[50];
   TVCARD tmpCardIf;
   uint   chipIdx;
   uint   chipType;
   HRESULT hres;
   bool   result = FALSE;

   if ((pChipType != NULL) && (pName != NULL))
   {
      if (drvType == BTDRV_SOURCE_PCI)
      {
         // note: only try to load driver for the first query
         if ( (sourceIdx == 0) && (btCardCount == CARD_COUNT_UNINITIALIZED) )
         {
            assert(pciDrvLoaded == FALSE);
            BtDriver_DsDrvCountSources(showDrvErr);
         }

         if (btCardCount != CARD_COUNT_UNINITIALIZED)
         {
            if (sourceIdx < btCardCount)
            {
               chipIdx  = btCardList[sourceIdx].chipIdx;
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
            else
               *pName = NULL;
         }
         else
         {  // failed to load driver for PCI scan -> just return names for already known cards
            if (*pChipType != 0)
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
                  MessageBox(NULL, "Cannot scan PCI bus for TV cards while connected to a TV application.\nTerminated the TV application and try again.", "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
               }
               *pName = NULL;
            }
         }
         result = TRUE;
      }
      else if (drvType == BTDRV_SOURCE_WDM)
      {
         // note: only try to load driver for the first query
         if ( (sourceIdx == 0) && (WdmSrcCount == CARD_COUNT_UNINITIALIZED) )
         {
            assert(wdmDrvLoaded == FALSE);
            BtDriver_WdmCountSources(showDrvErr);
         }

         if ( (WdmDrvHandle != NULL) &&
              (WdmSrcCount != CARD_COUNT_UNINITIALIZED) && (sourceIdx < WdmSrcCount) )
         {
            hres = WdmDrv.GetDeviceName(sourceIdx, (char **) pName);
            if ( WDM_DRV_OK(hres) )
            {
               *pChipType = 0;  // not used for WDM
            }
            else
            {
               // XXX retrieve & display error message
               sprintf(cardName, "unknown WDM source");
               *pName = cardName;
            }
         }
         else
            *pName = NULL;

         result = TRUE;
      }
      else
      {  // end of enumeration
         debug1("BtDriver-EnumCards: unknown driver tpe: %d", drvType);
         *pName = NULL;
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
static void BtDriver_WdmCountSources( bool showDrvErr )
{
   HRESULT hres;

   if (WdmDrvHandle == NULL)
   {
      assert(wdmDrvLoaded == FALSE);
      assert(WdmSrcCount == CARD_COUNT_UNINITIALIZED);

      BtDriver_WdmDllLoad(showDrvErr);
   }

   if (WdmDrvHandle != NULL)
   {
      if (WdmSrcCount == CARD_COUNT_UNINITIALIZED)
      {
         dprintf0("BtDriver-WdmSourceCount: entry\n");

         hres = WdmDrv.EnumDevices();
         if (WDM_DRV_OK(hres))
         {
            WdmSrcCount = (uint) hres;
            dprintf1("BtDriver-WdmSourceCount: have %d WDM sources\n", WdmSrcCount);
         }
         else
            debug1("BtDriver-WdmSourceCount: WDM interface DLL returned error %ld", hres);
      }
   }
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
                                  &btCardList[btCardCount].dwSubSystemID,
                                  &btCardList[btCardCount].dwBusNumber,
                                  &btCardList[btCardCount].dwSlotNumber) == ERROR_SUCCESS)
         {
            dprintf4("PCI scan: found capture chip %s, ID=%lx, bus=%ld, slot=%ld\n", CaptureChips[chipIdx].szName, btCardList[btCardCount].dwSubSystemID, btCardList[btCardCount].dwBusNumber, btCardList[btCardCount].dwSlotNumber);

            btCardList[btCardCount].VendorId    = CaptureChips[chipIdx].VendorId;
            btCardList[btCardCount].DeviceId    = CaptureChips[chipIdx].DeviceId;
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
   dprintf1("BtDriver-PciCardCount: found %d PCI cards\n", btCardCount);
}

// ---------------------------------------------------------------------------
// Generate a list of available cards
//
static bool BtDriver_DsDrvCountSources( bool showDrvErr )
{
   uint  loadError;
   bool  result = FALSE;

   // if the scan was already done skip it
   if (btCardCount == CARD_COUNT_UNINITIALIZED)
   {
      dprintf0("BtDriver-CountSources: PCI start\n");

      // note: don't reserve the card from TV app since a PCI scan does not cause conflicts
      if (pciDrvLoaded == FALSE)
      {
         loadError = DsDrvLoad();
         if (loadError == HWDRV_LOAD_SUCCESS)
         {
            // scan the PCI bus for known cards, but don't open any
            BtDriver_PciCardCount();
            DsDrvUnload();
            result = TRUE;
         }
         else if (showDrvErr)
         {
            MessageBox(NULL, DsDrvGetErrorMsg(loadError), "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
      }
      else
      {  // dsdrv already loaded -> just do the scan
         BtDriver_PciCardCount();
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
      chipIdx     = btCardList[sourceIdx].chipIdx;
      chipCardIdx = btCardList[sourceIdx].chipCardIdx;

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
            MessageBox(NULL, msgbuf, "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
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

   BtDriver_WdmDllFree();

   loadError = DsDrvLoad();
   if (loadError == HWDRV_LOAD_SUCCESS)
   {
      pciDrvLoaded = TRUE;
      BtDriver_DsDrvCountSources(FALSE);

      if (btCfg.sourceIdx < btCardCount)
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
      MessageBox(NULL, DsDrvGetErrorMsg(loadError), "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Shut down the WDM source and free all resources
// - called when switching to dsdrv or upon exit
//
static void BtDriver_WdmDllFree( void )
{
   // acquisition must be stopped
   assert(wdmDrvLoaded == FALSE);

   // additionally unload the library
   if (WdmDrvHandle != NULL)
   {
      WdmDrv.FreeDrv();
      FreeLibrary(WdmDrvHandle);

      WdmDrvHandle = NULL;
      WdmSrcCount = CARD_COUNT_UNINITIALIZED;
   }
}

// ----------------------------------------------------------------------------
// Reverse operation to WDM load
// - doesn't do much except resetting the "loaded" flag
// - the DLL is detached in a separate function for efficiency
//
static void BtDriver_WdmUnload( void )
{
   if (wdmDrvLoaded)
   {
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

   // load library and enumerate sources, if necessary
   BtDriver_WdmCountSources(FALSE);

   if (WdmDrvHandle != NULL)
   {
      if (btCfg.sourceIdx < WdmSrcCount)
      {
         wdmResult = WdmDrv.SelectDevice(btCfg.sourceIdx, 0);
         if ( WDM_DRV_OK(wdmResult) )
         {
            if ((wdmResult & WDM_DRV_WARNING_INUSE) == 0)
            {
               BtDriver_WdmInitSlicer();

               if ( WdmDrv.SetWSTPacketCallBack(BtDriver_WdmVbiCallback, TRUE) == S_OK )
               {
                  wdmDrvLoaded = TRUE;
                  WdmSkipFields = 0;
                  result = TRUE;
               }
            }
            else
               MessageBox(NULL, "Failed to start Nextview acquisition: Capture device already in use", "nxtvepg WDM driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         // else: user message already generated by open function (XXX really?)
         dprintf1("BtDriver-WdmLoad: WDM result %ld\n", (long)wdmResult);
      }
      else
         MessageBox(NULL, "Failed to start Nextview acquisition: Capture device not found", "nxtvepg WDM driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);

      if (result == FALSE)
      {
         BtDriver_WdmUnload();
      }
   }
   else
   {  // failed to load the DLL
      // XXX TODO print human readable error code
      //MessageBox(NULL, GetDriverErrorMsg(loadError), "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      result = FALSE;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Check if the parameters are valid for the given source
// - this function is used to warn the user abour parameter mismatch after
//   hardware or driver configuration changes
//
bool BtDriver_CheckCardParams( uint drvType, uint sourceIdx, uint chipId,
                               uint cardType, uint tunerType, uint pll, uint input )
{
   TVCARD tmpCardIf;
   bool   result = FALSE;

   if (drvType == BTDRV_SOURCE_PCI)
   {
      if (btCardCount != CARD_COUNT_UNINITIALIZED)
      {
         if (sourceIdx < btCardCount)
         {
            if (((btCardList[sourceIdx].VendorId << 16) |
                  btCardList[sourceIdx].DeviceId) == chipId)
            {
               if (BtDriver_PciCardGetInterface(&tmpCardIf, (chipId >> 16), CARD_COUNT_UNINITIALIZED) )
               {
                  result = (tmpCardIf.cfg->GetCardName(&tmpCardIf, cardType) != NULL) &&
                           (tmpCardIf.cfg->GetNumInputs(&tmpCardIf) > input) &&
                            (Tuner_GetName(tunerType) != NULL);
               }
            }
         }
         else
            debug2("BtDriver-CheckCardParams: source index %d no longer valid (>= %d)", sourceIdx, btCardCount);
      }
      // no PCI scan yet: just do rudimentary checks
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
   else if (drvType == BTDRV_SOURCE_WDM)
   {
      if ( (WdmSrcCount == CARD_COUNT_UNINITIALIZED) && !pciDrvLoaded)
      {
         BtDriver_WdmCountSources(FALSE);
      }
      if (WdmSrcCount != CARD_COUNT_UNINITIALIZED)
      {
         if (sourceIdx < WdmSrcCount)
         {
            // XXX TODO: compare device path
            result = TRUE;
         }
         else
            debug2("BtDriver-CheckCardParams: WDM source index %d no longer valid (>= %d)", sourceIdx, WdmSrcCount);
      }
      else
      {
         if (pciDrvLoaded)
            result = TRUE;  // cannot check WDM while PCI active
         else
            result = FALSE;  // WDM sources not counted: probably DLL missing
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Set user-configurable hardware parameters
// - called at program start and after config change
// - Important: input source and tuner freq must be set afterwards
//
bool BtDriver_Configure( int sourceIdx, int drvType, int prio, int chipType, int cardType,
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

   dprintf8("BtDriver-Configure: source=%d drvtype=%d prio=%d chipType=%d cardType=%d tunerType=%d pll=%d wdmStop=%d\n", sourceIdx, drvType, prio, chipType, cardType, tunerType, pllType, wdmStop);
   prio = BtDriver_GetAcqPriority(prio);

   if ( (drvType == BTDRV_SOURCE_PCI) &&
        (btCardCount != CARD_COUNT_UNINITIALIZED) && (sourceIdx < btCardCount) )
   {  // check if the configuration data still matches the hardware
      chipIdx     = btCardList[sourceIdx].chipIdx;
      oldChipType = ((CaptureChips[chipIdx].VendorId << 16) | CaptureChips[chipIdx].DeviceId);
      if (chipType != oldChipType)
      {
         debug3("BtDriver-Configure: PCI chip type of source #%d changed from 0x%X to 0x%X", sourceIdx, oldChipType, chipType);
         cardType = tunerType = pllType = wdmStop = 0;
      }
   }

   // check which values change
   sourceChange   = ((drvType  != btCfg.drvType) ||
                     (sourceIdx != btCfg.sourceIdx) ||
                     (wdmStop  != btCfg.wdmStop));
   cardTypeChange = (cardType  != btCfg.cardId);
   tunerChange    = (tunerType != btCfg.tunerType);
   pllChange      = (pllType   != btCfg.pllType);
   prioChange     = (prio      != btCfg.threadPrio);

   dprintf8("BtDriver-Configure: PREV-MODE: slave=%d pci=%d wdm=%d CHANGES: source:%d cardType:%d tuner:%d pll:%d prio:%d\n", shmSlaveMode, pciDrvLoaded, wdmDrvLoaded, sourceChange, cardTypeChange, tunerChange, pllChange, prioChange);

   // save the new values
   btCfg.drvType    = drvType;
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
            if (drvType == BTDRV_SOURCE_PCI)
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
            else if (drvType == BTDRV_SOURCE_WDM)
            {  // nothing to do for WDM source
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
                  WdmSkipFields = 2 * 4;
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
            result = WintvSharedMem_SetTunerFreq(freq, norm);
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
         *pInput = EPG_REQ_INPUT_NONE;  // dummy

         result = WintvSharedMem_GetTunerFreq(pFreq, pIsTuner) &&
                  (*pIsTuner) &&
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
   bool epgHasDriver;
   bool result = FALSE;

   if ( (shmSlaveMode == FALSE) && (pciDrvLoaded == FALSE) && (wdmDrvLoaded == FALSE) )
   {
      // check if the configured card is currently free
      shmSlaveMode = (WintvSharedMem_ReqTvCardIdx(btCfg.sourceIdx, &epgHasDriver) == FALSE);
      if (shmSlaveMode == FALSE)
      {
         if (pVbiBuf != NULL)
         {
            if (btCfg.drvType == BTDRV_SOURCE_PCI)
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

               if ( (btCardCount != CARD_COUNT_UNINITIALIZED) &&
                    (btCfg.sourceIdx >= btCardCount) )
               {
                  char msgbuf[200];

                  if (btCardCount == 0)
                     sprintf(msgbuf, "Cannot start EPG data acquisition because\n"
                                     "no supported TV capture PCI cards have been found.");
                  else
                     sprintf(msgbuf, "Cannot start EPG data acquisition because\n"
                                     "TV card #%d was not found on the PCI bus\n"
                                     "(found %d supported TV capture cards)",
                                     btCfg.sourceIdx, btCardCount);
                  MessageBox(NULL, msgbuf, "nxtvepg driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
               }
            }
            else if (btCfg.drvType == BTDRV_SOURCE_WDM)
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
                  dprintf2("BtDriver-StartAcq: WDM-result=%lu OK:%d\n", wdmResult, WDM_DRV_OK(wdmResult));
               }
               assert(wdmDrvLoaded == result);
            }
            else if (btCfg.drvType == BTDRV_SOURCE_NONE)
            {
               // TV card is disabled -> do nothing, return failure result code
            }
            else
               debug1("BtDriver-StartAcq: invalid drv type %d", btCfg.drvType);
         }

         if (result == FALSE)
            WintvSharedMem_FreeTvCard();
      }
      else if (epgHasDriver == FALSE)
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
      else
      {  // TV card already used by EPG app (e.g. EPG GUI: must stop acq before daemon can start)
         debug1("BtDriver-StartAcq: card already busy by another EPG app: %d", epgHasDriver);
         shmSlaveMode = FALSE;
         result = FALSE;
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
   btCfg.drvType   = BTDRV_SOURCE_NONE;
   btCfg.tunerType = TUNER_ABSENT;
   btCfg.inputSrc  = INVALID_INPUT_SOURCE;
   btCardCount = CARD_COUNT_UNINITIALIZED;
   WdmSrcCount = CARD_COUNT_UNINITIALIZED;
   pciDrvLoaded = FALSE;
   wdmDrvLoaded = FALSE;

   WdmDrvHandle = NULL;

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
   // release dynamically loaded TV card lists
   BtDriver_PciCardsRelease();
   // the WDM driver may be loaded even while acq is disabled (for TV card config dialog)
   BtDriver_WdmDllFree();
}

