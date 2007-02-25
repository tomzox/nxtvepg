/*
 *  CCrossbar
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
 *    This file is the implementation of the CCrossbar Class which is in
 *    charge of managing the Crossbar(s) for a capture device.
 *
 *  Author: Gérard Chevalier
 *
 */
#include <streams.h>
#include "CCrossbar.h"

CCrossbar::CCrossbar(IPin *pInitialInputPin, HRESULT *phr) {
    HRESULT hr;
	PIN_INFO pinInfo;
	IAMCrossbar *pCrossbar;

	DbgLog((LOG_TRACE,1,TEXT("CCrossbar Constructor")));

	// Initial checking
    ASSERT(phr);
    ASSERT (pInitialInputPin != NULL);
    
	// Creating the Paths list.
	m_PathsList = new CCrossbarRouteInfoList(TEXT("PathsList"), 5);
	if (m_PathsList == NULL) {
        hr = E_OUTOFMEMORY;
		goto ConstructorExit;
	}
	DbgLog((LOG_TRACE,2,TEXT("CCrossbarRouteInfoList created")));

	// Creating the Crossbar list.
	m_CrossbarInfoList = new CCrossbarInfoList(TEXT("CrossbarList"), 5);
	if (m_CrossbarInfoList == NULL) {
        hr = E_OUTOFMEMORY;
		goto ConstructorExit;
	}
	DbgLog((LOG_TRACE,2,TEXT("CCrossbarInfoList created")));

	// Finding the FinalOutput
	hr = pInitialInputPin->ConnectedTo(&pFinalOutput);
	if (hr != S_OK) {
		hr = E_FAIL;
		goto ConstructorExit;
	}
	DbgLog((LOG_TRACE,2,TEXT("FinalOutput found [%lx]"), pFinalOutput));

	// Finding associated Crossbar, This one will be the final one in the chain
	if (pFinalOutput->QueryPinInfo(&pinInfo) != S_OK) {
		hr = E_FAIL;
		goto ConstructorExit;
	}
	hr = pinInfo.pFilter->QueryInterface(IID_IAMCrossbar, (void **)&pCrossbar);
	pinInfo.pFilter->Release();
	if (hr != S_OK) {
		pCrossbar->Release();
		hr = E_FAIL;
		goto ConstructorExit;
	}
	DbgLog((LOG_TRACE,2,TEXT("Got IID_IAMCrossbar interface for first Crossbar")));

	// And adding it to the Crossbar Info List
	hr = AddCrossbar(pCrossbar);
	if (hr != S_OK) {
		pCrossbar->Release();
		hr = E_FAIL;
		goto ConstructorExit;
	}
	pCrossbar->Release(); // AddCrossbar() did an AddReff()

	// Construct the paths.
    if (m_PathsList)
        hr = BuildRoutes(pFinalOutput, NULL, 1);

ConstructorExit:
	// Now that Paths are built or error condition, we can destroy the Crossbar list
	DestroyCrossbarList();
	*phr = hr;
}

CCrossbar::~CCrossbar() {
    DbgLog((LOG_TRACE,1,TEXT("CCrossbar Destructor")));
	DestroyPathsList();
}

