/*
 *  VBIAcqWDMDrv : WDM module for VBI data acquisiton and Teletext decoding
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
 * This "Driver" is a DirectShow program that extracts Teletext data from the
 * VBI Output of a video capture device.
 * This code is derived from :
 *   DirectX 9 SDK amcap sample app
 *   nxtvepg for the Teletext data demodulation from VBI lines
 *     (http://nxtvepg.sourceforge.net)
 * It uses :
 *   The DirectShow filters provided by the TV card's manufacturer.
 *   The SampleGrabber filter from the SDK associated with the NULL renderer.
 *
 * Other sources :
 *   DScaler project : for signal detection substitution by get_HorizontalLocked
 *
 *  Author: Gérard Chevalier
 *
 * Files
 *
 *   VBIAcqWDMDrv.cpp			Main program
 *   CCrossbar.cpp & .h			Crossbar management
 *   SampleGrabberCB.cpp & .h	CallBack object used with the SampleGrabber
 *   WDMDRV.h					Error codes
 *
 */

#define REGISTER_FILTERGRAPH

#include <streams.h>
#include <commctrl.h>
#include <initguid.h>
#include <stdio.h>
#include <atlbase.h>
#include <wxdebug.h>
#include <ks.h>
#include <ksmedia.h>
#include <qedit.h>

#include "CCrossbar.h"
#include "SampleGrabberCB.h"
#include "CVBIFrameFIFO.h"
#include "WDMDRV.h"

//------------------------------------------------------------------------------
// Global data
//------------------------------------------------------------------------------
DWORD g_dwGraphRegister=0;	// For registering graph for graphedit
BOOL g_bDriverInitialized = FALSE;
#ifdef _DEBUG
BOOL FakeTimestamp, DumpTimestamp, DumpVBIData;
int MaxFramesDumped;
BOOL DebugPreview;
long ForceTVNorm;
char ForceRFInput;
int ForceCountryCode, ForceChannel;
HANDLE VBIDumpFileHANDLE;
//int DebugLevel = 0;
#endif

struct _capstuff {
    ICaptureGraphBuilder2 *pBuilder;	// graph builder object
    IVideoWindow *pVW;
    IBaseFilter *pVCap;		// Current Video Capture Filter
	IBaseFilter *pSGFilter;	// Sample Grabber Filter for VBI Acq
	ISampleGrabber *pSampleGrabber;	// SampleGrabber Interface (not released until driver free)
	IBaseFilter *pNRFilter;	// Null Renderer Filter for VBI Acq
	IBaseFilter *pSurfFilter;	// Video Surface Allocator
	IBaseFilter* pKernelTee;	// Pointer to Kernel Tee filter
	IPin *pVBIPin, *pVPVBIPin;
    IAMVideoCompression *pVC;
    IGraphBuilder *pFg;		// Filter Graph
    BOOL fGraphBuilt;
    BOOL fGraphRunning;
    BOOL VBIAvail;
    IMoniker *pmAllVideoDevices[10];	// Monikers to all Video Devices found (not released until driver free)
    IMoniker *pmVideo;		// Moniker to current Video Devices
    BOOL fWantPreview;
    char devicesFriendlyNames[5][MAX_PATH];
	char *currentDeviceInputsNames[10];
	bool isInputaTuner[10];
    CCrossbar *pCrossbar;
    LONG NumberOfVideoInputs;
    int iNumVCapDevices;		// Number of capture devices found
	int iCurrentDevSelected;	// Currently selected device by SelectDevice function
    IAMVfwCaptureDialogs *pDlg;
	int iSignalDetectMethod;
	// Interfaces to TVTuner & VideoDecoder (not released until driver free)
	IAMTVTuner *pTV;
	IAMAnalogVideoDecoder *pAD;
	// Following are tuning data :
	long tunerInputType;	// Cable or antena
	long tunerMode; // Currently supported PAL & SECAM
	int tunerChannel;
	int tunerCountryCode;
} gcap;

extern CSampleGrabberCB CB;
extern CVBIFrameFIFO VBIFrameFIFO;
char ErrMsgStringBuffer[2048]={0};

void CleanAll(bool KeepEnumeratedVideoMonikers);
HRESULT AddVBIFilters();
void ErrMsg(LPTSTR sz,...);
HRESULT InitCapFilters();
HRESULT StopAcq();
BOOL CheckInit(char *sContext, int iLevel);

#ifdef _DEBUG
char *GetOSName();
#endif

// ---------------------------------------------------------------------------
// Funtion to adds/removes a DirectShow filter graph from the Running Object Table,
// allowing GraphEdit to "spy" on a remote filter graph if enabled.
HRESULT AddGraphToRot(IUnknown *pUnkGraph, DWORD *pdwRegister);
void RemoveGraphFromRot(DWORD pdwRegister);

// Local defines
#define SAFE_RELEASE(x) { if (x) x->Release(); x = NULL; }
// Values for gcap.iSignalDetectMethod
#define NO_DETECTION	0
#define SIGNAL_PRESENT	1
#define HSYNC_LOCKED	2

// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	//DbgSetModuleLevel(LOG_TRACE, 1);
	//DbgLog((LOG_TRACE,1,TEXT("WDMDrv DLLMain")));
	return TRUE;
}

/*****************************************************************************
 *	           Begining of the exported functions section                    *
 *****************************************************************************/

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
HRESULT InitDrv() {// xx implement : char *LogFile
	DbgSetModuleLevel(LOG_TRACE, 2);
    gcap.iNumVCapDevices = -1;
	gcap.iCurrentDevSelected = -1;
	gcap.pBuilder = NULL;
	//CB.FctToCall = NULL;
	VBIFrameFIFO.m_FctToCall = NULL;

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED );
	DbgLog((LOG_TRACE,1,TEXT("CoInitialize returned %LX"), hr));

	g_bDriverInitialized = TRUE;

	DbgLog((LOG_TRACE,1,TEXT("WDM Driver Initialized")));

#ifdef _DEBUG
	// Put OS type in the trace
	DbgLog((LOG_TRACE,1,TEXT("OS detected : %s"), GetOSName()));

	// Getting a debug parameter level to know what to do
	//HANDLE hDebugParamFile;
	//DWORD nBytesRead;
	char CharBuff[15], *CharPtr;

	/*
	hDebugParamFile = CreateFile("DebugParams.txt", GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDebugParamFile == INVALID_HANDLE_VALUE) {
		DbgLog((LOG_TRACE,1,TEXT("No DebugParams.txt file")));
	} else {
		ReadFile(hDebugParamFile, CharBuff, 15, &nBytesRead, NULL);
		CloseHandle(hDebugParamFile);
		if (nBytesRead != 0) {
			int ConvRes = sscanf(CharBuff, "%i", &DebugLevel);
			if (ConvRes == 0 || ConvRes == EOF)
				DbgLog((LOG_TRACE,1,TEXT("Bad Debug Level in DebugParams.txt file")));
		} else
			DbgLog((LOG_TRACE,1,TEXT("No Debug Level in DebugParams.txt file")));
	}

	if (DebugLevel == 0)
		DbgLog((LOG_TRACE,1,TEXT("    Will have default behavior")));
	else
		DbgLog((LOG_TRACE,1,TEXT("Debug behavior parameter set to %d"), DebugLevel));
	*/
	GetPrivateProfileString("Debug Behaviour", "FakeTimestamp", "n", CharBuff, 15,
		".\\VBIAcqWDMDrv.ini");
	FakeTimestamp = CharBuff[0] == 'y';
	DbgLog((LOG_TRACE,1,TEXT("FakeTimestamp set to %d"), FakeTimestamp));

	GetPrivateProfileString("Debug Behaviour", "DumpTimestamp", "n", CharBuff, 15,
		".\\VBIAcqWDMDrv.ini");
	DumpTimestamp = CharBuff[0] == 'y';
	DbgLog((LOG_TRACE,1,TEXT("DumpTimestamp set to %d"), DumpTimestamp));

	GetPrivateProfileString("Debug Behaviour", "DumpVBIData", "n", CharBuff, 15,
		".\\VBIAcqWDMDrv.ini");
	DumpVBIData = CharBuff[0] == 'y';
	DbgLog((LOG_TRACE,1,TEXT("DumpVBIData set to %d"), DumpVBIData));

	GetPrivateProfileString("Debug Behaviour", "DebugPreview", "n", CharBuff, 15,
		".\\VBIAcqWDMDrv.ini");
	DebugPreview = CharBuff[0] == 'y';
	DbgLog((LOG_TRACE,1,TEXT("DebugPreview set to %d"), DebugPreview));

	GetPrivateProfileString("Debug Behaviour", "ForceTVNorm", "n", CharBuff, 15,
		".\\VBIAcqWDMDrv.ini");
	ForceTVNorm = strtol(CharBuff, &CharPtr, 16);
	DbgLog((LOG_TRACE,1,TEXT("ForceTVNorm set to 0x%lx"), ForceTVNorm));

	GetPrivateProfileString("Debug Behaviour", "ForceRFInput", "n", CharBuff, 15,
		".\\VBIAcqWDMDrv.ini");
	ForceRFInput = CharBuff[0];
	DbgLog((LOG_TRACE,1,TEXT("ForceRFInput set to %c"), ForceRFInput));

	GetPrivateProfileString("Debug Behaviour", "ForceCountryCode", "n", CharBuff, 15,
		".\\VBIAcqWDMDrv.ini");
	ForceCountryCode = atoi(CharBuff);
	DbgLog((LOG_TRACE,1,TEXT("ForceCountryCode set to %d"), ForceCountryCode));

	GetPrivateProfileString("Debug Behaviour", "ForceChannel", "n", CharBuff, 15,
		".\\VBIAcqWDMDrv.ini");
	ForceChannel = atoi(CharBuff);
	DbgLog((LOG_TRACE,1,TEXT("ForceChannel set to %d"), ForceChannel));

	if (DumpVBIData) {
		VBIDumpFileHANDLE = CreateFile("VBIDump.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (VBIDumpFileHANDLE == INVALID_HANDLE_VALUE) {
			DbgLog((LOG_TRACE,1,TEXT("### Cannot create VBIDump.txt file !")));
			DumpVBIData = FALSE;
		}
		MaxFramesDumped = 50;
	}
