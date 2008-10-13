/*
 *  Nextview stream decoder
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
 *    Assemble separate teletext packets to Nextview blocks,
 *    detect hamming and parity errors. The syntax of the stream
 *    is defined in ETS 300 708, chapter 4: "Page Format - Clear"
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgstream.c,v 1.33 2008/10/12 16:00:37 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_STREAM
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/hamming.h"
#include "epgdb/epgblock.h"
#include "epgui/dumpraw.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgaifrag.h"
#include "epgdb/epgstream.h"


// ----------------------------------------------------------------------------
// declaration of local data types

// max block len (12 bits) - see ETS 300 708
// note that this does not include the 4 header bytes with length and appId
#define NXTV_BLOCK_MAXLEN 2048

typedef struct
{
   uchar   ci;
   uchar   pkgCount;
   uchar   lastPkg;
   uchar   appID;
   uint    blockLen;
   uint    recvLen;
   bool    haveBlock;
   uint    haveHeader;
   uchar   blockBuf[NXTV_BLOCK_MAXLEN + 4];
} NXTV_STREAM;

// ---------------------------------------------------------------------------
// struct that holds the state of the ttx decoder in the master process
//
#define HEADER_CHECK_LEN         12
#define HEADER_CHECK_LEN_STR    "12"   // for debug output
#define HEADER_CHECK_MAX_ERR_BITS  2*3

static struct
{
   uint     epgPageNo;
   bool     isEpgPage;
   bool     bHeaderCheckInit;
   uchar    lastPageHeader[HEADER_CHECK_LEN];
} ttxState;

// ----------------------------------------------------------------------------
// local variables

static NXTV_STREAM streamData[2];
static uchar       streamOfPage;
static EPGDB_QUEUE * pBlockQueue = NULL;
static uint        epgStreamAppId;
static bool        enableAllTypes;
static EPG_STREAM_STATS streamStats;


// ----------------------------------------------------------------------------
// Take a pointer the control and string bit fields from the block decoder and
// convert them to a C "compound" structure depending on the type qualifier
//
static void EpgStreamConvertBlock( const uchar *pBuffer, uint blockLen, uchar stream, ushort parityErrCnt, uchar type )
{
   EPGDB_BLOCK *pBlock;
   uint ctrlLen = pBuffer[3] | ((uint)(pBuffer[4]&0x03)<<8);
   uint strLen = blockLen - (ctrlLen + 2)*2;

   switch (type)
   {
      case EPGDBACQ_TYPE_BI:
         pBlock = EpgBlockConvertBi(pBuffer, ctrlLen);
         break;
      case EPGDBACQ_TYPE_AI:
         pBlock = EpgBlockConvertAi(pBuffer, ctrlLen, strLen);
         if (pBlock != NULL)
            enableAllTypes = TRUE;
         break;
      case EPGDBACQ_TYPE_PI:
         pBlock = EpgBlockConvertPi(pBuffer, ctrlLen, strLen);
         break;
      case EPGDBACQ_TYPE_NI:
         pBlock = EpgBlockConvertNi(pBuffer, ctrlLen, strLen);
         break;
      case EPGDBACQ_TYPE_OI:
         pBlock = EpgBlockConvertOi(pBuffer, ctrlLen, strLen);
         break;
      case EPGDBACQ_TYPE_MI:
         pBlock = EpgBlockConvertMi(pBuffer, ctrlLen, strLen);
         break;
      case EPGDBACQ_TYPE_LI:
         pBlock = EpgBlockConvertLi(pBuffer, ctrlLen, strLen);
         break;
      case EPGDBACQ_TYPE_TI:
         pBlock = EpgBlockConvertTi(pBuffer, ctrlLen, strLen);
         break;
      case EPGDBACQ_TYPE_HI:
      case EPGDBACQ_TYPE_UI:
      case EPGDBACQ_TYPE_CI:
      default:
         pBlock = NULL;
         break;
   }

   if (pBlock != NULL)
   {
      pBlock->stream        = stream;
      pBlock->parityErrCnt  = parityErrCnt;
      pBlock->origChkSum    = pBuffer[2];
      pBlock->origBlkLen    = blockLen;

      EpgDumpRaw_IncomingBlock(&pBlock->blk, pBlock->type, stream);

      if (enableAllTypes || (type <= EPGDBACQ_TYPE_AI))
      {
         dprintf2("BLOCK ADD type=%d (0x%lx)\n", pBlock->type, (ulong)pBlock);
         // Append the converted block to a queue
         // (the buffer is neccessary because more than one block end in one packet,
         // so we can not simply return a block address from the stream decoder)
         EpgDbQueue_Add(pBlockQueue, pBlock);
      }
      else
      {
         dprintf2("BLOCK REJECT type=%d (0x%lx)\n", pBlock->type, (ulong)pBlock);
         xfree(pBlock);
      }
   }
   else
      EpgDumpRaw_IncomingUnknown(type, blockLen, stream);
}

// ---------------------------------------------------------------------------
// Compute the checksum of a block: 0x100 - (sum nibbles) % 0x100
//
static uint EpgStreamComputeChkSum( uchar *pdat, uint len )
{
   uint i;
   uint chkSum = 0;

   for (i=0; i<len; i++)
   {
      chkSum += *pdat & 0xf;
      chkSum += *pdat >> 4;
      pdat += 1;
   }

   return ((0x100 - (chkSum & 0xff)) & 0xff);
}

// ---------------------------------------------------------------------------
// Check error encoding on block contents
// - the control data is Hamming-8/4 encoded, the string data odd-parity
//
static void EpgStreamCheckBlock( NXTV_STREAM * const psd, uchar stream )
{
   schar c1, c2, c3, c4;
   uchar type;
   uint  ctrlLen, strLen, chkSum, myChkSum;
   ushort errCnt;

   if (psd->appID == 0)
   {  // Bundle Inventory (BI block)
      if (UnHam84Array(psd->blockBuf, psd->blockBuf, psd->blockLen / 2))
      {
         chkSum = psd->blockBuf[2];
         psd->blockBuf[2] = 0;  // checksum does not include itself
         myChkSum = EpgStreamComputeChkSum(psd->blockBuf, psd->blockLen / 2);
         if (chkSum == myChkSum)
         {
            psd->blockBuf[2] = chkSum;
            EpgStreamConvertBlock(psd->blockBuf, psd->blockLen/2, stream, 0, EPGDBACQ_TYPE_BI);
         }
         else
         {
            debug3("BI block chksum error: my %x != %x, len=%d", myChkSum, chkSum, psd->blockLen);
            streamStats.epgBlkErr += 1;
         }
      }
      else
         debug0("BI block hamming error");
   }
   else if (psd->appID == epgStreamAppId)
   {  // All kinds of EPG blocks
      if ( UnHam84Nibble(psd->blockBuf + 6, &c1) &&
           UnHam84Nibble(psd->blockBuf + 7, &c2) &&
           UnHam84Nibble(psd->blockBuf + 8, &c3) &&
           UnHam84Nibble(psd->blockBuf + 9, &c4) )
      {
         ctrlLen = ((uint)(c3 & 3)<<8) | (c2<<4) | c1;
         type    = (c3>>2) | (c4<<2);

         if (psd->blockLen >= (ctrlLen+2) * 2)
         {
            if ( UnHam84Array(psd->blockBuf, psd->blockBuf, ctrlLen + 2) )
            {
               strLen = psd->blockLen - (ctrlLen + 2)*2;
               errCnt = UnHamParityArray(psd->blockBuf + (ctrlLen+2)*2, psd->blockBuf + ctrlLen+2, strLen);
               DBGONLY(if (errCnt))
                  dprintf1("PARITY errors: %d\n", errCnt);
               streamStats.epgStrSum += strLen;
               streamStats.epgParErr += errCnt;

               chkSum = psd->blockBuf[2];
               psd->blockBuf[2] = 0;  // chksum does not include itself
               myChkSum = EpgStreamComputeChkSum(psd->blockBuf, ctrlLen + 2);
               if (chkSum == myChkSum)
               {
                  psd->blockBuf[2] = chkSum;
                  // real block size = header + control-data + string-data
                  EpgStreamConvertBlock(psd->blockBuf, 2 + ctrlLen + strLen, stream, errCnt, type);

                  if (type == EPGDBACQ_TYPE_AI)
                     EpgAiFragmentsRestart();
               }
               else
               {
                  debug2("block chksum error: my %x != %x", myChkSum, chkSum);
                  streamStats.epgBlkErr += 1;
               }
            }
            else
            {
               debug0("block content hamming error");
               streamStats.epgBlkErr += 1;
            }
         }
         else
         {
            debug2("block ctrl length error block=%d ctrl=%d", psd->blockLen, ctrlLen);
            streamStats.epgBlkErr += 1;
         }
      }
      else
      {
         debug0("block header hamming error");
         streamStats.epgBlkErr += 1;
      }
   }
   else
      debug1("unknown appID=%d", psd->appID);
}

// ---------------------------------------------------------------------------
// Add data from a TTX packet to the current or a new EPG block
//
static uint EpgStreamAddData( const uchar * dat, uint restLine, bool isNewBlock )
{
   NXTV_STREAM * const psd = &streamData[streamOfPage];
   uint usedLen;
   schar c1,c2,c3,c4;

   if (isNewBlock)
   {
      if (psd->haveBlock && (psd->recvLen > 0))
      {
         debug1("missing data for last block with recvLen=%d - discard block", psd->recvLen);
         if (psd->haveHeader)
            streamStats.epgBlkErr += 1;
      }

      psd->haveBlock  = TRUE;
      psd->haveHeader = FALSE;
      psd->recvLen    = 0;
   }
   usedLen = 0;

   if (psd->haveHeader == FALSE)
   {
      assert(psd->recvLen < 4);
      if (psd->recvLen + restLine < 4)
      {  // part of the header is in the next packet
         dprintf1("               start new block with %d byte header fragment\n", /*packNo, blockPtr - 1,*/ restLine);

         memcpy(psd->blockBuf + psd->recvLen, dat, restLine);
         psd->recvLen += restLine;
         usedLen = restLine;
         restLine = 0;
      }
      else
      {  // enough data for the block header -> complete and decode it
         memcpy(psd->blockBuf + psd->recvLen, dat, 4 - psd->recvLen);
         usedLen   = 4 - psd->recvLen;
         dat      += usedLen;
         restLine -= usedLen;
         psd->recvLen = 4;
         streamStats.epgBlkCount += 1;

         if ( UnHam84Nibble(psd->blockBuf + 0, &c1) &&
              UnHam84Nibble(psd->blockBuf + 1, &c2) &&
              UnHam84Nibble(psd->blockBuf + 2, &c3) &&
              UnHam84Nibble(psd->blockBuf + 3, &c4) )
         {
            psd->appID    = c1 | ((c2 & 1) << 4);
            psd->blockLen = ((c2 >> 1) | (c3 << 3) | (c4 << 7)) + 4;  // + length of block len itself
            psd->haveHeader = TRUE;

            if (psd->appID == epgStreamAppId)
            {
               EpgAiFragmentsBlockStart(psd->blockBuf, psd->blockLen, dat, restLine);
            }
            if (psd->appID == 0)
               streamStats.epgBiCount += 1;
            dprintf4("               start block recv stream %d, appID=%d, len=4+%d, startlen=4+%d\n", /*packNo, blockPtr,*/ streamOfPage+1, psd->appID, psd->blockLen - 4, restLine);
         }
         else
         {  // hamming error
            debug0("structure header hamming error - skipping block");
            streamStats.epgBlkErr += 1;
            psd->haveBlock = FALSE;
         }
      }
   }

   if (psd->haveBlock && psd->haveHeader)
   {
      if (psd->recvLen + restLine > psd->blockLen)
         restLine = psd->blockLen - psd->recvLen;

      dprintf2("               added %d bytes to block, sum=%d\n", /* packNo, blockPtr,*/ restLine, psd->recvLen + restLine);
      memcpy(psd->blockBuf + psd->recvLen, dat, restLine);
      psd->recvLen += restLine;
      usedLen += restLine;

      if (psd->recvLen >= psd->blockLen)
      {  // block complete
         dprintf1("               block complete, length %d\n", /* packNo, blockPtr*/ psd->blockLen);
         EpgStreamCheckBlock(psd, streamOfPage);

         psd->haveBlock = FALSE;
      }
   }
   return usedLen;
}