// This function add a new Crossbar to the list of known Crossbar. If the Crossbar given
// is already known, nothing is done. Else all it's pins are enumerated.
HRESULT CCrossbar::AddCrossbar(IAMCrossbar *pCrossbar) {
	HRESULT hr;
	LONG InputIndex, OutputIndex;
	LONG InputsCnt, OutputsCnt;
	IBaseFilter *pBaseFilter;
    IEnumPins *pins;
    IPin *pP;

	DbgLog((LOG_TRACE,2,TEXT("CCrossbar::AddCrossbar entry")));

	// Search the given Crossbar in the known list
	CCrossbarInfo *pCrossbarFound = GetCrossbarInfo(pCrossbar);
	DbgLog((LOG_TRACE,2,TEXT("Got Crossbar info [%lx] for CB %lx"),pCrossbarFound ,pCrossbar));

	// And returns if known.
	if (pCrossbarFound != NULL)
		return S_OK;

	DbgLog((LOG_TRACE,2,TEXT("Crossbar [%lx] not already known, adding"),pCrossbar));
    hr = pCrossbar->QueryInterface(IID_IBaseFilter, (void **)&pBaseFilter);
    if (hr != S_OK)
		return E_FAIL;

	hr = pBaseFilter->EnumPins(&pins);
	if (hr != S_OK) {
		pBaseFilter->Release();
		return E_FAIL;
	}

	// Constructing a new Crossbar info object and initializing it.
	CCrossbarInfo *pNewCrossbar = new CCrossbarInfo();
	DbgLog((LOG_TRACE,2,TEXT("CCrossbarInfo created")));
	EXECUTE_ASSERT(pCrossbar->get_PinCounts(&OutputsCnt, &InputsCnt) == S_OK);

	DbgLog((LOG_TRACE,2,TEXT("  Crossbar [%lx] %ld inputs, %ld outputs"),pCrossbar,
		InputsCnt, OutputsCnt));
	
	pNewCrossbar->InputsNb = InputsCnt;
	pNewCrossbar->OutputsNb = OutputsCnt;
	pNewCrossbar->Inputs = new IPinPtr[InputsCnt];
	pNewCrossbar->Outputs = new IPinPtr[OutputsCnt];
	InputIndex = OutputIndex = 0;
	DbgLog((LOG_TRACE,2,TEXT("  Pins array created")));

	// Enumerating all pins. The Next function calls  will return Input Pins first (0..InputsCnt)
	// then Output Pins (0..OutputsCnt).
	while(pins->Next(1, &pP, NULL) == S_OK) {
		// Check direction (no need to query pin info)
		if (InputIndex < InputsCnt) {
			// Current pin is an input
			pNewCrossbar->Inputs[InputIndex++] = pP;
			DbgLog((LOG_TRACE,2,TEXT("  Input Pin [%lx] added"), pP));
		} else {
			pNewCrossbar->Outputs[OutputIndex++] = pP;
			DbgLog((LOG_TRACE,2,TEXT("  Output Pin [%lx] added"), pP));
		// As we keep the IPin interface, we don't release it here
		}
	}
	pins->Release();
	pBaseFilter->Release();

	pNewCrossbar->m_pCrossbar = pCrossbar;
	pCrossbar->AddRef();	// Keep a reference until the list is destroyed.

	// Adding the new Crossbar in the list
	m_CrossbarInfoList->AddTail(pNewCrossbar);
	DbgLog((LOG_TRACE,2,TEXT("  CCrossbar [%lx] added"), pCrossbar));

	return S_OK;
}

void CCrossbar::DestroyCrossbarList() {
	CCrossbarInfo *pCurrentCrossbarInfo;

	DbgLog((LOG_TRACE,2,TEXT("CCrossbar::DestroyCrossbarList entry")));
	if (m_CrossbarInfoList == NULL)
		return;

	while (m_CrossbarInfoList->GetCount()) {
		pCurrentCrossbarInfo = m_CrossbarInfoList->GetHead();
		for (int Input = 0; Input < pCurrentCrossbarInfo->InputsNb; Input++)
			pCurrentCrossbarInfo->Inputs[Input]->Release();
		for (int Output = 0; Output < pCurrentCrossbarInfo->OutputsNb; Output++)
			pCurrentCrossbarInfo->Outputs[Output]->Release();
		pCurrentCrossbarInfo->m_pCrossbar->Release();

		m_CrossbarInfoList->RemoveHead();
	}
	delete m_CrossbarInfoList;
}

