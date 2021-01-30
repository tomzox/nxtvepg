/*
 *  Teletext page grabber
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
 *    Grabs a given range of teletext pages plus CNIs and forwards
 *    them to an external EPG parser.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: ttxgrab.c,v 1.20 2020/12/23 17:25:31 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgttx/ttx_cif.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/hamming.h"
#include "epgdb/ttxgrab.h"

#ifdef USE_TTX_GRABBER

#define XML_OUT_FILE_PREFIX   "ttx-"
#define XML_OUT_FILE_EXT      ".xml"
#define XML_TMP_FILE_EXT      ".tmp"  // appended after .xml
#define TTX_CAP_FILE_EXT      ".dat"

#ifndef O_BINARY
#define O_BINARY       0          // for M$-Windows only
#endif

#define MODULO_ADD(A,B,M)  (((A)+(B))%(M))
#define MODULO_SUB(A,B,M)  (((A)+(M)-(B))%(M))

// ---------------------------------------------------------------------------
// struct that holds the state of the ttx decoder in the master process
//
#define HEADER_CHECK_LEN         17    // max. 32, but minus length of clock & date
#define HEADER_CHECK_LEN_STR    "17"
#define HEADER_CMP_MIN           (HEADER_CHECK_LEN / 2)
#define HEADER_BLANK_MAX         (HEADER_CHECK_LEN * 2/3)
#define HEADER_PAR_ERR_MAX       (HEADER_CHECK_LEN / 4)
#define HEADER_CHECK_MAX_ERR_BITS  2*4
#define HEADER_MAX_CNT           10
#define HEADER_CHAR_MIN_REP      3

static struct
{
   uint         expireMin;
   bool         keepTtxInp;
   uint         startPage;
   uint         stopPage;

   bool         havePgHd;
   sint         refPgNumPos;
   uint         refPgDiffCnt;
   uchar        refPgHdText[HEADER_CHECK_LEN];
   uchar        curPgHdText[HEADER_CHECK_LEN];
   uchar        curPgHdRep[HEADER_CHECK_LEN];
   uint32_t     cniDecInd[CNI_TYPE_COUNT];
   void       * ttx_db;
   bool         postProcDone;
   TTX_GRAB_STATS stats;
} ttxGrabState[MAX_VBI_DVB_STREAMS];

// normalized path to XMLTV database output directory
static char * ttxGrabDbDir;

// ---------------------------------------------------------------------------
// Debug only: remove unprintable characters from TTX header
//
#if DEBUG_SWITCH != OFF
static const uchar * TtxGrab_PrintHeader( const uchar * pHeader, uint headOff, bool doPar )
{
   static uchar buf[41];
   uint idx;
   register uchar c;
   uint len;

   assert(headOff < sizeof(buf) - 1);
   len = sizeof(buf) - 1 - headOff;

   for (idx = 0; (idx < len) && (pHeader[idx] != 0); idx++)
   {
      if (doPar)
         c = parityTab[pHeader[headOff + idx]];
      else
         c = pHeader[idx];

      if (c >= 0x80)
         buf[idx] = '¹';
      else if (c == 0x7F)
         buf[idx] = ' ';
      else if (c >= 32)
         buf[idx] = c;
      else
         buf[idx] = ' ';
   }
   buf[idx] = 0;
   return buf;
}
#endif

// ---------------------------------------------------------------------------
// Search the page number in the page header string
//
static sint TtxGrab_SearchPageNoPos( const uchar * pHeader, uint pageNo )
{
   uchar buf[10];
   uint  idx;
   sint  pos = -1;

   sprintf((char*)buf, "%03X", ((pageNo < 0x100) ? (pageNo + 0x800) : pageNo));

   for (idx = 0; idx < HEADER_CHECK_LEN - 2; idx++)
   {
      if ( (pHeader[idx + 0] == buf[0]) &&
           (pHeader[idx + 1] == buf[1]) &&
           (pHeader[idx + 2] == buf[2]) )
      {
         pos = idx;
         break;
      }
   }
   return pos;
}

// ---------------------------------------------------------------------------
// Check the page-header of the current page
// - the acquisition control should immediately stop insertion of acquired
//   blocks, until a new instance of an AI block has been received and
//   its CNI evaluated
// - the page header comparison fails if more than 3 characters are
//   different; we need some tolerance here, since the page header
//   is only parity protected
// - XXX could make error limit depending on general parity error rate
// - XXX ignore page header changes if VPS does not change
//
static void TtxGrab_PageHeaderCheck( uint bufIdx, const uchar * curPageHeader, uint headOff, uint pageNo )
{
   uint  idx;
   uint  spcCnt, parErrCnt;
   uint  curBitDist, stableDist;
   uint  minRepCount;
   uchar dec;

   if (ttxGrabState[bufIdx].havePgHd)
   {
      curBitDist = stableDist = 0;
      spcCnt = parErrCnt = 0;

      for (idx=0; idx < HEADER_CHECK_LEN; idx++)
      {
         if ( (idx < ttxGrabState[bufIdx].refPgNumPos) ||
              (idx > ttxGrabState[bufIdx].refPgNumPos + 2) )
         {
            dec = parityTab[curPageHeader[headOff + idx]];
            if ((schar)dec >= 0)
            {
               // sum up the number of differing bits in current header
               if (dec != ttxGrabState[bufIdx].refPgHdText[idx])
               {
                  curBitDist += ByteBitDistance(dec, ttxGrabState[bufIdx].refPgHdText[idx]);
               }
               if (dec == ' ')
                  spcCnt += 1;

               // accumulate stable header
               if (dec != ttxGrabState[bufIdx].curPgHdText[idx])
               {
                  ttxGrabState[bufIdx].curPgHdText[idx] = dec;
                  ttxGrabState[bufIdx].curPgHdRep[idx] = 1;
               }
               else if (ttxGrabState[bufIdx].curPgHdRep[idx] < HEADER_CHAR_MIN_REP)
               {
                  ttxGrabState[bufIdx].curPgHdRep[idx] += 1;
               }
            }
            else if (ttxGrabState[bufIdx].refPgHdText[idx] != 0xff)
            {
               // count number of parity errors in current errors as indicator of reliability
               // but 
               parErrCnt += 1;
            }

            if ( (ttxGrabState[bufIdx].curPgHdRep[idx] >= HEADER_CHAR_MIN_REP) &&
                 (ttxGrabState[bufIdx].curPgHdText[idx] != ttxGrabState[bufIdx].refPgHdText[idx]) )
            {
               stableDist += 1;
            }
         }
      }

      // in a completely random signal there's 50% parity errors, so require significantly less
      // also ignore blank page headers (appear sometimes on empty/unused pages)
      if (stableDist != 0)
      {
         debug2("TtxGrab-PageHeaderCheck: stable header diff. %d chars from \"%." HEADER_CHECK_LEN_STR "s\"", stableDist, TtxGrab_PrintHeader(ttxGrabState[bufIdx].refPgHdText, 0, FALSE));
         debug3("TtxGrab-PageHeaderCheck: page:%03X pg no pos:%d, cur header:  \"%." HEADER_CHECK_LEN_STR "s\"", pageNo, ttxGrabState[bufIdx].refPgNumPos, TtxGrab_PrintHeader(curPageHeader, headOff, TRUE));
         ttxGrabState[bufIdx].refPgDiffCnt = HEADER_MAX_CNT;
         ttxGrabState[bufIdx].havePgHd = FALSE;
      }
      else if ( (parErrCnt <= HEADER_PAR_ERR_MAX) &&
                (spcCnt + parErrCnt <= HEADER_BLANK_MAX) &&
                (curBitDist >= HEADER_CHECK_MAX_ERR_BITS) )
      {
         debug2("TtxGrab-PageHeaderCheck: %d bits differ, header \"%." HEADER_CHECK_LEN_STR "s\"", curBitDist, TtxGrab_PrintHeader(ttxGrabState[bufIdx].refPgHdText, 0, FALSE));
         ttxGrabState[bufIdx].refPgDiffCnt += 1;
         if (ttxGrabState[bufIdx].refPgDiffCnt >= HEADER_MAX_CNT)
            ttxGrabState[bufIdx].havePgHd = FALSE;
      }
      else
         ttxGrabState[bufIdx].refPgDiffCnt = 0;
   }
   else
   {  // first header after channel change -> copy header
      uint errCnt = 0;
      uint diffCnt = 0;
      minRepCount = 0;
      for (idx=0; idx < HEADER_CHECK_LEN; idx++)
      {
         dec = parityTab[ curPageHeader[headOff + idx] ];
         if ((schar)dec >= 0)
         {
            if ( (ttxGrabState[bufIdx].curPgHdRep[idx] == 0) ||
                 (dec != ttxGrabState[bufIdx].refPgHdText[idx]) )
            {
               ttxGrabState[bufIdx].refPgHdText[idx] = dec;
               ttxGrabState[bufIdx].curPgHdRep[idx] = 1;
               diffCnt += 1;
            }
            else if (ttxGrabState[bufIdx].curPgHdRep[idx] < HEADER_CHAR_MIN_REP)
            {
               ttxGrabState[bufIdx].curPgHdRep[idx] += 1;
            }
         }
         else
            errCnt += 1;

         if (ttxGrabState[bufIdx].curPgHdRep[idx] >= HEADER_CHAR_MIN_REP)
            minRepCount += 1;
      }

      // only OK if no parity decoding error
      if (minRepCount >= HEADER_CMP_MIN)
      {
         // note: page number search includes characters with insufficient repeat count
         // this cannot be avoided since page number digits change on every page, i.e. are not repeated
         ttxGrabState[bufIdx].refPgNumPos = TtxGrab_SearchPageNoPos(ttxGrabState[bufIdx].refPgHdText, pageNo);
         if (ttxGrabState[bufIdx].refPgNumPos >= 0)
         {
            dprintf3("TtxGrab-PageHeaderCheck: store header \"%." HEADER_CHECK_LEN_STR "s\" page %03X pos:%d\n", TtxGrab_PrintHeader(ttxGrabState[bufIdx].refPgHdText, 0, FALSE), pageNo, ttxGrabState[bufIdx].refPgNumPos);
            memset(ttxGrabState[bufIdx].curPgHdRep, 1, sizeof(ttxGrabState[bufIdx].curPgHdRep));
            memcpy(ttxGrabState[bufIdx].curPgHdText, ttxGrabState[bufIdx].refPgHdText, sizeof(ttxGrabState[bufIdx].curPgHdText));
            ttxGrabState[bufIdx].refPgDiffCnt = 0;
            ttxGrabState[bufIdx].havePgHd = TRUE;
         }
         else
            dprintf2("TtxGrab-PageHeaderCheck: page no %03X not found in header \"%." HEADER_CHECK_LEN_STR "s\"\n", pageNo, TtxGrab_PrintHeader(ttxGrabState[bufIdx].refPgHdText, 0, FALSE));
      }
      else
         dprintf5("TtxGrab-PageHeaderCheck[%d]: minRep:%d err:%d diff:%d - header \"%." HEADER_CHECK_LEN_STR "s\"\n",
                  bufIdx, minRepCount, errCnt, diffCnt, TtxGrab_PrintHeader(ttxGrabState[bufIdx].refPgHdText, 0, FALSE));
   }
}

// ---------------------------------------------------------------------------
// Add CNI to grabber output
// - CNIs are essential to generate channel IDs for the XMLTV output
//
static void TtxGrab_ProcessCni( uint bufIdx )
{
   CNI_TYPE cniType;
   uint   newCni;

   // retrieve new CNI from teletext decoder, if any
   if ( TtxDecode_GetCniAndPil(bufIdx, &newCni, NULL, &cniType, ttxGrabState[bufIdx].cniDecInd, NULL, NULL) )
   {
      ttx_db_add_cni(ttxGrabState[bufIdx].ttx_db, newCni);
   }
}

// ---------------------------------------------------------------------------
// Retrieve all available packets from VBI buffer and forward them
// - in this function incoming data is passed from the slave to the master
//   process/thread by means of shared memory
// - returns FALSE when an uncontrolled channel change was detected; the caller
//   then should reset the acquisition
//
bool TtxGrab_ProcessPackets( uint bufIdx )
{
   const VBI_LINE * pVbl;
   uint pkgOff;
   //bool seenHeader;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->buf[bufIdx].chanChangeReq == pVbiBuf->buf[bufIdx].chanChangeCnf) )
   {
      //assert(pVbiBuf->startPageNo[bufIdx] == ttxGrabState[bufIdx].startPage);
      dprintf3("TtxGrab-ProcessPackets[%d] read=%d write=%d\n", bufIdx, pVbiBuf->buf[bufIdx].reader_idx, pVbiBuf->buf[bufIdx].writer_idx);

      // fetch the oldest packet from the teletext ring buffer
      //seenHeader = FALSE;
      pkgOff = 0;
      while ( (pVbl = TtxDecode_GetPacket(bufIdx, pkgOff)) != NULL )
      {
         dprintf6("Process stream[%d] idx=%d+%d: pg=%03X.%04X pkg=%d\n", bufIdx, pVbiBuf->buf[bufIdx].reader_idx, pkgOff, pVbl->pageno, pVbl->ctrl_lo & 0x3F7F, pVbl->pkgno);
         ttxGrabState[bufIdx].stats.ttxPkgCount += 1;
         pkgOff += 1;

         if (pVbl->pkgno == 0)
         {
            //dprintf3("%03X.%04X %s\n", pVbl->pageno, pVbl->ctrl_lo & 0x3F7F, TtxGrab_PrintHeader(pVbl->data, 0, TRUE));
            //seenHeader = TRUE;
            ttxGrabState[bufIdx].stats.ttxPagCount += 1;
            if (((pVbl->pageno & 0x0F) <= 9) && (((pVbl->pageno >> 4) & 0x0F) <= 9))
               TtxGrab_PageHeaderCheck(bufIdx, pVbl->data, 8, pVbl->pageno);
         }

         ttxGrabState[bufIdx].stats.ttxPkgStrSum += 40;
         ttxGrabState[bufIdx].stats.ttxPkgParErr +=
            UnHamParityArray(pVbl->data, (uchar*)pVbl->data, 40);
         // XXX FIXME: using void cast to remove const for target buffer

         // skip file output if disabled (note all other processing is intentionally done anyways)
         if (ttxGrabState[bufIdx].refPgDiffCnt == 0)
         {
            ttx_db_add_pkg( ttxGrabState[bufIdx].ttx_db,
                            pVbl->pageno,
                            pVbl->ctrl_lo | (pVbl->ctrl_hi << 16),
                            pVbl->pkgno,
                            pVbl->data,
                            time(NULL));
         }
      }

      // make sure to check at least one page header for channel changes
      // (i.e. even if no page in the requested range was found in this interval)
      {
         uchar buf[40];
         uint pgNum;
         pkgOff = 0;
         while (TtxDecode_GetPageHeader(bufIdx, buf, &pgNum, pkgOff))
         {
            // FIXME  +8 overflows buf
            if (((pgNum & 0x0F) <= 9) && (((pgNum >> 4) & 0x0F) <= 9))
               TtxGrab_PageHeaderCheck(bufIdx, buf, 8, pgNum);
            if (ttxGrabState[bufIdx].havePgHd && (ttxGrabState[bufIdx].refPgDiffCnt == 0))
               break;
            //else dprintf2("##### %03X %s\n", pgNum, TtxGrab_PrintHeader(buf, 0, TRUE));
            pkgOff += 1;
         }
      }

      // add CNIs to output buffer, if available
      TtxGrab_ProcessCni(bufIdx);
   }
   return (ttxGrabState[bufIdx].refPgDiffCnt < HEADER_MAX_CNT);
}

// ---------------------------------------------------------------------------
// Check number of lost packets and block decoding errors
// - returns FALSE if type should be changed
//
bool TtxGrab_CheckSlicerQuality( uint bufIdx )
{
   bool result = TRUE;

   if (ttxGrabState[bufIdx].stats.ttxPkgCount != 0)
   {
      if ((double)ttxGrabState[bufIdx].stats.ttxPkgParErr / ttxGrabState[bufIdx].stats.ttxPkgStrSum >= 0.005)
      {
         debug2("TtxGrab-CheckSlicerQuality: blanked %d of %d chars", ttxGrabState[bufIdx].stats.ttxPkgParErr, ttxGrabState[bufIdx].stats.ttxPkgStrSum);
         // reset stream statistics to give the new slicer a "clean slate"
         memset(&ttxGrabState[bufIdx].stats, 0, sizeof(ttxGrabState[bufIdx].stats));
         result = FALSE;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Determine source status
// - inRange: TRUE if requested page range is next-up in the page sequence
// - rangeDone: all pages of the requested range captured
// - sourceLock: page header stored (i.e. channel change detection enabled)
//
void TtxGrab_GetPageStats( uint bufIdx, bool * pInRange, bool * pRangeDone, bool * pSourceLock, uint * pPredictDelay )
{
   uint magBuf[8];
   uint nearBuf[3];
   uint idx;
   uint sum;
   uint mag;
   sint dirSum;
   bool revDirection;
   bool inRange = FALSE;
   bool rangeDone = FALSE;

   if (pSourceLock != NULL)
      *pSourceLock = FALSE;
   if (pPredictDelay != NULL)
      *pPredictDelay = 0;

   if (TtxDecode_GetMagStats(bufIdx, magBuf, &dirSum, FALSE))
   {
      sum = 0;
      for (idx=0; idx<8; idx++)
         sum += magBuf[idx];
      if (sum >= 20)
      {
         mag = (ttxGrabState[bufIdx].startPage >> 8) & 0x07;

         revDirection = ((0 - dirSum) >= (int)sum/4);
         if (revDirection)
         {
            nearBuf[0] = magBuf[mag];
            nearBuf[1] = magBuf[MODULO_ADD(mag,1,8)];
            nearBuf[2] = magBuf[MODULO_ADD(mag,2,8)];
         }
         else
         {
            nearBuf[0] = magBuf[mag];
            nearBuf[1] = magBuf[MODULO_SUB(mag,1,8)];
            nearBuf[2] = magBuf[MODULO_SUB(mag,2,8)];
         }

         if (nearBuf[2] >= sum * 80/100)
            inRange = TRUE;
         else if ((nearBuf[1] + nearBuf[2]) >= sum * 80/100)
            inRange = (nearBuf[0] <= 2);
         else
            inRange = FALSE;

         rangeDone = (magBuf[mag] <= 2) &&
                     ( (ttxGrabState[bufIdx].stats.ttxPagCount > 30) ||
                       (ttxGrabState[bufIdx].stats.ttxPagCount >= (ttxGrabState[bufIdx].stopPage - ttxGrabState[bufIdx].startPage + 1)));

         if (pSourceLock != NULL)
            *pSourceLock = ttxGrabState[bufIdx].havePgHd;

         if (!rangeDone && (pPredictDelay != NULL))
         {
            for (idx=0; idx<8; idx++)
            {
               if (magBuf[idx] >= sum * 75/100)
               {
                  if (idx == mag)  // one cycle
                     *pPredictDelay = 30;
                  else if (revDirection)
                     *pPredictDelay = ((idx + 8 - mag - 1) % 8) * 3;
                  else
                     *pPredictDelay = ((mag + 8 - idx - 1) % 8) * 3;
               }
            }
         }
#ifndef DPRINTF_OFF
         for (idx=0; idx<8; idx++)
            dprintf1("%2d%% ", magBuf[idx] * 100 / sum);
         dprintf5(" - sum:%d dir:%d rev:%d wait:%d done:%d\n",
                  sum, dirSum, revDirection, inRange, rangeDone);
#endif
      }
   }

   if (pInRange != NULL)
      *pInRange = inRange;
   if (pRangeDone != NULL)
      *pRangeDone = rangeDone;
}

// ---------------------------------------------------------------------------
// Fill struct with statistics data
//
void TtxGrab_GetStatistics( uint bufIdx, TTX_GRAB_STATS * pStats )
{
   if (pStats != NULL)
   {
      ttxGrabState[bufIdx].stats.ttxPageStartNo = ttxGrabState[bufIdx].startPage;
      ttxGrabState[bufIdx].stats.ttxPageStopNo  = ttxGrabState[bufIdx].stopPage;

      *pStats = ttxGrabState[bufIdx].stats;
   }
}

// ---------------------------------------------------------------------------
// Helper function which checks if an XML file exists and is non-zero-len
//
static bool TtxGrab_CheckFileExists( const char * pPath )
{
   bool result = FALSE;
#ifndef WIN32
   struct stat st;

   if (pPath != NULL)
   {
      result = (stat(pPath, &st) == 0) && S_ISREG(st.st_mode) && (st.st_size != 0);
   }
#else
   struct _stat st;

   if (pPath != NULL)
   {
      result = (_stat(pPath, &st) == 0) && S_ISREG(st.st_mode) && (st.st_size != 0);
   }
#endif
   return result;
}

// ---------------------------------------------------------------------------
// Check if post-processing is done: poll child process status
//
bool TtxGrab_CheckPostProcess( void )
{
   uint bufIdx;
   bool result = FALSE;

   for (bufIdx = 0; bufIdx < MAX_VBI_DVB_STREAMS; ++bufIdx)
   {
      if (ttxGrabState[bufIdx].postProcDone)
      {
         dprintf1("TtxGrab-CheckPostProcess: bufIdx:%d DONE\n", bufIdx);
         ttxGrabState[bufIdx].postProcDone = FALSE;
         result = TRUE;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Helper functions for copying a channel name with special chars replaced
//
static char * TtxGrab_CopyNameEscaped( char * pDst, const char * pSrc )
{
   while (*pSrc != 0)
   {
      if ( ((*pSrc >= 'A') && (*pSrc <= 'Z')) ||
           ((*pSrc >= 'a') && (*pSrc <= 'z')) ||
           ((*pSrc >= '0') && (*pSrc <= '9')) )
      {
         // copy character
         *(pDst++) = *(pSrc++);
      }
      else
      {  // replace illegal character
         *(pDst++) = '_';
         pSrc += 1;
      }
   }
   *pDst = 0;

   return pDst;
}

// ---------------------------------------------------------------------------
// Returns the XMLTV output file path for the given channel
// - note the returned path is normalized
// - caller needs to free the allocated memory
//
static char * TtxGrab_GetPathExt( uint serviceId, const char * pName, const char * pExt )
{
   char * pXmlOutFile;
   char * ps;
   int off;

   if (serviceId != 0)
   {
      pXmlOutFile = (char*) xmalloc(strlen(ttxGrabDbDir) + 1 +
				     + strlen(XML_OUT_FILE_PREFIX)
				     + 20 + strlen(pExt) + 1);

      sprintf(pXmlOutFile, "%s/" XML_OUT_FILE_PREFIX "%u%s", ttxGrabDbDir, serviceId, pExt);
   }
   else
   {
      pXmlOutFile = (char*) xmalloc(strlen(ttxGrabDbDir) + 1 +
				     + strlen(XML_OUT_FILE_PREFIX)
				     + strlen(pName)
				     + strlen(pExt) + 1);
      off = sprintf(pXmlOutFile, "%s/" XML_OUT_FILE_PREFIX, ttxGrabDbDir);
      ps = TtxGrab_CopyNameEscaped(pXmlOutFile + off, pName);
      strcpy(ps, pExt);
   }
   return pXmlOutFile;
}

// ---------------------------------------------------------------------------
// Returns the XMLTV output file path for the given channel name
//
char * TtxGrab_GetPath( uint serviceId, const char * pName )
{
   return TtxGrab_GetPathExt(serviceId, pName, XML_OUT_FILE_EXT);
}

// ---------------------------------------------------------------------------
// Stop acquisition and start post-processing
//
void TtxGrab_PostProcess( uint bufIdx, uint serviceId, const char * pName, bool reset )
{
   char * pXmlMergeFile;
   char * pXmlOutFile;
   char * pXmlTmpFile;
   char * pXmlChnId;
   char xmltvChnId[30];

   if (ttxGrabState[bufIdx].keepTtxInp)
   {
      char * pKeptInpFile = TtxGrab_GetPathExt(serviceId, pName, TTX_CAP_FILE_EXT);
      dprintf1("TtxGrab-PostProcess: dump complete database to '%s'\n", pKeptInpFile);

      ttx_db_dump(ttxGrabState[bufIdx].ttx_db, pKeptInpFile, 0x100, 0x8FF);
      xfree(pKeptInpFile);
   }

   // build target file name
   pXmlOutFile = TtxGrab_GetPathExt(serviceId, pName, XML_OUT_FILE_EXT);

   // build temporary file name: append ".tmp" suffix
   pXmlTmpFile = (char*) xmalloc(strlen(pXmlOutFile) + strlen(XML_TMP_FILE_EXT) + 1);
   sprintf(pXmlTmpFile, "%s" XML_TMP_FILE_EXT, pXmlOutFile);

   // build XMLTV channel ID based on service ID, or channel name
   if (serviceId != 0)
   {
      snprintf(xmltvChnId, sizeof(xmltvChnId) - 1, "SID_%d", serviceId);
      xmltvChnId[sizeof(xmltvChnId) - 1] = 0;
      pXmlChnId = xmltvChnId;
   }
   else
   {  // analog source: use channel name as ID, just with blank et.al. replaced by "_"
      pXmlChnId = (char*) xmalloc(strlen(pName) + 1);
      TtxGrab_CopyNameEscaped(pXmlChnId, pName);
   }

   // merge with pre-existing output file, if existing
   pXmlMergeFile = TtxGrab_CheckFileExists(pXmlOutFile) ? pXmlOutFile : NULL;

   dprintf4("TtxGrab-PostProcess: name:'%s' expire:%d merge?:%d out:'%s'\n",
            pName, ttxGrabState[bufIdx].expireMin, (pXmlMergeFile != NULL), pXmlTmpFile);

   if (ttx_db_parse(ttxGrabState[bufIdx].ttx_db,
                    ttxGrabState[bufIdx].startPage, ttxGrabState[bufIdx].stopPage,
                    ttxGrabState[bufIdx].expireMin,
                    pXmlMergeFile, pXmlTmpFile,
                    pName, xmltvChnId) == 0)
   {
      if (rename(pXmlTmpFile, pXmlOutFile) != 0)
      {
         debug3("TtxGrab-CheckPostProcess: failed to rename '%s' into '%s': %d", pXmlTmpFile, pXmlOutFile, errno);
         unlink(pXmlTmpFile);
      }
      ttxGrabState[bufIdx].postProcDone = TRUE;
   }
   xfree(pXmlTmpFile);
   xfree(pXmlOutFile);
   if (pXmlChnId != xmltvChnId)
      xfree(pXmlChnId);

   if (reset)
   {
      ttx_db_destroy(ttxGrabState[bufIdx].ttx_db);
      ttxGrabState[bufIdx].ttx_db = NULL;
   }
}

// ---------------------------------------------------------------------------
// Start or reset acquisition
// - start capturing packets from the requested page range
// - store packets into a temporary file, unless output is disabled
//   (output-less mode supports channel detection, e.g. in passive mode)
// - if acq. is already enabled, the previous output file is discarded;
//   note in this case a possibly running post-processing is not aborted
//
bool TtxGrab_Start( uint startPage, uint stopPage, bool enableOutput )
{
   bool result = TRUE;

   dprintf3("TtxGrab-Start: capture %03X-%03X enable:%d\n", startPage, stopPage, enableOutput);

   for (uint bufIdx = 0; bufIdx < MAX_VBI_DVB_STREAMS; ++bufIdx)
   {
      ttxGrabState[bufIdx].startPage = startPage;
      ttxGrabState[bufIdx].stopPage = stopPage;

      memset(&ttxGrabState[bufIdx].stats, 0, sizeof(ttxGrabState[bufIdx].stats));

      memset(&ttxGrabState[bufIdx].curPgHdRep, 0, sizeof(ttxGrabState[bufIdx].curPgHdRep));
      memset(&ttxGrabState[bufIdx].refPgHdText, 0xff, sizeof(ttxGrabState[bufIdx].refPgHdText));
      ttxGrabState[bufIdx].havePgHd = FALSE;
      memset(ttxGrabState[bufIdx].cniDecInd, 0, sizeof(ttxGrabState[bufIdx].cniDecInd));

      if (ttxGrabState[bufIdx].ttx_db == NULL)
      {
         ttxGrabState[bufIdx].ttx_db = ttx_db_create();
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition
// - called when acq is disabled (i.e. not upon channel switch)
//
void TtxGrab_Stop( void )
{
   for (uint bufIdx = 0; bufIdx < MAX_VBI_DVB_STREAMS; ++bufIdx)
   {
      ttx_db_destroy(ttxGrabState[bufIdx].ttx_db);
      ttxGrabState[bufIdx].ttx_db = NULL;
   }
}

// ---------------------------------------------------------------------------
// Configuration parameters
// - called during start-up and configuration changes
// - string is kept by reference: caller needs to keep the string content valid
//
void TtxGrab_SetConfig( const char * pDbDir, uint expireMin, bool keepTtxInp )
{
   for (uint bufIdx = 0; bufIdx < MAX_VBI_DVB_STREAMS; ++bufIdx)
   {
      // passed through to TTX grabber (discard programmes older than X hours)
      ttxGrabState[bufIdx].expireMin = expireMin;
      ttxGrabState[bufIdx].keepTtxInp = keepTtxInp;
   }

   if (ttxGrabDbDir != NULL)
      xfree(ttxGrabDbDir);
#ifndef WIN32
   char * pRealPath = realpath(pDbDir, NULL);
   if (pRealPath != NULL)
   {
      ttxGrabDbDir = xstrdup(pRealPath);
      free(pRealPath); // not xfree(): memory allocated by glibc
   }
   else
#endif
   {
      ttxGrabDbDir = xstrdup(pDbDir);
      // remove trailing slash
      size_t len = strlen(ttxGrabDbDir);
      char * p = ttxGrabDbDir + len - 1;
      while ((len-- > 1) && (*p == '/'))
         *(p--) = 0;
   }
}

// ---------------------------------------------------------------------------
// Free resources
//
void TtxGrab_Exit( void )
{
   TtxGrab_Stop();

   if (ttxGrabDbDir != NULL)
      xfree(ttxGrabDbDir);
}

// ---------------------------------------------------------------------------
// Initialize the internal decoder state
//
void TtxGrab_Init( void )
{
   memset(&ttxGrabState, 0, sizeof(ttxGrabState));
}

#endif  // USE_TTX_GRABBER
