/*
 *  Tuner channel to frequency translation
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
 *  Author: Tom Zoerner
 *
 *  $Id: tvchan.h,v 1.6 2002/05/11 15:42:50 tom Exp $
 */

#ifndef __TVCHAN_H
#define __TVCHAN_H


// ---------------------------------------------------------------------------
// declaration of service interface functions
//
bool TvChannels_GetNext( uint *pChan, uint *pFreq );
int  TvChannels_GetCount( void );
void TvChannels_GetName( uint channel, uchar * pName, uint maxNameLen );
void TvChannels_SelectFreqTable( uint tableIdx );
uint TvChannels_NameToFreq( const char * pName );


#endif  // __TVCHAN_H
