/////////////////////////////////////////////////////////////////////////////
// Dsdrv.h : 
// Layer to access Dsdrv v4 drivers from v3.
/////////////////////////////////////////////////////////////////////////////
//
//    This file is subject to the terms of the GNU General Public License as
//    published by the Free Software Foundation.  A copy of this license is
//    included with this software distribution in the file COPYING.  If you
//    do not have a copy, you may obtain a copy by writing to the Free
//    Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//
//    This software is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details
/////////////////////////////////////////////////////////////////////////////
// Extracted from Dscaler 
// Copyright (c) 2000 John Adcock.  All rights reserved.
/////////////////////////////////////////////////////////////////////////////
// nxtvepg $Id: dsdrvlib.h,v 1.9 2003/02/22 14:58:05 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#ifndef DSDRV43_HELPER
#define DSDRV43_HELPER


#include "dsdrv/dsdrv.h"

// for driver load result codes
#include "dsdrv/hwdrv.h"
#include "dsdrv/hwpci.h"

#define ALLOC_MEMORY_CONTIG 1


/////////////////////////////////////////////////////////////////////////////
// Exported functions
/////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif

DWORD DsDrvLoad( void );
void DsDrvUnload( void );
const char * DsDrvGetErrorMsg( DWORD loadError );

DWORD DoesThisPCICardExist(DWORD dwVendorID, DWORD dwDeviceID, DWORD dwCardIndex,
                           DWORD * pdwSubSystemId, DWORD * pdwBusNumber, DWORD * pdwSlotNumber);
DWORD pciGetHardwareResources(DWORD   dwVendorID,
                              DWORD   dwDeviceID,
                              DWORD   dwCardIndex,
                              BOOL    supportsAcpi,
                              HWPCI_RESET_CHIP_CB pResetCb);

void WriteByte(DWORD Offset, BYTE Data);
void WriteWord(DWORD Offset, WORD Data);
void WriteDword(DWORD Offset, DWORD Data);
BYTE ReadByte(DWORD Offset);
WORD ReadWord(DWORD Offset);
DWORD ReadDword(DWORD Offset);

void MaskDataByte(DWORD Offset, BYTE Data, BYTE Mask);
void MaskDataWord(DWORD Offset, WORD Data, WORD Mask);
void MaskDataDword(DWORD Offset, DWORD Data, DWORD Mask);
void AndDataByte(DWORD Offset, BYTE Data);
void AndDataWord (DWORD Offset, WORD Data);
void AndDataDword (DWORD Offset, DWORD Data);
void OrDataByte(DWORD Offset, BYTE Data);
void OrDataWord (DWORD Offset, WORD Data);
void OrDataDword (DWORD Offset, DWORD Data);

void HwPci_InitStateBuf( void );
void HwPci_RestoreState( void );
void ManageDword(DWORD Offset);
void ManageWord(DWORD Offset);
void ManageByte(DWORD Offset);


#ifdef __cplusplus
}
#endif


#endif
