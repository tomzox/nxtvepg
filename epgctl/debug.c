/*
 *  Debug service module
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 */

#define __DEBUG_C

#if DEBUG_GLOBAL_SWITCH == ON
# define DEBUG_SWITCH ON
#else
# define DEBUG_SWITCH OFF
#endif

#include <stdlib.h>
#ifdef WIN32
#include <process.h>
#if defined(HALT_ON_FAILED_ASSERTION) || (CHK_MALLOC == OFF)
#include <windows.h>
#endif
#endif


#if DEBUG_SWITCH == ON

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
#ifndef WIN32
# include <pthread.h>
#endif

#endif  // DEBUG_SWITCH == ON

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"


// ---------------------------------------------------------------------------
// definitions and globals vars for Malloc verification
//
#if CHK_MALLOC == ON
// define magic string that allows to detect memory overwrites
static const char * const pMallocMagic = "M\xe4gi";
static const char * const pMallocXmark = "N\xF6pe";
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
static uint malRealloc = 0;
static uint malReallocOk = 0;

#ifndef WIN32
static pthread_mutex_t  check_malloc_mutex;
#endif
#endif  // CHK_MALLOC == ON


#if DEBUG_SWITCH == ON
// ---------------------------------------------------------------------------
// global that contains the string to be logged
//
char debugStr[DEBUGSTR_LEN];

// ---------------------------------------------------------------------------
// Appends a message to the log file
//
void DebugLogLine( bool doHalt )
{
   static bool debugStrInitialized = FALSE;
   char *ct;
   sint fd;
   ssize_t wstat;

   fd = open("debug.out", O_WRONLY|O_CREAT|O_APPEND, 0666);
   if (fd >= 0)
   {
      time_t ts = time(NULL);
      ct = ctime(&ts);
      wstat = write(fd, ct, 20);
      wstat = write(fd, debugStr, strlen(debugStr));
      if (wstat == -1)
      {
        /* dummy to avoid compiler warning */
      }
      close(fd);
   }
   else
      perror("open epg log file debug.out");

   #ifndef WIN32
   fprintf(stderr, "%s", debugStr);
   #else
   OutputDebugString(debugStr);
   #endif

   #ifdef HALT_ON_FAILED_ASSERTION
   if (doHalt)
   {
      #ifndef WIN32
      abort();
      #else
      MessageBox(NULL, debugStr, "nxtvepg", MB_ICONSTOP | MB_OK | MB_SETFOREGROUND);
      #endif
   }
   #endif

   // write a little marker at the end of the debug string and check if it's ever overwritten
   if (debugStrInitialized == FALSE)
   {
      debugStr[DEBUGSTR_LEN - 1] = 0xF6U;
   }
   else if ((uchar)debugStr[DEBUGSTR_LEN - 1] != 0xF6U)
   {
      debugStrInitialized = FALSE;
      fatal0("DebugLogLine: exceeded debug str max length");  // note: recursive call!
   }
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
// - warning: not MT-safe
// - maintains a list of all allocated blocks
//
void * chk_malloc( size_t size, const char * pCallerFile, int callerLine )
{
   MALLOC_CHAIN * pElem;

   assert(size > 0);

   // perform the actual allocation, including some bytes for our framework
   pElem = (MALLOC_CHAIN*) malloc(size + MALLOC_CHAIN_ADD_LEN);

   // check the result pointer
   if (pElem == NULL)
   {
      SHOULD_NOT_BE_REACHED;
      fprintf(stderr, "malloc failed (%ld bytes) - abort.\n", (ulong)size);
      exit(-1);
   }

   // save info about the caller
   strncpy(pElem->fileName, pCallerFile, MALLOC_CHAIN_FILENAME_LEN - 1);
   pElem->fileName[MALLOC_CHAIN_FILENAME_LEN - 1] = 0;
   pElem->line = callerLine;
   pElem->size = size;

   // write a magic before and after the data
   memcpy(pElem->magic1, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN));
   memcpy((uchar *)&pElem[1] + size, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN));

#ifndef WIN32
   pthread_mutex_lock(&check_malloc_mutex);
#endif
   // monitor maximum memory usage
   malUsage += size;
   if (malUsage > malPeak)
      malPeak = malUsage;

   // unshift the element to the start of the chain
   pElem->next = pMallocChain;
   pElem->prev = NULL;
   pMallocChain = pElem;
   if (pElem->next != NULL)
   {
      assert(pElem->next->prev == NULL);
      pElem->next->prev = pElem;
   }
   //fprintf(stderr, "chk-malloc: 0x%lX: %s, line %d, size %ld\n", (long)pElem, pElem->fileName, pElem->line, (ulong)pElem->size);
