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
 *  $Id: ttxgrab.c,v 1.15 2011/01/09 16:53:29 tom Exp tom $
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

#define XML_OUT_FILE_PAT   "ttx-%s.xml.tmp"
#define TTX_CAP_FILE_PAT   "ttx-%s.dat"
#define TTX_CAP_FILE_TMP   "ttx-%s.dat.tmp"

#ifdef __MINGW32__
typedef long ssize_t;
#endif
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
   bool         postProcDone;
   TTX_GRAB_STATS stats;
} ttxGrabState;

// ---------------------------------------------------------------------------
// Debug only: remove unprintable characters from TTX header
//
#if DEBUG_SWITCH != OFF
static const uchar * TtxGrab_PrintHeader( const uchar * pHeader, bool doPar )
{
   static uchar buf[41];
   uint idx;
   register uchar c;

   for (idx = 0; (idx < sizeof(buf) - 1) && (pHeader[idx] != 0); idx++)
   {
      if (doPar)
         c = parityTab[pHeader[idx]];
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

   sprintf(buf, "%03X", ((pageNo < 0x100) ? (pageNo + 0x800) : pageNo));

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
//   different; we need some tolerance here, sonce the page header
//   is only parity protected
// - XXX could make error limit depending on general parity error rate
// - XXX ignore page header changes if VPS does not change
//
static void TtxGrab_PageHeaderCheck( const uchar * curPageHeader, uint pageNo )
{
   uint  idx;
   uint  spcCnt, parErrCnt;
   uint  curBitDist, stableDist;
   uint  minRepCount;
   uchar dec;

   if (ttxGrabState.havePgHd)
   {
      curBitDist = stableDist = 0;
      spcCnt = parErrCnt = 0;

      for (idx=0; idx < HEADER_CHECK_LEN; idx++)
      {
         if ( (idx < ttxGrabState.refPgNumPos) ||
              (idx > ttxGrabState.refPgNumPos + 2) )
         {
            dec = parityTab[curPageHeader[idx]];
            if ((schar)dec >= 0)
            {
               // sum up the number of differing bits in current header
               if (dec != ttxGrabState.refPgHdText[idx])
               {
                  curBitDist += ByteBitDistance(dec, ttxGrabState.refPgHdText[idx]);
               }
               if (dec == ' ')
                  spcCnt += 1;

               // accumulate stable header
               if (dec != ttxGrabState.curPgHdText[idx])
               {
                  ttxGrabState.curPgHdText[idx] = dec;
                  ttxGrabState.curPgHdRep[idx] = 1;
               }
               else if (ttxGrabState.curPgHdRep[idx] < HEADER_CHAR_MIN_REP)
               {
                  ttxGrabState.curPgHdRep[idx] += 1;
               }
            }
            else if (ttxGrabState.refPgHdText[idx] != 0xff)
            {
               // count number of parity errors in current errors as indicator of reliability
               // but 
               parErrCnt += 1;
            }

            if ( (ttxGrabState.curPgHdRep[idx] >= HEADER_CHAR_MIN_REP) &&
                 (ttxGrabState.curPgHdText[idx] != ttxGrabState.refPgHdText[idx]) )
            {
               stableDist += 1;
            }
         }
      }

      // in a completely random signal there's 50% parity errors, so require significantly less
      // also ignore blank page headers (appear sometimes on empty/unused pages)
      if (stableDist != 0)
      {
         debug2("TtxGrab-PageHeaderCheck: stable header diff. %d chars from \"%." HEADER_CHECK_LEN_STR "s\"", stableDist, TtxGrab_PrintHeader(ttxGrabState.refPgHdText, FALSE));
         debug3("TtxGrab-PageHeaderCheck: page:%03X pg no pos:%d, cur header:  \"%." HEADER_CHECK_LEN_STR "s\"", pageNo, ttxGrabState.refPgNumPos, TtxGrab_PrintHeader(curPageHeader, TRUE));
         ttxGrabState.refPgDiffCnt = HEADER_MAX_CNT;
         ttxGrabState.havePgHd = FALSE;
      }
      else if ( (parErrCnt <= HEADER_PAR_ERR_MAX) &&
                (spcCnt + parErrCnt <= HEADER_BLANK_MAX) &&
                (curBitDist >= HEADER_CHECK_MAX_ERR_BITS) )
      {
         debug2("TtxGrab-PageHeaderCheck: %d bits differ, header \"%." HEADER_CHECK_LEN_STR "s\"", curBitDist, TtxGrab_PrintHeader(ttxGrabState.refPgHdText, FALSE));
         ttxGrabState.refPgDiffCnt += 1;
         if (ttxGrabState.refPgDiffCnt >= HEADER_MAX_CNT)
            ttxGrabState.havePgHd = FALSE;
      }
      else
         ttxGrabState.refPgDiffCnt = 0;
   }
   else
   {  // first header after channel change -> copy header
      uint errCnt = 0;
      uint diffCnt = 0;
      minRepCount = 0;
      for (idx=0; idx < HEADER_CHECK_LEN; idx++)
      {
         dec = parityTab[ curPageHeader[idx] ];
         if ((schar)dec >= 0)
         {
            if ( (ttxGrabState.curPgHdRep[idx] == 0) ||
                 (dec != ttxGrabState.refPgHdText[idx]) )
            {
               ttxGrabState.refPgHdText[idx] = dec;
               ttxGrabState.curPgHdRep[idx] = 1;
               diffCnt += 1;
            }
            else if (ttxGrabState.curPgHdRep[idx] < HEADER_CHAR_MIN_REP)
            {
               ttxGrabState.curPgHdRep[idx] += 1;
            }
         }
         else
            errCnt += 1;

         if (ttxGrabState.curPgHdRep[idx] >= HEADER_CHAR_MIN_REP)
            minRepCount += 1;
      }

      // only OK if no parity decoding error
      if (minRepCount >= HEADER_CMP_MIN)
      {
         // note: page number search includes characters with insufficient repeat count
         // this cannot be avoided since page number digits change on every page, i.e. are not repeated
         ttxGrabState.refPgNumPos = TtxGrab_SearchPageNoPos(ttxGrabState.refPgHdText, pageNo);
         if (ttxGrabState.refPgNumPos >= 0)
         {
            dprintf3("TtxGrab-PageHeaderCheck: store header \"%." HEADER_CHECK_LEN_STR "s\" page %03X pos:%d\n", TtxGrab_PrintHeader(ttxGrabState.refPgHdText, FALSE), pageNo, ttxGrabState.refPgNumPos);
            memset(ttxGrabState.curPgHdRep, 1, sizeof(ttxGrabState.curPgHdRep));
            memcpy(ttxGrabState.curPgHdText, ttxGrabState.refPgHdText, sizeof(ttxGrabState.curPgHdText));
            ttxGrabState.refPgDiffCnt = 0;
            ttxGrabState.havePgHd = TRUE;
         }
         else
            dprintf2("TtxGrab-PageHeaderCheck: page no %03X not found in header \"%." HEADER_CHECK_LEN_STR "s\"\n", pageNo, TtxGrab_PrintHeader(ttxGrabState.refPgHdText, FALSE));
      }
      else
         dprintf4("TtxGrab-PageHeaderCheck: minRep:%d err:%d diff:%d - header \"%." HEADER_CHECK_LEN_STR "s\"\n",
                  minRepCount, errCnt, diffCnt, TtxGrab_PrintHeader(ttxGrabState.refPgHdText, FALSE));
   }
}

// ---------------------------------------------------------------------------
// Add CNI to grabber output
// - CNIs are essential to generate channel IDs for the XMLTV output
//
static void TtxGrab_ProcessCni( void )
{
   CNI_TYPE cniType;
   uint   newCni;

   // retrieve new CNI from teletext decoder, if any
   if ( TtxDecode_GetCniAndPil(&newCni, NULL, &cniType, ttxGrabState.cniDecInd, NULL, NULL) )
   {
      ttx_db_add_cni(newCni);
   }
}

// ---------------------------------------------------------------------------
// Retrieve all available packets from VBI buffer and forward them
// - in this function incoming data is passed from the slave to the master
//   process/thread by means of shared memory
// - returns FALSE when an uncontrolled channel change was detected; the caller
//   then should reset the acquisition
//
bool TtxGrab_ProcessPackets( void )
{
   const VBI_LINE * pVbl;
   uint pkgOff;
   bool seenHeader;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) )
   {
      //assert(pVbiBuf->startPageNo == ttxGrabState.startPage);

      // fetch the oldest packet from the teletext ring buffer
      seenHeader = FALSE;
      pkgOff = 0;
      while ( (pVbl = TtxDecode_GetPacket(pkgOff)) != NULL )
      {
         ttxGrabState.stats.ttxPkgCount += 1;
         pkgOff += 1;

         //dprintf4("Process idx=%d: pg=%03X.%04X pkg=%d\n", pVbiBuf->reader_idx, pVbl->pageno, pVbl->ctrl_lo & 0x3F7F, pVbl->pkgno);
         if (pVbl->pkgno == 0)
         {
            //dprintf3("%03X.%04X %s\n", pVbl->pageno, pVbl->ctrl_lo & 0x3F7F, TtxGrab_PrintHeader(pVbl->data, TRUE));
            seenHeader = TRUE;
            ttxGrabState.stats.ttxPagCount += 1;
            if (((pVbl->pageno & 0x0F) <= 9) && (((pVbl->pageno >> 4) & 0x0F) <= 9))
               TtxGrab_PageHeaderCheck(pVbl->data + 8, pVbl->pageno);
         }

         ttxGrabState.stats.ttxPkgStrSum += 40;
         ttxGrabState.stats.ttxPkgParErr +=
            UnHamParityArray(pVbl->data, (void*)pVbl->data, 40);
         // XXX FIXME: using void cast to remove const for target buffer

         // skip file output if disabled (note all other processing is intentionally done anyways)
         if (ttxGrabState.refPgDiffCnt == 0)
         {
            ttx_db_add_pkg( pVbl->pageno,
                            pVbl->ctrl_lo | (pVbl->ctrl_hi << 16),
                            pVbl->pkgno,
                            pVbl->data,
                            time(NULL));
         }
      }

      // make sure to check at least one page header for channel changes
      // (i.e. even if no page in the requested range was found in this interval)
      {
         char buf[40];
         uint pgNum;
         pkgOff = 0;
         while (TtxDecode_GetPageHeader(buf, &pgNum, pkgOff))
         {
            if (((pgNum & 0x0F) <= 9) && (((pgNum >> 4) & 0x0F) <= 9))
               TtxGrab_PageHeaderCheck(buf + 8, pgNum);
            if (ttxGrabState.havePgHd && (ttxGrabState.refPgDiffCnt == 0))
               break;
            //else dprintf2("##### %03X %s\n", pgNum, TtxGrab_PrintHeader(buf, TRUE));
            pkgOff += 1;
         }
      }

      // add CNIs to output buffer, if available
      TtxGrab_ProcessCni();
   }
   return (ttxGrabState.refPgDiffCnt < HEADER_MAX_CNT);
}

