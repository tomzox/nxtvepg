/*
 *  SampleGrabberCB include file
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
 *    This header file declares the SampleGrabberCB Class.
 *
 *
 *  Author: Gérard Chevalier
 *
 */
#pragma once
//#include <qedit.h>
#include <ks.h>
#include <ksmedia.h>

//
// Implementation of CSampleGrabberCB object
//
// Note: this object is a SEMI-COM object, and can only be created statically.
class CSampleGrabberCB : public ISampleGrabberCB {
	void DecodeLine(BYTE* pLine, DWORD dwLine);
	int FindFirstByteOnLine(BYTE* pLine, int& off) const;
	BYTE VTScan(const BYTE * pLine, DWORD * spos, int off);
private:
	// Internal static data xxgg : for ttx decoding
	static const int	AGC_START_OFF = 120;
	static const int	AGC_LENGTH = 300;
	static const int	PEAK_SEARCH_LEN = AGC_LENGTH;
	static const int	PEAK_SEARCH_OFF = 0;
	static const int	FPSHIFT = 16;
	static const int	FPFAC = 1 << FPSHIFT;
	static const unsigned long VT_RATE_CONV = 6937500L;

	// Internal data members xxgg : for ttx decoding
	int						m_iVTStep;

public:
	KS_VBIINFOHEADER m_VBIIH;

	CSampleGrabberCB(void);
	~CSampleGrabberCB(void);

	void SetSamplingRate(ULONG SamplingRate);

    // Fake out any COM ref counting
    //
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // Fake out any COM QI'ing
    //
    STDMETHODIMP QueryInterface(REFIID riid, void ** ppv);

    // We don't implement this one
    //
    STDMETHODIMP SampleCB( double SampleTime, IMediaSample * pSample );

    // The sample grabber is calling us back on its deliver thread.
    // This is NOT the main app thread!
    //
    STDMETHODIMP BufferCB( double SampleTime, BYTE * pBuffer, long BufferSize );
};
