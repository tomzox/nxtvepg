/////////////////////////////////////////////////////////////////////////////
// #Id: PCICard.h,v 1.14 2002/11/07 20:33:17 adcockj Exp #
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
// nxtvepg $Id: hwpci.h,v 1.6 2004/12/26 21:48:44 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#ifndef __PCICARD_H___
#define __PCICARD_H___

#include "hwdrv.h"

/** This class is used to provide access to the low level function provided
    by the drivers.  To use these function derive your card specific class 
    from this one.
*/
DWORD HwPci_GetSubSystemId( void );
WORD HwPci_GetDeviceId( void );
WORD HwPci_GetVendorId( void );
DWORD HwPci_GetMemoryAddress( void ); // E-nek
DWORD HwPci_GetMemoryLength( void );  // E-nek  

/* tomzo:
** Callback to reset card during card open or close
*/
typedef void (* HWPCI_RESET_CHIP_CB) ( DWORD m_BusNumber, DWORD m_SlotNumber );

/**  Try to find card with given attributes on system
   @return TRUE is device is found
*/
BOOL HwPci_OpenPCICard(WORD VendorID, WORD DeviceID, DWORD DeviceIndex,
                       BOOL supportsAcpi, HWPCI_RESET_CHIP_CB pResetCb);

// E-nek :
// C crade mais j'en ai besoin en public ...
// protected:
void HwPci_Create( void );
void HwPci_Destroy( void );
void HwPci_ClosePCICard( void );

void HwPci_WriteByte(DWORD Offset, BYTE Data);
void HwPci_WriteWord(DWORD Offset, WORD Data);
void HwPci_WriteDword(DWORD Offset, DWORD Data);

BYTE HwPci_ReadByte(DWORD Offset);
WORD HwPci_ReadWord(DWORD Offset);
DWORD HwPci_ReadDword(DWORD Offset);

void HwPci_MaskDataByte(DWORD Offset, BYTE Data, BYTE Mask);
void HwPci_MaskDataWord(DWORD Offset, WORD Data, WORD Mask);
void HwPci_MaskDataDword(DWORD Offset, DWORD Data, DWORD Mask);
void HwPci_AndOrDataByte(DWORD Offset, BYTE Data, BYTE Mask);
void HwPci_AndOrDataWord(DWORD Offset, WORD Data, WORD Mask);
void HwPci_AndOrDataDword(DWORD Offset, DWORD Data, DWORD Mask);
void HwPci_AndDataByte(DWORD Offset, BYTE Data);
void HwPci_AndDataWord(DWORD Offset, WORD Data);
void HwPci_AndDataDword(DWORD Offset, DWORD Data);
void HwPci_OrDataByte(DWORD Offset, BYTE Data);
void HwPci_OrDataWord(DWORD Offset, WORD Data);
void HwPci_OrDataDword(DWORD Offset, DWORD Data);

BOOL HwPci_GetPCIConfig(PCI_COMMON_CONFIG* pPCI_COMMON_CONFIG, DWORD Bus, DWORD Slot);
BOOL HwPci_SetPCIConfig(PCI_COMMON_CONFIG* pPCI_COMMON_CONFIG, DWORD Bus, DWORD Slot);
BOOL HwPci_GetPCIConfigOffset(BYTE* pbPCIConfig, DWORD Offset, DWORD Bus, DWORD Slot);
BOOL HwPci_SetPCIConfigOffset(BYTE* pbPCIConfig, DWORD Offset, DWORD Bus, DWORD Slot);

#endif
