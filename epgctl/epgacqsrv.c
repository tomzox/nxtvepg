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
 *  Description:
 *
 *    This module allows nxtvepg client to connect and request forwarding
 *    of incoming EPG blocks and retrieve current acq state information.
 *
 *  Author:
 *          Tom Zoerner
 *
 *  $Id: epgacqsrv.c,v 1.24 2020/06/17 08:19:39 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#ifdef USE_DAEMON

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef WIN32
#include <sys/types.h>
#endif

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgctl/epgversion.h"
#include "epgvbi/ttxdecode.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgswap.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgnetio.h"
#include "epgui/uictrl.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgacqsrv.h"


// ----------------------------------------------------------------------------
// Declaration of types of internal state variables

typedef enum
{
   SRV_STATE_WAIT_CON_REQ,
   SRV_STATE_WAIT_FWD_REQ,
   SRV_STATE_DUMP_REQUESTED,
   SRV_STATE_DUMP_ACQ,
   SRV_STATE_FORWARD,
} SRV_REQ_STATE;

typedef enum
{
   SRV_FWD_STATS_DONE,
   SRV_FWD_STATS_INITIAL,
   SRV_FWD_STATS_UPD,
   SRV_FWD_STATS_UPD_NO_AI,
} SRV_STATS_STATE;

// this struct holds client-specific state and parameters
typedef struct REQUEST_struct
{
   struct REQUEST_struct * pNext;

   SRV_REQ_STATE   state;
   EPGNETIO_STATE  io;
   bool            isLocalCon;
   bool            endianSwap;

   EPGNETIO_DUMP   dump;
   uint            dumpProvIdx;
   uint            dumpCnis[MAX_MERGED_DB_COUNT];
   uint            dumpedLastCni;
   time32_t        dumpStartTimes[MAX_MERGED_DB_COUNT];

   SRV_STATS_STATE doTtxStats;
   bool            doVpsPdc;
   time32_t        sendDbUpdTime;
   bool            enableAllBlocks;
   uint            cniCount;
   uint            provCnis[MAX_MERGED_DB_COUNT];
   uint            statsReqBits;

   EPGDB_CONTEXT * pDbContext;
   EPGDB_QUEUE     outQueue;
   EPGDB_PI_TSC    tscQueue;

   EPGDBSRV_MSG_BODY  msgBuf;

} EPGDBSRV_STATE;

// this struct holds the global state of the module
typedef struct
{
   char         *listen_ip;
   char         *listen_port;
   bool         do_tcp_ip;
   int          tcp_ip_fd;
   int          pipe_fd;
   uint         max_conn;
   uint         conCount;
   uint         lastFwdCni;
   bool         aiVersionChange;

} SRV_CTL_STRUCT;

// convenience macro
#define IS_AI_OI_BLOCK(X)  (((X)->type == BLOCK_TYPE_AI) || \
                            (((X)->type == BLOCK_TYPE_OI) && ((X)->blk.oi.block_no == 0)))

#define SRV_REPLY_TIMEOUT       60
#define SRV_STALLED_STATS_INTV  15

// ----------------------------------------------------------------------------
// Local variables
//
static bool               isDbServer = FALSE;
static EPGDBSRV_STATE   * pReqChain = NULL;
static SRV_CTL_STRUCT     srvState;

// ----------------------------------------------------------------------------
// Merge CNIs from all clients and send them to the acq-ctl layer
// - called after receiving the FWD message from a client
// - acqctl only uses the CNI list in FOLLOW-UI mode
//
static void EpgAcqServer_MergeCniLists( void )
{
   EPGDBSRV_STATE  * pReq;
   uint reqIdx, chkIdx;
   uint reqCni;
   uint cniCount;
   uint provCnis[MAX_MERGED_DB_COUNT];

   cniCount = 0;
   pReq = pReqChain;
   // loop over all client request structures
   while (pReq != NULL)
   {
      // loop over all CNIs requested by this client
      for (reqIdx=0; reqIdx < pReq->cniCount; reqIdx++)
      {
         reqCni = pReq->provCnis[reqIdx];
         // check if the CNI is already in the list
         for (chkIdx=0; chkIdx < cniCount; chkIdx++)
            if (provCnis[chkIdx] == reqCni)
               break;
         if (chkIdx >= cniCount)
         {  // new CNI -> append it to the merged list
            provCnis[cniCount] = reqCni;
            cniCount += 1;
         }
      }
      pReq = pReq->pNext;
   }

   // if at least one CNI was found, send it to acq ctl
   if (cniCount > 0)
   {
      EpgAcqCtl_UpdateProvList(cniCount, provCnis);
   }
}

// ----------------------------------------------------------------------------
// Build statistics message
// - called periodically after each AI update
//
static void EpgAcqServer_BuildStatsMsg( EPGDBSRV_STATE * req, bool aiFollows )
{
   EPG_ACQ_VPS_PDC  vpsPdc;
   EPG_ACQ_STATS acqStats;
   bool   newVpsPdc;
   uint   msgLen;

   if (EpgAcqCtl_GetAcqStats(&acqStats))
   {
      req->msgBuf.stats_ind.p.pNext   = NULL;
      req->msgBuf.stats_ind.aiFollows = aiFollows;
      // always include description of acq state
      EpgAcqCtl_DescribeAcqState(&req->msgBuf.stats_ind.descr);
      newVpsPdc = EpgAcqCtl_GetVpsPdc(&vpsPdc, VPSPDC_REQ_DAEMON, TRUE);

      if ((req->statsReqBits & STATS_REQ_BITS_HIST) == 0)
      {
         req->msgBuf.stats_ind.type = EPGDB_STATS_UPD_TYPE_MINIMAL;
         msgLen = sizeof(req->msgBuf.stats_ind) - sizeof(req->msgBuf.stats_ind.u) + sizeof(req->msgBuf.stats_ind.u.minimal);

         // include database statistics (block counts)
         memcpy(&req->msgBuf.stats_ind.u.minimal.count, &acqStats.nxtv.count, sizeof(acqStats.nxtv.count));
         req->msgBuf.stats_ind.u.minimal.nowMaxAcqNetCount = acqStats.nxtv.nowMaxAcqNetCount;
         if (newVpsPdc)
            req->msgBuf.stats_ind.u.minimal.vpsPdc = vpsPdc;
         else
            req->msgBuf.stats_ind.u.minimal.vpsPdc.cniType = STATS_IND_INVALID_VPS_PDC;
      }
      else
      {
         if (req->doTtxStats == SRV_FWD_STATS_INITIAL)
         {
            req->msgBuf.stats_ind.type = EPGDB_STATS_UPD_TYPE_INITIAL;
            msgLen = sizeof(req->msgBuf.stats_ind) - sizeof(req->msgBuf.stats_ind.u) + sizeof(req->msgBuf.stats_ind.u.initial);

            // include the complete statistics struct
            memcpy(&req->msgBuf.stats_ind.u.initial.stats, &acqStats, sizeof(acqStats));
            if (newVpsPdc)
               req->msgBuf.stats_ind.u.initial.vpsPdc = vpsPdc;
            else
               req->msgBuf.stats_ind.u.initial.vpsPdc.cniType = STATS_IND_INVALID_VPS_PDC;
         }
         else
         {
            req->msgBuf.stats_ind.type = EPGDB_STATS_UPD_TYPE_UPDATE;
            msgLen = sizeof(req->msgBuf.stats_ind) - sizeof(req->msgBuf.stats_ind.u) + sizeof(req->msgBuf.stats_ind.u.update);

            // include database statistics (block counts)
            memcpy(&req->msgBuf.stats_ind.u.update.count, &acqStats.nxtv.count, sizeof(acqStats.nxtv.count));

            req->msgBuf.stats_ind.u.update.ai      = acqStats.nxtv.ai;
            req->msgBuf.stats_ind.u.update.hist    = acqStats.nxtv.hist[acqStats.nxtv.histIdx];
            req->msgBuf.stats_ind.u.update.histIdx = acqStats.nxtv.histIdx;
            req->msgBuf.stats_ind.u.update.stream  = acqStats.nxtv.stream;
            req->msgBuf.stats_ind.u.update.ttx_dec = acqStats.ttx_dec;
            req->msgBuf.stats_ind.u.update.ttx_duration = acqStats.ttx_duration;
            req->msgBuf.stats_ind.u.update.nowMaxAcqRepCount = acqStats.nxtv.nowMaxAcqRepCount;
            req->msgBuf.stats_ind.u.update.nowMaxAcqNetCount = acqStats.nxtv.nowMaxAcqNetCount;
            req->msgBuf.stats_ind.u.update.lastStatsUpdate = acqStats.lastStatsUpdate;
            req->msgBuf.stats_ind.u.update.grabTtxStats = acqStats.ttx_grab.pkgStats;

            if (newVpsPdc)
               req->msgBuf.stats_ind.u.update.vpsPdc = vpsPdc;
            else
               req->msgBuf.stats_ind.u.update.vpsPdc.cniType = STATS_IND_INVALID_VPS_PDC;
         }
      }

      EpgNetIo_WriteMsg(&req->io, MSG_TYPE_STATS_IND, msgLen, &req->msgBuf.stats_ind, FALSE);
   }

   req->doTtxStats = SRV_FWD_STATS_DONE;
}