#ifndef WIN32
   pthread_mutex_unlock(&check_malloc_mutex);
#endif

   return (void *)&pElem[1];
}  

// ---------------------------------------------------------------------------
// Wrapper for the C library realloc() function
// - warning: not MT-safe
//
void * chk_realloc( void * ptr, size_t size, const char * pCallerFile, int callerLine )
{
   MALLOC_CHAIN * pElem;
   MALLOC_CHAIN * pPrevElem;
   size_t prevSize;

   if (ptr == NULL)
       return chk_malloc(size, pCallerFile, callerLine);

   pElem = (MALLOC_CHAIN *)((ulong)ptr - sizeof(MALLOC_CHAIN));
   pPrevElem = pElem;
   prevSize = pElem->size;

   assert(size > 0);
   // check the magic strings before and after the data
   assert(memcmp(pElem->magic1, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN)) == 0);
   assert(memcmp((uchar *)&pElem[1] + pElem->size, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN)) == 0);

   // perform the actual re-allocation
   pElem = (MALLOC_CHAIN*) realloc(pElem, size + MALLOC_CHAIN_ADD_LEN);

   // check the result pointer
   if (pElem == NULL)
   {
      SHOULD_NOT_BE_REACHED;
      fprintf(stderr, "realloc failed (%ld bytes) - abort.\n", (ulong)size);
      exit(-1);
   }

   // update magic after the data
   memcpy((uchar *)&pElem[1] + size, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN));
   //fprintf(stderr, "chk-realloc: 0x%lX: %s, line %d, size %ld; caller: %s, line: %d\n", (long)pElem, pElem->fileName, pElem->line, (ulong)pElem->size, pCallerFile, callerLine);

   // update size in header magic
   pElem->size = size;

#ifndef WIN32
   pthread_mutex_lock(&check_malloc_mutex);
#endif
   // update links to the current element in case the pointer changed
   if (pElem->prev != NULL)
   {
      pElem->prev->next = pElem;
   }
   else
   {
      pMallocChain = pElem;
   }
   if (pElem->next != NULL)
   {
      pElem->next->prev = pElem;
   }

   // monitor maximum memory usage
   assert(malUsage >= prevSize);
   malUsage = malUsage - prevSize + size;
   if (malUsage > malPeak)
      malPeak = malUsage;

   malRealloc += 1;
   if (pElem == pPrevElem)
      malReallocOk += 1;
#ifndef WIN32
   pthread_mutex_unlock(&check_malloc_mutex);
#endif

   return (void *)&pElem[1];
}

// ---------------------------------------------------------------------------
// Wrapper for the C library free() function
// - removes the block from the list of allocated blocks
// - checks if block boundaries were overwritten
//
void chk_free( void * ptr, const char * pCallerFile, int callerLine )
{
   MALLOC_CHAIN * pElem;

   pElem = (MALLOC_CHAIN *)((ulong)ptr - sizeof(MALLOC_CHAIN));

   //fprintf(stderr, "chk-free: 0x%lX: %s, line %d, size %ld; caller: %s, line: %d\n", (long)pElem, pElem->fileName, pElem->line, (ulong)pElem->size, pCallerFile, callerLine);

   // check the magic strings before and after the data
   if (memcmp(pElem->magic1, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN)) != 0)
   {
      if (memcmp(pElem->magic1, pMallocXmark, sizeof(MALLOC_CHAIN_MAGIC_LEN)) == 0)
         fatal6("chk-free: doubly freed allocated buffer 0x%lX: %s, line %d, size %ld; caller: %s, line %d", (long)pElem, pElem->fileName, pElem->line, (ulong)pElem->size, pCallerFile, callerLine);
      else
         fatal6("chk-free: invalid magic before allocated buffer 0x%lX: %s, line %d, size %ld; caller: %s, line %d", (long)pElem, pElem->fileName, pElem->line, (ulong)pElem->size, pCallerFile, callerLine);
   }
   if (memcmp((uchar *)&pElem[1] + pElem->size, pMallocMagic, sizeof(MALLOC_CHAIN_MAGIC_LEN)) != 0)
   {
      fatal6("chk-free: invalid magic after allocated buffer 0x%lX: %s, line %d, size %ld; caller: %s, line: %d", (long)pElem, pElem->fileName, pElem->line, (ulong)pElem->size, pCallerFile, callerLine);
   }

   // invalidate the magic
   memcpy(pElem->magic1, pMallocXmark, sizeof(MALLOC_CHAIN_MAGIC_LEN));
   memcpy((uchar *)&pElem[1] + pElem->size, pMallocXmark, sizeof(MALLOC_CHAIN_MAGIC_LEN));

