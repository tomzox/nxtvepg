/////////////////////////////////////////////////////////////////////////////
// #Id: HardwareDriver.cpp,v 1.22 2003/10/27 10:39:51 adcockj Exp #
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
// nxtvepg $Id: hwdrv.c,v 1.15 2004/12/26 21:55:20 tom Exp tom $
//////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#include "dsdrv/dsdrv.h"
#include "dsdrv/hwdrv.h"
#include "dsdrv/debuglog.h"
#include <aclapi.h>

// define this to force uninstallation of the NT driver on every destruction of the class.
//#define ALWAYS_UNINSTALL_NTDRIVER

static const LPSTR NTDriverName = NT_DRIVER_NAME;

static BOOL AdjustAccessRights( void );
static SC_HANDLE   m_hService;
static HANDLE      m_hFile;
static BOOL        m_bWindows95;

// the access rights that are needed to just use DScaler (no (un)installation).
#define DRIVER_ACCESS_RIGHTS (SERVICE_START | SERVICE_STOP)

void HwDrv_Create( void )  //CHardwareDriver::CHardwareDriver()
{
    OSVERSIONINFO ov;

    m_hFile = INVALID_HANDLE_VALUE;
    m_hService = NULL;

    ov.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx( &ov);
    m_bWindows95 = (ov.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS);
}

void HwDrv_Destroy( void )  //CHardwareDriver::~CHardwareDriver()
{
    // just in case the driver hasn't been closed properly
    HwDrv_UnloadDriver();

#if defined ALWAYS_UNINSTALL_NTDRIVER
    LOG(1,"(NT driver) ALWAYS_UNINSTALL_NTDRIVER");
    HwDrv_UnInstallNTDriver();
#endif
}

