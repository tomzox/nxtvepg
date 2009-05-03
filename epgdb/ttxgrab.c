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
 *  $Id: ttxgrab.c,v 1.11 2009/05/02 19:10:04 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#ifndef WIN32
#include <sys/types.h>
#include <signal.h>
#include <wait.h>
#else
#include <windows.h>
#endif

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/hamming.h"
#include "epgdb/ttxgrab.h"

#ifdef USE_TTX_GRABBER

#if !defined (TTX_GRAB_SCRIPT_PATH)
#error "Name of external grabber script missing"  // should be defined in makefile
#endif

// three possible locations for the grabber script: current dir, nxtvepg cfg dir, $PATH
#define TTX_GRAB_SCRIPT_LOCAL   "tv_grab_ttx.pl"
#ifndef WIN32
#define TTX_GRAB_SCRIPT_NXTVEPG  TTX_GRAB_SCRIPT_PATH "/tv_grab_ttx.pl"
#define TTX_GRAB_SCRIPT_GLOBAL  "tv_grab_ttx"
#endif

#define XML_OUT_FILE_PAT   "ttx-%s.xml.tmp"
#define TTX_CAP_FILE_PAT   "ttx-%s.dat"
#define TTX_CAP_FILE_TMP   "ttx.tmp"

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
   char       * pXmlTmpFile;
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
   FILE       * fpTtxTmp;
   int          ioError;
#ifndef WIN32
   int          child_pipe;
   pid_t        child_pid;
#else
   bool         child_active;
   HANDLE       child_handle;
   HANDLE       child_stderr;
   char       * pPerlExe;
#endif
   uint32_t     cniDecInd[CNI_TYPE_COUNT];
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
            }
            else if (ttxGrabState.curPgHdRep[idx] < HEADER_CHAR_MIN_REP)
            {
               ttxGrabState.curPgHdRep[idx] += 1;
            }
         }
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
   }
}