#endif

	return S_OK;
}

// ---------------------------------------------------------------------------
// Driver Free.
//   This function frees all resources currently owned by the driver.
//
// Returns :
//   always S_OK
HRESULT FreeDrv() {
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv FreeDrv")));

	if(gcap.fGraphRunning)
		StopAcq();

	CleanAll(FALSE);

	CoUninitialize();

	g_bDriverInitialized = FALSE;

#ifdef _DEBUG
	if (DumpVBIData)
		CloseHandle(VBIDumpFileHANDLE);
#endif

	return S_OK;
}

// ---------------------------------------------------------------------------
// Set CallBack function for sending decoded VBI data to application.
// This function will be called by a separate dedicated thread.
// Don't spend too much time in the CallBack, otherwise data loass may occur.
// However, there is a certain margin with current CPU > 2GHz.
// Parameters :
//   Fct : function to call for VBI data delivery
//   WantRawVBI : the application's callback function want raw VBI data
//     TRUE  : Client application is given the raw VBI data. In this case, the WSTPacket
//             parameters include a pointer to a bytes buffer of video data of the VBI
//             line (length of buffer given by GetVBISettings), plus time stamps and
//             interlace status.
//     FALSE : Teletext extraction is done in this DLL. In this case, the WSTPacket
//             parameter is a pointer to a 43 bytes teletext packet.
//             NOTE : this mode is no more supported !
//
// Returns :
//   S_OK if driver previously initialized.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized.
//
// When WantRawVBI is FALSE :
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
HRESULT SetWSTPacketCallBack(LPFN_CALLBACK Fct, BOOL WantRawVBI) {
	if (Fct == NULL) {
		IMediaControl *pMC = NULL;
		DbgLog((LOG_TRACE,1,TEXT("WDMDrv Canceling CallBack")));
		// xx NON garder pour + tard ...CB.FctToCall = NULL;
		HRESULT hr = gcap.pFg->QueryInterface(IID_IMediaControl, (void **)&pMC);
		if(SUCCEEDED(hr)) {
			hr = gcap.pSampleGrabber->SetCallback(NULL, 0);
			if(hr != S_OK) {
				ErrMsg(TEXT("Cannot cancel Sample Grabber CallBack : %LX"), hr);
				pMC->Release();
				return WDM_DRV_SYSTEMERR;
			}
			pMC->Release();
			return S_OK;
		}
	}

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv CallBack set to %x"), Fct));

	if (!CheckInit("SetWSTPacketCallBack", 0)) return WDM_DRV_UNINITIALIZED;

	VBIFrameFIFO.m_FctToCall = Fct;
	VBIFrameFIFO.m_WantRawVBIData = WantRawVBI;
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
HRESULT EnumDevices() {
    UINT    uNumDevices = 0;
    HRESULT hr;

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv EnumDevices")));

	if (!CheckInit("EnumDevices", 0)) return WDM_DRV_UNINITIALIZED;

	// enumerate all video capture devices
    ICreateDevEnum *pCreateDevEnum = 0;
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                          IID_ICreateDevEnum, (void**)&pCreateDevEnum);
    if(hr != S_OK) {
		ErrMsg(TEXT("Error Creating Device Enumerator : %LX"), hr);
        return WDM_DRV_SYSTEMERR;
    }

    IEnumMoniker *pEm=0;
    hr = pCreateDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEm, 0);
	pCreateDevEnum->Release();
    if(hr != S_OK) {
		DbgLog((LOG_TRACE,1,TEXT("  No video capture HW found")));
		return 0;
    }

    pEm->Reset();
    ULONG cFetched;
    IMoniker *pM;

    while(hr = pEm->Next(1, &pM, &cFetched), hr==S_OK) {
        IPropertyBag *pBag=0;

        hr = pM->BindToStorage(0, 0, IID_IPropertyBag, (void **)&pBag);
		if(!SUCCEEDED(hr)) {
			ErrMsg(TEXT("WDMDrv Error Binding IID_IPropertyBag : %LX"), hr);
			pM->Release();
			pEm->Release();
			pBag->Release();
			return WDM_DRV_SYSTEMERR;
		}

		VARIANT var;
		var.vt = VT_BSTR;
		WCHAR buf[MAX_PATH];
		hr = pBag->Read(L"FriendlyName", &var, NULL);
		if(hr == S_OK) {
			wcscpy(buf, var.bstrVal);
			WideCharToMultiByte(CP_ACP, 0, buf, -1,
				gcap.devicesFriendlyNames[uNumDevices], MAX_PATH,  NULL, NULL);

			DbgLog((LOG_TRACE,1,TEXT("  Found device : %s"),
				(char *)gcap.devicesFriendlyNames[uNumDevices]));
			SysFreeString(var.bstrVal);
			gcap.pmAllVideoDevices[uNumDevices] = pM;	// No release, we keep it
			CLSID Classouxx;
			pM->GetClassID(&Classouxx);
			
				DbgLog((LOG_TRACE,1,TEXT("  CLSID : %LX,%LX,%LX,%LX"),
				Classouxx.Data1,Classouxx.Data2,Classouxx.Data3,Classouxx.Data4));
		}
		//---------------------------
		hr = pBag->Read(L"DevicePath", &var, NULL);
		if(hr == S_OK) {
			char BufChar[260];
			wcscpy(buf, var.bstrVal);
			WideCharToMultiByte(CP_ACP, 0, buf, -1,
				BufChar, MAX_PATH,  NULL, NULL);

			DbgLog((LOG_TRACE,1,TEXT("  DevicePath is : %s"),
				BufChar));
			SysFreeString(var.bstrVal);
		}
		hr = pBag->Read(L"Description", &var, NULL);
		if(hr == S_OK) {
			char BufChar[260];
			wcscpy(buf, var.bstrVal);
			WideCharToMultiByte(CP_ACP, 0, buf, -1,
				BufChar, MAX_PATH,  NULL, NULL);

			DbgLog((LOG_TRACE,1,TEXT("  Description is : %s"),
				BufChar));
			SysFreeString(var.bstrVal);
		}
		//---------------------------

		pBag->Release();
		uNumDevices++;
    }
    pEm->Release();

    gcap.iNumVCapDevices = uNumDevices;
	return uNumDevices;
}