// ----------------------------------------------------------------------------
// Build VPS/PDC message
//
static void EpgAcqServer_BuildVpsPdcMsg( EPGDBSRV_STATE * req )
{
   EPG_ACQ_VPS_PDC vpsPdc;

   if ( EpgAcqCtl_GetVpsPdc(&vpsPdc, VPSPDC_REQ_DAEMON, TRUE) )
   {
      dprintf5("EpgAcqServer-BuildVpsPdcMsg: %02d.%02d. %02d:%02d (0x%04X)\n", (vpsPdc.pil >> 15) & 0x1F, (vpsPdc.pil >> 11) & 0x0F, (vpsPdc.pil >>  6) & 0x1F, (vpsPdc.pil      ) & 0x3F, vpsPdc.cni);

      req->msgBuf.vps_pdc_ind.vpsPdc = vpsPdc;

      EpgNetIo_WriteMsg(&req->io, MSG_TYPE_VPS_PDC_IND, sizeof(MSG_STRUCT_VPS_PDC_IND), &req->msgBuf, FALSE);
   }

   req->statsReqBits &= ~ STATS_REQ_BITS_VPS_PDC_UPD;
   req->doVpsPdc = FALSE;
}

// ----------------------------------------------------------------------------
// Build database update indication message
//
static void EpgAcqServer_BuildDbUpdateMsg( EPGDBSRV_STATE * req )
{
   req->msgBuf.db_upd_ind.mtime = req->sendDbUpdTime;
   req->sendDbUpdTime = 0;

   EpgNetIo_WriteMsg(&req->io, MSG_TYPE_DB_UPD_IND, sizeof(MSG_STRUCT_DB_UPD_IND), &req->msgBuf, FALSE);
}

// ----------------------------------------------------------------------------
// Build acquisition status description
// - result is a pointer to a single string; text lines are new-line separated
//
static char * EpgAcqServer_BuildAcqDescrStr( void )
{
   char * pMsg = xmalloc(10000);
   EPGACQ_DESCR acqState;
   EPG_ACQ_STATS sv;
   EPG_ACQ_VPS_PDC vpsPdc;
   //bool newVpsPdc;
   uint acq_duration;
   const char * pAcqModeStr;
   const char * pAcqPasvStr;
   uint off = 0;

   EpgAcqCtl_DescribeAcqState(&acqState);
   EpgAcqCtl_GetAcqStats(&sv);
   /*newVpsPdc =*/ EpgAcqCtl_GetVpsPdc(&vpsPdc, VPSPDC_REQ_DAEMON, TRUE);

   EpgAcqCtl_GetAcqModeStr(&acqState, TRUE, &pAcqModeStr, &pAcqPasvStr);
   off += snprintf(pMsg + off, 10000 - off, "Acq mode:               %s\n", pAcqModeStr);
   if (pAcqPasvStr != NULL)
      off += snprintf(pMsg + off, 10000 - off, "Passive reason:         %s\n", pAcqPasvStr);

   if ((sv.nxtv.acqStartTime > 0) && (sv.nxtv.acqStartTime <= sv.lastStatsUpdate))
      acq_duration = sv.lastStatsUpdate - sv.nxtv.acqStartTime;
   else
      acq_duration = 0;

   off += snprintf(pMsg + off, 10000 - off, "Acq working for:        %s EPG\n",
                                            acqState.isTtxSrc ? "Teletext" : "Nextview");

   off += snprintf(pMsg + off, 10000 - off, "Channel VPS/PDC CNI:    %04X\n"
                                            "Channel VPS/PDC PIL:    %02d.%02d. %02d:%02d\n",
                                     vpsPdc.cni,
                                     (vpsPdc.pil >> 15) & 0x1F, (vpsPdc.pil >> 11) & 0x0F,
                                     (vpsPdc.pil >>  6) & 0x1F, (vpsPdc.pil) & 0x3F);

   off += snprintf(pMsg + off, 10000 - off, "Nextview acq duration:  %d\n"
                                            "Nextview provider CNI:  %04X\n"
                                            "Providers done:         %d of %d\n"
                                            "Expected block total:   %d + %d\n"
                                            "Block total in DB:      %d + %d\n",
                               acq_duration,
                               acqState.nxtvDbCni,
                               acqState.cycleIdx, acqState.cniCount,
                               sv.nxtv.count[0].ai, sv.nxtv.count[1].ai,
                               sv.nxtv.count[0].allVersions + sv.nxtv.count[0].expired + sv.nxtv.count[0].defective,
                               sv.nxtv.count[1].allVersions + sv.nxtv.count[1].expired + sv.nxtv.count[1].defective
                  );
   if (ACQMODE_IS_CYCLIC(acqState.mode))
   {
      off += snprintf(pMsg + off, 10000 - off, "Network variance:       %1.2f / %1.2f\n",
                                   sv.nxtv.count[0].variance, sv.nxtv.count[1].variance);
   }

   off += snprintf(pMsg + off, 10000 - off,
                 "TTX pkg/frame:          %.1f\n"
                 "EPG pages/sec:          %1.2f\n"
                 "EPG page no:            %X\n"
                 "AI recv. count:         %d\n"
                 "AI min/avg/max [sec]:   %d/%2.2f/%d\n"
                 "PI repetition now/1/2:  %d/%.2f/%.2f\n"
                 "TTX acq. duration:      %d\n"
                 "TTX lost/got pages:     %d/%d\n"
                 "TTX lost/got pkg:       %d/%d\n"
                 "EPG dropped/got blocks: %d/%d\n"
                 "EPG blanked/got chars:  %d/%d\n",
                 (double)sv.ttx_dec.ttxPkgRate / (1 << TTX_PKG_RATE_FIXP),
                 ((acq_duration > 0) ? ((double)sv.ttx_dec.epgPagCount / acq_duration) : 0),
                 sv.nxtv.stream.epgPageNo,
                 sv.nxtv.ai.aiCount,
                 (int)sv.nxtv.ai.minAiDistance,
                    ((sv.nxtv.ai.aiCount > 1) ? ((double)sv.nxtv.ai.sumAiDistance / (sv.nxtv.ai.aiCount - 1)) : 0),
                    (int)sv.nxtv.ai.maxAiDistance,
                 sv.nxtv.nowMaxAcqRepCount, sv.nxtv.count[0].avgAcqRepCount, sv.nxtv.count[1].avgAcqRepCount,
                 sv.ttx_duration,
                 sv.nxtv.stream.epgPagMissing, sv.nxtv.stream.epgPagCount,
                 sv.nxtv.stream.epgPkgMissing, sv.nxtv.stream.epgPkgCount,
                 sv.nxtv.stream.epgBlkErr, sv.nxtv.stream.epgBlkCount,
                 sv.nxtv.stream.epgParErr, sv.nxtv.stream.epgStrSum
          );

   if (acqState.ttxSrcCount > 0)
   {
      off += snprintf(pMsg + off, 10000 - off, "Teletext sources done:  %d of %d\n"
                                               "Teletext source name:   %s\n"
                                               "Teletext acq duration:  %d\n",
                                  acqState.ttxGrabDone, acqState.ttxSrcCount,
                                  (*sv.ttx_grab.srcName ? (const char*)sv.ttx_grab.srcName : "-"),
                                  (int)(sv.lastStatsUpdate - sv.ttx_grab.acqStartTime)
                     );
   }

   return pMsg;
}

