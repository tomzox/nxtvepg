/////////////////////////////////////////////////////////////////////////////
// #Id: HardwareMemory.h,v 1.7 2001/12/03 19:33:59 adcockj Exp #
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
// nxtvepg $Id: hwmem.h,v 1.2 2003/01/09 14:42:37 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#ifndef __HWMEM_H
#define __HWMEM_H

#include "hwdrv.h"

typedef struct
{
   TMemStruct  * pMemStruct;
   void        * AllocatedBlock;
} THwMem;

void * HwMem_GetUserPointer( THwMem * pInstance );
DWORD  HwMem_TranslateToPhysical( THwMem * pInstance,
                                  void * pUser, DWORD dwSizeWanted, DWORD* pdwSizeAvailable );
BOOL   HwMem_IsValid( THwMem * pInstance );

/** Memory that is allocated in user space and mapped to driver space */
BOOL HwMem_AllocUserMemory( THwMem * pInstance, size_t Bytes );
void HwMem_FreeUserMemory( THwMem * pInstance );

/** Memory that is contiguous in both driver and user space */
BOOL HwMem_AllocContigMemory( THwMem * pInstance, size_t Bytes );
void HwMem_FreeContigMemory( THwMem * pInstance );


#endif  // __HWMEM_H
