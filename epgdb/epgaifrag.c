/*
 *  Nextview AI block fragments assembly
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
 *    This module assembles AI blocks from nonsequential teletext
 *    packets.  It's purpose is to allow to decode the block even
 *    under bad reception conditions when the block can never once
 *    be received completely without errors (i.e. decoding errors in
 *    the teletext packet header or the EPG control data.)
 *
 *    The module maintains two buffers in which undecoded control and
 *    text data is stored. It also maintains an offset into these buffers
 *    which is advanced for every incoming teletext packet, and also for
 *    gaps (detected by jumps in the teletext packet numbers.)  Data from
 *    incoming packets is written to the buffers at the current offset.  
 *    At the end of each block the control buffer is checked if its
 *    decodable; if yes, the block is added to the output queue.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgaifrag.c,v 1.4 2020/06/17 19:33:00 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_STREAM
#define DPRINTF_OFF

#include <stdio.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/hamming.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgstream.h"
#include "epgdb/epgaifrag.h"


// ----------------------------------------------------------------------------
// Module state

#define INVALID_HAMMING_84_CODE  0x01
#define INVALID_PARITY_CODE      0x00
#define AI_HEAD_BUF_LEN          (14 * 2)

typedef struct
{
   uchar        ctrlData[1024*2];       // buffer for control data, Hamming-84 encoded
   uchar        textData[2048];         // buffer for text data, parity encoded
   uchar        recvData[2048/8];       // bit array to mark received bytes
   bool         haveBlock;              // TRUE if ctrlData contains a (partial) block
   bool         activeBlock;            // TRUE if reception of new AI block is ongoing
   bool         completeBlock;          // TRUE if checksum is OK

   uint         blockLen;               // decoded control data elements
   uint         chkSum;
   uint         ctrlLen;
   uint         vers_1;
   uint         vers_2;

   uint         pagePkgCount;           // # of TTX packets on the current page
   uint         lastPagePkg;            // number of the last TTX packet
   uint         dataOff;                // offset into ctrlData/textData for the next packet

   uchar        newHead[AI_HEAD_BUF_LEN]; // temp buffer for assembling header of new blocks
   bool         haveHead;               // TRUE if reception of new block is ongoing
} EPG_AI_FRAG_STATE;

static EPG_AI_FRAG_STATE aifrag;

// ----------------------------------------------------------------------------
// Mark range covered by incoming data as received
//
static void EpgAiFrag_MarkReceived( uint lineOff, uint pkgLen )
{
   uint idx;
   uint off;

   off = lineOff / 8;
   if ((lineOff & 7) && (off < sizeof(aifrag.recvData)))
   {
      for (idx = (lineOff & 7); (idx < 8) && (pkgLen > 0); idx++)
      {
         aifrag.recvData[off] |= 1 << idx;
         pkgLen -= 1;
      }
      off++;
   }

   for ( ; (pkgLen > 8) && (off < sizeof(aifrag.recvData)); pkgLen -= 8)
   {
      aifrag.recvData[off++] = 0xff;
   }

   if ((pkgLen > 0) && (off < sizeof(aifrag.recvData)))
   {
      for (idx = 0; idx < pkgLen; idx++)
      {
         aifrag.recvData[off] |= 1 << idx;
      }
   }
}

// ----------------------------------------------------------------------------
// Check if all packets have been received
//
static bool EpgAiFrag_CheckReceived( void )
{
   uint off;
   uint idx;
   bool result;

   off = 28/8;
   result = (aifrag.recvData[off++] == 0xF0);
   if (result == FALSE)
      dprintf0("AiFrag: reception gap at start\n");

   if (result)
   {
      for ( ; off < (aifrag.blockLen / 8); off++)
      {
         if (aifrag.recvData[off] != 0xFF)
         {
            dprintf2("AiFrag: reception gap around offset %d (block len %d)\n", off*8, aifrag.blockLen);
            result = FALSE;
            break;
         }
      }
   }

   if (result)
   {
      for (idx = 0; idx < (aifrag.blockLen & 8); off++)
      {
         if ((aifrag.recvData[off] & (1 << idx)) == 0)
         {
            dprintf1("AiFrag: reception gap at block end (block len %d)\n", aifrag.blockLen);
            result = FALSE;
            break;
         }
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Check if a complete AI block has been received and control data is correct
//
static void EpgAiFrag_CheckBlock( void )
{
   const uchar *pdat;
   uchar chkSumBuf[2];
   uint idx;
   uint chkSum;
   schar c1;
   bool  result;

   // first check if all packets were received (esp. text data)
   result = EpgAiFrag_CheckReceived();
   if (result)
   {
      chkSumBuf[0] = aifrag.ctrlData[4];
      chkSumBuf[1] = aifrag.ctrlData[5];
      aifrag.ctrlData[4] = 5;  // checkSum does not include itself
      aifrag.ctrlData[5] = 5;  // 5 is ham84(0)

      pdat = aifrag.ctrlData;
      chkSum = 0;

      // decode hamming-84 and sum up all decoded values
      for (idx = 0; idx < aifrag.ctrlLen * 2; idx++)
      {
         if (UnHam84Nibble(pdat, &c1))
         {
            chkSum += c1;
            pdat += 1;
         }
         else
         {  // Hamming decoding error -> abort
            result = FALSE;
            break;
         }
      }
      if (result)
      {
         chkSum = (0x100 - (chkSum & 0xff)) & 0xff;

         if (chkSum == aifrag.chkSum)
         {
            aifrag.ctrlData[4] = chkSumBuf[0];
            aifrag.ctrlData[5] = chkSumBuf[1];

            aifrag.completeBlock = TRUE;
            dprintf5("AiFrag: block complete blen=%d sum=%d ctlen=%d v1=%d v2=%d\n", aifrag.blockLen, aifrag.chkSum, aifrag.ctrlLen, aifrag.vers_1, aifrag.vers_2);
         }
         else
            dprintf2("AiFrag: checksum error: 0x%02X expect:0x%02X\n", chkSum, aifrag.chkSum);
      }
      else
         dprintf0("AiFrag: hamming decoding error (block may be incomplete)\n");
   }
}

// ----------------------------------------------------------------------------
// Process header of an incoming block
// - decode the data - discard if hamming not OK
//   (note: checksum cannot be validated yet, so there is a slight chance of errors)
// - if it's an AI, enter AI assembly mode and copy data to fragment buffer
//
static void EpgAiFrag_DecodeHeader( void )
{
   uint  blockLen, chkSum, ctrlLen, blockType, vers_1, vers_2;
   uchar headCopy[AI_HEAD_BUF_LEN];

   // backup header data before overwriting with hamming-decoded data
   memcpy(headCopy, aifrag.newHead, AI_HEAD_BUF_LEN);

   if (UnHam84Array(aifrag.newHead, aifrag.newHead, sizeof(aifrag.newHead) / 2))
   {
      blockLen  = ((uint)(aifrag.newHead[0] >> 5) | (aifrag.newHead[1] << 3)) + 4;  // + length of block len itself
      chkSum    = aifrag.newHead[2];
      ctrlLen   = ((uint)aifrag.newHead[3] | ((aifrag.newHead[4] & 0x03)<<8)) + 2;
      blockType = (aifrag.newHead[4]>>2);
      vers_1    = (aifrag.newHead[5]>>4) | ((aifrag.newHead[6] & 0x03)<<4);
      vers_2    = (aifrag.newHead[6]>>2);

      // check if it's an AI block
      if (blockType == EPGDBACQ_TYPE_AI)
      {
         if (aifrag.haveBlock)
         {
            // already have an AI fragment: check if it's the same AI version as new one
            if ( (blockLen != aifrag.blockLen) ||
                 (chkSum != aifrag.chkSum) ||
                 (ctrlLen != aifrag.ctrlLen) ||
                 (vers_1 != aifrag.vers_1) ||
                 (vers_2 != aifrag.vers_2) )
            {
               dprintf5("AiFrag: AI block version change: old blen=%d sum=%d ctlen=%d v1=%d v2=%d\n", aifrag.blockLen, aifrag.chkSum, aifrag.ctrlLen, aifrag.vers_1, aifrag.vers_2);
               dprintf5("AiFrag:                          new blen=%d sum=%d ctlen=%d v1=%d v2=%d\n", blockLen, chkSum, ctrlLen, vers_1, vers_2);

               // not identical: discard the old fragment on the buffer
               // XXX note this decision may be wrong in case of undetected decoding errors
               aifrag.haveBlock = FALSE;
               aifrag.completeBlock = FALSE;
            }
         }

         if (aifrag.haveBlock == FALSE)
         {
            dprintf2("AiFrag: start AI block assembly: len=%d ver=%d\n", blockLen, vers_1);

            // first fragment: initialize the fragment buffer
            memset(aifrag.ctrlData, INVALID_HAMMING_84_CODE, sizeof(aifrag.ctrlData));
            memset(aifrag.textData, INVALID_PARITY_CODE, sizeof(aifrag.textData));
            memset(aifrag.recvData, 0, sizeof(aifrag.recvData));

            memcpy(aifrag.ctrlData, headCopy, sizeof(aifrag.newHead));
            aifrag.blockLen = blockLen;
            aifrag.chkSum = chkSum;
            aifrag.ctrlLen = ctrlLen;
            aifrag.vers_1 = vers_1;
            aifrag.vers_2 = vers_2;
         }
         else
            dprintf0("AiFrag: add new AI block\n");

         aifrag.haveBlock = TRUE;
         aifrag.activeBlock = TRUE;
      }
      else
         dprintf1("AiFrag: not an AI block: type %d\n", blockType);
   }
   else
      dprintf0("AiFrag: header decoding error (or short block)\n");
}

// ----------------------------------------------------------------------------
// Append incoming control data
//
static uint EpgAiFrag_AddControlData( const uchar * pData, uint pkgLen )
{
   uchar * pDst;
   schar c1;
   uint  restLen;
   uint  idx;

   if (aifrag.dataOff < aifrag.ctrlLen * 2)
   {
      restLen = aifrag.ctrlLen * 2 - aifrag.dataOff;

      if (pkgLen > restLen)
         pkgLen = restLen;

      EpgAiFrag_MarkReceived(aifrag.dataOff, pkgLen);

      dprintf2("AiFrag: off=%d add %d bytes to CTRL\n", aifrag.dataOff, pkgLen);

      pDst = aifrag.ctrlData + aifrag.dataOff;
      for (idx = 0; idx < pkgLen; idx++)
      {
         if (UnHam84Nibble(pData, &c1))
         {
            *pDst = *pData;
         }
         pDst += 1;
         pData += 1;
      }

      aifrag.dataOff += pkgLen;
   }
   else
      pkgLen = 0;

   return pkgLen;
}

// ----------------------------------------------------------------------------
// Append incoming text data
//
static uint EpgAiFrag_AddTextData( const uchar * pData, uint pkgLen )
{
   uchar * pDst;
   schar  c1;
   uint   restLen;
   uint   idx;

   if ( (aifrag.dataOff >= aifrag.ctrlLen * 2) &&
        (aifrag.dataOff < aifrag.blockLen) )
   {
      restLen = aifrag.blockLen - aifrag.dataOff;

      if (pkgLen > restLen)
         pkgLen = restLen;

      EpgAiFrag_MarkReceived(aifrag.dataOff, pkgLen);

      dprintf2("AiFrag: off=%d add %d bytes to TEXT\n", aifrag.dataOff, pkgLen);

      pDst = aifrag.textData + aifrag.dataOff - aifrag.ctrlLen * 2;
      for (idx = 0; idx < pkgLen; idx++)
      {
         if (UnHamParityByte(pData, &c1) || (*pDst == INVALID_PARITY_CODE))
         {
            *pDst = *pData;
         }
         pDst += 1;
         pData += 1;
      }

      aifrag.dataOff += pkgLen;
   }
   else
      pkgLen = 0;

   return pkgLen;
}

// ----------------------------------------------------------------------------
// Append incoming data
//
static void EpgAiFrag_AddData( const uchar * pData, uint pkgLen )
{
   uint headLen;
   uint lineOff = 0;

   if (aifrag.haveHead)
   {
      if (aifrag.dataOff + pkgLen > AI_HEAD_BUF_LEN)
         headLen = AI_HEAD_BUF_LEN - aifrag.dataOff;
      else
         headLen = pkgLen;

      memcpy(aifrag.newHead + aifrag.dataOff, pData, headLen);
      aifrag.dataOff += headLen;
      lineOff += headLen;

      if (aifrag.dataOff >= AI_HEAD_BUF_LEN)
      {
         // header is complete -> decode and check block type
         EpgAiFrag_DecodeHeader();

         aifrag.haveHead = FALSE;
      }
   }

   if (aifrag.activeBlock)
   {
      // control data: copy decodable bytes into data array
      lineOff += EpgAiFrag_AddControlData(pData + lineOff, pkgLen - lineOff);

      // string data: copy bytes with correct parity
      lineOff += EpgAiFrag_AddTextData(pData + lineOff, pkgLen - lineOff);

      if (aifrag.dataOff >= aifrag.blockLen)
      {
         // end of block reached -> check if block is decodable
         EpgAiFrag_CheckBlock();

         aifrag.activeBlock = FALSE;
      }
   }
}

// ----------------------------------------------------------------------------
// Add raw teletext data package
// - called for every incoming teletext packet on EPG pages
//   (i.e. regardless of stream decoder state)
// - check for gaps in the packet sequence and for the sync word (BP)
// - note during block assembly its mandatory to advance the buffer offset for
//   each incoming or missing packet, so that data is copied to the correct place
//
void EpgAiFragmentsAddPkg( uint stream, uint pkgNo, const uchar * pData )
{
   schar c1;
   uint  pkgLen;

   assert(!(aifrag.haveHead && aifrag.activeBlock));
   assert(!(aifrag.activeBlock && !aifrag.haveBlock));

   if (stream == NXTV_STREAM_NO_1)
   {
      // advance offset for missing packets
      if (pkgNo > aifrag.lastPagePkg + 1)
      {
         dprintf1("AiFrag: missing %d ttx packets\n", pkgNo - aifrag.lastPagePkg - 1);

         aifrag.dataOff += (pkgNo - aifrag.lastPagePkg - 1) * 39;
         // if packets missing during header: discard
         aifrag.haveHead = FALSE;
      }
      else if (pkgNo != aifrag.lastPagePkg + 1)
      {
         ifdebug3(aifrag.haveHead|aifrag.activeBlock, "AI-frag: packet seq. error %d of %d (have %d) - discard block", aifrag.lastPagePkg + 1, aifrag.pagePkgCount, pkgNo);
         aifrag.haveHead = FALSE;
         aifrag.activeBlock = FALSE;
      }

      // decode BP (byte pointer): if decodable, must be >= restlen
      if ( UnHam84Nibble(pData, &c1) && (c1 <= 0x0d) )
         pkgLen = 3 * c1;
      else
         pkgLen = 39;

      EpgAiFrag_AddData(pData + 1, pkgLen);

      if (pkgLen < 39)
         aifrag.activeBlock = FALSE;

      aifrag.lastPagePkg = pkgNo;
   }
}

// ----------------------------------------------------------------------------
// Start of a new page in the stream
// - called for every new teletext page with EPG data (if header is decoded correctly)
// - block assembly is aborted upon mising pages because most blocks will not span
//   across 3 blocks (and also we cannot be 100% sure how many packets were on the
//   missed page)
//
void EpgAiFragmentsStartPage( uint stream, uint firstPkg, uint newPkgCount )
{
   if (stream == NXTV_STREAM_NO_1)
   {
      if (aifrag.haveHead)
      {
         // abort in case of missing packets (block type not known yet)
         if (aifrag.lastPagePkg != aifrag.pagePkgCount)
         {
            aifrag.haveHead = FALSE;
         }
      }
      if (aifrag.activeBlock)
      {
         // take note of missing packets at the end of the last page
         if (aifrag.lastPagePkg != aifrag.pagePkgCount)
         {
            aifrag.dataOff += (aifrag.pagePkgCount - aifrag.lastPagePkg) * 39;
         }
      }
      // store packet count for the new page
      aifrag.pagePkgCount = newPkgCount;
      aifrag.lastPagePkg = firstPkg;
   }
}

// ----------------------------------------------------------------------------
// Start of a new block in the stream
// - called for any type of block except BI, i.e. AI but also all PI, NI etc.
//   because it's not known yet which type the block will have
//
void EpgAiFragmentsBlockStart( const uchar * pHead, uint blockLen, const uchar * pData, uint pkgLen )
{
   if (aifrag.activeBlock)
   {
      EpgAiFrag_CheckBlock();
      aifrag.activeBlock = FALSE;
   }

   // copy header into temporary buffer
   // (don't overwrite main buffer until block type and version can be checked)
   memcpy(aifrag.newHead, pHead, 4);
   aifrag.dataOff = 4;
   aifrag.haveHead = TRUE;

   EpgAiFrag_AddData(pData, pkgLen);
}

// ----------------------------------------------------------------------------
// Abort reception of current block after break in the page sequence
// - already stored data is not affected
//
void EpgAiFragmentsBreak( uint stream )
{
   if (stream == NXTV_STREAM_NO_1)
   {
      aifrag.haveHead = FALSE;
      aifrag.activeBlock = FALSE;
   }
}

// ----------------------------------------------------------------------------
// Discard data and reset the decoder state
// - called by the streams decoder when an AI block is added to the output queue
// - the call occurs before polling this module for a result, so we safely
//   avoid adding the AI block to the queue twice
//
void EpgAiFragmentsRestart( void )
{
   if (aifrag.completeBlock)
      dprintf0("AiFrag: restart: fragments not needed\n");

   aifrag.completeBlock = FALSE;
   aifrag.haveBlock = FALSE;
   aifrag.haveHead = FALSE;
   aifrag.activeBlock = FALSE;
}

// ----------------------------------------------------------------------------
// Retrieve a decoded block
// - the block is assembled from data in the control and text buffers, if the
//   checksum was successfully verified before; else NULL is returned
// - a separate buffer is used to avoid overwriting the data during Hamming and
//   parity decoding, esp. to allow continuous reception of text data which is
//   only parity protected
// - the caller must release the returned buffer using xfree()
//
const uchar * EpgAiFragmentsAssemble( uint * pBlockLen, uint * pParityErrCnt )
{
   uchar * pBuffer;
   uint errCnt;

   if (aifrag.completeBlock)
   {
      pBuffer = xmalloc(aifrag.blockLen);

      // decode and copy into the target buffer
      UnHam84Array(aifrag.ctrlData, pBuffer, aifrag.ctrlLen);

      errCnt = UnHamParityArray(aifrag.textData, pBuffer + aifrag.ctrlLen,
                                aifrag.blockLen - aifrag.ctrlLen * 2);

      aifrag.completeBlock = FALSE;
      dprintf1("AiFrag: ----> deliver block: %d parity err\n", errCnt);

      if (pBlockLen != NULL)
         *pBlockLen = aifrag.blockLen;
      if (pParityErrCnt != NULL)
         *pParityErrCnt = errCnt;
   }
   else
      pBuffer = NULL;

   return pBuffer;
}

