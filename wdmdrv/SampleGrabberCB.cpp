/*
 *  SampleGrabberCB
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
 *    This file is the implementation of the SampleGrabber CallBack object
 *    which is used for implementing the Callback associated with the
 *    SampleGrabber DirectShow Filter.
 *
 *  Author: Gérard Chevalier
 *
 */
#include <windows.h>
#include <wxdebug.h>
#include <atlbase.h>
#include <qedit.h>
// Next include for interlace status definitions
#include <dvdmedia.h>

//#include "StdAfx.h"
#include "SampleGrabberCB.h"
#include "CVBIFrameFIFO.h"
#include "WDMDRV.h"

CSampleGrabberCB CB;
extern CVBIFrameFIFO VBIFrameFIFO;

CSampleGrabberCB::CSampleGrabberCB(void) {
}

CSampleGrabberCB::~CSampleGrabberCB(void) {
}

STDMETHODIMP_(ULONG) CSampleGrabberCB::AddRef() { return 2; }
STDMETHODIMP_(ULONG) CSampleGrabberCB::Release() { return 1; }

STDMETHODIMP CSampleGrabberCB::QueryInterface(REFIID riid, void ** ppv) {
	CheckPointer(ppv,E_POINTER);

	if( riid == IID_ISampleGrabberCB || riid == IID_IUnknown ) {
		*ppv = (void *) static_cast<ISampleGrabberCB*> ( this );
		return NOERROR;
	}    

	return E_NOINTERFACE;
}

// Samplerate setting and VBI extraction parameters calculation
void CSampleGrabberCB::SetSamplingRate(ULONG SamplingRate) {
	// xxgg
	m_iVTStep = static_cast<int>(((double)FPFAC * SamplingRate / VT_RATE_CONV));
}

// CallBack Method with MediaSample API type.
// The sample grabber is calling us back on its deliver thread.
// This is NOT the main app thread!
STDMETHODIMP CSampleGrabberCB::SampleCB(double SampleTime, IMediaSample *pSample ) {
	IMediaSample2 *pSample2;
	int		MediaSampleSize, FrameType;
	BYTE	*pVBIData;  // Pointer to VBI data
	ULONG	nBytes;
	HRESULT hr;
	REFERENCE_TIME t1,t2;

	ASSERT(pSample != NULL);

	// Get the Sample time with GetTime to have Int64 instead of float to do integer arithmetic
	pSample->GetTime(&t1,&t2);

	// Ignore samples of the wrong size!!!
	if ((MediaSampleSize = pSample->GetActualDataLength()) != (int)m_VBIIH.BufferSize) {
		return NOERROR;
	}

	pSample->GetPointer(&pVBIData);
	ASSERT(pVBIData != NULL);

	nBytes = pSample->GetActualDataLength();
	ASSERT (nBytes == (m_VBIIH.EndLine - m_VBIIH.StartLine + 1) *
		m_VBIIH.StrideInBytes);

	// Get the interlaced / progressive and if necessary even / odd status of current frame
	FrameType = UNKNOWN_FRAME;
	hr = pSample->QueryInterface(IID_IMediaSample2,(void**)&pSample2);
	if(SUCCEEDED(hr)) {
		AM_SAMPLE2_PROPERTIES prop;
		hr = pSample2->GetProperties(sizeof(AM_SAMPLE2_PROPERTIES),(BYTE*)&prop);
		if(SUCCEEDED(hr)) {
			switch(prop.dwTypeSpecificFlags & AM_VIDEO_FLAG_FIELD_MASK) {
				case AM_VIDEO_FLAG_INTERLEAVED_FRAME:
					FrameType = PROGRESSIVE_FRAME;	// Not obvious despite the name !
					break;
				case AM_VIDEO_FLAG_FIELD1:
					FrameType = FIELD_ONE;
					break;
				case AM_VIDEO_FLAG_FIELD2:
					FrameType = FIELD_TWO;
					break;
			}
		}
		pSample2->Release();
	}

	// Write the frame in the VBI FIFO
	VBIFrameFIFO.VBIFIFOAddFrame(pVBIData, FrameType, t1);

	return NOERROR;
}