// ---------------------------------------------------------------------------
// Get the friendly name of a video capture device previously detected by EnumDevices
// See EnumDevices comment.
// Parameters :
//   iIndex : index of device from which we want name.
//            range : 0 .. EnumDevices()
//   DeviceName : address of caller pointer. Friendly names are allocated here in
//      this DLL, there is no need to copy the string.
//
// Returns :
//   S_OK and *DeviceName pointing to the asked name.
//   WDM_DRV_ERR_DEV_OUTOFRANGE and *DeviceName = NULL if asking for non existant device.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized or EnumDevices not previously called.
HRESULT GetDeviceName(int iIndex, char **DeviceName) {
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv GetDeviceName")));

	if (!CheckInit("GetDeviceName", 1)) return WDM_DRV_UNINITIALIZED;

	if (iIndex < 0 || iIndex >= gcap.iNumVCapDevices) {
		*DeviceName = NULL;
		return WDM_DRV_ERR_DEV_OUTOFRANGE;
	}
	*DeviceName = gcap.devicesFriendlyNames[iIndex];
	return S_OK;
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
//   WDM_DRV_ERR_NO_VBIPIN if VBI output not implemented by hardware device driver.
//   Same errors as GetDeviceName otherwise
HRESULT SelectDevice(int iIndex, int iRFTunerInput) {
	HRESULT hr;

	if (!CheckInit("SelectDevice", 1)) return WDM_DRV_UNINITIALIZED;

	if (iIndex < 0 || iIndex >= gcap.iNumVCapDevices)
		return WDM_DRV_ERR_DEV_OUTOFRANGE;

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv SelectDevice(%d)"), iIndex));
#ifdef _DEBUG
	gcap.fWantPreview = DebugPreview;
#else
	gcap.fWantPreview = FALSE;	// May be given by parameter in the future ?
#endif

	// No Device change while we're running
	if(gcap.fGraphRunning) {
        ErrMsg(TEXT("Asked Device selection while the graph is running !"));
		return WDM_DRV_SYSTEMERR;
	}

	if(gcap.iCurrentDevSelected == iIndex)
		return S_OK;

	// Else : new device choosen. Clean previous stuff and rebuild the graph.
	if(gcap.fGraphRunning)
		StopAcq();

	CleanAll(TRUE);

	gcap.iCurrentDevSelected = iIndex;
	IMoniker *pmVideo = gcap.pmAllVideoDevices[iIndex];
	gcap.pmVideo = pmVideo;

	if ((hr = InitCapFilters()) != S_OK) return hr;

	if ((hr = AddVBIFilters()) != S_OK) return hr;

	// Getting IID_IAMTVTuner interface to query for device use and SignalPresent
	// detection capability. We don't release this interface for further usage.
	DbgLog((LOG_TRACE,2,TEXT("WDMDrv getting IAMTVTuner interface with MEDIATYPE_Video")));
	hr = gcap.pBuilder->FindInterface(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video,
		gcap.pVCap, IID_IAMTVTuner, (void **)&gcap.pTV);

	if (hr == E_NOINTERFACE) {
		DbgLog((LOG_TRACE,2,TEXT("  Not found, trying with MEDIATYPE_Interleaved")));
		hr = gcap.pBuilder->FindInterface(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Interleaved,
			gcap.pVCap, IID_IAMTVTuner, (void **)&gcap.pTV);
	}

	if (hr == E_NOINTERFACE) {
		DbgLog((LOG_TRACE,2,TEXT("    Still not found, trying with NULL, NULL")));
		hr = gcap.pBuilder->FindInterface(NULL, NULL, gcap.pVCap,
			IID_IAMTVTuner, (void **)&gcap.pTV);
	}

	if (hr == E_NOINTERFACE) {
		// Can't find AMTVTuner interface with any method. Is there really a tuner ?
		ErrMsg(TEXT("Cannot get IAMTVTuner Interface for Capture Device. Is this really a TV card ?"));
		CleanAll(TRUE);
		return WDM_DRV_SYSTEMERR;
	}

	// If we fail getting IID_IAMTVTuner interface, that mean that the tuner is
	// currently used by another app. xx not sure ??
	if(hr != S_OK) {
		// Here, we have E_FAIL (0x80004005L), means probably that TV Tuner is in use
		ErrMsg(TEXT("Error getting IAMTVTuner Interface for Capture Device : %LX"), hr);
		CleanAll(TRUE);
		return S_OK | WDM_DRV_WARNING_INUSE;
	}

	// Getting IID_IAMAnalogVideoDecoder interface. We don't release this interface
	// for further usage.
	hr = gcap.pBuilder->FindInterface(&PIN_CATEGORY_CAPTURE,
		&MEDIATYPE_Video, gcap.pVCap,
		IID_IAMAnalogVideoDecoder, (void **)&gcap.pAD);

	if(!gcap.pAD) {
		gcap.pTV->Release();
		ErrMsg(TEXT("Cannot find IAMAnalogVideoDecoder Interface for Capture Device : %LX"),
			hr);
		return WDM_DRV_SYSTEMERR;
	}

	// Calling SignalPresent just to know if HW can detect signal
	long lSignalStrength;
	gcap.pTV->SignalPresent(&lSignalStrength);
	if (lSignalStrength == AMTUNER_HASNOSIGNALSTRENGTH) {
		// Selected device cannot detect signal, trying HSyncLocked method.
		// Hopefully this will work properly most of the time (from DScaler).
		DbgLog((LOG_TRACE,1,TEXT("WDMDrv : This device cannot report SignalPresent")));
		hr = gcap.pAD->get_HorizontalLocked(&lSignalStrength);
		if (hr != S_OK) {
			gcap.iSignalDetectMethod = NO_DETECTION;
			DbgLog((LOG_TRACE,1,TEXT("  and get_HorizontalLocked do not implemented too")));
			hr = S_OK | WDM_DRV_WARNING_NOSIGNALDETECTION;
		} else {
			// get_HorizontalLocked OK : reports it's status
			DbgLog((LOG_TRACE,1,TEXT("  but get_HorizontalLocked is OK")));
			gcap.iSignalDetectMethod = HSYNC_LOCKED;
			hr = S_OK;
		}
	} else {
		gcap.iSignalDetectMethod = SIGNAL_PRESENT;
		DbgLog((LOG_TRACE,1,TEXT("WDMDrv : This device can report SignalPresent")));
		hr = S_OK;
	}

	// Reset Tuning data
	gcap.tunerMode = -1;
	gcap.tunerCountryCode = -1;

	// Alway use tuning space ZERO 
	gcap.pTV->put_TuningSpace(0);

	// RF Input setting
#ifdef _DEBUG
	if (ForceRFInput == 'c')
		iRFTunerInput = 1;
	else if (ForceRFInput == 'a')
		iRFTunerInput = 0;
#endif
	gcap.tunerInputType = iRFTunerInput;
	if(gcap.tunerInputType == 0)
		gcap.pTV->put_InputType(0, TunerInputAntenna);
	else
		gcap.pTV->put_InputType(0, TunerInputCable);

	return hr;
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
HRESULT GetVBISettings(KS_VBIINFOHEADER *pVBISettings) {
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv GetVBISettings")));

	if (!CheckInit("GetVBISettings", 2)) return WDM_DRV_UNINITIALIZED;

	*pVBISettings = CB.m_VBIIH;
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
//   WDM_DRV_UNINITIALIZED if no device previously selected or driver not initialized.
HRESULT GetVideoInputName(int iIndex, char **PinName) {
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv GetVideoInputName")));

	if (!CheckInit("GetVideoInputName", 2)) return WDM_DRV_UNINITIALIZED;

	if (iIndex == -1) {
		*PinName = NULL;
		return gcap.NumberOfVideoInputs;
	}

	if (iIndex < 0 || iIndex >= gcap.NumberOfVideoInputs) {
		*PinName = NULL;
		return WDM_DRV_ERR_DEV_OUTOFRANGE;
	}
	*PinName = gcap.currentDeviceInputsNames[iIndex];
	return S_OK;
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
//   WDM_DRV_SYSTEMERR in other cases.
HRESULT SelectVideoInput(int iIndex) {
	HRESULT hr;
	static CurrentInput = -1;
	// Un-necessary calls filtered to avoid annoying "clocs" in speakers due to hardware
	// swithing in the crossbar's audio section.
	if (iIndex == CurrentInput) return S_OK;
	CurrentInput = iIndex;

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv SelectVideoInput (%d)"), iIndex));

	if (!CheckInit("SelectVideoInput", 2)) return WDM_DRV_UNINITIALIZED;

	if (iIndex < 0 || iIndex >= gcap.NumberOfVideoInputs)
		return WDM_DRV_ERR_DEV_OUTOFRANGE;

	hr = gcap.pCrossbar->SetInputIndex(iIndex, !gcap.fWantPreview);
	if (hr != S_OK) {
		ErrMsg(TEXT("Cannot set video input"));
		CleanAll(TRUE);
		return WDM_DRV_SYSTEMERR;
	}
	return S_OK;
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
HRESULT IsInputATuner(int iIndex, bool *pIsTuner) {

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv IsInputATuner (%d)"), iIndex));

	if (!CheckInit("IsInputATuner", 2)) return WDM_DRV_UNINITIALIZED;

	if (iIndex < 0 || iIndex >= gcap.NumberOfVideoInputs)
		return WDM_DRV_ERR_DEV_OUTOFRANGE;

	*pIsTuner = gcap.isInputaTuner[iIndex];
	return S_OK;
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
//   WDM_DRV_UNINITIALIZED if driver not yet initialized or no device currently selected.
//   WDM_DRV_SYSTEMERR in other cases.
HRESULT SetChannel(int Channel, int CountryCode, long TunerMode) {

	if (!CheckInit("SetChannel", 2)) return WDM_DRV_UNINITIALIZED;

#ifdef _DEBUG
	if (ForceChannel != 0)
		Channel = ForceChannel;
	if (ForceCountryCode != 0)
		CountryCode = ForceCountryCode;
	if (ForceTVNorm != 0)
		TunerMode = ForceTVNorm;
#endif

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv : SetChannel to %d, Country to %d, norm = 0x%x"), 
		Channel, CountryCode, TunerMode));
	// Autotune xx.

	// Setting CountryCode & TunerMode only if changed from current value.
	if (TunerMode != gcap.tunerMode) {
		gcap.pAD->put_TVFormat(TunerMode);
		gcap.tunerMode = TunerMode;
	}

	if (CountryCode != gcap.tunerCountryCode) {
		gcap.pTV->put_CountryCode(CountryCode);
		gcap.tunerCountryCode = CountryCode;
	}

	// Change the channel
	gcap.pTV->put_Channel(Channel, AMTUNER_SUBCHAN_NO_TUNE, AMTUNER_SUBCHAN_NO_TUNE);

	return S_OK;
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
HRESULT GetSignalStatus() {
	HRESULT hr;

	if (!CheckInit("GetSignalStatus", 2)) return WDM_DRV_UNINITIALIZED;

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv GetSignalStatus")));

	// Getting signal status with the avalable method.
	switch (gcap.iSignalDetectMethod) {
		case NO_DETECTION :
			// As we don't know, we return OK for signal present. The app will time-out
			// waiting data. But we report again WDM_DRV_WARNING_NOSIGNALDETECTION
			hr = S_OK | WDM_DRV_WARNING_NOSIGNALDETECTION;
			break;

		case SIGNAL_PRESENT :
			long lSignalStrength;
			gcap.pTV->SignalPresent(&lSignalStrength);
			if (lSignalStrength == AMTUNER_SIGNALPRESENT)
				hr = S_OK;
			else
				hr = S_OK | WDM_DRV_WARNING_NOSIGNAL;
			break;

		case HSYNC_LOCKED :
			gcap.pAD->get_HorizontalLocked(&lSignalStrength);
			hr = (lSignalStrength == 1 ? S_OK : S_OK | WDM_DRV_WARNING_NOSIGNAL);
			break;
	}
	return hr;
}

// ---------------------------------------------------------------------------
// Start VBI Acquisition
// Returns :
//   S_OK : everything OK.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized or no device currently selected
//     or CallBack function not declared to this driver.
//   WDM_DRV_SYSTEMERR in other cases.
HRESULT StartAcq() {
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv StartAcq")));

	if (!CheckInit("StartAcq", 4)) return WDM_DRV_UNINITIALIZED;

	// If already running, return OK silently
	if(gcap.fGraphRunning)
		return S_OK;

	// Run the graph
	IMediaControl *pMC = NULL;
	HRESULT hr = gcap.pFg->QueryInterface(IID_IMediaControl, (void **)&pMC);
	if(SUCCEEDED(hr)) {
		// Initialize the FIFO and the callback thread
		VBIFrameFIFO.InitializeFIFO(2, (CB.m_VBIIH.EndLine - CB.m_VBIIH.StartLine + 1) *
			CB.m_VBIIH.SamplesPerLine);
		// Setting the Callback on Sample Grabber
		DbgLog((LOG_TRACE,1,TEXT("WDMDrv : Setting CallBack for Sample Grabber Filter")));
		hr = gcap.pSampleGrabber->SetCallback(&CB, 0);
		if(hr != S_OK) {
			ErrMsg(TEXT("Cannot set Sample Grabber CallBack : %LX"), hr);
			pMC->Release();
			return WDM_DRV_SYSTEMERR;
		}

		// Everything ready to run
        hr = pMC->Run();
        if(FAILED(hr)) {
            // stop parts that ran
            pMC->Stop();
        } else
			DbgLog((LOG_TRACE,1,TEXT("WDMDrv : The Graph is now running")));

        pMC->Release();
    }
    if(FAILED(hr)) {
        ErrMsg(TEXT("Error %LX: Cannot run the graph for VBI acquisition"), hr);
        return WDM_DRV_SYSTEMERR;
    }

    gcap.fGraphRunning = TRUE;
    return S_OK;
}


// ---------------------------------------------------------------------------
// Stop VBI Acquisition
// Returns :
//   S_OK : everything OK.
//   WDM_DRV_UNINITIALIZED if driver not yet initialized.
//   WDM_DRV_SYSTEMERR in other cases.
HRESULT StopAcq() {
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv StopAcq")));

	//xxif (!CheckInit("StopAcq", 4)) return WDM_DRV_UNINITIALIZED;

	// If already stopped, return OK silently
    if(!gcap.fGraphRunning)
        return S_OK;

	// Canceling SampleGrabber Callback before stoping (else weird effect...)
	HRESULT hr = gcap.pSampleGrabber->SetCallback(NULL, 0);
	if(hr != S_OK) {
		ErrMsg(TEXT("Cannot cancel Sample Grabber CallBack : %LX"), hr);
		return WDM_DRV_SYSTEMERR;
	}

	// stop the graph
    IMediaControl *pMC = NULL;
	hr = gcap.pFg->QueryInterface(IID_IMediaControl, (void **)&pMC);
	if(SUCCEEDED(hr)) {
		hr = pMC->Stop();
		pMC->Release();

		if(FAILED(hr)) {
			ErrMsg(TEXT("Error %LX: Cannot stop the graph"), hr);
			return WDM_DRV_SYSTEMERR;
		}
	    gcap.fGraphRunning = FALSE;
		DbgLog((LOG_TRACE,1,TEXT("WDMDrv The graph is now stopped")));
	}

	// Destroy the FIFO and the callback thread
	hr = VBIFrameFIFO.DestroyFIFO();
	if(hr != S_OK) {
		ErrMsg(TEXT("Cannot destroy FIFO and CallBack thread : %LX"), hr);
		return WDM_DRV_SYSTEMERR;
	}

    return S_OK;
}

char *WDMDrvGetLastErrString() {
	return ErrMsgStringBuffer;
}
/*****************************************************************************
 *	  End of the exported functions section. All remainding functions are    *
 *    private to this DLL.                                                   *
 *****************************************************************************/

// ---------------------------------------------------------------------------
// Function to check correct initalization in various contexts
// Parameters :
//   sContext : string describing context for error messages
//   iLevel   : kind of check to do
//      0 : Driver initialization
//      1 : Devices enumerated
//      2 : Device selected
//      3 : Callback initialized
//      4 : Graph built and ready to run
//
// Returns :
//   TRUE  : Succesfull check
//   FALSE : Unsuccesfull check. Error message is issued here
BOOL CheckInit(char *sContext, int iLevel) {
	if (!g_bDriverInitialized) {
		ErrMsg(TEXT("Calling %s without Driver initialized"), sContext);
		return FALSE;
	}
	if (iLevel == 0) return TRUE;	// Done with checks

	if (gcap.iNumVCapDevices == -1) {
		ErrMsg(TEXT("Calling %s without previous devices enumeration"), sContext);
		return FALSE;
	}
	if (iLevel == 1) return TRUE;	// Done with checks

	if (gcap.iCurrentDevSelected == -1) {
		ErrMsg(TEXT("Calling %s without previous devices selection"), sContext);
		return FALSE;
	}
	if (iLevel == 2) return TRUE;	// Done with checks

	if (VBIFrameFIFO.m_FctToCall == NULL) {
//	if (CB.FctToCall == NULL) {
		ErrMsg(TEXT("Calling %s without previous CallBack setting"), sContext);
		return FALSE;
	}
	if (iLevel == 3) return TRUE;	// Done with checks

	if(!gcap.fGraphBuilt) {
        ErrMsg(TEXT("Calling %s without proper Graph initialization"), sContext);
        return FALSE;
	}
	return TRUE;	// All checks OK
}

// ---------------------------------------------------------------------------
// Make a graph builder object we can use for capture graph building
//
HRESULT MakeBuilder() {
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv MakeBuilder")));

    HRESULT hr = CoCreateInstance((REFCLSID)CLSID_CaptureGraphBuilder2,
        NULL, CLSCTX_INPROC, (REFIID)IID_ICaptureGraphBuilder2,
        (void **)&gcap.pBuilder);
	if (hr != S_OK) {
		ErrMsg(TEXT("Cannot create GraphBuilder2 instance : %LX"), hr);
		return WDM_DRV_SYSTEMERR;
	}

    return S_OK;
}

// ---------------------------------------------------------------------------
// Make a graph object we can use for capture graph building
//
HRESULT MakeGraph() {
    // we have one already
    if(gcap.pFg)
        return S_OK;

    HRESULT hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC,
                                  IID_IGraphBuilder, (LPVOID *)&gcap.pFg);

	if (hr != S_OK) {
		ErrMsg(TEXT("Cannot create FilterGraph instance : %LX"), hr);
		return WDM_DRV_SYSTEMERR;
	}

    return S_OK;
}

// ---------------------------------------------------------------------------
// Create the capture filters of the graph.  We need to keep them loaded from
// the beginning, so we can set parameters on them and have them remembered
HRESULT InitCapFilters() {
    HRESULT hr;

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv InitCapFilters")));
    gcap.VBIAvail = FALSE;

    hr = MakeBuilder();
    if(hr != S_OK)
        return hr;

    //
    // First, we need a Video Capture filter, and some interfaces
    //
    gcap.pVCap = NULL;

    if(gcap.pmVideo != 0) {
        hr = gcap.pmVideo->BindToObject(0, 0, IID_IBaseFilter, (void**)&gcap.pVCap);
    }

    if(gcap.pVCap == NULL || hr != S_OK) {
        ErrMsg(TEXT("Error %LX : Cannot create video capture filter"), hr);
        goto InitCapFiltersFail;
    }

    // make a filtergraph, give it to the graph builder and put the video
    // capture filter in the graph
    hr = MakeGraph();
    if(hr != S_OK)
        goto InitCapFiltersFail;

    hr = gcap.pBuilder->SetFiltergraph(gcap.pFg);
    if(hr != S_OK) {
		ErrMsg(TEXT("Cannot give graph to builder : %LX"), hr);
        goto InitCapFiltersFail;
    }

    // Add the video capture filter to the graph (without original name for simplicity)
    hr = gcap.pFg->AddFilter(gcap.pVCap, L"Video Capture");
    if(hr != S_OK) {
        ErrMsg(TEXT("Error %LX : Cannot add vidcap to filtergraph"), hr);
        goto InitCapFiltersFail;
    }

    // Calling FindInterface below will result in building the upstream
    // section of the capture graph (any WDM TVTuners or Crossbars we might
    // need).

    // we use this interface to get the name of the driver
    // Don't worry if it doesn't work:  This interface may not be available
    // until the pin is connected, or it may not be available at all.
    // (eg: interface may not be available for some DV capture)
    hr = gcap.pBuilder->FindInterface(&PIN_CATEGORY_CAPTURE,
                                      &MEDIATYPE_Interleaved, gcap.pVCap, 
                                      IID_IAMVideoCompression, (void **)&gcap.pVC);
    if(hr != S_OK) {
        hr = gcap.pBuilder->FindInterface(&PIN_CATEGORY_CAPTURE,
                                          &MEDIATYPE_Video, gcap.pVCap, 
                                          IID_IAMVideoCompression, (void **)&gcap.pVC);
    }

    // we use this interface to bring up the 3 dialogs
    // NOTE:  Only the VfW capture filter supports this.  This app only brings
    // up dialogs for legacy VfW capture drivers, since only those have dialogs
	// xx Can we remove that ?
    hr = gcap.pBuilder->FindInterface(&PIN_CATEGORY_CAPTURE,
                                      &MEDIATYPE_Video, gcap.pVCap,
                                      IID_IAMVfwCaptureDialogs, (void **)&gcap.pDlg);

	// Creating the CCrossbar object to manage the Crossbar. First, we need to
	// search the capture filters ANALOGVIDEO input pin.
	IPin        *pP = 0;
	IEnumPins   *pins=0;
	PIN_INFO     pinInfo;
	BOOL         Found = FALSE;
	IKsPropertySet *pKs=0;
	GUID guid;
	DWORD dw;
	BOOL fMatch = FALSE;

	gcap.pCrossbar = NULL;

	if(SUCCEEDED(gcap.pVCap->EnumPins(&pins))) {            
		while(!Found && (S_OK == pins->Next(1, &pP, NULL))) {
			if(S_OK == pP->QueryPinInfo(&pinInfo)) {
				if(pinInfo.dir == PINDIR_INPUT) {
					// is this pin an ANALOGVIDEOIN input pin?
					if(pP->QueryInterface(IID_IKsPropertySet,
						(void **)&pKs) == S_OK) {
							if(pKs->Get(AMPROPSETID_Pin,
								AMPROPERTY_PIN_CATEGORY, NULL, 0,
								&guid, sizeof(GUID), &dw) == S_OK) {
									if(guid == PIN_CATEGORY_ANALOGVIDEOIN)
										fMatch = TRUE;
								}
								pKs->Release();
						}

						if(fMatch) {
							HRESULT hrCreate=S_OK;
							gcap.pCrossbar = new CCrossbar(pP, &hrCreate);
							if (!gcap.pCrossbar || FAILED(hrCreate))
								goto InitCapFiltersFail;

							hr = gcap.pCrossbar->GetInputCount(&gcap.NumberOfVideoInputs);
							Found = TRUE;
						}
				}
				pinInfo.pFilter->Release();
			}
			pP->Release();
		}
		pins->Release();
	}

    // Can this filter do VBI ? Search a VBI pin. The Interface will not be released
	// for further use.
    hr = gcap.pBuilder->FindPin(gcap.pVCap, PINDIR_OUTPUT, &PIN_CATEGORY_VBI,
                                NULL, FALSE, 0, &gcap.pVBIPin);
    if(hr == S_OK) {
		DbgLog((LOG_TRACE,1,TEXT("WDMDrv : VBI Pin found !")));
        gcap.VBIAvail = TRUE;
    }
    else {
	    CleanAll(FALSE);
		return WDM_DRV_ERR_NO_VBIPIN;
    }

	// If this filter needs a VBI surface allocator, get it's interface here.
	// It will not be released for further use.
    hr = gcap.pBuilder->FindPin(gcap.pVCap, PINDIR_OUTPUT, &PIN_CATEGORY_VIDEOPORT_VBI,
                                NULL, FALSE, 0, &gcap.pVPVBIPin);
	if (hr != S_OK)
		gcap.pVPVBIPin = NULL;
	else
		DbgLog((LOG_TRACE,1,TEXT("A VBI surface allocator was found")));

	// If we have a crossbar retrieve the video input names and isaTuner status
    if(gcap.pCrossbar && gcap.NumberOfVideoInputs) {
        LONG j;
        LONG  PhysicalType;

        for(j = 0; j < gcap.NumberOfVideoInputs; j++) {

            EXECUTE_ASSERT(S_OK == gcap.pCrossbar->GetInputInfo(j,
				&gcap.currentDeviceInputsNames[j], &PhysicalType));
			DbgLog((LOG_TRACE,1,TEXT("WDMDrv Video input name : %s"), gcap.currentDeviceInputsNames[j]));

            // Update the IsaTuner Boolean
            gcap.isInputaTuner[j] = (PhysicalType == PhysConn_Video_Tuner);
        }
    }

    // Dumping graph to debug output
	DbgLog((LOG_TRACE,1,TEXT("Graph at End of InitCapFilters")));
	DumpGraph(gcap.pFg, 1);

    return S_OK;

InitCapFiltersFail:
    CleanAll(FALSE);
    return WDM_DRV_SYSTEMERR;
}

// ---------------------------------------------------------------------------
// CreateKernelFilter function introduced for the "Tee/Sink-to-Sink Converter"
// filter creation. Code is taken "as is" from MSDN documentation.
HRESULT CreateKernelFilter(
    const GUID &guidCategory,  // Filter category.
    LPCOLESTR szName,          // The name of the filter.
    IBaseFilter **ppFilter     // Receives a pointer to the filter.
)
{
    HRESULT hr;
    ICreateDevEnum *pDevEnum = NULL;
    IEnumMoniker *pEnum = NULL;
    if (!szName || !ppFilter) 
    {
        return E_POINTER;
    }

    // Create the system device enumerator.
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC,
        IID_ICreateDevEnum, (void**)&pDevEnum);
    if (FAILED(hr))
    {
        return hr;
    }

    // Create a class enumerator for the specified category.
    hr = pDevEnum->CreateClassEnumerator(guidCategory, &pEnum, 0);
    pDevEnum->Release();
    if (hr != S_OK) // S_FALSE means the category is empty.
    {
        return E_FAIL;
    }

    // Enumerate devices within this category.
    bool bFound = false;
    IMoniker *pMoniker;
    while (!bFound && (S_OK == pEnum->Next(1, &pMoniker, 0)))
    {
        IPropertyBag *pBag = NULL;
        hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void **)&pBag);
        if (FAILED(hr))
        {
            pMoniker->Release();
            continue; // Maybe the next one will work.
        }
        // Check the friendly name.
        VARIANT var;
        VariantInit(&var);
        hr = pBag->Read(L"FriendlyName", &var, NULL);
        if (SUCCEEDED(hr) && (lstrcmpiW(var.bstrVal, szName) == 0))
        {
            // This is the right filter.
            hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter,
                (void**)ppFilter);
            bFound = true;
        }
        VariantClear(&var);
        pBag->Release();
        pMoniker->Release();
    }
    pEnum->Release();
    return (bFound ? hr : E_FAIL);
}

