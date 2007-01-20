/////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2002
/////////////////////////////////////////////////////////////////////////////
//
//  This file is subject to the terms of the GNU General Public License as
//  published by the Free Software Foundation.  A copy of this license is
//  included with this software distribution in the file COPYING.  If you
//  do not have a copy, you may obtain a copy by writing to the Free
//  Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//
//  This software is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details
/////////////////////////////////////////////////////////////////////////////
//
// PCICard.cpp, HardwareDriver.cpp, HardwareMemory is from Dscaler
// Copyright (c) 2001 John Adcock.  All rights reserved.
/////////////////////////////////////////////////////////////////////////////
// nxtvepg $Id: dsdrv34.c,v 1.12 2006/12/21 20:31:47 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

// What's this ?
// Helper DLL to access DsDrv v4 drivers from software written for Dsdrv v3 
// It's "written" by me (E-nek) and it is a very ugly !
// Many Thanks to Dscaler team for their drivers.
//
// 29/3/2002: v0.2
// - Added VERSION resource
// - New functions :
//   LoadDriver, UnloadDriver, DoesThisPCICardExist, InstallNTDriver, UnInstallNTDriver
//   memoryMap & memoryUnMap have been suppressed
// - pciGetHardwareResources & DoesThisPCICardExist have more explicit
//   error codes.

// 24/3/2002: v0.1 (First release)
// - Fix crash when accesssing Pcicard and Pcicard was NULL ;-)
// - pciGetHardwareResources has a new parameter
// - memoryAlloc/memoryFree is using code from DSDrv v3

// General Notes :
// - Works on Win2K and on Win9X (you need Dsdrv.dll, dsdrv4.dll, dsdrv4.vxd)
// - I have made few "hackish" modifications in original Dscaler code
//   Search for "E-nek".
// - Not all functions are implemented (like readPort*/WritePort* ...)


#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <stdlib.h>
#include <winsvc.h>

#include <io.h>
#include <fcntl.h>

#include "dsdrv/dsdrv.h"
#include "dsdrv/hwpci.h"
#include "dsdrv/hwdrv.h"
#include "dsdrv/debuglog.h"
#include "dsdrv/dsdrvlib.h"



static BOOL  Pcicard = FALSE;
static BOOL  HardwareDriver = FALSE;
static BOOL  CardOpened = FALSE;     // BT Card has been opened ?

#define SERVICE_KEY "SYSTEM\\CurrentControlSet\\Services\\"

static BOOL CheckDriverImagePath( void )
{
   HKEY  hKey;
   DWORD dwSize, dwType;
   char  name_buf[512];
   BOOL  result = FALSE;

   if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SERVICE_KEY NT_DRIVER_NAME, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
   {
      dwSize = sizeof(name_buf) - 1;
      if (RegQueryValueEx(hKey, "ImagePath", 0, &dwType, name_buf, &dwSize) == ERROR_SUCCESS)
      {
         if (dwType == REG_EXPAND_SZ)
         {
            name_buf[dwSize] = 0;

            LOG(1,"CheckDriverImagePath: registered " NT_DRIVER_NAME " ImagePath='%s'", name_buf);

            if (strncmp(name_buf, "\\??\\", 4) == 0)
            {
               if (access(name_buf + 4, O_RDONLY) == 0)
               {
                  result = TRUE;
               }
            }
         }
         else
            LOG(1,"CheckDriverImagePath: service registry key has unexpected type %d", dwType);
      }
      else
         LOG(1,"CheckDriverImagePath: ImagePath key value not found", name_buf);

      RegCloseKey(hKey);
   }
   else
      LOG(1,"CheckDriverImagePath: key " SERVICE_KEY NT_DRIVER_NAME " not found", name_buf);

   return result;
}


const char * DsDrvGetErrorMsg( DWORD loadError )
{
   const char * errmsg;

   switch (loadError)
   {
      case HWDRV_LOAD_NOPERM:
         errmsg = "Failed to load the TV card driver: access denied.\n"
                  "On WinNT and Win2K you need admin permissions\n"
                  "to start the driver.  See README.txt for more info.";
         break;
      case HWDRV_LOAD_MISSING:
         errmsg = "Failed to load the TV card driver: could not start the service.\n"
                  "The the driver files dsdrv4.sys and dsdrv4.vxd may not be\n"
                  "in the nxtvepg directory. See README.txt for more info.";
         break;
      case HWDRV_LOAD_START:
         errmsg = "Failed to load the TV card driver: could not start the service.\n"
                  "See README.txt for more info.";
         break;
      case HWDRV_LOAD_INSTALL:
         errmsg = "Failed to load the TV card driver: could not install\n"
                  "the service.  See README.txt for more info.";
         break;
      case HWDRV_LOAD_CREATE:
         errmsg = "Failed to load the TV card driver: Another application may\n"
                  "already be using the driver. See README.txt for more info.\n";
         break;
      case HWDRV_LOAD_REMOTE_DRIVE:
         errmsg = "Failed to load the TV card driver: Cannot install the driver\n"
                  "on a network drive. See README.txt for more info.\n";
         break;
      case HWDRV_LOAD_VERSION:
         errmsg = "Failed to load the TV card driver: it's is an incompatible\n"
                  "version. See README.txt for more info.\n";
         break;
      case HWDRV_LOAD_OTHER:
      default:
         errmsg = "Failed to load the TV card driver.\n"
                  "See README.txt for more info.";
         break;
   }
   return errmsg;
}