void CCrossbar::DestroyPathsList() {
	CCrossbarRouteInfo *pCurrentPath;
	int CurrentLength;

	DbgLog((LOG_TRACE,2,TEXT("CCrossbar::DestroyPathsList entry")));
	if (m_PathsList == NULL)
		return;

	while (m_PathsList->GetCount()) {
		pCurrentPath = m_PathsList->GetHead();
		DbgLog((LOG_TRACE,2,TEXT("CCrossbar::DestroyPathsList processing entry [%lx]"),
			pCurrentPath));

		// Release Crossbar interfaces got during paths construction
		CurrentLength = pCurrentPath->PathLength;
		DbgLog((LOG_TRACE,2,TEXT("  PathLength is %d for current path"), CurrentLength));
		for (int i = 0; i < CurrentLength; i++) {
			if (pCurrentPath[i].pCrossbar != NULL) {
				ULONG RefCount;
				RefCount = pCurrentPath[i].pCrossbar->Release();
				DbgLog((LOG_TRACE,2,TEXT("CCrossbar::DestroyPathsList Crossbar [%lx] released (count : %ld)"),
					pCurrentPath[i].pCrossbar, RefCount));
			}
		}

		delete [] pCurrentPath;
		DbgLog((LOG_TRACE,2,TEXT("CCrossbar::DestroyPathsList current path deleted")));
		m_PathsList->RemoveHead();
		DbgLog((LOG_TRACE,2,TEXT("CCrossbar::DestroyPathsList current path removed from list")));
	}
	delete m_PathsList;
	DbgLog((LOG_TRACE,2,TEXT("CCrossbar::DestroyPathsList path list deleted")));
}

// Find a CrossbarInfo object in the CrossbarInfo list given a Crossbar reference (IAMCrossbar).
CCrossbarInfo *CCrossbar::GetCrossbarInfo(IAMCrossbar *pCrossbar) {
	CCrossbarInfo *pCurrentCrossbarInfo;
	int nCrossbarCount;

	DbgLog((LOG_TRACE,2,TEXT("CCrossbar::GetCrossbarInfo entry")));
	if ((nCrossbarCount = m_CrossbarInfoList->GetCount()) == 0)
		return NULL;

	POSITION pos = m_CrossbarInfoList->GetHeadPosition();

	for (int i = 0; i < nCrossbarCount; i++) {
		pCurrentCrossbarInfo = m_CrossbarInfoList->GetNext(pos);
		if (pCurrentCrossbarInfo->m_pCrossbar == pCrossbar)
			return pCurrentCrossbarInfo;
	}
	return NULL;
}

// As Crossbar API deals with pin indexes and al other API with IPin pointers, we have a
// function to translate index -> IPin pointers.
// Reverse translation is done by looking at CCrossbarInfo data.
int CCrossbar::GetPinIndex(CCrossbarInfo *pCrossbarInfo, IPin *pPin, int iPinDirection) {
	IPin **pSearchPin;
	int iNbPins;

	DbgLog((LOG_TRACE,2,TEXT("CCrossbar::GetPinIndex Entry")));
	if (iPinDirection == 0) {	// Input
		pSearchPin = pCrossbarInfo->Inputs;
		iNbPins = pCrossbarInfo->InputsNb;
	} else {
		pSearchPin = pCrossbarInfo->Outputs;
		iNbPins = pCrossbarInfo->OutputsNb;
	}

	for (int i = 0; i < iNbPins; i++)
		if (pSearchPin[i] == pPin) return i;

	// End of loop, and we did not find ! Error
	return E_POINTER;
}

CCrossbarRouteInfo *CCrossbar::GetPathAtIndex(LONG lIndex) {
	CCrossbarRouteInfo *pCurrentPath;

	DbgLog((LOG_TRACE,2,TEXT("CCrossbar::GetPathAtIndex entry")));

	EXECUTE_ASSERT(m_PathsList != NULL);
	EXECUTE_ASSERT(lIndex <= m_PathsList->GetCount());

	//CCrossbarRouteInfo *pCurrentPath = m_PathsList->GetHead();
	POSITION pos = m_PathsList->GetHeadPosition();
	for (int i = 0; i <= lIndex; i++)
		pCurrentPath = m_PathsList->GetNext(pos);

	return pCurrentPath;
}