// ---------------------------------------------------------------------------
// Add the filters for VBI data extraction
HRESULT AddVBIFilters() {
	HRESULT hr;

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv AddVBIFilters")));

	// Adding Sample Grabber filter to the graph
	//
	// Creating a Sample Grabber Filter instance
	hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void**)&gcap.pSGFilter);
	if(hr != S_OK) {
		ErrMsg(TEXT("Error Creating Sample Grabber Filter instance : %LX"), hr);
		return WDM_DRV_SYSTEMERR;
	}

	// Then adding it to the Graph
	hr = gcap.pFg->AddFilter(gcap.pSGFilter, L"Sample Grabber");
	if(hr != S_OK) {
		ErrMsg(TEXT("Error %LX : Cannot add Sample Grabber Filter to filtergraph"), hr);
		return WDM_DRV_SYSTEMERR;
	}

	// Getting Interface Pointer to ISampleGrabber of Sample Grabber filter for
	// further Callback settings / cancels when starting / stopping the graph.
	hr = gcap.pSGFilter->QueryInterface(IID_ISampleGrabber, (void**)&gcap.pSampleGrabber);
	if(hr != S_OK) {
		ErrMsg(TEXT("Error QueryInterface ISampleGrabber : %LX"), hr);
		return WDM_DRV_SYSTEMERR;
	}

	// Creating and adding a Null Renderer for connecting behind Sample Grabber
	hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void**)&gcap.pNRFilter);
	if(hr != S_OK) {
		ErrMsg(TEXT("Cannot create Null Renderer Filter instance : %LX"), hr);
		return WDM_DRV_SYSTEMERR;
	}

	// Then adding it to the Graph
	hr = gcap.pFg->AddFilter(gcap.pNRFilter, L"Null Renderer");
	if(hr != S_OK) {
		ErrMsg(TEXT("Error %LX : Cannot add Null Renderer Filter to filtergraph"), hr);
		DumpGraph(gcap.pFg, 1);
		return WDM_DRV_SYSTEMERR;
	}

	if (gcap.fWantPreview) {
		// Render the preview pin - even if there is not preview pin, the capture
		// graph builder will use a smart tee filter and provide a preview.

		// NOTE that we try to render the interleaved pin before the video pin, because
		// if BOTH exist, it's a DV filter and the only way to get the audio is to use
		// the interleaved pin.  Using the Video pin on a DV filter is only useful if
		// you don't want the audio.

		hr = gcap.pBuilder->RenderStream(&PIN_CATEGORY_PREVIEW,
			&MEDIATYPE_Interleaved, gcap.pVCap, NULL, NULL);
		if(hr == VFW_S_NOPREVIEWPIN) {
			// preview was faked up for us using the (only) capture pin
			// Informative, nothing to do...
		} else if(hr != S_OK) {
			// MEDIATYPE_Interleaved failed, is it DV ?
			hr = gcap.pBuilder->RenderStream(&PIN_CATEGORY_PREVIEW,
				&MEDIATYPE_Video, gcap.pVCap, NULL, NULL);
			if(hr == VFW_S_NOPREVIEWPIN) {
				// preview was faked up for us using the (only) capture pin
				// Informative, nothing to do...
			} else if (hr != S_OK) {
				ErrMsg(TEXT("This graph cannot preview! : %LX"), hr);
				gcap.fGraphBuilt = FALSE;
				DumpGraph(gcap.pFg, 1);
				return WDM_DRV_SYSTEMERR;
			}
		}
	}

	// Creating, adding and connecting the VBI surf alloc if one available
	if (gcap.pVPVBIPin != NULL) {
		hr = CoCreateInstance(CLSID_VBISurfaces, NULL, CLSCTX_INPROC_SERVER,
			IID_IBaseFilter, (void **)&gcap.pSurfFilter);
		if(hr != S_OK) {
			ErrMsg(TEXT("Cannot create VBI Surface Allocator instance : %LX"), hr);
			return WDM_DRV_SYSTEMERR;
		}

		// Then adding it to the Graph
		hr = gcap.pFg->AddFilter(gcap.pSurfFilter, L"VBI Surface");
		if(hr != S_OK) {
			ErrMsg(TEXT("Error %LX : Cannot add VBI Surface Allocator to filtergraph"), hr);
			gcap.pSurfFilter->Release();
			return WDM_DRV_SYSTEMERR;
		}

		hr = gcap.pBuilder->RenderStream(NULL, NULL, gcap.pVPVBIPin, 0, gcap.pSurfFilter);
		if(hr != S_OK) {
			ErrMsg(TEXT("Error %LX : Cannot connect VBI Surface Allocator"), hr);
			gcap.pSurfFilter->Release();
			DumpGraph(gcap.pFg, 1);
			return WDM_DRV_SYSTEMERR;
		}

		gcap.pSurfFilter->Release();
		DbgLog((LOG_TRACE,1,TEXT("VBI surface allocator created and connected")));
	}

	// Creating a "Tee/Sink-to-Sink Converter" filter.
	// On initial release, this filter was not there, and it worked.
	// Although it's mandatory status is not stated, it appears that it is implemented in a lot of
	// different applications. It has been introduced as an attempt to solve a problem of VBI data
	// flow acquisition stopping sometime without explanation.
	// But, the result is not clear : the problem still appears but at a lower rate.
	hr = CreateKernelFilter(AM_KSCATEGORY_SPLITTER, 
		OLESTR("Tee/Sink-to-Sink Converter"), &gcap.pKernelTee);
	if (FAILED(hr)) {
		ErrMsg(TEXT("Cannot create Tee/Sink-to-Sink Converter Filter : %LX"), hr);
		DumpGraph(gcap.pFg, 1);
		return WDM_DRV_SYSTEMERR;
	}
	// Then adding it to the Graph
	hr = gcap.pFg->AddFilter(gcap.pKernelTee, L"Tee/Sink-to-Sink Converter");
	gcap.pKernelTee->Release();
    if(hr != S_OK) {
		ErrMsg(TEXT("Cannot add Tee/Sink-to-Sink Converter Filter to graph : %LX"), hr);
		DumpGraph(gcap.pFg, 1);
		return WDM_DRV_SYSTEMERR;
    }

	// Connecting VBI output pin of Capture Device to Sink-to-Sink Converter Filter input pin
	// and Sink-to-Sink Converter Filter output pin to SampleGrabber input pin
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv : Connection of VBI pins")));
	hr = gcap.pBuilder->RenderStream(&PIN_CATEGORY_VBI, NULL, gcap.pVBIPin, 
		gcap.pKernelTee, gcap.pSGFilter);
	gcap.pKernelTee->Release();
    if(hr != S_OK) {
		ErrMsg(TEXT("Cannot connect VBI Pin -> Kernell Tee -> SampleGrabber : %LX"), hr);
		DumpGraph(gcap.pFg, 1);
		return WDM_DRV_SYSTEMERR;
    }

	// Connecting SampleGrabber output pin to Null Renderer Filter input pin.
	hr = gcap.pBuilder->RenderStream(NULL, NULL, gcap.pSGFilter,
		NULL, gcap.pNRFilter);
    if(hr != S_OK) {
		ErrMsg(TEXT("Cannot connect SampleGrabber -> NullRenderer : %LX"), hr);
		DumpGraph(gcap.pFg, 1);
		return WDM_DRV_SYSTEMERR;
    }

	// Now that Sample Grabber is connected, retrieves the media type for the connection just made
	// on the input pin. With the media type, getting usefull settings for further VBI extraction.
	AM_MEDIA_TYPE mt;
	hr = gcap.pSampleGrabber->GetConnectedMediaType(&mt);
	if (FAILED(hr)) {
		ErrMsg(TEXT("Cannot get MediaType of VBI stream : %LX"), hr);
		DumpGraph(gcap.pFg, 1);
		return WDM_DRV_SYSTEMERR;
	}

	// Examine the format block.
	if (!(
		(mt.pbFormat != NULL) &&
		(mt.majortype == KSDATAFORMAT_TYPE_VBI) &&
		(mt.subtype == KSDATAFORMAT_SUBTYPE_RAW8) &&
		(mt.formattype == KSDATAFORMAT_SPECIFIER_VBI))) {
			// Wrong format. Free the format block and return an error.
			FreeMediaType(mt);
			ErrMsg(TEXT("Wrong MediaType from VBI pin"));
			DumpGraph(gcap.pFg, 1);
			return WDM_DRV_SYSTEMERR;
	}
	
	PKS_VBIINFOHEADER pIH = (PKS_VBIINFOHEADER) mt.pbFormat;

	// Initialise CB members
	CB.m_VBIIH = *pIH;	// Save a copy of the VBI info header in the SampleGrabber CallBack object
	CB.SetSamplingRate(pIH->SamplingFrequency);

	// Format & standard checking
	switch (pIH->VideoStandard) {
	case KS_AnalogVideo_PAL_B: /* PAL & SECAM Standards */
	case KS_AnalogVideo_PAL_D:
	case KS_AnalogVideo_PAL_G:
	case KS_AnalogVideo_PAL_H:
	case KS_AnalogVideo_PAL_I:
	case KS_AnalogVideo_PAL_M:
	case KS_AnalogVideo_PAL_N:
	case KS_AnalogVideo_PAL_60:
	case KS_AnalogVideo_SECAM_B:
	case KS_AnalogVideo_SECAM_D:
	case KS_AnalogVideo_SECAM_G:
	case KS_AnalogVideo_SECAM_H:
	case KS_AnalogVideo_SECAM_K:
	case KS_AnalogVideo_SECAM_K1:
	case KS_AnalogVideo_SECAM_L:
	case KS_AnalogVideo_SECAM_L1:
	case KS_AnalogVideo_PAL_N_COMBO:
		DbgLog((LOG_TRACE,1,TEXT("WDMDrv : Capture Device reports PAL/SECAM : OK")));
		break;

	case KS_AnalogVideo_NTSC_M: /* NTSC Standards */
	case KS_AnalogVideo_NTSC_M_J:
	case KS_AnalogVideo_NTSC_433:
		DbgLog((LOG_TRACE,1,TEXT("WDMDrv : Capture Device reports NTSC : Not supported")));
		DbgLog((LOG_TRACE,1,TEXT("  Error condition temporarilly removed")));
		return WDM_DRV_SYSTEMERR;
		break;
	default:
		DbgLog((LOG_TRACE,1,TEXT("WDMDrv : Capture Device reports unknown standard : Error")));
		DumpGraph(gcap.pFg, 1);
		return WDM_DRV_SYSTEMERR;
	}

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv : VBI Characteristics :")));
	DbgLog((LOG_TRACE,1,TEXT("  StartLine = %lu, EndLine = %lu"), pIH->StartLine, pIH->EndLine));
	DbgLog((LOG_TRACE,1,TEXT("  SamplingFreq = %lu"), pIH->SamplingFrequency));
	DbgLog((LOG_TRACE,1,TEXT("  SamplesPerLine = %lu"), pIH->SamplesPerLine));
	DbgLog((LOG_TRACE,1,TEXT("  StrideInBytes = %lu"), pIH->StrideInBytes));

	ASSERT (pIH->BufferSize == ((pIH->EndLine - pIH->StartLine + 1) *
		pIH->StrideInBytes));

	// xx reporter dans le "if (gcap.fWantPreview)" + return SYSERR
    // This will find the IVideoWindow interface on the renderer.  It is 
    // important to ask the filtergraph for this interface... do NOT use
    // ICaptureGraphBuilder2::FindInterface, because the filtergraph needs to
    // know we own the window so it can give us display changed messages, etc.
    hr = gcap.pFg->QueryInterface(IID_IVideoWindow, (void **)&gcap.pVW);
    if(hr != S_OK) {
		ErrMsg(TEXT("This graph cannot preview properly : %LX"), hr);
		// xx return err ??
    }

    // Dumping graph to debug output
	DbgLog((LOG_TRACE,1,TEXT("Graph at End of AddVBIFilters")));
	DumpGraph(gcap.pFg, 1);

    // Add our graph to the running object table, which will allow
    // the GraphEdit application to "spy" on our graph
