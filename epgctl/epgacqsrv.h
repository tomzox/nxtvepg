/*
 *  Nextview EPG network acquisition server
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
 *  $Id: epgacqsrv.h,v 1.3 2002/02/13 21:03:41 tom Exp tom $
 */

#ifndef __EPGACQSRV_H
#define __EPGACQSRV_H

#include "epgdb/epgnetio.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmerge.h"
#include "epgui/uictrl.h"
#include "epgctl/epgctxctl.h"
#include "epgctl/epgacqctl.h"

#ifdef WIN32
#include <winsock2.h>
#endif

// ---------------------------------------------------------------------------
// Structure for acq stats reports
//
#define STATS_REQ_BITS_HIST         0x01   // full acq stats
#define STATS_REQ_BITS_TSC_REQ      0x02   // timescale for req. CNIs only
#define STATS_REQ_BITS_TSC_ALL      0x04   // timescale for all CNIs
#define STATS_REQ_BITS_VPS_PDC_REQ  0x08   // forward VPS/PDC CNI and PIL
#define STATS_REQ_BITS_VPS_PDC_UPD  0x10   // forward the next new VPS/PDC reading

typedef enum
{
   EPGDB_STATS_UPD_TYPE_MINIMAL,
   EPGDB_STATS_UPD_TYPE_INITIAL,
   EPGDB_STATS_UPD_TYPE_UPDATE,
} EPGDB_STATS_UPD_TYPE;

typedef struct MSG_STRUCT_STATS_IND_STRUCT
{
   void                * pNext;

   EPGDB_STATS_UPD_TYPE  type;
   bool                  aiFollows;
   EPGACQ_DESCR          descr;

   union
   {
      struct
      {
         EPGDB_BLOCK_COUNT    count[2];
         EPGDB_ACQ_VPS_PDC    vpsPdc;
         time_t               lastAiTime;
      } minimal;

      struct
      {
         EPGDB_STATS          stats;
      } initial;

      struct
      {
         EPGDB_BLOCK_COUNT    count[2];
         EPGDB_ACQ_AI_STATS   ai;
         EPGDB_ACQ_TTX_STATS  ttx;
         EPGDB_ACQ_VPS_PDC    vpsPdc;
         EPGDB_HIST           hist;
         uint                 histIdx;
         uint                 nowNextMaxAcqRepCount;
      } update;
   } u;
} MSG_STRUCT_STATS_IND;

// ----------------------------------------------------------------------------
// Declaration of messages
//
typedef struct
{
   uchar   magic[MAGIC_STR_LEN];    // magic string to identify the requested service
   uchar   reserved[64];            // reserved for future additions
} MSG_STRUCT_CONNECT_REQ;

typedef struct
{
   uchar   magic[MAGIC_STR_LEN];    // magic string to identify the provided service
   ushort  endianMagic;             // distinguish big/little endian server
   ulong   blockCompatVersion;      // version of EPG database block format
   ulong   protocolCompatVersion;   // protocol version
   ulong   swVersion;               // software version (informative only)
   uchar   reserved[64];            // reserved for future additions
} MSG_STRUCT_CONNECT_CNF;

typedef struct
{
   uint    cniCount;                // number of valid CNIs in list (0 allowed)
   uint    provCnis[MAX_MERGED_DB_COUNT];    // list of requested provider CNIs
   time_t  dumpStartTimes[MAX_MERGED_DB_COUNT];  // client-side time of last db update
   uint    statsReqBits;            // extent of acq stats requested by the GUI
} MSG_STRUCT_FORWARD_REQ;

typedef struct
{
   uint    cniCount;                // copy of REQ msg (to identify the reply): number of CNIs
   uint    provCnis[MAX_MERGED_DB_COUNT];  // copy of REQ msg: list of requested provider CNIs
} MSG_STRUCT_FORWARD_CNF;

typedef struct
{
   uint    cni;                     // CNI of the db acq is currently working for
} MSG_STRUCT_FORWARD_IND;

typedef struct
{
   uint    cni;                     // CNI of the dumped db
} MSG_STRUCT_DUMP_IND;

typedef struct
{
   uint    statsReqBits;            // extent of acq stats (handled by upper layers only)
} MSG_STRUCT_STATS_REQ;

typedef struct
{
   EPGDB_ACQ_VPS_PDC  vpsPdc;
} MSG_STRUCT_VPS_PDC_IND;

typedef union
{
   MSG_STRUCT_CONNECT_REQ   con_req;
   MSG_STRUCT_CONNECT_CNF   con_cnf;
   MSG_STRUCT_FORWARD_REQ   fwd_req;
   MSG_STRUCT_FORWARD_CNF   fwd_cnf;
   MSG_STRUCT_FORWARD_IND   fwd_ind;
   MSG_STRUCT_DUMP_IND      dump_ind;
   MSG_STRUCT_STATS_REQ     stats_req;
   MSG_STRUCT_STATS_IND     stats_ind;
   MSG_STRUCT_VPS_PDC_IND   vps_pdc_ind;
} EPGDBSRV_MSG_BODY;

// ----------------------------------------------------------------------------
// Declaration of the service interface functions
//
void EpgAcqServer_Init( bool have_tty );
bool EpgAcqServer_Listen( void );
void EpgAcqServer_SetMaxConn( uint max_conn );
void EpgAcqServer_SetAddress( bool do_tcp_ip, const char * pIpStr, const char * pPort );
void EpgAcqServer_SetProvider( uint cni );
void EpgAcqServer_SetVpsPdc( bool change );
void EpgAcqServer_Destroy( void );
uint EpgAcqServer_GetFdSet( fd_set * rd, fd_set * wr );
void EpgAcqServer_HandleSockets( fd_set * rd, fd_set * wr );
void EpgAcqServer_AddBlock( EPGDB_CONTEXT * dbc, EPGDB_BLOCK * pNewBlock );


#endif  // __EPGACQSRV_H
