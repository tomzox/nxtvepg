/*
 *  Nextview EPG block database dump & reload
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
 *    Saves the EPG database into a (binary) file. This is simply done by
 *    dumping all raw blocks consecutively. The reload function reverses
 *    these actions. The reload does not use the normal db insert functions,
 *    because the loaded blocks are already sorted and checked for overlaps,
 *    so that each can simply be appended to the database. Reloaded blocks
 *    are however thoroughly checked for consistancy (e.g. size and offset
 *    values must be limited to the total block size) to avoid application
 *    crashes due to corrupt input.
 *
 *    The module also offers functions to scan the database directory for
 *    database files (e.g. to present a provider list in the GUI) and to
 *    read or update database headers.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgdbsav.c,v 1.55 2004/05/31 14:38:47 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#ifndef WIN32
#include <unistd.h>
#include <dirent.h>
#else
#include <windows.h>
#include <stdlib.h>
#include <io.h>
#include <direct.h>
#include <ctype.h>
#endif
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/epgversion.h"
#include "epgctl/debug.h"
#include "epgvbi/syserrmsg.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgswap.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"



//internal shortcuts
typedef const EPGDB_CONTEXT * CPDBC;
typedef       EPGDB_CONTEXT * PDBC;


static const char * epgDbDirPath = NULL;
static const char * epgDemoDb    = NULL;

// cache current time during database reload (optimization only)
static time_t  epgDbReloadCurTime;
static time_t  epgDbReloadExpireDelayPi = EPGDBSAV_DEFAULT_EXPIRE_TIME;


// ---------------------------------------------------------------------------
// Write the file header
//
static bool EpgDbDumpHeader( CPDBC dbc, int fd )
{
   EPGDBSAV_HEADER header;
   size_t size, rest;
   const uchar *p;

   if (dbc->pAiBlock != NULL)
   {
      strncpy(header.magic, MAGIC_STR, MAGIC_STR_LEN);
      memset(header.reserved, 0, sizeof(header.reserved));
      header.endianMagic   = ENDIAN_MAGIC;
      header.compatVersion = DUMP_COMPAT;
      header.swVersion     = EPG_VERSION_NO;
      header.cni = AI_GET_NETWOP_N(&dbc->pAiBlock->blk.ai, dbc->pAiBlock->blk.ai.thisNetwop)->cni;
      header.pageNo = dbc->pageNo;
      header.tunerFreq = dbc->tunerFreq;
      header.appId = dbc->appId;
      header.lastAiUpdate = dbc->pAiBlock->acqTimestamp;

      if (dbc->pFirstPi != NULL)
         header.firstPiDate = dbc->pFirstPi->blk.pi.start_time;
      else
         header.firstPiDate = 0;  // db may contain no PI

      if (dbc->pLastPi != NULL)
         header.lastPiDate = dbc->pLastPi->blk.pi.stop_time;
      else
         header.lastPiDate = 0;  // db may contain no PI

      rest = sizeof(header);
      p = (uchar *) &header;
      do
      {
         size = write(fd, p, rest);
         if (size < 0)
         {
            fprintf(stderr, "Error writing database header: %s\n", strerror(errno));
            return FALSE;
         }
         p += size;
         rest -= size;
      }
      while (rest > 0);
   }
   else
      debug0("EpgDbDump-Header: no AI block");

   return TRUE;
}

// ---------------------------------------------------------------------------
// Append a single block to the output file
//
static bool EpgDbDumpAppendBlock( const EPGDB_BLOCK * pBlock, int fd )
{
   size_t size, rest;
   const uchar *p;

   rest = pBlock->size + BLK_UNION_OFF;
   p = (uchar *)pBlock;
   do
   {
      size = write(fd, p, rest);
      if (size < 0)
      {
         fprintf(stderr, "Error writing in database: %s\n", strerror(errno));
         return FALSE;
      }
      p += size;
      rest -= size;
   }
   while (rest > 0);

   return TRUE;
}

// ---------------------------------------------------------------------------
// Returns file name format and path for the given CNI
// - only for reading databases; for writing the same name is used as was found
//   for read; for new databases always use the default format
//
static DB_FMT_TYPE EpgDbBuildFileName( uint cni, char * pFilename )
{
   DB_FMT_TYPE firstFmt;
   DB_FMT_TYPE curFmt;

   curFmt = firstFmt = DUMP_NAME_DEF_FMT;
   do
   {
      // first assemble a path with the default format
      if (curFmt == DB_FMT_UNIX)
         sprintf(pFilename, "%s" PATH_SEPARATOR_STR DUMP_NAME_FMT_UNIX, epgDbDirPath, cni);
      else
         sprintf(pFilename, "%s" PATH_SEPARATOR_STR DUMP_NAME_FMT_DOS, epgDbDirPath, cni);

      // check if a file with this path exists
      if (access(pFilename, F_OK) == 0)
         break;

      // not found -> try next format
      curFmt = (curFmt + 1) % DB_FMT_COUNT;
   }
   while (curFmt != firstFmt);

   dprintf2("EpgDb-BuildFileName: file name format %d for %04X\n", curFmt, cni);

   return curFmt;
}

// ---------------------------------------------------------------------------
// Dump all types of blocks in the database into a file
// - returns TRUE if the operation was completed successfully
//
bool EpgDbDump( PDBC dbc )
{
   uchar * pFilename;
   #ifdef DUMP_NAME_TMP
   uchar * pTmpname;
   #endif
   EPGDB_BLOCK *pWalk;
   BLOCK_TYPE type;
   int  fd;
   uint cni;
   bool result = FALSE;

   EpgDbLockDatabase(dbc, TRUE);
   if ((dbc->pAiBlock != NULL) && (dbc->modified))
   {
      pFilename = xmalloc(strlen(epgDbDirPath) + 1 + DUMP_NAME_MAX_LEN);
      cni = AI_GET_NETWOP_N(&dbc->pAiBlock->blk.ai, dbc->pAiBlock->blk.ai.thisNetwop)->cni;
      if (dbc->fileNameFormat == DB_FMT_UNIX)
         sprintf(pFilename, "%s" PATH_SEPARATOR_STR DUMP_NAME_FMT_UNIX, epgDbDirPath, cni);
      else
         sprintf(pFilename, "%s" PATH_SEPARATOR_STR DUMP_NAME_FMT_DOS, epgDbDirPath, cni);
      #ifdef DUMP_NAME_TMP
      pTmpname = xmalloc(strlen(epgDbDirPath) + 1 + DUMP_NAME_MAX_LEN);
      strcpy(pTmpname, pFilename);
      strcat(pFilename, DUMP_NAME_TMP);
      #endif
      fd = open(pFilename, O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, 0666);
      if (fd >= 0)
      {
         result = EpgDbDumpHeader(dbc, fd) &&
                  EpgDbDumpAppendBlock(dbc->pAiBlock, fd);

         for (type=0; (type < BLOCK_TYPE_GENERIC_COUNT) && result; type++)
         {
            pWalk = dbc->pFirstGenericBlock[type];
            while ((pWalk != NULL) && result)
            {
               result = EpgDbDumpAppendBlock(pWalk, fd);
               pWalk = pWalk->pNextBlock;
            }
         }

         pWalk = dbc->pFirstPi;
         while ((pWalk != NULL) && result)
         {
            result = EpgDbDumpAppendBlock(pWalk, fd);
            pWalk = pWalk->pNextBlock;
         }

         pWalk = dbc->pObsoletePi;
         while ((pWalk != NULL) && result)
         {
            assert(pWalk->version == ((pWalk->stream == 0) ? dbc->pAiBlock->blk.ai.version : dbc->pAiBlock->blk.ai.version_swo));
            pWalk->type = BLOCK_TYPE_DEFECT_PI;
            result = EpgDbDumpAppendBlock(pWalk, fd);
            pWalk->type = BLOCK_TYPE_PI;
            pWalk = pWalk->pNextBlock;
         }

         close(fd);

         if (result != FALSE)
         {  // dump successful
            #ifdef DUMP_NAME_TMP
            // move the new file over the previous version
            rename(pFilename, pTmpname);
            #endif
            // mark the db as not modified
            dbc->modified = FALSE;
         }
         #ifdef DUMP_NAME_TMP
         else
         {  // dump failed for some reason -> remove temporary file
            unlink(pFilename);
         }
         #endif
      }
      else
         fprintf(stderr, "Failed to write database %s: %s\n", pFilename, strerror(errno));

      xfree(pFilename);
      #ifdef DUMP_NAME_TMP
      xfree(pTmpname);
      #endif
   }
   else
      dprintf1("EpgDb-Dump: %s - not dumped\n", ((dbc->pAiBlock == NULL) ? "no AI block" : "not modified"));

   EpgDbLockDatabase(dbc, FALSE);
   return result;
}

// ---------------------------------------------------------------------------
// Add a loaded AI block to the database
//
static EPGDB_RELOAD_RESULT EpgDbReloadAddAiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   EPGDB_RELOAD_RESULT result;

   if ( EpgBlockCheckConsistancy(pBlock) )
   {
      if (dbc->pAiBlock == NULL)
      {
         pBlock->pNextBlock       = NULL;
         pBlock->pPrevBlock       = NULL;
         pBlock->pNextNetwopBlock = NULL;
         pBlock->pPrevNetwopBlock = NULL;

         dbc->pAiBlock = pBlock;
      }
      else
      {
         debug2("EpgDbReload-AddAiBlock: prov=0x%04X: AI block already exists - new CNI 0x%04X", AI_GET_CNI(&dbc->pAiBlock->blk.ai), AI_GET_CNI(&pBlock->blk.ai));
         xfree(pBlock);
      }
      result = EPGDB_RELOAD_OK;
   }
   else
   {
      xfree(pBlock);
      result = EPGDB_RELOAD_CORRUPT;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Add a loaded generic-type block (i.e. OI,MI,NI etc.) to the database
//
static EPGDB_RELOAD_RESULT EpgDbReloadAddGenericBlock( PDBC dbc, EPGDB_BLOCK *pBlock, EPGDB_BLOCK **pPrevGeneric )
{
   EPGDB_BLOCK *pPrev;
   uint  blockCount;
   uint  block_no;
   EPGDB_RELOAD_RESULT result;

   if ( EpgBlockCheckConsistancy(pBlock) )
   {
      block_no = pBlock->blk.all.block_no;
      blockCount = EpgDbGetGenericMaxCount(dbc, pBlock->type);
      if ( (block_no < blockCount) ||
           ((block_no == 0x8000) && ((pBlock->type == BLOCK_TYPE_LI) || (pBlock->type == BLOCK_TYPE_TI))) )
      {
         if (dbc->pFirstGenericBlock[pBlock->type] == NULL)
         {  // no block of this type in the database yet
            //dprintf3("RELOAD first GENERIC type=%d ptr=%lx: blockno=%d\n", type, (ulong)pBlock, block_no);
            pBlock->pNextBlock = NULL;
            dbc->pFirstGenericBlock[pBlock->type] = pBlock;
            pPrevGeneric[pBlock->type] = pBlock;
         }
         else
         {
            pPrev = pPrevGeneric[pBlock->type];
            assert(pPrev != NULL);

            if (pPrev->blk.all.block_no < block_no)
            {  // blocks are still sorted by increasing block numbers -> append the block
               //dprintf3("RELOAD GENERIC type=%d ptr=%lx: blockno=%d\n", type, (ulong)pBlock, block_no);
               pBlock->pNextBlock = NULL;
               pPrev->pNextBlock = pBlock;
               pPrevGeneric[pBlock->type] = pBlock;
            }
            else
            {  // unexpected block number - refuse (should never happen, since blocks are dumped sorted)
               debug3("EpgDbReload-AddGenericBlock: prov=0x%04X: blockno=%d <= pPrev %d", AI_GET_CNI(&dbc->pAiBlock->blk.ai), block_no, pPrev->blk.all.block_no);
               xfree(pBlock);
            }
         }
      }
      else
      {
         debug4("EpgDbReload-AddGenericBlock: prov=0x%04X: type=%d: invalid block_no=%d >= count=%d", AI_GET_CNI(&dbc->pAiBlock->blk.ai), pBlock->type, pBlock->blk.all.block_no, blockCount);
         xfree(pBlock);
      }
      //assert(EpgDbCheckChains());
      result = EPGDB_RELOAD_OK;
   }
   else
   {
      result = EPGDB_RELOAD_CORRUPT;
      xfree(pBlock);
   }

   return result;
}

// ---------------------------------------------------------------------------
// Add an expired or defect PI block to the database
//
static EPGDB_RELOAD_RESULT EpgDbReloadAddDefectPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   EPGDB_RELOAD_RESULT result;

   if ( EpgBlockCheckConsistancy(pBlock) )
   {
      if (pBlock->version == ((pBlock->stream == 0) ? dbc->pAiBlock->blk.ai.version : dbc->pAiBlock->blk.ai.version_swo))
      {
         if ( EpgDbPiBlockNoValid(dbc, pBlock->blk.pi.block_no, pBlock->blk.pi.netwop_no) )
         {
            // prepend to list of defect blocks
            pBlock->pNextBlock = dbc->pObsoletePi;
            dbc->pObsoletePi = pBlock;
         }
         else
         {
            debug4("EpgDbReload-AddDefectPiBlock: prov=0x%04X: invalid netwop=%d stop=%ld block_no=%d", AI_GET_CNI(&dbc->pAiBlock->blk.ai), pBlock->blk.pi.netwop_no, pBlock->blk.pi.stop_time, pBlock->blk.pi.block_no);
            xfree(pBlock);
         }
      }
      else
      {  // this is not an error, since the PI block may not (yet) have been obsolete when the db was dumped
         xfree(pBlock);
      }
      result = EPGDB_RELOAD_OK;
   }
   else
   {
      xfree(pBlock);
      result = EPGDB_RELOAD_CORRUPT;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Add a PI block to the database
//
static EPGDB_RELOAD_RESULT EpgDbReloadAddPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK **pPrevNetwop )
{
   EPGDB_BLOCK *pPrev;
   uchar netwop;
   EPGDB_RELOAD_RESULT result;
   
   if (EpgBlockCheckConsistancy(pBlock))
   {
      netwop = pBlock->blk.pi.netwop_no;
      pPrev  = pPrevNetwop[netwop];

      if ( (netwop < dbc->pAiBlock->blk.ai.netwopCount) &&
           (pBlock->blk.pi.start_time < pBlock->blk.pi.stop_time) )
      {
         pBlock->acqRepCount = 0;
         // check if the block still part of current stream, i.e. fits in AI block range
         ((PI_BLOCK *)&pBlock->blk.pi)->block_no_in_ai =
            EpgDbPiBlockNoValid(dbc, pBlock->blk.pi.block_no, netwop);

         if ( (pBlock->blk.pi.block_no_in_ai) ||
              ( (pBlock->blk.pi.stop_time <= epgDbReloadCurTime) &&
                (pBlock->blk.pi.stop_time >= epgDbReloadCurTime - dbc->expireDelayPi) ))
         {
            //dprintf4("RELOAD PI ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pBlock, netwop, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);

            if (dbc->pFirstPi == NULL)
            {  // there's no PI yet in the database -> just link with head pointers
               assert(dbc->pLastPi == NULL);
               assert(dbc->pFirstNetwopPi[netwop] == NULL);
               pBlock->pNextNetwopBlock = NULL;
               pBlock->pPrevNetwopBlock = NULL;
               dbc->pFirstNetwopPi[netwop] = pBlock;
               pBlock->pPrevBlock = NULL;
               pBlock->pNextBlock = NULL;
               dbc->pFirstPi = pBlock;
               dbc->pLastPi  = pBlock;
               pPrevNetwop[netwop] = pBlock;
            }
            else
            if ( ( (pBlock->blk.pi.start_time > dbc->pLastPi->blk.pi.start_time) ||
                   ( (pBlock->blk.pi.start_time == dbc->pLastPi->blk.pi.start_time) &&
                     (pBlock->blk.pi.netwop_no > dbc->pLastPi->blk.pi.netwop_no) )) &&
                 ( (pPrev == NULL) ||
                   ( ( (pBlock->blk.pi.block_no_in_ai == FALSE) ||
                       (pPrev->blk.pi.block_no_in_ai == FALSE) ||
                       (EpgDbPiCmpBlockNoGt(dbc, pBlock->blk.pi.block_no, pPrev->blk.pi.block_no, netwop)) ) &&
                     (pBlock->blk.pi.start_time >= pPrev->blk.pi.stop_time) )))
            {  // append the block after the last reloaded one

               // append to network pointer chain
               if (dbc->pFirstNetwopPi[netwop] == NULL)
               {
                  dbc->pFirstNetwopPi[netwop] = pBlock;
                  pBlock->pPrevNetwopBlock = NULL;
               }
               else
               {
                  pPrevNetwop[netwop]->pNextNetwopBlock = pBlock;
                  pBlock->pPrevNetwopBlock = pPrevNetwop[netwop];
               }
               pBlock->pNextNetwopBlock = NULL;
               pPrevNetwop[netwop] = pBlock;

               // append to start time pointer chain
               pBlock->pPrevBlock = dbc->pLastPi;
               pBlock->pNextBlock = NULL;
               dbc->pLastPi->pNextBlock = pBlock;
               dbc->pLastPi = pBlock;
            }
            else
            {  // unexpected block number or start time -> refuse the block (should never happen)
               debug4("EpgDbReload-AddPiBlock: PI not consecutive: prov=0x%04X: netwop=%02d block_no=%d start=%ld", AI_GET_CNI(&dbc->pAiBlock->blk.ai), netwop, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);
               xfree(pBlock);
            }

            //assert(EpgDbCheckChains());
         }
         else
         {  // block expired or removed from stream
            xfree(pBlock);
         }
      }
      else
      {  // invalid netwop or defective stop time: should never happen (defective blocks are filtered before saving)
         //dprintf3("EXPIRED pi netwop=%d, blockno=%d start=%ld\n", pBlock->blk.pi.netwop_no, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);
         xfree(pBlock);
      }
      result = EPGDB_RELOAD_OK;
   }
   else
   {  // PI inconsistant
      xfree(pBlock);
      result = EPGDB_RELOAD_CORRUPT;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Read and verify the file header
//
static EPGDB_RELOAD_RESULT EpgDbReloadHeader( uint cni, int fd, EPGDBSAV_HEADER * pHead, bool * pSwapEndian )
{
   size_t size;
   bool   swapEndian;
   EPGDB_RELOAD_RESULT result;

   size = read(fd, (uchar *)pHead, sizeof(*pHead));
   if (size == sizeof(*pHead))
   {
      if (strncmp(pHead->magic, MAGIC_STR, MAGIC_STR_LEN) == 0)
      {
         if ( (pHead->endianMagic == ENDIAN_MAGIC) ||
              (pHead->endianMagic == WRONG_ENDIAN) )
         {
            if (pHead->endianMagic == WRONG_ENDIAN)
            {
               swapEndian = TRUE;

               swap32(&pHead->compatVersion);
               swap32(&pHead->swVersion);
               swap32(&pHead->lastPiDate);
               swap32(&pHead->firstPiDate);
               swap32(&pHead->lastAiUpdate);
               swap32(&pHead->cni);
               swap32(&pHead->pageNo);
               swap32(&pHead->tunerFreq);
               swap32(&pHead->appId);
            }
            else
               swapEndian = FALSE;

            if (pHead->compatVersion == DUMP_COMPAT)
            {
               if ((cni == RELOAD_ANY_CNI) || (pHead->cni == cni))
               {
                  result = EPGDB_RELOAD_OK;
                  if (pSwapEndian != NULL)
                     *pSwapEndian = swapEndian;
               }
               else
               {
                  debug2("EpgDbReload-Header: reload db 0x%04X: unexpected CNI 0x%04X found", cni, pHead->cni);
                  result = EPGDB_RELOAD_CORRUPT;
               }
            }
            else
            {
               debug2("EpgDbReload-Header: reload db 0x%04X: incompatible version %06x", cni, pHead->compatVersion);
               result = EPGDB_RELOAD_VERSION;
            }
         }
         else
         {  // invalid endian code -> quick check if this is a header of a previous version
            OBSOLETE_EPGDBSAV_HEADER * pObsHead = (OBSOLETE_EPGDBSAV_HEADER *) pHead;
            if ( (pObsHead->dumpVersion >= OBSOLETE_DUMP_MIN_VERSION) &&
                 (pObsHead->dumpVersion <= OBSOLETE_DUMP_MAX_VERSION) &&
                 ((cni == RELOAD_ANY_CNI) || (pObsHead->cni == cni)) )
            {
               debug2("EpgDbReload-Header: reload db 0x%04X: incompatible version %06lx", pObsHead->cni, pObsHead->dumpVersion);
               result = EPGDB_RELOAD_VERSION;
            }
            else
            {
               debug2("EpgDbReload-Header: reload db 0x%04X: illegal endian code 0x%02X found", cni, (int) pHead->endianMagic);
               result = EPGDB_RELOAD_CORRUPT;
            }
         }
      }
      else
      {
         debug1("EpgDbReload-Header: reload db 0x%04X: magic not found", cni);
         result = EPGDB_RELOAD_WRONG_MAGIC;
      }
   }
   else
   {
      debug1("EpgDbReload-Header: reload db 0x%04X: file too short", cni);
      result = EPGDB_RELOAD_WRONG_MAGIC;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Restore the complete Nextview EPG database from a file dump
//
PDBC EpgDbReload( uint cni, EPGDB_RELOAD_RESULT * pResult )
{
   EPGDBSAV_HEADER head;
   EPGDB_CONTEXT * dbc;
   EPGDB_BLOCK *pPrevNetwop[MAX_NETWOP_COUNT];
   EPGDB_BLOCK *pPrevGeneric[BLOCK_TYPE_GENERIC_COUNT];
   EPGDB_BLOCK *pBlock;
   uchar   buffer[BLK_UNION_OFF];
   uint32_t size;
   size_t  readSize;
   int     fd;
   bool    swapEndian;
   uchar * pFilename;
   time_t  piStartOff;
   BLOCK_TYPE type, lastType;
   DB_FMT_TYPE nameFmt;
   EPGDB_RELOAD_RESULT result;

   dbc = EpgDbCreate();

   if (epgDemoDb == NULL)
   {  // append database file name to db directory (from -dbdir argument)
      pFilename = xmalloc(strlen(epgDbDirPath) + 1 + DUMP_NAME_MAX_LEN);
      nameFmt = EpgDbBuildFileName(cni, pFilename);
   }
   else
   {  // demo mode: database file name is taken from command line argument
      // CNI function parameter is ignored
      pFilename = xmalloc(strlen(epgDemoDb) + 1);
      strcpy(pFilename, epgDemoDb);
      cni = RELOAD_ANY_CNI;
      nameFmt = DB_FMT_UNIX;  // dummy, never used
   }

   fd = open(pFilename, O_RDONLY|O_BINARY);
   if (fd >= 0)
   {
      result = EpgDbReloadHeader(cni, fd, &head, &swapEndian);
      if (result == EPGDB_RELOAD_OK)
      {
         dbc->pageNo       = head.pageNo;
         dbc->tunerFreq    = head.tunerFreq;
         dbc->appId        = head.appId;
         dbc->fileNameFormat = nameFmt;
         dbc->expireDelayPi = epgDbReloadExpireDelayPi;

         if (epgDemoDb != NULL)
         {  // demo mode: shift all PI in time to the current time and future
            piStartOff = time(NULL) - head.lastAiUpdate;
            piStartOff -= piStartOff % (60*60L);
            //printf("off=%lu = now=%lu - dump (= %lu=%s)\n", piStartOff, time(NULL), head.lastAiUpdate, ctime(&head.lastAiUpdate));
         }
         else
            piStartOff = 0;

         memset(pPrevNetwop, 0, sizeof(pPrevNetwop));
         memset(pPrevGeneric, 0, sizeof(pPrevGeneric));
         lastType = BLOCK_TYPE_INVALID;
         epgDbReloadCurTime = time(NULL);

         // load the header of the next block from the file (including the block type)
         while ( (result == EPGDB_RELOAD_OK) &&
                 (read(fd, buffer, BLK_UNION_OFF) == BLK_UNION_OFF) )
         {
            size = ((EPGDB_BLOCK *)buffer)->size;
            if (swapEndian)
               swap32(&size);
            // plausibility check for block size (avoid malloc failure, which results in program abort)
            if (size <= EPGDBSAV_MAX_BLOCK_SIZE)
            {
               result = EPGDB_RELOAD_CORRUPT;
               pBlock = (EPGDB_BLOCK *) xmalloc(size + BLK_UNION_OFF);
               memcpy(pBlock, buffer, BLK_UNION_OFF);
               // load the rest of the block from the file
               readSize = read(fd, (uchar *)pBlock + BLK_UNION_OFF, size);
               if (readSize == size)
               {
                  // save the type in temp var in case the block gets freed
                  type = pBlock->type;

                  if (swapEndian)
                  {
                     swap32(&type);
                     if (type == BLOCK_TYPE_DEFECT_PI)
                        pBlock->type = BLOCK_TYPE_PI << 24;
                     EpgBlockSwapEndian(pBlock);
                  }

                  switch (type)
                  {
                     case BLOCK_TYPE_AI:
                        if (lastType == BLOCK_TYPE_INVALID)
                           result = EpgDbReloadAddAiBlock(dbc, pBlock);
                        else
                        {
                           debug0("EpgDb-Reload: unexpected AI block");
                           xfree(pBlock);
                        }
                        break;

                     case BLOCK_TYPE_PI:
                        // in demo mode, add time offset to shift PI into present or future
                        ((PI_BLOCK*)&pBlock->blk.pi)->start_time += piStartOff;
                        ((PI_BLOCK*)&pBlock->blk.pi)->stop_time  += piStartOff;
                        result = EpgDbReloadAddPiBlock(dbc, pBlock, pPrevNetwop);
                        break;

                     case BLOCK_TYPE_DEFECT_PI:
                        // convert type to normal PI, since the "defect pi" type is known only inside this module
                        pBlock->type = BLOCK_TYPE_PI;
                        result = EpgDbReloadAddDefectPiBlock(dbc, pBlock);
                        break;

                     case BLOCK_TYPE_NI:
                     case BLOCK_TYPE_OI:
                     case BLOCK_TYPE_MI:
                     case BLOCK_TYPE_LI:
                     case BLOCK_TYPE_TI:
                        if ((lastType < BLOCK_TYPE_GENERIC_COUNT) || (lastType == BLOCK_TYPE_AI))
                           result = EpgDbReloadAddGenericBlock(dbc, pBlock, pPrevGeneric);
                        else
                        {
                           debug1("EpgDb-Reload: unexpected generic block, type %d", pBlock->type);
                           xfree(pBlock);
                        }
                        break;

                     default:
                        debug1("EpgDb-Reload: unknown or illegal block type 0x%x", pBlock->type);
                        xfree(pBlock);
                        break;
                  }
                  lastType = type;
               }
               else
               {
                  debug2("EpgDb-Reload: block read error: want %d, got %d", pBlock->size, size);
                  xfree(pBlock);
               }
            }
            else
            {
               debug1("EpgDb-Reload: illegal block size: %u", size);
               result = EPGDB_RELOAD_CORRUPT;
            }
         }
         assert(EpgDbCheckChains(dbc));

         // check if the database contains at least an AI block
         if ((dbc->pAiBlock == NULL) && (result == EPGDB_RELOAD_OK))
         {
            debug1("EpgDb-Reload: provider 0x%04X: no AI block in db", cni);
            result = EPGDB_RELOAD_CORRUPT;
         }
      }
      ifdebug2((result == EPGDB_RELOAD_CORRUPT), "EpgDb-Reload: db %04X corrupt at file offset %ld", cni, (long)lseek(fd, 0, SEEK_CUR));
      close(fd);
   }
   else
   {
      dprintf1("EpgDb-Reload: db of requested provider %04X not found\n", cni);
      if (errno == EACCES)
         result = EPGDB_RELOAD_ACCESS;
      else
         result = EPGDB_RELOAD_EXIST;
   }
   xfree(pFilename);

   // upon any errors, destroy the context
   if (result != EPGDB_RELOAD_OK)
   {
      EpgDbDestroy(dbc, FALSE);
      dbc = NULL;
   }

   // return the error code, if requested
   if (pResult != NULL)
   {
      *pResult = result;
   }

   return dbc;
}

// ---------------------------------------------------------------------------
// Scan database file for the OI block #0
// - returns a pointer to the block (the caller must free the memory)
//
static EPGDB_BLOCK * EpgDbPeekOi( int fd, bool swapEndian )
{
   EPGDB_BLOCK * pBlock;
   uchar buffer[BLK_UNION_OFF];
   BLOCK_TYPE  type;
   size_t      readSize;
   uint32_t    size;

   pBlock = NULL;

   while (read(fd, buffer, BLK_UNION_OFF) == BLK_UNION_OFF)
   {
      size = ((EPGDB_BLOCK *)buffer)->size;
      if (swapEndian)
         swap32(&size);
      // plausibility check for block size (avoid malloc failure, which reults in program abort)
      if (size <= EPGDBSAV_MAX_BLOCK_SIZE)
      {
         type = ((EPGDB_BLOCK *)buffer)->type;
         if (swapEndian)
            swap32(&type);

         if (type == BLOCK_TYPE_OI)
         {
            pBlock = (EPGDB_BLOCK *) xmalloc(size + BLK_UNION_OFF);
            memcpy(pBlock, buffer, BLK_UNION_OFF);
            // load the rest of the block from the file
            readSize = read(fd, (uchar *)pBlock + BLK_UNION_OFF, size);
            if (readSize == size)
            {
               if (swapEndian)
                  EpgBlockSwapEndian(pBlock);

               if (pBlock->blk.oi.block_no == 0)
               {  // found the block -> check it's consistancy
                  pBlock->pNextBlock = NULL;
                  if (EpgBlockCheckConsistancy(pBlock) == FALSE)
                  {
                     xfree(pBlock);
                     pBlock = NULL;
                  }
               }
               else
               {  // OI block, but not #0 -> finished (blocks are sorted by increasing blockno)
                  xfree(pBlock);
                  pBlock = NULL;
               }

               // done: return the found block or NULL
               break;
            }
            else
            {
               debug2("EpgDb-PeekOi: block read error: want %d, got %d", size, readSize);
               xfree(pBlock);
               pBlock = NULL;
               break;
            }
         }
         else if ( ((EPGDB_BLOCK *)buffer)->type >= BLOCK_TYPE_PI )
         {  // read past all generic blocks -> finished
            break;
         }
         else
         {  // skip the block in the file
            if (lseek(fd, size, SEEK_CUR) == -1)
            {
               debug1("EpgDb-PeekOi: lseek +%ld failed - abort search", (ulong)size);
               break;
            }
         }
      }
      else
      {
         debug1("EpgDb-PeekOi: illegal block size: %u", size);
         break;
      }
   }
   return pBlock;
}

// ---------------------------------------------------------------------------
// Peek inside a dumped database: retrieve provider info
//
EPGDB_CONTEXT * EpgDbPeek( uint cni, EPGDB_RELOAD_RESULT * pResult )
{
   EPGDBSAV_HEADER head;
   EPGDB_CONTEXT * pDbContext;
   EPGDB_BLOCK   * pBlock;
   uchar buffer[BLK_UNION_OFF];
   uint32_t size;
   size_t  readSize;
   int     fd;
   bool    swapEndian;
   uchar * pFilename;
   DB_FMT_TYPE nameFmt;
   EPGDB_RELOAD_RESULT result;

   pDbContext = NULL;
   pFilename = xmalloc(strlen(epgDbDirPath) + 1 + DUMP_NAME_MAX_LEN);
   nameFmt = EpgDbBuildFileName(cni, pFilename);
   fd = open(pFilename, O_RDONLY|O_BINARY);
   if (fd >= 0)
   {
      pDbContext = xmalloc(sizeof(EPGDB_CONTEXT));
      memset(pDbContext, 0, sizeof(EPGDB_CONTEXT));

      result = EpgDbReloadHeader(cni, fd, &head, &swapEndian);
      if (result == EPGDB_RELOAD_OK)
      {
         pDbContext->pageNo       = head.pageNo;
         pDbContext->tunerFreq    = head.tunerFreq;
         pDbContext->appId        = head.appId;
         pDbContext->fileNameFormat = nameFmt;
         pDbContext->expireDelayPi = epgDbReloadExpireDelayPi;

         if (read(fd, buffer, BLK_UNION_OFF) == BLK_UNION_OFF)
         {
            size = ((EPGDB_BLOCK *)buffer)->size;
            if (swapEndian)
               swap32(&size);
            // plausibility check for block size (avoid malloc failure, which reults in program abort)
            if (size <= EPGDBSAV_MAX_BLOCK_SIZE)
            {
               pBlock = (EPGDB_BLOCK *) xmalloc(size + BLK_UNION_OFF);
               memcpy(pBlock, buffer, BLK_UNION_OFF);
               // load the rest of the block from the file
               readSize = read(fd, (uchar *)pBlock + BLK_UNION_OFF, size);
               if (readSize == size)
               {
                  if (swapEndian)
                     EpgBlockSwapEndian(pBlock);

                  // check if it's a valid AI block (the first block in the dumped db must be AI)
                  if ( (pBlock->type == BLOCK_TYPE_AI) &&
                       EpgBlockCheckConsistancy(pBlock) )
                  {
                     pDbContext->pAiBlock = pBlock;
                     pDbContext->pFirstGenericBlock[BLOCK_TYPE_OI] = EpgDbPeekOi(fd, swapEndian);
                  }
                  else
                  {
                     debug0("EpgDb-Peek: no AI block found");
                     result = EPGDB_RELOAD_CORRUPT;
                     xfree(pBlock);
                  }
               }
               else
               {
                  debug2("EpgDb-Peek: block read error: want %d, got %d", size, readSize);
                  xfree(pBlock);
               }
            }
            else
            {
               debug1("EpgDb-Peek: illegal block size: %u", size);
               result = EPGDB_RELOAD_CORRUPT;
            }
         }
         else
         {
            debug0("EpgDb-Peek: file too short");
            result = EPGDB_RELOAD_CORRUPT;
         }
      }
      close(fd);

      if (pDbContext->pAiBlock == NULL)
      {  // database corrupt or wrong version -> free context
         xfree(pDbContext);
         pDbContext = NULL;
      }
   }
   else
   {
      dprintf1("EpgDb-Peek: db of requested provider %04X not found\n", cni);
      if (errno == EACCES)
         result = EPGDB_RELOAD_ACCESS;
      else
         result = EPGDB_RELOAD_EXIST;
   }

   xfree(pFilename);

   if (pResult != NULL)
   {
      *pResult = result;
   }

   return pDbContext;
}

// ----------------------------------------------------------------------------
// Append one element to the dynamically growing output buffer
// - helper function for the dbdir scan
//
static EPGDB_SCAN_BUF * EpgDbReloadScanAddCni( EPGDB_SCAN_BUF * pBuf,
                                               uint cni, time_t mtime, DB_FMT_TYPE nameFormat )
{
   EPGDB_SCAN_BUF  * pNewBuf;
   uint   idx;

   for (idx = 0; idx < pBuf->count; idx++)
   {
      if (pBuf->list[idx].cni == cni)
      {
         fprintf(stderr, "WARNING: found two database files for provider %04X in %s\n", cni, epgDbDirPath);

         if (mtime > pBuf->list[idx].mtime)
            pBuf->list[idx].nameFormat = nameFormat;
         break;
      }
   }
   // don't add element if same CNI already in list
   if (idx >= pBuf->count)
   {
      if (pBuf->count >= pBuf->size)
      {  // buffer is full -> allocate larger one and copy the old content into it
         assert(pBuf->count == pBuf->size);

         pNewBuf = xmalloc(EPGDB_SCAN_BUFSIZE(pBuf->size + EPGDB_SCAN_BUFSIZE_INCREMENT));
         memcpy(pNewBuf, pBuf, EPGDB_SCAN_BUFSIZE(pBuf->size));
         xfree(pBuf);
         pBuf = pNewBuf;

         pBuf->size  += EPGDB_SCAN_BUFSIZE_INCREMENT;
      }

      // append the new element
      pBuf->list[pBuf->count].cni   = cni;
      pBuf->list[pBuf->count].mtime = mtime;
      pBuf->list[pBuf->count].nameFormat = nameFormat;
      pBuf->count += 1;
   }
   return pBuf;
}

// ---------------------------------------------------------------------------
// Scans a given directory EPG databases
// - returns dynamically allocated struct which has to be freed by the caller
// - the struct holds a list which contains for each database the CNI
//   (extracted from the file name) and the file modification timestamp
//
const EPGDB_SCAN_BUF * EpgDbReloadScan( void )
{
#ifndef WIN32
   DIR    *dir;
   struct dirent *entry;
   struct stat st;
   uint   cni;
   int    scanLen;
   char   *pFilePath;
   DB_FMT_TYPE fileFmt;
   EPGDB_SCAN_BUF  * pBuf;

   pBuf = xmalloc(EPGDB_SCAN_BUFSIZE(EPGDB_SCAN_BUFSIZE_DEFAULT));
   pBuf->size  = EPGDB_SCAN_BUFSIZE_DEFAULT;
   pBuf->count = 0;

   dir = opendir(epgDbDirPath);
   if (dir != NULL)
   {
      while ((entry = readdir(dir)) != NULL)
      {
         if      ( (strlen(entry->d_name) == DUMP_NAME_LEN_UNIX) &&
                   (sscanf(entry->d_name, DUMP_NAME_EXP_UNIX "%n", &cni, &scanLen) == 1) &&
                   (scanLen == DUMP_NAME_LEN_UNIX) )
         {
            fileFmt = DB_FMT_UNIX;
         }
         else if ( (strlen(entry->d_name) == DUMP_NAME_LEN_DOS) &&
                   (sscanf(entry->d_name, DUMP_NAME_EXP_DOS "%n", &cni, &scanLen) == 1) &&
                   (scanLen == DUMP_NAME_LEN_DOS) )
         {
            fileFmt = DB_FMT_DOS;
         }
         else
            fileFmt = DB_FMT_COUNT;

         if (fileFmt < DB_FMT_COUNT)
         {  // file name matched (complete match is forced by %n)
            pFilePath = xmalloc(strlen(epgDbDirPath) + 1 + strlen(entry->d_name) + 1);
            sprintf(pFilePath, "%s" PATH_SEPARATOR_STR "%s", epgDbDirPath, entry->d_name);

            // get file status
            if (lstat(pFilePath, &st) == 0)
            {
               // check if it's a regular file; reject directories, devices, pipes etc.
               if (S_ISREG(st.st_mode))
                  pBuf = EpgDbReloadScanAddCni(pBuf, cni, st.st_mtime, fileFmt);
               else
                  fprintf(stderr, "dbdir scan: not a regular file: %s (skipped)\n", pFilePath);
            }
            else
               fprintf(stderr, "dbdir scan: failed stat %s: %s\n", pFilePath, strerror(errno));

            xfree(pFilePath);
         }
      }
      closedir(dir);
   }
   else
      fprintf(stderr, "failed to open dbdir %s: %s\n", epgDbDirPath, strerror(errno));

   return pBuf;

#else  //WIN32
   HANDLE hFind;
   WIN32_FIND_DATA finddata;
   SYSTEMTIME  systime;
   uint cni;
   uchar *pDirPath;
   struct tm tm;
   bool bMore;
   DB_FMT_TYPE fileFmt;
   EPGDB_SCAN_BUF  * pBuf;

   pBuf = xmalloc(EPGDB_SCAN_BUFSIZE(EPGDB_SCAN_BUFSIZE_DEFAULT));
   pBuf->size  = EPGDB_SCAN_BUFSIZE_DEFAULT;
   pBuf->count = 0;

   pDirPath = xmalloc(strlen(epgDbDirPath) + 1 + DUMP_NAME_MAX_LEN);
   for (fileFmt = 0; fileFmt < DB_FMT_COUNT; fileFmt++)
   {
      if (fileFmt == DB_FMT_UNIX)
         sprintf(pDirPath, "%s\\%s", epgDbDirPath, DUMP_NAME_PAT_UNIX);
      else
         sprintf(pDirPath, "%s\\%s", epgDbDirPath, DUMP_NAME_PAT_DOS);

      hFind = FindFirstFile(pDirPath, &finddata);
      bMore = (hFind != (HANDLE) -1);
      while (bMore)
      {
         if ( (strlen(finddata.cFileName) ==
                      ((fileFmt == DB_FMT_UNIX) ? DUMP_NAME_LEN_UNIX : DUMP_NAME_LEN_DOS)) &&
              (sscanf(finddata.cFileName,
                      ((fileFmt == DB_FMT_UNIX) ? DUMP_NAME_EXP_UNIX : DUMP_NAME_EXP_DOS),
                      &cni) == 1) )
         {  // file name matched
            FileTimeToSystemTime(&finddata.ftLastWriteTime, &systime);
            dprintf7("DB 0x%04X:  %02d:%02d:%02d - %02d.%02d.%04d\n", cni, systime.wHour, systime.wMinute, systime.wSecond, systime.wDay, systime.wMonth, systime.wYear);
            memset(&tm, 0, sizeof(tm));
            tm.tm_sec   = systime.wSecond;
            tm.tm_min   = systime.wMinute;
            tm.tm_hour  = systime.wHour;
            tm.tm_mday  = systime.wDay;
            tm.tm_mon   = systime.wMonth - 1;
            tm.tm_year  = systime.wYear - 1900;

            pBuf = EpgDbReloadScanAddCni(pBuf, cni, mktime(&tm), fileFmt);
         }
         bMore = FindNextFile(hFind, &finddata);
      }
      FindClose(hFind);
   }
   xfree(pDirPath);

   return pBuf;
#endif  //WIN32
}

// ---------------------------------------------------------------------------
// Initialize expire time for reloading PI
//
void EpgDbSavSetPiExpireDelay( time_t expireDelayPi )
{
   epgDbReloadExpireDelayPi = expireDelayPi;
}

// ---------------------------------------------------------------------------
// Create database directory, if neccessary
//
bool EpgDbSavSetupDir( const char * pDirPath, const char * pDemoDb )
{
   bool result = TRUE;

#ifndef WIN32
   struct stat st;

   epgDbDirPath = pDirPath;
   if (pDemoDb != NULL)
   {
      epgDemoDb = pDemoDb;
   }
   else if (pDirPath != NULL)
   {
      if (stat(pDirPath, &st) != 0)
      {  // directory does not exist -> create it
         if ((errno != ENOENT) || (mkdir(pDirPath, 0777) != 0))
         {
            fprintf(stderr, "cannot create database dir %s: %s\n", pDirPath, strerror(errno));
            result = FALSE;
         }
      }
      else if (S_ISDIR(st.st_mode) == FALSE)
      {  // target is not a directory -> warn
         fprintf(stderr, "database path '%s' exists but is not a directory\n", pDirPath);
         result = FALSE;
      }
   }

#else  //WIN32
   DWORD attr;
   DWORD errCode;
   char  * pError = NULL;

   epgDbDirPath = pDirPath;
   if (pDemoDb != NULL)
   {
      epgDemoDb = pDemoDb;
   }
   else if (pDirPath != NULL)
   {
      attr = GetFileAttributes(pDirPath);
      if (attr == INVALID_FILE_ATTRIBUTES)
      {
         errCode = GetLastError();
         debug1("EpgDb-SavSetupDir: dbdir error %ld", errCode);

         if (errCode == ERROR_FILE_NOT_FOUND)
         {
            // directory does no exist -> create it
            if (CreateDirectory(epgDbDirPath, NULL) == 0)
            {  // creation failed -> warn
               SystemErrorMessage_Set(&pError, errCode, "Check option -dbdir ", pDirPath,
                                      "\nPath does not exist and cannot be created: ", NULL);
               if (pError != NULL)
               {
                  MessageBox(NULL, pError, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
                  xfree(pError);
               }
               result = FALSE;
            }
         }
         else
         {
            errCode = GetLastError();
            SystemErrorMessage_Set(&pError, errCode, "Cannot access database directory:\n",
                                                     pDirPath, "\n", NULL);
            if (pError != NULL)
            {
               MessageBox(NULL, pError, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
               xfree(pError);
            }
            result = FALSE;
         }
      }
      else if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
      {  // target not a directory -> warn
         SystemErrorMessage_Set(&pError, 0, "Invalid database directory specified:\n",
                                pDirPath, "\nAlready exists, but is not a directory", NULL);
         if (pError != NULL)
         {
            MessageBox(NULL, pError, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            xfree(pError);
         }
         result = FALSE;
      }
   }
#endif

   return result;
}

// ---------------------------------------------------------------------------
// Determine dbdir and provider CNI from database file command line argument
//
void EpgDbDumpGetDirAndCniFromArg( char * pArg, const char ** ppDirPath, uint * pCni )
{
   char *pDirPath, *pNamePath;
   int len, scanLen;

   assert((ppDirPath != NULL) && (pCni != NULL));

   len = strlen(pArg);
   if (len > 0)
   {
      // search for the begin of the database file name
      pDirPath = NULL;
      pNamePath = pArg;
      while (--len >= 0)
      {
         if (pArg[len] == PATH_SEPARATOR)
         {
            pDirPath = pArg;
            pArg[len] = 0;
            pNamePath = pArg + len + 1;
            break;
         }
      }

      if (pDirPath == NULL)
      {  // no path separator found -> use current working directory
         pDirPath = ".";
         len = 0;
      }
      else if (len == 0)
      {
         pDirPath = PATH_ROOT;
      }

      // retrieve the hex CNI from the file name
      // %n is required to check for garbage behind the CNI
      if ( ( (strlen(pNamePath) == DUMP_NAME_LEN_UNIX) &&
             (sscanf(pNamePath, DUMP_NAME_EXP_UNIX "%n", pCni, &scanLen) == 1) &&
             (scanLen == DUMP_NAME_LEN_UNIX) ) ||
           ( (strlen(pNamePath) == DUMP_NAME_LEN_DOS) &&
             (sscanf(pNamePath, DUMP_NAME_EXP_DOS "%n", pCni, &scanLen) == 1) &&
             (scanLen == DUMP_NAME_LEN_DOS) ) )
      {  // this seems to be a regular database file -> set dbdir path
         *ppDirPath = (const char *) pDirPath;
      }
      else
      {  // not a valid database file format -> assume demo database
         // repair path name
         if (len > 0)
            pDirPath[len] = PATH_SEPARATOR;
         *pCni = 0;
      }
   }
   else
   {  // empty argument -> dummy return values
      *pCni = 0;
   }
}

// ---------------------------------------------------------------------------
// Update the tuner frequency in the dump file header
//
bool EpgDbDumpUpdateHeader( uint cni, uint freq )
{
   EPGDBSAV_HEADER head;
   size_t   size;
   uchar  * pFilename;
   int      fd;
   uint32_t headCompatVersion;
   uint32_t headCni;
   bool     result = FALSE;

   pFilename = xmalloc(strlen(epgDbDirPath) + 1 + DUMP_NAME_MAX_LEN);
   EpgDbBuildFileName(cni, pFilename);
   fd = open(pFilename, O_RDWR|O_BINARY);
   if (fd >= 0)
   {
      size = read(fd, (uchar *)&head, sizeof(head));
      if (size == sizeof(head))
      {
         if ( (strncmp(head.magic, MAGIC_STR, MAGIC_STR_LEN) == 0) &&
              ( (head.endianMagic == ENDIAN_MAGIC) ||
                (head.endianMagic == WRONG_ENDIAN)) )
         {
            headCompatVersion = head.compatVersion;
            headCni           = head.cni;

            if (head.endianMagic == WRONG_ENDIAN)
            {
               swap32(&headCompatVersion);
               swap32(&headCni);
            }

            if ( (headCompatVersion == DUMP_COMPAT) &&
                 (headCni == cni) )
            {
               head.tunerFreq = freq;

               if (head.endianMagic == WRONG_ENDIAN)
                  swap32(&head.tunerFreq);

               // rewind to the start of file and overwrite header
               if (lseek(fd, 0, SEEK_SET) == 0)
               {
                  size = write(fd, &head, sizeof(head));
                  if (size == sizeof(head))
                  {
                     result = TRUE;
                  }
                  else
                     debug2("EpgDbDump-UpdateHeader: write failed with %d, errno %d\n", size, errno);
               }
               else
                  debug1("EpgDbDump-UpdateHeader: seek failed with errno %d", errno);
            }
            else
               debug2("EpgDbDump-UpdateHeader: invalid CNI 0x%04X in db, != 0x%04X", head.cni, cni);
         }
         else
            debug0("EpgDbDump-UpdateHeader: invalid magic in db");
      }
      close(fd);
   }
   xfree(pFilename);
   return result;
}

// ---------------------------------------------------------------------------
// Read AI modification time from a database header
//
time_t EpgReadAiUpdateTime( uint cni )
{
   EPGDBSAV_HEADER head;
   size_t  size;
   uchar * pFilename;
   int     fd;
   time_t  aiUpdateTime = (time_t) 0;

   pFilename = xmalloc(strlen(epgDbDirPath) + 1 + DUMP_NAME_MAX_LEN);
   EpgDbBuildFileName(cni, pFilename);
   fd = open(pFilename, O_RDONLY|O_BINARY);
   if (fd >= 0)
   {
      size = read(fd, (uchar *)&head, sizeof(head));
      if (size == sizeof(head))
      {
         if ( (strncmp(head.magic, MAGIC_STR, MAGIC_STR_LEN) == 0) &&
              ( (head.endianMagic == ENDIAN_MAGIC) ||
                (head.endianMagic == WRONG_ENDIAN)) )
         {
            if (head.endianMagic == WRONG_ENDIAN)
            {
               swap32(&head.compatVersion);
               swap32(&head.cni);
               swap32(&head.lastAiUpdate);
            }

            if ( (head.compatVersion == DUMP_COMPAT) &&
                 (head.cni == cni) )
            {
               aiUpdateTime = head.lastAiUpdate;
            }
         }
      }
      close(fd);
   }

   xfree(pFilename);
   return aiUpdateTime;
}

// ---------------------------------------------------------------------------
// Reads tuner frequency from a database header, even for incompatible db versions
//
uint EpgDbReadFreqFromDefective( uint cni )
{
   EPGDBSAV_HEADER head;
   size_t  size;
   uchar * pFilename;
   int     fd;
   uint    tunerFreq = 0;

   pFilename = xmalloc(strlen(epgDbDirPath) + 1 + DUMP_NAME_MAX_LEN);
   EpgDbBuildFileName(cni, pFilename);
   fd = open(pFilename, O_RDONLY|O_BINARY);
   if (fd >= 0)
   {
      size = read(fd, (uchar *)&head, sizeof(head));
      if (size == sizeof(head))
      {
         if ( (strncmp(head.magic, MAGIC_STR, MAGIC_STR_LEN) == 0) &&
              ( (head.endianMagic == ENDIAN_MAGIC) ||
                (head.endianMagic == WRONG_ENDIAN)) )
         {
            if (head.endianMagic == WRONG_ENDIAN)
            {
               swap32(&head.cni);
               swap32(&head.tunerFreq);
            }

            if (head.cni == cni)
            {
               tunerFreq = head.tunerFreq;
            }
         }
      }
      close(fd);
   }

   xfree(pFilename);
   return tunerFreq;
}

// ---------------------------------------------------------------------------
// Remove a provider database
// - returns 0 on success, else an POSIX error code
//
uint EpgDbRemoveDatabaseFile( uint cni )
{
   uchar * pFilename;
   uint result;

   result = 0;

   pFilename = xmalloc(strlen(epgDbDirPath) + 1 + DUMP_NAME_MAX_LEN);

   sprintf(pFilename, "%s/" DUMP_NAME_FMT_UNIX, epgDbDirPath, cni);
   if ( (unlink(pFilename) != 0) && (access(pFilename, F_OK) != 0) )
   {
      debug2("EpgDb-RemoveDatabaseFile: failed to remove db file '%s': %s", pFilename, strerror(errno));
      result = errno;
   }

   sprintf(pFilename, "%s/" DUMP_NAME_FMT_DOS, epgDbDirPath, cni);
   if ( (unlink(pFilename) != 0) && (access(pFilename, F_OK) != 0) )
   {
      debug2("EpgDb-RemoveDatabaseFile: failed to remove db file '%s': %s", pFilename, strerror(errno));
      result = errno;
   }

   xfree(pFilename);

   return result;
}

