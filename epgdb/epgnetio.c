/*
 *  Nextview EPG block network client & server
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
 *    This module transmits received blocks and acq stats per UNIX domain
 *    (i.e. pipes) or TCP/IP from a server process to connected Nextview
 *    clients. This allows multiple GUIs to use one TV card for data
 *    acquisition.
 *
 *  Author:
 *          Tom Zoerner
 *
 *  $Id: epgnetio.c,v 1.28 2002/04/29 19:27:15 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#ifdef USE_DAEMON

#ifdef WIN32
#include <winsock2.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#ifndef WIN32
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <syslog.h>
#endif

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgnetio.h"

#if defined(linux) || defined(__NetBSD__)
#define HAVE_GETADDRINFO
#endif

#ifndef WIN32
#define closesocket(FD)  close(FD)
#endif


typedef struct
{
   bool    do_logtty;
   int     sysloglev;
   int     fileloglev;
   char  * pLogfileName;
} EPGNETIO_LOGCF;

static EPGNETIO_LOGCF epgNetIoLogCf =
#if (DEBUG_SWITCH == OFF) || defined(DPRINTF_OFF)
   {TRUE, 0, 0, NULL};
#else
   {FALSE, 0, 0, NULL};
#endif

// ----------------------------------------------------------------------------
// Local settings
//
#define SRV_IO_TIMEOUT          60
#define SRV_LISTEN_BACKLOG_LEN  10
#define SRV_CLNT_SOCK_PATH      "/tmp/nxtvepg.0"

// ---------------------------------------------------------------------------
// Open messagebox with system error string (e.g. "invalid path")
//
#ifdef WIN32
static void GetWindowsSocketErrorMsg( uchar * pCause, DWORD code )
{
   uchar * pMsg;

   pMsg = xmalloc(strlen(pCause) + 300);
   strcpy(pMsg, pCause);
   strcat(pMsg, ": ");

   // translate the error code into a human readable text
   FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, code, LANG_USER_DEFAULT,
                 pMsg + strlen(pMsg), 300 - strlen(pMsg) - 1, NULL);

   // open a small dialog window with the error message and an OK button
   MessageBox(NULL, pMsg, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);

   xfree(pMsg);
}
#endif

// ----------------------------------------------------------------------------
// Save text describing network error cause
// - argument list has to be terminated with NULL pointer
// - to be displayed by the GUI to help the user fixing the problem
//
void EpgNetIo_SetErrorText( char ** ppErrorText, const char * pText, ... )
{
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
      }
   }
}

// ----------------------------------------------------------------------------
// Append entry to logfile
//
void EpgNetIo_Logger( int level, int clnt_fd, const char * pText, ... )
{
   va_list argl;
   char timestamp[32], fdstr[20];
   const char *argv[5];
   uint argc, idx;
   sint fd;
   time_t now = time(NULL);

   if (pText != NULL)
   {
      // open the logfile, if one is configured
      if ( (level <= epgNetIoLogCf.fileloglev) &&
           (epgNetIoLogCf.pLogfileName != NULL) )
      {
         fd = open(epgNetIoLogCf.pLogfileName, O_WRONLY|O_CREAT|O_APPEND, 0666);
         if (fd >= 0)
         {  // each line in the file starts with a timestamp
            strftime(timestamp, sizeof(timestamp) - 1, "[%d/%b/%Y:%H:%M:%S +0000] ", gmtime(&now));
            write(fd, timestamp, strlen(timestamp));
         }
      }
      else
         fd = -1;

      if (epgNetIoLogCf.do_logtty && (level <= LOG_WARNING))
         fprintf(stderr, "nxtvepg: ");

      argc = 0;
      // add pointer to file descriptor (for client requests) or pid (for general infos)
      if (clnt_fd != -1)
         sprintf(fdstr, "fd %d: ", clnt_fd);
      else
      {
         #ifndef WIN32
         sprintf(fdstr, "pid %d: ", (int)getpid());
         #else
         // XXX TODO: check how to query pid on windows
         strcpy(fdstr, "daemon: ");
         #endif
      }
      argv[argc++] = fdstr;

      // add pointer to first log output string
      argv[argc++] = pText;

      // append pointers to the rest of the log strings
      va_start(argl, pText);
      while ((argc < 5) && ((pText = va_arg(argl, char *)) != NULL))
      {
         argv[argc++] = pText;
      }
      va_end(argl);

      // print the strings to the file and/or stderr
      for (idx=0; idx < argc; idx++)
      {
         if (fd >= 0)
            write(fd, argv[idx], strlen(argv[idx]));
         if (epgNetIoLogCf.do_logtty && (level <= LOG_WARNING))
            fprintf(stderr, "%s", argv[idx]);
      }

      // terminate the line with a newline character and close the file
      if (fd >= 0)
      {
         write(fd, "\n", 1);
         close(fd);
      }
      if (epgNetIoLogCf.do_logtty && (level <= LOG_WARNING))
      {
         fprintf(stderr, "\n");
         fflush(stderr);
      }

      #ifndef WIN32
      // syslog output
      if (level <= epgNetIoLogCf.sysloglev)
      {
         switch (argc)
         {
            case 1: syslog(level, "%s", argv[0]); break;
            case 2: syslog(level, "%s%s", argv[0], argv[1]); break;
            case 3: syslog(level, "%s%s%s", argv[0], argv[1],argv[2]); break;
            case 4: syslog(level, "%s%s%s%s", argv[0], argv[1],argv[2],argv[3]); break;
         }
      }
      #endif
   }
}

// ----------------------------------------------------------------------------
// Set parameters for event logging
// - loglevel usage
//   ERR    : fatal errors (which lead to program termination)
//   WARNING: this shouldn't happen error (OS failure or internal errors)
//   NOTICE : start/stop of the daemon
//   INFO   : connection establishment and shutdown
//
void EpgNetIo_SetLogging( int sysloglev, int fileloglev, const char * pLogfileName )
{
   // free the memory allocated for the old config strings
   if (epgNetIoLogCf.pLogfileName != NULL)
   {
      xfree(epgNetIoLogCf.pLogfileName);
      epgNetIoLogCf.pLogfileName = NULL;
   }

   // make a copy of the new config strings
   if (pLogfileName != NULL)
   {
      epgNetIoLogCf.pLogfileName = xmalloc(strlen(pLogfileName) + 1);
      strcpy(epgNetIoLogCf.pLogfileName, pLogfileName);
      epgNetIoLogCf.fileloglev = ((fileloglev > 0) ? (fileloglev + LOG_ERR) : -1);
   }
   else
      epgNetIoLogCf.fileloglev = -1;

   #ifndef WIN32
   if (sysloglev && !epgNetIoLogCf.sysloglev)
   {
      openlog("nxtvepg", LOG_PID, LOG_DAEMON);
   }
   else if (!sysloglev && epgNetIoLogCf.sysloglev)
   {
      closelog();
   }
   #endif

   // convert GUI log-level setting to syslog enum value
   epgNetIoLogCf.sysloglev = ((sysloglev > 0) ? (sysloglev + LOG_ERR) : -1);
}

// ----------------------------------------------------------------------------
// Check for incomplete read or write buffer
//
bool EpgNetIo_IsIdle( EPGNETIO_STATE * pIO )
{
   return ( (pIO->writeLen == 0) &&
            (pIO->waitRead == FALSE) && (pIO->readLen == 0) );
}

// ----------------------------------------------------------------------------
// Check for I/O timeout
// - returns TRUE in case of timeout
//
bool EpgNetIo_CheckTimeout( EPGNETIO_STATE * pIO, time_t now )
{
   return ( (now > pIO->lastIoTime + SRV_IO_TIMEOUT) &&
            (EpgNetIo_IsIdle(pIO) == FALSE) );
}

// ----------------------------------------------------------------------------
// Read or write a message from/to the network socket
// - only one of read or write is processed at the same time
// - reading is done in 2 phases: first the length of the message is read into
//   a small buffer; then a buffer is allocated for the complete message and the
//   length variable copied into it and the rest of the message read afterwords.
// - a closed network connection is indicated by a 0 read from a readable socket.
//   Readability is indicated by the select syscall and passed here via
//   parameter closeOnZeroRead.
// - after errors the I/O state (indicated by FALSE result) is not reset, because
//   the caller is expected to close the connection.
//
bool EpgNetIo_HandleIO( EPGNETIO_STATE * pIO, bool * pBlocked, bool closeOnZeroRead )
{
   time_t   now;
   ssize_t  len;
   bool     err = FALSE;
   bool     result = TRUE;

   *pBlocked = FALSE;
   now = time(NULL);
   if (pIO->writeLen > 0)
   {
      // write the message header
      assert(pIO->writeLen >= sizeof(EPGNETIO_MSG_HEADER));
      if (pIO->writeOff < sizeof(EPGNETIO_MSG_HEADER))
      {  // write message header
         len = send(pIO->sock_fd, ((char *)&pIO->writeHeader) + pIO->writeOff, sizeof(EPGNETIO_MSG_HEADER) - pIO->writeOff, 0);
         if (len >= 0)
         {
            pIO->lastIoTime = now;
            pIO->writeOff += len;
         }
         else
            err = TRUE;
      }

      // write the message body, if the header is written
      if ((err == FALSE) && (pIO->writeOff >= sizeof(EPGNETIO_MSG_HEADER)) && (pIO->writeOff < pIO->writeLen))
      {
         len = send(pIO->sock_fd, ((char *)pIO->pWriteBuf) + pIO->writeOff - sizeof(EPGNETIO_MSG_HEADER), pIO->writeLen - pIO->writeOff, 0);
         if (len > 0)
         {
            pIO->lastIoTime = now;
            pIO->writeOff += len;
         }
         else
            err = TRUE;
      }

      if (err == FALSE)
      {
         if (pIO->writeOff >= pIO->writeLen)
         {  // all data has been written -> free the buffer; reset write state
            if (pIO->freeWriteBuf)
               xfree(pIO->pWriteBuf);
            pIO->pWriteBuf = NULL;
            pIO->writeLen  = 0;
         }
         else if (pIO->writeOff < pIO->writeLen)
            *pBlocked = TRUE;
      }
      #ifndef WIN32
      else if ((errno != EAGAIN) && (errno != EINTR))
      #else
      else if ((WSAGetLastError() != WSAEWOULDBLOCK))
      #endif
      {  // network error -> close the connection
         debug2("EpgNetIo-HandleIO: write error on fd %d: %s", pIO->sock_fd, strerror(errno));
         result = FALSE;
      }
      else if (errno == EAGAIN)
         *pBlocked = TRUE;
   }
   else if (pIO->waitRead || (pIO->readLen > 0))
   {
      len = 0;  // compiler dummy
      if (pIO->waitRead)
      {  // in read phase one: read the message length
         assert(pIO->readOff < sizeof(pIO->readHeader));
         len = recv(pIO->sock_fd, (char *)&pIO->readHeader + pIO->readOff, sizeof(pIO->readHeader) - pIO->readOff, 0);
         if (len > 0)
         {
            closeOnZeroRead = FALSE;
            pIO->lastIoTime = now;
            pIO->readOff += len;
            if (pIO->readOff >= sizeof(pIO->readHeader))
            {  // message length variable has been read completely
               // convert from network byte order (big endian) to host byte order
               pIO->readLen = ntohs(pIO->readHeader.len);
               pIO->readHeader.len = pIO->readLen;
               //XXX//dprintf2("EpgNetIo-HandleIO: fd %d: new block: size %d\n", pIO->sock_fd, pIO->readLen);
               if ((pIO->readLen < EPGDBSAV_MAX_BLOCK_SIZE) &&
                   (pIO->readLen >= sizeof(EPGNETIO_MSG_HEADER)))
               {  // message size acceptable -> allocate a buffer with the given size
                  if (pIO->readLen > sizeof(EPGNETIO_MSG_HEADER))
                     pIO->pReadBuf = xmalloc(pIO->readLen - sizeof(EPGNETIO_MSG_HEADER));
                  // enter the second phase of the read process
                  pIO->waitRead = FALSE;
               }
               else
               {  // illegal message size -> protocol error
                  debug2("EpgNetIo-HandleIO: fd %d: illegal block size %d", pIO->sock_fd, pIO->readLen);
                  result = FALSE;
               }
            }
            else
               *pBlocked = TRUE;
         }
         else
            err = TRUE;
      }

      if ((err == FALSE) && (pIO->waitRead == FALSE) && (pIO->readLen > sizeof(EPGNETIO_MSG_HEADER)))
      {  // in read phase two: read the complete message into the allocated buffer
         assert(pIO->pReadBuf != NULL);
         len = recv(pIO->sock_fd, pIO->pReadBuf + pIO->readOff - sizeof(EPGNETIO_MSG_HEADER), pIO->readLen - pIO->readOff, 0);
         if (len > 0)
         {
            pIO->lastIoTime = now;
            pIO->readOff += len;
         }
         else
            err = TRUE;
      }

      if (err == FALSE)
      {
         if (pIO->readOff < pIO->readLen)
         {  // not all data has been read yet
            *pBlocked = TRUE;
         }
      }
      else
      {
         if ((len == 0) && closeOnZeroRead)
         {  // zero bytes read after select returned readability -> network error or connection closed by peer
            debug1("EpgNetIo-HandleIO: zero len read on fd %d", pIO->sock_fd);
            result = FALSE;
         }
         #ifndef WIN32
         else if ((len < 0) && (errno != EAGAIN) && (errno != EINTR))
         #else
         else if ((len < 0) && (WSAGetLastError() != WSAEWOULDBLOCK))
         #endif
         {  // network error -> close the connection
            #ifndef WIN32
            debug3("EpgNetIo-HandleIO: read error on fd %d: len=%d, %s", pIO->sock_fd, len, strerror(errno));
            #else
            GetWindowsSocketErrorMsg("socket read error", WSAGetLastError());
            #endif
            result = FALSE;
         }
         else if (errno == EAGAIN)
            *pBlocked = TRUE;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Free resources allocated for IO
//
void EpgNetIo_CloseIO( EPGNETIO_STATE * pIO )
{
   if (pIO->sock_fd != -1)
   {
      closesocket(pIO->sock_fd);
      pIO->sock_fd = -1;
   }

   if (pIO->pReadBuf != NULL)
   {
      xfree(pIO->pReadBuf);
      pIO->pReadBuf = NULL;
   }

   if (pIO->pWriteBuf != NULL)
   {
      if (pIO->freeWriteBuf)
         xfree(pIO->pWriteBuf);
      pIO->pWriteBuf = NULL;
   }
}

// ----------------------------------------------------------------------------
// Create a new message and prepare the I/O state for writing
// - length and pointer of the body may be zero (no payload)
//
void EpgNetIo_WriteMsg( EPGNETIO_STATE * pIO, EPGNETIO_MSG_TYPE type, uint msgLen, void * pMsg, bool freeBuf )
{
   assert((pIO->waitRead == FALSE) && (pIO->readLen == 0));  // I/O must be idle
   assert((pIO->writeLen == 0) && (pIO->pWriteBuf == NULL));
   assert((msgLen == 0) || (pMsg != NULL));

   dprintf2("EpgNetIo-WriteMsg: msg type %d, len %d\n", type, sizeof(EPGNETIO_MSG_HEADER) + msgLen);

   pIO->pWriteBuf    = pMsg;
   pIO->freeWriteBuf = freeBuf;
   pIO->writeLen     = sizeof(EPGNETIO_MSG_HEADER) + msgLen;
   pIO->writeOff     = 0;

   // message header: length is coded in network byte order (i.e. big endian)
   pIO->writeHeader.len  = htons(pIO->writeLen);
   pIO->writeHeader.type = type;
}

// ----------------------------------------------------------------------------
// Transmit EPG blocks from the out queue
// - socket is "stuffed", i.e. blocks are transmitted until the write returns 0
//   or a short length
//
bool EpgNetIo_WriteEpgQueue( EPGNETIO_STATE * pIO, EPGDB_QUEUE * pOutQueue )
{
   EPGDB_BLOCK * pBlock;
   bool  ioBlocked;
   bool  result = TRUE;

   while ((pBlock = EpgDbQueue_Get(pOutQueue)) != NULL)
   {
      dprintf2("EpgNetIo-WriteEpgQueue: fd %d: write EPG block, msg size %d\n", pIO->sock_fd, sizeof(EPGNETIO_MSG_HEADER) + pBlock->size + BLK_UNION_OFF);

      pIO->pWriteBuf        = (void *) pBlock;
      pIO->freeWriteBuf     = TRUE;
      pIO->writeLen         = sizeof(EPGNETIO_MSG_HEADER) + pBlock->size + BLK_UNION_OFF;
      pIO->writeOff         = 0;
      pIO->writeHeader.len  = htons(pIO->writeLen);
      pIO->writeHeader.type = MSG_TYPE_BLOCK_IND;

      if (EpgNetIo_HandleIO(pIO, &ioBlocked, FALSE))
      {
         // if the last block could not be transmitted fully, quit the loop
         if (pIO->writeLen > 0)
         {
            dprintf0("EpgNetIo-WriteEpgQueue: socket blocked\n");
            break;
         }
      }
      else
      {
         result = FALSE;
         break;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Transmit an EPG block which is part of the database (i.e. not in a queue)
// - if the socket blocks during the write, it must be copied to a temporary
//   buffer, because the block could have be freed until the socket is writable
//   again
// - returns TRUE if more data can be written
//
bool EpgNetIo_WriteUncopiedEpgBlock( EPGDB_BLOCK * pBlock, EPGNETIO_STATE * pIO )
{
   bool ioBlocked;
   bool result;

   pIO->pWriteBuf        = (void *) pBlock;
   pIO->freeWriteBuf     = FALSE;
   pIO->writeLen         = sizeof(EPGNETIO_MSG_HEADER) + pBlock->size + BLK_UNION_OFF;
   pIO->writeOff         = 0;
   pIO->writeHeader.len  = htons(pIO->writeLen);
   pIO->writeHeader.type = MSG_TYPE_BLOCK_IND;

   // write the EPG block to the socket
   if (EpgNetIo_HandleIO(pIO, &ioBlocked, FALSE))
   {
      if (pIO->writeLen > 0)
      {  // not all data could be written -> must copy the block
         dprintf0("EpgNetIo-DumpEpgBlock: socket blocked - copy block\n");
         pIO->pWriteBuf = xmalloc(pBlock->size + BLK_UNION_OFF);
         memcpy(pIO->pWriteBuf, pBlock, pBlock->size + BLK_UNION_OFF);
         pIO->freeWriteBuf = TRUE;
         result = FALSE;
      }
      else
         result = TRUE;
   }
   else
      result = FALSE;

   return result;
}

// ----------------------------------------------------------------------------
// Transmit buffers from the PI timescale queue
// - same procedure as for EPG blocks
//
bool EpgNetIo_WriteTscQueue( EPGNETIO_STATE * pIO, EPGDB_PI_TSC * pTscQueue )
{
   const EPGDB_PI_TSC_BUF * pTscBuf;
   uint bufLen;
   bool  ioBlocked;
   bool  result = TRUE;

   while ((pTscBuf = EpgTscQueue_PopBuffer(pTscQueue, &bufLen)) != NULL)
   {
      dprintf2("EpgNetIo-WriteTscQueue: fd %d: write timescale buffer, msg size %d\n", pIO->sock_fd, sizeof(EPGNETIO_MSG_HEADER) + bufLen);

      pIO->pWriteBuf    = (void *) pTscBuf;
      pIO->freeWriteBuf = TRUE;
      pIO->writeLen     = sizeof(EPGNETIO_MSG_HEADER) + bufLen;
      pIO->writeOff     = 0;
      pIO->writeHeader.len  = htons(pIO->writeLen);
      pIO->writeHeader.type = MSG_TYPE_TSC_IND;

      if (EpgNetIo_HandleIO(pIO, &ioBlocked, FALSE))
      {
         // if the last block could not be transmitted fully, quit the loop
         if (pIO->writeLen > 0)
         {
            dprintf0("EpgNetIo-WriteEpgQueue: socket blocked\n");
            break;
         }
      }
      else
      {
         result = FALSE;
         break;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Dump AI and OI#0
// - return TRUE as long as it needs to be invoked again (when the socket becomes writable)
//   returns FALSE when all blocks are dumped
//
bool EpgNetIo_DumpAiOi( EPGNETIO_STATE * pIO, EPGDB_CONTEXT * dbc, EPGNETIO_DUMP * pDump )
{
   EPGDB_BLOCK * pBlock;
   bool result;

   assert((pIO->writeLen == 0) && (pIO->pWriteBuf == NULL));

   pBlock = NULL;
   do
   {
      if (pDump->dumpType == BLOCK_TYPE_AI)
      {
         dprintf0("EpgNetIo-DumpEpgBlock: start with AI block\n");
         pBlock = dbc->pAiBlock;
      }
      else
      {
         assert(pDump->dumpType == BLOCK_TYPE_OI);
         dprintf1("EpgNetIo-DumpEpgBlock: start with OI block  block=%d\n", pDump->dumpBlockNo);
         pBlock = dbc->pFirstGenericBlock[BLOCK_TYPE_OI];
         // for non-requested providers only OI block #0 is forwarded
         if ( (pBlock != NULL) && (pBlock->blk.all.block_no != 0) )
            pBlock = NULL;
      }

      if (pBlock != NULL)
      {
         // write the EPG block to the socket
         result = ! EpgNetIo_WriteUncopiedEpgBlock(pBlock, pIO);

         if (pIO->sock_fd != -1)
         {
            if (pDump->dumpType == BLOCK_TYPE_AI)
            {
               dprintf0("EpgNetIo-DumpEpgBlock: dump AI\n");
               pDump->dumpType = BLOCK_TYPE_OI;
            }
            else
            {  // dumped OI#0 of non-requested provider -> complete
               assert(pDump->dumpType == BLOCK_TYPE_OI);
               dprintf1("EpgNetIo-DumpEpgBlock: dump OI block_no=%d\n", pBlock->blk.all.block_no);
               pDump->dumpType = BLOCK_TYPE_INVALID;
            }
         }
         else
         {  // I/O error -> drop connection
            result = TRUE;
         }
      }
      else
      {  // block not found -> finished
         pDump->dumpType = BLOCK_TYPE_INVALID;
         result = FALSE;
      }
   }
   while ((result == FALSE) && (pDump->dumpType != BLOCK_TYPE_INVALID));

   return result;
}

// ---------------------------------------------------------------------------
// Search and write the next EPG block to be dumped
// - return TRUE as long as it needs to be invoked again (when the socket becomes writable)
//   returns FALSE when all blocks are dumped
//
bool EpgNetIo_DumpAllBlocks( EPGNETIO_STATE * pIO, EPGDB_CONTEXT * dbc, EPGNETIO_DUMP * pDump, EPGDB_PI_TSC * pTscQueue )
{
   EPGDB_BLOCK * pBlock;
   bool result;

   pBlock = NULL;
   do
   {
      if (pBlock == NULL)
      {
         // skip over the already dumped blocks
         // acq is running in the background, so we cannot search for the exact
         // block that was dumped last - it may have been removed
         switch (pDump->dumpType)
         {
            case BLOCK_TYPE_AI:
               dprintf0("EpgNetIo-DumpEpgBlock: start with AI block\n");
               pBlock = dbc->pAiBlock;
               break;

            case BLOCK_TYPE_NI:
            case BLOCK_TYPE_OI:
            case BLOCK_TYPE_MI:
            case BLOCK_TYPE_LI:
            case BLOCK_TYPE_TI:
               dprintf2("EpgNetIo-DumpEpgBlock: start with GENERIC block type=%d, block=%d\n", pDump->dumpType, pDump->dumpBlockNo);
               pBlock = dbc->pFirstGenericBlock[pDump->dumpType];
               while (pBlock != NULL)
               {
                  // dumpBlockNo contains the last dumped block no + 1; this allows to find #0
                  if (pBlock->blk.all.block_no >= pDump->dumpBlockNo)
                     break;

                  pBlock = pBlock->pNextBlock;
               }
               break;

            case BLOCK_TYPE_PI:
               dprintf2("EpgNetIo-DumpEpgBlock: start with PI block net=%d start=%ld\n", pDump->dumpNetwop, pDump->dumpStartTime);
               pBlock = dbc->pFirstPi;
               while (pBlock != NULL)
               {
                  if ( (pBlock->blk.pi.start_time > pDump->dumpStartTime) ||
                       ( (pBlock->blk.pi.start_time == pDump->dumpStartTime) &&
                         (pBlock->blk.pi.netwop_no > pDump->dumpNetwop) ))
                  {
                     break;
                  }
                  pBlock = pBlock->pNextBlock;
               }
               break;

            case BLOCK_TYPE_BI:
            default:
               fatal1("EpgNetIo-DumpEpgBlock: illegal block type %d\n", pDump->dumpType);
               pBlock = NULL;
               break;
         }
      }

      // search for one which was updated after the timestamp
      if (pDump->dumpType != BLOCK_TYPE_AI)
      {
         while ((pBlock != NULL) && (pBlock->updTimestamp <= pDump->dumpAcqTime))
         {
            pBlock = pBlock->pNextBlock;
         }
      }

      // if found, put it in a message
      if (pBlock != NULL)
      {
         // write the EPG block to the socket
         result = ! EpgNetIo_WriteUncopiedEpgBlock(pBlock, pIO);

         if (pIO->sock_fd != -1)
         {
            switch (pDump->dumpType)
            {
               case BLOCK_TYPE_AI:
                  // there's just one AI block, so always go on to the next type
                  dprintf0("EpgNetIo-DumpEpgBlock: dump AI\n");
                  pDump->dumpType = BLOCK_TYPE_PI;
                  pBlock = NULL;
                  break;

               case BLOCK_TYPE_PI:
                  dprintf2("EpgNetIo-DumpEpgBlock: dump PI net=%0d start=%ld\n", pBlock->blk.pi.netwop_no, pBlock->blk.pi.start_time);
                  // add to PI timescale queue, if requested
                  if (pTscQueue != NULL)
                  {
                     // XXX not quite right - this function assumes all blocks have latest version
                     EpgTscQueue_AddPi(pTscQueue, dbc, &pBlock->blk.pi, pBlock->stream);
                  }
                  pDump->dumpStartTime = pBlock->blk.pi.start_time;
                  pDump->dumpNetwop    = pBlock->blk.pi.netwop_no;
                  pBlock = pBlock->pNextBlock;
                  break;

               case BLOCK_TYPE_NI:
               case BLOCK_TYPE_OI:
               case BLOCK_TYPE_MI:
               case BLOCK_TYPE_LI:
               case BLOCK_TYPE_TI:
                  dprintf2("EpgNetIo-DumpEpgBlock: dump GENERIC type=%d block_no=%d\n", pBlock->type, pBlock->blk.all.block_no);
                  pDump->dumpBlockNo = pBlock->blk.all.block_no + 1;
                  pBlock = pBlock->pNextBlock;
                  break;

               case BLOCK_TYPE_BI:
               default:
                  fatal1("EpgNetIo-DumpEpgBlock: illegal block type %d\n", pDump->dumpType);
                  return FALSE;
            }
         }
         else
         {  // I/O error -> drop connection
            result = TRUE;
         }
      }
      else
      {  // no block found for the given type -> continue with next type
         switch (pDump->dumpType)
         {
            case BLOCK_TYPE_AI:
               debug0("EpgNetIo-DumpEpgBlock: no AI block in acq db - abort");
               pDump->dumpType = BLOCK_TYPE_INVALID;
               result = FALSE;
               break;

            case BLOCK_TYPE_PI:
               // continue with the first "generic" type (e.g. NI blocks)
               pDump->dumpType     = 0;
               pDump->dumpBlockNo  = 0;
               break;

            case BLOCK_TYPE_NI:
            case BLOCK_TYPE_OI:
            case BLOCK_TYPE_MI:
            case BLOCK_TYPE_LI:
            case BLOCK_TYPE_TI:
               if (pDump->dumpType + 1 < BLOCK_TYPE_GENERIC_COUNT)
               {  // continue with the first block of the next "generic" type
                  pDump->dumpType    += 1;
                  pDump->dumpBlockNo  = 0;
                  result = TRUE;
               }
               else
               {  // dump is complete
                  pDump->dumpType = BLOCK_TYPE_INVALID;
                  result = FALSE;
               }
               break;

            case BLOCK_TYPE_BI:
            default:
               fatal1("EpgNetIo-DumpEpgBlock: illegal block type %d\n", pDump->dumpType);
               result = FALSE;
               break;
         }
         result = FALSE;
      }
   }
   while ((result == FALSE) && (pDump->dumpType != BLOCK_TYPE_INVALID));

   return result;
}

// ----------------------------------------------------------------------------
// Implementation of the C library address handling functions
// - for platforms which to not have them in libc
// - documentation see the manpages
//
#ifndef HAVE_GETADDRINFO

#ifndef AI_PASSIVE
# define AI_PASSIVE 1
#endif

struct addrinfo
{
   int  ai_flags;
   int  ai_family;
   int  ai_socktype;
   int  ai_protocol;
   struct sockaddr * ai_addr;
   int  ai_addrlen;
};

enum
{
   GAI_UNSUP_FAM       = -1,
   GAI_NO_SERVICE_NAME = -2,
   GAI_UNKNOWN_SERVICE = -3,
   GAI_UNKNOWN_HOST    = -4,
};

static int getaddrinfo( const char * pHostName, const char * pServiceName, const struct addrinfo * pInParams, struct addrinfo ** ppResult )
{
   struct servent  * pServiceEntry;
   struct hostent  * pHostEntry;
   struct addrinfo * res;
   char  * pServiceNumEnd;
   uint  port;
   int   result;

   res = malloc(sizeof(struct addrinfo));
   *ppResult = res;

   memset(res, 0, sizeof(*res));
   res->ai_socktype  = pInParams->ai_socktype;
   res->ai_family    = pInParams->ai_family;
   res->ai_protocol  = pInParams->ai_protocol;

   if (pInParams->ai_family == PF_INET)
   {
      if ((pServiceName != NULL) || (*pServiceName == 0))
      {
         port = strtol(pServiceName, &pServiceNumEnd, 0);
         if (*pServiceNumEnd != 0)
         {
            pServiceEntry = getservbyname(pServiceName, "tcp");
            if (pServiceEntry != NULL)
               port = ntohs(pServiceEntry->s_port);
            else
               port = 0;
         }

         if (port != 0)
         {
            if (pHostName != NULL)
               pHostEntry = gethostbyname(pHostName);
            else
               pHostEntry = NULL;

            if ((pHostName == NULL) || (pHostEntry != NULL))
            {
               struct sockaddr_in * iad;

               iad = malloc(sizeof(struct sockaddr_in));
               res->ai_addr    = (struct sockaddr *) iad;
               res->ai_addrlen = sizeof(struct sockaddr_in);

               iad->sin_family      = AF_INET;
               iad->sin_port        = htons(port);
               if (pHostName != NULL)
                  memcpy(&iad->sin_addr, (char *) pHostEntry->h_addr, pHostEntry->h_length);
               else
                  iad->sin_addr.s_addr = INADDR_ANY;
               result = 0;
            }
            else
               result = GAI_UNKNOWN_HOST;
         }
         else
            result = GAI_UNKNOWN_SERVICE;
      }
      else
         result = GAI_NO_SERVICE_NAME;
   }
   else
      result = GAI_UNSUP_FAM;

   if (result != 0)
   {
      free(res);
      *ppResult = NULL;
   }
   return result;
}

static void freeaddrinfo( struct addrinfo * res )
{
   if (res->ai_addr != NULL)
      free(res->ai_addr);
   free(res);
}

static char * gai_strerror( int errCode )
{
   switch (errCode)
   {
      case GAI_UNSUP_FAM:       return "unsupported protocol family";
      case GAI_NO_SERVICE_NAME: return "missing service name or port number for TCP/IP";
      case GAI_UNKNOWN_SERVICE: return "unknown service name";
      case GAI_UNKNOWN_HOST:    return "unknown host";
      default:                  return "internal or unknown error";
   }
}
#endif  // HAVE_GETADDRINFO

// ----------------------------------------------------------------------------
// Get socket address for PF_UNIX aka PF_LOCAL address family
// - result is in the same format as from getaddrinfo
// - note: Linux getaddrinfo currently supports PF_UNIX queries too, however
//   this feature is not standardized and hence not portable (e.g. to NetBSD)
//
static int EpgNetIo_GetLocalSocketAddr( const char * pPathName, const struct addrinfo * pInParams, struct addrinfo ** ppResult )
{
#ifndef WIN32
   struct addrinfo * res;
   struct sockaddr_un * saddr;

   assert((pInParams->ai_family == PF_UNIX) && (pPathName != NULL));

   // note: use regular malloc instead of xmalloc in case memory is freed by the libc internal freeaddrinfo
   res = malloc(sizeof(struct addrinfo));
   *ppResult = res;

   memset(res, 0, sizeof(*res));
   res->ai_socktype  = pInParams->ai_socktype;
   res->ai_family    = pInParams->ai_family;
   res->ai_protocol  = pInParams->ai_protocol;

   saddr = malloc(sizeof(struct sockaddr_un));
   res->ai_addr      = (struct sockaddr *) saddr;
   res->ai_addrlen   = sizeof(struct sockaddr_un);

   strncpy(saddr->sun_path, pPathName, sizeof(saddr->sun_path) - 1);
   saddr->sun_path[sizeof(saddr->sun_path) - 1] = 0;
   saddr->sun_family = AF_UNIX;

   return 0;
#else
   // M$ windows has no local transport
   return GAI_UNSUP_FAM; // == EAI_FAMILY
#endif
}

// ----------------------------------------------------------------------------
// Open socket for listening
//
int EpgNetIo_ListenSocket( bool is_tcp_ip, const char * listen_ip, const char * listen_port )
{
   struct addrinfo    ask, *res;
   int  opt, rc;
   int  sock_fd;
   bool result = FALSE;

   memset(&ask, 0, sizeof(ask));
   ask.ai_flags    = AI_PASSIVE;
   ask.ai_socktype = SOCK_STREAM;
   sock_fd = -1;
   res = NULL;

   #ifdef PF_INET6
   if (is_tcp_ip)
   {  // try IP-v6: not supported everywhere yet, so errors must be silently ignored
      ask.ai_family = PF_INET6;
      rc = getaddrinfo(listen_ip, listen_port, &ask, &res);
      if (rc == 0)
      {
         sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
         if (sock_fd == -1)
         {
            debug0("EpgNetIo-Listen: socket (ipv6)");
            freeaddrinfo(res);
            res = NULL;
         }
      }
      else
         debug1("EpgNetIo-Listen: getaddrinfo (ipv6): %s", gai_strerror(rc));
   }
   #endif

   if (sock_fd == -1)
   {
      if (is_tcp_ip)
      {  // IP-v4 (IP-address is optional, defaults to localhost)
         ask.ai_family = PF_INET;
         rc = getaddrinfo(listen_ip, listen_port, &ask, &res);
      }
      else
      {  // UNIX domain socket: named pipe located in /tmp directory
         ask.ai_family = PF_UNIX;
         rc = EpgNetIo_GetLocalSocketAddr(SRV_CLNT_SOCK_PATH, &ask, &res);
      }
      if (rc == 0)
      {
         sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
         if (sock_fd == -1)
         {
            EpgNetIo_Logger(LOG_ERR, -1, "socket create failed: ", strerror(errno), NULL);
         }
      }
      else
         EpgNetIo_Logger(LOG_ERR, -1, "Invalid hostname or service/port: ", gai_strerror(rc), NULL);
   }

   if (sock_fd != -1)
   {
      // allow immediate reuse of the port (e.g. after server stop and restart)
      opt = 1;
      if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt)) == 0)
      {
         // make the socket non-blocking
         #ifndef WIN32
         if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) == 0)
         #else
         ulong ioArg = TRUE;
         if (ioctlsocket(sock_fd, FIONBIO, &ioArg) == 0)
         #endif
         {
            // bind the socket
            if (bind(sock_fd, res->ai_addr, res->ai_addrlen) == 0)
            {
               #ifdef linux
               // set socket permissions: r/w allowed to everyone
               if ( (is_tcp_ip == FALSE) &&
                    (chmod(SRV_CLNT_SOCK_PATH, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) != 0) )
                  EpgNetIo_Logger(LOG_WARNING, -1, "chmod failed for named socket: ", strerror(errno), NULL);
               #endif

               // enable listening for new connections
               if (listen(sock_fd, SRV_LISTEN_BACKLOG_LEN) == 0)
               {  // finished without errors
                  result = TRUE;
               }
               else
               {
                  EpgNetIo_Logger(LOG_ERR, -1, "socket listen failed: ", strerror(errno), NULL);
                  if (is_tcp_ip)
                     unlink(SRV_CLNT_SOCK_PATH);
               }
            }
            else
               EpgNetIo_Logger(LOG_ERR, -1, "socket bind failed: ", strerror(errno), NULL);
         }
         else
            EpgNetIo_Logger(LOG_ERR, -1, "failed to set socket non-blocking: ", strerror(errno), NULL);
      }
      else
         EpgNetIo_Logger(LOG_ERR, -1, "socket setsockopt(SOL_SOCKET=SO_REUSEADDR) failed: ", strerror(errno), NULL);
   }

   if (res != NULL)
      freeaddrinfo(res);

   if ((result == FALSE) && (sock_fd != -1))
   {
      closesocket(sock_fd);
      sock_fd = -1;
   }

   return sock_fd;
}

// ----------------------------------------------------------------------------
// Stop listening a socket
//
void EpgNetIo_StopListen( bool is_tcp_ip, int sock_fd )
{
   if (sock_fd != -1)
   {
      if (is_tcp_ip == FALSE)
         unlink(SRV_CLNT_SOCK_PATH);

      closesocket(sock_fd);
      sock_fd = -1;
   }
}

// ----------------------------------------------------------------------------
// Accept a new connection
//
int EpgNetIo_AcceptConnection( int listen_fd )
{
   struct hostent * hent;
   char  hname_buf[129];
   uint  length, maxLength;
   struct {  // allocate enough room for all possible types of socket address structs
      struct sockaddr  sa;
      char             padding[64];
   } peerAddr;
   int   sock_fd;
   bool  result = FALSE;

   maxLength = length = sizeof(peerAddr);
   sock_fd = accept(listen_fd, &peerAddr.sa, &length);
   if (sock_fd != -1)
   {
      if (length <= maxLength)
      {
         #ifndef WIN32
         if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) == 0)
         #else
         ulong ioArg = TRUE;
         if (ioctlsocket(sock_fd, FIONBIO, &ioArg) == 0)
         #endif
         {
            if (peerAddr.sa.sa_family == AF_INET)
            {
               hent = gethostbyaddr((void *) &peerAddr.sa, maxLength, AF_INET);
               if (hent != NULL)
               {
                  strncpy(hname_buf, hent->h_name, sizeof(hname_buf) -1);
                  hname_buf[sizeof(hname_buf) - 1] = 0;
               }
               else
                  sprintf(hname_buf, "%s, port %d", inet_ntoa(((struct sockaddr_in *) &peerAddr.sa)->sin_addr), ((struct sockaddr_in *) &peerAddr.sa)->sin_port);

               EpgNetIo_Logger(LOG_INFO, sock_fd, "new connection from ", hname_buf, NULL);
               result = TRUE;
            }
            #ifdef HAVE_GETADDRINFO
            else if (peerAddr.sa.sa_family == AF_INET6)
            {
               if (getnameinfo(&peerAddr.sa, length, hname_buf, sizeof(hname_buf) - 1, NULL, 0, 0) == 0)
               {  // address could be resolved to hostname
                  EpgNetIo_Logger(LOG_INFO, sock_fd, "new connection from ", hname_buf, NULL);
                  result = TRUE;
               }
               else if (getnameinfo(&peerAddr.sa, length, hname_buf, sizeof(hname_buf) - 1, NULL, 0,
                                    NI_NUMERICHOST | NI_NUMERICSERV) == 0)
               {  // resolver failed - but numeric conversion was successful
                  debug1("EpgNetIo-AcceptConnection: IPv6 resolver failed for %s", hname_buf);
                  EpgNetIo_Logger(LOG_INFO, sock_fd, "new connection from ", hname_buf, NULL);
                  result = TRUE;
               }
               else
               {  // neither name looup nor numeric name output succeeded -> fatal error
                  EpgNetIo_Logger(LOG_INFO, sock_fd, "new connection: failed to get IPv6 peer name or IP-addr: ", strerror(errno), NULL);
                  result = FALSE;
               }
            }
            #endif
            else if (peerAddr.sa.sa_family == AF_UNIX)
            {
               EpgNetIo_Logger(LOG_INFO, sock_fd, "new connection from localhost via named socket", NULL);
               result = TRUE;
            }
            else
            {  // neither INET nor named socket -> internal error
               sprintf(hname_buf, "%d", peerAddr.sa.sa_family);
               EpgNetIo_Logger(LOG_WARNING, -1, "new connection via unexpected protocol family ", hname_buf, NULL);
            }
         }
         else
         {  // fcntl failed: OS error (should never happen)
            EpgNetIo_Logger(LOG_WARNING, -1, "new connection: failed to set socket to non-blocking: ", strerror(errno), NULL);
         }
      }
      else
      {  // socket address buffer too small: internal error
         sprintf(hname_buf, "need %d, have %d", length, maxLength);
         EpgNetIo_Logger(LOG_WARNING, -1, "new connection: saddr buffer too small: ", hname_buf, NULL);
      }

      if (result == FALSE)
      {  // error -> drop the connection
         closesocket(sock_fd);
         sock_fd = -1;
      }
   }
   else
   {  // connect accept failed: remote host may already have closed again
      if (errno != EAGAIN)
         EpgNetIo_Logger(LOG_INFO, -1, "accept failed: ", strerror(errno), NULL);
   }

   return sock_fd;
}

// ----------------------------------------------------------------------------
// Attempt to connect to an already running server
//
bool EpgNetIo_CheckConnect( void )
{
   #ifndef WIN32
   EPGNETIO_MSG_HEADER msgCloseInd;
   struct sockaddr_un saddr;
   int  fd;
   bool result = FALSE;

   fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (fd != -1)
   {
      saddr.sun_family = AF_UNIX;
      strcpy(saddr.sun_path, SRV_CLNT_SOCK_PATH);
      if (connect(fd, (struct sockaddr *) &saddr, sizeof(saddr)) != -1)
      {
         msgCloseInd.len  = htons(sizeof(EPGNETIO_MSG_HEADER));
         msgCloseInd.type = MSG_TYPE_CLOSE_IND;
         if (write(fd, &msgCloseInd, sizeof(msgCloseInd)) == sizeof(msgCloseInd))
         {
            result = TRUE;
         }
      }
      closesocket(fd);
   }

   // if no server is listening, remove the socket from the file system
   if (result == FALSE)
      unlink(SRV_CLNT_SOCK_PATH);

   return result;

   #else  // WIN32: XXX TODO named sockets not supported - find another way to check
   return FALSE;
   #endif
}

// ----------------------------------------------------------------------------
// Open client connection
// - since the socket is made non-blocking, the result of the connect is not
//   yet available when the function finishes; the caller has to wait for
//   completion with select() and then query the socket error status
//
int EpgNetIo_ConnectToServer( bool use_tcp_ip, const char * pSrvHost, const char * pSrvPort, char ** ppErrorText )
{
   struct addrinfo    ask, *res;
   int  sock_fd;
   int  rc;

   rc = 0;
   res = NULL;
   sock_fd = -1;
   memset(&ask, 0, sizeof(ask));
   ask.ai_flags = 0;
   ask.ai_socktype = SOCK_STREAM;

   #ifdef PF_INET6
   if (use_tcp_ip)
   {  // try IP-v6: not supported everywhere yet, so errors must be silently ignored
      ask.ai_family = PF_INET6;
      rc = getaddrinfo(pSrvHost, pSrvPort, &ask, &res);
      if (rc == 0)
      {
         sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
         if (sock_fd == -1)
         {
            freeaddrinfo(res);
            res = NULL;
            //debug0("socket (ipv6)");
         }
      }
      else
         debug1("getaddrinfo (ipv6): %s", gai_strerror(rc));
   }
   #endif

   if (sock_fd == -1)
   {
      if (use_tcp_ip)
      {
         ask.ai_family = PF_INET;
         rc = getaddrinfo(pSrvHost, pSrvPort, &ask, &res);
      }
      else
      {
         ask.ai_family = PF_UNIX;
         rc = EpgNetIo_GetLocalSocketAddr(SRV_CLNT_SOCK_PATH, &ask, &res);
      }
      if (rc == 0)
      {
         sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
         if (sock_fd == -1)
         {
            debug1("socket (ipv4): %s", strerror(errno));
            EpgNetIo_SetErrorText(ppErrorText, "Cannot create network socket: ", strerror(errno), NULL);
         }
      }
      else
      {
         debug1("getaddrinfo (ipv4): %s", gai_strerror(rc));
         EpgNetIo_SetErrorText(ppErrorText, "Invalid hostname or service/port: ", gai_strerror(rc), NULL);
      }
   }

   if (sock_fd != -1)
   {
      #ifndef WIN32
      if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) == 0)
      #else
      ulong ioArg = TRUE;
      if (ioctlsocket(sock_fd, FIONBIO, &ioArg) == 0)
      #endif
      {
         // connect to the server socket
         if ( (connect(sock_fd, res->ai_addr, res->ai_addrlen) == 0)
              #ifndef WIN32
              || (errno == EINPROGRESS)
              #else
              || (WSAGetLastError() == WSAEWOULDBLOCK)
              #endif
              )
         {
            // all ok: result is in sock_fd
         }
         else
         {
            #ifndef WIN32
            debug1("connect: %s", strerror(errno));
            #else
            //debug1("connect: %d", WSAGetLastError());
            GetWindowsSocketErrorMsg("connect to daemon", WSAGetLastError());
            #endif
            if (use_tcp_ip)
               EpgNetIo_SetErrorText(ppErrorText, "Server not running or not reachable: connect via TCP/IP failed: ", strerror(errno), NULL);
            else
               EpgNetIo_SetErrorText(ppErrorText, "Server not running: connect via " SRV_CLNT_SOCK_PATH " failed: ", strerror(errno), NULL);
            closesocket(sock_fd);
            sock_fd = -1;
         }
      }
      else
      {
         debug1("fcntl (F_SETFL=O_NONBLOCK): %s", strerror(errno));
         EpgNetIo_SetErrorText(ppErrorText, "Failed to set socket non-blocking: ", strerror(errno), NULL);
         closesocket(sock_fd);
         sock_fd = -1;
      }
   }

   if (res != NULL)
      freeaddrinfo(res);

   return sock_fd;
}

// ----------------------------------------------------------------------------
// Check for the result of the connect syscall
// - UNIX: called when select() indicates writability
// - Win32: called when select() indicates writablility (successful connected)
//   or an exception (connect failed)
//
bool EpgNetIo_FinishConnect( int sock_fd, char ** ppErrorText )
{
   bool result = FALSE;
#ifndef WIN32
   int  sockerr, sockerrlen;

   sockerrlen = sizeof(sockerr);
   if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, (void *)&sockerr, &sockerrlen) == 0)
   {
      if (sockerr == 0)
      {  // success -> send the first message of the startup protocol to the server
         dprintf0("EpgNetIo-FinishConnect: connect succeeded\n");
         result = TRUE;
      }
      else
      {  // failed to establish a connection to the server
         debug1("EpgNetIo-FinishConnect: connect failed: %s", strerror(sockerr));
         EpgNetIo_SetErrorText(ppErrorText, "Connect failed: ", strerror(sockerr), NULL);
      }
   }
   else
   {
      debug1("EpgNetIo-FinishConnect: getsockopt: %s", strerror(errno));
      EpgNetIo_SetErrorText(ppErrorText, "Failed to query socket connect result: ", strerror(errno), NULL);
   }

#else  // WIN32

#if 0
   if (exception)
   {  // on Win32 connect errors are reported as exception event by select()
      EpgNetIo_SetErrorText(ppErrorText, "Connect failed", NULL);
      result = FALSE;
   }
   else
#endif
   {  // on Windows the socket becomes writable if - and only if - the connect succeeded
      dprintf0("EpgNetIo-FinishConnect: connect succeeded\n");
      result = TRUE;
   }
#endif

   return result;
}

#endif  // USE_DAEMON