#ifdef REGISTER_FILTERGRAPH
    hr = AddGraphToRot(gcap.pFg, &g_dwGraphRegister);
    if(FAILED(hr)) {
        DbgLog((LOG_TRACE,1,TEXT("Failed to register filter graph with ROT!  hr=0x%x"), hr));
        g_dwGraphRegister = 0;
    }
#endif

    // All done.
    gcap.fGraphBuilt = TRUE;
    return S_OK;
}

// ---------------------------------------------------------------------------
// Removes everything in the current graph, free all filters, release all
// interfaces that we got and all dynamic objects.
// Parameter : KeepEnumeratedVideoMonikers set to TRUE means we want to
// keep the Interface pointers to video devices previously enumerated.
void CleanAll(bool KeepEnumeratedVideoMonikers) {
	IEnumFilters *pEnumFilters;
	IBaseFilter *pFilter;
	HRESULT hr;

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv CleanAll")));

	if(gcap.fGraphRunning) {
		hr = StopAcq();
		if (hr != S_OK) {
			ErrMsg(TEXT("WDMDrv : CleanAll, Cannot stop graph"));
			goto ReleaseRemaining;
		}
	}

	// If graph built, going through all filters in the graph
	if(gcap.fGraphBuilt) {
		hr = gcap.pFg->EnumFilters(&pEnumFilters);
		if (hr != S_OK) {
			ErrMsg(TEXT("WDMDrv : CleanAll, Cannot enum filters for removal"));
			goto ReleaseRemaining;
		}

		while (TRUE) {
			hr = pEnumFilters->Next(1, &pFilter, NULL);
			if (hr != S_OK) break;
			// From DX SDK Doc : "It is not necessary to disconnect the filter's pins before
			// calling RemoveFilter, but the filter graph should be in the Stopped state."
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxx about to remove %x"), pFilter));
			hr = gcap.pFg->RemoveFilter(pFilter);
			if (hr != S_OK) {
				ErrMsg(TEXT("WDMDrv : CleanAll, Cannot remove filter"));
				goto ReleaseRemaining;
			}
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxx gcap.pFg->RemoveFilter()")));
			// Releasing pointer to Kernel Tee bring a fatal exception, so avoid it.
			if (pFilter != gcap.pKernelTee)
				pFilter->Release();
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxx pFilter->Release()")));
			// IEnumFilters::Next returns VFW_E_ENUM_OUT_OF_SYNC if the graph change, and
			// this is what we are doing ! So we have to call IEnumFilters::Reset (see doc).
			hr = pEnumFilters->Reset();
		}
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxx end loop")));
		pEnumFilters->Release();
		SAFE_RELEASE(gcap.pFg);
		gcap.fGraphBuilt = FALSE;
	}

	// Releasing other objects.
ReleaseRemaining:
	if (!KeepEnumeratedVideoMonikers) {
		for (int i = 0; i < gcap.iNumVCapDevices; i++) {
			SAFE_RELEASE(gcap.pmAllVideoDevices[i]);
			gcap.pmAllVideoDevices[i] = NULL;
		}
		gcap.pmVideo = NULL;
		gcap.iNumVCapDevices = -1;
	}

    SAFE_RELEASE(gcap.pVCap);
    SAFE_RELEASE(gcap.pVBIPin);
    SAFE_RELEASE(gcap.pVPVBIPin);
    SAFE_RELEASE(gcap.pBuilder);
	SAFE_RELEASE(gcap.pTV);
	SAFE_RELEASE(gcap.pAD);
	SAFE_RELEASE(gcap.pVC);
	SAFE_RELEASE(gcap.pSGFilter);
	SAFE_RELEASE(gcap.pSampleGrabber);
	SAFE_RELEASE(gcap.pNRFilter);
    SAFE_RELEASE(gcap.pDlg);

    if(gcap.pCrossbar) {
        delete gcap.pCrossbar;
        gcap.pCrossbar = NULL;
    }

	// Remove preview windows
	if(gcap.pVW) {
        gcap.pVW->put_Owner(NULL);
        gcap.pVW->put_Visible(OAFALSE);
        gcap.pVW->Release();
        gcap.pVW = NULL;
    }

#ifdef REGISTER_FILTERGRAPH
    // Remove filter graph from the running object table   
    if(g_dwGraphRegister) {
        RemoveGraphFromRot(g_dwGraphRegister);
        g_dwGraphRegister = 0;
    }
#endif

	gcap.iCurrentDevSelected = -1;
	VBIFrameFIFO.m_FctToCall = NULL;
    gcap.fGraphBuilt = FALSE;
}