// ---------------------------------------------------------------------------
// Process EPG data packet
// - the function uses two separate internal states for the two streams
// - a single teletext packet can produce several EPG blocks
//
static void EpgStreamDecodePacket( uchar packNo, const uchar * dat )
{
   NXTV_STREAM * const psd = &streamData[streamOfPage];
   schar c1;
   uint  blockPtr;

   if (streamOfPage < NXTV_NO_STREAM)
   {
      if (packNo <= psd->lastPkg)
      {  // packet sequence broken, probably due to missing page header
         // don't know what stream this page belongs to -> discard it
         debug2("missed page header: got pkg %d <= last pkg %d - discard page", packNo, psd->lastPkg);
         streamOfPage = NXTV_NO_STREAM;
      }
      else if (packNo > streamData[streamOfPage].pkgCount)
      {  // unexpected packet, probably from different page -> stop decoding current page
         debug2("EpgStream-DecodePacket: invalid pkg no #%d > %d - discard page", packNo, streamData[streamOfPage].pkgCount);
         streamOfPage = NXTV_NO_STREAM;
      }
   }
   EpgAiFragmentsAddPkg(streamOfPage, packNo, dat);

   if (streamOfPage < NXTV_NO_STREAM)
   {
      if (packNo != psd->lastPkg + 1)
      {  // packet missing
         if (psd->haveBlock)
         {
            debug6("missing packet %d of %d (have %d) - discard block in stream %d (have-head=%d,len=%d)", psd->lastPkg + 1, streamData[streamOfPage].pkgCount, packNo, streamOfPage, psd->haveHeader, psd->blockLen);
            if (psd->haveHeader)
               streamStats.epgBlkErr += 1;
            psd->haveBlock = FALSE;
         }
         streamStats.epgPkgMissing += packNo - (psd->lastPkg + 1);
      }
      psd->lastPkg = packNo;

      if ( UnHam84Nibble(dat, &c1) && (c1 <= 0x0d) )
      {
         blockPtr = 1 + 3 * c1;

         if (psd->haveBlock && (blockPtr > 1))
         {  // append data to a block
            dprintf2("pkg=%2d, BP= 1: append up to %d bytes\n", packNo, blockPtr - 1);
            EpgStreamAddData(dat + 1, blockPtr - 1, FALSE);
         }
         else if ((psd->haveBlock == FALSE) && (blockPtr == 40))
            dprintf1("pkg=%2d: BP=0xD -> no block start in this packet\n", packNo);

         while (blockPtr < 40)
         {  // start of at least one new structure in this packet
            if ( UnHam84Nibble(dat + blockPtr, &c1) )
            {
               if (c1 == 0x0c)
               {
                  dprintf3("pkg=%2d, BP=%2d: start with %d bytes\n", packNo, blockPtr, 40 - 1 - blockPtr);
                  blockPtr += 1;
                  // write the data in the output buffer & process the EPG block when complete
                  blockPtr += EpgStreamAddData(dat + blockPtr, 40 - blockPtr, TRUE);
               }
               else if (c1 == 0x03)
               {  // filler byte -> skip one byte
                  dprintf2("pkg=%2d, BP=%2d: skipping filler byte\n", packNo, blockPtr);
                  blockPtr += 1;
               }
               else
               {  // unexpected content (lost sync) -> skip this packet
                  debug3("pkg=%2d, BP=%2d: %02X: neither BS nor FB - skipping", packNo, blockPtr, c1);
                  blockPtr = 40;
               }
            }
            else
            {  // decoding error - skip this packet
               debug4("pkg=%2d, BP=%2d: BS marker hamming error: BS=0x%02x; appID=%x", packNo, blockPtr, dat[blockPtr], psd->appID);
               blockPtr = 40;
            }
         }
      }
      else
      {
         debug2("pkg=%2d: BP hamming error on value 0x%02x - discard pkg", packNo, dat[0]);
         if (psd->haveHeader)
            streamStats.epgBlkErr += 1;
         psd->haveBlock = FALSE;
      }
   }
   else
      dprintf1("pkg=%2d: discarded - not on a valid page\n", packNo);
}

