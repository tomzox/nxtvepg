/////////////////////////////////////////////////////////////////////////////
// #Id: PCICard.cpp,v 1.14 2002/11/07 21:06:12 adcockj Exp #
/////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2001 John Adcock.  All rights reserved.
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
// nxtvepg $Id: hwpci.c,v 1.7 2003/02/22 14:58:13 tom Exp tom $
//////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN     // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <winsvc.h>
#include "dsdrv/dsdrv.h"
#include "dsdrv/hwpci.h"
#include "dsdrv/hwdrv.h"
#include "dsdrv/debuglog.h"

//protected:
static DWORD  m_SubSystemId;
static WORD   m_DeviceId;
static WORD   m_VendorId;
static BOOL   m_bOpen;
//private:
static DWORD  m_MemoryAddress;
static DWORD  m_MemoryLength;
static DWORD  m_BusNumber;
static DWORD  m_SlotNumber;
static DWORD  m_MemoryBase;
//static CHardwareDriver* m_pDriver;
static DWORD  m_InitialACPIStatus;

// forward declarations
static int  HwPci_GetACPIStatus( void );
static void HwPci_SetACPIStatus(int ACPIStatus);


//HwPci_CPCICard(CHardwareDriver* pDriver) :
void HwPci_Create( void )
{
   //m_pDriver(pDriver),
   m_MemoryAddress = 0;
   m_MemoryLength = 0;
   m_SubSystemId = 0;
   m_BusNumber = 0;
   m_SlotNumber = 0;
   m_MemoryBase = 0;
   m_bOpen = FALSE;
   m_InitialACPIStatus = 0;
}

void HwPci_Destroy( void )
{
    if (m_bOpen)
    {
        HwPci_ClosePCICard();
    }
}

DWORD HwPci_GetMemoryAddress( void )
{
    return m_MemoryAddress;
}

DWORD HwPci_GetMemoryLength( void )
{
    return m_MemoryLength;
}

DWORD HwPci_GetSubSystemId( void )
{
    return m_SubSystemId;
}

WORD HwPci_GetDeviceId( void )
{
    return m_DeviceId;
}

WORD HwPci_GetVendorId( void )
{
    return m_VendorId;
}

BOOL HwPci_OpenPCICard(WORD VendorID, WORD DeviceID, DWORD DeviceIndex, BOOL supportsAcpi)
{
    TDSDrvParam hwParam;
    DWORD dwReturnedLength;
    DWORD dwStatus;
    DWORD dwLength;
    TPCICARDINFO PCICardInfo;

    if (m_bOpen)
    {
        HwPci_ClosePCICard();
    }
    m_DeviceId = DeviceID;
    m_VendorId = VendorID;

    hwParam.dwAddress = VendorID;
    hwParam.dwValue = DeviceID;
    hwParam.dwFlags = DeviceIndex;

    dwStatus = HwDrv_SendCommandEx(IOCTL_DSDRV_GETPCIINFO,
                                        &hwParam,
                                        sizeof(hwParam),
                                        &PCICardInfo,
                                        sizeof(TPCICARDINFO),
                                        &dwLength);

    if ( dwStatus == ERROR_SUCCESS)
    {
        m_MemoryAddress = PCICardInfo.dwMemoryAddress;
        m_MemoryLength = PCICardInfo.dwMemoryLength;
        m_SubSystemId = PCICardInfo.dwSubSystemId;
        m_BusNumber = PCICardInfo.dwBusNumber;
        m_SlotNumber = PCICardInfo.dwSlotNumber;

        hwParam.dwAddress = m_BusNumber;
        hwParam.dwValue = m_MemoryAddress;
        hwParam.dwFlags = m_MemoryLength;

        // we need to map much more memory for the CX2388x 
        if((VendorID == 0x14F1) && (DeviceID == 0x8800))
        { 
            hwParam.dwFlags = 0x400000;
        } 

        dwStatus = HwDrv_SendCommandEx(IOCTL_DSDRV_MAPMEMORY,
                                            &hwParam,
                                            sizeof(hwParam),
                                            &(m_MemoryBase),
                                            sizeof(DWORD),
                                            &dwReturnedLength);

        if (dwStatus == ERROR_SUCCESS)
        {
            m_bOpen = TRUE;

            if (supportsAcpi)
            {
                m_InitialACPIStatus = HwPci_GetACPIStatus();
                // if the chip is powered down we need to power it up
                if(m_InitialACPIStatus != 0)
                {
                    HwPci_SetACPIStatus(0);
                }
            }
            else
                m_InitialACPIStatus = 0;
        }
        else
        {
            LOG(1, "MapMemory failed 0x%x", dwStatus);
        }
    }
    else
    {
        LOG(1, "GetPCIInfo failed for %X %X failed 0x%x", VendorID, DeviceID, dwStatus);
    }
    return m_bOpen;
}

