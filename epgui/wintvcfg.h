/*
 *  Retrieve channel table and TV card config from TV app INI files
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

#ifndef __WINTVCFG_H
#define __WINTVCFG_H

#ifdef WIN32
#include "epgvbi/winshm.h"
#else
#include "epgui/rcfile.h"
#endif

typedef struct
{
   const char             * pNameTab;
   const EPGACQ_TUNER_PAR * pFreqTab;
   uint                     chanCount;
   uint                     tvAppIdx;
} TVAPP_CHAN_TAB;

const TVAPP_CHAN_TAB * WintvCfg_GetFreqTab( char ** ppErrMsg );
void WintvCfg_ExtractName( const TVAPP_CHAN_TAB * pChanTab, uint chanIdx, char * pBuf, uint bufSize );
void WintvCfg_InvalidateCache( void );

bool WintvCfg_GetChanTab( uint appIdx, const char * pChanTabPath, char ** ppErrMsg,
                          char ** ppNameTab, EPGACQ_TUNER_PAR ** ppFreqTab, uint * pCount );
char * WintvCfg_GetRcPath( const char * pBase, uint appIdx );
bool WintvCfg_QueryApp( uint appIdx, const char ** ppAppName, int * pNeedPath );
TVAPP_NAME WintvCfg_GetAppIdx( void );
bool WintvCfg_IsEnabled( void );
const char * WintvCfg_GetName( void );
void WintvCfg_Destroy( void );


#endif  // __WINTVCFG_H