DWORD DsDrvLoad( void )
{
    DWORD result;

    //HardwareDriver = new CHardwareDriver();
    HwDrv_Create();

    result = HwDrv_LoadDriver();

    if (result != HWDRV_LOAD_SUCCESS)
    {
        if ( (result == HWDRV_LOAD_MISSING) ||
             (result == HWDRV_LOAD_START) ||
             (result == HWDRV_LOAD_INSTALL) ||
             (result == HWDRV_LOAD_VERSION) )
        {
            // write current ImagePath to the log output
            CheckDriverImagePath();

            LOG(1,"Attempting Bt8x8 driver recovery: uninstalling & installing driver");
            HwDrv_UnInstallNTDriver();
            HwDrv_Destroy();

            HwDrv_Create();
            HwDrv_InstallNTDriver();
            CheckDriverImagePath();

            // now try once more to start the driver
            result = HwDrv_LoadDriver();
        }
    }

    if (result == HWDRV_LOAD_SUCCESS)
    {
        HardwareDriver = TRUE;
        //Pcicard = new CPCICard(HardwareDriver);
        HwPci_Create();
        Pcicard = TRUE;
    }
    else
    {
        //delete HardwareDriver;
        //HardwareDriver = NULL;
        HwDrv_Destroy();
    }
    return result;
}


void DsDrvUnload( void )
{
    if (Pcicard)
    {
        //delete Pcicard;
        //Pcicard = NULL;
        HwPci_Destroy();
        CardOpened = FALSE;
        Pcicard = FALSE;
    }
    
    //HardwareDriver->UnloadDriver();
    //delete HardwareDriver;
    HwDrv_Destroy();
    HardwareDriver = FALSE;
}


DWORD DoesThisPCICardExist(DWORD dwVendorID, DWORD dwDeviceID, DWORD dwCardIndex,
                           DWORD * pdwSubSystemId, DWORD * pdwBusNumber, DWORD * pdwSlotNumber)
{
    BOOL ret;

    if ( !HardwareDriver || !Pcicard )
        return 1;

    ret = HwDrv_DoesThisPCICardExist( dwVendorID, dwDeviceID, dwCardIndex,
                                      pdwSubSystemId, pdwBusNumber, pdwSlotNumber );
    if ( !ret )
        return 2;

    return ERROR_SUCCESS;
}



// Note :
// New parameter : dwCardIndex
DWORD pciGetHardwareResources(DWORD   dwVendorID,
                              DWORD   dwDeviceID,
                              DWORD   dwCardIndex,
                              BOOL    supportsAcpi,
                              HWPCI_RESET_CHIP_CB pResetCb)
{
    BOOL ret;

    if ( !HardwareDriver || !Pcicard )
        return 1;

    ret = HwDrv_DoesThisPCICardExist( dwVendorID, dwDeviceID, dwCardIndex, NULL, NULL, NULL);
    if ( !ret )
        return 2;

    ret = HwPci_OpenPCICard(dwVendorID, dwDeviceID, dwCardIndex,
                            supportsAcpi, pResetCb);
    if ( !ret )
        return 3;

    //*pdwSubSystemId  = HwPci_GetSubSystemId();
    //*pdwMemoryLength = HwPci_GetMemoryLength();
    //*pdwSubSystemId  = HwPci_GetMemoryAddress();
    CardOpened = TRUE;

    return ERROR_SUCCESS;
}


void WriteByte(DWORD Offset, BYTE Data)
{ 
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_WriteByte(Offset, Data); 
}
void WriteWord(DWORD Offset, WORD Data)
{ 
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_WriteWord(Offset, Data); 
}
void WriteDword(DWORD Offset, DWORD Data)
{ 
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_WriteDword(Offset, Data); 
}

BYTE ReadByte(DWORD Offset)
{ 
    if (Pcicard == FALSE || !CardOpened) return 0;
    return HwPci_ReadByte(Offset); 
}
WORD ReadWord(DWORD Offset)
{ 
    if (Pcicard == FALSE || !CardOpened) return 0;
    return HwPci_ReadWord(Offset); 
}
DWORD ReadDword(DWORD Offset)
{ 
    if (Pcicard == FALSE || !CardOpened) return 0;
    return HwPci_ReadDword(Offset); 
}

