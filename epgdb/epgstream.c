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
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgstream.c,v 1.4 2000/06/13 18:16:20 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/hamming.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgtxtdump.h"
#include "epgdb/epgstream.h"


// ----------------------------------------------------------------------------
// internal status
//

static NXTV_STREAM streamData[2];
static uchar       streamOfPage;
static EPGDB_BLOCK * pScratchBuffer;
static uint        epgStreamAppId;
static bool        enableAllTypes;


// ----------------------------------------------------------------------------
// Append the converted block to a temporary buffer
// - the buffer is neccessary because more than one block end in one packet,
//   so we can not simply return a block address from the stream decoder.
//
static void EpgStreamAddBlockToScratch( EPGDB_BLOCK *pBlock )
{
   EPGDB_BLOCK *pWalk;

   if (pBlock != NULL)
   {
      dprintf2("SCRATCH ADD type=%d ptr=%lx\n", pBlock->type, (ulong)pBlock);

      if (pScratchBuffer != NULL)
      {
         pWalk = pScratchBuffer;
         while (pWalk->pNextBlock != NULL)
         {
            pWalk = pWalk->pNextBlock;
         }
         pWalk->pNextBlock = pBlock;
      }
      else
         pScratchBuffer = pBlock;
   }
   else
      debug0("EpgStream-AddBlockToScratch: called with NULL ptr");
}

// ----------------------------------------------------------------------------
// Get the first block from the scratch buffer
//
EPGDB_BLOCK * EpgStreamGetNextBlock( void )
{
   EPGDB_BLOCK * pBlock;

   if (pScratchBuffer != NULL)
   {
      pBlock = pScratchBuffer;
      pScratchBuffer = pBlock->pNextBlock;
      pBlock->pNextBlock = NULL;
   }
   else
      pBlock = NULL;

   return pBlock;
}

// ----------------------------------------------------------------------------
// Return a block with the given type from the scratch buffer
//
EPGDB_BLOCK * EpgStreamGetBlockByType( uchar type )
{
   EPGDB_BLOCK *pWalk, *pPrev;

   pPrev = NULL;
   pWalk = pScratchBuffer;
   while (pWalk != NULL)
   {
      if (pWalk->type == type)
      {  // found -> remove block from chain
         if (pPrev != NULL)
            pPrev->pNextBlock = pWalk->pNextBlock;
         else
            pScratchBuffer = pWalk->pNextBlock;

         pWalk->pNextBlock = NULL;
         return pWalk;
      }
      pPrev = pWalk;
      pWalk = pWalk->pNextBlock;
   }

   return NULL;
}

// ----------------------------------------------------------------------------
// Free all blocks in the scratch buffer
//
void EpgStreamClearScratchBuffer( void )
{
   EPGDB_BLOCK *pNext;

   while (pScratchBuffer != NULL)
   {
      pNext = pScratchBuffer->pNextBlock;
      free(pScratchBuffer);
      pScratchBuffer = pNext;
   }
}