DWORD HwDrv_LoadDriver( void )
{
    SC_HANDLE hSCManager = NULL;
    DWORD     loadError = HWDRV_LOAD_SUCCESS;
    BOOL      bError = FALSE;

    // make sure we start with nothing open
    HwDrv_UnloadDriver();

    if (!m_bWindows95)
    {
        LOG(2,"LoadDriver: Opening the Service Control Manager...");
        // get handle of the Service Control Manager

        // This function fails on WinXP when not logged on as an administrator.
        // The following note comes from the updated Platform SDK documentation:

        // Windows 2000 and earlier: All processes are granted SC_MANAGER_CONNECT, 
        // SC_MANAGER_ENUMERATE_SERVICE, and SC_MANAGER_QUERY_LOCK_STATUS access to all service control
        // manager databases. This enables any process to open a service control manager database handle
        // that it can use in the OpenService, EnumServicesStatus, and QueryServiceLockStatus functions. 
        //
        // Windows XP: Only authenticated users are granted SC_MANAGER_CONNECT, 
        // SC_MANAGER_ENUMERATE_SERVICE, and SC_MANAGER_QUERY_LOCK_STATUS access to all service control
        // manager databases. 

        hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
        if(hSCManager == NULL)
        {
            LOG(1, "OpenSCManager returned an error = 0x%X", GetLastError());
            bError = TRUE;
        }

        if(!bError)
        {
            LOG(2,"LoadDriver: Open the Service " NT_DRIVER_NAME "...");
            m_hService = OpenService(hSCManager, NTDriverName, DRIVER_ACCESS_RIGHTS);

            if(m_hService == NULL)
            {
                DWORD Err = GetLastError();
                if(Err == ERROR_SERVICE_DOES_NOT_EXIST)
                {
                    LOG(1,"(NT driver) Service does not exist: trying to install it...");

                    loadError = HwDrv_InstallNTDriver();
                    if (loadError != HWDRV_LOAD_SUCCESS)
                    {
                        LOG(1,"Failed to install NT driver. Giving up.");
                        bError = TRUE;
                    }
                }
                else if(Err == ERROR_ACCESS_DENIED)
                {
                    LOG(1,"(NT driver) Unable to open service: access denied");
                    loadError = HWDRV_LOAD_NOPERM;
                    bError = TRUE;
                }
                else
                {
                    LOG(1,"(NT driver) Unable to open service. 0x%X",Err);
                    bError = TRUE;
                }
            }
        }

        if(hSCManager != NULL)
        {
            if(!CloseServiceHandle(hSCManager))
            {
                LOG(1,"(NT driver) Failed to close handle to service control manager.");
                bError = TRUE;
            }
            hSCManager = NULL;
        }

        // try to start service
        if(!bError)
        {
            LOG(2,"LoadDriver: Starting the Service...");
            if(StartService(m_hService, 0, NULL) == FALSE)
            {
                DWORD Err = GetLastError();
                if ((Err == ERROR_PATH_NOT_FOUND) || (Err == ERROR_FILE_NOT_FOUND))
                {
                    LOG(1, "StartService() failed: file not found");
                    loadError = HWDRV_LOAD_MISSING;
                    bError = TRUE;
                }
                else if (Err != ERROR_SERVICE_ALREADY_RUNNING)
                {
                    LOG(1, "StartService() failed 0x%X", Err);
                    loadError = HWDRV_LOAD_START;
                    bError = TRUE;
                }
                else
                {
                    LOG(1, "Service already started");
                }
            }
            if(!bError)
            {
                LOG(2,"LoadDriver: Opening the DSDrv4.sys driver file...");
                m_hFile = CreateFile(
                                     "\\\\.\\DSDrv4",
                                     GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL,
                                     OPEN_EXISTING,
                                     0,
                                     INVALID_HANDLE_VALUE
                                    );
            }

        }

    }
    else
    {
        // it's much easier in windows 95, 98 and me
        LOG(2,"LoadDriver: Opening the DSDrv4.vxd driver file...");
        m_hFile = CreateFile(
                                "\\\\.\\DSDrv4.VXD",
                                0,
                                0,
                                NULL,
                                0,
                                FILE_FLAG_OVERLAPPED | FILE_FLAG_DELETE_ON_CLOSE,
                                NULL
                            );
    }

    if(!bError)
    {
        if(m_hFile != INVALID_HANDLE_VALUE)
        {
            // OK so we've loaded the driver 
            // we had better check that it's the same version as we are
            // otherwise all sorts of nasty things could happen
            // n.b. note that if someone else has already loaded our driver this may
            // happen.

            DWORD dwReturnedLength;
            DWORD dwVersion = 0;

            LOG(2,"LoadDriver: Checking the driver version...");
            HwDrv_SendCommandEx(
                        IOCTL_DSDRV_GETVERSION,
                        NULL,
                        0,
                        &dwVersion,
                        sizeof(dwVersion),
                        &dwReturnedLength
                       );

            if ( (dwVersion < DSDRV_COMPAT_MIN_VERSION) ||
                 ((dwVersion & DSDRV_COMPAT_MASK) != DSDRV_COMPAT_MAJ_VERSION) )
            {
                LOG(1, "We've loaded up an incompatible version of the driver");
                loadError = HWDRV_LOAD_VERSION;
                bError = TRUE;

                // Maybe another driver from an old DScaler version is still installed. 
                // Try to uninstall it.
                HwDrv_UnInstallNTDriver();
            }
            else
            {
                LOG(2, "Found driver version 0x%X", dwVersion);
            }
        }
        else
        {
            LOG(1, "CreateFile on Driver failed 0x%X", GetLastError());
            loadError = HWDRV_LOAD_CREATE;
            bError = TRUE;
        }
    }

    if(bError)
    {
        LOG(2,"LoadDriver: Error - unloading the driver...");
        HwDrv_UnloadDriver();

        if (loadError == HWDRV_LOAD_SUCCESS)
           loadError = HWDRV_LOAD_OTHER;
        return loadError;
    }
    else
    {
        LOG(1, "Hardware driver loaded successfully.");
        return HWDRV_LOAD_SUCCESS;
    }
}

// must make sure that this function copes with multiple calls
void HwDrv_UnloadDriver( void )
{
    if(m_hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }

    if (!m_bWindows95)
    {
        if (m_hService != NULL)
        {
            // do not stop the service, because another app might still be using the driver
            #if 0
            SERVICE_STATUS ServiceStatus;
            LOG(2,"UnloadDriver: stopping the service...");
            if(ControlService(m_hService, SERVICE_CONTROL_STOP, &ServiceStatus ) == FALSE)
            {
                LOG(1,"SERVICE_CONTROL_STOP failed, error 0x%X", GetLastError());
            }
            #endif

            if(m_hService != NULL)
            {
                if(CloseServiceHandle(m_hService) == FALSE)
                {
                    LOG(1,"CloseServiceHandle failed, error 0x%X", GetLastError());
                }
                m_hService = NULL;
            }
        }
    }
}