HRESULT CCrossbar::BuildRoutes(IPin *pFromOutput, CCrossbarRouteInfo *pUpstreamConnection,
					int CurrentPathLength) {
	HRESULT hr;
	CCrossbarRouteInfo CurrentConnection;
	PIN_INFO pinInfo;
	IAMCrossbar *pCrossbar, *pUpStreamCrossBar;
	CCrossbarInfo *pCrossbarInfo;
	LONG InputIndex, OutputIndex, RelatedInputIndex, RelatedOutputIndex;
	LONG InputPhysicalType, OutputPhysicalType;
	IPin *pNextUpstreamOutputPin;

	DbgLog((LOG_TRACE,2,TEXT("CCrossbar::BuildRoutes entry, PathLength is %d, FromOutput is %lx"),
		CurrentPathLength, pFromOutput));

	// By design, FromOutput belongs to a Crossbar, so examine all inputs that can
	// connect to it
	EXECUTE_ASSERT(pFromOutput->QueryPinInfo(&pinInfo) == S_OK);
	
	hr = pinInfo.pFilter->QueryInterface(IID_IAMCrossbar, (void **)&pCrossbar);
	pinInfo.pFilter->Release();
    if (hr != S_OK)
		return E_FAIL;

	// Get the Crossbar info (it must exists, so not found => error)
	pCrossbarInfo = GetCrossbarInfo(pCrossbar);
	if (pCrossbarInfo == NULL) {
		pCrossbar->Release();
		return E_FAIL;
	}
	DbgLog((LOG_TRACE,2,TEXT("Found Crossbar info [%lx] for CB %lx"), pCrossbarInfo, pCrossbar));

	OutputIndex = GetPinIndex(pCrossbarInfo, pFromOutput, 1);
	DbgLog((LOG_TRACE,2,TEXT("  Output index is %ld for [%lx]"), OutputIndex, pFromOutput));

	// Get related output
	EXECUTE_ASSERT(pCrossbar->get_CrossbarPinInfo(FALSE, OutputIndex,
		&RelatedOutputIndex, &OutputPhysicalType)== S_OK);
	for (InputIndex = 0; InputIndex < pCrossbarInfo->InputsNb; InputIndex++) {
		DbgLog((LOG_TRACE,2,TEXT("Checking input %ld of current CB [%lx]"),
			InputIndex, pCrossbar));
		// If current input is of video type, can it be routed to FromOutput ?
		EXECUTE_ASSERT(pCrossbar->get_CrossbarPinInfo(TRUE, InputIndex,
			&RelatedInputIndex, &InputPhysicalType) == S_OK);

		if (InputPhysicalType < PhysConn_Audio_Tuner) {
			DbgLog((LOG_TRACE,2,TEXT("Current input is of type video, CanRoute %ld / %ld ?"),
				OutputIndex, InputIndex));
			if (pCrossbar->CanRoute(OutputIndex, InputIndex) == S_OK) {
				// Found a possible connection for current input in the Crossbar.
				// Store all info in the current connection object.
				DbgLog((LOG_TRACE,2,TEXT("  Possible connection found with ouput %ld"), OutputIndex));
				CurrentConnection.VideoInputIndex = InputIndex;
				CurrentConnection.VideoOutputIndex = OutputIndex;
				CurrentConnection.AudioInputIndex = RelatedInputIndex;
				CurrentConnection.AudioOutputIndex = RelatedOutputIndex;
				CurrentConnection.PathLength = CurrentPathLength;
				CurrentConnection.pUpstreamConnection = pUpstreamConnection;
				CurrentConnection.pCrossbar = pCrossbar;
				pCrossbar->AddRef();
				CurrentConnection.InputPhysicalType = InputPhysicalType;

				// Now continue searching the video path upstream if we are not on
				// a terminal input. This last condition is met if :
				// - Current Input is NOT connected
				// - Or Pin connected to Current Input doesn't belong to a Crossbar
				BOOL bIsTerminalPin = FALSE;

				// Is current Input connected ?
				hr = pCrossbarInfo->Inputs[InputIndex]->ConnectedTo(&pNextUpstreamOutputPin);
				if (hr == VFW_E_NOT_CONNECTED) {
					bIsTerminalPin = TRUE;		// No : we reached an end of path
					DbgLog((LOG_TRACE,2,TEXT("Current input not connected")));
				} else if (hr == S_OK)
					DbgLog((LOG_TRACE,2,TEXT("Current input connected")));
				else
					return E_FAIL;			// If error abort

				// Is the NextUpstreamOutputPin belongs to a Crossbar ?
				if (!bIsTerminalPin) {
					DbgLog((LOG_TRACE,2,TEXT("Is Current input connected to a CrossBar ?")));
					EXECUTE_ASSERT(pNextUpstreamOutputPin->QueryPinInfo(&pinInfo) == S_OK);
					hr = pinInfo.pFilter->QueryInterface(IID_IAMCrossbar, (void **)&pUpStreamCrossBar);
					pinInfo.pFilter->Release();
					if (hr != S_OK) {
						bIsTerminalPin = TRUE;	// No Crossbar : we reached an end of path
						DbgLog((LOG_TRACE,2,TEXT("  No")));
					} else
						DbgLog((LOG_TRACE,2,TEXT("  Yes")));
				}

				if (bIsTerminalPin) {
					// We are then on a "terminal" pin.
					// The path constructed up to now is a transient data structure spread
					// on the call stack. We have to store it in the paths list.
					DbgLog((LOG_TRACE,2,TEXT("Found terminal pin, calling SavePath")));
					hr = SavePath(&CurrentConnection);
					if (hr != S_OK)
						return E_FAIL;
				} else {
					// Otherwise, continue upstream by calling ourself.
					// By the way, we discovered a new Crossbar, so declare it.
					DbgLog((LOG_TRACE,2,TEXT("Found an upstream CB, calling AddCrossbar")));
					hr = AddCrossbar(pUpStreamCrossBar);
					pUpStreamCrossBar->Release(); // AddCrossbar() did an AddReff()
					if (hr != S_OK)
						return E_FAIL;

					DbgLog((LOG_TRACE,2,TEXT("Then, recurse calling ourselves")));
					hr = BuildRoutes(pNextUpstreamOutputPin, &CurrentConnection,
						CurrentPathLength + 1);
					if (hr != S_OK)
						return E_FAIL;
				}
			}	// can route
		}	// all inputs
		// xx releasessss
	}

	return hr;
}

