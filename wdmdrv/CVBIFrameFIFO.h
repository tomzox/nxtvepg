/*
 *  CVBIFrameFIFO include file
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
 *    This header file declares the CVBIFrameFIFO Class and associated functions.
 *
 *
 *  Author: Gérard Chevalier
 *
 */
#include <WinBase.h>
#include <ks.h>			// For ksmedia include
#include <ksmedia.h>	// For REFERENCE_TIME define

// Definition for application Callback
typedef BOOL (__stdcall *LPFN_CALLBACK)(BYTE *WSTPacket, int FrameType, LONGLONG LineOrTime);

class CVBIFrameFIFO {
private:
public:
	BYTE *m_FrameFIFO;
	REFERENCE_TIME *m_TimeStamps;
	int *m_FrameTypes;
	CRITICAL_SECTION m_VBIFIFOCritSection;
	UINT m_FrameFIFODepth;
	UINT m_FramesInFIFO, m_FirstFrame, m_LastFrame;
	ULONG m_EltSize;
	bool m_FrameLost;
	LPFN_CALLBACK m_FctToCall;	// Function to call in the client app
	BOOL m_WantRawVBIData;

	HANDLE m_hFIFOEvent;
	const static int START = 1;
	const static int STOP = 2;

	HANDLE m_hThread;
	bool m_ThreadMustStop;
	DWORD m_VBIFIFOManagerThreadId;

	HRESULT InitializeFIFO(int FIFODepth, ULONG EltSize);
	HRESULT DestroyFIFO();
	void VBIFIFOAddFrame(BYTE *FramePtr, int FrameType, REFERENCE_TIME TimeStamp);
	static DWORD WINAPI VBIFIFOManagerThread(LPVOID lpParameter);
};
