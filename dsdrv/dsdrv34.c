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
// nxtvepg $Id: dsdrv34.c,v 1.3 2002/05/10 00:16:30 tom Exp tom $
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
#include <malloc.h>
#include <winsvc.h>

#include <io.h>
#include <fcntl.h>

#include "dsdrv/dsdrv.h"
#include "dsdrv/hwpci.h"
#include "dsdrv/hwdrv.h"
#include "dsdrv/debuglog.h"
#include "dsdrv/dsdrvlib.h"



BOOL  Pcicard = FALSE;
BOOL  HardwareDriver = FALSE;
BOOL  CardOpened = FALSE;     // BT Card has been opened ?

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



DWORD LoadDriver( void )
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


void UnloadDriver( void )
{
    if (Pcicard)
    {
        //delete Pcicard;
        //Pcicard = NULL;
        HwPci_Destroy();
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
                                      DWORD   dwCardIndex )
{
    BOOL ret;

    if ( !HardwareDriver || !Pcicard )
        return 1;

    ret = HwDrv_DoesThisPCICardExist( dwVendorID, dwDeviceID, dwCardIndex, NULL, NULL, NULL);
    if ( !ret )
        return 2;

    ret = HwPci_OpenPCICard(dwVendorID, dwDeviceID, dwCardIndex);
    if ( !ret )
        return 3;

    //*pdwSubSystemId  = HwPci_GetSubSystemId();
    //*pdwMemoryLength = HwPci_GetMemoryLength();
    //*pdwSubSystemId  = HwPci_GetMemoryAddress();
    CardOpened = TRUE;

    return ERROR_SUCCESS;
}


void memoryWriteBYTE(DWORD Offset, BYTE Data)  
{ 
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_WriteByte(Offset, Data); 
}
void memoryWriteWORD(DWORD Offset, WORD Data)  
{ 
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_WriteWord(Offset, Data); 
}
void memoryWriteDWORD(DWORD Offset, DWORD Data)
{ 
    if (Pcicard == FALSE || !CardOpened) return;
    HwPci_WriteDword(Offset, Data); 
}

BYTE memoryReadBYTE(DWORD Offset)       
{ 
    if (Pcicard == FALSE || !CardOpened) return 0;
    return HwPci_ReadByte(Offset); 
}
WORD memoryReadWORD(DWORD Offset)       
{ 
    if (Pcicard == FALSE || !CardOpened) return 0;
    return HwPci_ReadWord(Offset); 
}
DWORD memoryReadDWORD(DWORD Offset) 
{ 
    if (Pcicard == FALSE || !CardOpened) return 0;
    return HwPci_ReadDword(Offset); 
}

// Functions From DSDrv v3.11
//
// We should use CContigMemory & CUserMemory (newer code)
// but that's too complicated.
DWORD memoryAlloc(DWORD dwLength, DWORD dwFlags, PMemStruct* ppMemStruct)
{
    TDSDrvParam paramIn;
    DWORD dwReturnedLength;
    DWORD status;
    DWORD nPages = 0;
    DWORD dwOutParamLength;

    if(dwFlags & ALLOC_MEMORY_CONTIG)
    {
        nPages = 1;
    }
    else
    {
        nPages = (dwLength + 4095) / 4096 + 1;
    }
    
    dwOutParamLength = sizeof(TMemStruct) + nPages * sizeof(TPageStruct);
    *ppMemStruct = (PMemStruct) malloc(dwOutParamLength);

    paramIn.dwValue = dwLength;
    paramIn.dwFlags = dwFlags;
    if(dwFlags & ALLOC_MEMORY_CONTIG)
    {
        paramIn.dwAddress = 0;
    }
    else
    {
        paramIn.dwAddress = (ULONG)malloc(dwLength);
        memset((void*)paramIn.dwAddress, 0, dwLength);
        if(paramIn.dwAddress == 0)
        {
            free(*ppMemStruct);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    status = HwDrv_SendCommandEx(ioctlAllocMemory,
                            &paramIn,
                            sizeof(paramIn),
                            *ppMemStruct,
                            dwOutParamLength,
                            &dwReturnedLength);

    if(status != ERROR_SUCCESS || ppMemStruct == NULL || (*ppMemStruct)->dwUser == 0)
    {
        if(!(dwFlags & ALLOC_MEMORY_CONTIG))
        {
            free((void*)paramIn.dwAddress);
        }
        free(*ppMemStruct);
        *ppMemStruct = NULL;
    }

    return status;
}


DWORD memoryFree(PMemStruct pMemStruct)
{
    DWORD status = ERROR_SUCCESS;
    if(pMemStruct != NULL)
    {
        DWORD dwInParamLength = sizeof(TMemStruct) + pMemStruct->dwPages * sizeof(TPageStruct);
        status = HwDrv_SendCommand(ioctlFreeMemory, pMemStruct, dwInParamLength);
        if(!(pMemStruct->dwFlags & ALLOC_MEMORY_CONTIG))
        {
            free(pMemStruct->dwUser);
        }
        free(pMemStruct);
    }
    return status;
}

/*
//
// These functions are useless so they are not implemented ...
//

int isDriverOpened (void);
BYTE readPort(WORD address);
WORD readPortW(WORD address);
DWORD readPortL(WORD address);
void writePort(WORD address, BYTE bValue);
void writePortW(WORD address, WORD uValue);
void writePortL(WORD address, DWORD dwValue);
DWORD memoryMap  (DWORD dwAddress, DWORD dwLength) 
void memoryUnmap(DWORD dwAddress, DWORD dwLength) 
*/
