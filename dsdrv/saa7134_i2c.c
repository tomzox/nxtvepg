/*
 *  Win32 I2C driver for the Philips SAA7134 chip
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
 *    This module is a "user-space driver" for the Philips SAA7134 I2C bus
 *    and allows communication with peripheral devices like tuner chips.
 *    The source code was copied from DScaler (only converted to C)
 *
 *  Author:
 *    This software was based on I2CBus.cpp.  Those portions are
 *    copyleft 2001 itt@myself.com.
 *
 *  DScaler #Id: SAA7134I2CBus.cpp,v 1.3 2002/10/30 04:36:43 atnak Exp #
 *
 *  $Id: saa7134_i2c.c,v 1.6 2003/02/01 23:15:54 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "dsdrv/dsdrvlib.h"
#include "dsdrv/tvcard.h"
#include "dsdrv/saa7134_reg.h"
#include "dsdrv/saa7134_i2c.h"


// ---------------------------------------------------------------------------

enum
{
   COMMAND_STOP            = 0x40, // Stop transfer
   COMMAND_CONTINUE        = 0x80, // Continue transfer
   COMMAND_START           = 0xC0  // Start transfer (address device)
};

enum
{
   STATUS_IDLE             = 0x0,  // no I2C command pending
   STATUS_DONE_STOP        = 0x1,  // I2C command done and STOP executed
   STATUS_BUSY             = 0x2,  // executing I2C command
   STATUS_TO_SCL           = 0x3,  // executing I2C command, time out on clock stretching
   STATUS_TO_ARB           = 0x4,  // time out on arbitration trial, still trying
   STATUS_DONE_WRITE       = 0x5,  // I2C command done and awaiting next write command
   STATUS_DONE_READ        = 0x6,  // I2C command done and awaiting next read command
   STATUS_DONE_WRITE_TO    = 0x7,  // see 5, and time out on status echo
   STATUS_DONE_READ_TO     = 0x8,  // see 6, and time out on status echo
   STATUS_NO_DEVICE        = 0x9,  // no acknowledge on device slave address
   STATUS_NO_ACKN          = 0xA,  // no acknowledge after data byte transfer  
   STATUS_BUS_ERR          = 0xB,  // bus error
   STATUS_ARB_LOST         = 0xC,  // arbitration lost during transfer
   STATUS_SEQ_ERR          = 0xD,  // erroneous programming sequence
   STATUS_ST_ERR           = 0xE,  // wrong status echoing
   STATUS_SW_ERR           = 0xF   // software error
};

#define MAX_BUSYWAIT_RETRIES    16

static ULONG  m_I2CSleepCycle = 0;
static bool   m_InitializedSleep = FALSE;

// ---------------------------------------------------------------------------
// Execute I2C commands via SAA7134
//
static BYTE SAA7134_GetI2CStatus( void )
{
   return ReadByte(SAA7134_I2C_ATTR_STATUS) & 0x0F;
}

#if 0 // unused
static void SAA7134_SetI2CStatus( BYTE Status )
{
   MaskDataByte(SAA7134_I2C_ATTR_STATUS, Status, 0x0F);
}
#endif

static void SAA7134_SetI2CCommand( BYTE Command )
{
   MaskDataByte(SAA7134_I2C_ATTR_STATUS, Command, 0xC0);
}

static void SAA7134_SetI2CData( BYTE Data )
{
   WriteByte(SAA7134_I2C_DATA, Data);
}

static BYTE SAA7134_GetI2CData( void )
{
   return ReadByte(SAA7134_I2C_DATA);
}

// ---------------------------------------------------------------------------
//
static bool I2CIsError( BYTE Status )
{
   switch (Status)
   {
      case STATUS_NO_DEVICE:
      case STATUS_NO_ACKN:
      case STATUS_BUS_ERR:
      case STATUS_ARB_LOST:
      case STATUS_SEQ_ERR:
      case STATUS_ST_ERR:
      case STATUS_SW_ERR:
         return TRUE;

      default:
         // do nothing
         break;
   }
   return FALSE;
}

static ULONG SAA7134_GetTickCount( void )
{
   ULONGLONG ticks;
   ULONGLONG frequency;

   QueryPerformanceFrequency((PLARGE_INTEGER)&frequency);
   QueryPerformanceCounter((PLARGE_INTEGER)&ticks);
   ticks = (ticks & 0xFFFFFFFF00000000) / frequency * 10000000 +
           (ticks & 0xFFFFFFFF) * 10000000 / frequency;
   return (ULONG)(ticks / 10000);
}

static void InitializeSleep( void )
{
   ULONG elapsed = 0L;
   ULONG start;
   volatile ULONG i;

   m_I2CSleepCycle = 10000L;

   // get a stable reading
   while (elapsed < 5)
   {
      m_I2CSleepCycle *= 10;

      start = SAA7134_GetTickCount();
      for (i = m_I2CSleepCycle; i > 0; i--)
         ;
      elapsed = SAA7134_GetTickCount() - start;
   }
   // calculate how many cycles a 50kHZ is (half I2C bus cycle)
   m_I2CSleepCycle = m_I2CSleepCycle / elapsed * 1000L / 50000L;
   m_InitializedSleep = TRUE;
}

static void SAA7134_I2CSleep( void )
{
   volatile ULONG i;

   for (i = m_I2CSleepCycle; i > 0; i--)
      ;
}

static bool BusyWait( void )
{
   int Retries = 0;
   BYTE Status;

   if (m_InitializedSleep == FALSE)
   {
      InitializeSleep();
   }

   while ((Status = SAA7134_GetI2CStatus()) == STATUS_BUSY)
   {
      if (Retries++ > MAX_BUSYWAIT_RETRIES)
      {
         return FALSE;
      }
      SAA7134_I2CSleep();
   }

   return !I2CIsError(Status);
}

static void SetData(BYTE Data)
{
   SAA7134_SetI2CData(Data);
}

static BYTE GetData( void )
{
   return SAA7134_GetI2CData();
}

static bool IsBusReady( void )
{
   BYTE Status = SAA7134_GetI2CStatus();

   return ( (Status == STATUS_IDLE) ||
            (Status == STATUS_DONE_STOP) );
}

static bool I2CStart( void )
{
   SAA7134_SetI2CCommand(COMMAND_START);
   return BusyWait();
}

static bool I2CStop( void )
{
   SAA7134_SetI2CCommand(COMMAND_STOP);
   return BusyWait();
}

static bool I2CContinue( void )
{
   SAA7134_SetI2CCommand(COMMAND_CONTINUE);
   return BusyWait();
}

// ---------------------------------------------------------------------------
// Read a number of bytes from a given I2C device
// - the device address is given in the write buffer
//
static bool SAA7134_I2cRead( const TVCARD * pTvCard,
                             const BYTE *writeBuffer,
                             size_t writeBufferSize,
                             BYTE *readBuffer,
                             size_t readBufferSize )
{
   BYTE address;
   size_t i;

   if ((pTvCard != NULL) && (writeBuffer != NULL))
   {
      assert(writeBufferSize >= 1);
      assert((readBuffer != NULL) || (readBufferSize == 0));

      if (readBufferSize == 0)
      {
         return TRUE;
      }

      if (!IsBusReady())
      {
         // Try to ready the bus
         if (!I2CStop() || !IsBusReady())
         {
            return FALSE;
         }
      }

      address = writeBuffer[0];

      if (writeBufferSize != 1)
      {
         assert(writeBufferSize > 1);

         SetData(address & ~1);

         // Send the address
         if (!I2CStart())
         {
            debug1("SAA7134-I2cRead(0x%x) returned TRUE for write address in read", address & ~1);
            I2CStop();
            return FALSE;
         }

         for (i = 1; i < (writeBufferSize - 1); i++)
         {
            SetData(writeBuffer[i]);
            if(!I2CContinue())
            {
               I2CStop();
               return FALSE;
            }
         }

         // The last byte may also create a positive acknowledge, indicating, that
         // the device is "full", which is not an error.
         if (writeBufferSize >= 2)
         {
            SetData(writeBuffer[writeBufferSize - 1]);
            I2CContinue();
         }
      }

      SetData(address | 1);

      // The read address requires a negative ack
      if (!I2CStart())
      {
         debug1("SAA7134-I2cRead(0x%x) returned FALSE for read address in read", address | 1);
         I2CStop();
         return FALSE;
      }

      // \todo I don't know if the CONTINUE before reading is right.
      // STOP might be needed before the last grab to NAK the bus
      for (i = 0; i < readBufferSize; i++)
      {
         I2CContinue();
         readBuffer[i] = GetData();
      }

      I2CStop();
   }
   else
      fatal2("SAA7134-I2cRead: NULL ptr param: %ld,%ld", (long)pTvCard, (long)writeBuffer);

   return TRUE;
}

static bool SAA7134_I2cWrite( const TVCARD * pTvCard,
                              const BYTE *writeBuffer, size_t writeBufferSize )
{
   size_t  i;

   if ((pTvCard != NULL) && (writeBuffer != NULL))
   {
      assert(writeBufferSize >= 1);
      assert((writeBuffer[0] & 1) == 0);

      if (!IsBusReady())
      {
         // Try to ready the bus
         if (!I2CStop() || !IsBusReady())
         {
            return FALSE;
         }
      }

      SetData(writeBuffer[0]);
      if (!I2CStart())
      {
         I2CStop();
         return FALSE;
      }

      for (i = 1; i < writeBufferSize; i++)
      {
         SetData(writeBuffer[i]);
         if (!I2CContinue())
         {
            I2CStop();
            return FALSE;
         }
      }

      I2CStop();
   }
   else
      fatal2("SAA7134-I2cWrite: NULL ptr param: %ld,%ld", (long)pTvCard, (long)writeBuffer);

   return TRUE;
}

// ---------------------------------------------------------------------------
// Fill interface struct
//
static const I2C_RW SAA7134_I2cInterface =
{
   SAA7134_I2cRead,
   SAA7134_I2cWrite,
};

void SAA7134_I2cGetInterface( TVCARD * pTvCard )
{
   if (pTvCard != NULL)
   {
      pTvCard->i2cBus = &SAA7134_I2cInterface;
   }
   else
      fatal0("SAA7134-I2cGetInterface: NULL ptr param");
}