HRESULT CCrossbar::SavePath(CCrossbarRouteInfo *pFirstPathElement) {
	int PathLength = pFirstPathElement->PathLength;
	LONG InputPhysicalType;

	DbgLog((LOG_TRACE,2,TEXT("CCrossbar::SavePath entry")));

	// When called, *pFirstPathElement carries correct values for PathLength
	// and InputPhysicalType. So getting them.
	PathLength = pFirstPathElement->PathLength;
	InputPhysicalType = pFirstPathElement->InputPhysicalType;

	// Allocation of the path elements in a new array.
	CCrossbarRouteInfo *pNewRoute = new CCrossbarRouteInfo[PathLength];
	if (pNewRoute == NULL)
		return E_OUTOFMEMORY;
	DbgLog((LOG_TRACE,2,TEXT("Path elements array allocated (%d elts)"), PathLength));

	// Adding the new path in the list.
	m_PathsList->AddTail(pNewRoute);
	DbgLog((LOG_TRACE,2,TEXT("New path array added to paths list [%lx]"), pNewRoute));
	
	// Copying data for the path.
	for (int i = PathLength - 1; i >= 0; i--) {
		// Storing current path info in the array in the output to input order.
		// Chaining is no more required then.
		DbgLog((LOG_TRACE,2,TEXT("Copying data for current path at depth %d"), i));
		pNewRoute[i] = *pFirstPathElement;

		// Go ahead along the path
		pFirstPathElement = pFirstPathElement->pUpstreamConnection;
	}

	// Add usefull info on this path on the first element.
	pNewRoute[0].PathLength = PathLength;
	pNewRoute[0].InputPhysicalType = InputPhysicalType;
	return S_OK;
}