//
// CallBack Method with Buffer API type : not used.
STDMETHODIMP CSampleGrabberCB::BufferCB( double SampleTime, BYTE * pBuffer, long BufferSize ) {
	return 0;
}

int CSampleGrabberCB::FindFirstByteOnLine(BYTE* pLine, int& off) const {
   int i, p;
   int thresh, min = 255, max = 0;

   /* automatic gain control */
   for (i = AGC_START_OFF; i < AGC_START_OFF + AGC_LENGTH; i++)
   {
      if (pLine[i] < min)
         min = pLine[i];
      if (pLine[i] > max)
         max = pLine[i];
   }
   thresh = (max + min) / 2;
   off = 128 - thresh;

   // search for first 1 bit (VT always starts with 55 55 27)
   p = PEAK_SEARCH_OFF;
   while ((pLine[p] < thresh) && (p < PEAK_SEARCH_OFF + PEAK_SEARCH_LEN))
      p++;
   // search for maximum of 1st peak
   while ((pLine[p + 1] >= pLine[p]) && (p < PEAK_SEARCH_OFF + PEAK_SEARCH_LEN))
      p++;

   return (p << FPSHIFT);
}

BYTE CSampleGrabberCB::VTScan(const BYTE * pLine, DWORD * spos, int off) {
   int j;
   BYTE theByte;

   theByte = 0;
   for (j = 7; j >= 0; j--, *spos += m_iVTStep)
   {
      theByte |= ((pLine[*spos >> FPSHIFT] + off) & 0x80) >> j;
   }

   return theByte;
}


void CSampleGrabberCB::DecodeLine(BYTE* pLine, DWORD dwLine)
{
   BYTE		WSTPacket[45];
   int		off = 0;
   DWORD	spos = 0;
   DWORD    dpos = 0;

   // Perform the automatic gain control and find the 
   // first byte of this line.
   spos = dpos = FindFirstByteOnLine(pLine, off);
   /* ignore first bit for now */
   WSTPacket[0] = VTScan(pLine, &spos, off);

   if ((WSTPacket[0] & 0xfe) == 0x54)
   {
      WSTPacket[1] = VTScan(pLine, &spos, off);
      switch (WSTPacket[1])
      {
         case 0x75:             /* missed first two 1-bits, TZ991230++ */
            //printf("****** step back by 2\n");
            spos -= 2 * m_iVTStep;
            WSTPacket[1] = 0xd5;

		 case 0xd5:             /* oops, missed first 1-bit: backup 2 bits */
            //printf("****** step back by 1\n");
            spos -= 2 * m_iVTStep;
            WSTPacket[1] = 0x55;

         case 0x55: // Teletext packet
            WSTPacket[2] = VTScan(pLine, &spos, off);
            switch (WSTPacket[2])
            {
			case 0xd8:       /* this shows up on some channels?!?!? */
				//for (i=3; i<45; i++) 
				//  WSTPacket[i]=VTScan(pLine, &spos, off);
				//return;

			case 0x27:
				{
					// Copy the WST bytes to the WST buffer
					for (int i = 3; i < 45; i++)
						WSTPacket[i] = VTScan(pLine, &spos, off);

					// Pass the decoded data
					//xxxxxxx(void)(*CB.FctToCall)(&WSTPacket[3], (LONGLONG)dwLine);
				}
				break;

			default:
				//printf("****** line=%d  [2]=%x != 0x27 && 0xd8\n", line, WSTPacket[2]);
				break;
            }
            break;

         default:
            //printf("****** line=%d  [1]=%x != 0x55 && 0xd5\n", line, WSTPacket[1]);
            break;
      }
   }
}