// ---------------------------------------------------------------------------
// Check number of lost packets and block decoding errors
// - returns FALSE if type should be changed
//
bool TtxGrab_CheckSlicerQuality( void )
{
   bool result = TRUE;

   if (ttxGrabState.stats.ttxPkgCount != 0)
   {
      if ((double)ttxGrabState.stats.ttxPkgParErr / ttxGrabState.stats.ttxPkgStrSum >= 0.005)
      {
         debug2("TtxGrab-CheckSlicerQuality: blanked %d of %d chars", ttxGrabState.stats.ttxPkgParErr, ttxGrabState.stats.ttxPkgStrSum);
         // reset stream statistics to give the new slicer a "clean slate"
         memset(&ttxGrabState.stats, 0, sizeof(ttxGrabState.stats));
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
void TtxGrab_GetPageStats( bool * pInRange, bool * pRangeDone, bool * pSourceLock, uint * pPredictDelay )
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

   if (TtxDecode_GetMagStats(magBuf, &dirSum, FALSE))
   {
      sum = 0;
      for (idx=0; idx<8; idx++)
         sum += magBuf[idx];
      if (sum >= 20)
      {
         mag = (ttxGrabState.startPage >> 8) & 0x07;

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
                     ( (ttxGrabState.stats.ttxPagCount > 30) ||
                       (ttxGrabState.stats.ttxPagCount >= (ttxGrabState.stopPage - ttxGrabState.startPage + 1)));

         if (pSourceLock != NULL)
            *pSourceLock = ttxGrabState.havePgHd;

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
void TtxGrab_GetStatistics( TTX_GRAB_STATS * pStats )
{
   if (pStats != NULL)
   {
      ttxGrabState.stats.ttxPageStartNo = ttxGrabState.startPage;
      ttxGrabState.stats.ttxPageStopNo  = ttxGrabState.stopPage;

      *pStats = ttxGrabState.stats;
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
// - optionally returns a pointer to the name of the generated XML file
//   note: the caller must free the memory for the name!
//
bool TtxGrab_CheckPostProcess( char ** ppXmlName )
{
   bool result = FALSE;
   if (ttxGrabState.postProcDone)
   {
      dprintf0("TtxGrab-CheckPostProcess: DONE\n");
      ttxGrabState.postProcDone = FALSE;
      result = TRUE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition and start post-processing
//
void TtxGrab_PostProcess( const char * pName, bool reset )
{
   char * pXmlTmpFile;
   char * pXmlMergeFile;
   char * pXmlOutFile;

   if (ttxGrabState.keepTtxInp)
   {
      char * pKeptInpFile = xmalloc(strlen(pName) + 20);
      sprintf(pKeptInpFile, TTX_CAP_FILE_PAT, pName);
      ttx_db_dump(pKeptInpFile, 100, 899);
      xfree(pKeptInpFile);
   }

   // build output file name (note: includes ".tmp" suffix which is removed later)
   pXmlTmpFile = xmalloc(strlen(pName) + 20);
   sprintf(pXmlTmpFile, XML_OUT_FILE_PAT, pName);

   pXmlMergeFile = xstrdup(pXmlTmpFile);
   pXmlMergeFile[strlen(pXmlMergeFile) - 4] = 0;
   if (TtxGrab_CheckFileExists(pXmlMergeFile) == FALSE)
   {
      xfree(pXmlMergeFile);
      pXmlMergeFile = NULL;
   }
   dprintf4("TtxGrab-PostProcess: name:'%s' expire:%d merge:'%s' out:'%s'\n",
            pName, ttxGrabState.expireMin, (pXmlMergeFile != NULL) ? pXmlMergeFile : "", pXmlTmpFile);

   if (ttx_db_parse(ttxGrabState.startPage, ttxGrabState.stopPage, ttxGrabState.expireMin,
                    pXmlMergeFile, pXmlTmpFile, 0, 0) == 0)
   {
      // replace XML file with temporary output: XML_OUT_FILE_PAT
      pXmlOutFile = xstrdup(pXmlTmpFile);

      // remove ".tmp" suffix from output file name
      pXmlOutFile[strlen(pXmlOutFile) - 4] = 0;

      if (rename(pXmlTmpFile, pXmlOutFile) != 0)
      {
         debug3("TtxGrab-CheckPostProcess: failed to rename '%s' into '%s': %d", pXmlTmpFile, pXmlOutFile, errno);
         unlink(pXmlTmpFile);
      }
      xfree(pXmlOutFile);
      ttxGrabState.postProcDone = TRUE;
   }
   xfree(pXmlTmpFile);

   if (reset)
   {
      ttx_db_init();
   }

   if (pXmlMergeFile != NULL)
   {
      xfree(pXmlMergeFile);
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

   ttxGrabState.startPage = startPage;
   ttxGrabState.stopPage = stopPage;

   memset(&ttxGrabState.stats, 0, sizeof(ttxGrabState.stats));

   memset(&ttxGrabState.curPgHdRep, 0, sizeof(ttxGrabState.curPgHdRep));
   memset(&ttxGrabState.refPgHdText, 0xff, sizeof(ttxGrabState.refPgHdText));
   ttxGrabState.havePgHd = FALSE;
   memset(ttxGrabState.cniDecInd, 0, sizeof(ttxGrabState.cniDecInd));

   ttx_db_init();

   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition
// - called when acq is disabled (i.e. not upon channel switch)
//
void TtxGrab_Stop( void )
{
   ttx_db_init();
}

// ---------------------------------------------------------------------------
// Configuration parameters
// - called during start-up and configuration changes
//
void TtxGrab_SetConfig( uint expireMin, const char * pPerlPath, bool keepTtxInp )
{
   // passed through to TTX grabber (discard programmes older than X hours)
   ttxGrabState.expireMin = expireMin;
   ttxGrabState.keepTtxInp = keepTtxInp;
}

// ---------------------------------------------------------------------------
// Free resources
//
void TtxGrab_Exit( void )
{
   TtxGrab_Stop();
}

// ---------------------------------------------------------------------------
// Initialize the internal decoder state
//
void TtxGrab_Init( void )
{
   memset(&ttxGrabState, 0, sizeof(ttxGrabState));
}

#endif  // USE_TTX_GRABBER
