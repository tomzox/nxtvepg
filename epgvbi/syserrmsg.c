/*
 *  Build a system error message
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation. You find a copy of this
 *  license in the file COPYRIGHT in the root directory of this release.
 *
 *  THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL,
 *  BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF
 *  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *
 *  Description:
 *
 *    This module assembles an error message from separate strings and
 *    possibly a system error code.
 *
 *  Author:
 *          Tom Zoerner
 *
 *  $Id: syserrmsg.c,v 1.1 2002/11/10 19:49:30 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGVBI
#define DPRINTF_OFF

#ifdef WIN32
#include <windows.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/syserrmsg.h"


// ----------------------------------------------------------------------------
// Save text describing network error cause
// - argument list has to be terminated with NULL pointer
// - to be displayed by the GUI to help the user fixing the problem
//
void SystemErrorMessage_Set( char ** ppErrorText, int errCode, const char * pText, ... )
{
   #ifndef WIN32
   uchar * sysErrStr = NULL;  // init to avoid compiler warning
   #else
   uchar   sysErrStr[160];
   #endif
   va_list argl;
   const char *argv[20];
   uint argc, sumlen, off, idx;

   // free the previous error text
   if (*ppErrorText != NULL)
   {
      xfree(*ppErrorText);
      *ppErrorText = NULL;
   }

   // collect all given strings
   if (pText != NULL)
   {
      argc    = 1;
      argv[0] = pText;
      sumlen  = strlen(pText);

      if (errCode != 0)
      {
         #ifndef WIN32
         sysErrStr = strerror(errCode);
         #else
         FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errCode, LANG_USER_DEFAULT, sysErrStr, sizeof(sysErrStr) - 1, NULL);
         #endif
         sumlen += strlen(sysErrStr);
      }

      va_start(argl, pText);
      while (argc < 20 - 1)
      {
         argv[argc] = va_arg(argl, char *);
         if (argv[argc] == NULL)
            break;

         sumlen += strlen(argv[argc]);
         argc += 1;
      }
      va_end(argl);

      if (argc > 0)
      {
         // allocate memory for sum of all strings length
         *ppErrorText = xmalloc(sumlen + 1);

         // concatenate the strings
         off = 0;
         for (idx=0; idx < argc; idx++)
         {
            strcpy(*ppErrorText + off, argv[idx]);
            off += strlen(argv[idx]);
         }

         if (errCode != 0)
         {
            strcpy(*ppErrorText + off, sysErrStr);
            //off += strlen(sysErrStr);
         }
      }
   }
}

