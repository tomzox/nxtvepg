/*
 *  WDM driver simulation (dummy driver)
 *
 *  Copyright (C) 1999-2008 T. Zoerner
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
 *    This module is a replacement for the WDM interface DLL
 *    which can be used to debug nxtvepg.
 */

#ifndef WIN32
#error "This module is intended only for Win32 systems"
#else

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF
//#define WDM_DUMMY_DRV

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include <wdmdrv/wdmdrv.h>

#define DUMMY_DEVICE_COUNT 2
#define DUMMY_INPUT_COUNT 2
#define TUNER_INPUT_INDEX 0

static int wdmInitialized = 0;
static int wdmActive = 0;
static int wdmAcqEnabled = 0;

// ---------------------------------------------------------------------------
// Driver initialization.
// Parameters :
//   LogFile : error's log file xx not implemented yet
//     The LogFile parameter tells the driver where to log error messages in
//     case of system error. All "Application level" (such as no video device
//     found) are reported by specific error code by the API.
//     However, more severe errors yield to "WDM_DRV_SYSTEMERR", and the exact
//     error code plus a message can be found in debug output and log file given
//     here. If LogFile parameter == NULL, no loging will be done.
//
// Returns :
//   always S_OK
//
HRESULT APIENTRY InitDrv( void )
{
   wdmInitialized = 1;
   wdmActive = 0;
   wdmAcqEnabled = 0;
   return S_OK;
}

// ---------------------------------------------------------------------------
// Driver Free.
//   This function frees all resources currently owned by the driver.
//
// Returns :
//   always S_OK
HRESULT APIENTRY FreeDrv( void )
{
   wdmActive = 0;
   return S_OK;
}

// ---------------------------------------------------------------------------
// Set CallBack function for sending decoded VBI data to application.
// Be carefull : this function will be called by the DirectShow graph thread.
// So, don't spend too much time in the CallBack. If necessary, copy the data and return
// as quick as possible. However, there is a certain margin with current CPU > 2GHz.
// Parameters :
//   Fct : function to call for VBI data delivery
//   WantRawVBI : the application's callback function want raw VBI data
//     TRUE  : Client application is given the raw VBI data. In this case, the WSTPacket
//             parameter is a pointer to a xx bytes buffer of video data of the VBI line.
//     FALSE : Teletext extraction is done in this DLL. In this case, the WSTPacket
//             parameter is a pointer to a 43 bytes teletext packet.
//
// Returns :
//   S_OK if driver previously initialized.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized.
//
// In order to minimize CallBack definition, the single function defined by SetWSTPacketCallBack
// provides two functionalities :
//   - signaling the beginning of a new video frame
//       In this case, the WSTPacket parameter is NULL, and the called function must return to
//       the driver :
//         * TRUE if app want subsequent VBI data for that frame to be delivered.
//         * FALSE if app want the driver to disguard all VBI data for that frame.
//   - VBI data delivery
//       In this case, the WSTPacket parameter is a pointer to a teletext packet, or raw
//       data depending on WantRawVBI parameter, and the dwLine parameter gives the VBI line
//       number (7 to 23 with PAL/SECAM).
HRESULT APIENTRY SetWSTPacketCallBack(LPFN_CALLBACK pCallBackFunc, BOOL WantRawVBI)
{
   return S_OK;
}

