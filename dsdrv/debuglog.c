/////////////////////////////////////////////////////////////////////////////
// DebugLog.cpp
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
// nxtvepg $Id: debuglog.c,v 1.2 2002/05/06 11:42:05 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#define DEBUG_SWITCH DEBUG_SWITCH_DSDRV

#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>

#include "dsdrv/debuglog.h"
#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

static BOOL bDsdrvLogEnabled = FALSE;

// debug level 1: errors
// debug level 2: informative

void LOG(int DebugLevel, char * Format, ...)
{
    char szMessage[2048];
    va_list Args;
    int len;

#if DEBUG_SWITCH == OFF
    if (bDsdrvLogEnabled)
#endif
    {
       va_start(Args, Format);
       vsprintf(szMessage, Format, Args);
       va_end(Args);

       len = strlen(szMessage);
       if ((len > 0) && (szMessage[len - 1] != '\n'))
       {
           szMessage[len] = '\n';
           szMessage[len + 1] = 0;
           len += 1;
       }
    }

    // if the user requested driver logging, write the string to a file
    if (bDsdrvLogEnabled)
    {
        int fd;

        fd = open("dsdrv.log", O_WRONLY|O_CREAT|O_APPEND, 0666);
        if (fd >= 0)
        {
            write(fd, szMessage, len);
            close(fd);
        }
    }

#if DEBUG_SWITCH == ON
    if (DebugLevel == 1)
    {
        OutputDebugString(szMessage);
    }
#endif
}

void HwDrv_SetLogging( BOOL dsdrvLog )
{
   bDsdrvLogEnabled = dsdrvLog;
}