// ----------------------------------------------------------------------------
// Perform dump of a client-requested provider datbase
// - the db context is opened at the start of the dump and kept open until finished
// - returns TRUE if the dump is finished successfully; more specifically, TRUE is
//   returned when the func is called and nothing is to be done, so that I/O is
//   guaranteed to be idle
//
static bool EpgAcqServer_DumpReqDb( EPGDBSRV_STATE * req )
{
   time32_t swapTime;
   uint   swapCni;
   bool   result;

   if (req->pDbContext == NULL)
   {  // currently no dump in progress -> open next db
      // dump the next db from the requested list; loop until a dumpable one is found
      for ( ; req->dumpProvIdx < req->cniCount; req->dumpProvIdx++)
      {
         if ( (req->dumpProvIdx < req->cniCount - 1) && (req->dumpCnis[req->dumpProvIdx] == srvState.lastFwdCni) )
         {  // move the current acq db to the last position
            dprintf2("EpgAcqServer-DumpReqDb: move acq db #%d, CNI 0x%04X to end of list\n", req->dumpProvIdx, req->dumpCnis[req->dumpProvIdx]);
            swapCni  = req->dumpCnis[req->dumpProvIdx];
            swapTime = req->dumpStartTimes[req->dumpProvIdx];
            req->dumpCnis[req->dumpProvIdx]       = req->dumpCnis[req->cniCount - 1];
            req->dumpStartTimes[req->dumpProvIdx] = req->dumpStartTimes[req->cniCount - 1];
            req->dumpCnis[req->cniCount - 1]       = swapCni;
            req->dumpStartTimes[req->cniCount - 1] = swapTime;
         }

         if (req->dumpStartTimes[req->dumpProvIdx] < EpgContextCtl_GetAiUpdateTime(req->dumpCnis[req->dumpProvIdx], FALSE))
         {
            req->pDbContext = EpgContextCtl_Open(req->dumpCnis[req->dumpProvIdx], FALSE, CTX_FAIL_RET_NULL, CTX_RELOAD_ERR_ANY);
            if (req->pDbContext != NULL)
            {
               dprintf3("EpgAcqServer-DumpReqDb: dump db #%d, CNI 0x%04X: last %ld secs\n", req->dumpProvIdx, req->dumpCnis[req->dumpProvIdx], time(NULL) - req->dumpStartTimes[req->dumpProvIdx]);

               // initialize parameters for the dump
               req->dump.dumpAcqTime    = req->dumpStartTimes[req->dumpProvIdx];
               req->dump.dumpType       = BLOCK_TYPE_AI;
               req->dump.dumpBlockNo    = 0;
               req->dump.dumpStartTime  = 0;
               req->dump.dumpNetwop     = 0;
               // remember the last dumped provider
               req->dumpedLastCni       = req->dumpCnis[req->dumpProvIdx];
               // send a dump indication to the client
               req->msgBuf.dump_ind.cni = req->dumpCnis[req->dumpProvIdx];
               EpgNetIo_WriteMsg(&req->io, MSG_TYPE_DUMP_IND, sizeof(req->msgBuf.dump_ind), &req->msgBuf.dump_ind, FALSE);
               break;
            }
         }
         else
            dprintf2("EpgAcqServer-DumpReqDb: skip dump of db #%d, CNI 0x%04X: no newer blocks\n", req->dumpProvIdx, req->dumpCnis[req->dumpProvIdx]);
      }
   }

   if (req->pDbContext != NULL)
   {
      if (EpgNetIo_DumpAllBlocks(&req->io, req->pDbContext, &req->dump, ((req->statsReqBits & STATS_REQ_BITS_TSC_REQ) ? &req->tscQueue : NULL)) == FALSE)
      {
         // dump for this db is finished -> close the dumped db
         EpgContextCtl_Close(req->pDbContext);
         req->pDbContext = NULL;
         // update dump status
         req->dumpStartTimes[req->dumpProvIdx] = time(NULL);
         req->dumpProvIdx += 1;
      }
      result = FALSE;
   }
   else  // all finished
      result = TRUE;

   return result;
}

// ----------------------------------------------------------------------------
// Initialize the state for start of dump of requested dbs
// - called after reception of forward request message
//
static void EpgAcqServer_DumpReqInit( EPGDBSRV_STATE * req, const time32_t * pDumpStartTimes )
{
   memcpy(req->dumpCnis, req->provCnis, sizeof(req->dumpCnis));
   memcpy(req->dumpStartTimes, pDumpStartTimes, sizeof(req->dumpStartTimes));
   req->dumpedLastCni = 0;
   req->dumpProvIdx   = 0;
}

// ----------------------------------------------------------------------------
// Check if the current acq database is requested for forwarding
//
static void EpgAcqServer_EnableBlockFwd( EPGDBSRV_STATE * req )
{
   uint  dbIdx;

   if (req->cniCount == 0)
   {  // client specified no CNIs
      req->enableAllBlocks = FALSE;
   }
   else
   {  // search for the current AI CNI in the requested list
      for (dbIdx=0; dbIdx < req->cniCount; dbIdx++)
         if (req->provCnis[dbIdx] == srvState.lastFwdCni)
            break;
      // enable block forward for all types only if acq is working on a requested CNI
      req->enableAllBlocks = (dbIdx < req->cniCount);
   }
}

// ----------------------------------------------------------------------------
// Close the connection to the client
// - frees all allocated resources
//
static void EpgAcqServer_Close( EPGDBSRV_STATE * req, bool closeAll )
{
   dprintf1("EpgAcqServer-Close: fd %d\n", req->io.sock_fd);
   EpgNetIo_Logger(LOG_INFO, req->io.sock_fd, 0, "closing connection", NULL);

   EpgNetIo_CloseIO(&req->io);

   if (req->pDbContext != NULL)
   {
      EpgContextCtl_Close(req->pDbContext);
      req->pDbContext = NULL;
   }

   EpgDbQueue_Clear(&req->outQueue);
   EpgTscQueue_Clear(&req->tscQueue);

   if ((req->cniCount > 0) && (closeAll == FALSE))
      EpgAcqServer_MergeCniLists();
}

