/////////////////////////////////////////////////////////////////////////////
// #Id: HardwareDriver.h,v 1.7 2002/02/03 22:47:31 robmuller Exp #
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
// nxtvepg $Id: hwdrv.h,v 1.1 2002/05/04 18:39:12 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#ifndef __HARDWAREDRIVER_H___
#define __HARDWAREDRIVER_H___


/** Allows access to the DSDrv4 driver
*/

BOOL HwDrv_UnInstallNTDriver( void );
BOOL HwDrv_InstallNTDriver( void );
void HwDrv_Create( void );  // CHardwareDriver( void );
void HwDrv_Destroy( void );  //~CHardwareDriver( void );

enum
{
   HWDRV_LOAD_SUCCESS,
   HWDRV_LOAD_OTHER,
   HWDRV_LOAD_NOPERM,
   HWDRV_LOAD_START,
   HWDRV_LOAD_MISSING,
   HWDRV_LOAD_INSTALL,
   HWDRV_LOAD_CREATE,
   HWDRV_LOAD_VERSION
};

DWORD HwDrv_LoadDriver( void );
void HwDrv_UnloadDriver( void );

DWORD HwDrv_SendCommandEx(
               DWORD dwIOCommand,
               LPVOID pvInput,
               DWORD dwInputLength,
               LPVOID pvOutput,
               DWORD dwOutputLength,
               LPDWORD pdwReturnedLength
            );

DWORD HwDrv_SendCommand(
               DWORD dwIOCommand,
               LPVOID pvInput,
               DWORD dwInputLength
            );

BOOL HwDrv_DoesThisPCICardExist(
                        WORD VendorID, 
                        WORD DeviceID, 
                        int DeviceIndex, 
                        DWORD *SubSystemId
                     );


#endif