// ---------------------------------------------------------------------------
// Process a newly acquire teletext header packet
// - returns TRUE if this is an EPG page, i.e. if the rest of the packets
//   should be passed into the decoder
//
static bool EpgStreamNewPage( uint sub )
{
   uchar newCi, newPkgCount, firstPkg;
   bool  result = FALSE;

   // decode stream number of the data in the new page: only 0 or 1 are valid for Nextview
   switch ((sub >> 8) & 0xf)
   {
      case 0:
         streamOfPage = NXTV_STREAM_NO_1;
         break;
      case 1:
         streamOfPage = NXTV_STREAM_NO_2;
         break;
      default:
         debug1("EpgStream-NewPage: unexpected stream number %d - ignoring page", (sub >> 8) & 0xf);
         streamOfPage = NXTV_NO_STREAM;
         break;
   }

   if (streamOfPage != NXTV_NO_STREAM)
   {
      // decode TTX packet count in the new page  (#0 is the header, #1-#25 can be used for data)
      newPkgCount = ((sub & 0x3000) >> (12-3)) | ((sub & 0x70) >> 4);
      if (newPkgCount <= 25)
      {
         newCi = sub & 0xf;
         firstPkg = 0;

         if (streamData[streamOfPage].ci != 0xff)
         {  // skip continuity check for the very first page

            if ( streamData[streamOfPage].ci == newCi )
            {  // fragmented page transmission -> continue normally with pkg-no +1
               if (streamData[streamOfPage].pkgCount != newPkgCount)
               {  // mismatch in packet count per page -> not same page after all
                  debug4("EpgStream-NewPage: packet count error for continued page CI=%d (%d -> %d) stream %d", streamData[streamOfPage].ci, streamData[streamOfPage].pkgCount, newPkgCount, streamOfPage);
                  if (streamData[streamOfPage].haveBlock && streamData[streamOfPage].haveHeader)
                     streamStats.epgBlkErr += 1;
                  streamData[streamOfPage].haveBlock = FALSE;
                  EpgAiFragmentsBreak(streamOfPage);
               }
               else
               {
                  dprintf2("repetition of CI=%d in stream %d\n", newCi, streamOfPage);
                  firstPkg = streamData[streamOfPage].lastPkg;
               }
            }
            else if ( ((streamData[streamOfPage].ci + 1) & 0xf) != newCi )
            {  // continuity is broken -> discard partial EPG block in the buffer
               debug3("EpgStream-NewPage: page continuity error (%d -> %d) stream %d", streamData[streamOfPage].ci, newCi, streamOfPage);
               if (streamData[streamOfPage].haveBlock && streamData[streamOfPage].haveHeader)
                  streamStats.epgBlkErr += 1;
               streamData[streamOfPage].haveBlock = FALSE;
               streamStats.epgPagMissing += 1;
               EpgAiFragmentsBreak(streamOfPage);
            }
            else if (streamData[streamOfPage].lastPkg != streamData[streamOfPage].pkgCount)
            {  // packets missing at the end of the last page -> discard partial block
               debug2("EpgStream-NewPage: packets missing at page end: %d < %d", streamData[streamOfPage].lastPkg, streamData[streamOfPage].pkgCount);
               if (streamData[streamOfPage].haveBlock && streamData[streamOfPage].haveHeader)
                  streamStats.epgBlkErr += 1;
               streamData[streamOfPage].haveBlock = FALSE;
               streamStats.epgPkgMissing += 1;
            }
         }

         dprintf4("pkg= 0: ***NEW PAGE*** CI=%d stream=%d pkgno=%d last=%d\n", newCi, streamOfPage, newPkgCount, firstPkg);
         EpgAiFragmentsStartPage(streamOfPage, firstPkg, newPkgCount);
         streamData[streamOfPage].ci = newCi;
         streamData[streamOfPage].pkgCount = newPkgCount;
         streamData[streamOfPage].lastPkg = firstPkg;
         streamStats.epgPagCount += 1;
         result = TRUE;
      }
      else
      {
         debug2("EpgStream-NewPage: too many packets (%d) for page in stream %d", streamData[streamOfPage].pkgCount, streamOfPage);
         streamOfPage = NXTV_NO_STREAM;
         EpgAiFragmentsBreak(streamOfPage);
      }
   }

   return result;
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
static bool EpgStreamPageHeaderCheck( const uchar * curPageHeader )
{
   uint  i, err;
   schar dec;
   bool  result = TRUE;

   if (ttxState.bHeaderCheckInit)
   {
      err = 0;
      for (i=0; i < HEADER_CHECK_LEN; i++)
      {
         dec = (schar)parityTab[curPageHeader[i]];
         if ( (dec >= 0) && (dec != ttxState.lastPageHeader[i]) )
         {  // add the number of differing bits
            err += ByteBitDistance(curPageHeader[i] & 0x7f, ttxState.lastPageHeader[i]);
         }
      }

      if (err >= HEADER_CHECK_MAX_ERR_BITS)
      {
         debug2("EpgStream-PageHeaderCheck: %d bits differ from header \"%" HEADER_CHECK_LEN_STR "s\" - stop acq", err, ttxState.lastPageHeader);
         result = FALSE;
      }
   }
   else
   {  // first header after channel change -> copy header
      for (i=0; i < HEADER_CHECK_LEN; i++)
      {
         dec = (schar)parityTab[ curPageHeader[i] ];
         if (dec >= 0)
         {
            ttxState.lastPageHeader[i] = dec;
         }
         else
            break;
      }

      // only OK if no parity decoding error
      if (i >= HEADER_CHECK_LEN)
      {
         dprintf1("EpgStream-PageHeaderCheck: found header \"%" HEADER_CHECK_LEN_STR "s\"\n", ttxState.lastPageHeader);
         ttxState.bHeaderCheckInit = TRUE;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Retrieve all available packets from VBI buffer and convert them to EPG blocks
// - at this point the EPG database process/thread takes the data from the
//   teletext acquisition process
// - returns FALSE when an uncontrolled channel change was detected; the caller
//   then should reset the acquisition
//
bool EpgStreamProcessPackets( void )
{
   const VBI_LINE * pVbl;
   uint pkgOff;
   bool channelChange = TRUE;
   const uchar * pAiBuffer;
   uint blockLen;
   uint parityErrCnt;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) )
   {
      assert(pVbiBuf->epgPageNo == ttxState.epgPageNo);

      // fetch the oldest packet from the teletext ring buffer
      pkgOff = 0;
      while ( (pVbl = TtxDecode_GetPacket(pkgOff)) != NULL )
      {
         streamStats.epgPkgCount += 1;
         pkgOff += 1;

         //dprintf4("Process idx=%d: pkg=%d pg=%03X.%04X\n", pVbiBuf->reader_idx, pVbl->pkgno, pVbl->pageno, pVbl->sub);
         if (pVbl->pkgno == 0)
         {
            // have to check page number again for the case of a page number change, in which
            // the separate teletext thread would finish its current page with the wrong number
            if (pVbl->pageno == ttxState.epgPageNo)
            {
               if ( EpgStreamPageHeaderCheck(pVbl->data + 13 - 5) == FALSE )
               {
                  ttxState.bHeaderCheckInit = FALSE;
                  // abort processing remaining data
                  channelChange = FALSE;
                  break;
               }
               else
                  ttxState.isEpgPage = EpgStreamNewPage(pVbl->ctrl_lo & 0x3F7F);
            }
            else
            {  // found an unexpected ttx page in the buffer
               ttxState.isEpgPage = FALSE;
            }
         }
         else
         {
            if ( ttxState.isEpgPage && (pVbl->pageno == ttxState.epgPageNo) )
            {
               EpgStreamDecodePacket(pVbl->pkgno, pVbl->data);
            }
         }
      }

      // if AI block was missed by the stream decoder, check if AI fragment assembly can deliver
      pAiBuffer = EpgAiFragmentsAssemble(&blockLen, &parityErrCnt);
      if ( pAiBuffer != NULL )
      {
         EpgStreamConvertBlock(pAiBuffer, blockLen, NXTV_STREAM_NO_1, parityErrCnt, EPGDBACQ_TYPE_AI);
         xfree((void *) pAiBuffer);
      }
   }
   return channelChange;
}

// ---------------------------------------------------------------------------
// Check number of lost packets and block decoding errors
// - returns FALSE if type should be changed
//
bool EpgStreamCheckSlicerQuality( void )
{
   bool result = TRUE;

   if ( (streamStats.epgPkgCount != 0) &&
        (streamStats.epgBiCount != 0) &&
        (streamStats.epgBlkCount != 0) )
   {
      if ( ((double)streamStats.epgBlkErr / streamStats.epgBlkCount >= 0.008) ||
           ((double)streamStats.epgParErr / streamStats.epgStrSum >= 0.005) )
      {
         debug4("EpgStream-CheckSlicerQuality: dropped %d of %d blocks, blanked %d of %d chars", streamStats.epgBlkErr, streamStats.epgBlkCount, streamStats.epgParErr, streamStats.epgStrSum);
         // reset stream statistics to give the new slicer a "clean slate"
         memset(&streamStats, 0, sizeof(streamStats));
         result = FALSE;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Fill struct with statistics data
//
void EpgStreamGetStatistics( EPG_STREAM_STATS * pStats )
{
   if (pStats != NULL)
   {
      streamStats.epgAppId = epgStreamAppId;
      streamStats.epgPageNo = ttxState.epgPageNo;

      *pStats = streamStats;
   }
}

// ---------------------------------------------------------------------------
// Initialize the internal decoder state
//
void EpgStreamInit( EPGDB_QUEUE *pDbQueue, bool bWaitForBiAi, uint appId, uint pageNo )
{
   memset(&streamData, 0, sizeof(streamData));
   streamData[0].ci = 0xff;
   streamData[1].ci = 0xff;
   streamOfPage   = NXTV_NO_STREAM;
   epgStreamAppId = appId;
   enableAllTypes = !bWaitForBiAi;
   pBlockQueue    = pDbQueue;

   // initialize the decoder state in the master process
   ttxState.epgPageNo = pageNo;
   ttxState.isEpgPage = FALSE;
   ttxState.bHeaderCheckInit = FALSE;

   memset(&streamStats, 0, sizeof(streamStats));

   EpgAiFragmentsRestart();

   // note: intentionally using clear func instead of overwriting the pointer with NULL
   // (simplifies upper layers' state machines: start acq may be called while acq is already running)
   EpgDbQueue_Clear(pBlockQueue);
}

// ---------------------------------------------------------------------------
// Free resources
//
void EpgStreamClear( void )
{
   if (pBlockQueue != NULL)
   {
     EpgDbQueue_Clear(pBlockQueue);
     pBlockQueue = NULL;
   }
}