// ----------------------------------------------------------------------------
// Initialize a request structure for a new client and add it to the list
//
static void EpgAcqServer_AddConnection( int listen_fd, bool isLocal )
{
   EPGDBSRV_STATE * req;
   int sock_fd;

   sock_fd = EpgNetIo_AcceptConnection(listen_fd);
   if (sock_fd != -1)
   {
      dprintf1("EpgAcqServer-AddConnection: fd %d\n", sock_fd);

      req = xmalloc(sizeof(EPGDBSRV_STATE));
      memset(req, 0, sizeof(EPGDBSRV_STATE));

      req->state         = SRV_STATE_WAIT_CON_REQ;
      req->isLocalCon    = isLocal;
      req->io.lastIoTime = time(NULL);
      req->io.sock_fd    = sock_fd;

      // insert request into the chain
      req->pNext = pReqChain;
      pReqChain  = req;
      srvState.conCount  += 1;
   }
}

// ----------------------------------------------------------------------------
// Checks the size of a message from client to server
//
static bool EpgAcqServer_CheckMsg( uint len, EPGNETIO_MSG_HEADER * pHead, EPGDBSRV_MSG_BODY * pBody, bool * pEndianSwap )
{
   uint idx;
   bool result = FALSE;

   switch (pHead->type)
   {
      case MSG_TYPE_CONNECT_REQ:
         if ( (len == sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->con_req)) &&
              (memcmp(pBody->con_req.magic, MAGIC_STR, MAGIC_STR_LEN) == 0) )
         {
            if (pBody->con_req.endianMagic == PROTOCOL_ENDIAN_MAGIC)
            {
               *pEndianSwap = FALSE;
               result       = TRUE;
            }
            else if (pBody->con_req.endianMagic == PROTOCOL_WRONG_ENDIAN)
            {
               *pEndianSwap = TRUE;
               result       = TRUE;
            }
         }
         else if ( (len == sizeof(EPGNETIO_MSG_HEADER) + 7) &&
                   (memcmp(pBody, "ACQSTAT", 7) == 0) )
         {
            result = TRUE;
         }
         else if ( (len == sizeof(EPGNETIO_MSG_HEADER) + 3) &&
                   (memcmp(pBody, "PID", 3) == 0) )
         {
            result = TRUE;
         }
         break;

      case MSG_TYPE_FORWARD_REQ:
         if (len == sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->fwd_req))
         {
            if (*pEndianSwap)
            {
               swap32(&pBody->fwd_req.statsReqBits);
               swap32(&pBody->fwd_req.cniCount);
               if (pBody->fwd_req.cniCount <= MAX_MERGED_DB_COUNT)
               {
                  for (idx=0; idx < pBody->fwd_req.cniCount; idx++)
                  {
                     swap32(&pBody->fwd_req.dumpStartTimes[idx]);
                     swap32(&pBody->fwd_req.provCnis[idx]);
                  }
                  result = TRUE;
               }
            }
            else if (pBody->fwd_req.cniCount <= MAX_MERGED_DB_COUNT)
            {
               result = TRUE;
            }
         }
         break;

      case MSG_TYPE_STATS_REQ:
         if (len == sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->stats_req))
         {
            if (*pEndianSwap)
               swap32(&pBody->stats_req.statsReqBits);
            result = TRUE;
         }
         break;

      case MSG_TYPE_CLOSE_IND:
         result = (len == sizeof(EPGNETIO_MSG_HEADER));
         break;

      case MSG_TYPE_CONNECT_CNF:
      case MSG_TYPE_CONQUERY_CNF:
      case MSG_TYPE_FORWARD_CNF:
      case MSG_TYPE_FORWARD_IND:
      case MSG_TYPE_BLOCK_IND:
      case MSG_TYPE_TSC_IND:
      case MSG_TYPE_VPS_PDC_IND:
      case MSG_TYPE_DB_UPD_IND:
      case MSG_TYPE_STATS_IND:
      case MSG_TYPE_DUMP_IND:
         debug1("EpgAcqServer-CheckMsg: recv client msg type %d", pHead->type);
         result = FALSE;
         break;
      default:
         debug1("EpgAcqServer-CheckMsg: unknown msg type %d", pHead->type);
         result = FALSE;
         break;
   }

   ifdebug2(result==FALSE, "EpgAcqServer-CheckMsg: illegal msg: len=%d, type=%d", len, pHead->type);

   return result;
}