#ifndef WIN32
   pthread_mutex_lock(&check_malloc_mutex);
#endif
   // update memory usage
   if (malUsage < pElem->size)
   {
      fatal7("chk-free: mem usage %ld < freed size: buffer 0x%lX: %s, line %d, size %ld; caller: %s, line: %d", malUsage, (long)pElem, pElem->fileName, pElem->line, (ulong)pElem->size, pCallerFile, callerLine);
   }
   malUsage -= pElem->size;

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
#ifndef WIN32
   pthread_mutex_unlock(&check_malloc_mutex);
#endif

   // perform the actual free
   free(pElem);
}

// ---------------------------------------------------------------------------
// Check if all memory was freed
// - should be called right before the process terminates
//
void chk_memleakage( void )
{
#if DEBUG_SWITCH == ON
   MALLOC_CHAIN * pWalk;
   uint  count, sum;

   if (pMallocChain != NULL)
   {
      pWalk = pMallocChain;
      sum = count = 0;
      while (pWalk != NULL)
      {
         sum   += pWalk->size;
         count += 1;
         pWalk = pWalk->next;
      }
      debug2("chk-memleakage: %d elements not freed, total %d bytes", count, sum);

      pWalk = pMallocChain;
      for (count=0; count < 10; count++)
      {
         debug3("chk-memleakage: not freed: %s, line %d, size %ld", pWalk->fileName, pWalk->line, (ulong)pWalk->size);
         pWalk = pWalk->next;
      }
   }
#endif

   assert(pMallocChain == NULL);
   assert(malUsage == 0);

   //fprintf(stderr, "chk-memleakage: no leak, max usage: %ld bytes\n", malPeak);
   //fprintf(stderr, "chk-memleakage: no leak, in-place reallocs: %d of %d\n", malReallocOk, malRealloc);
}

// ---------------------------------------------------------------------------
// Replacement for strdup which allows checking that memory is freed
//
char * chk_strdup( const char * pSrc, const char * pCallerFile, int callerLine )
{
   char * pDst = (char*) chk_malloc(strlen(pSrc) + 1, pCallerFile, callerLine);
   strcpy(pDst, pSrc);
   return pDst;
}  

#else //CHK_MALLOC == OFF

// ---------------------------------------------------------------------------
// Wrapper for malloc to check for error result
//
void * xmalloc( size_t size )
{
   void * ptr;

   assert(size > 0);

   ptr = malloc(size);
   if (ptr == NULL)
   {  // malloc failed - should never happen
      #ifndef WIN32
      fprintf(stderr, "malloc failed (%d bytes) - abort.\n", (int) size);
      SHOULD_NOT_BE_REACHED;
      exit(-1);
      #else
      MessageBox(NULL, "Memory allocation failure - Terminating", "nxtvepg", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      // force an exception that will be caught and shut down the process properly
      *(uchar *)ptr = 0;
      #endif
   }
   return ptr;
}

// ---------------------------------------------------------------------------
// Wrapper for realloc to check for error result
//
void * xrealloc( void * ptr, size_t size )
{
   assert(size > 0);

   ptr = realloc(ptr, size);
   if (ptr == NULL)
   {  // malloc failed - should never happen
      #ifndef WIN32
      fprintf(stderr, "realloc failed (%d bytes) - abort.\n", (int) size);
      SHOULD_NOT_BE_REACHED;
      exit(-1);
      #else
      MessageBox(NULL, "Memory allocation failure - Terminating", "nxtvepg", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
      // force an exception that will be caught and shut down the process properly
      *(uchar *)ptr = 0;
      #endif
   }
   return ptr;
}

// ---------------------------------------------------------------------------
// Replacement for strdup to check for error result
//
char * xstrdup( const char * pSrc )
{
   char * pDst = (char*) xmalloc(strlen(pSrc) + 1);
   strcpy(pDst, pSrc);
   return pDst;
}  

#endif //CHK_MALLOC == OFF

