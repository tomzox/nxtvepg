/*
 *  Nextview EPG block decoder
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
 *    This module is the center of the acquisition process. It
 *    contains the interface between the slave and control processes,
 *    i.e. there are two execution threads running here. The slave
 *    puts all teletext packets of a given page into a ring buffer.
 *    Additionally it decodes MIP, and during scan also packet 8/30.
 *    This data is passed via shared memory to the master. The master
 *    takes the packets from the ring buffer and hands them to the
 *    streams module for assembly to Nextview blocks. The interface
 *    to the higher level acquisition control consists of functions
 *    to start and stop acquisition, get statistics values and detect
 *    channel changes.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgdbacq.c,v 1.25 2001/02/25 16:00:45 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <string.h>
#include <stdio.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/hamming.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbacq.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgtxtdump.h"
#include "epgui/statswin.h"
#include "epgvbi/btdrv.h"
#include "epgctl/epgacqctl.h"


// ----------------------------------------------------------------------------
// internal decoder status
//

#define HEADER_CHECK_LEN         12
#define HEADER_CHECK_LEN_STR    "12"   // for debug output
#define HEADER_CHECK_MAX_ERRORS   2

static uint        epgAppId;
static uint        epgPageNo;
static bool        isEpgPage;
static bool        bEpgDbAcqEnabled;
static bool        bScratchAcqMode;
static bool        bHeaderCheckInit;
static uchar       lastPageHeader[HEADER_CHECK_LEN];


// ----------------------------------------------------------------------------
// Initialize the internal state
//
void EpgDbAcqInit( void )
{
   bEpgDbAcqEnabled = FALSE;

   pVbiBuf->writer_idx = 0;
   pVbiBuf->reader_idx = 0;
   pVbiBuf->isEnabled = FALSE;
}

// ----------------------------------------------------------------------------
// Start the acquisition
//
void EpgDbAcqStart( EPGDB_CONTEXT *dbc, uint pageNo, uint appId )
{
   bool bWaitForBiAi;

   if (bEpgDbAcqEnabled == FALSE)
   {
      isEpgPage = FALSE;
      bHeaderCheckInit = FALSE;

      if (pageNo != EPG_ILLEGAL_PAGENO)
         epgPageNo = dbc->pageNo = pageNo;
      else if ((dbc->pageNo != EPG_ILLEGAL_PAGENO) && VALID_EPG_PAGENO(dbc->pageNo))
         epgPageNo = dbc->pageNo;
      else
         epgPageNo = dbc->pageNo = EPG_DEFAULT_PAGENO;

      if (appId != EPG_ILLEGAL_APPID)
         epgAppId = appId;
      else if (dbc->appId != EPG_ILLEGAL_APPID)
         epgAppId = dbc->appId;
      else
         epgAppId = EPG_DEFAULT_APPID;

      // clear the ring buffer
      // (must not modify the writer index, which belongs to another process/thread)
      pVbiBuf->reader_idx = pVbiBuf->writer_idx;
      // pass the configuration variables to the ttx process via IPC
      // and reset the ttx decoder state
      pVbiBuf->epgPageNo = epgPageNo;
      pVbiBuf->mipPageNo = 0;
      pVbiBuf->isEpgPage = FALSE;
      pVbiBuf->isMipPage = 0;
      pVbiBuf->isEpgScan = FALSE;
      pVbiBuf->doVpsPdc  = FALSE;
      pVbiBuf->isEnabled = TRUE;
      // reset statistics variables
      pVbiBuf->ttxPkgCount = 0;
      pVbiBuf->epgPkgCount = 0;
      pVbiBuf->epgPagCount = 0;

      if (dbc->pAiBlock != NULL)
      {  // known provider -> enabled scratch mode until AI CNI is verified
         bWaitForBiAi = FALSE;
         // set up a list of alphabets for string decoding
         EpgBlockSetAlphabets(&dbc->pAiBlock->blk.ai);
      }
      else
      {  // unknown provider -> wait for AI
         bWaitForBiAi = TRUE;
      }

      // initialize the state of the streams decoder
      EpgStreamInit(bWaitForBiAi, epgAppId);

      bScratchAcqMode = TRUE;
      bEpgDbAcqEnabled = TRUE;
   }
   else
      debug0("EpgDbAcq-Start: already running");
}

// ----------------------------------------------------------------------------
// Stop the acquisition
// - the external process may continue to collect data
//
void EpgDbAcqStop( void )
{
   if (bEpgDbAcqEnabled)
   {
      // inform writer process/thread
      pVbiBuf->isEnabled = FALSE;

      // remove unused blocks from scratch buffer in stream decoder
      EpgStreamClearScratchBuffer();

      bEpgDbAcqEnabled = FALSE;
   }
   else
      debug0("EpgDbAcq-Stop: already stopped");
}

// ---------------------------------------------------------------------------
// Stop and Re-Start the acquisition
// - called after change of channel or internal parameters
//
void EpgDbAcqReset( EPGDB_CONTEXT *dbc, uint pageNo, uint appId )
{
   if (bEpgDbAcqEnabled)
   {
      // discard remaining blocks in the scratch buffer
      EpgStreamClearScratchBuffer();

      // set acq state to OFF internally only to satisfy start-acq function
      bEpgDbAcqEnabled = FALSE;

      EpgDbAcqStart(dbc, pageNo, appId);
   }
   else
      debug0("EpgDbAcq-Reset: acq not enabled");
}

// ---------------------------------------------------------------------------
// Initialize EPG scan upon start-up or after a channel change
//
void EpgDbAcqInitScan( void )
{
   if (bEpgDbAcqEnabled)
   {
      EpgStreamSyntaxScanInit();

      // reset result variables
      pVbiBuf->vpsCni = 0;
      pVbiBuf->pdcCni = 0;
      pVbiBuf->ni = 0;
      pVbiBuf->niRepCnt = 0;
      pVbiBuf->dataPageCount = 0;
      // reset statistics variables
      pVbiBuf->ttxPkgCount = 0;
      pVbiBuf->epgPkgCount = 0;
      pVbiBuf->epgPagCount = 0;
      // enable scan mode
      pVbiBuf->isEpgScan = TRUE;
      pVbiBuf->doVpsPdc  = TRUE;
      // clear the ring buffer
      pVbiBuf->reader_idx = pVbiBuf->writer_idx;
   }
   else
      debug0("EpgDbAcq-InitScan: acq not enabled");
}

// ---------------------------------------------------------------------------
// En-/Disable VPS and Packet 8/30/x acquisition
//
void EpgDbAcqEnableVpsPdc( bool enable )
{
   if (bEpgDbAcqEnabled)
   {
      // reset result variables
      pVbiBuf->vpsCni = 0;
      pVbiBuf->pdcCni = 0;
      pVbiBuf->ni = 0;
      pVbiBuf->niRepCnt = 0;

      // en-/disable VPS and PDC acquisition
      pVbiBuf->doVpsPdc = enable;
   }
   else
      debug0("EpgDbAcq-EnableVpsPdc: acq not enabled");
}

// ---------------------------------------------------------------------------
// Return CNIs and number of data pages found
//
void EpgDbAcqGetScanResults( uint *pCni, bool *pNiWait, uint *pDataPageCnt )
{
   *pCni = 0;
   *pNiWait = 0;

   if (pVbiBuf->vpsCni != 0)
   {
      *pCni = pVbiBuf->vpsCni;
   }
   else if (pVbiBuf->pdcCni != 0)
   {
      *pCni = pVbiBuf->pdcCni;
   }
   else if (pVbiBuf->ni != 0)
   {
      if (pVbiBuf->niRepCnt > 2)
         *pCni = pVbiBuf->ni;
      else
         *pNiWait = (pVbiBuf->niRepCnt < 2);
   }

   *pDataPageCnt = pVbiBuf->dataPageCount;
}

// ---------------------------------------------------------------------------
// Decode an incoming MIP packet
// - note that this runs inside of the ttx sub-thread
// - if an EPG id is found, it is passed to the acq control via shared memory
//
static void EpgDbAcqMipPacket( uchar magNo, uchar pkgNo, const uchar *data )
{
   sint id;
   uint i;

   if ((pkgNo >= 6) && (pkgNo <= 8))
   {
      for (i=0; i < 20; i++)
      {
         if ( UnHam84Byte(data + i * 2, &id) && (id == MIP_EPG_ID))
         {
            pVbiBuf->mipPageNo = (0xA0 + (pkgNo - 6) * 0x20 + (i / 10) * 0x10 + (i % 10)) | (uint)(magNo << 8);
            break;
         }
      }
   }
   else if ((pkgNo >= 9) && (pkgNo <= 13))
   {
      for (i=0; i < 18; i++)
      {
         if ( UnHam84Byte(data + i * 2, &id) && (id == MIP_EPG_ID))
         {
            pVbiBuf->mipPageNo = (0x0A + (pkgNo - 9) * 0x30 + (i / 6) * 0x10 + (i % 6)) | (uint)(magNo << 8);
            break;
         }
      }
   }
   else if (pkgNo == 14)
   {
      for (i=0; i < 6; i++)
      {
         if ( UnHam84Byte(data + i * 2, &id) && (id == MIP_EPG_ID))
         {
            pVbiBuf->mipPageNo = (0xFA + i) | (uint)(magNo << 8);
            break;
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Obtain EPG page number found in MIP
// - returns 0 if MIP not received or EPG not listed in MIP
//
uint EpgDbAcqGetMipPageNo( void )
{
   return pVbiBuf->mipPageNo;
}

// ---------------------------------------------------------------------------
// reverse bit order in one byte
//
static uchar EpgDbAcqReverseBitOrder( uchar b )
{
   int i;
   uchar result;

   result = b & 0x1;

   for (i=0; i < 7; i++)
   {
      b >>= 1;
      result <<= 1;

      result |= b & 0x1;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Parse packet 8/30 /1 and /2 for CNI
//
static void EpgDbAcqGetP830Cni( const char * data )
{
   uchar pdcbuf[10];
   schar dc;
   uint cni;

   if ( UnHam84Nibble(data, &dc) )
   {
      if (dc == 0)
      {  // this is a packet 8/30/1
         cni = ((uint)EpgDbAcqReverseBitOrder(data[7]) << 8) |
                      EpgDbAcqReverseBitOrder(data[8]);
         if ((cni != 0) && (cni != 0xffff))
         {
            // CNI is not parity protected, so we have to receive and compare it several times
            if ( (pVbiBuf->niRepCnt > 0) && (pVbiBuf->ni != cni))
            {  // comparison failure -> restart
               debug2("EpgDbAcqGetP830Cni: pkg 8/30/1 CNI error: %04X != %04X", pVbiBuf->ni, cni);
               pVbiBuf->niRepCnt = 0;
            }
            pVbiBuf->ni = cni;
            pVbiBuf->niRepCnt += 1;
         }
      }
      else if (dc == 4)
      {  // this is a packet 8/30/2 (PDC)
         memcpy(pdcbuf, data + 9, 9);
         if (UnHam84Array(pdcbuf, 9))
         {
            cni = (data[0] << 12) | ((data[6] & 3) << 10) | ((data[7] & 0xc) << 6) |
                  ((data[1] & 0xc) << 4) | ((data[7] & 0x3) << 4) | (data[8] & 0xf);
            if ((cni != 0) && (cni != 0xffff))
            {
               pVbiBuf->pdcCni = cni;
            }
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Save an VPS code from the VBI process/thread
//
void EpgDbAcqAddVpsCode( uint cni )
{
   if (pVbiBuf != NULL)
   {
      pVbiBuf->vpsCni = cni;
   }
}

// ---------------------------------------------------------------------------
// Return statistics collected by slave process/thread during acquisition
//
void EpgDbAcqGetStatistics( ulong *pTtxPkgCount, ulong *pEpgPkgCount, ulong *pEpgPagCount )
{
   *pTtxPkgCount = pVbiBuf->ttxPkgCount;
   *pEpgPkgCount = pVbiBuf->epgPkgCount;
   *pEpgPagCount = pVbiBuf->epgPagCount;
}

// ---------------------------------------------------------------------------
// Check the page-header of the current page
// - the acquisition control should immediately stop insertion of acquired
//   blocks, until a new instance of an AI block has been received and
//   its CNI evaluated
// - the page header comparison fails if more than 3 characters are
//   different; we need some tolerance here, sonce the page header
//   is only parity protected
//
static bool EpgDbAcqDoPageHeaderCheck( const uchar * curPageHeader )
{
   uint  i, err;
   schar dec;
   bool  result = TRUE;

   if (bHeaderCheckInit)
   {
      err = 0;
      for (i=0; i < HEADER_CHECK_LEN; i++)
      {
         dec = (schar)parityTab[curPageHeader[i]];
         if ( (dec >= 0) && (dec != lastPageHeader[i]) )
         {  // Abweichung gefunden
            err += 1;
         }
      }

      if (err > HEADER_CHECK_MAX_ERRORS)
      {
         debug2("EpgDbAcq-DoPageHeaderCheck: %d diffs from header \"%" HEADER_CHECK_LEN_STR "s\" - stop acq", err, lastPageHeader);
         result = FALSE;
      }
   }
   else
   {  // erster Aufruf -> Parity dekodieren; falls fehlerfrei abspeichern
      for (i=0; i < HEADER_CHECK_LEN; i++)
      {
         dec = (schar)parityTab[ curPageHeader[i] ];
         if (dec >= 0)
         {
            lastPageHeader[i] = dec;
         }
         else
            break;
      }

      // erst OK wenn fehlerfrei abgespeichert
      if (i >= HEADER_CHECK_LEN)
      {
         dprintf1("EpgDbAcq-DoPageHeaderCheck: found header \"%" HEADER_CHECK_LEN_STR "s\"\n", lastPageHeader);
         bHeaderCheckInit = TRUE;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// The following functions implement a ring buffer for incoming EPG packets
//
//    All icoming EPG packets are stored in a ring buffer to
//    de-couple the time-critical (real-time!) teletext decoder
//    from the rest of the database and user-interface handling.
//    Optimally, the teletext decoder should run in a separate
//    thread or process (connected by shared memory)
//

// ---------------------------------------------------------------------------
// Retrieve and process all available packets from VBI buffer
// - passing pointer to database context pointer, b/c the context pointer
//   may change inside the AI callback (e.g. change of provider)
// - at this point the EPG database process/thread takes the data from the
//   teletext acquisition process
//
void EpgDbAcqProcessPackets( EPGDB_CONTEXT * const * pdbc )
{
   EPGDB_BLOCK *pBlock;
   VBI_LINE *vbl;

   if (bEpgDbAcqEnabled)
   {
      assert((pVbiBuf->reader_idx <= EPGACQ_BUF_COUNT) && (pVbiBuf->writer_idx <= EPGACQ_BUF_COUNT));

      while ((pVbiBuf->reader_idx != pVbiBuf->writer_idx) && bEpgDbAcqEnabled)
      {
         vbl = &pVbiBuf->line[pVbiBuf->reader_idx];
         //dprintf4("Process idx=%d: pkg=%d pg=%03X.%04X\n", pVbiBuf->reader_idx, vbl->pkgno, vbl->pageno, vbl->sub);
         if (vbl->pkgno == 0)
         {
            // have to check page number again for the case of a page number change, in which
            // the separate teletext thread would finish its current page with the wrong number
            if (vbl->pageno == epgPageNo)
            {
               if ( EpgDbAcqDoPageHeaderCheck(vbl->data + 13 - 5) == FALSE )
               {
                  bHeaderCheckInit = FALSE;
                  EpgAcqCtl_ChannelChange(TRUE);
                  // must exit loop b/c reader_idx might be changed by AcqReset!
                  break;
               }
               else
                  isEpgPage = EpgStreamNewPage(vbl->sub);
            }
            else
               isEpgPage = FALSE;
         }
         else if (isEpgPage)
         {
            EpgStreamDecodePacket(vbl->pkgno, vbl->data);
         }
         pVbiBuf->reader_idx = (pVbiBuf->reader_idx + 1) % EPGACQ_BUF_COUNT;
      }

      if ( bEpgDbAcqEnabled && bScratchAcqMode )
      {
         pBlock = EpgStreamGetBlockByType(BLOCK_TYPE_BI);
         if (pBlock != NULL)
         {
            EpgAcqCtl_BiCallback(&pBlock->blk.bi);
            xfree(pBlock);
         }

         pBlock = EpgStreamGetBlockByType(BLOCK_TYPE_AI);
         if (pBlock != NULL)
         {
            if (EpgAcqCtl_AiCallback(&pBlock->blk.ai))
            {  // AI has been accepted -> start full acquisition
               if ( EpgDbAddBlock(*pdbc, pBlock) )
               {
                  bScratchAcqMode = FALSE;
                  EpgStreamEnableAllTypes();
               }
               else
                  xfree(pBlock);
            }
         }
      }

      if ( bEpgDbAcqEnabled && !bScratchAcqMode )
      {
         // note: the acq might get switched off by the callbacks inside the loop,
         // but we don't need to check this since then the buffer is cleared
         while ((pBlock = EpgStreamGetNextBlock()) != NULL)
         {
            if (pBlock->type == BLOCK_TYPE_BI)
            {  // the Bi block never is added to the db, only evaluated by acq ctl
               EpgAcqCtl_BiCallback(&pBlock->blk.bi);
               xfree(pBlock);
            }
            else if (pBlock->type == BLOCK_TYPE_AI)
            {
               if ( (EpgAcqCtl_AiCallback(&pBlock->blk.ai) == FALSE) || 
                    (EpgDbAddBlock(*pdbc, pBlock) == FALSE) )
               {  // block was not accepted
                  xfree(pBlock);
               }
            }
            else
            {
               if ( EpgDbAddBlock(*pdbc, pBlock) == FALSE )
               {  // block was not accepted
                  xfree(pBlock);
               }
            }
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Check if there are packets in the buffer
//
bool EpgDbAcqCheckForPackets( void )
{
   bool result;

   if (bEpgDbAcqEnabled)
   {
      result = (pVbiBuf->reader_idx != pVbiBuf->writer_idx);

      if ((result == FALSE) && (pVbiBuf->isEnabled == FALSE))
      {  // vbi slave has stopped acquisition -> inform master control
         EpgAcqCtl_Stop();
      }
   }
   else
      result = FALSE;

   return result;
}

// ---------------------------------------------------------------------------
// Append a packet to the VBI buffer
//
static void EpgDbAcqBufferAdd( uint pageNo, uint sub, uchar pkgno, const uchar * data )
{
   static int overflow = 0;

   assert((pVbiBuf->reader_idx <= EPGACQ_BUF_COUNT) && (pVbiBuf->writer_idx <= EPGACQ_BUF_COUNT));

   if (pVbiBuf->reader_idx != ((pVbiBuf->writer_idx + 1) % EPGACQ_BUF_COUNT))
   {
      pVbiBuf->line[pVbiBuf->writer_idx].pageno = pageNo;
      pVbiBuf->line[pVbiBuf->writer_idx].sub    = sub;
      pVbiBuf->line[pVbiBuf->writer_idx].pkgno  = pkgno;

      memcpy(pVbiBuf->line[pVbiBuf->writer_idx].data, data, 40);

      pVbiBuf->writer_idx = (pVbiBuf->writer_idx + 1) % EPGACQ_BUF_COUNT;

      overflow = 0;
   }
   else
   {
      if ((overflow % EPGACQ_BUF_COUNT) == 0)
      {
         debug0("EpgDbAcq-BufferAdd: buffer overflow");
         #ifndef WIN32
         BtDriver_CheckParent();
         #endif
      }
      overflow += 1;
   }
}

// ---------------------------------------------------------------------------
// Notification of a lost frame
// - executed inside the teletext slave process/thread
// - when a complete frame is lost, it's quite likely that a page header
//   was lost, so we cannot assume that the following packets still belong
//   to the current page
// - reset the ttx page state machine
// - there's no need to reset the EPG decoder (i.e. to throw away the current
//   block) because the next processed packet will be an header, which carries
//   a continuation index (CI) which will detect if an EPG page was lost
//
void EpgDbAcqLostFrame( void )
{
   debug1("EpgDbAcq-LostFrame: lost vbi frame %u", pVbiBuf->frameSeqNo + 1);
   pVbiBuf->isEpgPage = FALSE;
   pVbiBuf->isMipPage = 0;
}

// ---------------------------------------------------------------------------
// Process a received teletext packet, check if it belongs to EPG
// - note that this procedure is called by the teletext decoder process/thread
//   and hence can not access the state variables of the EPG process/thread
//   except for the shared buffer
//
bool EpgDbAcqAddPacket( uint pageNo, uint sub, uchar pkgno, const uchar * data )
{
   bool result = FALSE;

   if ((pVbiBuf != NULL) && pVbiBuf->isEnabled)
   {
      pVbiBuf->ttxPkgCount += 1;

      if (pkgno == 0)
      {  // new page header
         if (pageNo == pVbiBuf->epgPageNo)
         {
            pVbiBuf->isEpgPage = TRUE;
            EpgDbAcqBufferAdd(pageNo, sub, 0, data);
            pVbiBuf->epgPkgCount += 1;
            pVbiBuf->epgPagCount += 1;
            result = TRUE;
         }
         else
         {
            if ( (pageNo >> 8) == (pVbiBuf->epgPageNo >> 8) )
            {  // same magazine, but not the EPG page -> previous EPG page closed
               pVbiBuf->isEpgPage = FALSE;
            }

            if ( (pageNo & 0xFF) == 0xFD )
            {  // magazine inventory page - is decoded immediately by the ttx client
               pVbiBuf->isMipPage |= (1 << (pageNo >> 8));
            }
            else
               pVbiBuf->isMipPage &= ~(1 << (pageNo >> 8));
         }

         if (pVbiBuf->isEpgScan)
            EpgStreamSyntaxScanHeader(pageNo, sub);
      }
      else
      {
         if ( pVbiBuf->isEpgPage &&
                   ((pageNo >> 8) == (pVbiBuf->epgPageNo >> 8)) &&
                   (pkgno < 26) )
         {  // new EPG packet
            EpgDbAcqBufferAdd(0, 0, pkgno, data);
            pVbiBuf->epgPkgCount += 1;
            result = TRUE;
         }
         else if ( pVbiBuf->isMipPage & (1 << (pageNo >> 8)) )
         {  // new MIP packet (may be received in parallel for several magazines)
            EpgDbAcqMipPacket((uchar)(pageNo >> 8), pkgno, data);
         }
         else if (pVbiBuf->doVpsPdc && (pkgno == 30) && ((pageNo >> 8) == 0))
         {  // packet 8/30/1 is evaluated for CNI during EPG scan
            EpgDbAcqGetP830Cni(data);
         }
         // packets from other magazines (e.g. in parallel mode) and 8/30 have to be ignored

         if (pVbiBuf->isEpgScan)
            if (EpgStreamSyntaxScanPacket((uchar)(pageNo >> 8), pkgno, data))
               pVbiBuf->dataPageCount += 1;
      }
   }

   // return TRUE if packet was used by EPG
   return result;
}

