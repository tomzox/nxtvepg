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
 *    This module manages M$ Windows WDM drivers. It provides a common interface
 *    to higher-level modules (i.e. shared with Linux), for example for starting
 *    or stopping acquisition or change the channel.
 *
 *  Authors:
 *
 *    Original WDM support
 *      February 2004 by Gérard Chevalier (gd_chevalier@hotmail.com)
 *
 *    The rest
 *      Tom Zoerner
 *
 *
 *  $Id: btdrv4win.c,v 1.63 2020/06/24 07:25:39 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF
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

#define CARD_COUNT_UNINITIALIZED  (~0u)
#define INVALID_INPUT_SOURCE  0xff

static struct
{
   BTDRV_SOURCE_TYPE drvType;
   uint        sourceIdx;
   uint        cardId;
   uint        threadPrio;
   uint        inputSrc;
   uint        tunerFreq;
   uint        tunerNorm;
} btCfg;

static BOOL wdmDrvLoaded;
static BOOL shmSlaveMode = FALSE;
static VBI_SLICER_TYPE vbiSlicerType;

// ----------------------------------------------------------------------------

volatile EPGACQ_BUF * pVbiBuf;
static EPGACQ_BUF vbiBuf;

static void BtDriver_WdmDllFree( void );
static void BtDriver_WdmCountSources( bool showDrvErr );

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
   //uint maxFieldDelta;
   uint field_idx;
   uint line_count;
   uint line_idx;

   switch (FrameType)
   {
      case PROGRESSIVE_FRAME:
         FirstField = 0;
         LastField = 1;
         //maxFieldDelta = 400000 *1.5;  // 25 frames per second
         break;
      case FIELD_ONE:
         FirstField = LastField = 0;
         //maxFieldDelta = 200000 *1.5;  // 2*25 fields per second
         break;
      case FIELD_TWO:
         FirstField = LastField = 1;
         //maxFieldDelta = 200000 *1.5;
         break;
      default:
         FirstField = LastField = 0;
         //maxFieldDelta = 0;
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
      if (vbiSlicerType != VBI_SLICER_ZVBI)
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
      case EPGACQ_TUNER_NORM_PAL :
         WdmNorm = AnalogVideo_PAL_B;
         break;
      case EPGACQ_TUNER_NORM_SECAM :
         WdmNorm = AnalogVideo_SECAM_L;
         break;
      case EPGACQ_TUNER_NORM_NTSC :
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
      if (wdmDrvLoaded)
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
const char * BtDriver_GetInputName( uint sourceIdx, BTDRV_SOURCE_TYPE drvType, uint inputIdx )
{
   const char * pName = NULL;
   HRESULT hres;

   if (drvType == BTDRV_SOURCE_WDM)
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
      debug3("BtDriver-GetInputName: invalid driver type %d: sourceIdx=%d, inputIdx=%d", drvType, sourceIdx, inputIdx);

   return pName;
}

// ---------------------------------------------------------------------------
// Determine default driver type upon first start
// - prefer WDM if DLL is available
//
BTDRV_SOURCE_TYPE BtDriver_GetDefaultDrvType( void )
{
   return BTDRV_SOURCE_WDM;
}

// ---------------------------------------------------------------------------
// Return name & chip type for given TV card
// - end of enumeration is indicated by a FALSE result or NULL name pointer
//
const char * BtDriver_GetCardName( BTDRV_SOURCE_TYPE drvType, uint sourceIdx, bool showDrvErr )
{
   HRESULT hres;
   const char * pName = NULL;

   if (drvType == BTDRV_SOURCE_WDM)
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
         hres = WdmDrv.GetDeviceName(sourceIdx, (char **) &pName);
         if ( WDM_DRV_OK(hres) == FALSE )
         {
            // XXX retrieve & display error message
            pName = "Unknown WDM source";
         }
      }
   }
   else if (drvType == BTDRV_SOURCE_NONE)
   {
   }
   else
   {  // end of enumeration
      debug1("BtDriver-EnumCards: unknown driver tpe: %d", drvType);
   }

   return pName;
}

