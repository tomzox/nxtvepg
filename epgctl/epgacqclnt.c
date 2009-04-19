/*
 *  Nextview EPG network acquisition client
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
 *    This module allows a nxtvepg client to connect to an acquisition
 *    server and request forwarding of incoming EPG blocks and to
 *    retrieve acq status information.
 *
 *  Author:
 *          Tom Zoerner
 *
 *  $Id: epgacqclnt.c,v 1.25 2009/03/29 19:16:54 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#ifdef USE_DAEMON

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef WIN32
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgctl/epgversion.h"
#include "epgvbi/syserrmsg.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgswap.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgnetio.h"
#include "epgdb/epgdbmgmt.h"
#include "epgui/epgmain.h"
#include "epgui/dumpraw.h"
#include "epgui/uictrl.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgctxctl.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"


// ---------------------------------------------------------------------------
// Declaration of module state variables

typedef enum
{
   CLNT_STATE_OFF,
   CLNT_STATE_ERROR,
   CLNT_STATE_RETRY,
   CLNT_STATE_WAIT_CONNECT,
   CLNT_STATE_WAIT_CON_CNF,
   CLNT_STATE_WAIT_FWD_CNF,
   CLNT_STATE_WAIT_DUMP,
   CLNT_STATE_WAIT_BLOCKS,
   CLNT_STATE_COUNT
} CLNT_STATE;

typedef struct
{
   CLNT_STATE               state;
   EPGNETIO_STATE           io;
   int                      daemonPid;
   bool                     endianSwap;
   ulong                    rxTotal;
   ulong                    rxStartTime;
   char                     *pSrvHost;
   char                     *pSrvPort;
   char                     *pErrorText;

   EPGDB_CONTEXT            *pAcqDbContext;
   EPGDB_QUEUE              acqDbQueue;
   EPGDB_PI_TSC             acqTscQueue;
   EPG_ACQ_STATS            acqStats;
   EPG_ACQ_VPS_PDC          acqVpsPdc;
   uint                     acqVpsPdcInd;
   EPGACQ_DESCR             acqDescr;
   EPGDBSRV_MSG_BODY        *pStatsMsg;
   uint                     fwdProvCni;
   uint                     statsReqBits;
   bool                     statsReqUpdate;
   bool                     acqContextIsPeek;
   uint                     cniCount;
   uint                     provCnis[MAX_MERGED_DB_COUNT];
   bool                     provUpdate;
   bool                     waitAi;
   void                     (* pCbUpdateEvHandler) ( EPGACQ_EVHAND * pAcqEv );

} CLNT_CTL_STRUCT;

#define CLNT_RETRY_INTERVAL     20
#define CLNT_MAX_MSG_LOOP_COUNT 50

#define SRV_REPLY_TIMEOUT       60

// when very many new blocks are in the queue, connection to UI must be locked
#define EPG_QUEUE_OVERFLOW_LEN  250

// ----------------------------------------------------------------------------
// Local variables
//
static CLNT_CTL_STRUCT    clientState;
static EPGDBSRV_MSG_BODY  clientMsg;

// forward declarations
static void EpgAcqClient_SwitchDb( uint acqCni );
static void EpgAcqClient_CloseDb( void );

// ----------------------------------------------------------------------------
// Open client connection
// - automatically chooses the optimum transport: TCP/IP or pipe for local
// - since the socket is made non-blocking, the result of the connect is not
//   yet available when the function finishes; the caller has to wait for
//   completion with select() and then query the socket error status
//
static void EpgAcqClient_ConnectServer( void )
{
   bool use_tcp_ip;
   int  sock_fd;

   // clear any old error messages
   SystemErrorMessage_Set(&clientState.pErrorText, 0, NULL);

   // check if a server address has been configured
   if ((clientState.pSrvHost != NULL) && (clientState.pSrvPort != NULL))
   {
      // switch automatically to UNIX domain sockets (i.e. pipes) for a local server
      // (note: can be avoided by using 127.0.0.1 as server hostname)
      #ifndef WIN32
      use_tcp_ip = (EpgAcqClient_IsLocalServer() == FALSE);
      #else
      // XXX TODO: use named pipes on windows
      use_tcp_ip = TRUE;
      #endif

      sock_fd = EpgNetIo_ConnectToServer(use_tcp_ip, clientState.pSrvHost, clientState.pSrvPort, &clientState.pErrorText);
      if (sock_fd != -1)
      {
         // initialize IO state
         memset(&clientState.io, 0, sizeof(clientState.io));
         clientState.io.sock_fd    = sock_fd;
         clientState.io.lastIoTime = time(NULL);
         clientState.rxStartTime   = clientState.io.lastIoTime;
         clientState.rxTotal       = 0;
      }
   }
   else
   {
      debug0("EpgDbClient-ConnectServer: Hostname or port not configured");
      if (clientState.pSrvHost == NULL)
         SystemErrorMessage_Set(&clientState.pErrorText, 0, "Server hostname not configured", NULL);
      else if (clientState.pSrvPort == NULL)
         SystemErrorMessage_Set(&clientState.pErrorText, 0, "Server service name (aka port) not configured", NULL);
   }
}

// ----------------------------------------------------------------------------
// Free all stats reports in the queue
//
static void EpgAcqClient_FreeStats( void )
{
   EPGDBSRV_MSG_BODY  * pWalk;
   EPGDBSRV_MSG_BODY  * pNext;

   pWalk = clientState.pStatsMsg;
   while (pWalk != NULL)
   {
      pNext = pWalk->stats_ind.p.pNext;
      xfree(pWalk);
      pWalk = pNext;
   }
   clientState.pStatsMsg = NULL;
}

// ----------------------------------------------------------------------------
// Swap all struct elements > 1 byte for non-endian-matching databases
//
static void EpgAcqClient_SwapEpgAcqDescr( EPGACQ_DESCR * pDescr )
{
   swap32(&pDescr->nxtvState);
   swap32(&pDescr->ttxGrabState);
   swap32(&pDescr->mode);
   swap32(&pDescr->cyclePhase);
   swap32(&pDescr->passiveReason);
   swap32(&pDescr->nxtvDbCni);
   swap32(&pDescr->cycleCni);
   swap16(&pDescr->cniCount);
   swap16(&pDescr->ttxSrcCount);
   swap16(&pDescr->ttxGrabIdx);
   swap16(&pDescr->ttxGrabDone);
}

static void EpgAcqClient_SwapEpgdbBlockCount( EPGDB_BLOCK_COUNT * pCounts )
{
   uint  idx;

   for (idx=0; idx < 2; idx++, pCounts++)
   {
      swap32(&pCounts->ai);
      swap32(&pCounts->curVersion);
      swap32(&pCounts->allVersions);
      swap32(&pCounts->expired);
      swap32(&pCounts->defective);
      swap32(&pCounts->extra);
      swap32(&pCounts->sinceAcq);

      swap64(&pCounts->variance);
      swap64(&pCounts->avgAcqRepCount);
   }
}

static void EpgAcqClient_SwapEpgdbAcqAiStats( EPGDB_ACQ_AI_STATS * pAiStats )
{
   swap32(&pAiStats->lastAiTime);
   swap32(&pAiStats->minAiDistance);
   swap32(&pAiStats->maxAiDistance);
   swap32(&pAiStats->sumAiDistance);
   swap32(&pAiStats->aiCount);
}

static void EpgAcqClient_SwapEpgdbVarHist( EPGDB_VAR_HIST * pVarHist )
{
   uint  varIdx;
   uint  loopIdx;

   for (loopIdx=0; loopIdx < 2; loopIdx++, pVarHist++)
   {
      for (varIdx=0; varIdx < VARIANCE_HIST_COUNT; varIdx++)
         swap64(&pVarHist->buf[varIdx]);
      swap16(&pVarHist->count);
      swap16(&pVarHist->lastIdx);
   }
}

// ----------------------------------------------------------------------------
// Checks the size of a message from server to client
//
static bool EpgAcqClient_CheckMsg( uint len, EPGNETIO_MSG_HEADER * pHead, EPGDBSRV_MSG_BODY * pBody )
{
   EPGDB_BLOCK * pNewBlock;
   uint idx;
   bool result = FALSE;

   switch (pHead->type)
   {
      case MSG_TYPE_CONNECT_CNF:
         if ( (len == sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->con_cnf)) &&
              (memcmp(pBody->con_cnf.magic, MAGIC_STR, MAGIC_STR_LEN) == 0) )
         {
            if (pBody->con_cnf.endianMagic == PROTOCOL_ENDIAN_MAGIC)
            {  // endian type matches -> no swapping required
               clientState.endianSwap = FALSE;
               result                 = TRUE;
            }
            else if (pBody->con_cnf.endianMagic == PROTOCOL_WRONG_ENDIAN)
            {  // endian type does not match -> convert "endianess" of all msg elements > 1 byte
               swap32(&pBody->con_cnf.blockCompatVersion);
               swap32(&pBody->con_cnf.protocolCompatVersion);
               swap32(&pBody->con_cnf.swVersion);

               // enable byte swapping for all following messages
               clientState.endianSwap = TRUE;
               result                 = TRUE;
            }
         }
         break;

      case MSG_TYPE_FORWARD_CNF:
         if (len == sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->fwd_cnf))
         {
            if (clientState.endianSwap)
            {
               swap32(&pBody->fwd_cnf.cniCount);
               if (pBody->fwd_cnf.cniCount <= MAX_MERGED_DB_COUNT)
               {
                  for (idx=0; idx < pBody->fwd_cnf.cniCount; idx++)
                  {
                     swap32(&pBody->fwd_cnf.provCnis[idx]);
                  }
                  result = TRUE;
               }
            }
            else if (pBody->fwd_cnf.cniCount <= MAX_MERGED_DB_COUNT)
            {
               result = TRUE;
            }
         }
         break;

      case MSG_TYPE_FORWARD_IND:
         if (len == sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->fwd_ind))
         {
            if (clientState.endianSwap)
            {
               swap32(&pBody->fwd_ind.cni);
            }
            result = TRUE;
         }
         break;

      case MSG_TYPE_DUMP_IND:
         if (len == sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->dump_ind));
         {
            if (clientState.endianSwap)
            {
               swap32(&pBody->dump_ind.cni);
            }
            result = TRUE;
         }
         break;

      case MSG_TYPE_BLOCK_IND:
#ifdef USE_32BIT_COMPAT
         pNewBlock = (EPGDB_BLOCK *)((uint8_t *)pBody + BLK_HEAD_SIZE_DATA - BLK_UNION_OFF);
         if (pHead->len >= sizeof(EPGNETIO_MSG_HEADER) + BLK_HEAD_SIZE_DATA)
#else
         pNewBlock = (EPGDB_BLOCK *) pBody;
         if (pHead->len >= sizeof(EPGNETIO_MSG_HEADER) + BLK_UNION_OFF)
#endif
         {
            uint32_t blockSize = pNewBlock->size;
            uint32_t checkSize;
            if (clientState.endianSwap)
               swap32(&blockSize);
#ifdef USE_32BIT_COMPAT
            checkSize = sizeof(EPGNETIO_MSG_HEADER) + blockSize + BLK_HEAD_SIZE_DATA;
#else
            checkSize = sizeof(EPGNETIO_MSG_HEADER) + blockSize + BLK_UNION_OFF;
#endif
            if (pHead->len == checkSize)
            {
               result = (!clientState.endianSwap || EpgBlockSwapEndian(pNewBlock)) &&
                        EpgBlockCheckConsistancy(pNewBlock);
            }
            else
               debug2("EpgAcqClient-CheckMsg: BLOCK_IND msg len %d != block size=%d", pHead->len, checkSize);
         }
         else
            debug1("EpgAcqClient-CheckMsg: BLOCK_IND msg too short: len=%d", pHead->len);
         break;

      case MSG_TYPE_TSC_IND:
         if (len > sizeof(EPGNETIO_MSG_HEADER))
         {
            EPGDB_PI_TSC_BUF * pTscBuf = (EPGDB_PI_TSC_BUF *) pBody;

            if (clientState.endianSwap)
            {
               swap32(&pTscBuf->provCni);
               swap16(&pTscBuf->fillCount);
               swap16(&pTscBuf->popIdx);
               swap32(&pTscBuf->baseTime);
            }
            if ( (pTscBuf->fillCount <= PI_TSC_GET_BUF_COUNT(pTscBuf->mode)) &&
                 (len == sizeof(EPGNETIO_MSG_HEADER) + PI_TSC_GET_BUF_SIZE(pTscBuf->fillCount)) )
            {
               if (clientState.endianSwap)
               {
                  EPGDB_PI_TSC_ELEM  * pTscElem = pTscBuf->pi;
                  uint  idx;

                  for (idx = 0; idx < pTscBuf->fillCount; idx++, pTscElem++)
                  {
                     swap16(&pTscElem->startOffMins);
                     swap16(&pTscElem->durationMins);
                  }
               }
               result = TRUE;
            }
            else
               debug3("EpgAcqClient-CheckMsg: TSC_IND msg len %d too short for %d entries (expected %d)", len, pTscBuf->fillCount, PI_TSC_GET_BUF_SIZE(pTscBuf->fillCount));
         }
         else
            debug1("EpgAcqClient-CheckMsg: TSC_IND msg len %d too short", len);
         break;

      case MSG_TYPE_VPS_PDC_IND:
         if (len == sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->vps_pdc_ind))
         {
            if (clientState.endianSwap)
            {
               swap32(&pBody->vps_pdc_ind.vpsPdc.cni);
               swap32(&pBody->vps_pdc_ind.vpsPdc.pil);
            }
            result = TRUE;
         }
         break;

      case MSG_TYPE_DB_UPD_IND:
         if (len == sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->db_upd_ind))
         {
            if (clientState.endianSwap)
            {
               swap32(&pBody->db_upd_ind.mtime);
            }
            result = TRUE;
         }
         break;

      case MSG_TYPE_STATS_IND:
         if (len > sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->stats_ind) - sizeof(pBody->stats_ind.u))
         {
            uint size = len - (sizeof(EPGNETIO_MSG_HEADER) + sizeof(pBody->stats_ind) - sizeof(pBody->stats_ind.u));

            if (clientState.endianSwap)
            {
               swap32(&pBody->stats_ind.type);
               EpgAcqClient_SwapEpgAcqDescr(&pBody->stats_ind.descr);
            }

            switch (pBody->stats_ind.type)
            {
               case EPGDB_STATS_UPD_TYPE_MINIMAL:
                  if (size == sizeof(pBody->stats_ind.u.minimal))
                  {
                     if (clientState.endianSwap)
                     {
                        EpgAcqClient_SwapEpgdbBlockCount(pBody->stats_ind.u.minimal.count);
                        swap32(&pBody->stats_ind.u.minimal.nowMaxAcqNetCount);
                        swap32(&pBody->stats_ind.u.minimal.vpsPdc.cni);
                        swap32(&pBody->stats_ind.u.minimal.vpsPdc.pil);
                        swap32(&pBody->stats_ind.u.minimal.lastAiTime);
                     }
                     result = TRUE;
                  }
                  else
                     debug2("EpgAcqClient-CheckMsg: STATS_IND type MINIMAL illegal msg len %d != %d", size, (uint)sizeof(pBody->stats_ind.u.minimal));
                  break;
               case EPGDB_STATS_UPD_TYPE_INITIAL:
                  if (size == sizeof(pBody->stats_ind.u.initial))
                  {
                     if (clientState.endianSwap)
                     {
                        swap32(&pBody->stats_ind.u.initial.stats.nxtv.acqStartTime);
                        swap32(&pBody->stats_ind.u.initial.stats.lastStatsUpdate);
                        EpgAcqClient_SwapEpgdbAcqAiStats(&pBody->stats_ind.u.initial.stats.nxtv.ai);
                        swap32(&pBody->stats_ind.u.initial.stats.ttx_duration);
                        for (idx=0; idx < sizeof(pBody->stats_ind.u.initial.stats.ttx_dec) / sizeof(uint32_t); idx++)
                           swap32(((uint32_t *)&pBody->stats_ind.u.initial.stats.ttx_dec) + idx);
                        for (idx=0; idx < sizeof(pBody->stats_ind.u.initial.stats.nxtv.stream) / sizeof(uint32_t); idx++)
                           swap32(((uint32_t *)&pBody->stats_ind.u.initial.stats.nxtv.stream) + idx);
                        swap16(&pBody->stats_ind.u.initial.stats.nxtv.histIdx);
                        EpgAcqClient_SwapEpgdbBlockCount(pBody->stats_ind.u.initial.stats.nxtv.count);
                        EpgAcqClient_SwapEpgdbVarHist(pBody->stats_ind.u.initial.stats.nxtv.varianceHist);
                        swap32(&pBody->stats_ind.u.initial.stats.nxtv.nowMaxAcqRepCount);
                        swap32(&pBody->stats_ind.u.initial.stats.nxtv.nowMaxAcqNetCount);
                        swap32(&pBody->stats_ind.u.initial.vpsPdc.cni);
                        swap32(&pBody->stats_ind.u.initial.vpsPdc.pil);
                        // teletext grabber
                        swap32(&pBody->stats_ind.u.initial.stats.ttx_grab.acqStartTime);
                        swap32(&pBody->stats_ind.u.initial.stats.ttx_grab.srcIdx);
                        for (idx=0; idx < sizeof(pBody->stats_ind.u.initial.stats.ttx_grab.pkgStats) / sizeof(uint32_t); idx++)
                           swap32(((uint32_t *)&pBody->stats_ind.u.initial.stats.ttx_grab.pkgStats) + idx);
                     }
                     result = TRUE;
                  }
                  else
                     debug2("EpgAcqClient-CheckMsg: STATS_IND type INITIAL illegal msg len %d != %d", size, (uint)sizeof(pBody->stats_ind.u.initial));
                  break;
               case EPGDB_STATS_UPD_TYPE_UPDATE:
                  if (size == sizeof(pBody->stats_ind.u.update))
                  {
                     if (clientState.endianSwap)
                     {
                        EpgAcqClient_SwapEpgdbBlockCount(pBody->stats_ind.u.update.count);
                        EpgAcqClient_SwapEpgdbAcqAiStats(&pBody->stats_ind.u.update.ai);
                        swap32(&pBody->stats_ind.u.update.vpsPdc.cni);
                        swap32(&pBody->stats_ind.u.update.vpsPdc.pil);
                        swap16(&pBody->stats_ind.u.update.histIdx);
                        swap32(&pBody->stats_ind.u.update.nowMaxAcqRepCount);
                        swap32(&pBody->stats_ind.u.update.nowMaxAcqNetCount);
                        swap32(&pBody->stats_ind.u.update.ttx_duration);
                        swap32(&pBody->stats_ind.u.update.lastStatsUpdate);
                        for (idx=0; idx < sizeof(pBody->stats_ind.u.update.ttx_dec) / sizeof(uint32_t); idx++)
                           swap32(((uint32_t *)&pBody->stats_ind.u.update.ttx_dec) + idx);
                        for (idx=0; idx < sizeof(pBody->stats_ind.u.update.stream) / sizeof(uint32_t); idx++)
                           swap32(((uint32_t *)&pBody->stats_ind.u.update.stream) + idx);
                        // teletext grabber
                        for (idx=0; idx < sizeof(pBody->stats_ind.u.update.grabTtxStats) / sizeof(uint32_t); idx++)
                           swap32(((uint32_t *)&pBody->stats_ind.u.update.grabTtxStats) + idx);
                     }
                     result = TRUE;
                  }
                  else
                     debug2("EpgAcqClient-CheckMsg: STATS_IND type UPDATE illegal msg len %d != %d", size, (uint)sizeof(pBody->stats_ind.u.update));
                  break;
               default:
                  debug1("EpgAcqClient-CheckMsg: STATS_IND illegal type %d", pBody->stats_ind.type);
                  result = FALSE;
                  break;
            }
         }
         else
            result = FALSE;
         break;

      case MSG_TYPE_CLOSE_IND:
         result = (len == sizeof(EPGNETIO_MSG_HEADER));
         break;

      case MSG_TYPE_CONQUERY_CNF:
         debug0("EpgAcqClient-CheckMsg: Unexpected CONQUERY_CNF");
         break;

      case MSG_TYPE_CONNECT_REQ:
      case MSG_TYPE_FORWARD_REQ:
      case MSG_TYPE_STATS_REQ:
         debug1("EpgAcqClient-CheckMsg: recv server msg type %d", pHead->type);
         result = FALSE;
         break;
      default:
         debug1("EpgAcqClient-CheckMsg: unknown msg type %d", pHead->type);
         result = FALSE;
         break;
   }

   ifdebug2(result==FALSE, "EpgAcqClient-CheckMsg: illegal msg len %d for type %d", len, pHead->type);

   return result;
}

// ----------------------------------------------------------------------------
// Handle message from server
//
static bool EpgAcqClient_TakeMessage( EPGACQ_EVHAND * pAcqEv, EPGDBSRV_MSG_BODY * pMsg )
{
   uint dbIdx;
   bool result = FALSE;

   //if (clientState.io.readHeader.type != MSG_TYPE_BLOCK_IND) //XXX
   dprintf2("EpgDbClient-TakeMessage: recv msg type %d, len %d\n", clientState.io.readHeader.type, clientState.io.readHeader.len);

   switch (clientState.io.readHeader.type)
   {
      case MSG_TYPE_CONNECT_CNF:
         if (clientState.state == CLNT_STATE_WAIT_CON_CNF)
         {
            dprintf3("EpgDbClient-TakeMessage: CONNECT_CNF: reply version %x, dump %x protocol %x\n", pMsg->con_cnf.swVersion, pMsg->con_cnf.blockCompatVersion, pMsg->con_cnf.protocolCompatVersion);
            // first server message received: contains version info
            // note: nxtvepg and endian magics are already checked
            if ( (pMsg->con_cnf.blockCompatVersion != DUMP_COMPAT) ||
#ifdef USE_32BIT_COMPAT
                 (pMsg->con_cnf.use_32_bit_compat == FALSE) ||
#else
                 (pMsg->con_cnf.use_32_bit_compat) ||
#endif
                 (pMsg->con_cnf.protocolCompatVersion != PROTOCOL_COMPAT) )
            {
               SystemErrorMessage_Set(&clientState.pErrorText, 0, "Incompatible server version", NULL);
            }
#ifdef USE_UTF8
            else if (pMsg->con_cnf.use_utf8 == FALSE)
            {
               SystemErrorMessage_Set(&clientState.pErrorText, 0, "Incompatible text encoding (not UTF-8)", NULL);
            }
#else
            else if (pMsg->con_cnf.use_utf8)
            {
               SystemErrorMessage_Set(&clientState.pErrorText, 0, "Incompatible text encoding (server uses UTF-8)", NULL);
            }
#endif
            else
            {  // version ok -> request block forwarding
               clientState.daemonPid = pMsg->con_cnf.daemon_pid;
               memcpy(&clientMsg.fwd_req.provCnis, clientState.provCnis, sizeof(clientMsg.fwd_req.provCnis));
               for (dbIdx=0; dbIdx < clientState.cniCount; dbIdx++)
                  clientMsg.fwd_req.dumpStartTimes[dbIdx] = EpgContextCtl_GetAiUpdateTime(clientState.provCnis[dbIdx], FALSE);
               clientMsg.fwd_req.cniCount     = clientState.cniCount;
               clientMsg.fwd_req.statsReqBits = clientState.statsReqBits;
               // if VPS/PDC was requested, force an immediate update
               if ((clientState.statsReqBits & STATS_REQ_BITS_VPS_PDC_REQ) != 0)
                  clientMsg.fwd_req.statsReqBits |= STATS_REQ_BITS_VPS_PDC_UPD;
               EpgNetIo_WriteMsg(&clientState.io, MSG_TYPE_FORWARD_REQ, sizeof(clientMsg.fwd_req), &clientMsg.fwd_req, FALSE);
               pAcqEv->blockOnWrite = pAcqEv->blockOnRead = FALSE;

               clientState.state = CLNT_STATE_WAIT_FWD_CNF;
               clientState.statsReqUpdate = FALSE;
               result = TRUE;
            }
         }
         break;

      case MSG_TYPE_FORWARD_CNF:
         // server confirms he restarts for a new provider request list
         if (clientState.state == CLNT_STATE_WAIT_FWD_CNF)
         {
            // drop the reply if it doesn't refer to the latest CNI list
            if ( (pMsg->fwd_cnf.cniCount == clientState.cniCount) &&
                 (memcmp(pMsg->fwd_cnf.provCnis, clientState.provCnis, sizeof(uint) * clientState.cniCount) == 0) )
            {
               dprintf0("EpgDbClient-TakeMessage: FORWARD_CNF\n");
               // this message carries no information for the client, not is it replied to
               // it's purpose is to synchronize server and client
               clientState.state = CLNT_STATE_WAIT_DUMP;

               if (EpgDbContextGetCni(clientState.pAcqDbContext) == 0)
               {  // initial connect -> update network state
                  UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
               }
            }
            else
               debug4("EpgDbClient-TakeMessage: FORWARD_CNF: param mismatch msg/clnt: count=%d/%d, CNI=%04X/%04X", pMsg->fwd_cnf.cniCount, clientState.cniCount, pMsg->fwd_cnf.provCnis[0], clientState.provCnis[0]);

            result = TRUE;
         }
         break;

      case MSG_TYPE_DUMP_IND:
         // server starts to dump a provider database delta
         if (clientState.state == CLNT_STATE_WAIT_DUMP)
         {
            // do nothing here (the message is currently unused)
            result = TRUE;
         }
         else if (clientState.state == CLNT_STATE_WAIT_FWD_CNF)
         {  // ignore message (may arrive after provider change)
            result = TRUE;
         }
         break;

      case MSG_TYPE_FORWARD_IND:
         if (clientState.state == CLNT_STATE_WAIT_DUMP)
         {  // dumps for all requested databases are finished
            // -> next time "process blocks" function is invoked insert all cached EPG blocks
            dprintf1("EpgDbClient-TakeMessage: FORWARD_IND: initial dump finished CNI:0x%04X\n", pMsg->fwd_ind.cni);
            clientState.fwdProvCni = pMsg->fwd_ind.cni;
            clientState.state = CLNT_STATE_WAIT_BLOCKS;
            result = TRUE;
         }
         else if (clientState.state == CLNT_STATE_WAIT_BLOCKS)
         {  // unused message
            dprintf1("EpgDbClient-TakeMessage: FORWARD_IND: acq prov switch dump finished CNI:0x%04X\n", pMsg->fwd_ind.cni);
            clientState.fwdProvCni = pMsg->fwd_ind.cni;
            result = TRUE;
         }
         else if (clientState.state == CLNT_STATE_WAIT_FWD_CNF)
         {  // ignore message (may arrive after a provider change)
            result = TRUE;
         }
         break;

      case MSG_TYPE_BLOCK_IND:
         if ( (clientState.state == CLNT_STATE_WAIT_DUMP) ||
              (clientState.state == CLNT_STATE_WAIT_BLOCKS) )
         {
            EPGDB_BLOCK * pNewBlock;
#ifdef USE_32BIT_COMPAT
            uint blkSize;

            pNewBlock = (EPGDB_BLOCK *)((uint8_t *)pMsg + BLK_HEAD_SIZE_DATA - BLK_UNION_OFF);
            blkSize = pNewBlock->size;

            pNewBlock = xmalloc(blkSize + BLK_HEAD_SIZE_MEM);
            memcpy((uchar *)pNewBlock + BLK_HEAD_PTR_OFF_MEM,
                   (uchar *)pMsg, blkSize + BLK_HEAD_SIZE_DATA);
#else
            pNewBlock = (EPGDB_BLOCK *) pMsg;
            // must not free the message because it's added to the EPG block queue
            pMsg = NULL;
#endif

            if (pNewBlock->type == BLOCK_TYPE_AI)
            {
               dprintf1("EpgDbClient-TakeMessage: BLOCK_IND: AI block CNI 0x%04x\n", AI_GET_THIS_NET_CNI(&pNewBlock->blk.ai));
               clientState.fwdProvCni = AI_GET_THIS_NET_CNI(&pNewBlock->blk.ai);
            }

            // received new EPG block
            pNewBlock->pNextBlock = NULL;
            pNewBlock->pPrevBlock = NULL;
            pNewBlock->pNextNetwopBlock = NULL;
            pNewBlock->pPrevNetwopBlock = NULL;

            // offer the block to the ASCII dump module
            EpgDumpRaw_IncomingBlock(&pNewBlock->blk, pNewBlock->type, pNewBlock->stream);
            // append the block to the end of the input queue
            EpgDbQueue_Add(&clientState.acqDbQueue, pNewBlock);

            result = TRUE;
         }
         else if (clientState.state == CLNT_STATE_WAIT_FWD_CNF)
         {  // message accepted, but discard the block (is still from the previous provider)
            result = TRUE;
         }
         break;

      case MSG_TYPE_STATS_IND:
         if ( (clientState.state == CLNT_STATE_WAIT_DUMP) ||
              (clientState.state == CLNT_STATE_WAIT_BLOCKS) ||
              (clientState.state == CLNT_STATE_WAIT_FWD_CNF) )
         {
            // note: stats indication cannot be processed immediately; instead it must be
            // processed after all EPG blocks in the queue have been inserted to the db,
            // because they may still belong to the old database; the stats indication
            // however may already refer to the new provider

            EPGDBSRV_MSG_BODY  * pWalk, * pPrev;

            dprintf2("EpgDbClient-TakeMessage: STATS_IND: type=%d, histIdx=%d\n", pMsg->stats_ind.type, pMsg->stats_ind.u.update.histIdx);
            pMsg->stats_ind.p.pNext = NULL;
            // append the message to the queue of stats reports
            pPrev = NULL;
            pWalk = clientState.pStatsMsg;
            while (pWalk != NULL)
            {
               pPrev = pWalk;
               pWalk = pWalk->stats_ind.p.pNext;
            }
            if (pPrev != NULL)
               pPrev->stats_ind.p.pNext = pMsg;
            else
               clientState.pStatsMsg = pMsg;
            // must not free the message yet
            pMsg = NULL;
            result = TRUE;
         }
         break;

      case MSG_TYPE_VPS_PDC_IND:
         // VPS/PDC: accepted in all states, because it's independent of the acq provider
         if (clientState.statsReqBits & STATS_REQ_BITS_VPS_PDC_REQ)
         {
            clientState.statsReqBits &= ~ STATS_REQ_BITS_VPS_PDC_UPD;
            clientState.acqVpsPdc = pMsg->vps_pdc_ind.vpsPdc;
            clientState.acqVpsPdcInd += 1;
            UiControlMsg_AcqEvent(ACQ_EVENT_VPS_PDC);
         }
         else  // message accepted, but discarded (forward was disabled)
            dprintf0("EpgDbClient-TakeMessage: discarding VPS/PDC info\n");
         result = TRUE;
         break;

      case MSG_TYPE_DB_UPD_IND:
         // note: mtime parameter is currently unused
         UiControlMsg_AcqEvent(ACQ_EVENT_NEW_DB);
         result = TRUE;
         break;

      case MSG_TYPE_TSC_IND:
         // note: cannot check here if msg must be dropped when only ALL bit was cleared -> done in GUI
         if (clientState.statsReqBits & STATS_REQ_BITS_TSC_REQ)
         {
            if (EpgTscQueue_PushBuffer(&clientState.acqTscQueue, (EPGDB_PI_TSC_BUF *) pMsg,
                                       clientState.io.readHeader.len - sizeof(EPGNETIO_MSG_HEADER)))
            {
               // must not free the message because it's been added to the tsc queue
               pMsg = NULL;
               result = TRUE;
            }
         }
         else
         {  // message accepted, but discarded (PI timescale forward was disabled)
            dprintf0("EpgDbClient-TakeMessage: discarding TSC buffer\n");
            result = TRUE;
         }
         break;

      case MSG_TYPE_CLOSE_IND:
         break;

      case MSG_TYPE_CONNECT_REQ:
      case MSG_TYPE_FORWARD_REQ:
      case MSG_TYPE_STATS_REQ:
      default:
         break;
   }

   if ((result == FALSE) && (clientState.pErrorText == NULL))
   {
      debug3("EpgDbClient-TakeMessage: message type %d (len %d) not expected in state %d", clientState.io.readHeader.type, clientState.io.readHeader.len, clientState.state);
      SystemErrorMessage_Set(&clientState.pErrorText, 0, "Protocol error (unecpected message)", NULL);
   }
   if (pMsg != NULL)
      xfree(pMsg);

   return result;
}

// ----------------------------------------------------------------------------
// Close client connection
//
static void EpgAcqClient_Close( bool removeHandler )
{
   EPGACQ_EVHAND  acqEv;

   EpgNetIo_CloseIO(&clientState.io);

   memset(&clientState.io, 0, sizeof(clientState.io));
   clientState.io.sock_fd    = -1;
   clientState.io.lastIoTime = time(NULL);

   // free all EPG blocks in the input queue
   EpgDbQueue_Clear(&clientState.acqDbQueue);
   EpgTscQueue_Clear(&clientState.acqTscQueue);

   EpgAcqClient_FreeStats();

   if (removeHandler)
   {
      memset(&acqEv, 0, sizeof(acqEv));
      acqEv.fd = -1;
      clientState.pCbUpdateEvHandler(&acqEv);
   }

   if (clientState.state != CLNT_STATE_OFF)
   {
      // enter error state
      // will be changed to retry or off once the error is reported to the upper layer
      clientState.state = CLNT_STATE_ERROR;
   }
}

// ----------------------------------------------------------------------------
// Main client handler
// - called by file handler when network socket is readable or writable
// - accept new incoming messages; advance ongoing I/O; when complete, process
//   messages according to current protocol state
// - don't stop after one message complete; loop until socket blocks,
//   or a maximum message count is reached (allow other events to be processed)
// - most protocol states are protected by a timeout, which is implemented
//   on a polling basis (i.e. the func is called periodically) in a separate
//   check function.
//
void EpgAcqClient_HandleSocket( EPGACQ_EVHAND * pAcqEv )
{
   uint dbIdx;
   uint loopCount;
   bool readable;
   bool read2ndMsg;
   bool ioBlocked;

   readable = pAcqEv->blockOnRead;
   read2ndMsg = TRUE;
   loopCount  = 0;

   #ifdef WIN32
   if (pAcqEv->errCode != ERROR_SUCCESS)
   {
      debug1("EpgAcqClient-HandleSocket: aborting due to select err code %d", pAcqEv->errCode);
      SystemErrorMessage_Set(&clientState.pErrorText, pAcqEv->errCode,
                            ((clientState.state == CLNT_STATE_WAIT_CONNECT) ? "Connect failed: " : "I/O error: "), NULL);
      EpgAcqClient_Close(FALSE);
   }
   pAcqEv->blockOnConnect = TRUE;
   #endif

   if (clientState.state == CLNT_STATE_WAIT_CONNECT)
   {
      if (EpgNetIo_FinishConnect(clientState.io.sock_fd, &clientState.pErrorText))
      {
         // the message contains a magic to allow the server to immediately drop
         // unintended (e.g. wrong service or port) or malicious (e.g. port scanner) connections
         memcpy(clientMsg.con_req.magic, MAGIC_STR, MAGIC_STR_LEN);
         memset(clientMsg.con_req.reserved, 0, sizeof(clientMsg.con_req.reserved));
         clientMsg.con_req.endianMagic = PROTOCOL_ENDIAN_MAGIC;
         EpgNetIo_WriteMsg(&clientState.io, MSG_TYPE_CONNECT_REQ, sizeof(clientMsg.con_req), &clientMsg.con_req, FALSE);

         clientState.state = CLNT_STATE_WAIT_CON_CNF;
      }
      else
      {  // failed to establish a connection to the server
         EpgAcqClient_Close(FALSE);
      }
   }

   if ( (clientState.state != CLNT_STATE_RETRY) &&
        (clientState.state != CLNT_STATE_ERROR) &&
        (clientState.state != CLNT_STATE_WAIT_CONNECT) )
   {
      do
      {
         //XXX//dprintf1("EpgDbClient-HandleSocket: handle client: readable=%d\n", readable);
         if (EpgNetIo_IsIdle(&clientState.io))
         {  // no ongoing I/O -> check if anything needs to be sent and for incoming data
            if (clientState.provUpdate)
            {  // update the provider table
               clientState.io.lastIoTime  = time(NULL);
               clientState.provUpdate     = FALSE;
               clientState.statsReqUpdate = FALSE;

               memcpy(&clientMsg.fwd_req.provCnis, clientState.provCnis, sizeof(clientMsg.fwd_req.provCnis));
               for (dbIdx=0; dbIdx < clientState.cniCount; dbIdx++)
                  clientMsg.fwd_req.dumpStartTimes[dbIdx] = EpgContextCtl_GetAiUpdateTime(clientState.provCnis[dbIdx], FALSE);
               clientMsg.fwd_req.cniCount     = clientState.cniCount;
               clientMsg.fwd_req.statsReqBits = clientState.statsReqBits;
               EpgNetIo_WriteMsg(&clientState.io, MSG_TYPE_FORWARD_REQ, sizeof(clientMsg.fwd_req), &clientMsg.fwd_req, FALSE);
            }
            else if (clientState.statsReqUpdate)
            {
               clientMsg.stats_req.statsReqBits = clientState.statsReqBits;
               EpgNetIo_WriteMsg(&clientState.io, MSG_TYPE_STATS_REQ, sizeof(clientMsg.stats_req), &clientMsg.stats_req, FALSE);

               clientState.io.lastIoTime = time(NULL);
               clientState.statsReqUpdate = FALSE;
            }
            else if (readable || read2ndMsg)
            {  // new incoming data -> start reading
               clientState.io.waitRead = TRUE;
               clientState.io.readLen  = 0;
               clientState.io.readOff  = 0;
               if (readable == FALSE)
                  read2ndMsg = FALSE;
            }
         }

         if (EpgNetIo_HandleIO(&clientState.io, &ioBlocked, readable))
         {
            assert((clientState.io.waitRead == FALSE) || (clientState.io.readOff > 0) || (readable == FALSE));

            if ( (clientState.io.writeLen == 0) &&
                 (clientState.io.readLen != 0) &&
                 (clientState.io.readLen == clientState.io.readOff) )
            {  // message completed and no outstanding I/O

               if (EpgAcqClient_CheckMsg(clientState.io.readLen, &clientState.io.readHeader, (EPGDBSRV_MSG_BODY *) clientState.io.pReadBuf))
               {
                  clientState.rxTotal += clientState.io.readHeader.len;
                  clientState.io.readLen = 0;
                  
                  // process the message - frees the buffer if neccessary
                  if (EpgAcqClient_TakeMessage(pAcqEv, (EPGDBSRV_MSG_BODY *) clientState.io.pReadBuf) == FALSE)
                  {  // protocol error -> abort connection
                     clientState.io.pReadBuf = NULL;
                     EpgAcqClient_Close(FALSE);
                  }
                  else
                     clientState.io.pReadBuf = NULL;
               }
               else
               {  // consistancy error
                  EpgAcqClient_Close(FALSE);
               }

               // check if one more message can be read before leaving the handler
               read2ndMsg = TRUE;
            }
         }
         else
         {  // I/O error; note: acq is not stopped, instead try to reconnect periodically
            SystemErrorMessage_Set(&clientState.pErrorText, 0, "Lost connection (I/O error)", NULL);
            EpgAcqClient_Close(FALSE);
         }
         readable = FALSE;
         loopCount += 1;
      }
      while ( (ioBlocked == FALSE) &&
              (clientState.io.sock_fd != -1) &&
              (loopCount < CLNT_MAX_MSG_LOOP_COUNT) &&
              ( clientState.io.waitRead || (clientState.io.readLen > 0) ||
                (clientState.io.writeLen > 0) ||
                clientState.provUpdate || clientState.statsReqUpdate ||
                read2ndMsg ) );
   }

   // reset "start new block" flag if no data was received using read2ndMsg
   if (clientState.io.readLen == 0)
      clientState.io.waitRead = 0;

   pAcqEv->blockOnWrite = (clientState.io.writeLen > 0);
   pAcqEv->blockOnRead  = !pAcqEv->blockOnWrite;
   pAcqEv->fd           = clientState.io.sock_fd;
   // trigger acq ctl module if EPG blocks or other messages are queued
   pAcqEv->processQueue = ( (EpgDbQueue_GetBlockCount(&clientState.acqDbQueue) > 0) ||
                            EpgTscQueue_HasElems(&clientState.acqTscQueue) ||
                            (clientState.pStatsMsg != NULL) );
}

// ----------------------------------------------------------------------------
// Start network acquisition, i.e. initiate network connection
//
bool EpgAcqClient_Start( void )
{
   EPGACQ_EVHAND   acqEv;

   if (clientState.state == CLNT_STATE_OFF)
   {
      assert(clientState.pAcqDbContext == NULL);

      EpgDbQueue_Init(&clientState.acqDbQueue);
      EpgTscQueue_Init(&clientState.acqTscQueue);

      EpgAcqClient_ConnectServer();
      if (clientState.io.sock_fd != -1)
      {
         clientState.state = CLNT_STATE_WAIT_CONNECT;

         memset(&acqEv, 0, sizeof(acqEv));
         acqEv.fd             = clientState.io.sock_fd;
         acqEv.blockOnWrite   = TRUE;
         acqEv.blockOnConnect = TRUE;
         clientState.pCbUpdateEvHandler(&acqEv);

         // open the dummy database
         clientState.pAcqDbContext = EpgContextCtl_OpenDummy();
         clientState.acqContextIsPeek = FALSE;

         // first block must be an AI to determine the provider database
         clientState.waitAi = TRUE;
         clientState.acqDescr.nxtvState = ACQDESCR_NET_CONNECT;
         memset(&clientState.acqStats, 0, sizeof(clientState.acqStats));
      }
      else
      {  // connect failed -> abort
         clientState.state = CLNT_STATE_OFF;
      }
   }
   else
      debug0("EpgDbClient-Start: acq already enabled");

   return (clientState.state != CLNT_STATE_OFF);
}

// ----------------------------------------------------------------------------
// Stop network acquisition, i.e. close connection
//
void EpgAcqClient_Stop( void )
{
   if (clientState.state != CLNT_STATE_OFF)
   {
      // note: set the new state first to prevent callback from close function
      clientState.state = CLNT_STATE_OFF;

      EpgTscQueue_Clear(&clientState.acqTscQueue);

      EpgAcqClient_CloseDb();

      EpgAcqClient_Close(TRUE);
   }
   else
      debug0("EpgDbClient-Stop: acq not enabled");
}

// ----------------------------------------------------------------------------
// Connect to the server, transmit a query and read the response
//
static char * EpgAcqClient_SimpleQuery( const char * pQueryStr, char ** ppErrorMsg )
{
   struct timeval  timeout;
   struct timeval  tv;
   struct timeval  selectStart;
   fd_set  fds;
   bool    ioBlocked;
   sint    selSockCnt;
   char * pMsgBuf = NULL;

   EpgAcqClient_ConnectServer();
   if (clientState.io.sock_fd != -1)
   {
      // overall timeout for connect and message exchange
      timeout.tv_sec  = 20;
      timeout.tv_usec = 0;

      // wait for the connect to complete
      FD_ZERO(&fds);
      FD_SET(clientState.io.sock_fd, &fds);
      tv = timeout;
      EpgNetIo_GetTimeOfDay(&selectStart);
      selSockCnt = select(clientState.io.sock_fd + 1, NULL, &fds, NULL, &tv);
      if (selSockCnt > 0)
      {
         EpgNetIo_UpdateTimeout(&selectStart, &timeout);

         if (EpgNetIo_FinishConnect(clientState.io.sock_fd, ppErrorMsg))
         {
            // write request message & wait until it's sent out
            EpgNetIo_WriteMsg(&clientState.io, MSG_TYPE_CONNECT_REQ,
                              strlen(pQueryStr), (char*)pQueryStr, FALSE);
            do
            {
               FD_ZERO(&fds);
               FD_SET(clientState.io.sock_fd, &fds);
               EpgNetIo_GetTimeOfDay(&selectStart);
               tv = timeout;

               selSockCnt = select(clientState.io.sock_fd + 1, NULL, &fds, NULL, &tv);
               if (selSockCnt <= 0)
                  goto socket_error;

               if (EpgNetIo_HandleIO(&clientState.io, &ioBlocked, FALSE) == FALSE)
                  goto failure;

               EpgNetIo_UpdateTimeout(&selectStart, &timeout);

            } while (clientState.io.writeLen != 0);

            // wait for the reply message
            clientState.io.waitRead = TRUE;
            do
            {
               FD_ZERO(&fds);
               FD_SET(clientState.io.sock_fd, &fds);
               EpgNetIo_GetTimeOfDay(&selectStart);
               tv = timeout;

               selSockCnt = select(clientState.io.sock_fd + 1, &fds, NULL, NULL, &tv);
               if (selSockCnt <= 0)
                  goto socket_error;

               if (EpgNetIo_HandleIO(&clientState.io, &ioBlocked, TRUE) == FALSE)
                  goto failure;

               EpgNetIo_UpdateTimeout(&selectStart, &timeout);

            } while ( (clientState.io.waitRead) ||
                      (clientState.io.readLen != clientState.io.readOff) );

            if (clientState.io.readHeader.type == MSG_TYPE_CONQUERY_CNF)
            {
               // make a null-terminated copy of the response text
               pMsgBuf = xmalloc(clientState.io.readLen + 1);
               memcpy(pMsgBuf, clientState.io.pReadBuf, clientState.io.readLen);
               pMsgBuf[clientState.io.readLen - sizeof(EPGNETIO_MSG_HEADER)] = 0;
            }
            else
            {
               debug1("EpgAcqClient-SimpleQuery: unexpected response type %d", clientState.io.readHeader.type);
               SystemErrorMessage_Set(ppErrorMsg, 0, "Protocol error", NULL);
            }

            xfree(clientState.io.pReadBuf);
            clientState.io.pReadBuf = NULL;
            clientState.io.readLen = 0;
            clientState.io.readOff = 0;
         }
      }
      else
         goto socket_error;
   }
   EpgAcqClient_Close(FALSE);
   return pMsgBuf;

socket_error:
   if (selSockCnt == 0)
   {
      SystemErrorMessage_Set(ppErrorMsg, 0, "Communication timeout (no response from server)", NULL);
      debug0("EpgAcqClient-SimpleQuery: timeout waiting for connect");
   }
   else
   {
      SystemErrorMessage_Set(ppErrorMsg, errno, "Lost connection (I/O error)", NULL);
      debug1("EpgAcqClient-SimpleQuery: connect failed: error=%d", errno);
   }
failure:
   EpgAcqClient_Close(FALSE);
   return NULL;
}

// ----------------------------------------------------------------------------
// Connect and query daemon for it's acq status
// - the caller must free the returned message buffer
//
char * EpgAcqClient_QueryAcqStatus( char ** ppErrorMsg )
{
   char * pMsgBuf = NULL;

   *ppErrorMsg = NULL;

   if (clientState.state == CLNT_STATE_OFF)
   {
      pMsgBuf = EpgAcqClient_SimpleQuery("ACQSTAT", ppErrorMsg);

      if (ppErrorMsg != NULL)
      {
         *ppErrorMsg = clientState.pErrorText;
         clientState.pErrorText = NULL;
      }
      EpgAcqClient_Close(FALSE);
   }
   return pMsgBuf;
}

#ifndef WIN32
// ----------------------------------------------------------------------------
// Connect and query daemon for it's process ID, then kill the process
//
bool EpgAcqClient_TerminateDaemon( char ** ppErrorMsg )
{
   struct timeval  timeout;
   struct timeval  tv;
   struct timeval  selectStart;
   fd_set  fds;
   bool    ioBlocked;
   sint    selSockCnt;
   long    pid_val;
   pid_t   pid;

   *ppErrorMsg = NULL;
   selSockCnt = 0;
   pid = -1;

   if (clientState.state == CLNT_STATE_OFF)
   {
      char * pMsgBuf = EpgAcqClient_SimpleQuery("PID", ppErrorMsg);
      if (pMsgBuf != NULL)
      {
         if ((sscanf(pMsgBuf, "PID %ld", &pid_val) == 1) && (pid_val > 0))
         {
            pid = (pid_t) pid_val;
         }
         else
         {
            SystemErrorMessage_Set(ppErrorMsg, 0, "Incompatible server version", NULL);
         }
         xfree(pMsgBuf);
      }
   }
   else if (clientState.state >= CLNT_STATE_WAIT_FWD_CNF)
   {
      pid = clientState.daemonPid;
   }

   // note: not only check -1: must make sure not to kill entire process groups
   if (pid > 0)
   {
      if (kill(pid, SIGTERM) == 0)
      {
         // overall timeout for waiting for the connection to close
         timeout.tv_sec  = 10;
         timeout.tv_usec = 0;

         // wait for daemon to close the socket, i.e. for socket to become readable
         clientState.io.waitRead = TRUE;
         while (clientState.io.sock_fd != -1)
         {
            FD_ZERO(&fds);
            FD_SET(clientState.io.sock_fd, &fds);
            EpgNetIo_GetTimeOfDay(&selectStart);
            tv = timeout;

            selSockCnt = select(clientState.io.sock_fd + 1, &fds, NULL, NULL, &tv);
            if (selSockCnt <= 0)
            {
               // do not report this error to the user, as the kill above was successful
               debug1("EpgAcqClient-TerminateDaemon: socket error %d", errno);
               EpgAcqClient_Close(FALSE);
               break;
            }
            EpgNetIo_UpdateTimeout(&selectStart, &timeout);

            if (EpgNetIo_HandleIO(&clientState.io, &ioBlocked, TRUE) == FALSE)
            {  // connection lost -> daemon is dead -> done (not an error here)
               EpgAcqClient_Close(FALSE);
               break;
            }
            else if (clientState.io.readLen == clientState.io.readOff)
            {  // discard any messages which arrive
               xfree(clientState.io.pReadBuf);
               clientState.io.pReadBuf = NULL;
               clientState.io.readLen = 0;
               clientState.io.readOff = 0;
               clientState.io.waitRead = TRUE;
            }
         }
      }
      else
      {
         SystemErrorMessage_Set(ppErrorMsg, errno, "Failed to terminate the daemon process", NULL);
         pid = -1;
      }
   }

   if (clientState.pErrorText != NULL)
   {
      if (ppErrorMsg != NULL)
      {
         *ppErrorMsg = clientState.pErrorText;
         clientState.pErrorText = NULL;
      }
      else
         SystemErrorMessage_Set(&clientState.pErrorText, 0, NULL);
   }

   return (pid > 0);
}
#endif

// ----------------------------------------------------------------------------
// Compare and copy a list of provider CNIs
// - filters out non-Nextview EPG providers, since XMLTV isn't forwarded
//
static bool EpgAcqClient_CmpAndCopyCnis( const uint * pCniTab, uint cniCount )
{
   uint dbIdx;
   uint newCount;
   bool result = TRUE;

   if (cniCount > MAX_MERGED_DB_COUNT)
      cniCount = MAX_MERGED_DB_COUNT;

   newCount = 0;
   for (dbIdx=0; dbIdx < cniCount; dbIdx++)
   {
      if ( IS_NXTV_CNI(pCniTab[dbIdx]) )
      {
         if ( (newCount >= clientState.cniCount) ||
              (pCniTab[dbIdx] != clientState.provCnis[newCount]) )
         {
            clientState.provCnis[newCount] = pCniTab[dbIdx];
            result = FALSE;
         }
         newCount += 1;
      }
   }
   clientState.cniCount = newCount;

   return result;
}

// ----------------------------------------------------------------------------
// Set or change the list of requested providers
// - the list is stored in the client state and will be used automatically
//   during connection establishment; the server is sent a message only if
//   a connection is already established
// - the message cannot be sent immediately if there's an I/O in progress
//   e.g. an incoming EPG block; in this case only a flag will be set that's
//   checked in the client I/O handler
//
bool EpgAcqClient_SetProviders( const uint * pCniTab, uint cniCount )
{
   EPGACQ_EVHAND acqEv;
   bool ioBlocked;
   uint dbIdx;
   uint acqCni;

   if (clientState.state != CLNT_STATE_OFF)
   {
      if (EpgAcqClient_CmpAndCopyCnis(pCniTab, cniCount) == FALSE)
      {
         dprintf5("EpgDbClient-ChangeProviders: request prov list: %d CNIs: 0x%04X, 0x%04X, 0x%04X, 0x%04X\n", cniCount, pCniTab[0], pCniTab[1], pCniTab[2], pCniTab[3]);

         if ( (clientState.state == CLNT_STATE_WAIT_FWD_CNF) ||
              (clientState.state == CLNT_STATE_WAIT_DUMP) ||
              (clientState.state == CLNT_STATE_WAIT_BLOCKS) )
         {  // schedule for message to be sent as soon as I/O is idle
            if (EpgNetIo_IsIdle(&clientState.io))
            {  // no outstanding I/O
               // build the fwd-req message with the new provider CNI list
               memcpy(&clientMsg.fwd_req.provCnis, clientState.provCnis, sizeof(clientMsg.fwd_req.provCnis));
               clientMsg.fwd_req.cniCount   = clientState.cniCount;
               for (dbIdx=0; dbIdx < clientState.cniCount; dbIdx++)
                  clientMsg.fwd_req.dumpStartTimes[dbIdx] = EpgContextCtl_GetAiUpdateTime(clientState.provCnis[dbIdx], FALSE);
               clientMsg.fwd_req.statsReqBits = clientState.statsReqBits;

               EpgNetIo_WriteMsg(&clientState.io, MSG_TYPE_FORWARD_REQ, sizeof(clientMsg.fwd_req), &clientMsg.fwd_req, FALSE);
               memset(&acqEv, 0, sizeof(acqEv));
               if (EpgNetIo_HandleIO(&clientState.io, &ioBlocked, FALSE))
               {
                  acqEv.fd           = clientState.io.sock_fd;
                  acqEv.blockOnWrite = (clientState.io.writeLen > 0);
                  acqEv.blockOnRead  = ! acqEv.blockOnWrite;
                  clientState.pCbUpdateEvHandler(&acqEv);
               }
               else
               {
                  SystemErrorMessage_Set(&clientState.pErrorText, 0, "Lost connection (I/O error)", NULL);
                  EpgAcqClient_Close(TRUE);
               }
            }
            else
            {  // I/O busy -> send message when current I/O is completed
               dprintf7("EpgDbClient-ChangeProviders: I/O busy (state %d, writeLen=%d, writeOff=%d, waitRead=%d, readLen=%d, readOff=%d, read msg type=%d)\n", clientState.state, clientState.io.writeLen, clientState.io.writeOff, clientState.io.waitRead, clientState.io.readLen, clientState.io.readOff, clientState.io.readHeader.type);
               clientState.provUpdate = TRUE;
            }

            // free all EPG blocks in the input queue
            EpgDbQueue_Clear(&clientState.acqDbQueue);
            EpgTscQueue_Clear(&clientState.acqTscQueue);
            EpgAcqClient_FreeStats();

            clientState.state = CLNT_STATE_WAIT_FWD_CNF;
         }
      }

      // upgrade/downgrade current acq db from/to peek if now/not-anymore in GUI list
      acqCni = EpgDbContextGetCni(clientState.pAcqDbContext);
      if (acqCni != 0)
      {
         EpgAcqClient_SwitchDb(acqCni);
      }
   }
   else
   {  // idle -> just copy parameters
      EpgAcqClient_CmpAndCopyCnis(pCniTab, cniCount);
   }
   return TRUE;
}

// ----------------------------------------------------------------------------
// Update the acq statistics mode on server side
//
static bool EpgAcqClient_UpdateAcqStatsMode( uint statsReqBits )
{
   EPGACQ_EVHAND acqEv;
   bool ioBlocked;

   // server is only notified is the parameters have changed
   if (clientState.statsReqBits != statsReqBits)
   {
      dprintf3("EpgDbClient-UpdateAcqStatsMode: new stats mode: HIST=%s TSC-REQ=%s TSC-ALL=%s\n", ((statsReqBits & STATS_REQ_BITS_HIST) ? "on":"off"), ((statsReqBits & STATS_REQ_BITS_TSC_REQ) ? "on":"off"), ((statsReqBits & STATS_REQ_BITS_TSC_ALL) ? "on":"off"));
      clientState.statsReqBits = statsReqBits;

      if (clientState.state >= CLNT_STATE_WAIT_FWD_CNF)
      {
         if (EpgNetIo_IsIdle(&clientState.io))
         {  // no outstanding I/O
            clientMsg.stats_req.statsReqBits     = statsReqBits;
            EpgNetIo_WriteMsg(&clientState.io, MSG_TYPE_STATS_REQ, sizeof(clientMsg.stats_req), &clientMsg.stats_req, FALSE);

            memset(&acqEv, 0, sizeof(acqEv));
            if (EpgNetIo_HandleIO(&clientState.io, &ioBlocked, FALSE))
            {
               acqEv.fd           = clientState.io.sock_fd;
               acqEv.blockOnWrite = (clientState.io.writeLen > 0);
               acqEv.blockOnRead  = ! acqEv.blockOnWrite;
               clientState.pCbUpdateEvHandler(&acqEv);
            }
            else
            {
               SystemErrorMessage_Set(&clientState.pErrorText, 0, "Lost connection (I/O error)", NULL);
               EpgAcqClient_Close(TRUE);
            }

            // clear the "one time use" bits, now that the message is sent
            clientState.statsReqBits &= ~ STATS_REQ_BITS_VPS_PDC_UPD;
         }
         else
         {
            dprintf7("EpgDbClient-UpdateAcqStatsMode: I/O busy (state %d, writeLen=%d, writeOff=%d, waitRead=%d, readLen=%d, readOff=%d, read msg type=%d)\n", clientState.state, clientState.io.writeLen, clientState.io.writeOff, clientState.io.waitRead, clientState.io.readLen, clientState.io.readOff, clientState.io.readHeader.type);
            clientState.statsReqUpdate = TRUE;
         }
      }
   }
   return TRUE;
}

// ----------------------------------------------------------------------------
// En-/disable extended statistics reports
// - the difference between normal and extended is not known to this module;
//   the flag is passed to the acqctl callback on server-side
//
bool EpgAcqClient_SetAcqStatsMode( bool enable )
{
   uint statsReqBits = clientState.statsReqBits;

   if (enable)
      statsReqBits |= STATS_REQ_BITS_HIST;
   else
      statsReqBits &= ~STATS_REQ_BITS_HIST;

   return EpgAcqClient_UpdateAcqStatsMode(statsReqBits);
}

// ----------------------------------------------------------------------------
// En-/disable forwarding of PI timescale information
// - when param enable is set to FALSE, nothing is forwarded
// - param allProviders decides for which providers info is forwarded:
//   + if set to FALSE, only for providers which are in the forward list
//   + if set to TRUE: immediately send timescale info for all PI already
//     in the db, if acq is currently running for a non-requested provider;
//     after that all incoming PI are reported (even if not forwarded)
//
bool EpgAcqClient_SetAcqTscMode( bool enable, bool allProviders )
{
   uint statsReqBits = clientState.statsReqBits;

   if (enable)
   {
      statsReqBits |= STATS_REQ_BITS_TSC_REQ;
      if (allProviders)
         statsReqBits |= STATS_REQ_BITS_TSC_ALL;
   }
   else
   {
      statsReqBits &= ~(STATS_REQ_BITS_TSC_REQ | STATS_REQ_BITS_TSC_ALL);

      if (clientState.state != CLNT_STATE_OFF)
      {
         EpgTscQueue_Clear(&clientState.acqTscQueue);
      }
   }

   return EpgAcqClient_UpdateAcqStatsMode(statsReqBits);
}

// ----------------------------------------------------------------------------
// En-/disable forwarding of VPS/PDC CNI and PIL codes
//
bool EpgAcqClient_SetVpsPdcMode( bool enable, bool reset )
{
   uint statsReqBits = clientState.statsReqBits;

   assert(enable || (reset == FALSE));  // if enable is FALSE other modes are unused and should be zero

   if (enable)
   {
      statsReqBits |= STATS_REQ_BITS_VPS_PDC_REQ;

      if (reset)
      {
         statsReqBits |= STATS_REQ_BITS_VPS_PDC_UPD;
         // clear bit to force sending of the message to the server
         clientState.statsReqBits &= ~ STATS_REQ_BITS_VPS_PDC_UPD;
      }
   }
   else
      statsReqBits &= ~(STATS_REQ_BITS_VPS_PDC_REQ | STATS_REQ_BITS_VPS_PDC_UPD);

   return EpgAcqClient_UpdateAcqStatsMode(statsReqBits);
}

// ----------------------------------------------------------------------------
// Check for protocol timeouts
// - returns TRUE if an error occurred since the last polling
//   note: in subsequent calls it's no longer set, even if acq is not running
// - should be called every second
// - also used to initiate connect retries
//
bool EpgAcqClient_CheckTimeouts( void )
{
   EPGACQ_EVHAND   acqEv;
   bool    stopped;
   time_t  now = time(NULL);

   stopped = FALSE;

   // check for protocol or network I/O timeout
   if ( (now > clientState.io.lastIoTime + SRV_REPLY_TIMEOUT) &&
        ( (EpgNetIo_IsIdle(&clientState.io) == FALSE) ||
          (clientState.state == CLNT_STATE_WAIT_CONNECT) ||
          (clientState.state == CLNT_STATE_WAIT_CON_CNF) ||
          (clientState.state == CLNT_STATE_WAIT_FWD_CNF) ))
   {
      debug0("EpgDbClient-CheckForBlocks: network timeout");
      SystemErrorMessage_Set(&clientState.pErrorText, 0, "Lost connection (I/O timeout)", NULL);
      EpgAcqClient_Close(TRUE);
   }
   else if ( (clientState.state == CLNT_STATE_RETRY) &&
             (now > clientState.io.lastIoTime + CLNT_RETRY_INTERVAL) )
   {
      dprintf0("EpgDbClient-CheckForBlocks: initiate connect retry\n");
      clientState.io.lastIoTime = now;
      EpgAcqClient_ConnectServer();
      if (clientState.io.sock_fd != -1)
      {
         clientState.state  = CLNT_STATE_WAIT_CONNECT;

         memset(&acqEv, 0, sizeof(acqEv));
         acqEv.fd             = clientState.io.sock_fd;
         acqEv.blockOnWrite   = TRUE;
         acqEv.blockOnConnect = TRUE;
         clientState.pCbUpdateEvHandler(&acqEv);
      }
      else
         stopped = TRUE;
   }

   if (clientState.state == CLNT_STATE_ERROR)
   {  // an error has occured and the upper layer is not yet informed
      dprintf0("EpgDbClient-CheckForBlocks: report error\n");
      assert(EpgDbQueue_GetBlockCount(&clientState.acqDbQueue) == 0);
      clientState.state = CLNT_STATE_RETRY;
      clientState.waitAi = TRUE;
      memset(&clientState.acqStats, 0, sizeof(clientState.acqStats));

      stopped = TRUE;
   }

   return stopped;
}

// ----------------------------------------------------------------------------
// Process all stats reports received from server
// - called after the block input queue was processed
// - 4 modes are supported (to minimize required bandwidth): initial, initial
//   with prov info, update with prov info, minimal update (prov info must be
//   forwarded when acq runs on a database whose blocks are not forwarded)
// - depending on the mode a different structure is enclosed
//
static bool EpgAcqClient_ProcessStats( bool * pAiFollows )
{
   const MSG_STRUCT_STATS_IND * pUpd;
   EPGDBSRV_MSG_BODY          * pNext;
   EPG_ACQ_STATS * pAcqStats;
   EPGACQ_DESCR * pAcqDescr;
   time_t  lastAiAcqTime;
   uint    histIdx;
   bool    received;

   pAcqStats = &clientState.acqStats;
   pAcqDescr = &clientState.acqDescr;
   received = FALSE;
   lastAiAcqTime = 0;
   *pAiFollows = FALSE;

   while (clientState.pStatsMsg != NULL)
   {
      pUpd = &clientState.pStatsMsg->stats_ind;
      *pAiFollows |= pUpd->aiFollows;
      received = TRUE;

      switch (pUpd->type)
      {
         case EPGDB_STATS_UPD_TYPE_MINIMAL:
            // first stats for a new database which is open on client side (i.e. used by GUI)

            // invalidate extended statistics data
            memset(pAcqStats, 0, sizeof(*pAcqStats));

            memcpy(pAcqDescr, &pUpd->descr, sizeof(*pAcqDescr));
            memcpy(&pAcqStats->nxtv.count, &pUpd->u.minimal.count, sizeof(pAcqStats->nxtv.count));
            pAcqStats->nxtv.nowMaxAcqNetCount = pUpd->u.minimal.nowMaxAcqNetCount;
            lastAiAcqTime = pUpd->u.minimal.lastAiTime;

            if (pUpd->u.minimal.vpsPdc.cniType != STATS_IND_INVALID_VPS_PDC)
               clientState.acqVpsPdcInd += 1;
            break;

         case EPGDB_STATS_UPD_TYPE_INITIAL:
            memcpy(pAcqDescr, &pUpd->descr, sizeof(*pAcqDescr));
            memcpy(pAcqStats, &pUpd->u.initial.stats, sizeof(*pAcqStats));
            lastAiAcqTime = pUpd->u.initial.stats.nxtv.ai.lastAiTime;

            if (pUpd->u.initial.vpsPdc.cniType != STATS_IND_INVALID_VPS_PDC)
               clientState.acqVpsPdcInd += 1;
            break;

         case EPGDB_STATS_UPD_TYPE_UPDATE:
            if (pAcqDescr->nxtvDbCni != pUpd->descr.nxtvDbCni)
            {  // should not happen - after provider change an "initial" report is expected
               debug2("EpgAcqClient-ProcessStats: unexpected provider change from 0x%04X to 0x%04X in UPDATE", pAcqDescr->nxtvDbCni, pUpd->descr.nxtvDbCni);
               // clear all previous information
               memset(pAcqStats, 0, sizeof(*pAcqStats));
            }

            memcpy(pAcqDescr, &pUpd->descr, sizeof(*pAcqDescr));
            memcpy(&pAcqStats->nxtv.count, &pUpd->u.update.count, sizeof(pAcqStats->nxtv.count));

            pAcqStats->nxtv.ai = pUpd->u.update.ai;
            pAcqStats->ttx_dec = pUpd->u.update.ttx_dec;
            pAcqStats->ttx_duration = pUpd->u.update.ttx_duration;
            pAcqStats->nxtv.stream = pUpd->u.update.stream;
            pAcqStats->nxtv.nowMaxAcqRepCount = pUpd->u.update.nowMaxAcqRepCount;
            pAcqStats->nxtv.nowMaxAcqNetCount = pUpd->u.update.nowMaxAcqNetCount;
            pAcqStats->lastStatsUpdate = pUpd->u.update.lastStatsUpdate;
            pAcqStats->ttx_grab.pkgStats = pUpd->u.update.grabTtxStats;
            lastAiAcqTime = pUpd->u.update.ai.lastAiTime;

            histIdx = pAcqStats->nxtv.histIdx;
            do
            {
               histIdx = (histIdx + 1) % STATS_HIST_WIDTH;
               pAcqStats->nxtv.hist[histIdx] = pUpd->u.update.hist;
            } while (histIdx != pUpd->u.update.histIdx);
            pAcqStats->nxtv.histIdx = pUpd->u.update.histIdx;

            if (pUpd->u.update.vpsPdc.cniType != STATS_IND_INVALID_VPS_PDC)
               clientState.acqVpsPdcInd += 1;

            break;
      }
      dprintf6("ProcessStats-ProcessStats: type %d, AI follows=%d, acqstate=%d, cyCni=%04X, dbCni=%04X, histIdx=%d\n", pUpd->type, pUpd->aiFollows, pUpd->descr.nxtvState, pUpd->descr.cycleCni, pUpd->descr.nxtvDbCni, pAcqStats->nxtv.histIdx);

      // free the message
      pNext = clientState.pStatsMsg->stats_ind.p.pNext;
      xfree((void *) clientState.pStatsMsg);
      clientState.pStatsMsg = pNext;
   }

   if (received && !*pAiFollows && (lastAiAcqTime != 0))
   {  // update AI block acquisition time
      // required as start time for the next dump (upon connection loss and re-establishment)
      // XXX TODO: must not set time for non-req providers XXX
      EpgDbSetAiUpdateTime(clientState.pAcqDbContext, lastAiAcqTime);
   }

   return received;
}

// ----------------------------------------------------------------------------
// Describe the state of the connection
//
bool EpgAcqClient_DescribeNetState( EPGDBSRV_DESCR * pNetState )
{
   bool result;

   memset(pNetState, 0, sizeof(*pNetState));

   switch(clientState.state)
   {
      case CLNT_STATE_RETRY:
      case CLNT_STATE_ERROR:
         pNetState->state = NETDESCR_ERROR;
         pNetState->cause = clientState.pErrorText;
         result = TRUE;
         break;
      case CLNT_STATE_WAIT_CONNECT:
         pNetState->state = NETDESCR_CONNECT;
         result = TRUE;
         break;
      case CLNT_STATE_WAIT_CON_CNF:
      case CLNT_STATE_WAIT_FWD_CNF:
         pNetState->state = NETDESCR_CONNECT;
         result = TRUE;
         break;
      case CLNT_STATE_WAIT_DUMP:
         pNetState->state = NETDESCR_LOADING;
         result = TRUE;
         break;
      case CLNT_STATE_WAIT_BLOCKS:
         pNetState->state = NETDESCR_RUNNING;
         result = TRUE;
         break;
      case CLNT_STATE_OFF:
         pNetState->state = NETDESCR_DISABLED;
         pNetState->cause = clientState.pErrorText;
         result = TRUE;
         break;
      default:
         fatal1("EpgDbClient-DescribeNetState: illegal state %d", clientState.state);
         pNetState->state = NETDESCR_DISABLED;
         result = FALSE;
         break;
   }
   // statistics about data rate in rx direction (tx is neglectable)
   pNetState->rxTotal     = clientState.rxTotal;
   pNetState->rxStartTime = clientState.rxStartTime;

   return result;
}

// ----------------------------------------------------------------------------
// Query if the client is connected to a local server (i.e. running on the same host)
// - check if server name is equal "localhost" or local hostname
//
bool EpgAcqClient_IsLocalServer( void )
{
   bool result;

   if (clientState.pSrvHost != NULL)
   {
      if (strcmp(clientState.pSrvHost, "localhost") != 0)
      {
         result = EpgNetIo_IsLocalHost(clientState.pSrvHost);
      }
      else
         result = TRUE;
   }
   else
      result = FALSE;

   return result;
}

// ----------------------------------------------------------------------------
// Set server address
//
bool EpgAcqClient_SetAddress( const char * pHostName, const char * pPort )
{
   bool changed;
   bool result = TRUE;

   // discard strings with zero length
   if ((pHostName != NULL) && (pHostName[0] == 0))
      pHostName = NULL;
   if ((pPort != NULL) && (pPort[0] == 0))
      pPort = NULL;

   // check if the parameters differ
   if (pHostName != NULL)
      changed = ((clientState.pSrvHost == NULL) || (strcmp(pHostName, clientState.pSrvHost) != 0));
   else
      changed = (clientState.pSrvHost != NULL);

   if (pPort != NULL)
      changed |= ((clientState.pSrvPort == NULL) || (strcmp(pPort, clientState.pSrvPort) != 0));
   else
      changed |= (clientState.pSrvPort != NULL);

   if (changed)
   {
      // free the memory allocated for the old config strings
      if (clientState.pSrvHost != NULL)
      {
         xfree(clientState.pSrvHost);
         clientState.pSrvHost = NULL;
      }
      if (clientState.pSrvPort != NULL)
      {
         xfree(clientState.pSrvPort);
         clientState.pSrvPort = NULL;
      }

      // make a copy of the new config strings
      if (pHostName != NULL)
      {
         clientState.pSrvHost = xmalloc(strlen(pHostName) + 1);
         strcpy(clientState.pSrvHost, pHostName);
      }
      if (pPort != NULL)
      {
         clientState.pSrvPort = xmalloc(strlen(pPort) + 1);
         strcpy(clientState.pSrvPort, pPort);
      }

      // if acq already running, restart with new params
      if (clientState.state != CLNT_STATE_OFF)
      {
         EpgAcqClient_Stop();
         result = EpgAcqClient_Start();
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Close the acq database
// - automatically chooses the right close function for the acq database
//   depending on how it was opened, i.e. as peek or fully
//
static void EpgAcqClient_CloseDb( void )
{
   if (clientState.pAcqDbContext != NULL)
   {
      if (clientState.acqContextIsPeek == FALSE)
         EpgContextCtl_Close(clientState.pAcqDbContext);
      else
         EpgContextCtl_ClosePeek(clientState.pAcqDbContext);
      clientState.pAcqDbContext = NULL;
   }
   else
      fatal0("EpgAcqClient-CloseDb: db not open");
}

// ---------------------------------------------------------------------------
// Switch provider database to a new provider
// - called by AI callback upon provider change
//   and called upon GUI provider list update
// - in network mode it may be that the acquisition database is only opened as
//   "peek" if the acq db is not in the GUI provider list; when the GUI list is
//   updated it may be required to up-/downgrade the db
//
static void EpgAcqClient_SwitchDb( uint acqCni )
{
   uint  idx;
   uint  oldCni;

   oldCni = EpgDbContextGetCni(clientState.pAcqDbContext);

   // check if current acq provider is used by the new GUI providers
   for (idx=0; idx < clientState.cniCount; idx++)
      if (clientState.provCnis[idx] == acqCni)
         break;

   if (idx < clientState.cniCount)
   {  // used by GUI
      if (acqCni != oldCni)
      {  // used by GUI -> open the db completely
         EpgAcqClient_CloseDb();
         clientState.acqContextIsPeek = FALSE;
         clientState.pAcqDbContext = EpgContextCtl_Open(acqCni, FALSE, CTX_FAIL_RET_CREATE, CTX_RELOAD_ERR_ANY);
      }
      else if (clientState.acqContextIsPeek == TRUE)
      {  // currently open as peek -> upgrade to full open
         dprintf1("EpgAcqClient-Switchdb: upgrade acq db 0x%04X to full open\n", acqCni);
         EpgAcqClient_CloseDb();
         clientState.pAcqDbContext = EpgContextCtl_Open(acqCni, FALSE, CTX_FAIL_RET_DUMMY, CTX_RELOAD_ERR_ANY);
         clientState.acqContextIsPeek = FALSE;
      }
   }
   else
   {  // current acq db is not in GUI list
      if (acqCni != oldCni)
      {  // just open a peek, i.e. AI and OI #0
         EpgAcqClient_CloseDb();
         clientState.acqContextIsPeek = FALSE;
         clientState.pAcqDbContext = EpgContextCtl_Peek(acqCni, CTX_RELOAD_ERR_ANY);
         if (clientState.pAcqDbContext != NULL)
            clientState.acqContextIsPeek = TRUE;
         else
            clientState.pAcqDbContext = EpgContextCtl_Open(acqCni, FALSE, CTX_FAIL_RET_CREATE, CTX_RELOAD_ERR_ANY);
      }
      else if (clientState.acqContextIsPeek)
      {  // currently fully open -> downgrade to peek
         dprintf1("EpgAcqClient-Switchdb: downgrade acq db 0x%04X to peek\n", acqCni);
         EpgAcqClient_CloseDb();
         clientState.acqContextIsPeek = FALSE;
         clientState.pAcqDbContext = EpgContextCtl_Peek(acqCni, CTX_RELOAD_ERR_ANY);
         if (clientState.pAcqDbContext != NULL)
            clientState.acqContextIsPeek = TRUE;
         else
            clientState.pAcqDbContext = EpgContextCtl_OpenDummy();
      }
   }
}

// ---------------------------------------------------------------------------
// AI callback: invoked before a new AI block is inserted into the database
//
static bool EpgAcqClient_AiCallback( const AI_BLOCK *pNewAi )
{
   const AI_BLOCK *pOldAi;
   uint ai_cni;

   assert (AI_GET_THIS_NET_CNI(pNewAi) != 0);  // rejected on server side
   if (clientState.waitAi)
      dprintf3("EpgCtl: AI found, CNI=0x%04X version %d/%d\n", AI_GET_THIS_NET_CNI(pNewAi), pNewAi->version, pNewAi->version_swo);
   clientState.waitAi = FALSE;

   EpgDbLockDatabase(clientState.pAcqDbContext, TRUE);
   pOldAi = EpgDbGetAi(clientState.pAcqDbContext);
   ai_cni = AI_GET_THIS_NET_CNI(pNewAi);

   if (pOldAi == NULL)
   {  // no database open yet -> open db (or dummy db if it's a new provider)
      EpgDbLockDatabase(clientState.pAcqDbContext, FALSE);

      EpgAcqClient_SwitchDb(ai_cni);
      dprintf2("EpgAcqClient: empty acq db, AI found: CNI 0x%04X (%s)\n", ai_cni, ((EpgDbContextGetCni(clientState.pAcqDbContext) == 0) ? "new" : "reload ok"));

      // update the ui netwop list and acq stats output if neccessary
      UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
   }
   else
   {  // already an AI in the database: check if it's the same provider
      if (clientState.pAcqDbContext->provCni == AI_GET_THIS_NET_CNI(pNewAi))
      {
         if ( (pOldAi->version != pNewAi->version) ||
              (pOldAi->version_swo != pNewAi->version_swo) )
         {
            dprintf2("EpgAcqClient: version number has changed, was: %d/%d\n", pOldAi->version, pOldAi->version_swo);
            UiControlMsg_AcqEvent(ACQ_EVENT_AI_VERSION_CHANGE);
         }
         else
         {  // same AI version
            if (EpgDbComparePiRanges(clientState.pAcqDbContext, pOldAi, pNewAi))
            {
               // new PI added at the end -> redraw timescales to mark ranges with missing PI
               UiControlMsg_AcqEvent(ACQ_EVENT_AI_PI_RANGE_CHANGE);
            }
            else
               UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
         }
         EpgDbLockDatabase(clientState.pAcqDbContext, FALSE);
      }
      else
      {  // different provider -> switch databases
         dprintf2("EpgAcqClient: switching acq db from %04X to %04X\n", AI_GET_THIS_NET_CNI(pOldAi), ai_cni);
         EpgDbLockDatabase(clientState.pAcqDbContext, FALSE);

         EpgAcqClient_SwitchDb(ai_cni);

         UiControlMsg_AcqEvent(ACQ_EVENT_PROV_CHANGE);
         memset(&clientState.acqStats, 0, sizeof(clientState.acqStats));
      }
   }

   assert(EpgDbIsLocked(clientState.pAcqDbContext) == FALSE);
   // always accept the block into the database
   return TRUE;
}

// ---------------------------------------------------------------------------
// Insert newly acquired blocks into the EPG db
// - to be called when ProcessPackets returns TRUE, i.e. EPG blocks in input queue
// - database MUST NOT be locked by GUI
//
void EpgAcqClient_ProcessBlocks( void )
{
   static const EPGDB_ADD_CB epgQueueCb = { EpgAcqClient_AiCallback, NULL };
   const EPGDB_BLOCK  * pBlock;
   bool overflow;
   bool aiFollows;

   if (clientState.state != CLNT_STATE_OFF)
   {
      assert(EpgDbIsLocked(clientState.pAcqDbContext) == FALSE);

      overflow = (EpgDbQueue_GetBlockCount(&clientState.acqDbQueue) >= EPG_QUEUE_OVERFLOW_LEN);
      if (overflow)
         overflow = UiControlMsg_AcqQueueOverflow(TRUE);

      if (clientState.waitAi)
      {  // fail-safe: check if the first block is a AI block
         while ( ((pBlock = EpgDbQueue_Peek(&clientState.acqDbQueue)) != NULL) &&
                 (pBlock->type != BLOCK_TYPE_AI) )
         {
            debug1("EpgAcqClient: illegal block type %d in queue while waiting for AI", pBlock->type);
            xfree((void *) EpgDbQueue_Get(&clientState.acqDbQueue));
         }
      }
      EpgDbProcessQueue(&clientState.pAcqDbContext, &clientState.acqDbQueue, NULL, &epgQueueCb);

      if (overflow)
         UiControlMsg_AcqQueueOverflow(FALSE);

      if (EpgTscQueue_HasElems(&clientState.acqTscQueue))
      {  // PI timescale info has been received -> unlock new buffers and trigger GUI
         EpgTscQueue_UnlockBuffers(&clientState.acqTscQueue);
         UiControlMsg_AcqEvent(ACQ_EVENT_PI_ADDED);
      }

      if ( (clientState.fwdProvCni == 0) &&
           (EpgDbContextGetCni(clientState.pAcqDbContext) != 0) )
      {
         // nextview acq switched to passive mode with dummy database
         dprintf0("EpgAcqClient-ProcessBlocks: closing acq db\n");
         EpgDbQueue_Clear(&clientState.acqDbQueue);
         EpgTscQueue_Clear(&clientState.acqTscQueue);
         EpgAcqClient_CloseDb();
         clientState.acqContextIsPeek = FALSE;
         clientState.pAcqDbContext = EpgContextCtl_OpenDummy();
         clientState.waitAi = TRUE;
      }

      if (EpgAcqClient_ProcessStats(&aiFollows))
      {
         ifdebug2(clientState.acqDescr.nxtvDbCni != EpgDbContextGetCni(clientState.pAcqDbContext), "EpgAcqClient-ProcessBlocks: stats dbCni=0x%04X != acq db CNI=0x%04X", clientState.acqDescr.nxtvDbCni, EpgDbContextGetCni(clientState.pAcqDbContext));

         if (aiFollows == FALSE)
         {  // no AI transmitted (no reception, or AI unchanged) -> trigger status update from here
            UiControlMsg_AcqEvent(ACQ_EVENT_STATS_UPDATE);
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Get Pointer to PI timescale queue
// - used by the GUI to retrieve info from the queue (and thereby emptying it)
// - returns NULL if acq is off (queue might not be initialized yet)
//
EPGDB_PI_TSC * EpgAcqClient_GetTimescaleQueue( void )
{
   if (clientState.state != CLNT_STATE_OFF)
      return &clientState.acqTscQueue;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Get acquisition database context
//
EPGDB_CONTEXT * EpgAcqClient_GetDbContext( void )
{
   return clientState.pAcqDbContext;
}

// ---------------------------------------------------------------------------
// Return complete set of acq state and statistic values
// - used by "View acq statistics" popup window
//
bool EpgAcqClient_GetAcqStats( EPG_ACQ_STATS * pAcqStats )
{
   bool result = FALSE;

   if (clientState.state != CLNT_STATE_OFF)
   {
      memcpy(pAcqStats, &clientState.acqStats, sizeof(EPG_ACQ_STATS));
      result = TRUE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Return VPS/PDC data
//
bool EpgAcqClient_GetVpsPdc( EPG_ACQ_VPS_PDC * pVpsPdc, uint * pReqInd )
{
   bool result = FALSE;

   if ((pReqInd == NULL) || (*pReqInd != clientState.acqVpsPdcInd))
   {
      if (pVpsPdc != NULL)
         *pVpsPdc = clientState.acqVpsPdc;

      if (pReqInd != NULL)
         *pReqInd = clientState.acqVpsPdcInd;

      result = TRUE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Determine state of acquisition for user information
// - if state is set to INVALID (zero) caller should assume network state "loading db"
//   because it takes a short while after loading the db until the first stats report
//   is available
//
void EpgAcqClient_DescribeAcqState( EPGACQ_DESCR * pAcqState )
{
   assert(clientState.state != CLNT_STATE_OFF);

   if ( (clientState.state != CLNT_STATE_ERROR) &&
        (clientState.state != CLNT_STATE_RETRY) )
   {
      memcpy(pAcqState, &clientState.acqDescr, sizeof(*pAcqState));
      pAcqState->isLocalServer = EpgAcqClient_IsLocalServer();
   }
   else
   {
      memset(pAcqState, 0, sizeof(*pAcqState));
      pAcqState->nxtvState = ACQDESCR_DISABLED;
   }
   pAcqState->isNetAcq = TRUE;
}

// ----------------------------------------------------------------------------
// Initialize DB client
//
void EpgAcqClient_Init( void (* pCbUpdateEvHandler) ( EPGACQ_EVHAND * pAcqEv ) )
{
   EpgNetIo_Init(&clientState.pErrorText);

   // initialize client state
   memset(&clientState, 0, sizeof(clientState));
   clientState.pCbUpdateEvHandler = pCbUpdateEvHandler;

   clientState.state       = CLNT_STATE_OFF;
   clientState.io.sock_fd  = -1;
}

// ----------------------------------------------------------------------------
// Free client resources
//
void EpgAcqClient_Destroy( void )
{
   // close the connection (during normal shutdown it should already be closed)
   if (clientState.state != CLNT_STATE_OFF)
      EpgAcqClient_Stop();

   // free the memory allocated for the config strings and error text
   EpgAcqClient_SetAddress(NULL, NULL);
   SystemErrorMessage_Set(&clientState.pErrorText, 0, NULL);

   EpgNetIo_Destroy();
}

#endif  // USE_DAEMON
