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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: btdrv4dummy.c,v 1.1 2000/12/18 19:43:45 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"


EPGACQ_BUF *pVbiBuf;
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

bool BtDriver_IsVideoPresent( void )
{
   return FALSE;
}

bool BtDriver_TuneChannel( ulong freq, bool keepOpen )
{
   return FALSE;
}

bool BtDriver_SetInputSource( int inputIdx, bool keepOpen, bool * pIsTuner )
{
   if (pIsTuner != NULL)
   {
      *pIsTuner = FALSE;
   }
   return FALSE;
}


void BtDriver_CheckParent( void )
{
}

void BtDriver_CloseDevice( void )
{
}

bool BtDriver_CheckDevice( void )
{
   return FALSE;
}

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

const char * BtDriver_GetInputName( uint cardIdx, uint inputIdx )
{
   if (inputIdx == 0)
      return "Dummy input";
   else
      return NULL;
}

void BtDriver_Configure( int cardIndex, int tunerType, int pll, int prio )
{
}

