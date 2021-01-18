/*
 *  VBI driver interface dummy
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
 *    This is a dummy interface for systems without a Bt8x8 driver.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: btdrv4dummy.c,v 1.23 2007/12/30 21:43:40 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#ifdef WIN32
#include <windows.h>
#endif
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"


volatile EPGACQ_BUF *pVbiBuf;
static EPGACQ_BUF vbiBuf;

// ---------------------------------------------------------------------------
// Interface to acquisition control and EPG scan
//
bool BtDriver_Init( void )
{
   memset(&vbiBuf, 0, sizeof(vbiBuf));
   pVbiBuf = &vbiBuf;

   return TRUE;
}

void BtDriver_Exit( void )
{
}

bool BtDriver_StartAcq( void )
{
#ifdef WIN32
   MessageBox(NULL, "Cannot start acquisition: the application was compiled without VBI device support",
                    "nxtvepg driver problem",
                    MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
#endif
   return FALSE;
}

void BtDriver_StopAcq( void )
{
}

const char * BtDriver_GetLastError( void )
{
#ifndef WIN32
   return "application was compiled without VBI device support";
#else
   // errors are already reported by the driver in WIN32
   return NULL;
#endif
}

#ifdef WIN32
bool BtDriver_Restart( void )
{
   BtDriver_StopAcq();
   return BtDriver_StartAcq();
}

bool BtDriver_GetState( bool * pEnabled, bool * pHasDriver, uint * pCardIdx )
{
   if (pEnabled != NULL)
      *pEnabled = FALSE;
   if (pHasDriver != NULL)
      *pHasDriver = FALSE;
   if (pCardIdx != NULL)
      *pCardIdx = 0;

   return TRUE;
}
#endif

bool BtDriver_IsVideoPresent( void )
{
   return FALSE;
}

bool BtDriver_QueryChannel( uint * pFreq, uint * pInput, bool * pIsTuner )
{
   return FALSE;
}

bool BtDriver_TuneChannel( int inputIdx, uint freq, bool keepOpen, bool * pIsTuner )
{
   if (pIsTuner != NULL)
   {
      *pIsTuner = FALSE;
   }
   return FALSE;
}

void BtDriver_TuneDvbPid( const int * pidList, const int * sidList, uint pidCount )
{
}

void BtDriver_CloseDevice( void )
{
}

bool BtDriver_QueryChannelToken( void )
{
   return FALSE;
}

void BtDriver_SetChannelProfile( VBI_CHANNEL_PRIO_TYPE prio,
                                 int subPrio, int duration, int minDuration )
{
}

bool BtDriver_CheckDevice( void )
{
   return FALSE;
}

#ifndef WIN32
int BtDriver_GetDeviceOwnerPid( void )
{
   return -1;
}
#endif

// ---------------------------------------------------------------------------
// Interface to GUI
//
const char * BtDriver_GetCardName( BTDRV_SOURCE_TYPE drvType, uint cardIdx, bool showDrvErr )
{
   if (cardIdx == 0)
      return "Dummy TV card";
   else
      return NULL;
}

bool BtDriver_CheckCardParams( BTDRV_SOURCE_TYPE drvType, uint sourceIdx, uint input )
{
   return TRUE;
}

BTDRV_SOURCE_TYPE BtDriver_GetDefaultDrvType( void )
{
   return BTDRV_SOURCE_PCI;
}

const char * BtDriver_GetInputName( uint cardIdx, uint cardType, BTDRV_SOURCE_TYPE drvType, uint inputIdx )
{
   if (inputIdx == 0)
      return "Dummy input";
   else
      return NULL;
}

bool BtDriver_Configure( int sourceIdx, BTDRV_SOURCE_TYPE drvType, int prio, int chipType, int cardType,
                         int tunerType, int pllType, bool wdmStop )
{
   return TRUE;
}

void BtDriver_SelectSlicer( VBI_SLICER_TYPE slicerType )
{
}
