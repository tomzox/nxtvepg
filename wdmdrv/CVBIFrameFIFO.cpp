/*
 *  CVBIFrameFIFO
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
 *    This file is the implementation of the CVBIFrameFIFO object which
 *    is used for implementing a FIFO between the SampleGrabber Filter
 *    and the application. This was introduced because deadlocks occured
 *    between threads before.
 *
 *  Author: Gérard Chevalier
 *
 */

// The following to make DbgLog working in this file.
#ifdef _DEBUG
#define DEBUG
#endif

#include <windows.h>
#include <wxdebug.h>
#include <atlbase.h>
#include <qedit.h>

#include "CVBIFrameFIFO.h"
#include "SampleGrabberCB.h"
#include "WDMDRV.h"

void ErrMsg(LPTSTR sz,...);

CVBIFrameFIFO VBIFrameFIFO;
extern CSampleGrabberCB CB;

#ifdef _DEBUG
extern int DebugLevel;
extern BOOL FakeTimestamp;
extern BOOL DumpTimestamp;
extern BOOL DumpVBIData;
extern HANDLE VBIDumpFileHANDLE;
extern int MaxFramesDumped;
int InitialFramesToSkip = 1000;
#endif

// xx file to be commented

HRESULT CVBIFrameFIFO::InitializeFIFO(int FIFODepth, ULONG EltSize) {
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv : InitializeFIFO (Depth = %d, Elt Size = %d"),
		FIFODepth, EltSize));

	// Creating stuff for VBI data FIFO management (Memory + management + Thread)
	m_FrameFIFODepth = FIFODepth;
	m_EltSize = EltSize;
	m_FrameFIFO = (BYTE *)malloc(m_FrameFIFODepth * m_EltSize);
	m_TimeStamps = (REFERENCE_TIME *)malloc(m_FrameFIFODepth * sizeof(REFERENCE_TIME));
	m_FrameTypes = (int *)malloc(m_FrameFIFODepth * sizeof(int));
	if (m_FrameFIFO == NULL || m_TimeStamps == NULL || m_FrameTypes == NULL) {
		ErrMsg(TEXT("Not enought memory to create VBI Frame FIFO"));
		return WDM_DRV_SYSTEMERR;
	}
//DbgLog((LOG_TRACE,1,TEXT("xxx createFIFO m_TimeStamps = %lx, m_FrameTypes = %lx, m_FrameFIFO = %lx"),
//	   m_TimeStamps, m_FrameTypes, m_FrameFIFO));

	if (!InitializeCriticalSectionAndSpinCount(&m_VBIFIFOCritSection,
		100 || ~(((DWORD) -1) >> 1))) {
		ErrMsg(TEXT("Critical section for VBI Frame FIFO failed"));
		return WDM_DRV_SYSTEMERR;
	}

	m_hFIFOEvent = CreateEvent(NULL, FALSE, FALSE,NULL);
	if (m_hFIFOEvent == NULL) {
		ErrMsg(TEXT("Create Event for VBI Frame FIFO management failed"));
		return WDM_DRV_SYSTEMERR;
	}

	// Initialize FIFO
	m_FramesInFIFO = 0;
	m_FirstFrame = m_LastFrame = 0;
	m_FrameLost = FALSE;

	// Initialize and run the acq thread
	m_ThreadMustStop = FALSE;
	m_hThread = CreateThread(NULL, 0, VBIFIFOManagerThread,
		&VBIFrameFIFO, 0, &m_VBIFIFOManagerThreadId);
	if (m_hThread == NULL) {
		ErrMsg(TEXT("Create Thread for VBI Frame FIFO management failed"));
		return WDM_DRV_SYSTEMERR;
	}
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv : Acq thread id = %Ld"), m_VBIFIFOManagerThreadId));

	return S_OK;
}

HRESULT CVBIFrameFIFO::DestroyFIFO() {
	DWORD Result;
	HRESULT hr = S_OK;

	DbgLog((LOG_TRACE,1,TEXT("WDMDrv : DestroyFIFO")));

	m_ThreadMustStop = TRUE;	// Telling thread to stop
	SetEvent(m_hFIFOEvent);		// Awake it

	// Waiting for the thread end. No need to get it's exit code.
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxxxxxxx waiting end thread")));
	Result = WaitForSingleObject(m_hThread, 500);
	if (Result == WAIT_TIMEOUT) {
		// Thread could not stop properly, killing it and reporting error.
		ErrMsg(TEXT("Acquisition Thread did not stop. killing it"));
		TerminateThread(m_hThread, -1);
		hr = WDM_DRV_SYSTEMERR;
	}
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxxxxxxx waiting end thread : OK")));
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxxxxxxx pointers : ")));
//DbgLog((LOG_TRACE,1,TEXT("xxx m_TimeStamps = %lx, m_FrameTypes = %lx, m_FrameFIFO = %lx"),
//	   m_TimeStamps, m_FrameTypes, m_FrameFIFO));

//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxxxxxxx will free(m_TimeStamps)")));
	free(m_TimeStamps);
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxxxxxxx will free(m_FrameTypes)")));
	free(m_FrameTypes);
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxxxxxxx will free(m_FrameFIFO)")));
	free(m_FrameFIFO);
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxxxxxxx free OK")));
	DeleteCriticalSection(&m_VBIFIFOCritSection);
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxxxxxxx DeleteCriticalSection OK")));
	CloseHandle(m_hFIFOEvent);
	CloseHandle(m_hThread);
//DbgLog((LOG_TRACE,1,TEXT("xxxxxxxxxxxxxxxx end destroy FIFO")));
	return hr;
}

