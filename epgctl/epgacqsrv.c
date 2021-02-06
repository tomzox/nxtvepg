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
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/epgnetio.h"
#include "epgui/uictrl.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgacqsrv.h"


// ----------------------------------------------------------------------------
// Declaration of types of internal state variables

typedef enum
{
   SRV_STATE_WAIT_CON_REQ,
   SRV_STATE_FORWARD,
} SRV_REQ_STATE;

// this struct holds client-specific state and parameters
typedef struct REQUEST_struct
{
   struct REQUEST_struct * pNext;

   SRV_REQ_STATE   state;
   EPGNETIO_STATE  io;
   bool            isLocalCon;
   bool            endianSwap;

   bool            doTtxStats;
   bool            doVpsPdc;
   time32_t        sendDbUpdTime;
   uint            statsReqBits;

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

#define SRV_REPLY_TIMEOUT       60
#define SRV_STATS_IND_INTV_HIGH  2
#define SRV_STATS_IND_INTV_LOW  20

// ----------------------------------------------------------------------------
// Local variables
//
static bool               isDbServer = FALSE;
static EPGDBSRV_STATE   * pReqChain = NULL;
static SRV_CTL_STRUCT     srvState;

// ----------------------------------------------------------------------------
// Build statistics message
// - called periodically after each AI update
//
static void EpgAcqServer_BuildStatsMsg( EPGDBSRV_STATE * req )
{
   EPG_ACQ_VPS_PDC  vpsPdc;
   EPG_ACQ_STATS acqStats;
   bool   newVpsPdc;
   uint   msgLen;

   if (EpgAcqCtl_GetAcqStats(&acqStats))
   {
      req->msgBuf.stats_ind.p.pNext   = NULL;
      // always include description of acq state
      EpgAcqCtl_DescribeAcqState(&req->msgBuf.stats_ind.descr);
      newVpsPdc = EpgAcqCtl_GetVpsPdc(&vpsPdc, VPSPDC_REQ_DAEMON, TRUE);

      if (STATS_REQ_MINIMAL(req->statsReqBits))
      {
         req->msgBuf.stats_ind.type = EPGDB_STATS_UPD_TYPE_MINIMAL;
         msgLen = sizeof(req->msgBuf.stats_ind) - sizeof(req->msgBuf.stats_ind.u) + sizeof(req->msgBuf.stats_ind.u.minimal);

         if (newVpsPdc)
            req->msgBuf.stats_ind.u.minimal.vpsPdc = vpsPdc;
         else
            req->msgBuf.stats_ind.u.minimal.vpsPdc.cniType = STATS_IND_INVALID_VPS_PDC;
      }
      else
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

      EpgNetIo_WriteMsg(&req->io, MSG_TYPE_STATS_IND, msgLen, &req->msgBuf.stats_ind, FALSE);
   }

   req->doTtxStats = FALSE;
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
   char * pMsg = (char*) xmalloc(10000);
   EPGACQ_DESCR acqState;
   EPG_ACQ_STATS sv;
   EPG_ACQ_VPS_PDC vpsPdc;
   //bool newVpsPdc;
   const char * pAcqModeStr;
   const char * pAcqPasvStr;
   uint off = 0;

   EpgAcqCtl_DescribeAcqState(&acqState);
   EpgAcqCtl_GetAcqStats(&sv);
   /*newVpsPdc =*/ EpgAcqCtl_GetVpsPdc(&vpsPdc, VPSPDC_REQ_DAEMON, TRUE);

   EpgAcqCtl_GetAcqModeStr(&acqState, &pAcqModeStr, &pAcqPasvStr, NULL);
   off += snprintf(pMsg + off, 10000 - off, "Acq mode:             %s\n", pAcqModeStr);
   if (pAcqPasvStr != NULL)
      off += snprintf(pMsg + off, 10000 - off, "Passive reason:       %s\n", pAcqPasvStr);

   off += snprintf(pMsg + off, 10000 - off, "Channel VPS/PDC CNI:  %04X\n"
                                            "Channel VPS/PDC PIL:  %02d.%02d. %02d:%02d\n",
                                     vpsPdc.cni,
                                     (vpsPdc.pil >> 15) & 0x1F, (vpsPdc.pil >> 11) & 0x0F,
                                     (vpsPdc.pil >>  6) & 0x1F, (vpsPdc.pil) & 0x3F);

   off += snprintf(pMsg + off, 10000 - off, "Acq runtime:          %02d:%02d (of %02d:%02d)\n"
                                            "Channels done:        %d of %d\n"
                                            "Channels in work:     %d\n",
                                     (sv.acqRuntime / 60), (sv.acqRuntime % 60),
                                        (sv.acqDuration / 60), (sv.acqDuration % 60),
                                     acqState.ttxGrabDone, acqState.ttxSrcCount,
                                     acqState.ttxGrabCount);

   return pMsg;
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

      req = (EPGDBSRV_STATE*) xmalloc(sizeof(EPGDBSRV_STATE));
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
      case MSG_TYPE_VPS_PDC_IND:
      case MSG_TYPE_DB_UPD_IND:
      case MSG_TYPE_STATS_IND:
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
            char * pStr = (char*) xmalloc(20);
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
            req->msgBuf.con_cnf.endianMagic           = PROTOCOL_ENDIAN_MAGIC;
            req->msgBuf.con_cnf.protocolCompatVersion = PROTOCOL_COMPAT;
            req->msgBuf.con_cnf.swVersion             = EPG_VERSION_NO;
#ifndef WIN32
            req->msgBuf.con_cnf.daemon_pid            = getpid();
#else
            req->msgBuf.con_cnf.daemon_pid            = 0;
#endif
#ifdef USE_32BIT_COMPAT
            req->msgBuf.con_cnf.use_32_bit_compat     = TRUE;
#else
            req->msgBuf.con_cnf.use_32_bit_compat     = FALSE;
#endif
            EpgNetIo_WriteMsg(&req->io, MSG_TYPE_CONNECT_CNF, sizeof(req->msgBuf.con_cnf), &req->msgBuf.con_cnf, FALSE);

            req->statsReqBits = 0;
            req->doTtxStats = TRUE;

            req->state = SRV_STATE_FORWARD;
            result = TRUE;
         }
         break;

      case MSG_TYPE_STATS_REQ:
         // update stats request bits: just copy the value, no confirmation sent
         // this message is accepted in all states
         if (req->state == SRV_STATE_FORWARD)
         {
            // send statistics if extended reports were requested
            if ( !STATS_REQ_MINIMAL(pMsg->stats_req.statsReqBits) &&
                 STATS_REQ_MINIMAL(req->statsReqBits) )
            {
               req->doTtxStats = TRUE;
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
          (req->doTtxStats) ||
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

         if ( (req->doVpsPdc) &&
              (req->state == SRV_STATE_FORWARD) )
         {  // send VPS/PDC
            EpgAcqServer_BuildVpsPdcMsg(req);
         }
         else if ( (req->sendDbUpdTime != 0) &&
                   (req->state == SRV_STATE_FORWARD) )
         {
            EpgAcqServer_BuildDbUpdateMsg(req);
         }
         else if ( (req->doTtxStats) && (req->state == SRV_STATE_FORWARD) )
         {  // statistics are due: include them now if either the block queue is empty
            // or next is an AI block (but suppress stats during provider change)
            EpgAcqServer_BuildStatsMsg(req);
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
           (req->state == SRV_STATE_WAIT_CON_REQ) )
      {
         debug2("EpgAcqServer-HandleSockets: fd %d: protocol timeout in state %d", req->io.sock_fd, req->state);
         EpgAcqServer_Close(req, FALSE);
      }
      else if ( (now > req->io.lastIoTime
                         + ( ((req->statsReqBits & STATS_REQ_BITS_HIGH_FREQ) != 0)
                             ? SRV_STATS_IND_INTV_HIGH : SRV_STATS_IND_INTV_LOW )) &&
                (req->state == SRV_STATE_FORWARD) &&
                EpgNetIo_IsIdle(&req->io) )
      {
         dprintf1("EpgAcqServer-HandleSockets: fd %d: send 'no reception' stats\n", req->io.sock_fd);
         req->io.lastIoTime = now;
         EpgAcqServer_BuildStatsMsg(req);
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
      srvState.listen_ip = (char*) xmalloc(strlen(pIpStr) + 1);
      strcpy(srvState.listen_ip, pIpStr);
   }
   if (pPort != NULL)
   {
      srvState.listen_port = (char*) xmalloc(strlen(pPort) + 1);
      strcpy(srvState.listen_port, pPort);
   }
   srvState.do_tcp_ip = do_tcp_ip;
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
         req->doTtxStats = TRUE;
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

#endif // USE_DAEMON
