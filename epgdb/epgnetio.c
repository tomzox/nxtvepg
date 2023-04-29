/*
 *  Nextview EPG block network client & server
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
 *    This module transmits received blocks and acq stats per UNIX domain
 *    (i.e. pipes) or TCP/IP from a server process to connected Nextview
 *    clients. This allows multiple GUIs to use one TV card for data
 *    acquisition.
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
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifndef WIN32
#include <sys/signal.h>
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
#include "epgvbi/syserrmsg.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/epgnetio.h"

#if defined(linux) || defined(__NetBSD__) || defined(__FreeBSD__)
#define HAVE_GETADDRINFO
#endif

#ifndef WIN32
#define closesocket(FD)  close(FD)
#define sockErrno        errno
#define sockStrError(X)  strerror(X)
#else
#define sockErrno        WSAGetLastError()
#define sockStrError(X)  WinSocketStrError(X)
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

// settings & variables for WIN NT event log
#ifdef WIN32
#define EVLOG_APPNAME     "nxtvepg daemon"
#define EVLOG_REGKEY_NAME "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\" EVLOG_APPNAME
#define EVLOG_MSGFILE     "%SystemRoot%\\System32\\netmsg.dll"
#define EVLOG_ERRNO       3299
static const char * const pEvlogMsgFile = EVLOG_MSGFILE;
static HANDLE hEventSource = NULL;
#endif

// max size is much larger than any block will ever become
// but should be small enough so it can safely be malloc'ed during reload
#define EPGDBSAV_MAX_BLOCK_SIZE  30000

// ---------------------------------------------------------------------------
// Return socket system error string
//
#if defined(WIN32) && (DEBUG_SWITCH == ON)
static const char * WinSocketStrError( DWORD errCode )
{
   static char msg[300];

   // translate the error code into a human readable text
   if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errCode, LANG_USER_DEFAULT,
                     msg, strlen(msg) - 1, NULL) == 0)
   {
      if ((errCode < WSABASEERR) || (errCode > WSABASEERR + 2000))
      {
         debug1("WinSocketStrError: FormatMessage failed for errCode %ld", errCode);
         sprintf(msg, "System error #%ld", errCode);
      }
      else
         sprintf(msg, "WinSock error #%ld", errCode);
   }

   return msg;
}
#endif  // WIN32 && DEBUG_SWITCH==ON

#ifdef WIN32
// ----------------------------------------------------------------------------
// WIN NT syslog output
//
static void EpgNetIo_Win32SyslogOpen( void )
{
   DWORD  evMask;
   HKEY   hKey;

   if (hEventSource == NULL)
   {
      if (RegCreateKey(HKEY_LOCAL_MACHINE, EVLOG_REGKEY_NAME, &hKey) == 0)
      {
         RegSetValueEx(hKey, "EventMessageFile", 0, REG_EXPAND_SZ, (LPBYTE) pEvlogMsgFile, strlen(pEvlogMsgFile) + 1);

         evMask = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;

         RegSetValueEx(hKey, "TypesSupported", 0, REG_DWORD, (LPBYTE) &evMask, sizeof(evMask));
         RegCloseKey(hKey);
      }

      hEventSource = RegisterEventSource(NULL, EVLOG_APPNAME);
   }
}
#endif

// ----------------------------------------------------------------------------
// Append entry to logfile
//
void EpgNetIo_Logger( int level, int clnt_fd, int errCode, const char * pText, ... )
{
   #ifdef WIN32
   char   sysErrStr[160];
   uint    evType;
   #endif
   va_list argl;
   char timestamp[32], fdstr[20];
   const char *argv[10];
   uint argc, idx;
   sint fd;
   ssize_t wstat;
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
            wstat = write(fd, timestamp, strlen(timestamp));
            if (wstat < 0)
            {
               debug1("EpgNetIo-Logger: failed to write log: %s", strerror(errno));
            }
         }
         else
            debug2("EpgNetIo-Logger: failed to open '%s': %s", epgNetIoLogCf.pLogfileName, strerror(errno));
      }
      else
         fd = -1;

      if (epgNetIoLogCf.do_logtty && (level <= LOG_WARNING))
         fprintf(stderr, "nxtvepg: ");

      argc = 0;
      memset(argv, 0, sizeof(argv));
      // add pointer to file descriptor (for client requests) or pid (for general infos)
      if (clnt_fd != -1)
         sprintf(fdstr, "fd %d: ", clnt_fd);
      else
      {
         #ifndef WIN32
         sprintf(fdstr, "pid %d: ", (int)getpid());
         #else
         sprintf(fdstr, "pid %ld: ", GetCurrentProcessId());
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

      // add system error message
      if (errCode != 0)
      {
         #ifndef WIN32
         argv[argc++] = strerror(errCode);
         #else
         FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errCode, LANG_USER_DEFAULT, sysErrStr, sizeof(sysErrStr) - 1, NULL);
         argv[argc++] = sysErrStr;
         #endif
      }

      // print the strings to the file and/or stderr
      for (idx=0; idx < argc; idx++)
      {
         if (fd >= 0)
         {
            wstat = write(fd, argv[idx], strlen(argv[idx]));
            ifdebug1(wstat < 0, "EpgNetIo-Logger: failed to write log: %s", strerror(errno));
         }
         if (epgNetIoLogCf.do_logtty && (level <= LOG_WARNING))
            fprintf(stderr, "%s", argv[idx]);
      }

      // terminate the line with a newline character and close the file
      if (fd >= 0)
      {
         wstat = write(fd, "\n", 1);
         ifdebug1(wstat < 0, "EpgNetIo-Logger: failed to write log: %s", strerror(errno));
         close(fd);
      }
      if (epgNetIoLogCf.do_logtty && (level <= LOG_WARNING))
      {
         fprintf(stderr, "\n");
         fflush(stderr);
      }

      // syslog output
      if (level <= epgNetIoLogCf.sysloglev)
      {
         #ifndef WIN32
         switch (argc)
         {
            case 1: syslog(level, "%s", argv[0]); break;
            case 2: syslog(level, "%s%s", argv[0], argv[1]); break;
            case 3: syslog(level, "%s%s%s", argv[0], argv[1],argv[2]); break;
            case 4: syslog(level, "%s%s%s%s", argv[0], argv[1],argv[2],argv[3]); break;
         }
         #else
         if (hEventSource != NULL)
         {
            if (level <= LOG_ERR)
               evType = EVENTLOG_ERROR_TYPE;
            else if (level <= LOG_WARNING)
               evType = EVENTLOG_WARNING_TYPE;
            else
               evType = EVENTLOG_INFORMATION_TYPE;
            ReportEvent(hEventSource, evType, 0, EVLOG_ERRNO, NULL, 9, 0, argv, NULL);
         }
         #endif
      }
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
      epgNetIoLogCf.pLogfileName = (char*) xmalloc(strlen(pLogfileName) + 1);
      strcpy(epgNetIoLogCf.pLogfileName, pLogfileName);
      epgNetIoLogCf.fileloglev = ((fileloglev > 0) ? (fileloglev + LOG_ERR) : -1);
   }
   else
      epgNetIoLogCf.fileloglev = -1;

   if (sysloglev && !epgNetIoLogCf.sysloglev)
   {
      #ifndef WIN32
      openlog("nxtvepg", LOG_PID, LOG_DAEMON);
      #else
      EpgNetIo_Win32SyslogOpen();
      #endif
   }
   else if (!sysloglev && epgNetIoLogCf.sysloglev)
   {
      #ifndef WIN32
      closelog();
      #endif
   }

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
   int      len;
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
         len = send(pIO->sock_fd, ((char *)pIO->pWriteBuf) + pIO->writeGap + pIO->writeOff - sizeof(EPGNETIO_MSG_HEADER), pIO->writeLen - pIO->writeOff, 0);
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
         debug2("EpgNetIo-HandleIO: write error on fd %d: %s", pIO->sock_fd, sockStrError(sockErrno));
         result = FALSE;
      }
      #ifndef WIN32
      else if (errno == EAGAIN)
      #else
      else if (WSAGetLastError() == WSAEWOULDBLOCK)
      #endif
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
                     pIO->pReadBuf = (char*) xmalloc(pIO->readLen - sizeof(EPGNETIO_MSG_HEADER));
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
            debug3("EpgNetIo-HandleIO: read error on fd %d: len=%d, %s", pIO->sock_fd, len, sockStrError(sockErrno));
            result = FALSE;
         }
         #ifndef WIN32
         else if (errno == EAGAIN)
         #else
         else if (WSAGetLastError() == WSAEWOULDBLOCK)
         #endif
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
#ifdef WIN32
      shutdown(pIO->sock_fd, SD_SEND);
#endif
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

   dprintf2("EpgNetIo-WriteMsg: msg type %d, len %d\n", type, (int)sizeof(EPGNETIO_MSG_HEADER) + msgLen);

   pIO->pWriteBuf    = pMsg;
   pIO->freeWriteBuf = freeBuf;
   pIO->writeLen     = sizeof(EPGNETIO_MSG_HEADER) + msgLen;
   pIO->writeGap     = 0;
   pIO->writeOff     = 0;

   // message header: length is coded in network byte order (i.e. big endian)
   pIO->writeHeader.len  = htons(pIO->writeLen);
   pIO->writeHeader.type = type;
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

static const char * gai_strerror( int errCode )
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
   res = (struct addrinfo*) malloc(sizeof(struct addrinfo));
   *ppResult = res;

   memset(res, 0, sizeof(*res));
   res->ai_socktype  = pInParams->ai_socktype;
   res->ai_family    = pInParams->ai_family;
   res->ai_protocol  = pInParams->ai_protocol;

   saddr = (struct sockaddr_un*) malloc(sizeof(struct sockaddr_un));
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
            EpgNetIo_Logger(LOG_ERR, -1, sockErrno, "socket create failed: ", NULL);
         }
      }
      else
         EpgNetIo_Logger(LOG_ERR, -1, 0, "Invalid hostname or service/port: ", gai_strerror(rc), NULL);
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
                  EpgNetIo_Logger(LOG_WARNING, -1, errno, "chmod failed for named socket: ", NULL);
               #endif

               // enable listening for new connections
               if (listen(sock_fd, SRV_LISTEN_BACKLOG_LEN) == 0)
               {  // finished without errors
                  result = TRUE;
               }
               else
               {
                  EpgNetIo_Logger(LOG_ERR, -1, sockErrno, "socket listen failed: ", NULL);
                  if (is_tcp_ip)
                     unlink(SRV_CLNT_SOCK_PATH);
               }
            }
            else
               EpgNetIo_Logger(LOG_ERR, -1, sockErrno, "socket bind failed: ", NULL);
         }
         else
            EpgNetIo_Logger(LOG_ERR, -1, sockErrno, "failed to set socket non-blocking: ", NULL);
      }
      else
         EpgNetIo_Logger(LOG_ERR, -1, sockErrno, "socket setsockopt(SOL_SOCKET=SO_REUSEADDR) failed: ", NULL);
   }

   if (res != NULL)
   {
      if ((res->ai_family == PF_UNIX) && (res->ai_addr != NULL))
      {  // free manually allocated memory for UNIX domain socket
         free(res->ai_addr);
         res->ai_addr = NULL;
      }
      freeaddrinfo(res);
   }

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
   #ifndef WIN32
   struct hostent * hent;
   socklen_t length;
   #else
   int   length;
   #endif
   size_t  maxLength;
   char  hname_buf[129];
   struct {  // allocate enough room for all possible types of socket address structs
      struct sockaddr  sa;
      char             padding[64];
   } peerAddr;
   int   sock_fd;
   bool  result = FALSE;

   length = maxLength = sizeof(peerAddr);
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
               #ifndef WIN32  // XXX WIN32 is useless due to 5 secs timeout on 127.0.0.1
               hent = gethostbyaddr((void *) &peerAddr.sa, maxLength, AF_INET);
               if (hent != NULL)
               {
                  strncpy(hname_buf, hent->h_name, sizeof(hname_buf) -1);
                  hname_buf[sizeof(hname_buf) - 1] = 0;
               }
               else
               #endif
                  sprintf(hname_buf, "%s, port %d", inet_ntoa(((struct sockaddr_in *) &peerAddr.sa)->sin_addr), ((struct sockaddr_in *) &peerAddr.sa)->sin_port);

               EpgNetIo_Logger(LOG_INFO, sock_fd, 0, "new connection from ", hname_buf, NULL);
               result = TRUE;
            }
            #ifdef HAVE_GETADDRINFO
            else if (peerAddr.sa.sa_family == AF_INET6)
            {
               if (getnameinfo(&peerAddr.sa, length, hname_buf, sizeof(hname_buf) - 1, NULL, 0, 0) == 0)
               {  // address could be resolved to hostname
                  EpgNetIo_Logger(LOG_INFO, sock_fd, 0, "new connection from ", hname_buf, NULL);
                  result = TRUE;
               }
               else if (getnameinfo(&peerAddr.sa, length, hname_buf, sizeof(hname_buf) - 1, NULL, 0,
                                    NI_NUMERICHOST | NI_NUMERICSERV) == 0)
               {  // resolver failed - but numeric conversion was successful
                  debug1("EpgNetIo-AcceptConnection: IPv6 resolver failed for %s", hname_buf);
                  EpgNetIo_Logger(LOG_INFO, sock_fd, 0, "new connection from ", hname_buf, NULL);
                  result = TRUE;
               }
               else
               {  // neither name looup nor numeric name output succeeded -> fatal error
                  EpgNetIo_Logger(LOG_INFO, sock_fd, sockErrno, "new connection: failed to get IPv6 peer name or IP-addr: ", NULL);
                  result = FALSE;
               }
            }
            #endif
            else if (peerAddr.sa.sa_family == AF_UNIX)
            {
               EpgNetIo_Logger(LOG_INFO, sock_fd, 0, "new connection from localhost via named socket", NULL);
               result = TRUE;
            }
            else
            {  // neither INET nor named socket -> internal error
               sprintf(hname_buf, "%d", peerAddr.sa.sa_family);
               EpgNetIo_Logger(LOG_WARNING, -1, 0, "new connection via unexpected protocol family ", hname_buf, NULL);
            }
         }
         else
         {  // fcntl failed: OS error (should never happen)
            EpgNetIo_Logger(LOG_WARNING, -1, sockErrno, "new connection: failed to set socket to non-blocking: ", NULL);
         }
      }
      else
      {  // socket address buffer too small: internal error
         sprintf(hname_buf, "need %d, have %d", (int)length, (int)maxLength);
         EpgNetIo_Logger(LOG_WARNING, -1, 0, "new connection: saddr buffer too small: ", hname_buf, NULL);
      }

      if (result == FALSE)
      {  // error -> drop the connection
         closesocket(sock_fd);
         sock_fd = -1;
      }
   }
   else
   {  // connect accept failed: remote host may already have closed again
      #ifndef WIN32
      if (errno == EAGAIN)
      #else
      if (WSAGetLastError() == WSAEWOULDBLOCK)
      #endif
         EpgNetIo_Logger(LOG_INFO, -1, sockErrno, "accept failed: ", NULL);
   }

   return sock_fd;
}

// ----------------------------------------------------------------------------
// Check if the given host name refers to the local host
//
bool EpgNetIo_IsLocalHost( char * pHostname )
{
   struct hostent  * pHostEntry;
   struct in_addr  * pInAddr;
   char local_name[100];
   bool result;

   if (gethostname(local_name, sizeof(local_name)-1) == 0)
   {
      if (strcmp(pHostname, local_name) != 0)
      {
         pHostEntry = gethostbyname(pHostname);
         if (pHostEntry != NULL)
         {
            if (strcmp(pHostEntry->h_name, local_name) != 0)
            {
               pInAddr = (struct in_addr *)(pHostEntry->h_addr_list[0]);
               if ( (pHostEntry->h_addrtype == AF_INET) &&
                    (ntohl(pInAddr->s_addr) == INADDR_LOOPBACK) )
               {
                  result = TRUE;
               }
               else
                  result = FALSE;
            }
            else
               result = TRUE;
         }
         else
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
            debug1("socket (ipv4): %s", sockStrError(sockErrno));
            SystemErrorMessage_Set(ppErrorText, sockErrno, "Cannot create network socket: ", NULL);
         }
      }
      else
      {
         debug1("getaddrinfo (ipv4): %s", gai_strerror(rc));
         SystemErrorMessage_Set(ppErrorText, 0, "Invalid hostname or service/port: ", gai_strerror(rc), NULL);
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
            debug1("connect: %s", sockStrError(sockErrno));
            if (use_tcp_ip)
               SystemErrorMessage_Set(ppErrorText, sockErrno, "Server not running or not reachable: connect via TCP/IP failed: ", NULL);
            else
               SystemErrorMessage_Set(ppErrorText, sockErrno, "Server not running: connect via " SRV_CLNT_SOCK_PATH " failed: ", NULL);
            closesocket(sock_fd);
            sock_fd = -1;
         }
      }
      else
      {
         debug1("fcntl (F_SETFL=O_NONBLOCK): %s", sockStrError(sockErrno));
         SystemErrorMessage_Set(ppErrorText, sockErrno, "Failed to set socket non-blocking: ", NULL);
         closesocket(sock_fd);
         sock_fd = -1;
      }
   }

   if (res != NULL)
   {
      if ((res->ai_family == PF_UNIX) && (res->ai_addr != NULL))
      {  // free manually allocated memory for UNIX domain socket
         free(res->ai_addr);
         res->ai_addr = NULL;
      }
      freeaddrinfo(res);
   }

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
   int  sockerr;
   socklen_t sockerrlen = sizeof(sockerr);

   if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, (void *)&sockerr, &sockerrlen) == 0)
   {
      if (sockerr == 0)
      {  // success -> send the first message of the startup protocol to the server
         dprintf0("EpgNetIo-FinishConnect: connect succeeded\n");
         result = TRUE;
      }
      else
      {  // failed to establish a connection to the server
         debug1("EpgNetIo-FinishConnect: connect failed: %s", sockStrError(sockerr));
         SystemErrorMessage_Set(ppErrorText, sockerr, "Connect failed: ", NULL);
      }
   }
   else
   {
      debug1("EpgNetIo-FinishConnect: getsockopt: %s", sockStrError(sockErrno));
      SystemErrorMessage_Set(ppErrorText, sockErrno, "Failed to query socket connect result: ", NULL);
   }

#else  // WIN32
   // on Windows the socket becomes writable if - and only if - the connect succeeded
   dprintf0("EpgNetIo-FinishConnect: connect succeeded\n");
   result = TRUE;
#endif

   return result;
}

// ----------------------------------------------------------------------------
// Query a mille-second -resolution time value
// - required because WIN32 doesn't support gettimeofday()
//
void EpgNetIo_GetTimeOfDay( struct timeval * pTime )
{
#ifdef WIN32
   uint msecs = GetCurrentTime();
   pTime->tv_sec = msecs / 1000;
   pTime->tv_usec = (msecs % 1000) * 1000;
#else
   gettimeofday(pTime, NULL);
#endif
}

// ----------------------------------------------------------------------------
// Substract time spent waiting in select from a given max. timeout struct
// - This function is intended for functions which call select() repeatedly
//   but need to implement an overall timeout.  After each select() call the
//   time already spent in waiting has to be substracted from the timeout.
// - Note we don't use the Linux select(2) feature to return the time not
//   slept in the timeout struct, because that's not portable.)
//
void EpgNetIo_UpdateTimeout( struct timeval * pStartTime, struct timeval * pTimeout )
{
   struct timeval delta;
   struct timeval tv_stop;
   int            errno_saved;

   errno_saved = errno;
   EpgNetIo_GetTimeOfDay(&tv_stop);
   errno = errno_saved;

   // first calculate difference between start and current time
   delta.tv_sec = tv_stop.tv_sec - pStartTime->tv_sec;
   if (tv_stop.tv_usec < pStartTime->tv_usec)
   {
      delta.tv_usec = 1000000 + tv_stop.tv_usec - pStartTime->tv_usec;
      delta.tv_sec += 1;
   }
   else
      delta.tv_usec = tv_stop.tv_usec - pStartTime->tv_usec;

   assert((delta.tv_sec >= 0) && (delta.tv_usec >= 0));

   // substract delta from the given max. timeout
   pTimeout->tv_sec -= delta.tv_sec;
   if (pTimeout->tv_usec < delta.tv_usec)
   {
      pTimeout->tv_usec = 1000000 + pTimeout->tv_usec - delta.tv_usec;
      pTimeout->tv_sec -= 1;
   }
   else
      pTimeout->tv_usec -= delta.tv_usec;

   // check if timeout was underrun -> set rest timeout to zero
   if ( (pTimeout->tv_sec < 0) || (pTimeout->tv_usec < 0) )
   {
      pTimeout->tv_sec  = 0;
      pTimeout->tv_usec = 0;
   }
}

// ----------------------------------------------------------------------------
// Initialize the module
//
bool EpgNetIo_Init( char ** ppErrorText )
{
#ifndef WIN32
   struct sigaction  act;

   // setup signal handler
   memset(&act, 0, sizeof(act));
   sigemptyset(&act.sa_mask);
   act.sa_handler = SIG_IGN;
   sigaction(SIGPIPE, &act, NULL);
   return TRUE;

#else // WIN32
   WSADATA stWSAData;
   bool   result;

   result = (WSAStartup(0x202, &stWSAData) == 0);
   if (result == FALSE)
   {
      if (ppErrorText == NULL)
         EpgNetIo_Logger(LOG_ERR, -1, sockErrno, "winsock2 DLL init failed: ", NULL);
      else
         SystemErrorMessage_Set(ppErrorText, sockErrno, "Failed to initialize winsock2 DLL: ", NULL);
   }

   return result;
#endif
}

// ----------------------------------------------------------------------------
// Free module resources
//
void EpgNetIo_Destroy( void )
{
#ifdef WIN32
   WSACleanup();
#endif
}

#endif  // USE_DAEMON
