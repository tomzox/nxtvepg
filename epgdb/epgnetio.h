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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgnetio.h,v 1.30 2009/03/28 21:20:49 tom Exp $
 */

#ifndef __EPGNETIO_H
#define __EPGNETIO_H

#include "epgdb/epgdbmerge.h"

#define PROTOCOL_COMPAT          EPG_VERSION_TO_INT(2,8,0xA1)
#define PROTOCOL_ENDIAN_MAGIC    0xAA55
#define PROTOCOL_WRONG_ENDIAN    (((PROTOCOL_ENDIAN_MAGIC>>8)&0xFF)|((PROTOCOL_ENDIAN_MAGIC&0xFF)<<8))


#ifdef WIN32
// declare missing stuff to compile under windows
enum
{
   LOG_ERR,
   LOG_WARNING,
   LOG_NOTICE,
   LOG_INFO,
};
#else
#include <sys/syslog.h>
#endif

#ifdef WIN32
struct timeval;         // requires winsock.h
#endif

// ----------------------------------------------------------------------------
// Declaration of message IDs and the common header struct
// - the actual message structs are declared in the upper layers

typedef enum
{
   MSG_TYPE_CONNECT_REQ,
   MSG_TYPE_CONNECT_CNF,
   MSG_TYPE_CONQUERY_CNF,
   MSG_TYPE_FORWARD_REQ,
   MSG_TYPE_FORWARD_CNF,
   MSG_TYPE_FORWARD_IND,
   MSG_TYPE_STATS_REQ,
   MSG_TYPE_STATS_IND,
   MSG_TYPE_TSC_IND,
   MSG_TYPE_VPS_PDC_IND,
   MSG_TYPE_DB_UPD_IND,
   MSG_TYPE_CLOSE_IND
} EPGNETIO_MSG_TYPE;

typedef struct
{
   uint16_t  len;
   uint8_t   type;
   uint8_t   reserved;
} EPGNETIO_MSG_HEADER;

// ----------------------------------------------------------------------------
// Declaration of the IO state struct

typedef struct
{
   int              sock_fd;        // socket file handle or -1 if closed
   time_t           lastIoTime;     // timestamp of last i/o (for timeouts)

   uint             writeLen;       // number of bytes in write buffer, including header
   uint             writeOff;       // number of already written bytes, including header
   EPGNETIO_MSG_HEADER writeHeader; // header to be written
   uint             writeGap;       // offset to start of data in output buffer
   void             * pWriteBuf;    // data to be written
   bool             freeWriteBuf;   // TRUE if the buffer shall be freed by the I/O handler

   bool             waitRead;       // TRUE while length of incoming msg is not completely read
   ushort           readLen;        // length of incoming message (including itself)
   uint             readOff;        // number of already read bytes
   char             *pReadBuf;      // msg buffer; allocated after length is read
   EPGNETIO_MSG_HEADER readHeader;  // received message header
} EPGNETIO_STATE;

// ----------------------------------------------------------------------------
// Declaration of the service interface functions
// - note that this is only the interface from the upper layers (main loop,
//   acq control) to the client/server level. The opposite direction is
//   implemented via the callbacks defined above. The client additionally
//   uses an I/O file handler (to block on read/write) which is invoked by
//   the main event handler.
//
void EpgNetIo_SetLogging( int fileloglev, int sysloglev, const char * pLogfileName );
void EpgNetIo_Logger( int level, int clnt_fd, int errCode, const char * pText, ... );

bool EpgNetIo_IsIdle( EPGNETIO_STATE * pIO );
bool EpgNetIo_CheckTimeout( EPGNETIO_STATE * pIO, time_t now );
bool EpgNetIo_HandleIO( EPGNETIO_STATE * pIO, bool * pBlocked, bool closeOnZeroRead );
void EpgNetIo_CloseIO( EPGNETIO_STATE * pIO );
void EpgNetIo_WriteMsg( EPGNETIO_STATE * pIO, EPGNETIO_MSG_TYPE type, uint msgLen, void * pMsg, bool freeBuf );

int  EpgNetIo_ListenSocket( bool is_tcp_ip, const char * listen_ip, const char * listen_port );
void EpgNetIo_StopListen( bool is_tcp_ip, int sock_fd );
int  EpgNetIo_AcceptConnection( int listen_fd );
bool EpgNetIo_IsLocalHost( char * pHostname );
bool EpgNetIo_CheckConnect( void );
int  EpgNetIo_ConnectToServer( bool use_tcp_ip, const char * pSrvHost, const char * pSrvPort, char ** ppErrorText );
bool EpgNetIo_FinishConnect( int sock_fd, char ** ppErrorText );
void EpgNetIo_GetTimeOfDay( struct timeval * pStartTime );
void EpgNetIo_UpdateTimeout( struct timeval * pStartTime, struct timeval * pTimeout );
bool EpgNetIo_Init( char ** ppErrorText );
void EpgNetIo_Destroy( void );


#endif  // __EPGNETIO_H
