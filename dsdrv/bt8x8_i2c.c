/*
 *  M$ Windows Bt8x8 I2C line driver
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
 *    This module is a "user-space driver" for the I2C bus attached
 *    to Bt8x8 chips.  This driver does not use the hardware I2C
 *    capabilities of the Bt8x8; instead every bit of the protocol
 *    is implemented in software, i.e. every bit is handled
 *    explicitly -> hence the name "line driver"
 *
 *    The code was copied from DScaler I2CBusForLineInterface.cpp
 *
 *
 *  Authors:
 *
 *      Copyleft 2001 itt@myself.com
 *
 *  DScaler #Id: I2CBusForLineInterface.cpp,v 1.4 2001/12/08 13:43:20 adcockj Exp #
 *  DScaler #Id: I2CBus.cpp,v 1.2 2002/09/27 14:10:25 kooiman Exp #
 *
 *  $Id: bt8x8_i2c.c,v 1.6 2003/03/09 19:28:10 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <windows.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "dsdrv/dsdrvlib.h"
#include "dsdrv/tvcard.h"
#include "dsdrv/bt8x8_reg.h"
#include "dsdrv/bt8x8_i2c.h"

typedef const TVCARD * CPTVC;

// ---------------------------------------------------------------------------
// I2C line bus algorithm
//
static void Bt8x8_I2cSetSDALo( CPTVC pTvCard )
{
   pTvCard->i2cLineBus->SetSDA(FALSE);
   pTvCard->i2cLineBus->Sleep();
}

static void Bt8x8_I2cSetSDAHi( CPTVC pTvCard )
{
   pTvCard->i2cLineBus->SetSDA(TRUE);
   pTvCard->i2cLineBus->Sleep();
}

static void Bt8x8_I2cSetSCLLo( CPTVC pTvCard )
{
   pTvCard->i2cLineBus->SetSCL(FALSE);
   pTvCard->i2cLineBus->Sleep();
}

static void Bt8x8_I2cSetSCLHi( CPTVC pTvCard )
{
   pTvCard->i2cLineBus->SetSCL(TRUE);
   pTvCard->i2cLineBus->Sleep();
   while (!pTvCard->i2cLineBus->GetSCL())
   {
      /* the hw knows how to read the clock line,
       * so we wait until it actually gets high.
       * This is safer as some chips may hold it low
       * while they are processing data internally.
       */
      pTvCard->i2cLineBus->SetSCL(TRUE);
      /// \todo FIXME yield here/timeout
   }
}

static void Bt8x8_I2cStart( CPTVC pTvCard )
{
   dprintf0("I2C BusForLine Start\n");
   // I2C start: SDA 1 -> 0 with SCL = 1
   // SDA   ^^^\___
   // SCL ___/^^^\_____
   //
   Bt8x8_I2cSetSCLLo(pTvCard);
   Bt8x8_I2cSetSDAHi(pTvCard);
   Bt8x8_I2cSetSCLHi(pTvCard);
   Bt8x8_I2cSetSDALo(pTvCard);
   Bt8x8_I2cSetSCLLo(pTvCard);
}

static void Bt8x8_I2cStop( CPTVC pTvCard )
{
   dprintf0("I2C BusForLine Stop\n");
   // I2C stop: SDA 0 -> 1 with SCL = 1
   // SDA    ___/^^^
   // SCL ____/^^^
   Bt8x8_I2cSetSCLLo(pTvCard);
   Bt8x8_I2cSetSDALo(pTvCard);
   Bt8x8_I2cSetSCLHi(pTvCard);
   Bt8x8_I2cSetSDAHi(pTvCard);
}

static bool Bt8x8_I2cGetAcknowledge( CPTVC pTvCard )
{
   bool result;

   Bt8x8_I2cSetSCLLo(pTvCard);
   Bt8x8_I2cSetSDAHi(pTvCard);

   // SDA = 0 means the slave ACK'd
   result = pTvCard->i2cLineBus->GetSDA();

   Bt8x8_I2cSetSCLHi(pTvCard);
   Bt8x8_I2cSetSCLLo(pTvCard);

   dprintf0(result ? "I2C BusForLine got NAK\n" : "I2C BusForLine got ACK\n");
   return !result;
}

static void Bt8x8_I2cSendNAK( CPTVC pTvCard )
{
   dprintf0("I2C BusForLine send NAK\n");
   Bt8x8_I2cSetSCLLo(pTvCard);
   Bt8x8_I2cSetSDAHi(pTvCard);
   Bt8x8_I2cSetSCLHi(pTvCard);
   Bt8x8_I2cSetSCLLo(pTvCard);
   Bt8x8_I2cSetSDALo(pTvCard);
}

static void Bt8x8_I2cSendACK( CPTVC pTvCard )
{
   dprintf0("I2C BusForLine send ACK\n");
   Bt8x8_I2cSetSCLLo(pTvCard);
   Bt8x8_I2cSetSDALo(pTvCard);
   Bt8x8_I2cSetSCLHi(pTvCard);
   Bt8x8_I2cSetSCLLo(pTvCard);
   Bt8x8_I2cSetSDAHi(pTvCard);
}

