/////////////////////////////////////////////////////////////////////////////
// #Id: PCICard.cpp,v 1.6 2002/02/12 02:29:40 ittarnavsky Exp #
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
// nxtvepg $Id: hwpci.c,v 1.1 2002/05/04 18:39:12 tom Exp tom $
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

BOOL HwPci_OpenPCICard(WORD VendorID, WORD DeviceID, int DeviceIndex)
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

    dwStatus = HwDrv_SendCommandEx(ioctlGetPCIInfo,
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

        dwStatus = HwDrv_SendCommandEx(ioctlMapMemory,
                                            &hwParam,
                                            sizeof(hwParam),
                                            &(m_MemoryBase),
                                            sizeof(DWORD),
                                            &dwReturnedLength);

        if (dwStatus == ERROR_SUCCESS)
        {
            m_bOpen = TRUE;
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
        hwParam.dwAddress = m_MemoryBase;
        hwParam.dwValue   = m_MemoryLength;

        dwStatus = HwDrv_SendCommand(ioctlUnmapMemory, &hwParam, sizeof(hwParam));

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

    dwStatus = HwDrv_SendCommand(ioctlWriteMemoryBYTE, &hwParam, sizeof(hwParam));

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

    dwStatus = HwDrv_SendCommand(ioctlWriteMemoryWORD, &hwParam, sizeof(hwParam));

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

    dwStatus = HwDrv_SendCommand(ioctlWriteMemoryDWORD, &hwParam, sizeof(hwParam));

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
    dwStatus = HwDrv_SendCommandEx(ioctlReadMemoryBYTE,
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
    dwStatus = HwDrv_SendCommandEx(ioctlReadMemoryWORD,
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
    dwStatus = HwDrv_SendCommandEx(ioctlReadMemoryDWORD,
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

void HwPci_MaskDataDword(DWORD Offset, WORD Data, WORD Mask)
{
    DWORD Result;

    Result = HwPci_ReadDword(Offset);
    Result = (Result & ~Mask) | (Data & Mask);
    HwPci_WriteDword(Offset, Result);
}

void HwPci_AndOrDataByte(DWORD Offset, DWORD Data, BYTE Mask)
{
    BYTE Result;

    Result = HwPci_ReadByte(Offset);
    Result = (Result & Mask) | Data;
    HwPci_WriteByte(Offset, Result);
}

void HwPci_AndOrDataWord(DWORD Offset, DWORD Data, WORD Mask)
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

void HwPci_AndDataDword(DWORD Offset, WORD Data)
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