void HwPci_ClosePCICard( void )
{
    TDSDrvParam hwParam;
    DWORD dwStatus;

    if (m_MemoryBase != 0)
    {
        // if the chip was not in D0 state we restore the original ACPI power state
        if(m_InitialACPIStatus != 0)
        {
            HwPci_SetACPIStatus(m_InitialACPIStatus);
        }

        hwParam.dwAddress = m_MemoryBase;
        hwParam.dwValue   = m_MemoryLength;

        dwStatus = HwDrv_SendCommand(IOCTL_DSDRV_UNMAPMEMORY, &hwParam, sizeof(hwParam));

        if (dwStatus != ERROR_SUCCESS)
        {
            LOG(1, "UnmapMemory failed 0x%x", dwStatus);
        }

        m_bOpen = FALSE;
    }
}

void HwPci_WriteByte(DWORD Offset, BYTE Data)
{
    TDSDrvParam hwParam;
    DWORD dwStatus;

    hwParam.dwAddress = m_MemoryBase + Offset;
    hwParam.dwValue = Data;

    dwStatus = HwDrv_SendCommand(IOCTL_DSDRV_WRITEMEMORYBYTE, &hwParam, sizeof(hwParam));

    if (dwStatus != ERROR_SUCCESS)
    {
        LOG(1, "WriteMemoryBYTE failed 0x%x", dwStatus);
    }
}

void HwPci_WriteWord(DWORD Offset, WORD Data)
{
    TDSDrvParam hwParam;
    DWORD dwStatus;

    hwParam.dwAddress = m_MemoryBase + Offset;
    hwParam.dwValue = Data;

    dwStatus = HwDrv_SendCommand(IOCTL_DSDRV_WRITEMEMORYWORD, &hwParam, sizeof(hwParam));

    if (dwStatus != ERROR_SUCCESS)
    {
        LOG(1, "WriteMemoryWORD failed 0x%x", dwStatus);
    }
}

void HwPci_WriteDword(DWORD Offset, DWORD Data)
{
    TDSDrvParam hwParam;
    DWORD dwStatus;

    hwParam.dwAddress = m_MemoryBase + Offset;
    hwParam.dwValue = Data;

    dwStatus = HwDrv_SendCommand(IOCTL_DSDRV_WRITEMEMORYDWORD, &hwParam, sizeof(hwParam));

    if (dwStatus != ERROR_SUCCESS)
    {
        LOG(1, "WriteMemoryDWORD failed 0x%x", dwStatus);
    }
}

BYTE HwPci_ReadByte(DWORD Offset)
{
    TDSDrvParam hwParam;
    DWORD dwReturnedLength;
    BYTE bValue = 0;
    DWORD dwStatus;

    hwParam.dwAddress = m_MemoryBase + Offset;
    dwStatus = HwDrv_SendCommandEx(IOCTL_DSDRV_READMEMORYBYTE,
                                            &hwParam,
                                            sizeof(hwParam.dwAddress),
                                            &bValue,
                                            sizeof(bValue),
                                            &dwReturnedLength);

    if (dwStatus != ERROR_SUCCESS)
    {
        LOG(1, "ReadMemoryBYTE failed 0x%x", dwStatus);
    }
    return bValue;
}

WORD HwPci_ReadWord(DWORD Offset)
{
    TDSDrvParam hwParam;
    DWORD dwReturnedLength;
    WORD wValue = 0;
    DWORD dwStatus;

    hwParam.dwAddress = m_MemoryBase + Offset;
    dwStatus = HwDrv_SendCommandEx(IOCTL_DSDRV_READMEMORYWORD,
                                            &hwParam,
                                            sizeof(hwParam.dwAddress),
                                            &wValue,
                                            sizeof(wValue),
                                            &dwReturnedLength);

    if (dwStatus != ERROR_SUCCESS)
    {
        LOG(1, "ReadMemoryWORD failed 0x%x", dwStatus);
    }
    return wValue;
}

