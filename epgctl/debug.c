/*
 *  Debug service module
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
 *    Provides service routines for debugging. The functions in
 *    this module however never should be called directly; always
 *    use the macros defined in the header file. The intention is
 *    that the extent of debugging can easily be controlled, e.g.
 *    debug output easily be turned off in the final release.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: debug.c,v 1.3 2000/06/01 19:44:32 tom Exp tom $
 */

#define __DEBUG_C

#include "epgctl/mytypes.h"

#if DEBUG_GLOBAL_SWITCH == ON

#define DEBUG_SWITCH ON
#include "epgctl/debug.h"

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <string.h>


// ---------------------------------------------------------------------------
// global that contains the to-be-logged string
//
char debugStr[DEBUGSTR_LEN];

// ---------------------------------------------------------------------------
// Appends a message to the log file
//
void DebugLogLine( void )
{
   char *ct;
   sint fd;

   fd = open("EPG.LOG", O_WRONLY|O_CREAT|O_APPEND, 0666);
   if (fd >= 0)
   {
      time_t ts = time(NULL);
      ct = ctime(&ts);
      write(fd, ct, 20);
      write(fd, debugStr, strlen(debugStr));
      close(fd);
   }
   else
      perror("open epg log file EPG.LOG");

   printf("%s", debugStr);
}

// ---------------------------------------------------------------------------
// Empty function, only provided as a default break point
//
void DebugSetError( void )
{
}  

#endif // DEBUG_GLOBAL_SWITCH == ON