// ----------------------------------------------------------------------------
// Handle message from client
// - note: consistancy checks were already done by the I/O handler
//   except for higher level messages (must be checked by acqctl module)
// - implemented as a matrix: "switch" over server state, and "if" cascades
//   over message type
//
static bool EpgAcqServer_TakeMessage( EPGDBSRV_STATE *req, EPGDBSRV_MSG_BODY * pMsg )
{
   bool result = FALSE;

   dprintf2("EpgAcqServer-TakeMessage: fd %d: recv msg type %d\n", req->io.sock_fd, req->io.readHeader.type);

   switch (req->io.readHeader.type)
   {
      case MSG_TYPE_CONNECT_REQ:
         // shortest must be checked first to avoid reading beyond end-of-message
         if (memcmp(pMsg, "PID", 3) == 0)
         {
            char * pStr = xmalloc(20);
            snprintf(pStr, 20, "PID %ld\n", (long) getpid());
            EpgNetIo_WriteMsg(&req->io, MSG_TYPE_CONQUERY_CNF, strlen(pStr), pStr, TRUE);
            result = TRUE;
         }
         else if (memcmp(pMsg, "ACQSTAT", 7) == 0)
         {
            char * pStr = EpgAcqServer_BuildAcqDescrStr();
            EpgNetIo_WriteMsg(&req->io, MSG_TYPE_CONQUERY_CNF, strlen(pStr), pStr, TRUE);
            result = TRUE;
         }
         else if ( (memcmp(pMsg->con_req.magic, MAGIC_STR, MAGIC_STR_LEN) == 0) &&
                   (req->state == SRV_STATE_WAIT_CON_REQ) )
         {
            // Nextview browser requests acq forward: reply with version information
            dprintf0("EpgAcqServer-TakeMessage: con req for BROWSER type\n");
            memcpy(req->msgBuf.con_cnf.magic, MAGIC_STR, MAGIC_STR_LEN);
            memset(req->msgBuf.con_cnf.reserved, 0, sizeof(req->msgBuf.con_cnf.reserved));
            req->msgBuf.con_cnf.endianMagic           = ENDIAN_MAGIC;
            req->msgBuf.con_cnf.blockCompatVersion    = DUMP_COMPAT;
            req->msgBuf.con_cnf.protocolCompatVersion = PROTOCOL_COMPAT;
            req->msgBuf.con_cnf.swVersion             = EPG_VERSION_NO;
#ifndef WIN32
            req->msgBuf.con_cnf.daemon_pid            = getpid();
#else
            req->msgBuf.con_cnf.daemon_pid            = 0;
#endif
#ifdef USE_UTF8
            req->msgBuf.con_cnf.use_utf8              = TRUE;
#else
            req->msgBuf.con_cnf.use_utf8              = FALSE;
#endif
#ifdef USE_32BIT_COMPAT
            req->msgBuf.con_cnf.use_32_bit_compat     = TRUE;
#else
            req->msgBuf.con_cnf.use_32_bit_compat     = FALSE;
#endif
            EpgNetIo_WriteMsg(&req->io, MSG_TYPE_CONNECT_CNF, sizeof(req->msgBuf.con_cnf), &req->msgBuf.con_cnf, FALSE);

            req->state = SRV_STATE_WAIT_FWD_REQ;
            result = TRUE;
         }
         break;

      case MSG_TYPE_FORWARD_REQ:
         // the client submits a list of providers of which he wants all incoming blocks
         // this message is sent initially and after a GUI provider change
         // note: if acq is working on a provider not in this list, AI and OI#0 are forwarded anyways
         dprintf1("EpgAcqServer-TakeMessage: fwd req for %d CNIs\n", pMsg->fwd_req.cniCount);
         if ( (req->state == SRV_STATE_WAIT_FWD_REQ) ||
              (req->state == SRV_STATE_DUMP_REQUESTED) ||
              (req->state == SRV_STATE_DUMP_ACQ) ||
              (req->state == SRV_STATE_FORWARD) )
         {
            // save the new CNI list in the client req structure
            memcpy(req->provCnis, pMsg->fwd_req.provCnis, sizeof(req->provCnis));
            req->cniCount = pMsg->fwd_req.cniCount;
            req->statsReqBits = pMsg->fwd_req.statsReqBits;
            EpgAcqServer_DumpReqInit(req, pMsg->fwd_req.dumpStartTimes);

            // in case state is >= dump must reset it before the merge, which may cause a prov change
            req->state = SRV_STATE_WAIT_FWD_REQ;
            // merge the list with other clients and it to the acq ctl layer
            EpgAcqServer_MergeCniLists();

            // free old blocks in the output queues (e.g. during dump)
            EpgDbQueue_Clear(&req->outQueue);
            EpgTscQueue_Clear(&req->tscQueue);

            // immediately reply with confirmation message: mandatory for synchronization with the client
            // - copy params from request into reply (to identify the request we reply to)
            memcpy(req->msgBuf.fwd_cnf.provCnis, req->provCnis, sizeof(req->msgBuf.fwd_cnf.provCnis));
            req->msgBuf.fwd_cnf.cniCount = req->cniCount;
            EpgNetIo_WriteMsg(&req->io, MSG_TYPE_FORWARD_CNF, sizeof(req->msgBuf.fwd_cnf), &req->msgBuf.fwd_cnf, FALSE);

            // check if the current acq db is requested for forwarding
            EpgAcqServer_EnableBlockFwd(req);
            req->doTtxStats = SRV_FWD_STATS_INITIAL;

            req->state = SRV_STATE_DUMP_REQUESTED;
            result = TRUE;
         }
         break;

      case MSG_TYPE_STATS_REQ:
         // update stats request bits: just copy the value, no confirmation sent
         // this message is accepted in all states
         if (req->state == SRV_STATE_FORWARD)
         {
            // send statistics if extended reports were requested
            if ( ((pMsg->stats_req.statsReqBits & STATS_REQ_BITS_HIST) != 0) &&
                 ((req->statsReqBits & STATS_REQ_BITS_HIST) == 0) )
               req->doTtxStats = SRV_FWD_STATS_INITIAL;

            // if timescale forward is (or was already) disabled, clear the queue
            if ((req->statsReqBits & STATS_REQ_BITS_TSC_REQ) == 0)
            {
               EpgTscQueue_Clear(&req->tscQueue);
            }
            // full timescale info need only be sent if this is a non-requested db
            // and the user has opened the acq timescales
            if ( (req->enableAllBlocks == FALSE) &&
                 ((pMsg->stats_req.statsReqBits & STATS_REQ_BITS_TSC_REQ) != 0) &&
                 ((pMsg->stats_req.statsReqBits & STATS_REQ_BITS_TSC_ALL) != 0) &&
                 ((req->statsReqBits & STATS_REQ_BITS_TSC_ALL) == 0) )
            {
               EPGDB_CONTEXT * pDbContext = EpgAcqCtl_GetDbContext(TRUE);
               if (pDbContext != NULL)
               {
                  EpgTscQueue_AddAll(&req->tscQueue, pDbContext);
               }
               EpgAcqCtl_GetDbContext(FALSE);
            }

            // immediately send VPS/PDC updates if requested and available
            if (pMsg->stats_req.statsReqBits & STATS_REQ_BITS_VPS_PDC_REQ)
            {
               if (pMsg->stats_req.statsReqBits & STATS_REQ_BITS_VPS_PDC_UPD)
               {  // update requested (after channel change)
                  // query VPS/PDC now, thereby discarding old CNI/PIL results
                  EpgAcqCtl_ProcessVps();
                  // wait for the next update
                  req->doVpsPdc = FALSE;
               }
               else if ((req->statsReqBits & STATS_REQ_BITS_VPS_PDC_REQ) == 0)
               {  // VPS requested for the first time -> reply immediately
                  // fetch the content of the message from the acqctl layer
                  EpgAcqServer_BuildVpsPdcMsg(req);
               }
            }
         }
         req->statsReqBits = pMsg->stats_req.statsReqBits;
         result = TRUE;
         break;

      case MSG_TYPE_CLOSE_IND:
         // close the connection
         EpgAcqServer_Close(req, FALSE);
         // message was freed in close function
         pMsg = NULL;
         result = TRUE;
         break;

      default:
         // unknown message or client-only message
         debug1("EpgAcqServer-HandleSockets: protocol error: unexpected message type %d", req->io.readHeader.type);
         break;
   }

   ifdebug3(result==FALSE, "EpgAcqServer-TakeMessage: message type %d (len %d) not expected in state %d", req->io.readHeader.type, req->io.readHeader.len, req->state);
   if (pMsg != NULL)
      xfree(pMsg);

   return result;
}

// ----------------------------------------------------------------------------
// Set bits for all active sockets in fd_set for select syscall
//
sint EpgAcqServer_GetFdSet( fd_set * rd, fd_set * wr )
{
   EPGDBSRV_STATE    *req;
   sint              max_fd;

   // add TCP/IP and UNIX-domain listening sockets
   max_fd = 0;
   if ((srvState.max_conn == 0) || (srvState.conCount < srvState.max_conn))
   {
      if (srvState.tcp_ip_fd != -1)
      {
         FD_SET(srvState.tcp_ip_fd, rd);
         if (srvState.tcp_ip_fd > max_fd)
             max_fd = srvState.tcp_ip_fd;
      }
      if (srvState.pipe_fd != -1)
      {
         FD_SET(srvState.pipe_fd, rd);
         if (srvState.pipe_fd > max_fd)
             max_fd = srvState.pipe_fd;
      }
   }

   // add client connection sockets
   for (req = pReqChain; req != NULL; req = req->pNext)
   {
      // read and write are exclusive and write takes prcedence over read
      // (i.e. read only if no write is pending or if a read operation has already been started)
      if (req->io.waitRead || (req->io.readLen > 0))
         FD_SET(req->io.sock_fd, rd);
      else
      if ((req->io.writeLen > 0) ||
          (EpgDbQueue_GetBlockCount(&req->outQueue) > 0) ||
          EpgTscQueue_HasElems(&req->tscQueue) ||
          (req->state == SRV_STATE_DUMP_REQUESTED) ||
          (req->state == SRV_STATE_DUMP_ACQ) ||
          (req->doTtxStats != SRV_FWD_STATS_DONE) ||
          (req->doVpsPdc) ||
          (req->sendDbUpdTime != 0))
         FD_SET(req->io.sock_fd, wr);
      else
         FD_SET(req->io.sock_fd, rd);

      if (req->io.sock_fd > max_fd)
          max_fd = req->io.sock_fd;
   }

   return max_fd;
}