// ---------------------------------------------------------------------------
// Check if the current source has a video signal
//
bool BtDriver_IsVideoPresent( void )
{
   HRESULT wdmResult;
   bool result;

   if (wdmDrvLoaded)
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

   assert(shmSlaveMode == FALSE);
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
// - this function is used to warn the user about parameter mismatch after
//   hardware or driver configuration changes
//
bool BtDriver_CheckCardParams( BTDRV_SOURCE_TYPE drvType, uint sourceIdx, uint input )
{
   bool   result = FALSE;

   if (drvType == BTDRV_SOURCE_WDM)
   {
      if (WdmSrcCount == CARD_COUNT_UNINITIALIZED)
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
bool BtDriver_Configure( int sourceIdx, BTDRV_SOURCE_TYPE drvType, int prio )
{
   bool sourceChange;
   bool result = TRUE;

   dprintf3("BtDriver-Configure: source=%d drvtype=%d prio=%d\n", sourceIdx, drvType, prio);
   prio = BtDriver_GetAcqPriority(prio);

   // check which values change
   sourceChange   = ((drvType  != btCfg.drvType) || (sourceIdx != btCfg.sourceIdx));

   dprintf3("BtDriver-Configure: PREV-MODE: slave=%d wdm=%d CHANGES: source:%d\n", shmSlaveMode, wdmDrvLoaded, sourceChange);

   // save the new values
   btCfg.drvType    = drvType;
   btCfg.sourceIdx  = sourceIdx;
   btCfg.threadPrio = prio;

   if (shmSlaveMode == FALSE)
   {
      if (wdmDrvLoaded)
      {  // acquisition already running -> must change parameters on the fly

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
            if (drvType == BTDRV_SOURCE_WDM)
            {  // nothing to do for WDM source
            }
         }
      }
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
         vbiSlicerType = slicerType;
   }
   else
      debug1("BtDriver-SelectSlicer: invalid slicer type %d", slicerType);
}

// ----------------------------------------------------------------------------
// Change the tuner frequency
// - makes only sense if TV tuner is input source
//
bool BtDriver_TuneChannel( int inputIdx, const EPGACQ_TUNER_PAR * pFreqPar, bool keepOpen, bool * pIsTuner )
{
   bool result = FALSE;

   if (BtDriver_SetInputSource(inputIdx, pFreqPar->norm, pIsTuner))
   {
      if (*pIsTuner && (pFreqPar->freq != 0))
      {
         // remember frequency for later
         btCfg.tunerFreq = pFreqPar->freq;
         btCfg.tunerNorm = pFreqPar->norm;

         if (shmSlaveMode == FALSE)
         {
            if (wdmDrvLoaded)
            {
               AnalogVideoStandard WdmNorm;
               uint country;
               sint channel;

               channel = TvChannels_FreqToWdmChannel(pFreqPar->freq, pFreqPar->norm, &country);
               if (channel != -1)
               {
                  WdmNorm = BtDriver_WdmMapNorm(pFreqPar->norm);
                  WdmSkipFields = 2 * 4;
                  result = WDM_DRV_OK( WdmDrv.SetChannel(channel, country, WdmNorm) );

                  if (result)
                     dprintf4("TUNE: freq=%.2f, norm=%d: WDM channel=%d, country=%d\n", (double)pFreqPar->freq/16.0, pFreqPar->norm, channel, country);
                  else
                     debug4("FAILED to tune: freq=%.2f, norm=%d: WDM channel=%d, country=%d", (double)pFreqPar->freq/16.0, pFreqPar->norm, channel, country);
               }
               else
                  debug2("BtDriver-TuneChannel: freq conversion failed for %.2f norm=%d", (double)pFreqPar->freq/16.0, pFreqPar->norm);
            }
            else
            {  // driver not loaded -> freq will be tuned upon acq start
               result = TRUE;
            }
         }
         else
         {  // even in slave mode the TV app may have granted the tuner to us
            result = WintvSharedMem_SetTunerFreq(pFreqPar->freq, pFreqPar->norm);
         }
      }
      else
         result = TRUE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Configure DVB demux PID
// - dummy interface - DVB is currently not supported for Windows
//
void BtDriver_TuneDvbPid( const int * pidList, const int * sidList, uint pidCount )
{
}

// ----------------------------------------------------------------------------
// Get the current tuner frequency
//
bool BtDriver_QueryChannel( EPGACQ_TUNER_PAR * pFreqPar, uint * pInput, bool * pIsTuner )
{
   HRESULT hres;
   bool result = FALSE;

   if ((pFreqPar != NULL) && (pInput != NULL) && (pIsTuner != NULL))
   {
      if (shmSlaveMode == FALSE)
      {
         pFreqPar->freq = btCfg.tunerFreq | (btCfg.tunerNorm << 24);
         *pInput = btCfg.inputSrc;

         if (wdmDrvLoaded)
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
         uint lfreq = 0;
         *pInput = EPG_REQ_INPUT_NONE;  // dummy

         if (WintvSharedMem_GetTunerFreq(&lfreq, pIsTuner) &&
             (*pIsTuner) && (lfreq != EPG_REQ_FREQ_NONE))
         {
            pFreqPar->freq = lfreq;
            result = TRUE;
         }
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

// ---------------------------------------------------------------------------
// Set channel priority for next channel change
// - not supported for Windows
//
void BtDriver_SetChannelProfile( VBI_CHANNEL_PRIO_TYPE prio )
{
}

// ---------------------------------------------------------------------------
// Query if acquisition is currently enabled and in which mode
// - return value pointers may be NULL if value not required
//
bool BtDriver_GetState( bool * pEnabled, bool * pHasDriver, uint * pCardIdx )
{
   if (pEnabled != NULL)
      *pEnabled = (shmSlaveMode | wdmDrvLoaded);
   if (pHasDriver != NULL)
      *pHasDriver = wdmDrvLoaded;
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

   if ( (shmSlaveMode == FALSE) && (wdmDrvLoaded == FALSE) )
   {
      // check if the configured card is currently free
      shmSlaveMode = (WintvSharedMem_ReqTvCardIdx(btCfg.sourceIdx, &epgHasDriver) == FALSE);
      if (shmSlaveMode == FALSE)
      {
         if (pVbiBuf != NULL)
         {
            if (btCfg.drvType == BTDRV_SOURCE_WDM)
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
      if (wdmDrvLoaded)
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
   btCfg.tunerNorm  = EPGACQ_TUNER_NORM_PAL;
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
   btCfg.drvType   = BTDRV_SOURCE_NONE;
   btCfg.inputSrc  = INVALID_INPUT_SOURCE;
   WdmSrcCount = CARD_COUNT_UNINITIALIZED;
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
   if (wdmDrvLoaded)
   {  // acq is still running - should never happen
      BtDriver_StopAcq();
   }
   // the WDM driver may be loaded even while acq is disabled (for TV card config dialog)
   BtDriver_WdmDllFree();
}

