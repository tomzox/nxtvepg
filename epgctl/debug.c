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
 *  Author: Tom Zoerner
 *
 *  $Id: debug.c,v 1.11 2001/02/25 16:03:08 tom Exp tom $
 */

#define __DEBUG_C

#if DEBUG_GLOBAL_SWITCH == ON
# define DEBUG_SWITCH ON
#else
# define DEBUG_SWITCH OFF
#endif

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include <malloc.h>
#ifdef WIN32
#include <process.h>
#ifdef HALT_ON_FAILED_ASSERTION
#include <windows.h>
#endif
#endif


#if DEBUG_SWITCH == ON

#ifdef WIN32
#include <stdlib.h>
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
// definitions and globals vars for Malloc verification
//
#if CHK_MALLOC == ON
// define magic string that allows to detect memory overwrites
static const char * const pMallocMagic = "Mägi";
static const char * const pMallocXmark = "Nöpe";
#define MALLOC_CHAIN_MAGIC_LEN     4

// define structure that's used to chain all malloc'ed memory
#ifndef WIN32
#define MALLOC_CHAIN_FILENAME_LEN 20
#else
// need more space for Win32 since there the complete path is included in __FILE__
#define MALLOC_CHAIN_FILENAME_LEN 50
#endif
typedef struct MALLOC_CHAIN_STRUCT
{
   struct MALLOC_CHAIN_STRUCT * prev;
   struct MALLOC_CHAIN_STRUCT * next;
   char                         fileName[MALLOC_CHAIN_FILENAME_LEN];
   int                          line;
   size_t                       size;
   char                         magic1[MALLOC_CHAIN_MAGIC_LEN];
} MALLOC_CHAIN;
#define MALLOC_CHAIN_ADD_LEN (sizeof(MALLOC_CHAIN) + MALLOC_CHAIN_MAGIC_LEN)

// global that points to the start of the chain of malloc'ed memory
static MALLOC_CHAIN * pMallocChain = NULL;

// globals that allow to monitor memory usage
static ulong malUsage = 0L;
static ulong malPeak  = 0L;
#endif

// ---------------------------------------------------------------------------
// global that contains the string to be logged
//
char debugStr[DEBUGSTR_LEN];

// ---------------------------------------------------------------------------
// Appends a message to the log file
//
void DebugLogLine( bool doHalt )
{
   char *ct;
   sint fd;

   fd = open("debug.out", O_WRONLY|O_CREAT|O_APPEND, 0666);
   if (fd >= 0)
   {
      time_t ts = time(NULL);
      ct = ctime(&ts);
      write(fd, ct, 20);
      write(fd, debugStr, strlen(debugStr));
      close(fd);
   }
   else
      perror("open epg log file debug.out");

   printf("%s", debugStr);

   #ifdef HALT_ON_FAILED_ASSERTION
   if (doHalt)
   {
      #ifndef WIN32
      abort();
      #else
      MessageBox(NULL, debugStr, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      #endif
   }
   #endif
}

// ---------------------------------------------------------------------------
// Empty function, only provided as a default break point
//
void DebugSetError( void )
{
}  

#endif // DEBUG_GLOBAL_SWITCH == ON

#if CHK_MALLOC == ON
// ---------------------------------------------------------------------------
// Wrapper for the C library malloc() function
// - maintains a list of all allocated blocks
//
void * chk_malloc( size_t size, const char * pFileName, int line )
{
   MALLOC_CHAIN * pElem;

   // perform the actual allocation, including some bytes for our framework
   pElem = malloc(size + MALLOC_CHAIN_ADD_LEN);

   // check the result pointer
   if (pElem == NULL)
   {
      SHOULD_NOT_BE_REACHED;
      fprintf(stderr, "malloc failed (%d bytes) - abort.\n", size);
      exit(-1);
   }

   // save info about the caller
   strncpy(pElem->fileName, pFileName, MALLOC_CHAIN_FILENAME_LEN - 1);
   pElem->fileName[MALLOC_CHAIN_FILENAME_LEN - 1] = 0;
   pElem->line = line;
   pElem->size = size;

   // monitor maximum memory usage
   malUsage += size;
   if (malUsage > malPeak)
      malPeak = malUsage;

   // write a magic before and after the data
   memcpy(pElem->magic1, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN));
   memcpy((uchar *)&pElem[1] + size, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN));

   // unshift the element to the start of the chain
   pElem->next = pMallocChain;
   pElem->prev = NULL;
   pMallocChain = pElem;
   if (pElem->next != NULL)
   {
      assert(pElem->next->prev == NULL);
      pElem->next->prev = pElem;
   }

   return (void *)&pElem[1];
}  

// ---------------------------------------------------------------------------
// Wrapper for the C library free() function
// - removes the block from the list of allocated blocks
// - checks if block boundaries were overwritten
//
void chk_free( void * ptr )
{
   MALLOC_CHAIN * pElem;

   pElem = (MALLOC_CHAIN *)((ulong)ptr - sizeof(MALLOC_CHAIN));

   // check the magic strings before and after the data
   assert(memcmp(pElem->magic1, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN)) == 0);
   assert(memcmp((uchar *)&pElem[1] + pElem->size, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN)) == 0);

   // update memory usage
   assert(malUsage >= pElem->size);
   malUsage -= pElem->size;

   // invalidate the magic
   memcpy(pElem->magic1, pMallocXmark, sizeof(MALLOC_CHAIN_MAGIC_LEN));
   memcpy((uchar *)&pElem[1] + pElem->size, pMallocXmark, sizeof(MALLOC_CHAIN_MAGIC_LEN));

   // remove the element from the chain
   if (pElem->prev != NULL)
   {
      assert(pElem->prev->next == pElem);
      pElem->prev->next = pElem->next;
   }
   else
   {
      assert(pMallocChain == pElem);
      pMallocChain = pElem->next;
   }
   if (pElem->next != NULL)
   {
      assert(pElem->next->prev == pElem);
      pElem->next->prev = pElem->prev;
   }

   // perform the actual free
   free(pElem);
}

// ---------------------------------------------------------------------------
// Check if all memory was freed
// - should be called right before the process terminates
//
void chk_memleakage( void )
{
   assert(pMallocChain == NULL);
   assert(malUsage == 0);

   //printf("chk-memleakage: no leak, max usage: %ld bytes\n", malPeak);
}

#else //CHK_MALLOC == OFF

// ---------------------------------------------------------------------------
// Wrapper for malloc to check for error return
//
void * xmalloc( size_t size )
{
   void * ptr = malloc(size);
   if (ptr == NULL)
   {  // malloc failed - should never happen
      #ifndef WIN32
      fprintf(stderr, "malloc failed (%d bytes) - abort.\n", size);
      SHOULD_NOT_BE_REACHED;
      exit(-1);
      #else
      MessageBox(NULL, "Memory allocation failure - Terminating", "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      // force an exception that will be caught and shut down the process properly
      *(uchar *)ptr = 0;
      #endif
   }
   return ptr;
}

#endif //CHK_MALLOC == OFF