// ----------------------------------------------------------------------------
// Daemon central connection handling
//
void EpgAcqServer_HandleSockets( fd_set * rd, fd_set * wr )
{
   EPGDB_CONTEXT     *pDbContext;
   const EPGDB_BLOCK *pOutBlock;
   EPGDBSRV_STATE    *req;
   EPGDBSRV_STATE    *prev, *tmp;
   bool              ioBlocked;  // dummy
   time_t now = time(NULL);

   // accept new TCP/IP connections
   if ((srvState.tcp_ip_fd != -1) && (FD_ISSET(srvState.tcp_ip_fd, rd)))
   {
      EpgAcqServer_AddConnection(srvState.tcp_ip_fd, FALSE);
   }

   // accept new local connections
   if ((srvState.pipe_fd != -1) && (FD_ISSET(srvState.pipe_fd, rd)))
   {
      EpgAcqServer_AddConnection(srvState.pipe_fd, TRUE);
   }

   // handle active connections
   for (req = pReqChain, prev = NULL; req != NULL; )
   {
      if ( FD_ISSET(req->io.sock_fd, rd) ||
           ((req->io.writeLen > 0) && FD_ISSET(req->io.sock_fd, wr)) )
      {
         req->io.lastIoTime = now;

         if ( EpgNetIo_IsIdle(&req->io) )
         {  // currently no I/O in progress

            if (FD_ISSET(req->io.sock_fd, rd))
            {  // new incoming data -> start reading
               dprintf1("EpgAcqServer-HandleSockets: fd %d: receiving new msg\n", req->io.sock_fd);
               req->io.waitRead = TRUE;
               req->io.readLen  = 0;
               req->io.readOff  = 0;
            }
         }
         if (EpgNetIo_HandleIO(&req->io, &ioBlocked, TRUE))
         {
            // check for finished read -> process request
            if ( (req->io.readLen != 0) && (req->io.readLen == req->io.readOff) )
            {
               if (EpgAcqServer_CheckMsg(req->io.readLen, &req->io.readHeader, (EPGDBSRV_MSG_BODY *) req->io.pReadBuf, &req->endianSwap))
               {
                  req->io.readLen  = 0;

                  if (EpgAcqServer_TakeMessage(req, (EPGDBSRV_MSG_BODY *) req->io.pReadBuf) == FALSE)
                  {  // message no accepted (e.g. wrong state)
                     req->io.pReadBuf = NULL;
                     EpgAcqServer_Close(req, FALSE);
                  }
                  else  // ok
                     req->io.pReadBuf = NULL;
               }
               else
               {  // message has illegal size or content
                  EpgAcqServer_Close(req, FALSE);
               }
            }
         }
         else
            EpgAcqServer_Close(req, FALSE);
      }
      else if (EpgNetIo_IsIdle(&req->io))
      {  // currently no I/O in progress

         pOutBlock = EpgDbQueue_Peek(&req->outQueue);
         if (req->state == SRV_STATE_DUMP_REQUESTED)
         {
            if (EpgAcqServer_DumpReqDb(req) && (req->io.sock_fd != -1))
            {  // dump is finished -> advance to the next state
               if ((req->enableAllBlocks == FALSE) || (req->dumpedLastCni != srvState.lastFwdCni))
               {  // current acq db is not among the client-requested
                  // dump at least AI and OI#0 (AI is required so that acqctl opens the new db)
                  req->dump.dumpType = BLOCK_TYPE_AI;
                  req->state = SRV_STATE_DUMP_ACQ;
               }
               else
               {  // current acq db is among the requested, i.e. has already been dumped
                  // inform the client that dumps are over
                  req->msgBuf.fwd_ind.cni = srvState.lastFwdCni;
                  EpgNetIo_WriteMsg(&req->io, MSG_TYPE_FORWARD_IND, sizeof(req->msgBuf.fwd_ind), &req->msgBuf.fwd_ind, FALSE);
                  req->state = SRV_STATE_FORWARD;
               }

               // send updated acq status information
               req->doTtxStats = SRV_FWD_STATS_INITIAL;
            }
         }
         else if (req->state == SRV_STATE_DUMP_ACQ)
         {  // Perform dump of AI and OI#0 of non-requested db
            pDbContext = EpgAcqCtl_GetDbContext(TRUE);
            if (pDbContext != NULL)
            {
               if ( (EpgNetIo_DumpAiOi(&req->io, pDbContext, &req->dump) == FALSE) && (req->io.sock_fd != -1) )
               {
                  if ( (req->statsReqBits & STATS_REQ_BITS_TSC_REQ) &&
                       (req->statsReqBits & STATS_REQ_BITS_TSC_ALL) &&
                       (req->enableAllBlocks == FALSE) )
                  {  // if this is a non-forwarded provider but acq timescales are open,
                     // generate full timescale info
                     EpgTscQueue_AddAll(&req->tscQueue, pDbContext);
                  }
                  req->msgBuf.fwd_ind.cni = srvState.lastFwdCni;
                  EpgNetIo_WriteMsg(&req->io, MSG_TYPE_FORWARD_IND, sizeof(req->msgBuf.fwd_ind), &req->msgBuf.fwd_ind, FALSE);

                  req->state = SRV_STATE_FORWARD;
               }
            }
            else
            {  // acquisition has been stopped (can only occur during server shutdown)
               EpgAcqServer_Close(req, TRUE);
            }
            EpgAcqCtl_GetDbContext(FALSE);
         }
         else if ( (req->doVpsPdc) &&
                   (req->state == SRV_STATE_FORWARD) )
         {  // send VPS/PDC
            EpgAcqServer_BuildVpsPdcMsg(req);
         }
         else if ( (req->sendDbUpdTime != 0) &&
                   (req->state == SRV_STATE_FORWARD) )
         {
            EpgAcqServer_BuildDbUpdateMsg(req);
         }
         else if ( (req->doTtxStats != SRV_FWD_STATS_DONE) &&
                   (req->state == SRV_STATE_FORWARD) &&
                   ( (pOutBlock == NULL) ||
                     (req->doTtxStats == SRV_FWD_STATS_UPD_NO_AI) ||
                     (pOutBlock->type == BLOCK_TYPE_AI) ) )
         {  // statistics are due: include them now if either the block queue is empty
            // or next is an AI block (but suppress stats during provider change)
            EpgAcqServer_BuildStatsMsg(req, (pOutBlock != NULL) && (pOutBlock->type == BLOCK_TYPE_AI));
         }
         else if (pOutBlock != NULL)
         {  // send the next acq block
            if (EpgNetIo_WriteEpgQueue(&req->io, &req->outQueue) == FALSE)
               EpgAcqServer_Close(req, FALSE);
         }
         else if (EpgTscQueue_HasElems(&req->tscQueue))
         {
            if (EpgNetIo_WriteTscQueue(&req->io, &req->tscQueue) == FALSE)
               EpgAcqServer_Close(req, FALSE);
         }

         if ((req->io.sock_fd != -1) && (req->io.writeLen > 0))
         {
            if (EpgNetIo_HandleIO(&req->io, &ioBlocked, TRUE) == FALSE)
               EpgAcqServer_Close(req, FALSE);
         }
      }

      if (req->io.sock_fd == -1)
      {  // free resources (should be redundant, but does no harm)
         EpgAcqServer_Close(req, FALSE);
      }
      else if (EpgNetIo_CheckTimeout(&req->io, now))
      {
         debug7("EpgAcqServer-HandleSockets: fd %d: i/o timeout in state %d (writeLen=%d, waitRead=%d, readLen=%d, readOff=%d, read msg type=%d)", req->io.sock_fd, req->state, req->io.writeLen, req->io.waitRead, req->io.readLen, req->io.readOff, req->io.readHeader.type);
         EpgAcqServer_Close(req, FALSE);
      }
      else // check for protocol or network I/O timeout
      if ( (now > req->io.lastIoTime + SRV_REPLY_TIMEOUT) &&
           ( (req->state == SRV_STATE_WAIT_CON_REQ) ||
             (req->state == SRV_STATE_WAIT_FWD_REQ) ))
      {
         debug2("EpgAcqServer-HandleSockets: fd %d: protocol timeout in state %d", req->io.sock_fd, req->state);
         EpgAcqServer_Close(req, FALSE);
      }
      else if ( (now > req->io.lastIoTime + SRV_STALLED_STATS_INTV) &&
                (req->state == SRV_STATE_FORWARD) &&
                EpgNetIo_IsIdle(&req->io) )
      {
         dprintf1("EpgAcqServer-HandleSockets: fd %d: send 'no reception' stats\n", req->io.sock_fd);
         req->io.lastIoTime = now;
         req->doTtxStats = SRV_FWD_STATS_UPD_NO_AI;
         EpgAcqServer_BuildStatsMsg(req, FALSE);
      }

      if (req->io.sock_fd == -1)
      {  // connection was closed after network error
         srvState.conCount -= 1;
         dprintf1("EpgAcqServer-HandleSockets: closed conn, %d remain\n", srvState.conCount);
         // unlink from list
         tmp = req;
         if (prev == NULL)
         {
            pReqChain = req->pNext;
            req = pReqChain;
         }
         else
         {
            prev->pNext = req->pNext;
            req = req->pNext;
         }
         xfree(tmp);
      }
      else
      {
         prev = req;
         req = req->pNext;
      }
   }
}

