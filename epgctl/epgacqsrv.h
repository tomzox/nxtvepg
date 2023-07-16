/*
 *  Nextview EPG network acquisition server
 *
 *  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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
 *  Description: see C source file.
 */

#ifndef __EPGACQSRV_H
#define __EPGACQSRV_H

#include "epgdb/epgnetio.h"
#include "epgdb/epgdbmerge.h"
#include "epgctl/epgctxctl.h"
#include "epgctl/epgacqctl.h"

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
// Connect request
//
#define MAGIC_STR      "NXTVEPG-NETACQ"
#define MAGIC_STR_LEN  14

// ---------------------------------------------------------------------------
// Structure for acq stats reports
//
#define STATS_REQ_BITS_LOW_FREQ     0x01   // full acq stats, 20 sec interval
#define STATS_REQ_BITS_HIGH_FREQ    0x02   // full acq stats, 2 sec interval
#define STATS_REQ_BITS_VPS_PDC_REQ  0x08   // forward VPS/PDC CNI and PIL
#define STATS_REQ_BITS_VPS_PDC_UPD  0x10   // forward the next new VPS/PDC reading

#define STATS_REQ_MINIMAL(X) (((X) & (STATS_REQ_BITS_LOW_FREQ | STATS_REQ_BITS_HIGH_FREQ)) == 0)

#define STATS_IND_INVALID_VPS_PDC   0xFF

typedef enum
{
   EPGDB_STATS_UPD_TYPE_MINIMAL,
   EPGDB_STATS_UPD_TYPE_INITIAL,
} EPGDB_STATS_UPD_TYPE;

typedef struct MSG_STRUCT_STATS_IND_STRUCT
{
   union
   {
      void             * pNext;
#if defined (USE_32BIT_COMPAT)
      uint8_t            resv_align0[8];   // 64-bit pointer size
#endif
   } p;

   EPGDB_STATS_UPD_TYPE  type;
   uint8_t               resv_align1[3];
   EPGACQ_DESCR          descr;

   union
   {
      struct
      {
         EPG_ACQ_VPS_PDC      vpsPdc;
         time32_t             lastAiTime;
         time32_t             resv_align2; // 64-bit alignment
      } minimal;

      struct
      {
         EPG_ACQ_STATS        stats;
         EPG_ACQ_VPS_PDC      vpsPdc;
         time32_t             resv_align3; // 64-bit alignment
      } initial;
   } u;
} MSG_STRUCT_STATS_IND;

// ----------------------------------------------------------------------------
// Declaration of messages
//
typedef struct
{
   uchar     magic[MAGIC_STR_LEN];    // magic string to identify the requested service
   uint16_t  endianMagic;             // distinguish big/little endian client
   uint8_t   reserved[62];            // reserved for future additions
} MSG_STRUCT_CONNECT_REQ;

typedef struct
{
   uchar     magic[MAGIC_STR_LEN];    // magic string to identify the provided service
   uint16_t  endianMagic;             // distinguish big/little endian server
   uint32_t  protocolCompatVersion;   // protocol version
   uint32_t  swVersion;               // software version (informative only)
   int32_t   daemon_pid;
   uint8_t   use_32_bit_compat;       // compile switch USE_32BIT_COMPAT
   uint8_t   reserved[60-1];          // reserved for future additions
} MSG_STRUCT_CONNECT_CNF;

typedef struct
{
   uint32_t  statsReqBits;            // extent of acq stats (handled by upper layers only)
} MSG_STRUCT_STATS_REQ;

typedef struct
{
   EPG_ACQ_VPS_PDC  vpsPdc;
} MSG_STRUCT_VPS_PDC_IND;

typedef struct
{
   uint32_t  mtime;
} MSG_STRUCT_DB_UPD_IND;

typedef union
{
   MSG_STRUCT_CONNECT_REQ   con_req;
   MSG_STRUCT_CONNECT_CNF   con_cnf;
   MSG_STRUCT_STATS_REQ     stats_req;
   MSG_STRUCT_STATS_IND     stats_ind;
   MSG_STRUCT_VPS_PDC_IND   vps_pdc_ind;
   MSG_STRUCT_DB_UPD_IND    db_upd_ind;
} EPGDBSRV_MSG_BODY;

// ----------------------------------------------------------------------------
// Declaration of the service interface functions
//
void EpgAcqServer_Init( bool have_tty );
bool EpgAcqServer_Listen( void );
void EpgAcqServer_SetMaxConn( uint max_conn );
void EpgAcqServer_SetAddress( bool do_tcp_ip, const char * pIpStr, const char * pPort );
void EpgAcqServer_SetVpsPdc( bool change );
void EpgAcqServer_TriggerStats( void );
void EpgAcqServer_TriggerDbUpdate( time_t mtime );
void EpgAcqServer_Destroy( void );
sint EpgAcqServer_GetFdSet( fd_set * rd, fd_set * wr );
void EpgAcqServer_HandleSockets( fd_set * rd, fd_set * wr );
void EpgAcqServer_AddBlock( EPGDB_CONTEXT * dbc, EPGDB_PI_BLOCK * pNewBlock );


#endif  // __EPGACQSRV_H