// ---------------------------------------------------------------------------
// Adds a DirectShow filter graph to the Running Object Table,
// allowing GraphEdit to "spy" on a remote filter graph.
HRESULT AddGraphToRot(IUnknown *pUnkGraph, DWORD *pdwRegister) {
    IMoniker * pMoniker;
    IRunningObjectTable *pROT;
    WCHAR wsz[128];
    HRESULT hr;

    if (!pUnkGraph || !pdwRegister)
        return E_POINTER;

    if(FAILED(GetRunningObjectTable(0, &pROT)))
        return E_FAIL;

    wsprintfW(wsz, L"FilterGraph %08x pid %08x\0", (DWORD_PTR)pUnkGraph, 
        GetCurrentProcessId());

    hr = CreateItemMoniker(L"!", wsz, &pMoniker);
    if(SUCCEEDED(hr)) {
        // Use the ROTFLAGS_REGISTRATIONKEEPSALIVE to ensure a strong reference
        // to the object.  Using this flag will cause the object to remain
        // registered until it is explicitly revoked with the Revoke() method.
        //
        // Not using this flag means that if GraphEdit remotely connects
        // to this graph and then GraphEdit exits, this object registration 
        // will be deleted, causing future attempts by GraphEdit to fail until
        // this application is restarted or until the graph is registered again.
        hr = pROT->Register(ROTFLAGS_REGISTRATIONKEEPSALIVE, pUnkGraph, 
                            pMoniker, pdwRegister);
        pMoniker->Release();
    }

    pROT->Release();
    return hr;
}