// ---------------------------------------------------------------------------
// Video devices enumeration
// Call this function to enumerate all VDM video capture devices in the system.
// It prepares internal structures for further works with those devices.
// Friendly names of devices can be retrieved by the GetDeviceName function.
// Those names could have been returned here in a strings array, but this DLL is
// developped with the aim of using it with .NET. And passing OUT a strings array
// seems to be difficult (perhaps not possible ?).
// Returns :
//   Number of video capture devices found, 0 if none.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized.
//   WDM_DRV_SYSTEMERR in other cases.
//
HRESULT APIENTRY EnumDevices( void )
{
   HRESULT result;

   if (wdmInitialized)
      result = DUMMY_DEVICE_COUNT;
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

// ---------------------------------------------------------------------------
// Get the friendly name of a video capture device previously detected by EnumDevices
// See EnumDevices comment.
// Parameters :
//   iIndex : index of device from which we want name.
//            range : 0 .. (EnumDevices() - 1)
//   DeviceName : address of caller pointer. Friendly names are allocated here in
//      this DLL, there is no need to copy the string.
//
// Returns :
//   S_OK and *DeviceName pointing to the asked name.
//   WDM_DRV_ERR_DEV_OUTOFRANGE and *DeviceName = NULL if asking for non existant device.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized or EnumDevices not previously called.
//
HRESULT APIENTRY GetDeviceName(int DevNum, char **Name)
{
   static char name_buf[50];
   HRESULT result;

   if (wdmInitialized)
   {
      if (DevNum < DUMMY_DEVICE_COUNT)
      {
         sprintf(name_buf, "WDM dummy device %d", DevNum);
         *Name = name_buf;
         result = S_OK;
      }
      else
      {
         *Name = NULL;
         result = WDM_DRV_ERR_DEV_OUTOFRANGE;
      }
   }
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

// ---------------------------------------------------------------------------
// Select a device in the list built by Enumdevices.
// Everything is done to make this device ready for futher VBI data extraction.
// Parameters :
//   iIndex : Device to select. Range : 0..EnumDevices()
//   iRFTunerInput : Which RF input to use. 0 = Antenna, 1 = Cable.
//
// Successive calls with same index will do nothing and return S_OK.
//
// Returns :
//   S_OK if OK.
//     Along with S_OK, flags are added :
//       * S_OK | WDM_DRV_WARNING_NOSIGNALDETECTION if Hardware has no signal
//         strength detection capability
//       * S_OK | WDM_DRV_WARNING_INUSE if device already used by an other app.
//   WDM_DRV_ERR_NOVBI if VBI output not implemented by hardware device driver. xx to implement
//   Same errors as GetDeviceName otherwise
HRESULT APIENTRY SelectDevice(int DevNum, int iRFTunerInput)
{
   HRESULT result;

   if (wdmInitialized)
   {
      if (DevNum < DUMMY_DEVICE_COUNT)
      {
         result = S_OK;
      }
      else
      {
         result = WDM_DRV_ERR_DEV_OUTOFRANGE;
      }
      wdmActive = 1;
   }
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

// ---------------------------------------------------------------------------
// GetVBISettings :
//
// This function gives VBI parameters for client applications which want to extract
// VBI data themselves.
//
// Returns :
//   S_OK is no error
//   WDM_DRV_UNINITIALIZED if no device previously selected or driver not initialized.
HRESULT APIENTRY GetVBISettings(KS_VBIINFOHEADER *pVBISettings)
{
   memset(pVBISettings, 0, sizeof(KS_VBIINFOHEADER));
   return S_OK;
}

// ---------------------------------------------------------------------------
// GetVideoInputName : two purposes :
//   - Get the number of video input pins for the currently selected device.
//   - Get the name of a given video input pin for the currently selected device.
// For first usage : give iIndex = -1
// For second usage : give iIndex = 0..n
//   As this second form sets *PinName = NULL when Index is out of range, it
//   can also be used to enumerate the number of pins by successive calls.
//
// See EnumDevices comment about using a string array.
// Parameters :
//   iIndex : index of pin from which we want name.
//            range : 0 .. GetVideoInputName(-1)
//      OR    -1 if we want number of video input pins
//   PinName : address of caller pointer. Pin names are allocated here in
//      this DLL, there is no need to copy the string.
//
// Returns :
//   S_OK and *PinName pointing to the asked name.
//   Number of video crossbar inputs if iIndex = -1
//   WDM_DRV_ERR_DEV_OUTOFRANGE and *PinName = NULL if asking for non existant pin.
//   WDM_DRV_UNINITIALIZED if no device not previously selected or driver not initialized.
//
HRESULT APIENTRY GetVideoInputName(int InpNum, char **Name)
{
   static char name_buf[50];
   HRESULT result;

   if (wdmInitialized && wdmActive)
   {
      if (InpNum == -1)
      {
         *Name = NULL;
         result = 2;
      }
      else if (InpNum < DUMMY_INPUT_COUNT)
      {
         sprintf(name_buf, "WDM dummy input %d", InpNum);
         *Name = name_buf;
         result = S_OK;
      }
      else
      {
         *Name = NULL;
         result = WDM_DRV_ERR_DEV_OUTOFRANGE;
      }
   }
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

// ---------------------------------------------------------------------------
// SelectVideoInput
//
// Parameters :
//   iIndex : index of video input pin to select for VBI capture.
//            range : 0 .. GetVideoInputName(-1)
//
// Returns :
//   S_OK if no error.
//   WDM_DRV_ERR_DEV_OUTOFRANGE if asking for non existant pin.
//   WDM_DRV_UNINITIALIZED if no device not previously selected or driver not initialized.
//
HRESULT APIENTRY SelectVideoInput(int iIndex)
{
   HRESULT  result;

   if (wdmInitialized && wdmActive)
   {
      if (iIndex < DUMMY_INPUT_COUNT)
      {
         result = S_OK;
      }
      else
         result = WDM_DRV_ERR_DEV_OUTOFRANGE;
   }
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

// ---------------------------------------------------------------------------
// IsInputATuner : get a boolean to tell if a given input is connected
// to a tuner.
//
// Parameters :
//   iIndex : index of video input pin to select for VBI capture.
//            range : 0 .. GetVideoInputName(-1)
//   pIsTuner : pointer to the boolean to set.
//
// Returns :
//   S_OK if no error.
//   WDM_DRV_ERR_DEV_OUTOFRANGE if asking for non existant pin.
//   WDM_DRV_UNINITIALIZED if no device not previously selected or driver not initialized.
//
HRESULT APIENTRY IsInputATuner(int iIndex, bool *pIsTuner)
{
   HRESULT  result;

   if (wdmInitialized && wdmActive)
   {
      if (iIndex < DUMMY_INPUT_COUNT)
      {
         if (iIndex == TUNER_INPUT_INDEX)
            *pIsTuner = TRUE;
         else
            *pIsTuner = FALSE;

         result = S_OK;
      }
      else
         result = WDM_DRV_ERR_DEV_OUTOFRANGE;
   }
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

// ---------------------------------------------------------------------------
// Change the channel of the currently selected device. Channel to frequecy
// translation is done by DirectShow according to Country Code setting.
// Parameters :
//   Channel : new channel to tune to
//     if Channel == -1, returns Signal reception status without changing channel
//   CountryCode : Country code.
//   TunerMode : TV Norm to use.
//
// Returns :
//   S_OK : everything OK.
//   S_OK | WDM_DRV_WARNING_NOSIGNAL : Channel changed successfully,
//     but no signal received.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized or no device currently selected.
//   WDM_DRV_SYSTEMERR in other cases.
//
HRESULT APIENTRY SetChannel(int Channel, int CountryCode, long TunerMode)
{
   HRESULT  result;

   if (wdmInitialized && wdmActive)
      result = S_OK;
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

// ---------------------------------------------------------------------------
// Check if a TV signal is present for the current channel tuned
// Returns :
//   S_OK : TV signal received on current channel.
//   S_OK | WDM_DRV_WARNING_NOSIGNAL : no signal received.
//   S_OK | WDM_DRV_WARNING_NOSIGNALDETECTION if Hardware has no signal
//         strength detection capability
//   WDM_DRV_UNINITIALIZED if driver not yet initialized or no device currently selected.
//   WDM_DRV_SYSTEMERR in other cases.
HRESULT APIENTRY GetSignalStatus( void )
{
   HRESULT  result;

   if (wdmInitialized && wdmActive && wdmAcqEnabled)
   {
      result = S_OK | WDM_DRV_WARNING_NOSIGNALDETECTION;
   }
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

// ---------------------------------------------------------------------------
// Start VBI Acquisition
// Returns :
//   S_OK : everything OK.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized or no device currently selected
//     or CallBack function not declared to this driver.
//   WDM_DRV_SYSTEMERR in other cases.
//
HRESULT APIENTRY StartAcq( void )
{
   HRESULT  result;

   if (wdmInitialized && wdmActive)
   {
      wdmAcqEnabled = 1;
      result = S_OK;
   }
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

// ---------------------------------------------------------------------------
// Stop VBI Acquisition
// Returns :
//   S_OK : everything OK.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized.
//   WDM_DRV_SYSTEMERR in other cases.
//
HRESULT APIENTRY StopAcq( void )
{
   HRESULT  result;

   if (wdmInitialized && wdmActive)
   {
      wdmAcqEnabled = 0;
      result = S_OK;
   }
   else
      result = WDM_DRV_UNINITIALIZED;

   return result;
}

//BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
//{
//  return TRUE;
//}

WDMDRV_CTL WdmDummyIf =
{
   InitDrv,
   FreeDrv,
   SetWSTPacketCallBack,
   EnumDevices,
   GetDeviceName,
   SelectDevice,
   GetVBISettings,
   GetVideoInputName,
   SelectVideoInput,
   IsInputATuner,
   SetChannel,
   GetSignalStatus,
   StartAcq,
   StopAcq

};

#endif  // not WIN32