// ----------------------------------------------------------------------------
// Take a pointer the control and string bit fields from the block decoder and
// convert them to a C "compound" structure depending on the type qualifier
//
static void EpgStreamConvertBlock( const uchar *pBuffer, uint blockLen, uchar stream, uchar type )
{
   EPGDB_BLOCK *pBlock;
   uint ctrlLen = pBuffer[3] | ((uint)(pBuffer[4]&0x03)<<8);
   uint strLen = blockLen - (ctrlLen + 2)*2;

   switch (type)
   {
      case EPGDBACQ_TYPE_BI:
         pBlock = EpgBlockConvertBi(pBuffer, ctrlLen);
         EpgTxtDumpBi(stdout, &pBlock->blk.bi, stream);
         break;
      case EPGDBACQ_TYPE_AI:
         pBlock = EpgBlockConvertAi(pBuffer, ctrlLen, strLen);
         EpgTxtDumpAi(stdout, &pBlock->blk.ai, stream);
         break;
      case EPGDBACQ_TYPE_PI:
         pBlock = EpgBlockConvertPi(pBuffer, ctrlLen, strLen);
         if (pBlock != NULL)
         {
            pBlock->stream = stream;
            EpgTxtDumpPi(stdout, &pBlock->blk.pi, stream, NULL);
         }
         break;
      case EPGDBACQ_TYPE_NI:
         pBlock = EpgBlockConvertNi(pBuffer, ctrlLen, strLen);
         EpgTxtDumpNi(stdout, &pBlock->blk.ni, stream);
         break;
      case EPGDBACQ_TYPE_OI:
         pBlock = EpgBlockConvertOi(pBuffer, ctrlLen, strLen);
         EpgTxtDumpOi(stdout, &pBlock->blk.oi, stream);
         break;
      case EPGDBACQ_TYPE_MI:
         pBlock = EpgBlockConvertMi(pBuffer, ctrlLen, strLen);
         EpgTxtDumpMi(stdout, &pBlock->blk.mi, stream);
         break;
      case EPGDBACQ_TYPE_LI:
         pBlock = EpgBlockConvertLi(pBuffer, ctrlLen, strLen);
         EpgTxtDumpLi(stdout, &pBlock->blk.li, stream);
         break;
      case EPGDBACQ_TYPE_TI:
         pBlock = EpgBlockConvertTi(pBuffer, ctrlLen, strLen);
         EpgTxtDumpTi(stdout, &pBlock->blk.ti, stream);
         break;
      case EPGDBACQ_TYPE_HI:
      case EPGDBACQ_TYPE_UI:
      case EPGDBACQ_TYPE_CI:
      default:
         EpgTxtDumpUnknown(stdout, type);
         pBlock = NULL;
         break;
   }

   if (pBlock != NULL)
   {
      if (enableAllTypes || (type <= EPGDBACQ_TYPE_AI))
      {
         pBlock->stream = stream;
         EpgStreamAddBlockToScratch(pBlock);
      }
      else
         free(pBlock);
   }
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
   uint type, tmp, ctrlLen, strLen, chkSum, myChkSum;

   if (psd->appID == 0)
   { // Bundle Inventory (BI block)
      UnHam84Array(psd->blockBuf, psd->blockLen / 2);
      if (!hamErr)
      {
         chkSum = psd->blockBuf[2];
         psd->blockBuf[2] = 0;  // checksum does not include itself
         myChkSum = EpgStreamComputeChkSum(psd->blockBuf, psd->blockLen / 2);
         if (!hamErr && (chkSum == myChkSum))
         {
            psd->blockBuf[2] = chkSum;
            EpgStreamConvertBlock(psd->blockBuf, psd->blockLen/2, stream, EPGDBACQ_TYPE_BI);
         }
         else
            debug3("BI block chksum error: my %x != %x, len=%d", myChkSum, chkSum, psd->blockLen);
      }
      else
         debug0("BI block hamming error");
   }
   else if (psd->appID == epgStreamAppId)
   {  // All kinds of EPG blocks
      tmp = UnHam84Word(psd->blockBuf + 6);
      type = tmp >> 10;
      ctrlLen = tmp & 0x3ff;

      if (!hamErr && (psd->blockLen >= (ctrlLen+2) * 2))
      {
         strLen = psd->blockLen - (ctrlLen + 2)*2;
         UnHam84Array(psd->blockBuf, ctrlLen + 2);
         UnHamParityArray(psd->blockBuf + (ctrlLen+2)*2, psd->blockBuf + ctrlLen+2, strLen);

         chkSum = psd->blockBuf[2];
         psd->blockBuf[2] = 0;  // chksum does not include itself
         myChkSum = EpgStreamComputeChkSum(psd->blockBuf, ctrlLen + 2);
         if (!hamErr && (chkSum == myChkSum))
         {
            psd->blockBuf[2] = chkSum;
            // real block size = header + control-data + string-data
            EpgStreamConvertBlock(psd->blockBuf, 2 + ctrlLen + strLen, stream, type);
         }
         else
            debug2("block chksum error: my %x != %x", myChkSum, chkSum);
      }
      else
          debug2("block hamming or length error block=%d ctrl=%d", psd->blockLen, ctrlLen);
   }
   else
      debug1("unknown appID=%d", psd->appID);
}

