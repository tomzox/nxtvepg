/*
 *  Win32 VBI capture driver management
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
 *    This module manages M$ Windows drivers for all supported TV cards.
 *    It provides an abstraction layer with higher-level functions,
 *    e.g. to start/stop acquisition or change the channel.
 *
 *    It's based on DSdrv by Mathias Ellinger and John Adcock: a free open-source
 *    driver for PCI cards. The driver offers generic I/O functions to directly
 *    access the hardware (memory map PCI registers, allocate memory for DMA
 *    etc.)  Much of the code to control the specific cards was also taken from
 *    DScaler, although a large part (if not the most) originally came from the
 *    Linux bttv and saa7134 drivers (see also copyrights below).  Linux bttv
 *    was originally ported to Windows by "Espresso".
 *
 *
 *  Authors:
 *
 *    WinDriver adaption (from MultiDec)
 *      Copyright (C) 2000 Espresso (echter_espresso@hotmail.com)
 *
 *    WinDriver replaced with DSdrv Bt8x8 code (DScaler driver)
 *      March 2002 by E-Nek (e-nek@netcourrier.com)
 *
 *    Support for multiple cards on the PCI bus & different drivers,
 *    and "slave mode" when connected to a TV app
 *      Tom Zoerner
 *
 *
 *  $Id: btdrv4win.c,v 1.49 2004/07/11 19:13:11 tom Exp $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/vbidecode.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"

#include "dsdrv/dsdrvlib.h"
#include "dsdrv/tvcard.h"
#include "dsdrv/bt8x8.h"
#include "dsdrv/saa7134.h"
#include "dsdrv/cx2388x.h"
#include "dsdrv/wintuner.h"


// ----------------------------------------------------------------------------
// Declaration of internal variables

typedef struct
{
   WORD   VendorId;
   WORD   DeviceId;
   char * szName;
} TCaptureChip;

#define PCI_ID_BROOKTREE  0x109e
#define PCI_ID_PHILIPS    0x1131
#define PCI_ID_CONEXANT   0x14F1

static const TCaptureChip CaptureChips[] =
{
   { PCI_ID_BROOKTREE, 0x036e, "Brooktree Bt878"  },
   { PCI_ID_BROOKTREE, 0x036f, "Brooktree Bt878A" },
   { PCI_ID_BROOKTREE, 0x0350, "Brooktree Bt848"  },
   { PCI_ID_BROOKTREE, 0x0351, "Brooktree Bt849"  },
   { PCI_ID_PHILIPS,   0x7134, "Philips SAA7134" },
   { PCI_ID_PHILIPS,   0x7133, "Philips SAA7133" },
   { PCI_ID_PHILIPS,   0x7130, "Philips SAA7130" },
   //{ PCI_ID_PHILIPS,  0x7135, "Philips SAA7135" },
   { PCI_ID_CONEXANT,  0x8800, "Conexant CX23881 (Bt881)" }
};
#define CAPTURE_CHIP_COUNT (sizeof(CaptureChips) / sizeof(CaptureChips[0]))

typedef struct
{
   uint  chipIdx;
   uint  chipCardIdx;
   WORD  VendorId;
   WORD  DeviceId;
   DWORD dwSubSystemID;
   DWORD dwBusNumber;
   DWORD dwSlotNumber;
} TVCARD_ID;

#define MAX_CARD_COUNT  4
#define CARD_COUNT_UNINITIALIZED  (MAX_CARD_COUNT + 1)
static TVCARD_ID btCardList[MAX_CARD_COUNT];
static uint      btCardCount;

#define INVALID_INPUT_SOURCE  0xff

static struct
{
   uint        cardIdx;
   uint        cardId;
   uint        tunerType;
   uint        pllType;
   uint        wdmStop;
   uint        threadPrio;
   uint        inputSrc;
   uint        tunerFreq;
   uint        tunerNorm;
} btCfg;

static TVCARD cardif;
static BOOL drvLoaded;
static BOOL shmSlaveMode = FALSE;

volatile EPGACQ_BUF * pVbiBuf;
static EPGACQ_BUF vbiBuf;

static void BtDriver_CountCards( void );
static BOOL BtDriver_OpenCard( uint cardIdx );

// ----------------------------------------------------------------------------
// Helper function to set user-configured priority in IRQ and VBI threads
//
static int BtDriver_GetAcqPriority( int level )
{
   int prio;

   switch (level)
   {
      default:
      case 0: prio = THREAD_PRIORITY_NORMAL; break;
      case 1: prio = THREAD_PRIORITY_ABOVE_NORMAL; break;
      // skipping HIGHEST by (arbitrary) choice
      case 2: prio = THREAD_PRIORITY_TIME_CRITICAL; break;
   }

   return prio;
}

// ---------------------------------------------------------------------------
// Get interface functions for a TV card
//
static bool BtDriver_GetCardInterface( TVCARD * pTvCard, DWORD VendorId, uint cardIdx )
{
   bool result = TRUE;

   memset(pTvCard, 0, sizeof(* pTvCard));

   switch (VendorId)
   {
      case PCI_ID_BROOKTREE:
         Bt8x8_GetInterface(pTvCard);
         break;

      case PCI_ID_PHILIPS:
         SAA7134_GetInterface(pTvCard);
         break;

      case PCI_ID_CONEXANT:
         Cx2388x_GetInterface(pTvCard);
         break;

      default:
         result = FALSE;
         break;
   }

   // copy card parameters into the struct
   if ((btCardCount != CARD_COUNT_UNINITIALIZED) && (cardIdx < btCardCount))
   {
      assert(btCardList[cardIdx].VendorId == VendorId);

      pTvCard->params.BusNumber   = btCardList[cardIdx].dwBusNumber;
      pTvCard->params.SlotNumber  = btCardList[cardIdx].dwSlotNumber;
      pTvCard->params.VendorId    = btCardList[cardIdx].VendorId;
      pTvCard->params.DeviceId    = btCardList[cardIdx].DeviceId;
      pTvCard->params.SubSystemId = btCardList[cardIdx].dwSubSystemID;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Select the video input source
// - which input is tuner and which composite etc. is completely up to the
//   card manufacturer, but it seems that almost all use the 2,3,1,1 muxing
// - returns TRUE in *pIsTuner if the selected source is the TV tuner
//
static bool BtDriver_SetInputSource( uint inputIdx, uint norm, bool * pIsTuner )
{
   bool isTuner = FALSE;
   bool result = FALSE;

   // remember the input source for later
   btCfg.inputSrc = inputIdx;

   if (shmSlaveMode == FALSE)
   {
      if (drvLoaded)
      {
         // XXX TODO norm switch
         result = cardif.cfg->SetVideoSource(&cardif, inputIdx);

         isTuner = cardif.cfg->IsInputATuner(&cardif, inputIdx);
      }
   }
   else
   {  // slave mode -> set param in shared memory
      result = WintvSharedMem_SetInputSrc(inputIdx);
      isTuner = TRUE;  // XXX TODO
   }

   if (pIsTuner != NULL)
      *pIsTuner = isTuner;

   return result;
}

// ---------------------------------------------------------------------------
// Return name for given input source index
// - has to be called repeatedly with incremented indices until NULL is returned
//
const char * BtDriver_GetInputName( uint cardIdx, uint cardType, uint inputIdx )
{
   const char * pName = NULL;
   TVCARD tmpCardIf;
   uint   chipIdx;

   if ((btCardCount != CARD_COUNT_UNINITIALIZED) && (cardIdx < btCardCount))
   {
      chipIdx = btCardList[cardIdx].chipIdx;

      if (BtDriver_GetCardInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, CARD_COUNT_UNINITIALIZED) )
      {
         tmpCardIf.params.cardId = cardType;

         if (inputIdx < tmpCardIf.cfg->GetNumInputs(&tmpCardIf))
            pName = tmpCardIf.cfg->GetInputName(&tmpCardIf, inputIdx);
         else
            pName = NULL;
      }
   }
   return pName;
}

// ---------------------------------------------------------------------------
// Auto-detect card type and return parameters from config table
// - card type may also be set manually, in which case only params are returned
//
bool BtDriver_QueryCardParams( uint cardIdx, sint * pCardType, sint * pTunerType, sint * pPllType )
{
   TVCARD tmpCardIf;
   uint   chipIdx;
   bool   drvWasLoaded;
   uint   loadError;
   bool   result = FALSE;

   if ((pCardType != NULL) && (pTunerType != NULL) && (pPllType != NULL))
   {
      if ((btCardCount != CARD_COUNT_UNINITIALIZED) && (cardIdx < btCardCount))
      {
         if (shmSlaveMode)
         {  // error
            debug0("BtDriver-QueryCardParams: driver is in slave mode");
            MessageBox(NULL, "Cannot query the TV card while connected to a TV application.\nTerminated the TV application and try again.", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         else if (drvLoaded && (btCfg.cardIdx != cardIdx))
         {  // error
            debug2("BtDriver-QueryCardParams: acq running for different card %d instead req. %d", btCfg.cardIdx, cardIdx);
            MessageBox(NULL, "Acquisition is running for a different TV card.\nStop acquisition and try again.", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         else
         {
            drvWasLoaded = drvLoaded;
            if (drvLoaded == FALSE)
            {
               shmSlaveMode = (WintvSharedMem_ReqTvCardIdx(cardIdx) == FALSE);
               if (shmSlaveMode == FALSE)
               {
                  loadError = LoadDriver();
                  if (loadError == HWDRV_LOAD_SUCCESS)
                  {
                     // scan the PCI bus for known cards
                     BtDriver_CountCards();

                     drvLoaded = BtDriver_OpenCard(cardIdx);
                     if (drvLoaded == FALSE)
                        UnloadDriver();
                  }
                  else
                  {
                     MessageBox(NULL, GetDriverErrorMsg(loadError), "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
                  }
               }
            }

            if (drvLoaded)
            {
               chipIdx = btCardList[cardIdx].chipIdx;
               if ( BtDriver_GetCardInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, cardIdx) )
               {
                  if (*pCardType <= 0)
                     *pCardType  = tmpCardIf.cfg->AutoDetectCardType(&tmpCardIf);

                  if (*pCardType > 0)
                  {
                     *pTunerType = tmpCardIf.cfg->AutoDetectTuner(&tmpCardIf, *pCardType);
                     *pPllType   = tmpCardIf.cfg->GetPllType(&tmpCardIf, *pCardType);
                  }
                  else
                     *pTunerType = *pPllType = 0;

                  result = TRUE;
               }

               if (drvWasLoaded == FALSE)
               {
                  UnloadDriver();
                  drvLoaded = FALSE;
               }
            }

            if (drvWasLoaded == FALSE)
            {
               WintvSharedMem_FreeTvCard();
               shmSlaveMode = FALSE;
            }
         }
      }
      else
         debug2("BtDriver-QueryCardParams: PCI bus not scanned or invalid card idx %d >= count %d", cardIdx, btCardCount);
   }
   else
      fatal3("BtDriver-QueryCardParams: illegal NULL ptr params %lx,%lx,%lx", (long)pCardType, (long)pTunerType, (long)pPllType);

   return result;
}

// ---------------------------------------------------------------------------
// Return name from card list for a given chip
//
const char * BtDriver_GetCardNameFromList( uint cardIdx, uint listIdx )
{
   const char * pName = NULL;
   TVCARD tmpCardIf;
   uint chipIdx;

   if ((btCardCount != CARD_COUNT_UNINITIALIZED) && (cardIdx < btCardCount))
   {
      chipIdx = btCardList[cardIdx].chipIdx;

      if ( BtDriver_GetCardInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, CARD_COUNT_UNINITIALIZED) )
      {
         pName = tmpCardIf.cfg->GetCardName(&tmpCardIf, listIdx);
      }
   }
   return pName;
}

// ---------------------------------------------------------------------------
// Return name & chip type for given TV card
// - if the driver was never loaded before the PCI bus is scanned now;
//   no error message displayed if the driver fails to load, but result set to FALSE
// - end of enumeration is indicated by a FALSE result or NULL name pointer
//
bool BtDriver_EnumCards( uint cardIdx, uint cardType, uint * pChipType, const char ** pName, bool showDrvErr )
{
   static char cardName[50];
   TVCARD tmpCardIf;
   uint   chipIdx;
   uint   chipType;
   uint   loadError;
   bool   result = FALSE;

   if ((pChipType != NULL) && (pName != NULL))
   {
      // note: only try to load driver for the first query
      if ( (cardIdx == 0) && (btCardCount == CARD_COUNT_UNINITIALIZED) &&
           (drvLoaded == FALSE) && (shmSlaveMode == FALSE) )
      {
         shmSlaveMode = (WintvSharedMem_ReqTvCardIdx(cardIdx) == FALSE);
         if (shmSlaveMode == FALSE)
         {
            loadError = LoadDriver();
            if (loadError == HWDRV_LOAD_SUCCESS)
            {
               // scan the PCI bus for known cards, but don't open any
               BtDriver_CountCards();
               UnloadDriver();
            }
            else if (showDrvErr)
            {
               MessageBox(NULL, GetDriverErrorMsg(loadError), "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            }

         }
         else if (showDrvErr)
         {
            MessageBox(NULL, "Cannot query the TV card while connected to a TV application.\nTerminated the TV application and try again.", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
         WintvSharedMem_FreeTvCard();
         shmSlaveMode = FALSE;
      }

      if (btCardCount != CARD_COUNT_UNINITIALIZED)
      {
         if (cardIdx < btCardCount)
         {
            chipIdx  = btCardList[cardIdx].chipIdx;
            chipType = (CaptureChips[chipIdx].VendorId << 16) | CaptureChips[chipIdx].DeviceId;

            if ((cardType != 0) && (chipType == *pChipType))
            {
               if ( BtDriver_GetCardInterface(&tmpCardIf, CaptureChips[chipIdx].VendorId, CARD_COUNT_UNINITIALIZED) )
               {
                  *pName = tmpCardIf.cfg->GetCardName(&tmpCardIf, cardType);
               }
               else
                  *pName = "unknown chip type";
            }
            else
            {
               sprintf(cardName, "unknown %s card", CaptureChips[chipIdx].szName);
               *pName = cardName;
            }
            *pChipType = chipType;
         }
         else
         {  // end of enumeration
            *pName = NULL;
         }
         result = TRUE;
      }
      else
      {  // failed to load driver for PCI scan -> just return names for already known cards
         if (*pChipType != 0)
         {
            if ( BtDriver_GetCardInterface(&tmpCardIf, *pChipType >> 16, CARD_COUNT_UNINITIALIZED) )
            {
               *pName = tmpCardIf.cfg->GetCardName(&tmpCardIf, cardType);
            }
            else
               *pName = "unknown card";

            // return 0 as chip type to indicate driver failure
            *pChipType = 0;
         }
         else
         {  // end of enumeration
            if (shmSlaveMode && showDrvErr && (cardIdx == 0))
            {
               MessageBox(NULL, "Cannot scan PCI bus for TV cards while connected to a TV application.\nTerminated the TV application and try again.", "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            }
            *pName = NULL;
         }

         result = TRUE;
      }
   }
   else
      fatal2("BtDriver-EnumCards: illegal NULL ptr params %lx,%lx", (long)pChipType, (long)pName);

   return result;
}

// ---------------------------------------------------------------------------
// Check if the current source has a video signal
//
bool BtDriver_IsVideoPresent( void )
{
   bool result;

   if (drvLoaded)
   {
      result = cardif.ctl->IsVideoPresent();
   }
   else if (shmSlaveMode)
   {  // this operation is currently not supported by the TV app interaction protocol
      // but it's not really needed since normally we'll be using the TV app channel table for a scan
      result = TRUE;
   }
   else
      result = FALSE;

   return result;
}

// ---------------------------------------------------------------------------
// Generate a list of available cards
//
static void BtDriver_CountCards( void )
{
   uint  chipIdx, cardIdx;

   // if the scan was already done skip it
   if (btCardCount == CARD_COUNT_UNINITIALIZED)
   {
      btCardCount = 0;
      for (chipIdx=0; (chipIdx < CAPTURE_CHIP_COUNT) && (btCardCount < MAX_CARD_COUNT); chipIdx++)
      {
         cardIdx = 0;
         while (btCardCount < MAX_CARD_COUNT)
         {
            if (DoesThisPCICardExist(CaptureChips[chipIdx].VendorId, CaptureChips[chipIdx].DeviceId,
                                     cardIdx,
                                     &btCardList[btCardCount].dwSubSystemID,
                                     &btCardList[btCardCount].dwBusNumber,
                                     &btCardList[btCardCount].dwSlotNumber) == ERROR_SUCCESS)
            {
               dprintf4("PCI scan: found capture chip %s, ID=%lx, bus=%ld, slot=%ld\n", CaptureChips[chipIdx].szName, btCardList[btCardCount].dwSubSystemID, btCardList[btCardCount].dwBusNumber, btCardList[btCardCount].dwSlotNumber);
               btCardList[btCardCount].VendorId    = CaptureChips[chipIdx].VendorId;
               btCardList[btCardCount].DeviceId    = CaptureChips[chipIdx].DeviceId;
               btCardList[btCardCount].chipIdx     = chipIdx;
               btCardList[btCardCount].chipCardIdx = cardIdx;
               btCardCount += 1;
            }
            else
            {  // no more cards with this chip -> next chip (outer loop)
               break;
            }
            cardIdx += 1;
         }
      }
      dprintf1("BT8X8-CountCards: found %d cards", btCardCount);
   }
}

// ---------------------------------------------------------------------------
// Open the driver device and allocate I/O resources
//
static BOOL BtDriver_OpenCard( uint cardIdx )
{
   char msgbuf[200];
   int  ret;
   int  chipIdx, chipCardIdx;
   BOOL supportsAcpi;
   BOOL result = FALSE;

   if (cardIdx < btCardCount)
   {
      chipIdx     = btCardList[cardIdx].chipIdx;
      chipCardIdx = btCardList[cardIdx].chipCardIdx;

      if ( BtDriver_GetCardInterface(&cardif, CaptureChips[chipIdx].VendorId, cardIdx) )
      {
         supportsAcpi = cardif.cfg->SupportsAcpi(&cardif);

         ret = pciGetHardwareResources(CaptureChips[chipIdx].VendorId,
                                       CaptureChips[chipIdx].DeviceId,
                                       chipCardIdx,
                                       supportsAcpi);

         if (ret == ERROR_SUCCESS)
         {
            dprintf2("BtDriver-OpenCard: %s driver loaded, card #%d opened\n", CaptureChips[chipIdx].szName, cardIdx);
            result = TRUE;
         }
         else if (ret == 3)
         {  // card found, but failed to open -> abort
            sprintf(msgbuf, "Capture card #%d (with %s chip) cannot be locked!", cardIdx, CaptureChips[chipIdx].szName);
            MessageBox(NULL, msgbuf, "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
         }
      }
   }
   else
   {
      if (cardIdx > 0)
         sprintf(msgbuf, "Capture card #%d not found! (Found %d cards on PCI bus)", cardIdx, btCardCount);
      else
         sprintf(msgbuf, "No supported capture cards found on PCI bus!");
      MessageBox(NULL, msgbuf, "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Shut down the driver and free all resources
// - after this function is called, other processes can use the card
//
static void BtDriver_Unload( void )
{
   if (drvLoaded)
   {
      cardif.ctl->Close(&cardif);
      UnloadDriver();

      drvLoaded = FALSE;
   }
   dprintf0("BtDriver-Unload: driver unloaded\n");
}

// ----------------------------------------------------------------------------
// Boot the driver, allocate resources and initialize all subsystems
//
static bool BtDriver_Load( void )
{
#ifndef WITHOUT_TVCARD
   DWORD loadError;
   bool result;

   assert(shmSlaveMode == FALSE);

   loadError = LoadDriver();
   if (loadError == HWDRV_LOAD_SUCCESS)
   {
      BtDriver_CountCards();

      result = BtDriver_OpenCard(btCfg.cardIdx);
      if (result)
      {
         cardif.params.cardId = btCfg.cardId;

         result = cardif.ctl->Open(&cardif, btCfg.wdmStop);
         if (result)
         {
            drvLoaded = TRUE;

            result = cardif.ctl->Configure(btCfg.threadPrio, btCfg.pllType);
            if (result)
            {
               // initialize tuner module for the configured tuner type
               Tuner_Init(btCfg.tunerType, &cardif);

               if (btCfg.tunerFreq != 0)
               {  // if freq already set, apply it now
                  Tuner_SetFrequency(btCfg.tunerType, btCfg.tunerFreq, btCfg.tunerNorm);
               }
               if (btCfg.inputSrc != INVALID_INPUT_SOURCE)
               {  // if source already set, apply it now
                  BtDriver_SetInputSource(btCfg.inputSrc, VIDEO_MODE_PAL, NULL);
               }
            }
            else
            {  // driver boot failed - abort
               BtDriver_Unload();
            }
         }
         else
         {  // card init failed -> unload driver (do not call card close function)
            UnloadDriver();
         }
      }
      else
      {  // else: user message already generated by open function
         UnloadDriver();
      }
   }
   else
   {  // failed to load the driver
      MessageBox(NULL, GetDriverErrorMsg(loadError), "Nextview EPG driver problem", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      result = FALSE;
   }

   return result;

#else  // WITHOUT_TVCARD
   assert(shmSlaveMode == FALSE);
   drvLoaded = FALSE;
   return TRUE;
#endif
}

// ----------------------------------------------------------------------------
// Check if the parameters are valid for the given source
// - this function is used to warn the user abour parameter mismatch after
//   hardware or driver configuration changes
//
bool BtDriver_CheckCardParams( int cardIdx, int chipId,
                               int cardType, int tunerType, int pll, int input )
{
   TVCARD tmpCardIf;
   bool   result = FALSE;

   if (btCardCount != CARD_COUNT_UNINITIALIZED)
   {
      if (cardIdx < btCardCount)
      {
         if (((btCardList[cardIdx].VendorId << 16) |
               btCardList[cardIdx].DeviceId) == chipId)
         {
            if (BtDriver_GetCardInterface(&tmpCardIf, (chipId >> 16), CARD_COUNT_UNINITIALIZED) )
            {
               result = (tmpCardIf.cfg->GetCardName(&tmpCardIf, cardType) != NULL) &&
                        (tmpCardIf.cfg->GetNumInputs(&tmpCardIf) > input) &&
                         (Tuner_GetName(tunerType) != NULL);
            }
         }
      }
      else
         debug2("BtDriver-CheckCardParams: source index %d no longer valid (>= %d)", cardIdx, btCardCount);
   }
   else
   {  // no PCI scan yet: just do rudimentary checks
      if (cardIdx < CAPTURE_CHIP_COUNT)
      {
         if (chipId != 0)
         {
            if (BtDriver_GetCardInterface(&tmpCardIf, (chipId >> 16), CARD_COUNT_UNINITIALIZED) )
            {
               tmpCardIf.params.cardId = cardType;

               result = (tmpCardIf.cfg->GetCardName(&tmpCardIf, cardType) != NULL) &&
                        (tmpCardIf.cfg->GetNumInputs(&tmpCardIf) > input) &&
                        (Tuner_GetName(tunerType) != NULL);
            }
            else
               debug1("BtDriver-CheckCardParams: unknown PCI ID 0x%X", chipId);
         }
      }
      else
         fatal2("BtDriver-CheckCardParams: illegal source index %d (>= %d)", cardIdx, CAPTURE_CHIP_COUNT);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Set user-configurable hardware parameters
// - called at program start and after config change
// - Important: input source and tuner freq must be set afterwards
//
bool BtDriver_Configure( int cardIdx, int prio, int chipType, int cardType,
                         int tunerType, int pllType, bool wdmStop )
{
   bool cardChange;
   bool cardTypeChange;
   bool pllChange;
   bool tunerChange;
   bool prioChange;
   bool result = TRUE;

   prio = BtDriver_GetAcqPriority(prio);

   if (btCardCount != CARD_COUNT_UNINITIALIZED)
   {  // check if the configuration data still matches the hardware
      if ( (cardIdx >= btCardCount) ||
           (chipType != ((btCardList[cardIdx].VendorId << 16) |
                         btCardList[cardIdx].DeviceId)) )
      {
         ifdebug3(cardIdx < btCardCount, "BtDriver-Configure: PCI chip type of card #%d changed from 0x%X to 0x%X", cardIdx, ((btCardList[cardIdx].VendorId << 16) | btCardList[cardIdx].DeviceId), chipType);
         cardType = tunerType = pllType = wdmStop = 0;
      }
   }

   // check which values change
   cardChange     = ((cardIdx   != btCfg.cardIdx) ||
                     (wdmStop  != btCfg.wdmStop));
   cardTypeChange = (cardType  != btCfg.cardId);
   tunerChange    = (tunerType != btCfg.tunerType);
   pllChange      = (pllType   != btCfg.pllType);
   prioChange     = (prio      != btCfg.threadPrio);

   // save the new values
   btCfg.cardIdx    = cardIdx;
   btCfg.threadPrio = prio;
   btCfg.cardId     = cardType;
   btCfg.tunerType  = tunerType;
   btCfg.pllType    = pllType;
   btCfg.wdmStop    = wdmStop;

   if (shmSlaveMode == FALSE)
   {
      if (drvLoaded)
      {  // acquisition already running -> must change parameters on the fly
         cardif.params.cardId = btCfg.cardId;

         if (cardChange)
         {  // change of TV card -> unload and reload driver
#ifndef WITHOUT_TVCARD
            BtDriver_StopAcq();

            if (BtDriver_StartAcq() == FALSE)
            {
               if (pVbiBuf != NULL)
                  pVbiBuf->hasFailed = TRUE;
               result = FALSE;
            }
#endif
         }
         else
         {  // same card index: just update tuner type and PLL
            if (tunerChange && (btCfg.tunerType != 0) && (btCfg.inputSrc == 0))
            {
               Tuner_Init(btCfg.tunerType, &cardif);
            }

            if (prioChange || pllChange)
            {
               cardif.ctl->Configure(btCfg.threadPrio, btCfg.pllType);
            }

            if (cardTypeChange)
            {
               BtDriver_SetInputSource(btCfg.inputSrc, btCfg.tunerNorm, NULL);
            }
         }
      }
   }
   else
   {  // slave mode -> new card idx
      if (cardChange)
      {
#ifndef WITHOUT_TVCARD
         BtDriver_StopAcq();

         if (BtDriver_StartAcq() == FALSE)
         {
            if (pVbiBuf != NULL)
               pVbiBuf->hasFailed = TRUE;
            result = FALSE;
         }
#endif
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Set slicer type
// - note: slicer type "automatic" not allowed here:
//   type must be decided by upper layers
//
void BtDriver_SelectSlicer( VBI_SLICER_TYPE slicerType )
{
   if ((slicerType != VBI_SLICER_AUTO) && (slicerType < VBI_SLICER_COUNT))
   {
      dprintf1("BtDriver-SelectSlicer: slicer %d\n", slicerType);

      if (pVbiBuf != NULL)
         pVbiBuf->slicerType = slicerType;
   }
   else
      debug1("BtDriver-SelectSlicer: invalid slicer type %d", slicerType);
}

// ----------------------------------------------------------------------------
// Change the tuner frequency
// - makes only sense if TV tuner is input source
//
bool BtDriver_TuneChannel( int inputIdx, uint freq, bool keepOpen, bool * pIsTuner )
{
   uint norm;
   bool result = FALSE;

   norm  = freq >> 24;
   freq &= 0xffffff;

   if (BtDriver_SetInputSource(inputIdx, norm, pIsTuner))
   {
      if (*pIsTuner && (freq != 0))
      {
         // remember frequency for later
         btCfg.tunerFreq = freq;
         btCfg.tunerNorm = norm;

         if (shmSlaveMode == FALSE)
         {
            if (drvLoaded)
            {
               result = Tuner_SetFrequency(btCfg.tunerType, freq, norm);
            }
            else
            {  // driver not loaded -> freq will be tuned upon acq start
               result = TRUE;
            }
         }
         else
         {  // even in slave mode the TV app may have granted the tuner to us
            result = WintvSharedMem_SetTunerFreq(freq);
         }
      }
      else
         result = TRUE;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get the current tuner frequency
//
bool BtDriver_QueryChannel( uint * pFreq, uint * pInput, bool * pIsTuner )
{
   bool result = FALSE;

   if ((pFreq != NULL) && (pInput != NULL) && (pIsTuner != NULL))
   {
      if (shmSlaveMode == FALSE)
      {
         *pFreq = btCfg.tunerFreq | (btCfg.tunerNorm << 24);
         *pInput = btCfg.inputSrc;
         if (drvLoaded)
            *pIsTuner = cardif.cfg->IsInputATuner(&cardif, btCfg.inputSrc);
         else
            *pIsTuner = TRUE;
         result = TRUE;
      }
      else
      {
         *pFreq = WintvSharedMem_GetTunerFreq();
         *pInput = WintvSharedMem_GetInputSource();
         *pIsTuner = TRUE;  // XXX TODO

         result = (*pInput != EPG_REQ_INPUT_NONE) &&
                  (*pFreq != EPG_REQ_FREQ_NONE);
      }
   }
   else
      debug0("BtDriver-QueryChannel: invalid NULL ptr params");

   return result;
}

// ----------------------------------------------------------------------------
// Dummies - not used for Windows
//
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

// ----------------------------------------------------------------------------
// Check if the driver has control over the TV channel selection
//
bool BtDriver_CheckDevice( void )
{
   return (shmSlaveMode == FALSE);
}

// ----------------------------------------------------------------------------
// Retrieve identifier strings for supported tuner types
// - called by user interface
//
const char * BtDriver_GetTunerName( uint idx )
{
   return Tuner_GetName(idx);
}

// ---------------------------------------------------------------------------
// Query if acquisition is currently enabled and in which mode
// - return value pointers may be NULL if value not required
//
bool BtDriver_GetState( bool * pEnabled, bool * pHasDriver, uint * pCardIdx )
{
   if (pEnabled != NULL)
      *pEnabled = (shmSlaveMode | drvLoaded);
   if (pHasDriver != NULL)
      *pHasDriver = drvLoaded;
   if (pCardIdx != NULL)
      *pCardIdx = btCfg.cardIdx;

   return TRUE;
}

// ---------------------------------------------------------------------------
// Stop and start driver -> toggle slave mode
// - called when a TV application attaches or detaches
// - should not be used to change parameters - used Configure instead
//
bool BtDriver_Restart( void )
{
   bool result;
   uint prevFreq, prevNorm, prevInput;

   // save current input settings into temporary variables
   prevFreq  = btCfg.tunerFreq;
   prevNorm  = btCfg.tunerNorm;
   prevInput = btCfg.inputSrc;

   BtDriver_StopAcq();

   // restore input params
   btCfg.tunerFreq = prevFreq;
   btCfg.tunerNorm = prevNorm;
   btCfg.inputSrc  = prevInput;

   // start acquisition
   result = BtDriver_StartAcq();

   // inform acq control if acq is switched off now
   if (pVbiBuf != NULL)
      pVbiBuf->hasFailed = !result;

   return result;
}

// ---------------------------------------------------------------------------
// Start acquisition
// - the driver is automatically loaded and initialized
//
bool BtDriver_StartAcq( void )
{
   bool result = FALSE;

   if ( (shmSlaveMode == FALSE) && (drvLoaded == FALSE) )
   {
      // check if the configured card is currently free
      shmSlaveMode = (WintvSharedMem_ReqTvCardIdx(btCfg.cardIdx) == FALSE);
      if (shmSlaveMode == FALSE)
      {
         if (pVbiBuf != NULL)
         {
            // load driver & initialize driver for selected TV card
            if (BtDriver_Load())
            {
#ifndef WITHOUT_TVCARD
               if (drvLoaded)
               {
                  pVbiBuf->hasFailed = FALSE;
                  result = cardif.ctl->StartAcqThread();
               }
               else
                  result = FALSE;

               if (result == FALSE)
                  UnloadDriver();
#else
               result = TRUE;
#endif
            }
         }

         if (result == FALSE)
            WintvSharedMem_FreeTvCard();
      }
      else
      {  // TV card is already used by TV app -> slave mode
         dprintf0("BtDriver-StartAcq: starting in slave mode");

         assert(pVbiBuf == &vbiBuf);
         pVbiBuf = WintvSharedMem_GetVbiBuf();
         if (pVbiBuf != NULL)
         {
            memcpy((char *)pVbiBuf, &vbiBuf, sizeof(*pVbiBuf));
            pVbiBuf->hasFailed = FALSE;
            result = TRUE;
         }
         else
            WintvSharedMem_FreeTvCard();
      }
   }
   else
   {  // acq already active - should never happen
      debug0("BtDriver-StartAcq: driver already loaded");
      result = TRUE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition
// - the driver is automatically stopped and removed
//
void BtDriver_StopAcq( void )
{
   if (shmSlaveMode == FALSE)
   {
      if (drvLoaded)
      {
         cardif.ctl->StopAcqThread();
      }

      BtDriver_Unload();

      // notify connected TV app that card & driver are now free
      WintvSharedMem_FreeTvCard();
   }
   else
   {
      if (pVbiBuf != NULL)
      {
         dprintf0("BtDriver-StopAcq: stopping slave mode acq");

         // clear requests in shared memory
         WintvSharedMem_FreeTvCard();

         // switch back to the internal VBI buffer
         assert(pVbiBuf != &vbiBuf);
         memcpy(&vbiBuf, (char *)pVbiBuf, sizeof(vbiBuf));
         pVbiBuf = &vbiBuf;
      }
      else
         debug0("BtDriver-StopAcq: shared memory not allocated");

      shmSlaveMode = FALSE;
   }

   btCfg.tunerFreq  = 0;
   btCfg.tunerNorm  = VIDEO_MODE_PAL;
   btCfg.inputSrc   = INVALID_INPUT_SOURCE;
}

// ---------------------------------------------------------------------------
// Query error description for last failed operation
// - currently unused because errors are already reported by the driver in WIN32
//
const char * BtDriver_GetLastError( void )
{
   return NULL;
}

// ---------------------------------------------------------------------------
// Initialize the driver module
// - called once at program start
//
bool BtDriver_Init( void )
{
   memset(&vbiBuf, 0, sizeof(vbiBuf));
   pVbiBuf = &vbiBuf;

   memset(&btCfg, 0, sizeof(btCfg));
   btCfg.tunerType = TUNER_ABSENT;
   btCfg.inputSrc  = INVALID_INPUT_SOURCE;
   btCardCount = CARD_COUNT_UNINITIALIZED;
   drvLoaded = FALSE;

   return TRUE;
}

// ---------------------------------------------------------------------------
// Clean up the driver module for exit
// - called once at program termination
//
void BtDriver_Exit( void )
{
   if (drvLoaded)
   {  // acq is still running - should never happen
      BtDriver_StopAcq();
   }
}