DWORD HwPci_ReadDword(DWORD Offset)
{
    TDSDrvParam hwParam;
    DWORD dwReturnedLength;
    DWORD dwValue = 0;
    DWORD dwStatus;

    hwParam.dwAddress = m_MemoryBase + Offset;
    dwStatus = HwDrv_SendCommandEx(IOCTL_DSDRV_READMEMORYDWORD,
                                            &hwParam,
                                            sizeof(hwParam.dwAddress),
                                            &dwValue,
                                            sizeof(dwValue),
                                            &dwReturnedLength);

    if (dwStatus != ERROR_SUCCESS)
    {
        LOG(1, "ReadMemoryDWORD failed 0x%x", dwStatus);
    }
    return dwValue;
}


void HwPci_MaskDataByte(DWORD Offset, BYTE Data, BYTE Mask)
{
    BYTE Result;

    Result = HwPci_ReadByte(Offset);
    Result = (Result & ~Mask) | (Data & Mask);
    HwPci_WriteByte(Offset, Result);
}

void HwPci_MaskDataWord(DWORD Offset, WORD Data, WORD Mask)
{
    WORD Result;
    Result = HwPci_ReadWord(Offset);
    Result = (Result & ~Mask) | (Data & Mask);
    HwPci_WriteWord(Offset, Result);
}

void HwPci_MaskDataDword(DWORD Offset, DWORD Data, DWORD Mask)
{
    DWORD Result;

    Result = HwPci_ReadDword(Offset);
    Result = (Result & ~Mask) | (Data & Mask);
    HwPci_WriteDword(Offset, Result);
}

void HwPci_AndOrDataByte(DWORD Offset, BYTE Data, BYTE Mask)
{
    BYTE Result;

    Result = HwPci_ReadByte(Offset);
    Result = (Result & Mask) | Data;
    HwPci_WriteByte(Offset, Result);
}

void HwPci_AndOrDataWord(DWORD Offset, WORD Data, WORD Mask)
{
    WORD Result;

    Result = HwPci_ReadWord(Offset);
    Result = (Result & Mask) | Data;
    HwPci_WriteWord(Offset, Result);
}

void HwPci_AndOrDataDword(DWORD Offset, DWORD Data, DWORD Mask)
{
    DWORD Result;

    Result = HwPci_ReadDword(Offset);
    Result = (Result & Mask) | Data;
    HwPci_WriteDword(Offset, Result);
}

void HwPci_AndDataByte(DWORD Offset, BYTE Data)
{
    BYTE Result;

    Result = HwPci_ReadByte(Offset);
    Result &= Data;
    HwPci_WriteByte(Offset, Result);
}

void HwPci_AndDataWord(DWORD Offset, WORD Data)
{
    WORD Result;

    Result = HwPci_ReadWord(Offset);
    Result &= Data;
    HwPci_WriteWord(Offset, Result);
}

void HwPci_AndDataDword(DWORD Offset, DWORD Data)
{
    DWORD Result;

    Result = HwPci_ReadDword(Offset);
    Result &= Data;
    HwPci_WriteDword(Offset, Result);
}

void HwPci_OrDataByte(DWORD Offset, BYTE Data)
{
    BYTE Result;

    Result = HwPci_ReadByte(Offset);
    Result |= Data;
    HwPci_WriteByte(Offset, Result);
}

void HwPci_OrDataWord(DWORD Offset, WORD Data)
{
    WORD Result;

    Result = HwPci_ReadWord(Offset);
    Result |= Data;
    HwPci_WriteWord(Offset, Result);
}

void HwPci_OrDataDword(DWORD Offset, DWORD Data)
{
    DWORD Result;

    Result = HwPci_ReadDword(Offset);
    Result |= Data;
    HwPci_WriteDword(Offset, Result);
}