// ---------------------------------------------------------------------------
// Removes a filter graph from the Running Object Table
void RemoveGraphFromRot(DWORD pdwRegister) {
    IRunningObjectTable *pROT;

    if(SUCCEEDED(GetRunningObjectTable(0, &pROT))) {
        pROT->Revoke(pdwRegister);
        pROT->Release();
    }
}

// ---------------------------------------------------------------------------
// Error reporting function xx log to file to be implemented
void ErrMsg(LPTSTR szFormat,...) {
    const size_t NUMCHARS = sizeof(ErrMsgStringBuffer) / sizeof(ErrMsgStringBuffer[0]);
    const int LASTCHAR = NUMCHARS - 1;

    // Format the input string
    va_list pArgs;
    va_start(pArgs, szFormat);

    // Use a bounded buffer size to prevent buffer overruns.  Limit count to
    // character size minus one to allow for a NULL terminating character.
    _vsnprintf(ErrMsgStringBuffer, NUMCHARS - 1, szFormat, pArgs);
    va_end(pArgs);

    // Ensure that the formatted string is NULL-terminated
    ErrMsgStringBuffer[LASTCHAR] = TEXT('\0');

	DbgLog((LOG_TRACE,1,TEXT("VBIDrv Error : %s"), ErrMsgStringBuffer));

	//if (bDsdrvLogEnabled) {
	//	int fd;

	//	fd = open("wdmdrv.log", O_WRONLY|O_CREAT|O_APPEND, 0666);
	//	if (fd >= 0) {
	//		write(fd, ErrMsgStringBuffer, strlen(ErrMsgStringBuffer));
	//		close(fd);
	//	}
	//}

    //MessageBox(NULL, ErrMsgStringBuffer, NULL, 
    //           MB_OK | MB_ICONEXCLAMATION | MB_TASKMODAL);
}