// ---------------------------------------------------------------------------
// Process EPG data packet
// - the function uses two separate internal states the two streams
// - a single teletext packet can produce several blocks
//
void EpgStreamDecodePacket( uchar packNo, const uchar * dat )
{
   NXTV_STREAM * const psd = &streamData[streamOfPage];
   uchar bs, blockPtr;
   uchar c1,c2,c3,c4;
   uint  restLine;

   if ( (streamOfPage < NXTV_NO_STREAM) &&
        (packNo <= streamData[streamOfPage].pkgCount) )
   {
      if ((psd->haveBlock || psd->haveHeader) &&
          (packNo != psd->lastPkg + 1))
      { // packet missing
        debug2("missing packet %d (have %d) - discard block", psd->lastPkg + 1, packNo);
        psd->haveHeader = FALSE;
        psd->haveBlock = FALSE;
      }
      psd->lastPkg = packNo;

      hamErr = 0;
      blockPtr = UnHam84Nibble(dat);

      if (!hamErr && (blockPtr <= 0x0d))
      {
         blockPtr = 1 + 3 * blockPtr;

         if (psd->haveHeader)
         {  // continuation of the header fragment in the last packet
            assert(psd->haveHeader < 4+1);
            psd->haveHeader -= 1;
            for (restLine=0; restLine < psd->haveHeader; restLine++)
               psd->blockBuf[restLine] = psd->headerFragment[restLine];
            for ( ; restLine < 4; restLine++)
               psd->blockBuf[restLine] = dat[1 + restLine - psd->haveHeader];
            c1 = UnHam84Nibble(psd->blockBuf + 0);
            c2 = UnHam84Nibble(psd->blockBuf + 1);
            c3 = UnHam84Nibble(psd->blockBuf + 2);
            c4 = UnHam84Nibble(psd->blockBuf + 3);
            if (!hamErr)
            {
               psd->appID    = c1 | ((c2 & 1) << 4);
               psd->blockLen = ((c2 >> 1) | (c3 << 3) | (c4 << 7)) + 4;  // + length of block len itself
               psd->recvLen  = 0;

               if ((blockPtr == 40) || (blockPtr - 1 >= psd->blockLen - psd->haveHeader))
               {
                  restLine = blockPtr - 1 - (4 - psd->haveHeader);
                  if ((psd->blockLen - 4) < restLine)
                     // ToDo: assert diff<3 && filler bytes==ham48(0x03)
                     restLine = psd->blockLen - 4;
                  dprintf6("pkg=%d, BP=%d: start frag. block recv stream %d, appID=%d, len=%d, startlen=%d\n", packNo, blockPtr, streamOfPage+1, psd->appID, psd->blockLen, restLine+4);
                  memcpy(psd->blockBuf + 4, dat + 1 + (4 - psd->haveHeader), restLine);
                  psd->recvLen = restLine + 4;
                  if (psd->recvLen >= psd->blockLen)
                  {  // Block complete
                     dprintf1("pkg=%d: short block complete\n", packNo);
                     EpgStreamCheckBlock(psd, streamOfPage);
                  }
                  else
                     psd->haveBlock = TRUE;
               }
               else
               {  // nicht genug Restdaten im packet
                  debug5("pkg=%d: too few data for frag. block rest len: %d < %d, stream=%d appID=%d", packNo, blockPtr-1, psd->blockLen - psd->haveHeader, streamOfPage, psd->appID);
               }
            }
            else
            {  // hamming error
               debug0("structure header hamming error - skipping block");
            }
            psd->haveHeader = FALSE;
         }
         else if (psd->haveBlock)
         {  // append data to current block
            restLine = psd->blockLen - psd->recvLen;
            if (restLine > 39)
               restLine = 39;
            if (blockPtr - 1 >= restLine)
            {
               memcpy(psd->blockBuf+psd->recvLen, dat+1, restLine);
               psd->recvLen += restLine;
               if (psd->recvLen >= psd->blockLen)
               {  // Block complete
                  dprintf4("pkg=%d, BP=%d: completed block with %d bytes, sum=%d\n", packNo, blockPtr, restLine, psd->recvLen);
                  EpgStreamCheckBlock(psd, streamOfPage);
                  psd->haveBlock = FALSE;
               }
               else dprintf4("pkg=%d, BP=%d: added %d bytes to block, sum=%d\n", packNo, blockPtr, restLine, psd->recvLen);
            }
            else
            {
               debug7("pkg=%d: too few data for block rest len: %d < %d of %d, stream=%d app=%d, type=%d",
                      packNo, blockPtr-1, restLine, psd->blockLen - psd->recvLen, streamOfPage, psd->appID, (UnHam84Word(psd->blockBuf + 6) >> 10));
               psd->haveBlock = FALSE;
            }
         }
         else if (blockPtr == 40) dprintf1("BS=0xD -> no block start in this packet %d\n", packNo);

         while (blockPtr < 40)
         { // start of at least one new structure in this packet
            hamErr = 0;
            if (blockPtr >= 36)
            {  // part of the header is in the next packet
               bs = UnHam84Nibble(dat + blockPtr);
               if (bs == 0x0c)
               {
                  psd->haveHeader = 40 - blockPtr;  // 1+ Anz. Bytes im Buffer
                  dprintf3("pkg=%d, BP=%d: start new block with %d byte header fragment\n", packNo, blockPtr, psd->haveHeader-1);
                  for (restLine=0; restLine < psd->haveHeader-1; restLine++)
                  {
                     psd->headerFragment[restLine] = dat[blockPtr + 1 + restLine];
                  }
               }
               else
                  debug3("pck %d, BP=%d: discard new block header fragment, BS=%x", packNo, blockPtr, bs);
               blockPtr = 40;
            }
            else
            {
               bs = UnHam84Nibble(dat + blockPtr);
               c1 = UnHam84Nibble(dat + blockPtr + 1);
               c2 = UnHam84Nibble(dat + blockPtr + 2);
               c3 = UnHam84Nibble(dat + blockPtr + 3);
               c4 = UnHam84Nibble(dat + blockPtr + 4);
               if (!hamErr)
               {
                  psd->appID    = c1 | ((c2 & 1) << 4);
                  psd->blockLen = ((c2 >> 1) | (c3 << 3) | (c4 << 7)) + 4;  // + length of block len itself
                  psd->recvLen  = 0;

                  if (bs == 0x0c)
                  {
                     psd->haveBlock = TRUE;
                     restLine = 40 - (blockPtr + 1);
                     if (restLine > psd->blockLen)
                        restLine = psd->blockLen;
                     dprintf6("pkg=%d, BP=%d: start block recv stream %d, appID=%d, len=%d, startlen=%d\n", packNo, blockPtr, streamOfPage+1, psd->appID, psd->blockLen, restLine);
                     memcpy(psd->blockBuf, dat+blockPtr+1, restLine);
                     psd->recvLen = restLine;
                     blockPtr += 1 + restLine;
                     if (psd->recvLen >= psd->blockLen)
                     { // Block complete
                        dprintf2("pkg=%d, BP=%d: short block complete\n", packNo, blockPtr);
                        EpgStreamCheckBlock(psd, streamOfPage);
                        psd->haveHeader = FALSE;
                        psd->haveBlock = FALSE;
                        // Filler bytes ueberspringen
                        while ((blockPtr < 40) && (UnHam84Nibble(dat + blockPtr) == 0x03))
                        {
                           dprintf2("pkg=%d, BP=%d: skipping filler byte\n", packNo, blockPtr);
                           blockPtr += 1;
                        }
                     }
                  }
                  else
                  {  // decoding error - skip this packet
                     debug3("struct header error: appID=%x blockLen=%x bs=%x - skipping block", psd->appID, psd->blockLen, bs);
                     psd->haveHeader = FALSE;
                     psd->haveBlock = FALSE;
                     blockPtr = 40;
                  }
               }
               else
               {  // hamming error
                  debug0("structure header hamming error - skipping block");
                  psd->haveHeader = FALSE;
                  psd->haveBlock = FALSE;
                  blockPtr = 40;
               }
            }
         }
      }
      else
      {
         debug2("packet hamming error in BP=%x - discard packet %d", blockPtr, packNo);
         psd->haveHeader = FALSE;
         psd->haveBlock = FALSE;
      }
   }
   else
      debug3("EpgStream-DecodePacket: invalid stream=%d or packet=%d > %d", streamOfPage, packNo, streamData[streamOfPage].pkgCount);
}

