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
 *  $Id: btdrv4dummy.c,v 1.15 2003/02/03 21:22:45 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

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
   return FALSE;
}

void BtDriver_StopAcq( void )
{
}

const char * BtDriver_GetLastError( void )
{
   return "application was compiled without VBI device support";
}

bool BtDriver_IsVideoPresent( void )
{
   return FALSE;
}

uint BtDriver_QueryChannel( void )
{
   return 0;
}

bool BtDriver_TuneChannel( int inputIdx, uint freq, bool keepOpen, bool * pIsTuner )
{
   if (pIsTuner != NULL)
   {
      *pIsTuner = FALSE;
   }
   return FALSE;
}

void BtDriver_CloseDevice( void )
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
const char * BtDriver_GetCardName( uint cardIdx )
{
   if (cardIdx == 0)
      return "Bt8x8 dummy";
   else
      return NULL;
}

const char * BtDriver_GetTunerName( uint tunerIdx )
{
   if (tunerIdx == 0)
      return "Tuner dummy";
   else
      return NULL;
}

const char * BtDriver_GetInputName( uint cardIdx, uint cardType, uint inputIdx )
{
   if (inputIdx == 0)
      return "Dummy input";
   else
      return NULL;
}

bool BtDriver_Configure( int cardIndex, int prio, int chipType, int cardType, int tunerType, int pllType )
{
   return TRUE;
}