// On success m_hService will contain the handle to the service.
DWORD HwDrv_InstallNTDriver( void )
{
    LPSTR       pszName;
    char        szDriverPath[MAX_PATH];
    SC_HANDLE   hSCManager = NULL;
    DWORD       loadError = HWDRV_LOAD_SUCCESS;
    BOOL        bError = FALSE;

    LOG(1, "Attempting to install NT driver.");

    HwDrv_UnloadDriver();

    if (m_bWindows95)
    {
        LOG(1, "No need to install NT driver with win9x/ME.");
        return TRUE;
    }

    if (!GetModuleFileName(NULL, szDriverPath, sizeof(szDriverPath)))
    {
        LOG(1, "cannot get module file name. 0x%X",GetLastError());
        szDriverPath[0] = '\0';
        bError = TRUE;
    }
    
    if(!bError)
    {
        pszName = szDriverPath + strlen(szDriverPath);
        while (pszName >= szDriverPath && *pszName != '\\')
        {
            *pszName-- = 0;
        }

        if(GetDriveType(szDriverPath) == DRIVE_REMOTE)
        {
            LOG(1, "InstallNTDriver: cannot install on remote drive: %s", szDriverPath);
            loadError = HWDRV_LOAD_REMOTE_DRIVE;
            bError = TRUE;
        }
    }

    if(!bError)
    {
        strcat(szDriverPath, NTDriverName);
        strcat(szDriverPath, ".sys");       
        
        LOG(2,"InstallNTDriver: Opening the service control manager...");
        hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if(hSCManager == NULL)
        {
            LOG(1, "OpenSCManager returned an error = 0x%X", GetLastError());
            bError = TRUE;
        }   
    }
    
    if(!bError)
    {
        // Make sure no spaces exist in the path since CreateService() does not like spaces.
        GetShortPathName(szDriverPath, szDriverPath, MAX_PATH);

        LOG(2,"InstallNTDriver: Creating the service %s (kernel driver, manual start)",szDriverPath);
        m_hService = CreateService(
            hSCManager,            // SCManager database
            NTDriverName,          // name of service
            NTDriverName,          // name to display
            SERVICE_ALL_ACCESS,    // desired access
            SERVICE_KERNEL_DRIVER, // service type
            SERVICE_DEMAND_START,  // start type
            SERVICE_ERROR_NORMAL,  // error control type
            szDriverPath,          // service's binary
            NULL,                  // no load ordering group
            NULL,                  // no tag identifier
            NULL,                  // no dependencies
            NULL,                  // LocalSystem account
            NULL                   // no password
            );
        
        if(m_hService == NULL)
        {
            // if the service already exists delete it and create it again.
            // this might prevent problems when the existing service points to another driver.
            if(GetLastError() == ERROR_SERVICE_EXISTS)
            {              
                LOG(2,"InstallNTDriver: Create failed: already exists: will attempt to delete the service");

                LOG(2,"InstallNTDriver: Opening the service " NT_DRIVER_NAME "...");
                m_hService = OpenService(hSCManager, NTDriverName, SERVICE_ALL_ACCESS);

                LOG(2,"InstallNTDriver: Deleting the service...");
                if(DeleteService(m_hService) == FALSE)
                {
                    LOG(1,"InstallNTDriver: DeleteService failed, error 0x%X", GetLastError());
                    bError = TRUE;
                }
                else
                    LOG(2,"InstallNTDriver: Succeeded to delete the service.");

                if(m_hService != NULL)
                {
                    CloseServiceHandle(m_hService);
                    m_hService = NULL;
                }
                if(!bError)
                {
                    LOG(2,"InstallNTDriver: 2nd attempt at creating service %s (kernel driver, manual start)",szDriverPath);
                    m_hService = CreateService(
                        hSCManager,            // SCManager database
                        NTDriverName,          // name of service
                        NTDriverName,          // name to display
                        SERVICE_ALL_ACCESS,    // desired access
                        SERVICE_KERNEL_DRIVER, // service type
                        SERVICE_DEMAND_START,  // start type
                        SERVICE_ERROR_NORMAL,  // error control type
                        szDriverPath,          // service's binary
                        NULL,                  // no load ordering group
                        NULL,                  // no tag identifier
                        NULL,                  // no dependencies
                        NULL,                  // LocalSystem account
                        NULL                   // no password
                        );
                    if(m_hService == NULL)
                    {
                        LOG(1,"(NT driver) CreateService #2 failed. 0x%X", GetLastError());
                        bError = TRUE;
                    }
                    else
                        LOG(2,"InstallNTDriver: 2nd attempt to create service succeeded.");
                }
            }
            else
            {
                LOG(1,"(NT driver) CreateService #1 failed. 0x%X", GetLastError());
                bError = TRUE;
            }
        }
    }
    
    if(!bError)
    {
        if(!AdjustAccessRights())
        {
            // note: logging is done inside of the func
            bError = TRUE;
        }
    }
    
    if(hSCManager != NULL)
    {
        if(!CloseServiceHandle(hSCManager))
        {
            LOG(1, "(NT driver) Failed to close handle to service control manager.");
            bError = TRUE;
        }
        hSCManager = NULL;
    }
    
    if(bError)
    {
        LOG(1, "(NT driver) Failed to install driver.");
        HwDrv_UnloadDriver();

        if (loadError == HWDRV_LOAD_SUCCESS)
           loadError = HWDRV_LOAD_INSTALL;
        return loadError;
    }
    else
    {
        LOG(1, "(NT driver) Install complete.");
        return HWDRV_LOAD_SUCCESS;
    }
}


