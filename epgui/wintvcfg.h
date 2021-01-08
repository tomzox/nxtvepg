/*
 *  Retrieve channel table and TV card config from TV app INI files
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: wintvcfg.h,v 1.5 2006/11/26 12:56:52 tom Exp tom $
 */

#ifndef __WINTVCFG_H
#define __WINTVCFG_H


void WintvCfg_ExtractName( const char * pNameTab, uint count, uint chanIdx, char * pBuf, uint bufSize );
bool WintvCfg_GetFreqTab( char ** ppNameTab, EPGACQ_TUNER_PAR ** ppFreqTab, uint * pCount, char ** ppErrMsg );
bool WintvCfg_GetChanTab( uint appIdx, const char * pChanTabPath, char ** ppErrMsg,
                          char ** ppNameTab, EPGACQ_TUNER_PAR ** ppFreqTab, uint * pCount );
char * WintvCfg_GetRcPath( const char * pBase, uint appIdx );
bool WintvCfg_QueryApp( uint appIdx, const char ** ppAppName, bool * pNeedPath );
uint WintvCfg_GetAppIdx( void );
bool WintvCfg_IsEnabled( void );
const char * WintvCfg_GetName( void );


#endif  // __WINTVCFG_H