// ----------------------------------------------------------------------------
// Stop the server, close all connections, free resources
//
void EpgAcqServer_Destroy( void )
{
   EPGDBSRV_STATE  *pReq, *pNext;

   // shutdown all client connections & free resources
   pReq = pReqChain;
   while (pReq != NULL)
   {
      pNext = pReq->pNext;
      EpgAcqServer_Close(pReq, TRUE);
      xfree(pReq);
      pReq = pNext;
   }
   pReqChain = NULL;
   srvState.conCount = 0;

   // close listening sockets
   if (srvState.pipe_fd != -1)
   {
      EpgNetIo_StopListen(FALSE, srvState.pipe_fd);
      EpgNetIo_Logger(LOG_NOTICE, -1, 0, "shutting down", NULL);
   }
   if (srvState.tcp_ip_fd != -1)
   {
      EpgNetIo_StopListen(TRUE, srvState.pipe_fd);
   }

   // free the memory allocated for the config strings
   EpgAcqServer_SetAddress(FALSE, NULL, NULL);
   EpgNetIo_SetLogging(0, 0, NULL);

   EpgNetIo_Destroy();
}

// ----------------------------------------------------------------------------
// Initialize DB server
//
void EpgAcqServer_Init( bool have_tty )
{
   EpgNetIo_Init(NULL);

   // initialize state struct
   memset(&srvState, 0, sizeof(srvState));
   srvState.pipe_fd = -1;
   srvState.tcp_ip_fd = -1;
   isDbServer = TRUE;
}

// ----------------------------------------------------------------------------
// Set up sockets for listening to client requests
//
bool EpgAcqServer_Listen( void )
{
   bool result = FALSE;

   if (EpgNetIo_CheckConnect() == FALSE)
   {
      // create named socket in /tmp for listening to local clients
      #ifndef WIN32
      srvState.pipe_fd = EpgNetIo_ListenSocket(FALSE, NULL, NULL);
      if (srvState.pipe_fd != -1)
      #else
      srvState.pipe_fd = -1;
      #endif
      {
         if (srvState.do_tcp_ip)
         {
            // create TCP/IP socket
            srvState.tcp_ip_fd = EpgNetIo_ListenSocket(TRUE, srvState.listen_ip, srvState.listen_port);
            if (srvState.tcp_ip_fd != -1)
            {
               #ifndef WIN32
               EpgNetIo_Logger(LOG_NOTICE, -1, 0, "started listening on local and TCP/IP socket, port ", srvState.listen_port, NULL);
               #else
               EpgNetIo_Logger(LOG_NOTICE, -1, 0, "started listening on TCP/IP socket, port ", srvState.listen_port, NULL);
               #endif
               result = TRUE;
            }
         }
         else
         {  // no TCP/IP socket requested
            #ifndef WIN32
            EpgNetIo_Logger(LOG_NOTICE, -1, 0, "started listening on local socket", NULL);
            result = TRUE;
            #else
            EpgNetIo_Logger(LOG_ERR, -1, 0, "TCP/IP not enabled - only TCP/IP supported for Win32", NULL);
            #endif
         }
      }
   }
   else
      EpgNetIo_Logger(LOG_ERR, -1, 0, "a nxtvepg daemon is already running", NULL);

   return result;
}

// ----------------------------------------------------------------------------
// Set maximum number of open client connections
// - note: does not close connections if max count is already exceeded
//
void EpgAcqServer_SetMaxConn( uint max_conn )
{
   srvState.max_conn = max_conn;
}

// ----------------------------------------------------------------------------
// Set server IP address
// - must be called before the listening sockets are created
//
void EpgAcqServer_SetAddress( bool do_tcp_ip, const char * pIpStr, const char * pPort )
{
   // free the memory allocated for the old config strings
   if (srvState.listen_ip != NULL)
   {
      xfree(srvState.listen_ip);
      srvState.listen_ip = NULL;
   }
   if (srvState.listen_port != NULL)
   {
      xfree(srvState.listen_port);
      srvState.listen_port = NULL;
   }

   // make a copy of the new config strings
   if (pIpStr != NULL)
   {
      srvState.listen_ip = xmalloc(strlen(pIpStr) + 1);
      strcpy(srvState.listen_ip, pIpStr);
   }
   if (pPort != NULL)
   {
      srvState.listen_port = xmalloc(strlen(pPort) + 1);
      strcpy(srvState.listen_port, pPort);
   }
   srvState.do_tcp_ip = do_tcp_ip;
}

