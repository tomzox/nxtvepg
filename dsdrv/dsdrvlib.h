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
// nxtvepg $Id: dsdrvlib.h,v 1.4 2002/05/05 20:46:25 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#ifndef DSDRV43_HELPER
#define DSDRV43_HELPER


#include "dsdrv/dsdrv.h"

// for driver load result codes
#include "dsdrv/hwdrv.h"

#define ALLOC_MEMORY_CONTIG 1


/////////////////////////////////////////////////////////////////////////////
// Exported functions
/////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif

DWORD LoadDriver( void );
void UnloadDriver( void );

DWORD InstallNTDriver( void );
DWORD UnInstallNTDriver( void );

DWORD DoesThisPCICardExist(DWORD dwVendorID, DWORD dwDeviceID, DWORD dwCardIndex);
DWORD pciGetHardwareResources(DWORD   dwVendorID,
                              DWORD   dwDeviceID,
                              DWORD   dwCardIndex,
                              PDWORD  pdwMemoryAddress,
                              PDWORD  pdwMemoryLength,
                              PDWORD  pdwSubSystemId);

DWORD memoryAlloc(DWORD  dwLength, DWORD  dwFlags, PMemStruct* ppMemStruct);
DWORD memoryFree(PMemStruct pMemStruct);

void memoryWriteDWORD(DWORD dwAddress, DWORD dwValue);
DWORD memoryReadDWORD(DWORD dwAddress);
void memoryWriteWORD(DWORD dwAddress, WORD wValue);
WORD memoryReadWORD(DWORD dwAddress);
void memoryWriteBYTE(DWORD dwAddress, BYTE ucValue);
BYTE memoryReadBYTE(DWORD dwAddress);

#ifdef __cplusplus
}
#endif


#endif