#if 0
// note: following functions are implemented in the dsdrv shell (dsdrv34.c)
static void HwPci_SaveState( void ) {}
static void HwPci_RestoreState( void ) {}
void HwPci_ManageDword(DWORD Offset) {}
void HwPci_ManageWord(DWORD Offset) {}
void HwPci_ManageByte(DWORD Offset) {}
#endif

BOOL HwPci_GetPCIConfig(PCI_COMMON_CONFIG* pPCI_COMMON_CONFIG, DWORD Bus, DWORD Slot)
{
    TDSDrvParam hwParam;
    DWORD dwStatus;
    DWORD dwLength;

    if(pPCI_COMMON_CONFIG == NULL)
    {
        LOG(1, "HwPci-GetPCIConfig failed. pPCI_COMMON_CONFIG == NULL");
        return FALSE;
    }

    hwParam.dwAddress = Bus;
    hwParam.dwValue = Slot;

    dwStatus = HwDrv_SendCommandEx(IOCTL_DSDRV_GETPCICONFIG,
                                        &hwParam,
                                        sizeof(hwParam),
                                        pPCI_COMMON_CONFIG,
                                        sizeof(PCI_COMMON_CONFIG),
                                        &dwLength);

    if(dwStatus != ERROR_SUCCESS)
    {
        LOG(1, "HwPci-GetPCIConfig failed for %X %X failed 0x%x", Bus, Slot, dwStatus);
        return FALSE;
    }
    return TRUE;
}

BOOL HwPci_SetPCIConfig(PCI_COMMON_CONFIG* pPCI_COMMON_CONFIG, DWORD Bus, DWORD Slot)
{
    TDSDrvParam hwParam;
    DWORD dwStatus;
    DWORD dwLength;

    if(pPCI_COMMON_CONFIG == NULL)
    {
        LOG(1, "HwPci-SetPCIConfig failed. pPCI_COMMON_CONFIG == NULL");
        return FALSE;
    }

    hwParam.dwAddress = Bus;
    hwParam.dwValue = Slot;

    dwStatus = HwDrv_SendCommandEx(IOCTL_DSDRV_SETPCICONFIG,
                                        &hwParam,
                                        sizeof(hwParam),
                                        pPCI_COMMON_CONFIG,
                                        sizeof(PCI_COMMON_CONFIG),
                                        &dwLength);

    if(dwStatus != ERROR_SUCCESS)
    {
        LOG(1, "HwPci-SetPCIConfig failed for %X %X failed 0x%x", Bus, Slot, dwStatus);
        return FALSE;
    }
    return TRUE;
}

// this functions returns 0 if the card is in ACPI state D0 or on error
// returns 3 if in D3 state (full off)
static int HwPci_GetACPIStatus( void )
{
    PCI_COMMON_CONFIG PCI_Config;

    // only some cards are able to power down
    //if(!SupportsACPI()) { return 0; }  // checked by caller
    
    if(HwPci_GetPCIConfig(&PCI_Config, m_BusNumber, m_SlotNumber))
    {
        DWORD ACPIStatus = PCI_Config.DeviceSpecific[0x10] & 3;

        LOG(1, "Bus %d Card %d ACPI status: D%d", m_BusNumber, m_SlotNumber, ACPIStatus);
        return ACPIStatus;
    }

    return 0;
}

// Set ACPIStatus to 0 for D0/full on state. 3 for D3/full off
static void HwPci_SetACPIStatus(int ACPIStatus)
{
    PCI_COMMON_CONFIG PCI_Config;

    // only some cards are able to power down
    //if(!SupportsACPI()) { return; }  // checked during open

    if(!HwPci_GetPCIConfig(&PCI_Config, m_BusNumber, m_SlotNumber))
    {
        return;
    }
    PCI_Config.DeviceSpecific[0x10] &= ~3;
    PCI_Config.DeviceSpecific[0x10] |= ACPIStatus;

    LOG(1, "Attempting to set Bus %d Card %d ACPI status to D%d", m_BusNumber, m_SlotNumber, ACPIStatus);

    HwPci_SetPCIConfig(&PCI_Config, m_BusNumber, m_SlotNumber);

    if(ACPIStatus == 0)
    {
        Sleep(500);
        //ResetChip();  // done by caller
    }
    LOG(1, "Set ACPI status complete");
}

