/*
 *  Teletext decoder
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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: vbidecode.h,v 1.10 2000/09/28 20:32:05 tom Exp tom $
 */

#ifndef __VBIDECODE_H
#define __VBIDECODE_H


// ---------------------------------------------------------------------------
// Buffer for communication between slave and master process
//
#ifndef WIN32
extern EPGACQ_BUF *pVbiBuf;
#endif

// ---------------------------------------------------------------------------
// declaration of service interface functions
//
void VbiDecodeLine(const uchar *lbuf, int line);

#ifndef WIN32
bool VbiDecodeInit( uchar cardPostfix );
void VbiDecodeExit( void );
bool VbiDecodeFrame( void );
void VbiDecodeCheckParent( void );
bool VbiDecodeWakeUp( void );

bool VbiGetNextChannel( uint *pChan, ulong *pFreq );
int  VbiGetChannelCount( void );
bool VbiTuneChannel( ulong freq, bool keepOpen );
uint VbiTuneGetSignalStrength( void );
void VbiTuneCloseDevice( void );
#endif


#endif // __VBIDECODE_H
