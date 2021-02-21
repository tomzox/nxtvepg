/*
 *  Tuner channel to frequency translation
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *  Description: see C source file.
 */

#ifndef __TVCHAN_H
#define __TVCHAN_H


// ---------------------------------------------------------------------------

#define TV_CHAN_FREQ_DEF(FREQV, NORM) ((FREQV) | ((NORM)<<24))
#define TV_CHAN_GET_FREQ(FREQ)  ((FREQ) & 0xFFFFFF)
#define TV_CHAN_GET_NORM(FREQ)  ((EPGACQ_TUNER_NORM)((FREQ) >> 24))

// ---------------------------------------------------------------------------
// declaration of service interface functions
//
bool TvChannels_GetNext( uint *pChan, uint *pFreq );
int  TvChannels_GetCount( void );
void TvChannels_GetName( uint channel, char * pName, uint maxNameLen );
void TvChannels_SelectFreqTable( uint tableIdx );
uint TvChannels_NameToFreq( const char * pName );
#ifdef WIN32
int  TvChannels_FreqToWdmChannel( uint ifreq, uint norm, uint * pCountry );
#endif


#endif  // __TVCHAN_H