static BOOL AdjustAccessRights( void )
{
    PSECURITY_DESCRIPTOR    psd = NULL;
    SECURITY_DESCRIPTOR     sd;
    DWORD                   dwSize = 0;
    DWORD                   dwError = 0;
    BOOL                    bError = FALSE;
    BOOL                    bDaclPresent = FALSE;
    BOOL                    bDaclDefaulted = FALSE;
    PACL                    pNewAcl = NULL;
    PACL                    pacl = NULL;
    EXPLICIT_ACCESS         ea;
    HINSTANCE               hInstance;
    VOID  (WINAPI *BuildExplicitAccessWithName)(PEXPLICIT_ACCESS_A,LPSTR,DWORD,ACCESS_MODE,DWORD);
    DWORD (WINAPI *SetEntriesInAcl)(ULONG,PEXPLICIT_ACCESS_A,PACL,PACL*);

    if(m_bWindows95)
    {
        return TRUE;
    }
    if(m_hService == NULL)
    {
        return FALSE;
    }

    //http://groups.google.de/groups?hl=de&lr=&threadm=u4WzwtCQBHA.1564%40tkmsftngp03&rnum=2&prev=/groups%3Fq%3DBuildExplicitAccessWithName%2Bwindows-95%26hl%3Dde
    hInstance = LoadLibrary("advapi32.dll");
    BuildExplicitAccessWithName = (void *) GetProcAddress(hInstance, "BuildExplicitAccessWithNameA");
    SetEntriesInAcl = (void *) GetProcAddress(hInstance, "SetEntriesInAclA");

    if ( (BuildExplicitAccessWithName == NULL) ||
         (SetEntriesInAcl == NULL) )
    {
       LOG(1, "ACL functions not found in advapi32.dll");
       FreeLibrary(hInstance);
       return FALSE;
    }

    // Find out how much memory to allocate for psd.
    // psd can't be NULL so we let it point to itself.
    
    psd = (PSECURITY_DESCRIPTOR)&psd;
    
    if(!QueryServiceObjectSecurity(m_hService, DACL_SECURITY_INFORMATION, psd, 0, &dwSize))
    {
        if(GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            psd = (PSECURITY_DESCRIPTOR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSize);
            if(psd == NULL)
            {
                LOG(1, "HeapAlloc failed.");
                // note: HeapAlloc does not support GetLastError()
                bError = TRUE;
            }
        }
        else
        {
            LOG(1,"QueryServiceObjectSecurity #1 failed. 0x%X", GetLastError());
            bError = TRUE;
        }                
    }
    
    // Get the current security descriptor.
    if(!bError)
    {
        if(!QueryServiceObjectSecurity(m_hService, DACL_SECURITY_INFORMATION, psd,dwSize, &dwSize))
        {
            LOG(1,"QueryServiceObjectSecurity #2 failed. 0x%X",GetLastError());
            bError = TRUE;                       
        }
    }
    
    // Get the DACL.
    if(!bError)
    {
        if(!GetSecurityDescriptorDacl(psd, &bDaclPresent, &pacl, &bDaclDefaulted))
        {
            LOG(1,"GetSecurityDescriptorDacl failed. 0x%X",GetLastError());
            bError = TRUE;
        }
    }
    
    // Build the ACE.
    if(!bError)
    {
        SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
        PSID pSIDEveryone;

        // Create a SID for the Everyone group.
        if (!AllocateAndInitializeSid(&SIDAuthWorld, 1,
                     SECURITY_WORLD_RID,
                     0,
                     0, 0, 0, 0, 0, 0,
                     &pSIDEveryone))
        {
            LOG(1,"AllocateAndInitializeSid() failed. 0x%X",GetLastError());
            bError = TRUE;
        }
        else
        {
            ea.grfAccessMode = SET_ACCESS;
            ea.grfAccessPermissions = DRIVER_ACCESS_RIGHTS;
            ea.grfInheritance = NO_INHERITANCE;
            ea.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
            ea.Trustee.pMultipleTrustee = NULL;
            ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            ea.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
            ea.Trustee.ptstrName = (char *)pSIDEveryone;
    
            dwError = SetEntriesInAcl(1, &ea, pacl, &pNewAcl);
            if(dwError != ERROR_SUCCESS)
            {
                LOG(0,"SetEntriesInAcl failed. %d", dwError);
                bError = TRUE;
            }
        }
        FreeSid(pSIDEveryone);
    }
    
    // Initialize a new Security Descriptor.
    if(!bError)
    {
        if(!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
        {
            LOG(1,"InitializeSecurityDescriptor failed. 0x%X",GetLastError());
            bError = TRUE;
        }
    }
    
    // Set the new DACL in the Security Descriptor.
    if(!bError)
    {
        if(!SetSecurityDescriptorDacl(&sd, TRUE, pNewAcl, FALSE))
        {
            LOG(1, "SetSecurityDescriptorDacl", GetLastError());
            bError = TRUE;
        }
    }
    
    // Set the new DACL for the service object.
    if(!bError)
    {
        if (!SetServiceObjectSecurity(m_hService, DACL_SECURITY_INFORMATION, &sd))
        {
            LOG(1, "SetServiceObjectSecurity", GetLastError());
            bError = TRUE;
        }
    }
    
    // Free buffers.
    LocalFree((HLOCAL)pNewAcl);
    HeapFree(GetProcessHeap(), 0, (LPVOID)psd);

    FreeLibrary(hInstance);
    
    return !bError;
}

BOOL HwDrv_UnInstallNTDriver( void )
{
    SC_HANDLE hSCManager = NULL;
    BOOL      bError = FALSE;

    LOG(1, "Attempting to uninstall NT driver.");

    if (m_bWindows95)
    {
        LOG(1,"(NT driver) Uninstall not needed with win9x/ME.");
    }
    else
    {
        HwDrv_UnloadDriver();

        // get handle of the Service Control Manager
        hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if(hSCManager == NULL)
        {
            LOG(1, "OpenSCManager returned an error = 0x%X", GetLastError());
            bError = TRUE;
        }

        if(!bError)
        {
            m_hService = OpenService(hSCManager, NTDriverName, SERVICE_ALL_ACCESS);
            if(m_hService == NULL)
            {
                if(GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
                {
                    LOG(1,"(NT driver) Service does not exist, no need to uninstall.");
                    CloseServiceHandle(m_hService);
                    m_hService = NULL;
                    return TRUE;
                }
                else
                {
                    LOG(1,"(NT driver) Unable to open service. 0x%X",GetLastError());
                    bError = TRUE;
                }
            }
        }

        if(!bError)
        {
            SERVICE_STATUS ServiceStatus;
            if(ControlService(m_hService, SERVICE_CONTROL_STOP, &ServiceStatus) == FALSE)
            {
                LOG(1,"SERVICE_CONTROL_STOP failed, error 0x%X", GetLastError());
            }
        }

        if(hSCManager != NULL)
        {
            if(!CloseServiceHandle(hSCManager))
            {
                LOG(1,"(NT driver) Failed to close handle to service control manager.");
                bError = TRUE;
            }
            hSCManager = NULL;
        }

        if(!bError)
        {
            if (DeleteService(m_hService) == FALSE)
            {
                LOG(1,"DeleteService failed, error 0x%X", GetLastError());
                bError = TRUE;
            }
        }

        if(m_hService != NULL)
        {
            if(CloseServiceHandle(m_hService) == FALSE)
            {
                LOG(1,"CloseServiceHandle failed, error 0x%X", GetLastError());
                bError = TRUE;
            }
            m_hService = NULL;
        }

        if(bError)
        {
            HwDrv_UnloadDriver();
            LOG(1, "Failed to uninstall driver.");
            return FALSE;
        }
        LOG(1, "Uninstall NT driver complete.");
    }
    return TRUE;
}

DWORD HwDrv_SendCommandEx( DWORD dwIOCommand,
                           LPVOID pvInput,
                           DWORD dwInputLength,
                           LPVOID pvOutput,
                           DWORD dwOutputLength,
                           LPDWORD pdwReturnedLength
                         )
{
    if (DeviceIoControl(
                        m_hFile,
                        dwIOCommand,
                        pvInput,
                        dwInputLength,
                        pvOutput,
                        dwOutputLength,
                        pdwReturnedLength,
                        NULL
                      ))
    {
        return 0;
    }
    else
    {
        // Suppress the error when DoesThisPCICardExist() probes for a non-existing card
        if(dwIOCommand == IOCTL_DSDRV_GETPCIINFO)
        {
            LOG(2, "DeviceIoControl returned an error = 0x%X For Command GetPCIInfo. This is probably by design (PCI bus scan), do not worry.", GetLastError());
        }
        else
        {
            LOG(1, "DeviceIoControl returned an error = 0x%X For Command 0x%X", GetLastError(), dwIOCommand);
        }
        return GetLastError();
    }
}

DWORD HwDrv_SendCommand( DWORD dwIOCommand,
                         LPVOID pvInput,
                         DWORD dwInputLength
                        )
{
    DWORD dwDummy;

    if(DeviceIoControl(
                        m_hFile,
                        dwIOCommand,
                        pvInput,
                        dwInputLength,
                        NULL,
                        0,
                        &dwDummy,
                        NULL
                      ))
    {
        return 0;
    }
    else
    {
        LOG(1, "DeviceIoControl returned an error = 0x%X For Command 0x%X", GetLastError(), dwIOCommand);
        return GetLastError();
    }
}

BOOL HwDrv_DoesThisPCICardExist(WORD VendorID, WORD DeviceID, int DeviceIndex,
                                DWORD * pdwSubSystemId, DWORD * pdwBusNumber, DWORD * pdwSlotNumber)
{
    TDSDrvParam hwParam;
    DWORD dwStatus;
    DWORD dwLength;
    TPCICARDINFO PCICardInfo;

    hwParam.dwAddress = VendorID;
    hwParam.dwValue = DeviceID;
    hwParam.dwFlags = DeviceIndex;

    dwStatus = HwDrv_SendCommandEx(
                            IOCTL_DSDRV_GETPCIINFO,
                            &hwParam,
                            sizeof(hwParam),
                            &PCICardInfo,
                            sizeof(TPCICARDINFO),
                            &dwLength
                          );

    if(dwStatus == ERROR_SUCCESS)
    {
       if (pdwSubSystemId != NULL)
          *pdwSubSystemId = PCICardInfo.dwSubSystemId;
       if (pdwBusNumber != NULL)
          *pdwBusNumber   = PCICardInfo.dwBusNumber;
       if (pdwSlotNumber != NULL)
          *pdwSlotNumber  = PCICardInfo.dwSlotNumber;
    }

    return (dwStatus == ERROR_SUCCESS);
}
