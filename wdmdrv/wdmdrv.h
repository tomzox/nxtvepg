/*
 *  Declaration of interface to WDM
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
 *    This header file declares the interface to the WDM VBI
 *    capture library.
 *
 *
 *  Author: Gérard Chevalier
 *
 *  $Id: wdmdrv.h,v 1.5 2004/04/26 21:11:01 tom Exp tom $
 */

#ifndef __WDMDRV_H
#define __WDMDRV_H

// ----------------------------------------------------------------------------
// Result constants
//
#define WDM_DRV_SYSTEMERR -1
#define WDM_DRV_UNINITIALIZED -2
#define WDM_DRV_ERR_DEV_OUTOFRANGE -3
#define WDM_DRV_ERR_NO_VBIPIN -4

#define WDM_DRV_WARNING_NOSIGNAL 1
#define WDM_DRV_WARNING_NOSIGNALDETECTION 2
#define WDM_DRV_WARNING_INUSE 4
#define WDM_DRV_OK(X)  ((X) >= S_OK)

// ----------------------------------------------------------------------------
// Video Standards definition
//
typedef enum tagAnalogVideoStandard
{
    AnalogVideo_None       = 0x00000000,
    AnalogVideo_NTSC_M     = 0x00000001,
    AnalogVideo_NTSC_M_J   = 0x00000002,
    AnalogVideo_NTSC_433   = 0x00000004,
    AnalogVideo_PAL_B      = 0x00000010,
    AnalogVideo_PAL_D      = 0x00000020,
    AnalogVideo_PAL_H      = 0x00000080,
    AnalogVideo_PAL_I      = 0x00000100,
    AnalogVideo_PAL_M      = 0x00000200,
    AnalogVideo_PAL_N      = 0x00000400,
    AnalogVideo_PAL_60     = 0x00000800,
    AnalogVideo_SECAM_B    = 0x00001000,
    AnalogVideo_SECAM_D    = 0x00002000,
    AnalogVideo_SECAM_G    = 0x00004000,
    AnalogVideo_SECAM_H    = 0x00008000,
    AnalogVideo_SECAM_K    = 0x00010000,
    AnalogVideo_SECAM_K1   = 0x00020000,
    AnalogVideo_SECAM_L    = 0x00040000,
    AnalogVideo_SECAM_L1   = 0x00080000,
    AnalogVideo_PAL_N_COMBO = 0x00100000
} AnalogVideoStandard;

// ----------------------------------------------------------------------------
// Struct passed between driver and application for VBI parameters obtention
// with GetVBISettings function (from the DirectShow SDK).
//
typedef struct
{
    ULONG       StartLine;              // inclusive
    ULONG       EndLine;                // inclusive
    ULONG       SamplingFrequency;      // Hz.
    ULONG       MinLineStartTime;       // microSec * 100 from HSync LE
    ULONG       MaxLineStartTime;       // microSec * 100 from HSync LE
    ULONG       ActualLineStartTime;    // microSec * 100 from HSync LE
    ULONG       ActualLineEndTime;      // microSec * 100 from HSync LE
    ULONG       VideoStandard;          // KS_AnalogVideoStandard*
    ULONG       SamplesPerLine;
    ULONG       StrideInBytes;          // May be > SamplesPerLine
    ULONG       BufferSize;             // Bytes
} KS_VBIINFOHEADER;

// ----------------------------------------------------------------------------
// Definition of callback function called by the WDM VBI Driver
// In order to minimize CallBack definition, the single function defined by SetWSTPacketCallBack
// provides two functionalities :
//   - signaling the beginning of a new frame
//       In this case, the WSTPacket parameter is NULL, and we must return to the driver :
//         * TRUE if we want subsequent VBI data for that frame to be delivered.
//         * FALSE if we want the drive to disguard all VBI data for that frame.
//   - VBI data delivery
//       In this case, the WSTPacket parameter is a pointer to a 43 bytes teletext packet,
//       and the dwLine parameter gives the VBI line number (7 to 23 with PAL/SECAM)
//
typedef BOOL (__stdcall *LPFN_CALLBACK)(BYTE *WSTPacket, LONGLONG dwLine);


// ----------------------------------------------------------------------------
// Declaration of DLL API
//
typedef struct
{
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
   HRESULT (WINAPI *InitDrv)();

   // ---------------------------------------------------------------------------
   // Driver Free.
   //   This function frees all resources currently owned by the driver.
   //
   // Returns :
   //   always S_OK
   HRESULT (WINAPI *FreeDrv)();

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
   HRESULT (WINAPI *SetWSTPacketCallBack)(LPFN_CALLBACK pCallBackFunc, BOOL WantRawVBI);

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
   HRESULT (WINAPI *EnumDevices)();

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
   HRESULT (WINAPI *GetDeviceName)(int DevNum, char **Name);

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
   HRESULT (WINAPI *SelectDevice)(int DevNum, int iRFTunerInput);

   // ---------------------------------------------------------------------------
   // GetVBISettings :
   //
   // This function gives VBI parameters for client applications which want to extract
   // VBI data themselves.
   //
   // Returns :
   //   S_OK is no error
   //   WDM_DRV_UNINITIALIZED if no device previously selected or driver not initialized.
   HRESULT (WINAPI *GetVBISettings)(KS_VBIINFOHEADER *pVBISettings);

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
   HRESULT (WINAPI *GetVideoInputName)(int InpNum, char **Name);

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
   HRESULT (WINAPI *SelectVideoInput)(int iIndex);

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
   HRESULT (WINAPI *IsInputATuner)(int iIndex, bool *pIsTuner);

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
   HRESULT (WINAPI *SetChannel)(int Channel, int CountryCode, long TunerMode);

   // ---------------------------------------------------------------------------
   // Check if a TV signal is present for the current channel tuned
   // Returns :
   //   S_OK : TV signal received on current channel.
   //   S_OK | WDM_DRV_WARNING_NOSIGNAL : no signal received.
   //   S_OK | WDM_DRV_WARNING_NOSIGNALDETECTION if Hardware has no signal
   //         strength detection capability
   //   WDM_DRV_UNINITIALIZED if driver not yet initialized or no device currently selected.
   //   WDM_DRV_SYSTEMERR in other cases.
   HRESULT (WINAPI *GetSignalStatus)();

   // ---------------------------------------------------------------------------
   // Start VBI Acquisition
   // Returns :
   //   S_OK : everything OK.
   //   WDM_DRV_UNINITIALIZED if driver not yet initialized or no device currently selected
   //     or CallBack function not declared to this driver.
   //   WDM_DRV_SYSTEMERR in other cases.
   //
   HRESULT (WINAPI *StartAcq)();

   // ---------------------------------------------------------------------------
   // Stop VBI Acquisition
   // Returns :
   //   S_OK : everything OK.
   //   WDM_DRV_UNINITIALIZED if driver not yet initialized.
   //   WDM_DRV_SYSTEMERR in other cases.
   //
   HRESULT (WINAPI *StopAcq)();

} WDMDRV_CTL;

#endif  // __WDMDRV_H