#ifdef _DEBUG
char *GetOSName() {
	static char Result[128];
	char *pt;
	OSVERSIONINFOEX osvi;
	BOOL bOsVersionInfoEx;

	ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	pt = Result;

	if( !(bOsVersionInfoEx = GetVersionEx ((OSVERSIONINFO *) &osvi)) ) {
		// If OSVERSIONINFOEX doesn't work, try OSVERSIONINFO.
		osvi.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
		if (! GetVersionEx ( (OSVERSIONINFO *) &osvi) ) 
			return "None";
	}

	switch (osvi.dwPlatformId) {
	  case VER_PLATFORM_WIN32_NT:
		  // Test for the product.
		  if ( osvi.dwMajorVersion <= 4 )
			  pt += sprintf(pt, "Windows NT ");

		  if ( osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 0 )
			  pt += sprintf(pt, "Windows 2000 ");

		  if ( osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 1 )
			  pt += sprintf(pt, "Windows XP ");

		  // Test for product type.
		  if ( bOsVersionInfoEx ) {
			  if ( osvi.wProductType == VER_NT_WORKSTATION ) {
				  if( osvi.wSuiteMask & VER_SUITE_PERSONAL )
					  pt += sprintf (pt, "Personal " );
				  else
					  pt += sprintf (pt, "Professional " );
			  }

			  else if ( osvi.wProductType == VER_NT_SERVER ) {
				  if( osvi.wSuiteMask & VER_SUITE_DATACENTER )
					  pt += sprintf (pt, "DataCenter Server " );
				  else if( osvi.wSuiteMask & VER_SUITE_ENTERPRISE )
					  pt += sprintf (pt, "Advanced Server " );
				  else
					  pt += sprintf (pt, "Server " );
			  }
		  } else {
			  HKEY hKey;
			  char szProductType[80];
			  DWORD dwBufLen;

			  RegOpenKeyEx( HKEY_LOCAL_MACHINE,
				  "SYSTEM\\CurrentControlSet\\Control\\ProductOptions",
				  0, KEY_QUERY_VALUE, &hKey );
			  RegQueryValueEx( hKey, "ProductType", NULL, NULL,
				  (LPBYTE) szProductType, &dwBufLen);
			  RegCloseKey( hKey );
			  if ( lstrcmpi( "WINNT", szProductType) == 0 )
				  pt += sprintf(pt, "Professional " );
			  if ( lstrcmpi( "LANMANNT", szProductType) == 0 )
				  pt += sprintf(pt, "Server " );
			  if ( lstrcmpi( "SERVERNT", szProductType) == 0 )
				  pt += sprintf(pt, "Advanced Server " );
		  }

		  // Display version, service pack (if any), and build number.
		  if ( osvi.dwMajorVersion <= 4 ) {
			  pt += sprintf (pt, "version %d.%d %s (Build %d)",
				  osvi.dwMajorVersion, osvi.dwMinorVersion,
				  osvi.szCSDVersion, osvi.dwBuildNumber & 0xFFFF);
		  } else { 
			  pt += sprintf (pt, "%s (Build %d)", osvi.szCSDVersion, osvi.dwBuildNumber & 0xFFFF);
		  }
		  break;

	  case VER_PLATFORM_WIN32_WINDOWS:

		  if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 0) {
			  pt += sprintf (pt, "Windows 95 ");
			  if ( osvi.szCSDVersion[1] == 'C' || osvi.szCSDVersion[1] == 'B' )
				  pt += sprintf(pt, "OSR2 " );
		  } 

		  if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 10) {
			  pt += sprintf (pt, "Windows 98 ");
			  if ( osvi.szCSDVersion[1] == 'A' )
				  pt += sprintf(pt, "SE " );
		  } 

		  if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 90) {
			  pt += sprintf (pt, "Windows Me ");
		  } 
		  break;

	  case VER_PLATFORM_WIN32s:

		  pt += sprintf (pt, "Win32s ");
		  break;
	}
	return Result; 
}
#endif