static bool Bt8x8_SendByte( CPTVC pTvCard, BYTE byte )
{
   BYTE mask;

   dprintf1("I2C BusForLine NAK Write %02X\n", byte);

   for(mask = 0x80; mask > 0; mask /= 2)
   {
      Bt8x8_I2cSetSCLLo(pTvCard);
      if ((byte & mask) != 0)
      {
          Bt8x8_I2cSetSDAHi(pTvCard);
      }
      else
      {
          Bt8x8_I2cSetSDALo(pTvCard);
      }
      Bt8x8_I2cSetSCLHi(pTvCard);
   }
   return Bt8x8_I2cGetAcknowledge(pTvCard);
}

static BYTE Bt8x8_I2cReadByte( CPTVC pTvCard, bool last )
{
   BYTE mask;
   BYTE result = 0;

   Bt8x8_I2cSetSDAHi(pTvCard);
   for (mask = 0x80; mask > 0; mask /= 2)
   {
      Bt8x8_I2cSetSCLLo(pTvCard);
      Bt8x8_I2cSetSCLHi(pTvCard);
      if (pTvCard->i2cLineBus->GetSDA())
      {
          result |= mask;
      }
   }
   dprintf1("I2C BusForLine Read %02X\n", result);
   if (last)
   {
      Bt8x8_I2cSendNAK(pTvCard);
   }
   else
   {
      Bt8x8_I2cSendACK(pTvCard);
   }
   return result;
}

// ---------------------------------------------------------------------------
// Read the given number of bytes from the given device address
//
static bool Bt8x8I2c_Read( const TVCARD * pTvCard,
                           const BYTE *writeBuffer, size_t writeBufferSize,
                           BYTE *readBuffer, size_t readBufferSize )
{
   BYTE address;
   size_t idx;

   if ((pTvCard != NULL) && (pTvCard->i2cLineBus != NULL))
   {
      assert(writeBuffer != NULL);
      assert(writeBufferSize >= 1);
      assert((readBuffer != NULL) || (readBufferSize == 0));

      if (readBufferSize == 0)
         return TRUE;

      address = writeBuffer[0];

      if (writeBufferSize != 1)
      {
         assert(writeBufferSize > 1);

         Bt8x8_I2cStart(pTvCard);

         // send the address
         if (!Bt8x8_SendByte(pTvCard, address & ~1))
         {
            debug1("Bt8x8I2c-Read: SendByte(0x%x) returned FALSE for write address", address & ~1);
            Bt8x8_I2cStop(pTvCard);
            return FALSE;
         }

         for (idx = 1; idx < (writeBufferSize - 1); idx++)
         {
            if(!Bt8x8_SendByte(pTvCard, writeBuffer[idx]))
            {
               Bt8x8_I2cStop(pTvCard);
               return FALSE;
            }
         }

         // The last byte may also create a positive acknowledge, indicating, that
         // the device is "full", which is not an error.
         if (writeBufferSize >= 2)
            Bt8x8_SendByte(pTvCard, writeBuffer[writeBufferSize - 1]);
      }

      Bt8x8_I2cStart(pTvCard);

      // The read address requires a negative ack
      if (!Bt8x8_SendByte(pTvCard, address | 1))
      {
         debug1("Bt8x8I2c-Read: SendByte(0x%x) returned FALSE for write address", address | 1);
         Bt8x8_I2cStop(pTvCard);
         return FALSE;
      }

      for (idx = 0; idx < (readBufferSize - 1); idx++)
      {
         readBuffer[idx] = Bt8x8_I2cReadByte(pTvCard, FALSE);
      }
      readBuffer[idx] = Bt8x8_I2cReadByte(pTvCard, TRUE);
      Bt8x8_I2cStop(pTvCard);
   }
   else
      fatal2("Bt8x8I2c-Read: illegal NULL ptr params %lX, %lX", (long)pTvCard, (long)((pTvCard != NULL) ? pTvCard->i2cLineBus : NULL));

   return TRUE;
}

// ---------------------------------------------------------------------------
// Write the given bytes to the given device address
//
static bool Bt8x8I2c_Write( const TVCARD * pTvCard,
                            const BYTE * writeBuffer, size_t writeBufferSize )
{
   size_t idx;
   bool   result = FALSE;

   if ((pTvCard != NULL) && (pTvCard->i2cLineBus != NULL))
   {
      if (writeBufferSize > 0)
      {
         assert((writeBuffer[0] & 1) == 0);

         Bt8x8_I2cStart(pTvCard);

         result = TRUE;
         for (idx=0; (idx < writeBufferSize) && result; idx++)
         {
            result &= Bt8x8_SendByte(pTvCard, writeBuffer[idx]);
         }
         Bt8x8_I2cStop(pTvCard);
      }
      else
         fatal0("Bt8x8I2c-Write: called with 0 bytes to write");
   }
   else
      fatal2("Bt8x8I2c-Write: illegal NULL ptr params %lX, %lX", (long)pTvCard, (long)((pTvCard != NULL) ? pTvCard->i2cLineBus : NULL));

   return result;
}

// ---------------------------------------------------------------------------
// Fill interface struct
//
static const I2C_RW Bt8x8_I2cInterface =
{
   Bt8x8I2c_Read,
   Bt8x8I2c_Write,
};

void Bt8x8I2c_GetInterface( TVCARD * pTvCard )
{
   pTvCard->i2cBus = &Bt8x8_I2cInterface;
}

