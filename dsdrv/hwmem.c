/////////////////////////////////////////////////////////////////////////////
// #Id: HardwareMemory.cpp,v 1.8 2002/10/22 16:01:41 adcockj Exp #
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
// nxtvepg $Id: hwmem.c,v 1.5 2020/06/15 10:01:11 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#include <stdlib.h>

#include "dsdrv/dsdrv.h"
#include "dsdrv/hwmem.h"
#include "dsdrv/debuglog.h"


void * HwMem_GetUserPointer( THwMem * pInstance )
{
    if (pInstance->pMemStruct != NULL)
    {
        return pInstance->pMemStruct->dwUser;
    }
    else
    {
        return NULL;
    }
}


DWORD HwMem_TranslateToPhysical( THwMem * pInstance,
                                 void * pUser, DWORD dwSizeWanted, DWORD* pdwSizeAvailable )
{
    if(pInstance->pMemStruct != NULL)
    {
        TPageStruct* pPages = (TPageStruct*)(pInstance->pMemStruct + 1);
        DWORD Offset;
        DWORD i; 
        DWORD sum;
        DWORD pRetVal = 0;

        Offset = (DWORD)pUser - (DWORD)pInstance->pMemStruct->dwUser;
        sum = 0; 
        i = 0;
        while (i < pInstance->pMemStruct->dwPages)
        {
            if (sum + pPages[i].dwSize > (unsigned)Offset)
            {
                Offset -= sum;
                pRetVal = pPages[i].dwPhysical + Offset;    
                if ( pdwSizeAvailable != NULL )
                {
                    *pdwSizeAvailable = pPages[i].dwSize - Offset;
                }
                break;
            }
            sum += pPages[i].dwSize; 
            i++;
        }
        if(pRetVal == 0)
        {
            sum++;
        }
        if ( pdwSizeAvailable != NULL )
        {
            if (*pdwSizeAvailable < dwSizeWanted)
            {
                sum++;
            }
        }

        return pRetVal; 
    }
    else
    {
        return 0;
    }
}

BOOL HwMem_IsValid( THwMem * pInstance )
{
    return (pInstance->pMemStruct != NULL);
}

/** Memory that is allocated in user space and mapped to driver space */
BOOL HwMem_AllocUserMemory( THwMem * pInstance, size_t Bytes )
{
    TDSDrvParam paramIn;
    DWORD dwReturnedLength;
    DWORD status;
    DWORD nPages = 0;
    DWORD dwOutParamLength;

    memset(pInstance, 0, sizeof(*pInstance));

    pInstance->AllocatedBlock = malloc(Bytes + 0xFFF);
    if(pInstance->AllocatedBlock == NULL)
    {
        //throw std::runtime_error("Out of memory");
        return FALSE;
    }

    memset(pInstance->AllocatedBlock, 0, Bytes + 0xFFF);

    nPages = Bytes / 0xFFF + 1;
    
    dwOutParamLength = sizeof(TMemStruct) + nPages * sizeof(TPageStruct);
    pInstance->pMemStruct = (TMemStruct*) malloc(dwOutParamLength);
    if(pInstance->pMemStruct == NULL)
    {
        free(pInstance->AllocatedBlock);
        pInstance->AllocatedBlock = NULL;
        //throw std::runtime_error("Out of memory");
        return FALSE;
    }
	
	memset(pInstance->pMemStruct, 0, dwOutParamLength);

    paramIn.dwValue = Bytes;
    paramIn.dwFlags = 0;

    // align memory to page boundary
    if(((DWORD)pInstance->AllocatedBlock & 0xFFFFF000) < (DWORD)pInstance->AllocatedBlock)
    {
        paramIn.dwAddress = (((DWORD)pInstance->AllocatedBlock + 0xFFF) & 0xFFFFF000);
    }
    else
    {
        paramIn.dwAddress = (DWORD)pInstance->AllocatedBlock;
    }

    status = HwDrv_SendCommandEx(IOCTL_DSDRV_ALLOCMEMORY,
                            &paramIn,
                            sizeof(paramIn),
                            pInstance->pMemStruct,
                            dwOutParamLength,
                            &dwReturnedLength);

    if(status != ERROR_SUCCESS || pInstance->pMemStruct->dwUser == 0)
    {
        free(pInstance->pMemStruct);
        free((void*)pInstance->AllocatedBlock);
        pInstance->AllocatedBlock = NULL;
        pInstance->pMemStruct = NULL;
        //throw std::runtime_error("Memory mapping failed");
        return FALSE;
    }
    return TRUE;
}

void HwMem_FreeUserMemory( THwMem * pInstance )
{
    //DWORD status = ERROR_SUCCESS;
    if(pInstance->pMemStruct != NULL)
    {
        DWORD dwInParamLength = sizeof(TMemStruct) + pInstance->pMemStruct->dwPages * sizeof(TPageStruct);
        /*status =*/ HwDrv_SendCommand(IOCTL_DSDRV_FREEMEMORY, pInstance->pMemStruct, dwInParamLength);
        free(pInstance->pMemStruct);
    }
    if(pInstance->AllocatedBlock != NULL)
    {
        free((void*)pInstance->AllocatedBlock);
    }
}

/** Memory that is contiguous in both driver and user space */
BOOL HwMem_AllocContigMemory( THwMem * pInstance, size_t Bytes )
{
    TDSDrvParam paramIn;
    DWORD dwReturnedLength;
    DWORD status;
    
    DWORD dwOutParamLength = sizeof(TMemStruct) + sizeof(TPageStruct);
    pInstance->pMemStruct = (TMemStruct*) malloc(dwOutParamLength);
    if(pInstance->pMemStruct == NULL)
    {
        //throw std::runtime_error("Out of memory");
        return FALSE;
    }

    paramIn.dwValue = Bytes;
    paramIn.dwFlags = ALLOC_MEMORY_CONTIG;
    paramIn.dwAddress = 0;
    status = HwDrv_SendCommandEx(IOCTL_DSDRV_ALLOCMEMORY,
                            &paramIn,
                            sizeof(paramIn),
                            pInstance->pMemStruct,
                            dwOutParamLength,
                            &dwReturnedLength);

    if(status != ERROR_SUCCESS || pInstance->pMemStruct->dwUser == 0)
    {
        free(pInstance->pMemStruct);
        //throw std::runtime_error("Memory mapping failed");
        return FALSE;
    }
    return TRUE;
}

void HwMem_FreeContigMemory( THwMem * pInstance )
{
    //DWORD Status = ERROR_SUCCESS;
    if(pInstance->pMemStruct != NULL)
    {
        DWORD dwInParamLength = sizeof(TMemStruct) + sizeof(TPageStruct);
        /*Status =*/ HwDrv_SendCommand(
                                          IOCTL_DSDRV_FREEMEMORY, 
                                          pInstance->pMemStruct, 
                                         dwInParamLength                                       
                                       );
        free(pInstance->pMemStruct);
    }
}