DWORD WINAPI CVBIFrameFIFO::VBIFIFOManagerThread(LPVOID lpParameter) {
	DWORD Result;
	BYTE *CurrentFramePtr;
	BYTE *pb;
	CVBIFrameFIFO *pVBIFrameFIFO;
	bool TimeOutSignaled = FALSE;

	pVBIFrameFIFO = (CVBIFrameFIFO *)lpParameter;

	DbgInitialise(NULL);
	DbgSetModuleLevel(LOG_TRACE, 1);
	DbgLog((LOG_TRACE,1,TEXT("WDMDrv Acquisition Thread started")));

	// Thread main loop processing
	while (true) {
		// Wait for a new frame posted by the SampleGrabber
		Result = WaitForSingleObject(pVBIFrameFIFO->m_hFIFOEvent, 2000);

		// If given the quit flag, exiting the thread
		if (pVBIFrameFIFO->m_ThreadMustStop) {
			DbgLog((LOG_TRACE,1,TEXT("WDMDrv : Exiting acquisition thread")));
			ExitThread(0);
		}

		// See if there was a break in the VBI data flow (for debug purpose)
		if (Result == WAIT_TIMEOUT) {
			if (!TimeOutSignaled) {
				TimeOutSignaled = TRUE;
				DbgLog((LOG_TRACE,1,TEXT("WDMDrv : >>>> VBI data flow stopped")));
			}
			continue;
		}
		if (Result != WAIT_OBJECT_0) {
			if (Result == WAIT_FAILED) {
				ErrMsg(TEXT("Error in WaitForSingleObject in acq thread"));
				ExitThread(-1);
			}
			// Should normally not happen
			DbgLog((LOG_TRACE,1,TEXT("WDMDrv : void VBI FIFO signaling")));
			continue;
		}

		// Now we have frame(s) signaled
		if (TimeOutSignaled) {
			TimeOutSignaled = FALSE;
			DbgLog((LOG_TRACE,1,TEXT("WDMDrv : >>>> VBI data flow Resumed")));
		}

		// If FIFO overrun condition, signal it. xx and caller ?
		if (pVBIFrameFIFO->m_FrameLost) {
			pVBIFrameFIFO->m_FrameLost = FALSE;
			DbgLog((LOG_TRACE,1,TEXT("WDMDrv : ******** Lost Frame(s)in  VBI FIFO")));
		}

		// While we have frames in FIFO
		EnterCriticalSection(&pVBIFrameFIFO->m_VBIFIFOCritSection);
		UINT NbFrames = pVBIFrameFIFO->m_FramesInFIFO;
		LeaveCriticalSection(&pVBIFrameFIFO->m_VBIFIFOCritSection);
		while (NbFrames != 0) {
			// Get a pointer to current frame
			CurrentFramePtr = pVBIFrameFIFO->m_FrameFIFO +
				pVBIFrameFIFO->m_FirstFrame * pVBIFrameFIFO->m_EltSize * sizeof(BYTE);

			// Then call the client application
			if (!pVBIFrameFIFO->m_WantRawVBIData) {
				// We decode the frame here for the application
				// Signal start of frame (ie CallBack param 1 == NULL) and discard it if
				// signaling return FALSE.
				if ((*pVBIFrameFIFO->m_FctToCall)(NULL,
					pVBIFrameFIFO->m_FrameTypes[pVBIFrameFIFO->m_FirstFrame],
					pVBIFrameFIFO->m_TimeStamps[pVBIFrameFIFO->m_FirstFrame]))
				{
					// Sending all lines by iterating over all strides
					for (DWORD dwLine = CB.m_VBIIH.StartLine; 
						dwLine <= CB.m_VBIIH.EndLine; dwLine++)
					{
						pb = CurrentFramePtr + ((dwLine - CB.m_VBIIH.StartLine) * 
							CB.m_VBIIH.StrideInBytes); 

						// Call the Decoding function
						// xx DecodeLine(pb, dwLine); Branch of code no more maintained
					}
				}
			} else	// Want Raw Data : passing whole sample then
#ifdef _DEBUG
			{	// Faking TimeStamp initially introduced for PVR 250 / 350 & ATI AIW
				static REFERENCE_TIME FakedTimeStamp = 0;
				if (FakeTimestamp)
					FakedTimeStamp += 200000;
				else	// Make Faked TimeStamp real
					FakedTimeStamp = pVBIFrameFIFO->m_TimeStamps[pVBIFrameFIFO->m_FirstFrame];

				(void)(*pVBIFrameFIFO->m_FctToCall)(CurrentFramePtr,
					pVBIFrameFIFO->m_FrameTypes[pVBIFrameFIFO->m_FirstFrame],
					FakedTimeStamp);
			}
#else
				(void)(*pVBIFrameFIFO->m_FctToCall)(CurrentFramePtr,
					pVBIFrameFIFO->m_FrameTypes[pVBIFrameFIFO->m_FirstFrame],
					pVBIFrameFIFO->m_TimeStamps[pVBIFrameFIFO->m_FirstFrame]);
#endif
#ifdef _DEBUG
			if (DumpTimestamp)
				// Print start time of frame and interlace information.
				DbgLog((LOG_TRACE,1,TEXT("Sample %I64d, Type %d"),
					pVBIFrameFIFO->m_TimeStamps[pVBIFrameFIFO->m_FirstFrame],
					pVBIFrameFIFO->m_FrameTypes[pVBIFrameFIFO->m_FirstFrame]));

			if (DumpVBIData && (InitialFramesToSkip == 0) && (MaxFramesDumped != 0)) {
				DWORD BytesWritten;
				int NbToWrite;
				char LineBuff[4000*4];	// Suffisant room for a line of VBI samples xx should be malloced 

				// Write start time of frame.
				NbToWrite = sprintf(LineBuff, "Frame time %I64d\n",
					pVBIFrameFIFO->m_TimeStamps[pVBIFrameFIFO->m_FirstFrame]);
				WriteFile(VBIDumpFileHANDLE, LineBuff, NbToWrite, &BytesWritten, NULL);
				MaxFramesDumped--;

				// Dump each line
				for (DWORD dwLine = CB.m_VBIIH.StartLine; 
					dwLine <= CB.m_VBIIH.EndLine; dwLine++) 
				{
					unsigned int j;
					char *pLine;

					pb = CurrentFramePtr + ((dwLine - CB.m_VBIIH.StartLine) * 
						CB.m_VBIIH.StrideInBytes);
					NbToWrite = sprintf(LineBuff, "Line %d\n", dwLine);
					WriteFile(VBIDumpFileHANDLE, LineBuff, NbToWrite, &BytesWritten, NULL);

					for (j = 0, pLine = LineBuff; j < CB.m_VBIIH.SamplesPerLine; j++) {
						char ConvertBuff[10];
						_itoa(*pb++, ConvertBuff, 10 );	// sprintf loads too much the CPU
						strcpy(pLine, ConvertBuff);
						pLine += strlen(ConvertBuff);
						*pLine++ = ' ';
					}
					*pLine++ = '\n';
					NbToWrite = (int) (pLine - LineBuff);

					WriteFile(VBIDumpFileHANDLE, LineBuff, NbToWrite, &BytesWritten, NULL);
				}
			} else if (DumpVBIData && (InitialFramesToSkip != 0))
				InitialFramesToSkip--;
#endif

			// Removing current frame from FIFO
			EnterCriticalSection(&pVBIFrameFIFO->m_VBIFIFOCritSection);
			if (++pVBIFrameFIFO->m_FirstFrame == pVBIFrameFIFO->m_FrameFIFODepth)
				pVBIFrameFIFO->m_FirstFrame = 0;
			pVBIFrameFIFO->m_FramesInFIFO--;
			NbFrames = pVBIFrameFIFO->m_FramesInFIFO;
			LeaveCriticalSection(&pVBIFrameFIFO->m_VBIFIFOCritSection);
		} // While we have frames in FIFO
	} // End Thread main loop
}

void CVBIFrameFIFO::VBIFIFOAddFrame(BYTE *FramePtr, int FrameType, REFERENCE_TIME TimeStamp) {
	BYTE *CurrentFramePtr;

	EnterCriticalSection(&m_VBIFIFOCritSection);

	if (m_FramesInFIFO == m_FrameFIFODepth)
		m_FrameLost = TRUE;
	else {
		CurrentFramePtr = m_FrameFIFO + (m_LastFrame * m_EltSize);
//DbgLog((LOG_TRACE,1,TEXT("xxx add frame at %lx, last = %d, Esz = %d"),CurrentFramePtr,
//	   m_LastFrame,m_EltSize));

		memcpy(CurrentFramePtr, FramePtr, m_EltSize);
		m_TimeStamps[m_LastFrame] = TimeStamp;
		m_FrameTypes[m_LastFrame] = FrameType;
		if (++m_LastFrame == m_FrameFIFODepth)
			m_LastFrame = 0;
		m_FramesInFIFO++;
	}

	LeaveCriticalSection(&m_VBIFIFOCritSection);

	if (!SetEvent(m_hFIFOEvent)) {
		// xx err
	}
}