void MaskDataByte(DWORD Offset, BYTE Data, BYTE Mask)
{
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_MaskDataByte(Offset, Data, Mask);
}
void MaskDataWord(DWORD Offset, WORD Data, WORD Mask)
{
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_MaskDataWord(Offset, Data, Mask);
}
void MaskDataDword(DWORD Offset, DWORD Data, DWORD Mask)
{
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_MaskDataDword(Offset, Data, Mask);
}
void AndDataByte(DWORD Offset, BYTE Data)
{
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_AndDataByte(Offset, Data);
}
void AndDataWord (DWORD Offset, WORD Data)
{
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_AndDataWord(Offset, Data);
}
void AndDataDword (DWORD Offset, DWORD Data)
{
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_AndDataDword(Offset, Data);
}

void OrDataByte(DWORD Offset, BYTE Data)
{
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_OrDataByte(Offset, Data);
}
void OrDataWord (DWORD Offset, WORD Data)
{
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_OrDataWord(Offset, Data);
}
void OrDataDword (DWORD Offset, DWORD Data)
{
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_OrDataDword(Offset, Data);
}


// ----------------------------------------------------------------------------
// Structure that holds register state array
// - dynamically growing if more elements than fit the default max. table size
//
// default allocation size for buffer size
#define STATE_CLUSTER_SIZE  100

typedef struct
{
   DWORD  * pValues;
   int      maxCount;
   int      fillCount;
} CARD_STATE_BUF;

static CARD_STATE_BUF cardStateBuf = {NULL, 0, 0};
static BOOL           cardStateReading;
static unsigned       cardStateReadIdx;

// ----------------------------------------------------------------------------
// Helper func to append register value values to a growing list
// - the list grows automatically when required
//
static void DsDrv_AddElement( CARD_STATE_BUF * pStateBuf, DWORD value )
{
   DWORD * pPrevValues;

   if (pStateBuf->fillCount > pStateBuf->maxCount)
      LOG(1,"DsDrv-AddElement: illegal fill count %d >= %d", pStateBuf->fillCount, pStateBuf->maxCount);

   if (pStateBuf->fillCount == pStateBuf->maxCount)
   {
      pPrevValues = pStateBuf->pValues;
      pStateBuf->maxCount += STATE_CLUSTER_SIZE;
      pStateBuf->pValues = malloc(pStateBuf->maxCount * sizeof(*pStateBuf->pValues));

      if (pPrevValues != NULL)
      {
         memcpy(pStateBuf->pValues, pPrevValues, pStateBuf->fillCount * sizeof(*pStateBuf->pValues));
         free(pPrevValues);
      }
   }

   pStateBuf->pValues[pStateBuf->fillCount] = value;
   pStateBuf->fillCount += 1;
}

// ----------------------------------------------------------------------------
// Prepare the state buffer for writing
// - discard the previous content (but keep the allocated memory)
// - note: do not overwrite pointer with NULL because buffer is not freed by restore
//
void HwPci_InitStateBuf( void )
{
   cardStateBuf.fillCount = 0;
   cardStateBuf.maxCount  = 0;
   cardStateReading = FALSE;
   cardStateReadIdx = 0;
}

// ----------------------------------------------------------------------------
// Prepare the state buffer for retrieving previously written content
//
void HwPci_RestoreState( void )
{
   cardStateReading = TRUE;
   cardStateReadIdx = 0;
}

// ----------------------------------------------------------------------------
// Write to or read from the buffer
// - note: in the buffer each write uses a DWORD, even if only a BYTE is written
//   the caller must retrieve the elements in the same order and width
//
void ManageDword( DWORD Offset )
{
   if (cardStateReading)
   {
      if (cardStateReadIdx < cardStateBuf.fillCount)
      {
         WriteDword(Offset, cardStateBuf.pValues[cardStateReadIdx]);
         cardStateReadIdx += 1;
      }
      else
         LOG(1, "Manage-Dword: illegal read index %d >= %d", cardStateReadIdx, cardStateBuf.fillCount);
   }
   else
   {
      DsDrv_AddElement(&cardStateBuf, ReadDword(Offset));
   }
}

void ManageWord( DWORD Offset )
{
   if (cardStateReading)
   {
      if (cardStateReadIdx < cardStateBuf.fillCount)
      {
         WriteWord(Offset, (WORD)cardStateBuf.pValues[cardStateReadIdx]);
         cardStateReadIdx += 1;
      }
      else
         LOG(1, "Manage-Word: illegal read index %d >= %d", cardStateReadIdx, cardStateBuf.fillCount);
   }
   else
   {
      DsDrv_AddElement(&cardStateBuf, (DWORD)ReadWord(Offset));
   }
}

void ManageByte( DWORD Offset )
{
   if (cardStateReading)
   {
      if (cardStateReadIdx < cardStateBuf.fillCount)
      {
         WriteByte(Offset, (BYTE)cardStateBuf.pValues[cardStateReadIdx]);
         cardStateReadIdx += 1;
      }
      else
         LOG(1, "Manage-Byte: illegal read index %d >= %d", cardStateReadIdx, cardStateBuf.fillCount);
   }
   else
   {
      DsDrv_AddElement(&cardStateBuf, (DWORD)ReadByte(Offset));
   }
}

void DsDrv_LockCard( void )
{
   HwPci_LockCard();
}

void DsDrv_UnlockCard( void )
{
   HwPci_UnlockCard();
}