// ---------------------------------------------------------------------------
// Process a newly acquire teletext header packet
// - returns TRUE if this is an EPG page
//
bool EpgStreamNewPage( uint sub )
{
   uchar newCi, newPkgCount, firstPkg;
   bool  result = FALSE;

   // Stream-number fuer folgende Seite merken
   switch ((sub & 0xf00) >> 8)
   {
      case 0:
         streamOfPage = NXTV_STREAM_NO_1;
         break;
      case 1:
         streamOfPage = NXTV_STREAM_NO_2;
         break;
      default:
         debug1("EpgStream-NewPage: unexpected stream number %d - ignoring page", sub & 0xf00);
         streamOfPage = NXTV_NO_STREAM;
         break;
   }

   if (streamOfPage != NXTV_NO_STREAM)
   {
      // Anzahl packages mit Nutzdaten in der folgenden Seite
      newPkgCount = ((sub & 0x3000) >> (12-3)) | ((sub & 0x70) >> 4);
      if (newPkgCount <= 25)
      {
         newCi = sub & 0xf;
         firstPkg = 0;

         if (streamData[streamOfPage].haveBlock || streamData[streamOfPage].haveHeader)
         {  // unvollstaendiger block im Buffer - Kontinuitaet pruefen

            if ( streamData[streamOfPage].ci == newCi )
            {  // fragmentierte Uebertragung der Seite -> Fortsetzung ab letzter pkg-no +1
               dprintf2("repetition of CI=%d in stream %d\n", newCi, streamOfPage);
               firstPkg = streamData[streamOfPage].lastPkg;
            }
            else if ( ((streamData[streamOfPage].ci + 1) & 0xf) != newCi )
            {  // Luecke in Seitennumerierung -> unvollstaendiger Block muss verworfen werden
               debug3("EpgStream-NewPage: page continuity error (%d -> %d) - discard current block in stream %d", streamData[streamOfPage].ci, newCi, streamOfPage);
               streamData[streamOfPage].haveHeader = FALSE;
               streamData[streamOfPage].haveBlock = FALSE;
            }
            else if (streamData[streamOfPage].lastPkg != streamData[streamOfPage].pkgCount)
            {  // packets am Ende der letzten Seite fehlen -> unvollstaendiger Block
               debug2("EpgStream-NewPage: packets missing at page end: %d < %d", streamData[streamOfPage].lastPkg, streamData[streamOfPage].pkgCount);
               streamData[streamOfPage].haveHeader = FALSE;
               streamData[streamOfPage].haveBlock = FALSE;
            }
         }

         dprintf4("new page: CI=%d stream=%d pkgno=%d last=%d\n", newCi, streamOfPage, newPkgCount, firstPkg);
         streamData[streamOfPage].ci = newCi;
         streamData[streamOfPage].pkgCount = newPkgCount;
         streamData[streamOfPage].lastPkg = firstPkg;
         result = TRUE;
      }
      else
      {
         debug2("EpgStream-NewPage: too many packets (%d) for page in stream %d", streamData[streamOfPage].pkgCount, streamOfPage);
         streamOfPage = NXTV_NO_STREAM;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Allow all block types into scratch buffer from now on
//
void EpgStreamEnableAllTypes( void )
{
   enableAllTypes = TRUE;
}

// ---------------------------------------------------------------------------
// Initialize the internal decoder state
//
void EpgStreamInit( bool bWaitForBiAi, uint appId )
{
   memset(&streamData, 0, sizeof(streamData));
   streamOfPage = NXTV_NO_STREAM;
   pScratchBuffer = NULL;
   epgStreamAppId = appId;
   enableAllTypes = !bWaitForBiAi;
}