// ---------------------------------------------------------------------------
// Add CNI to grabber output
// - CNIs are essential to generate channel IDs for the XMLTV output
//
static void TtxGrab_ProcessCni( void )
{
   VBI_LINE vbl;
   CNI_TYPE cniType;
   uint   newCni;
   ssize_t ret;

   // retrieve new CNI from teletext decoder, if any
   if ( TtxDecode_GetCniAndPil(&newCni, NULL, &cniType, ttxGrabState.cniDecInd, NULL, NULL) )
   {
      memset(&vbl, 0, sizeof(vbl));

      switch (cniType)
      {
         case CNI_TYPE_VPS:
            vbl.pkgno = 32;
            break;
         case CNI_TYPE_PDC:
         case CNI_TYPE_NI:
            vbl.pageno = 0x800;
            vbl.pkgno = 30;
            break;
         default:
            SHOULD_NOT_BE_REACHED;
            break;
      }
      // note: data buffer is not dword-aligned -> use memcpy
      memcpy(vbl.data, &newCni, sizeof(newCni));

      //dprintf2("TtxGrab-StoreCni: 0x%04X -> 0x%04X\n", newCni, CniConvertUnknownToPdc(newCni));

      if (ttxGrabState.fpTtxTmp != NULL)
      {
         ret = fwrite(&vbl, sizeof(vbl), 1, ttxGrabState.fpTtxTmp);
         if (ret != 1)
         {
            debug1("TtxGrab-ProcessPackets: write error %d", errno);
            ttxGrabState.ioError = ferror(ttxGrabState.fpTtxTmp);
            fclose(ttxGrabState.fpTtxTmp);
            ttxGrabState.fpTtxTmp = NULL;
            // TODO notify parent
         }
      }
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
   ssize_t ret;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) )
   {
      assert(pVbiBuf->startPageNo == ttxGrabState.startPage);

      // fetch the oldest packet from the teletext ring buffer
      seenHeader = FALSE;
      pkgOff = 0;
      while ( (pVbl = TtxDecode_GetPacket(pkgOff)) != NULL )
      {
         ttxGrabState.stats.ttxPkgCount += 1;
         pkgOff += 1;

         if ( (pVbl->pageno >= ttxGrabState.startPage) &&
              (pVbl->pageno <= ttxGrabState.stopPage) )
         {
            //dprintf4("Process idx=%d: pg=%03X.%04X pkg=%d\n", pVbiBuf->reader_idx, pVbl->pageno, pVbl->ctrl_lo & 0x3F7F, pVbl->pkgno);
            if (pVbl->pkgno == 0)
            {
               //dprintf3("%03X.%04X %s\n", pVbl->pageno, pVbl->ctrl_lo & 0x3F7F, TtxGrab_PrintHeader(pVbl->data, TRUE));
               seenHeader = TRUE;
               ttxGrabState.stats.ttxPagCount += 1;
               TtxGrab_PageHeaderCheck(pVbl->data + 8, pVbl->pageno);
            }

            ttxGrabState.stats.ttxPkgStrSum += 40;
            ttxGrabState.stats.ttxPkgParErr +=
               UnHamParityArray(pVbl->data, (void*)pVbl->data, 40);
            // XXX FIXME: using void cast to remove const for target buffer

            // skip file output if disabled (note all other processing is intentionally done anyways)
            if ( (ttxGrabState.fpTtxTmp != NULL) &&
                 (ttxGrabState.refPgDiffCnt == 0) )
            {
               ret = fwrite(pVbl, sizeof(*pVbl), 1, ttxGrabState.fpTtxTmp);
               if (ret != 1)
               {
                  debug1("TtxGrab-ProcessPackets: write error %d", ferror(ttxGrabState.fpTtxTmp));
                  ttxGrabState.ioError = ferror(ttxGrabState.fpTtxTmp);
                  fclose(ttxGrabState.fpTtxTmp);
                  ttxGrabState.fpTtxTmp = NULL;
                  // TODO notify parent
                  break;
               }
            }
         }
      }

      // make sure to check at least one page header for channel changes
      // (i.e. even if no page in the requested range was found in this interval)
      if ((seenHeader == FALSE) || ttxGrabState.havePgHd)
      {
         char buf[40];
         uint pgNum;
         pkgOff = 0;
         while (TtxDecode_GetPageHeader(buf, &pgNum, pkgOff))
         {
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
void TtxGrab_GetPageStats( bool * pInRange, bool * pRangeDone, bool * pSourceLock )
{
   uint magBuf[8];
   uint idx;
   uint sum;
   uint mag;
   sint dirSum;
   bool revDirection;
   bool inRange = FALSE;
   bool rangeDone = FALSE;

   if (TtxDecode_GetMagStats(magBuf, &dirSum, TRUE))
   {
      sum = 0;
      for (idx=0; idx<8; idx++)
         sum += magBuf[idx];
      if (sum != 0)
      {
         mag = (ttxGrabState.startPage >> 8) & 0x07;

         inRange = (magBuf[mag] <= 2);
         revDirection = ((0 - dirSum) >= (int)sum/4) && (sum > 10);
         if (revDirection)
            inRange &= ((magBuf[MODULO_ADD(mag,1,8)] + magBuf[MODULO_ADD(mag,2,8)]) > sum / 4)
                    && (magBuf[MODULO_ADD(mag,1,8)] > 0);
         else
            inRange &= ((magBuf[MODULO_SUB(mag,1,8)] + magBuf[MODULO_SUB(mag,2,8)]) > sum / 4)
                    && (magBuf[MODULO_SUB(mag,1,8)] > 0);

         rangeDone = (magBuf[mag] <= 2) && (sum > 10) &&
                     ( (ttxGrabState.stats.ttxPagCount > 30) ||
                       (ttxGrabState.stats.ttxPagCount >= (ttxGrabState.stopPage - ttxGrabState.startPage + 1)));

#ifndef DPRINTF_OFF
         for (idx=0; idx<8; idx++)
            dprintf1("%2d%% ", magBuf[idx] * 100 / sum);
         dprintf5(" - sum:%d dir:%d rev:%d wait:%d done:%d\n", sum, dirSum, revDirection, inRange, rangeDone);
#endif
      }
   }

   if (pInRange != NULL)
      *pInRange = inRange;
   if (pRangeDone != NULL)
      *pRangeDone = rangeDone;
   if (pSourceLock != NULL)
      *pSourceLock = ttxGrabState.havePgHd;
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
// Terminate the post-processor process forcefully
//
static bool TtxGrab_KillParser( void )
{
   bool result = FALSE;
#ifndef WIN32
   assert((ttxGrabState.child_pid == -1) || (ttxGrabState.child_pid > 0));

   if (ttxGrabState.child_pid > 0)  // fail-safe: do not check for -1 only
   {
      int status;
      if (kill(ttxGrabState.child_pid, SIGTERM) == 0)
         dprintf1("TtxGrab-Stop: killed grabber PID %d\n", ttxGrabState.child_pid);
      waitpid(ttxGrabState.child_pid, &status, WNOHANG);
      ttxGrabState.child_pid = -1;
      result = TRUE;
   }
#else
   if (ttxGrabState.child_active)
   {
      dprintf0("TtxGrab-Stop: killing grabber process\n");
      assert(ttxGrabState.child_handle != INVALID_HANDLE_VALUE);
      TerminateProcess(ttxGrabState.child_handle, 1);
      WaitForSingleObject(ttxGrabState.child_handle, 200);  // max. 200 ms
      CloseHandle(ttxGrabState.child_handle);
      CloseHandle(ttxGrabState.child_stderr);
      ttxGrabState.child_active = FALSE;
      ttxGrabState.child_handle = INVALID_HANDLE_VALUE;
      result = TRUE;
   }
#endif
   return result;
}

// ---------------------------------------------------------------------------
// Spawn child process and invoke perl
//
static bool TtxGrab_SpawnParser( FILE * fpGrabInTmp, const char * pOutFile, const char * pMergeFile )
{ 
   bool result = FALSE;
#ifndef WIN32
   int pipe_fd[2];
   pid_t pid;
   char exp_str[20];
   char * argv[16];
   uint arg_idx;

   if (pipe(pipe_fd) == 0)
   {
      pid = fork();
      if (pid == 0)
      {
         // assign pipe to child's STDERR
         close(pipe_fd[0]);
         dup2(pipe_fd[1], 2);
         close(pipe_fd[1]);

         // assign teletext packet temp file as STDIN
         dup2(fileno(fpGrabInTmp), 0);

         sprintf(exp_str, "%d", ttxGrabState.expireMin);
         //unlink(pOutFile); // FIXME

         arg_idx = 0;
         argv[arg_idx++] = TTX_GRAB_SCRIPT_LOCAL;
         argv[arg_idx++] = TTX_GRAB_SCRIPT_LOCAL;
         argv[arg_idx++] = "-expire";
         argv[arg_idx++] = exp_str;
         if (pMergeFile != NULL)
         {
            argv[arg_idx++] = "-merge";
            argv[arg_idx++] = (char *) pMergeFile;
         }
         argv[arg_idx++] = "-outfile";
         argv[arg_idx++] = (char *) pOutFile;
         argv[arg_idx++] = "-";
         argv[arg_idx] = NULL;

         if (access(TTX_GRAB_SCRIPT_LOCAL, R_OK) == 0)
         {
            dprintf9("EXEC perl %s %s %s %s %s %s %s %s %s\n", argv[0], argv[1], argv[2], argv[3], ((arg_idx > 4) ? argv[4] : ""), ((arg_idx > 5) ? argv[5] : ""), ((arg_idx > 6) ? argv[6] : ""), ((arg_idx > 7) ? argv[7] : ""), ((arg_idx > 8) ? argv[8] : ""));
            execvp("perl", argv);
         }
         else if (access(TTX_GRAB_SCRIPT_NXTVEPG, R_OK) == 0)
         {
            argv[0] = TTX_GRAB_SCRIPT_NXTVEPG;
            argv[1] = TTX_GRAB_SCRIPT_NXTVEPG;
            dprintf9("EXEC perl %s %s %s %s %s %s %s %s %s\n", argv[0], argv[1], argv[2], argv[3], ((arg_idx > 4) ? argv[4] : ""), ((arg_idx > 5) ? argv[5] : ""), ((arg_idx > 6) ? argv[6] : ""), ((arg_idx > 7) ? argv[7] : ""), ((arg_idx > 8) ? argv[8] : ""));
            execvp("perl", argv);
         }
         else
         {
            argv[1] = TTX_GRAB_SCRIPT_GLOBAL;
            dprintf8("EXEC %s %s %s %s %s %s %s %s\n", argv[1], argv[2], argv[3], ((arg_idx > 4) ? argv[4] : ""), ((arg_idx > 5) ? argv[5] : ""), ((arg_idx > 6) ? argv[6] : ""), ((arg_idx > 7) ? argv[7] : ""), ((arg_idx > 8) ? argv[8] : ""));
            execvp(TTX_GRAB_SCRIPT_GLOBAL, argv + 1);
         }

         // only reached in error cases
         fprintf(stderr, "Failed to start grabber script or perl interpreter: %s\n", strerror(errno));
         exit(-1);
      } 
      else if (pid > 0)
      {
         // parent process
         close(pipe_fd[1]);
         fcntl(pipe_fd[0], F_SETFL, O_NONBLOCK);
         ttxGrabState.child_pipe = pipe_fd[0];
         ttxGrabState.child_pid = pid;

         dprintf3("TtxGrab-SpawnParser: grabbing into '%s', merge '%s', pid=%d\n", pOutFile, ((pMergeFile!=NULL)?pMergeFile:""), (int)pid);
         result = TRUE;
      } 
      else
      { 
         fprintf(stderr, "Failed to fork for spawning perl interpreter: %s\n", strerror(errno));
         close(pipe_fd[0]);
         close(pipe_fd[1]);
      }
   }
   else
   {
      fprintf(stderr, "Failed to create pipe to perl interpreter: %s\n", strerror(errno));
   } 
#else
   PROCESS_INFORMATION proc_info;
   STARTUPINFO         startup;
   SECURITY_ATTRIBUTES sec_att;
   HANDLE rdPipe, wrPipe;
   char * pPerlExe;
   char * pCmd;

   memset(&proc_info, 0, sizeof(proc_info));
   memset(&startup, 0, sizeof(startup));
   startup.cb = sizeof(startup);
   startup.dwFlags = STARTF_USESTDHANDLES;
   startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);

   // assign teletext packet temp file as STDIN
   startup.hStdInput = (HANDLE)_get_osfhandle(_fileno(fpGrabInTmp));
   ifdebug2(startup.hStdInput == INVALID_HANDLE_VALUE, "Failed to get handle for ttx temp file (fd %d): %ld", _fileno(fpGrabInTmp), GetLastError());
   SetHandleInformation(startup.hStdInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

   memset(&sec_att, 0, sizeof(sec_att));
   sec_att.nLength = sizeof(sec_att);
   sec_att.bInheritHandle = TRUE;
   CreatePipe(&rdPipe, &wrPipe, &sec_att, 0);
   SetHandleInformation(rdPipe, HANDLE_FLAG_INHERIT, 0);
   startup.hStdError = wrPipe;
   //startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

   if (ttxGrabState.pPerlExe != NULL)
      pPerlExe = ttxGrabState.pPerlExe;
   else
      pPerlExe = "perl";

   if (pMergeFile != NULL)
   {
      pCmd = xmalloc(strlen(pPerlExe) + strlen(pOutFile) + strlen(pMergeFile) + 100);
      sprintf(pCmd, "\"%s\" \"%s\" -expire %d -merge \"%s\" -outfile \"%s\" -", pPerlExe, TTX_GRAB_SCRIPT_LOCAL, ttxGrabState.expireMin, pMergeFile, pOutFile);
   }
   else
   {
      pCmd = xmalloc(strlen(pPerlExe) + strlen(pOutFile) + 100);
      sprintf(pCmd, "\"%s\" \"%s\" -expire %d -outfile \"%s\" -", pPerlExe, TTX_GRAB_SCRIPT_LOCAL, ttxGrabState.expireMin, pOutFile);
   }

   if (CreateProcess(NULL, pCmd, NULL, NULL, TRUE,
                     CREATE_NO_WINDOW, NULL, NULL, &startup, &proc_info) )
   {
      result = TRUE;

      ttxGrabState.child_active = TRUE;
      ttxGrabState.child_handle = proc_info.hProcess;
      ttxGrabState.child_stderr = rdPipe;

      SetThreadPriority(proc_info.hThread, THREAD_PRIORITY_BELOW_NORMAL);
      CloseHandle(proc_info.hThread);
      CloseHandle(wrPipe);

      dprintf3("TtxGrab-SpawnParser: PID %ld, grabbing into '%s', merge '%s'\n", proc_info.dwProcessId, pOutFile, ((pMergeFile!=NULL)?pMergeFile:""));
   }
   else
   {  // FIXME cannot report to stderr since we're not a console process
      debug1("Failed to start process: system error #%ld", GetLastError());
      CloseHandle(rdPipe);
      CloseHandle(wrPipe);
   }
   xfree(pCmd);
#endif
   return result;
}

// ---------------------------------------------------------------------------
// Check if the child-process has finished
// - the child status is polled from inside the ttx packet handler
//   i.e. there's no asynchronous notification
// - UNIX: wait for zero-read on child's STDERR output
// - WIN32: poll child process status
//
static bool TtxGrab_CheckChildStatus( uint * pExitCode )
{
   bool result = FALSE;
#ifndef WIN32
   struct timeval tv;
   char buf[256];
   fd_set rd;
   int sret;
   ssize_t rdlen;

   if (ttxGrabState.child_pipe != -1)
   {
      do
      {
         do
         {
            FD_ZERO(&rd);
            FD_SET(ttxGrabState.child_pipe, &rd);
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            sret = select(ttxGrabState.child_pipe + 1, &rd, NULL, NULL, &tv);
         } while ((sret < 0) && (errno == EINTR));

         if (sret > 0)
         {
            if (FD_ISSET(ttxGrabState.child_pipe, &rd))
            {
               rdlen = read(ttxGrabState.child_pipe, buf, sizeof(buf)-1);
               if (rdlen > 0)
               {
                  buf[rdlen] = 0;
                  dprintf1("TtxGrab-CheckChildStatus: read from STDERR: '%s'\n", buf);
               }
               else if ((rdlen == 0) || ((rdlen < 0) && (errno != EINTR) && (errno != EAGAIN)))
               {
                  dprintf1("TtxGrab-CheckChildStatus: post-processing finished (PID %d)\n", (int)ttxGrabState.child_pid);
                  *pExitCode = 0; //FIXME
                  result = TRUE;
               }
            }
         }
         else if (sret < 0)
         {
            debug1("TtxGrab-CheckChildStatus: select on child handle failed: %d", errno);
            *pExitCode = -1;
            result = TRUE;
         }
      } while (!result && (sret > 0));
   }
   if (result)
   {
      //sret = waitpid(ttxGrabState.childPid, &status, WNOHANG);
      close(ttxGrabState.child_pipe);
      ttxGrabState.child_pipe = -1;
      ttxGrabState.child_pid = -1;
   }
#else
   DWORD exitCode;
   DWORD lenAvail;
   char buf[256];

   if (ttxGrabState.child_active)
   {
      while (PeekNamedPipe(ttxGrabState.child_stderr, &buf, 1,
                           NULL, &lenAvail, NULL) && (lenAvail > 0))
      {
         if (lenAvail >= sizeof(buf))
            lenAvail = sizeof(buf) - 1;

         if (ReadFile(ttxGrabState.child_stderr, &buf, lenAvail, &lenAvail, NULL))
         {
            buf[lenAvail] = 0;
            debug1("TtxGrab-CheckChildStatus: read from STDERR: '%s'", buf);
         }
      }

      if (GetExitCodeProcess(ttxGrabState.child_handle, &exitCode))
      {
         if (exitCode != STILL_ACTIVE)
         {
#ifndef DPRINTF_OFF
            FILETIME CreationTime, ExitTime, KernelTime, UserTime;

            if (GetProcessTimes(ttxGrabState.child_handle,
                                &CreationTime, &ExitTime, &KernelTime, &UserTime))
            {
               ULARGE_INTEGER KernelTimeInt, UserTimeInt;  // 100ns per tick
               memcpy(&KernelTimeInt, &KernelTime, sizeof(KernelTime));
               memcpy(&UserTimeInt, &UserTime, sizeof(UserTime));
               dprintf2("TtxGrab-CheckChildStatus: post-processing is done: exit code %ld, required %lld ms\n", exitCode, (KernelTimeInt.QuadPart + UserTimeInt.QuadPart)/10000);
            }
#endif
            if (WaitForSingleObject(ttxGrabState.child_handle, 200) == WAIT_FAILED)
               debug1("TtxGrab-CheckChildStatus: failed to wait for child: %ld", GetLastError());
            CloseHandle(ttxGrabState.child_handle);
            CloseHandle(ttxGrabState.child_stderr);
            ttxGrabState.child_active = FALSE;
            ttxGrabState.child_handle = INVALID_HANDLE_VALUE;

            ifdebug1(exitCode != 0, "TtxGrab-CheckChildStatus: non-zero exit code from perl script: %ld", exitCode);
            *pExitCode = (uint) exitCode;
            result = TRUE;
         }
      }
      else
         debug1("TtxGrab-CheckChildStatus: failed to query child process status: %ld", GetLastError());
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
   char * pXmlOutFile;
   uint exitCode = 0;
   bool result = FALSE;

   if ( TtxGrab_CheckChildStatus(&exitCode) )
   {
      if (ttxGrabState.pXmlTmpFile != NULL)
      {
         if ( (exitCode == 0) &&
              TtxGrab_CheckFileExists(ttxGrabState.pXmlTmpFile) )
         {
            pXmlOutFile = xstrdup(ttxGrabState.pXmlTmpFile);

            // remove ".tmp" suffix from output file name
            pXmlOutFile[strlen(pXmlOutFile) - 4] = 0;

            // replace XML file with temporary output: XML_OUT_FILE_PAT
#ifndef WIN32
            if (rename(ttxGrabState.pXmlTmpFile, pXmlOutFile) != 0)
            {
               debug3("TtxGrab-CheckPostProcess: failed to rename '%s' into '%s': %d", ttxGrabState.pXmlTmpFile, pXmlOutFile, errno);
               unlink(ttxGrabState.pXmlTmpFile);
               xfree(pXmlOutFile);
            }
#else
            // note: MoveFileEx which allows safely replacing existing files is not available for W98 (how lame is that)
            if (DeleteFile(pXmlOutFile) == 0)
               ifdebug2(GetLastError() != ERROR_FILE_NOT_FOUND, "TtxGrab-CheckPostProcess: failed to remove '%s': %ld", pXmlOutFile, GetLastError());
            if (MoveFile(ttxGrabState.pXmlTmpFile, pXmlOutFile) == 0)
            {
               debug3("TtxGrab-CheckPostProcess: failed to rename '%s' into '%s': %ld", ttxGrabState.pXmlTmpFile, pXmlOutFile, GetLastError());
               unlink(ttxGrabState.pXmlTmpFile);
               xfree(pXmlOutFile);
            }
#endif
            else
            {  // successfully renamed tmp output file to target XML file
               // if requested, return XML file name; else free the memory
               if (ppXmlName != NULL)
                  *ppXmlName = pXmlOutFile;
               else
                  xfree(pXmlOutFile);

               result = TRUE;
            }
         }
         else
         {
            ifdebug1(exitCode == 0, "TtxGrab-CheckPostProcess: grabber output file missing or empty (%s)", ttxGrabState.pXmlTmpFile);
            unlink(ttxGrabState.pXmlTmpFile);
         }

         xfree(ttxGrabState.pXmlTmpFile);
         ttxGrabState.pXmlTmpFile = NULL;
      }
      else
         debug0("TtxGrab-CheckPostProcess: XML tmp file undefined");
   }
   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition and start post-processing
//
void TtxGrab_PostProcess( const char * pName, bool reset )
{
   char * pXmlMergeFile;
   char * pKeptInpFile;
   int sys_ret;

   // optional (debug only): keep VBI raw data by renaming the file
   if (ttxGrabState.keepTtxInp && (ttxGrabState.fpTtxTmp != NULL))
   {
#ifdef WIN32
      fclose(ttxGrabState.fpTtxTmp);
#endif
      pKeptInpFile = xmalloc(strlen(pName) + 20);
      sprintf(pKeptInpFile, TTX_CAP_FILE_PAT, pName);
      rename(TTX_CAP_FILE_TMP, pKeptInpFile);
#ifdef WIN32
      ttxGrabState.fpTtxTmp = fopen(pKeptInpFile, "r+b");
#endif
      xfree(pKeptInpFile);
   }

   if (ttxGrabState.fpTtxTmp != NULL)
   {
      // assign the ttx temp output file as XML input
      fflush(ttxGrabState.fpTtxTmp);
      rewind(ttxGrabState.fpTtxTmp);

      // note: old grabber must finish before we get here again
      TtxGrab_CheckPostProcess(NULL);
      if (ttxGrabState.pXmlTmpFile != NULL)
      {
         debug1("TtxGrab-PostProcess: old grabber still running! terminating process and removing tmp file '%s'", ttxGrabState.pXmlTmpFile);
         TtxGrab_KillParser();
         sys_ret = unlink(ttxGrabState.pXmlTmpFile);
         ifdebug2((sys_ret != 0), "TtxGrab-PostProcess: failed to remove tmp file: %d (%s)", errno, strerror(errno));
         xfree(ttxGrabState.pXmlTmpFile);
         ttxGrabState.pXmlTmpFile = NULL;
      }

      // build output file name (note: includes ".tmp" suffix which is removed later)
      ttxGrabState.pXmlTmpFile = xmalloc(strlen(pName) + 20);
      sprintf(ttxGrabState.pXmlTmpFile, XML_OUT_FILE_PAT, pName);

      pXmlMergeFile = xstrdup(ttxGrabState.pXmlTmpFile);
      pXmlMergeFile[strlen(pXmlMergeFile) - 4] = 0;
      if (TtxGrab_CheckFileExists(pXmlMergeFile) == FALSE)
      {
         xfree(pXmlMergeFile);
         pXmlMergeFile = NULL;
      }

      if (TtxGrab_SpawnParser(ttxGrabState.fpTtxTmp, ttxGrabState.pXmlTmpFile, pXmlMergeFile) == FALSE)
      {
         // failed to start parser -> remove input and output files
         xfree(ttxGrabState.pXmlTmpFile);
         ttxGrabState.pXmlTmpFile = NULL;
      }

      if (reset)
      {
         fclose(ttxGrabState.fpTtxTmp);
         ttxGrabState.fpTtxTmp = NULL;
      }
      else
      {
         fseek(ttxGrabState.fpTtxTmp, 0, SEEK_END);
      }

      if (pXmlMergeFile != NULL)
      {
         xfree(pXmlMergeFile);
      }
   }
   else
      debug0("TtxGrab-PostProcess: acquisition not active");
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

   // close output file if still open (acquisition reset)
   if (ttxGrabState.fpTtxTmp != NULL)
   {
      fclose (ttxGrabState.fpTtxTmp);
      ttxGrabState.fpTtxTmp = NULL;
   }

#ifdef WIN32
   // check if grabber script exists and is readable (need not be executable)
   if (access(TTX_GRAB_SCRIPT_LOCAL, R_OK) != 0)
   {
      debug3("TtxGrab-Start: grabber script '%s' not available: %s (%d)", TTX_GRAB_SCRIPT_LOCAL, strerror(errno), (int)errno);
      result = FALSE;
   }
   else
#endif
   {
      if (enableOutput)
      {
         ttxGrabState.ioError = 0;
         if (ttxGrabState.keepTtxInp)
            ttxGrabState.fpTtxTmp = fopen(TTX_CAP_FILE_TMP, "w+b");
         else
            ttxGrabState.fpTtxTmp = tmpfile();  // note: mode is "w+b"
         ifdebug1(ttxGrabState.fpTtxTmp == NULL, "TtxGrab-Start: failed to create tmpfile: %d", errno);
         result = (ttxGrabState.fpTtxTmp != NULL);
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
   if (ttxGrabState.fpTtxTmp != NULL)
   {
      // note: closing a file created with tmpfile() automatically deletes the file
      fclose (ttxGrabState.fpTtxTmp);
      ttxGrabState.fpTtxTmp = NULL;
   }
   if (ttxGrabState.keepTtxInp)
   {
      unlink(TTX_CAP_FILE_TMP);
   }

   // stop the child-process, if running
   TtxGrab_KillParser();

   // remove unprocessed XML output files
   if (ttxGrabState.pXmlTmpFile != NULL)
   {
      unlink(ttxGrabState.pXmlTmpFile);
      xfree(ttxGrabState.pXmlTmpFile);
      ttxGrabState.pXmlTmpFile = NULL;
   }
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

#ifdef WIN32
   if (ttxGrabState.pPerlExe != NULL)
   {
      xfree(ttxGrabState.pPerlExe);
      ttxGrabState.pPerlExe = NULL;
   }
   if (pPerlPath != NULL)
   {
      ttxGrabState.pPerlExe = xstrdup(pPerlPath);
   }
#endif
}

// ---------------------------------------------------------------------------
// Free resources
//
void TtxGrab_Exit( void )
{
   TtxGrab_Stop();

#ifdef WIN32
   if (ttxGrabState.pPerlExe != NULL)
   {
      xfree(ttxGrabState.pPerlExe);
      ttxGrabState.pPerlExe = NULL;
   }
#endif
}

// ---------------------------------------------------------------------------
// Initialize the internal decoder state
//
void TtxGrab_Init( void )
{
   memset(&ttxGrabState, 0, sizeof(ttxGrabState));
#ifndef WIN32
   ttxGrabState.child_pipe = -1;
   ttxGrabState.child_pid = -1;
#else
   ttxGrabState.child_handle = INVALID_HANDLE_VALUE;  // fail-safe only
#endif
}

#endif  // USE_TTX_GRABBER