/*
 *	Begining of the public functions section
 */
HRESULT CCrossbar::GetInputCount (LONG *pCount) {
	if (pCount == NULL || m_PathsList == NULL)
		return E_POINTER;

	*pCount = m_PathsList->GetCount();
	return S_OK;
}

HRESULT CCrossbar::GetInputInfo(LONG Index, char **pName, LONG *pPhysicalType) {
	if (pName == NULL || pPhysicalType == NULL)
		return E_POINTER;

	if (Index >= m_PathsList->GetCount())
		return E_FAIL;

	CCrossbarRouteInfo *pPath = GetPathAtIndex(Index);
	*pPhysicalType = pPath->InputPhysicalType;

	switch (*pPhysicalType) {
	case PhysConn_Video_Tuner:            *pName = "Video Tuner"; break;
	case PhysConn_Video_Composite:        *pName = "Video Composite"; break;
	case PhysConn_Video_SVideo:           *pName = "S-Video"; break;
	case PhysConn_Video_RGB:              *pName = "Video RGB"; break;
	case PhysConn_Video_YRYBY:            *pName = "Video YRYBY"; break;
	case PhysConn_Video_SerialDigital:    *pName = "Video Serial Digital"; break;
	case PhysConn_Video_ParallelDigital:  *pName = "Video Parallel Digital"; break;
	case PhysConn_Video_SCSI:             *pName = "Video SCSI"; break;
	case PhysConn_Video_AUX:              *pName = "Video AUX"; break;
	case PhysConn_Video_1394:             *pName = "Video 1394"; break;
	case PhysConn_Video_USB:              *pName = "Video USB"; break;
	case PhysConn_Video_VideoDecoder:     *pName = "Video Decoder"; break;
	case PhysConn_Video_VideoEncoder:     *pName = "Video Encoder"; break;

	case PhysConn_Audio_Tuner:            *pName = "Audio Tuner"; break;
	case PhysConn_Audio_Line:             *pName = "Audio Line"; break;
	case PhysConn_Audio_Mic:              *pName = "Audio Microphone"; break;
	case PhysConn_Audio_AESDigital:       *pName = "Audio AES/EBU Digital"; break;
	case PhysConn_Audio_SPDIFDigital:     *pName = "Audio S/PDIF"; break;
	case PhysConn_Audio_SCSI:             *pName = "Audio SCSI"; break;
	case PhysConn_Audio_AUX:              *pName = "Audio AUX"; break;
	case PhysConn_Audio_1394:             *pName = "Audio 1394"; break;
	case PhysConn_Audio_USB:              *pName = "Audio USB"; break;
	case PhysConn_Audio_AudioDecoder:     *pName = "Audio Decoder"; break;

	default:                              *pName = "Unknown Type";
	}

	return S_OK;
}

HRESULT CCrossbar::SetInputIndex(LONG Index, BOOL Mute) {
	HRESULT hr;

	if (Index >= m_PathsList->GetCount())
		return E_FAIL;

	CCrossbarRouteInfo *pPath = GetPathAtIndex(Index);

	// Going through all the Crossbar and connecting pins. If Mute is False,
	// connecting audio too.
	int PathLength = pPath[0].PathLength;

	for (int i = 0; i < PathLength; i++) {
		hr = pPath[i].pCrossbar->Route(pPath[i].VideoOutputIndex,
			pPath[i].VideoInputIndex);
		if (hr != S_OK)
			return E_FAIL;

		if (!Mute)
			hr = pPath[i].pCrossbar->Route(pPath[i].AudioOutputIndex,
				pPath[i].AudioInputIndex);
		else
			hr = pPath[i].pCrossbar->Route(pPath[i].AudioOutputIndex, -1);
		if (hr != S_OK)
			return E_FAIL;
	}
	return S_OK;
}
