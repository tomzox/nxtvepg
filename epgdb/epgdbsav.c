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
 *    Saves the EPG database into a (binary) file. This is simply
 *    done by dumping all raw blocks consecutively. The reload
 *    function reverses these actions; expired PI blocks are
 *    skipped though. Only the EPG data structures are saved;
 *    all pointer chains are restored during reload. This is
 *    simple (compared to the normal insertion after acquisition),
 *    because the blocks are already checked and sorted.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgdbsav.c,v 1.30 2001/01/08 20:43:50 tom Exp tom $
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

#include "epgctl/epgmain.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbacq.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"



//internal shortcuts
typedef const EPGDB_CONTEXT * CPDBC;
typedef       EPGDB_CONTEXT * PDBC;


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
      header.dumpDate = time(NULL);

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
            perror("write header");
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
         perror("write dump");
         return FALSE;
      }
      p += size;
      rest -= size;
   }
   while (rest > 0);

   return TRUE;
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
   bool result = FALSE;

   EpgDbLockDatabase(dbc, TRUE);
   if ((dbc->pAiBlock != NULL) && (dbc->modified))
   {
      pFilename = xmalloc(strlen(dbdir) + 1 + DUMP_NAME_MAX);
      sprintf(pFilename, "%s/" DUMP_NAME_FMT, dbdir, AI_GET_NETWOP_N(&dbc->pAiBlock->blk.ai, dbc->pAiBlock->blk.ai.thisNetwop)->cni);
      #ifdef DUMP_NAME_TMP
      pTmpname = xmalloc(strlen(dbdir) + 1 + DUMP_NAME_MAX);
      strcpy(pTmpname, pFilename);
      strcat(pFilename, DUMP_NAME_TMP);
      #endif
      fd = open(pFilename, O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, 0666);
      if (fd >= 0)
      {
         chmod(pFilename, 0666);

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
         perror("EpgDb-Dump: open/create");

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
// Check reloaded AI block for gross consistancy errors
//
static EPGDB_RELOAD_RESULT EpgDbReloadCheckAiBlock( EPGDB_BLOCK * pBlock )
{
   EPGDB_RELOAD_RESULT result = EPGDB_RELOAD_CORRUPT;
   const AI_NETWOP * pNetwop;
   const uchar *pBlockEnd, *pName;
   uchar netwop;

   pBlockEnd = (uchar *) pBlock + pBlock->size + BLK_UNION_OFF;

   if ((pBlock->blk.ai.netwopCount == 0) || (pBlock->blk.ai.netwopCount > MAX_NETWOP_COUNT))
   {
      debug1("EpgDbReload-CheckAiBlock: illegal netwop count %d", pBlock->blk.ai.netwopCount);
   }
   else if (pBlock->blk.ai.thisNetwop >= pBlock->blk.ai.netwopCount)
   {
      debug2("EpgDbReload-CheckAiBlock: this netwop %d >= count %d", pBlock->blk.ai.thisNetwop, pBlock->blk.ai.netwopCount);
   }
   else if ((uchar *)AI_GET_NETWOP_N(&pBlock->blk.ai, pBlock->blk.ai.netwopCount) > pBlockEnd)
   {
      debug1("EpgDbReload-CheckAiBlock: netwop count %d exceeds block length", pBlock->blk.ai.netwopCount);
   }
   else if ( (AI_GET_SERVICENAME(&pBlock->blk.ai) >= pBlockEnd) ||
             (AI_GET_SERVICENAME(&pBlock->blk.ai) + strlen(AI_GET_SERVICENAME(&pBlock->blk.ai)) + 1 > pBlockEnd) )
   {
      debug0("EpgDbReload-CheckAiBlock: service name exceeds block size");
   }
   else
   {
      result = EPGDB_RELOAD_OK;

      // check the name string offsets the netwop array
      pNetwop = AI_GET_NETWOPS(&pBlock->blk.ai);
      for (netwop=0; netwop < pBlock->blk.ai.netwopCount; netwop++, pNetwop++)
      {
         pName = AI_GET_STR_BY_OFF(&pBlock->blk.ai, pNetwop->off_name);
         if ( (pName >= pBlockEnd) ||
              (pName + strlen(pName) + 1 > pBlockEnd) )
         {
            debug1("EpgDbReload-CheckAiBlock: netwop name #%d exceeds block size", netwop);
            result = EPGDB_RELOAD_CORRUPT;
            break;
         }
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Add a loaded AI block to the database
//
static EPGDB_RELOAD_RESULT EpgDbReloadAddAiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   EPGDB_RELOAD_RESULT result;

   result = EpgDbReloadCheckAiBlock(pBlock);
   if (result == EPGDB_RELOAD_OK)
   {
      if (dbc->pAiBlock == NULL)
      {
         pBlock->pNextBlock       = NULL;
         pBlock->pPrevBlock       = NULL;
         pBlock->pNextNetwopBlock = NULL;
         pBlock->pPrevNetwopBlock = NULL;

         dbc->pAiBlock = pBlock;
         result = EPGDB_RELOAD_OK;
      }
      else
      {
         debug2("EpgDbReload-AddAiBlock: prov=0x%04X: AI block already exists - new CNI 0x%04X", AI_GET_CNI(&dbc->pAiBlock->blk.ai), AI_GET_CNI(&pBlock->blk.ai));
         xfree(pBlock);
      }
   }
   else
      xfree(pBlock);

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
   EPGDB_RELOAD_RESULT result = EPGDB_RELOAD_CORRUPT;

   if (pBlock->type < BLOCK_TYPE_GENERIC_COUNT)
   {
      block_no = pBlock->blk.all.block_no;
      blockCount = EpgDbGetGenericMaxCount(dbc, pBlock->type);
      if ( (block_no < blockCount) ||
           ((block_no == 0x8000) && ((pBlock->type == BLOCK_TYPE_LI) || (pBlock->type == BLOCK_TYPE_TI))) )
      {
         if (dbc->pFirstGenericBlock[pBlock->type] == NULL)
         {  // allererster Block dieses Typs
            //dprintf3("RELOAD first GENERIC type=%d ptr=%lx: blockno=%d\n", type, (ulong)pBlock, block_no);
            pBlock->pNextBlock = NULL;
            dbc->pFirstGenericBlock[pBlock->type] = pBlock;
            pPrevGeneric[pBlock->type] = pBlock;
            result = EPGDB_RELOAD_OK;
         }
         else
         {
            pPrev = pPrevGeneric[pBlock->type];
            assert(pPrev != NULL);

            if ( pPrev->blk.all.block_no < block_no)
            {  // Block passt in Sortierung -> ersetzen
               //dprintf3("RELOAD GENERIC type=%d ptr=%lx: blockno=%d\n", type, (ulong)pBlock, block_no);
               pBlock->pNextBlock = NULL;
               pPrev->pNextBlock = pBlock;
               pPrevGeneric[pBlock->type] = pBlock;
               result = EPGDB_RELOAD_OK;
            }
            else
            {  // Blocknummer nicht groesser als die des letzten Blocks
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
   }
   else
   {
      debug3("EpgDbReload-AddGenericBlock: prov=0x%04X: invalid type %d or block_no %d", AI_GET_CNI(&dbc->pAiBlock->blk.ai), pBlock->type, pBlock->blk.all.block_no);
      xfree(pBlock);
   }
   return result;
}

// ---------------------------------------------------------------------------
// Check reloaded PI block for gross consistancy errors
//
static EPGDB_RELOAD_RESULT EpgDbReloadCheckPiBlock( EPGDB_BLOCK * pBlock )
{
   EPGDB_RELOAD_RESULT result = EPGDB_RELOAD_CORRUPT;
   const PI_BLOCK * pPi = &pBlock->blk.pi;
   uchar *pBlockEnd;

   pBlockEnd = (uchar *) pBlock + pBlock->size + BLK_UNION_OFF;

   if (pPi->no_themes > PI_MAX_THEME_COUNT)
   {
      debug1("EpgDbReload-CheckPiBlock: illegal theme count %d", pPi->no_themes);
   }
   else if (pPi->no_sortcrit > PI_MAX_SORTCRIT_COUNT)
   {
      debug1("EpgDbReload-CheckPiBlock: illegal sortcrit count %d", pPi->no_sortcrit);
   }
   else if (pPi->off_title == 0)
   {
      debug0("EpgDbReload-CheckPiBlock: has no title");
   }
   #if 0
   // these tests are too time consuming and the other tests should be safe enough
   else if ( PI_HAS_SHORT_INFO(pPi) &&
             ( (pStr = PI_GET_SHORT_INFO(pPi) >= pBlockEnd) ||
               (pStr + strlen(pStr) + 1 > pBlockEnd) ))
   {
      debug0("EpgDbReload-CheckPiBlock: short info exceeds block size");
   }
   else if ( PI_HAS_LONG_INFO(pPi) &&
             ( (pStr = PI_GET_LONG_INFO(pPi) >= pBlockEnd) ||
               (pStr + strlen(pStr) + 1 > pBlockEnd) ))
   {
      debug0("EpgDbReload-CheckPiBlock: short info exceeds block size");
   }
   #endif
   else if ( (pPi->no_descriptors > 0) &&
             ((uchar *)&PI_GET_DESCRIPTORS(pPi)[pPi->no_descriptors] > pBlockEnd) )
   {
      debug1("EpgDbReload-CheckPiBlock: descriptor count %d exceeds block length", pPi->no_descriptors);
   }
   else
      result = EPGDB_RELOAD_OK;

   return result;
}

// ---------------------------------------------------------------------------
// Add an expired or defect PI block to the database
//
static EPGDB_RELOAD_RESULT EpgDbReloadAddDefectPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   EPGDB_RELOAD_RESULT result;

   result = EpgDbReloadCheckPiBlock(pBlock);
   if (result == EPGDB_RELOAD_OK)
   {
      if (pBlock->version == ((pBlock->stream == 0) ? dbc->pAiBlock->blk.ai.version : dbc->pAiBlock->blk.ai.version_swo))
      {
         if ( EpgDbPiBlockNoValid(dbc, pBlock->blk.pi.block_no, pBlock->blk.pi.netwop_no) )
         {
            // convert type to normal PI, since the "defect pi" type is known only inside this module
            pBlock->type = BLOCK_TYPE_PI;
            // prepend to list of defect blocks
            pBlock->pNextBlock = dbc->pObsoletePi;
            dbc->pObsoletePi = pBlock;
            result = EPGDB_RELOAD_OK;
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
         result = EPGDB_RELOAD_OK;
      }
   }
   else
      xfree(pBlock);

   return result;
}

// ---------------------------------------------------------------------------
// Add a PI block to the database
//
static EPGDB_RELOAD_RESULT EpgDbReloadAddPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK **pPrevNetwop )
{
   EPGDB_BLOCK *pPrev;
   uchar netwop;
   time_t now;
   EPGDB_RELOAD_RESULT result;
   
   result = EpgDbReloadCheckPiBlock(pBlock);
   if (result == EPGDB_RELOAD_OK)
   {
      netwop = pBlock->blk.pi.netwop_no;
      if ( EpgDbPiBlockNoValid(dbc, pBlock->blk.pi.block_no, netwop) )
      {
         now = time(NULL);

         //if ( pBlock->blk.pi.start_time >= now + 28*60*60 ) xfree(pBlock); else
         if ( (pBlock->blk.pi.start_time < pBlock->blk.pi.stop_time) &&
              (pBlock->blk.pi.stop_time > now) )
         {  // Sendung noch nicht abgelaufen

            //dprintf4("RELOAD PI ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pBlock, netwop, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);
            pBlock->acqRepCount = 0;

            if (dbc->pFirstPi == NULL)
            {  // allererstes Element
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
               result = EPGDB_RELOAD_OK;
            }
            else
            if ( ( (pBlock->blk.pi.start_time > dbc->pLastPi->blk.pi.start_time) ||
                   ( (pBlock->blk.pi.start_time == dbc->pLastPi->blk.pi.start_time) &&
                     (pBlock->blk.pi.netwop_no > dbc->pLastPi->blk.pi.netwop_no) )) &&
                 ( ((pPrev = pPrevNetwop[netwop]) == NULL) ||
                   ( EpgDbPiCmpBlockNoGt(dbc, pBlock->blk.pi.block_no, pPrev->blk.pi.block_no, netwop) &&
                     (pBlock->blk.pi.start_time >= pPrev->blk.pi.stop_time) )))
            {  // hinten anhaengen

               // Netwop-Verzeigerung
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

               // in Startzeit-Verzeigerung einhaengen
               pBlock->pPrevBlock = dbc->pLastPi;
               pBlock->pNextBlock = NULL;
               dbc->pLastPi->pNextBlock = pBlock;
               dbc->pLastPi = pBlock;
               result = EPGDB_RELOAD_OK;
            }
            else
            {  // Startzeit und BlockNo nicht groesser als die des letzten Blocks -> ignorieren
               debug4("EpgDbReload-AddPiBlock: PI not consecutive: prov=0x%04X: netwop=%02d block_no=%d start=%ld", AI_GET_CNI(&dbc->pAiBlock->blk.ai), netwop, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);
               xfree(pBlock);
            }

            //assert(EpgDbCheckChains());
         }
         else
         {  // Sendung bereits abgelaufen -> PI nicht aufnehmen
            //dprintf3("EXPIRED pi netwop=%d, blockno=%d start=%ld\n", pBlock->blk.pi.netwop_no, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);
            result = EpgDbReloadAddDefectPiBlock(dbc, pBlock);
         }
      }
      else
      {  // invalid netwop or blockno
         xfree(pBlock);
      }
   }
   else
   {  // PI inconsistant
      xfree(pBlock);
   }
   return result;
}

// ---------------------------------------------------------------------------
// Read and verify the file header
//
static EPGDB_RELOAD_RESULT EpgDbReloadHeader( uint cni, int fd, EPGDBSAV_HEADER * pHead )
{
   size_t size;
   EPGDB_RELOAD_RESULT result;

   size = read(fd, (uchar *)pHead, sizeof(*pHead));
   if (size == sizeof(*pHead))
   {
      if (strncmp(pHead->magic, MAGIC_STR, MAGIC_STR_LEN) == 0)
      {
         if (pHead->endianMagic == ENDIAN_MAGIC)
         {
            if (pHead->compatVersion == DUMP_COMPAT)
            {
               if ((cni == RELOAD_ANY_CNI) || (pHead->cni == cni))
               {
                  result = EPGDB_RELOAD_OK;
               }
               else
               {
                  debug2("EpgDbReload-Header: reload db 0x%04X: unexpected CNI 0x%04X found", cni, pHead->cni);
                  result = EPGDB_RELOAD_CORRUPT;
               }
            }
            else
            {
               debug2("EpgDbReload-Header: reload db 0x%04X: incompatible version %06lx", cni, pHead->compatVersion);
               result = EPGDB_RELOAD_VERSION;
            }
         }
         else if (pHead->endianMagic != WRONG_ENDIAN)
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
         else
         {
            debug2("EpgDbReload-Header: reload db 0x%04X: incompatible endian 0x%04x", cni, pHead->endianMagic);
            result = EPGDB_RELOAD_ENDIAN;
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
   uchar buffer[BLK_UNION_OFF];
   size_t size;
   int fd;
   uchar * pFilename;
   time_t piStartOff;
   BLOCK_TYPE lastType;
   EPGDB_RELOAD_RESULT result;

   dbc = EpgDbCreate();

   if (pDemoDatabase == NULL)
   {  // append database file name to db directory (from -dbdir argument)
      pFilename = xmalloc(strlen(dbdir) + 1 + DUMP_NAME_MAX);
      sprintf(pFilename, "%s/" DUMP_NAME_FMT, dbdir, cni);
   }
   else
   {  // demo mode: database file name is taken from command line argument
      // CNI function parameter is ignored
      pFilename = xmalloc(strlen(pDemoDatabase) + 1);
      strcpy(pFilename, pDemoDatabase);
      cni = RELOAD_ANY_CNI;
   }

   fd = open(pFilename, O_RDONLY|O_BINARY);
   if (fd >= 0)
   {
      result = EpgDbReloadHeader(cni, fd, &head);
      if (result == EPGDB_RELOAD_OK)
      {
         dbc->pageNo      = head.pageNo;
         dbc->tunerFreq   = head.tunerFreq;
         dbc->appId       = head.appId;

         if (pDemoDatabase != NULL)
         {  // demo mode: shift all PI in time to the current time and future
            piStartOff = time(NULL) - head.dumpDate;
            piStartOff -= piStartOff % (60*60L);
            //printf("off=%lu = now=%lu - dump (= %lu=%s)\n", piStartOff, time(NULL), head.dumpDate, ctime(&head.dumpDate));
         }
         else
            piStartOff = 0;

         memset(pPrevNetwop, 0, sizeof(pPrevNetwop));
         memset(pPrevGeneric, 0, sizeof(pPrevGeneric));
         lastType = BLOCK_TYPE_INVALID;

         // load the header of the next block from the file (including the block type)
         while ( (result == EPGDB_RELOAD_OK) &&
                 ((size = read(fd, buffer, BLK_UNION_OFF)) == BLK_UNION_OFF) )
         {
            size = ((EPGDB_BLOCK *)buffer)->size;
            // plausibility check for block size (avoid malloc failure, which reults in program abort)
            if (size <= EPGDBSAV_MAX_BLOCK_SIZE)
            {
               result = EPGDB_RELOAD_CORRUPT;
               pBlock = (EPGDB_BLOCK *) xmalloc(size + BLK_UNION_OFF);
               memcpy(pBlock, buffer, BLK_UNION_OFF);
               // load the rest of the block from the file
               size = read(fd, (uchar *)pBlock + BLK_UNION_OFF, pBlock->size);
               if (size == pBlock->size)
               {
                  switch (pBlock->type)
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
                        // in demo mode, add time offset to shift PI into presence or future
                        ((PI_BLOCK*)&pBlock->blk.pi)->start_time += piStartOff;
                        ((PI_BLOCK*)&pBlock->blk.pi)->stop_time  += piStartOff;
                        result = EpgDbReloadAddPiBlock(dbc, pBlock, pPrevNetwop);
                        break;

                     case BLOCK_TYPE_DEFECT_PI:
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
                  lastType = pBlock->type;
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
      EpgDbDestroy(dbc);
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
// Peek inside a dumped database: retrieve provider info
//
const EPGDBSAV_PEEK * EpgDbPeek( uint cni, EPGDB_RELOAD_RESULT * pResult )
{
   EPGDBSAV_HEADER head;
   EPGDBSAV_PEEK *pPeek;
   EPGDB_BLOCK *pBlock;
   uchar buffer[BLK_UNION_OFF];
   size_t size;
   int fd;
   uchar *pFilename;
   EPGDB_RELOAD_RESULT result;

   pPeek = NULL;
   pFilename = xmalloc(strlen(dbdir) + 1 + DUMP_NAME_MAX);
   sprintf(pFilename, "%s/" DUMP_NAME_FMT, dbdir, cni);
   fd = open(pFilename, O_RDONLY|O_BINARY);
   if (fd >= 0)
   {
      pPeek = (EPGDBSAV_PEEK *) xmalloc(sizeof(EPGDBSAV_PEEK));
      pPeek->pAiBlock = NULL;

      result = EpgDbReloadHeader(cni, fd, &head);
      if (result == EPGDB_RELOAD_OK)
      {
         pPeek->pageNo      = head.pageNo;
         pPeek->tunerFreq   = head.tunerFreq;
         pPeek->appId       = head.appId;
         pPeek->dumpDate    = head.dumpDate;
         pPeek->firstPiDate = head.firstPiDate;
         pPeek->lastPiDate  = head.lastPiDate;

         if ((size = read(fd, buffer, BLK_UNION_OFF)) == BLK_UNION_OFF)
         {
            size = ((EPGDB_BLOCK *)buffer)->size;
            // plausibility check for block size (avoid malloc failure, which reults in program abort)
            if (size <= EPGDBSAV_MAX_BLOCK_SIZE)
            {
               pBlock = (EPGDB_BLOCK *) xmalloc(size + BLK_UNION_OFF);
               memcpy(pBlock, buffer, BLK_UNION_OFF);
               // load the rest of the block from the file
               size = read(fd, (uchar *)pBlock + BLK_UNION_OFF, pBlock->size);
               // check if it's a valid AI block (the first block in the dumped db must be AI)
               if ( (size == pBlock->size) &&
                    (pBlock->type == BLOCK_TYPE_AI) &&
                    (EpgDbReloadCheckAiBlock(pBlock) == EPGDB_RELOAD_OK) )
               {
                  pPeek->pAiBlock = pBlock;
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

      if (pPeek->pAiBlock == NULL)
      {  // database corrupt or wrong version -> free context
         xfree(pPeek);
         pPeek = NULL;
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

   return (const EPGDBSAV_PEEK *) pPeek;
}

// ---------------------------------------------------------------------------
// Free a PEEK struct
//
void EpgDbPeekDestroy( const EPGDBSAV_PEEK *pPeek )
{
   if (pPeek != NULL)
   {
      if (pPeek->pAiBlock != NULL)
         xfree(pPeek->pAiBlock);
      xfree((uchar*)pPeek);
   }
}

// ---------------------------------------------------------------------------
// Scans a given directory for the best or the next database
// - if index is -1 the best is searched
// - the "best" db is currently the last used (i.e. youngest mtime)
// - returns CNI of best provider, 0 if none found
//
uint EpgDbReloadScan( int nextIndex )
{
#ifndef WIN32
   DIR    *dir;
   struct dirent *entry;
   struct stat st;
   time_t best_mtime;
   uint   cni, best_cni;
   int    this_index, scanLen;
   char   *pFilePath;

   best_cni = 0;
   best_mtime = 0;
   this_index = -1;
   dir = opendir(dbdir);
   if (dir != NULL)
   {
      while ((entry = readdir(dir)) != NULL)
      {
         if ( (strlen(entry->d_name) == DUMP_NAME_LEN) &&
              (sscanf(entry->d_name, DUMP_NAME_FMT "%n", &cni, &scanLen) == 1) &&
              (scanLen == DUMP_NAME_LEN) )
         {  // file name matched (complete match is forced by %n)
            if (nextIndex != -1)
            {
               this_index += 1;
               if (this_index == nextIndex)
               {
                  best_cni = cni;
                  break;
               }
            }
            else
            {
               pFilePath = xmalloc(strlen(dbdir) + 1 + strlen(entry->d_name) + 1);
               sprintf(pFilePath, "%s/%s", dbdir, entry->d_name);

               if ( (stat(pFilePath, &st) == 0) &&
                    (st.st_mtime > best_mtime) )
               {
                  best_mtime = st.st_mtime;
                  best_cni = cni;
               }
               xfree(pFilePath);
            }
         }
      }
      closedir(dir);
   }
   else
      perror("opendir");

   return best_cni;

#else  //WIN32
   HANDLE hFind;
   WIN32_FIND_DATA finddata;
   FILETIME best_mtime;
   uint cni, best_cni;
   uchar *p, *pDirPath;
   bool bMore;
   int  this_index;

   best_cni = 0;
   this_index = -1;

   pDirPath = xmalloc(strlen(dbdir) + 1 + DUMP_NAME_MAX);
   sprintf(pDirPath, "%s\\%s", dbdir, DUMP_NAME_PAT);
   hFind = FindFirstFile(pDirPath, &finddata);
   bMore = (hFind != (HANDLE) -1);
   while (bMore)
   {
      for (p=finddata.cFileName; *p; p++)
      {  // convert file name to upper case for comparison
         *p = toupper(*p);
      }

      if ( (strlen(finddata.cFileName) == DUMP_NAME_LEN) &&
           (sscanf(finddata.cFileName, DUMP_NAME_FMT, &cni) == 1) )
      {  // file name matched
         if (nextIndex != -1)
         {
            this_index += 1;
            if (this_index == nextIndex)
            {
               best_cni = cni;
               break;
            }
         }
         else if ( (best_cni == 0) ||
                   (CompareFileTime(&best_mtime, &finddata.ftLastWriteTime) == -1) )
         {
            best_mtime = finddata.ftLastWriteTime;
            best_cni = cni;
         }
      }
      bMore = FindNextFile(hFind, &finddata);
   }
   FindClose(hFind);
   xfree(pDirPath);

   return best_cni;
#endif  //WIN32
}

// ---------------------------------------------------------------------------
// Create database directory, if neccessary
//
bool EpgDbDumpCreateDir( const char * pDirPath )
{
   bool result = TRUE;

#ifndef WIN32
   struct stat st;

   if (pDirPath != NULL)
   {
      if (stat(pDirPath, &st) != 0)
      {  // directory does no exist -> create it
         if ((errno != ENOENT) || (mkdir(pDirPath, 0777) != 0))
         {
            fprintf(stderr, "cannot create dbdir %s: %s\n", pDirPath, strerror(errno));
            result = FALSE;
         }
      }
      else if (S_ISDIR(st.st_mode) == FALSE)
      {  // target is not a directory -> warn
         fprintf(stderr, "dbdir '%s' is not a directory\n", pDirPath);
         result = FALSE;
      }

      if (result)
      {  // set permissions of database directory: world-r/w-access
         if ((st.st_mode != 0777) && (chmod(pDirPath, 0777) != 0))
         {
            fprintf(stderr, "warning: cannot set permissions 0777 for dbdir %s: %s\n", pDirPath, strerror(errno));
            // this is not a fatal error, result remains TRUE
         }
      }
   }

#else  //WIN32
   HANDLE hFind;
   WIN32_FIND_DATA finddata;
   DWORD attr;
   char msg[256];

   if (pDirPath != NULL)
   {
      hFind = FindFirstFile(pDirPath, &finddata);
      if (hFind == (HANDLE) -1)
      {  // directory does no exist -> create it
         if (mkdir(dbdir) != 0)
         {  // creation failed -> warn
            sprintf(msg, "Cannot create database directory %s:\n%s\nCheck your -dbdir command line option", pDirPath, strerror(errno));
            MessageBox(NULL, msg, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            result = FALSE;
         }
      }
      if (result)
      {  // check if the file found is a directory
         attr = GetFileAttributes(pDirPath);
         if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
         {  // target not a directory -> warn
            sprintf(msg, "Invalid database directory specified: %s\nAlready exists, but is not a directory", pDirPath);
            MessageBox(NULL, msg, "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            result = FALSE;
         }
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
      if ( (strlen(pNamePath) == DUMP_NAME_LEN) &&
           (sscanf(pNamePath, DUMP_NAME_FMT "%n", pCni, &scanLen) == 1) &&
           (scanLen == DUMP_NAME_LEN) )
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
bool EpgDbDumpUpdateHeader( uint cni, ulong freq )
{
   EPGDBSAV_HEADER head;
   size_t size;
   uchar * pFilename;
   int fd;
   bool result = FALSE;

   pFilename = xmalloc(strlen(dbdir) + 1 + DUMP_NAME_MAX);
   sprintf(pFilename, "%s/" DUMP_NAME_FMT, dbdir, cni);
   fd = open(pFilename, O_RDWR|O_BINARY);
   if (fd >= 0)
   {
      size = read(fd, (uchar *)&head, sizeof(head));
      if (size == sizeof(head))
      {
         if ( (strncmp(head.magic, MAGIC_STR, MAGIC_STR_LEN) == 0) &&
              (head.compatVersion == DUMP_COMPAT) &&
              (head.endianMagic == ENDIAN_MAGIC) &&
              (head.cni == cni) )
         {
            head.tunerFreq = freq;

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
            debug0("EpgDbDump-UpdateHeader: invalid magic or cni in db");
      }
   }
   xfree(pFilename);
   return result;
}