// ----------------------------------------------------------------------------
// Notification about provider switches or version change by acqctl module
// - when the acq provider changes, all connected providers are set into an
//   intermediate state in which the server waits until the output queues are
//   empty; then the client is informed about the new acq provider and forwarding
//   is re-established starting with the "wait dump confirmation" state.
//
void EpgAcqServer_SetProvider( uint cni )
{
   EPGDBSRV_STATE *req;

   if (isDbServer)
   {
      if (cni != srvState.lastFwdCni)
      {
         dprintf2("EpgAcqServer-SetProvider: CNI %04X -> %04X\n", srvState.lastFwdCni, cni);
         srvState.lastFwdCni = cni;
         srvState.aiVersionChange = FALSE;

         for (req = pReqChain; req != NULL; req = req->pNext)
         {
            if (req->state >= SRV_STATE_DUMP_REQUESTED)
            {
               EpgAcqServer_EnableBlockFwd(req);

               if (req->state == SRV_STATE_DUMP_REQUESTED)
               {  // nothing to do: prov switch is handled by dump loop
               }
               else if ( (req->state == SRV_STATE_DUMP_ACQ) ||
                         (req->state == SRV_STATE_FORWARD) )
               {  // dump AI and OI#0 (AI is required so that acqctl opens the new db)
                  req->dump.dumpType = BLOCK_TYPE_AI;
                  req->state = SRV_STATE_DUMP_ACQ;
               }
               // send updated acq status information
               req->doTtxStats = SRV_FWD_STATS_INITIAL;
            }
         }
      }
      else
      {  // AI version or PI range changed - this trigger arrives before
         // the new AI block is in the db, and before obsolete PI are removed
         // -> all we can do here is set a flag
         srvState.aiVersionChange = TRUE;
      }
   }
}

// ----------------------------------------------------------------------------
// Trigger sending of VPS/PDC CNI and PIL to all interested clients
// - called every time acquisition has a new VPS/PDC CNI or PIL;
//   The boolean param tells if the CNI or PIL did change since the last call.
// - note: the latest CNI and PIL is also part of the statistics reports
//   but may be sent separately to avoid the delay of up to 15 seconds
//
void EpgAcqServer_SetVpsPdc( bool change )
{
   EPGDBSRV_STATE *req;

   for (req = pReqChain; req != NULL; req = req->pNext)
   {
      if (req->state == SRV_STATE_FORWARD)
      {
         // send the value to this client, if VPS forwarding is enabled AND
         // the CNI or PIL has changed, or an immediate update was requested
         if ( (req->statsReqBits & STATS_REQ_BITS_VPS_PDC_REQ) &&
              (change || (req->statsReqBits & STATS_REQ_BITS_VPS_PDC_UPD)) )
         {
            // schedule sending of VPS/PDC indication
            req->doVpsPdc = TRUE;
            // remove immediate update request bit
            req->statsReqBits &= ~ STATS_REQ_BITS_VPS_PDC_UPD;
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Trigger sending of full statistics (instead of update only)
//
void EpgAcqServer_TriggerStats( void )
{
   EPGDBSRV_STATE *req;

   for (req = pReqChain; req != NULL; req = req->pNext)
   {
      if (req->state == SRV_STATE_FORWARD)
      {
         if (req->statsReqBits & STATS_REQ_BITS_HIST)
         {
            req->doTtxStats = SRV_FWD_STATS_INITIAL;
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Trigger sending of a database update indicator
// - sent after an XML file has been updated by the teletext EPG grabber
// - the mtime parameter is the time at which the database was written;
//   this allows the client to drop obsolete indications
//
void EpgAcqServer_TriggerDbUpdate( time_t mtime )
{
   EPGDBSRV_STATE *req;

   assert(mtime != 0);

   for (req = pReqChain; req != NULL; req = req->pNext)
   {
      if (req->state == SRV_STATE_FORWARD)
      {
         req->sendDbUpdTime = mtime;
      }
   }
}

// ----------------------------------------------------------------------------
// Append a block to the queues of all interested clients
// - blocks are only forwarded if their version or content has changed
// - for every AI blocks a acq stats update is sent, even if the AI is not forwarded
// - blocks are only forwarded if the client has requested the provider
//   except for AI and OI#0 which are always forwarded (if version or content changed)
//
void EpgAcqServer_AddBlock( EPGDB_CONTEXT * dbc, EPGDB_BLOCK * pNewBlock )
{
   EPGDB_CONTEXT * pDbContext;
   EPGDB_BLOCK * pCopyBlock;
   EPGDBSRV_STATE *req;

   if ( isDbServer && (srvState.lastFwdCni != 0) )
   {
      assert ((pNewBlock->type != BLOCK_TYPE_AI) || (srvState.lastFwdCni == AI_GET_THIS_NET_CNI(&pNewBlock->blk.ai)));

      // if content changed -> offer block to all connected clients
      if (pNewBlock->updTimestamp == pNewBlock->acqTimestamp)
      {
         for (req = pReqChain; req != NULL; req = req->pNext)
         {
            if (req->state >= SRV_STATE_DUMP_REQUESTED)
            {
               if (req->enableAllBlocks || IS_AI_OI_BLOCK(pNewBlock))
               {
                  dprintf2("EpgAcqServer-AddBlock: fd %d: add block type %d\n", req->io.sock_fd, pNewBlock->type);

                  // make a copy of the EPG block
                  // XXX TODO do not copy, do not use queue; instead save netwop & startTime in list
                  pCopyBlock = xmalloc(pNewBlock->size + BLK_UNION_OFF);
                  memcpy(pCopyBlock, pNewBlock, pNewBlock->size + BLK_UNION_OFF);
                  pCopyBlock->pNextBlock = NULL;

                  // append the block to the end of the output queue
                  EpgDbQueue_Add(&req->outQueue, pCopyBlock);
               }
            }
         }
      }

      // send an acq stats report with every AI (even if the AI is not forwarded)
      if (pNewBlock->type == BLOCK_TYPE_AI)
      {
         for (req = pReqChain; req != NULL; req = req->pNext)
         {
            if ( (req->state == SRV_STATE_FORWARD) &&
                 (req->doTtxStats != SRV_FWD_STATS_INITIAL) )
            {
               if (pNewBlock->updTimestamp == pNewBlock->acqTimestamp)
                  req->doTtxStats = SRV_FWD_STATS_UPD;
               else
                  req->doTtxStats = SRV_FWD_STATS_UPD_NO_AI;
            }
         }
      }

      // generate PI timescale info if requested
      if (srvState.aiVersionChange == TRUE)
      {
         // after a AI Version change or PI range change, the complete scale info is sent again...
         for (req = pReqChain; req != NULL; req = req->pNext)
         {
            if (req->state == SRV_STATE_FORWARD)
            {
               // ...but only to clients which have opened the acq timescale window
               // and the current provider is not among the requested ones
               if ( (req->statsReqBits & STATS_REQ_BITS_TSC_REQ) &&
                    (req->statsReqBits & STATS_REQ_BITS_TSC_ALL) &&
                    (req->enableAllBlocks == FALSE) )
               {
                  pDbContext = EpgAcqCtl_GetDbContext(TRUE);
                  if (pDbContext != NULL)
                  {
                     EpgTscQueue_AddAll(&req->tscQueue, pDbContext);
                  }
                  EpgAcqCtl_GetDbContext(FALSE);
               }
            }
         }
         srvState.aiVersionChange = FALSE;
      }
      else
      {
         if (pNewBlock->type == BLOCK_TYPE_PI)
         {
            for (req = pReqChain; req != NULL; req = req->pNext)
            {
               if (req->state == SRV_STATE_FORWARD)
               {
                  if ( (req->statsReqBits & STATS_REQ_BITS_TSC_REQ) &&
                       (req->enableAllBlocks || (req->statsReqBits & STATS_REQ_BITS_TSC_ALL)) )
                  {
                     EpgTscQueue_AddPi(&req->tscQueue, dbc, &pNewBlock->blk.pi, pNewBlock->stream);
                  }
               }
            }
         }
      }
   }
}

#endif  // USE_DAEMON
