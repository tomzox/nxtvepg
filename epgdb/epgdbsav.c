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
 *  $Id: epgdbsav.c,v 1.16 2000/10/14 21:57:05 tom Exp tom $
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

   strncpy(header.magic, MAGIC_STR, MAGIC_STR_LEN);
   memset(header.reserved, 0, sizeof(header.reserved));
   header.dumpVersion = DUMP_VERSION;

   if (dbc->pAiBlock != NULL)
   {
      header.cni = AI_GET_NETWOP_N(&dbc->pAiBlock->blk.ai, dbc->pAiBlock->blk.ai.thisNetwop)->cni;
      header.pageNo = dbc->pageNo;
      header.tunerFreq = dbc->tunerFreq;

      if (dbc->pLastPi != NULL)
         header.lastPiDate = dbc->pLastPi->blk.pi.start_time;
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
      debug0("EpgDb-DumpHeader: no AI block");

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
   if ((dbc->pBiBlock != NULL) && (dbc->pAiBlock != NULL) && (dbc->modified))
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
                  EpgDbDumpAppendBlock(dbc->pBiBlock, fd) &&
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
      xfree(pTmpname);
   }
   else
      dprintf1("EpgDb-Dump: %s - not dumped\n", ((dbc->pBiBlock==NULL)?"no BI block":((dbc->pAiBlock==NULL)?"no AI block":"not modified")));

   EpgDbLockDatabase(dbc, FALSE);
   return result;
}

// ---------------------------------------------------------------------------
// Add a loaded BI block to the database
//
static void EpgDbReloadAddBiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   if (dbc->pBiBlock == NULL)
   {
      pBlock->pNextBlock       = NULL;
      pBlock->pPrevBlock       = NULL;
      pBlock->pNextNetwopBlock = NULL;

      dbc->pBiBlock = pBlock;
   }
   else
   {
      debug0("EpgDb-Reload-AddBiBlock: BI block already exists");
      xfree(pBlock);
   }
}

// ---------------------------------------------------------------------------
// Add a loaded AI block to the database
//
static void EpgDbReloadAddAiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   if (dbc->pAiBlock == NULL)
   {
      pBlock->pNextBlock       = NULL;
      pBlock->pPrevBlock       = NULL;
      pBlock->pNextNetwopBlock = NULL;

      dbc->pAiBlock = pBlock;
   }
   else
   {
      debug0("EpgDb-Reload-AddAiBlock: AI block already exists");
      xfree(pBlock);
   }
}

// ---------------------------------------------------------------------------
// Add a loaded generic-type block (i.e. OI,MI,NI etc.) to the database
//
static void EpgDbReloadAddGenericBlock( PDBC dbc, EPGDB_BLOCK *pBlock, EPGDB_BLOCK **pPrevGeneric )
{
   EPGDB_BLOCK *pPrev;
   uint  blockCount;
   uint  block_no;

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
            }
            else
            {  // Blocknummer nicht groesser als die des letzten Blocks -> ignorieren
               debug2("EpgDb-AddGenericBlock: blockno=%d <= pPrev %d", block_no, pPrev->blk.all.block_no);
               xfree(pBlock);
            }
         }
      }
      else
      {
         debug3("EpgDb-AddGenericBlock: type=%d: invalid block_no=%d >= count=%d", pBlock->type, pBlock->blk.all.block_no, blockCount);
         xfree(pBlock);
      }
      //assert(EpgDbCheckChains());
   }
   else
   {
      debug2("EpgDb-AddGenericBlock: invalid type %d or block_no %d", pBlock->type, pBlock->blk.all.block_no);
      xfree(pBlock);
   }
}

// ---------------------------------------------------------------------------
// Add an expired or defect PI block to the database
//
static void EpgDbReloadAddDefectPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock )
{
   if (pBlock->version == dbc->pAiBlock->blk.ai.version)
   {
      if ( EpgDbPiBlockNoValid(dbc, pBlock->blk.pi.block_no, pBlock->blk.pi.netwop_no) )
      {
         // convert type to normal PI, since the "defect pi" type is known only inside this module
         pBlock->type = BLOCK_TYPE_PI;
         // prepend to list of defect blocks
         pBlock->pNextBlock = dbc->pObsoletePi;
         dbc->pObsoletePi = pBlock;
      }
      else
      {
         debug3("EpgDbReload-AddDefectPiBlock: invalid netwop=%d stop=%ld block_no=%d", pBlock->blk.pi.netwop_no, pBlock->blk.pi.stop_time, pBlock->blk.pi.block_no);
         xfree(pBlock);
      }
   }
   else
   {
      dprintf5("EpgDbReload-AddDefectPiBlock: PI obsolete version=%d != %d: netwop=%02d block_no=%d start=%ld\n", pBlock->version, dbc->pAiBlock->blk.ai.version, pBlock->blk.pi.netwop_no, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);
      xfree(pBlock);
   }
}

// ---------------------------------------------------------------------------
// Add a PI block to the database
//
static void EpgDbReloadAddPiBlock( PDBC dbc, EPGDB_BLOCK * pBlock, EPGDB_BLOCK **pPrevNetwop )
{
   EPGDB_BLOCK *pPrev;
   uchar netwop;
   ulong now;
   
   netwop = pBlock->blk.pi.netwop_no;
   if ( (netwop < dbc->pAiBlock->blk.ai.netwopCount) &&
        EpgDbPiBlockNoValid(dbc, pBlock->blk.pi.block_no, netwop) )
   {
      now = time(NULL);

      if ( (pBlock->blk.pi.start_time < pBlock->blk.pi.stop_time) &&
           (pBlock->blk.pi.stop_time > now) )
      {  // Sendung noch nicht abgelaufen

         //dprintf4("RELOAD PI ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pBlock, netwop, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);

         if (dbc->pFirstPi == NULL)
         {  // allererstes Element
            assert(dbc->pLastPi == NULL);
            assert(dbc->pFirstNetwopPi[netwop] == NULL);
            pBlock->pNextNetwopBlock = NULL;
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
              ( ((pPrev = pPrevNetwop[netwop]) == NULL) ||
                ( EpgDbPiCmpBlockNoGt(dbc, pBlock->blk.pi.block_no, pPrev->blk.pi.block_no, netwop) &&
                  (pBlock->blk.pi.start_time >= pPrev->blk.pi.stop_time) )))
         {  // hinten anhaengen

            // Netwop-Verzeigerung
            if (dbc->pFirstNetwopPi[netwop] == NULL)
               dbc->pFirstNetwopPi[netwop] = pBlock;
            else
            {
               assert(pPrevNetwop[netwop] != NULL);
               pPrevNetwop[netwop]->pNextNetwopBlock = pBlock;
            }
            pBlock->pNextNetwopBlock = NULL;
            pPrevNetwop[netwop] = pBlock;

            // in Startzeit-Verzeigerung einhaengen
            pBlock->pPrevBlock = dbc->pLastPi;
            pBlock->pNextBlock = NULL;
            dbc->pLastPi->pNextBlock = pBlock;
            dbc->pLastPi = pBlock;
         }
         else
         {  // Startzeit und BlockNo nicht groesser als die des letzten Blocks -> ignorieren
            debug3("EpgDb-Reload-AddPiBlock: PI not consecutive: netwop=%02d block_no=%d start=%ld", netwop, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);
            xfree(pBlock);
         }

         //assert(EpgDbCheckChains());
      }
      else
      {  // Sendung bereits abgelaufen -> PI nicht aufnehmen
         //dprintf3("EXPIRED pi netwop=%d, blockno=%d start=%ld\n", pBlock->blk.pi.netwop_no, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);
         EpgDbReloadAddDefectPiBlock(dbc, pBlock);
      }
   }
   else
   {  // ungueltige netwop-Angabe -> PI nicht aufnehmen
      debug3("EpgDbReload-AddPiBlock: invalid netwop=%d stop=%ld block_no=%d", netwop, pBlock->blk.pi.stop_time, pBlock->blk.pi.block_no);
      xfree(pBlock);
   }
}

// ---------------------------------------------------------------------------
// Read and verify the file header
//
static bool EpgDbReloadHeader( uint cni, int fd, uint *pPageNo, ulong *pFreq )
{
   EPGDBSAV_HEADER head;
   size_t size;
   bool result = FALSE;

   size = read(fd, (uchar *)&head, sizeof(head));
   if (size == sizeof(head))
   {
      if ( (strncmp(head.magic, MAGIC_STR, MAGIC_STR_LEN) == 0) &&
           (head.dumpVersion <= DUMP_VERSION) &&
           (head.dumpVersion >= DUMP_COMPAT) &&
           ((cni == RELOAD_ANY_CNI) || (head.cni == cni)) )
      {
         *pPageNo = head.pageNo;
         *pFreq   = head.tunerFreq;
         result = TRUE;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Restore the complete Nextview EPG database from a file dump
// - XXX TODO: ensure that BI and AI are loaded first
// - XXX TODO: handle read i/o errors
//
bool EpgDbReload( PDBC dbc, uint cni )
{
   EPGDB_BLOCK *pPrevNetwop[MAX_NETWOP_COUNT];
   EPGDB_BLOCK *pPrevGeneric[BLOCK_TYPE_GENERIC_COUNT];
   EPGDB_BLOCK *pBlock;
   uchar buffer[BLK_UNION_OFF];
   size_t size;
   int fd;
   uchar * pFilename;
   bool result = FALSE;

   if ( EpgDbIsLocked(dbc) == FALSE )
   {
      if ( (dbc->pAiBlock == NULL) &&
           (dbc->pBiBlock == NULL) &&
           (dbc->pFirstPi == NULL) )
      {
         pFilename = xmalloc(strlen(dbdir) + 1 + DUMP_NAME_MAX);
         sprintf(pFilename, "%s/" DUMP_NAME_FMT, dbdir, cni);
         fd = open(pFilename, O_RDONLY|O_BINARY);
         if (fd >= 0)
         {
            if (EpgDbReloadHeader(cni, fd, &dbc->pageNo, &dbc->tunerFreq))
            {
               memset(pPrevNetwop, 0, sizeof(pPrevNetwop));
               memset(pPrevGeneric, 0, sizeof(pPrevGeneric));

               while ((size = read(fd, buffer, BLK_UNION_OFF)) == BLK_UNION_OFF)
               {
                  pBlock = (EPGDB_BLOCK *) xmalloc(((EPGDB_BLOCK *)buffer)->size + BLK_UNION_OFF);
                  if (pBlock != NULL)
                  {
                     memcpy(pBlock, buffer, BLK_UNION_OFF);
                     size = read(fd, (uchar *)pBlock + BLK_UNION_OFF, pBlock->size);
                     if (size == pBlock->size)
                     {
                        switch (pBlock->type)
                        {
                           case BLOCK_TYPE_AI:
                              EpgDbReloadAddAiBlock(dbc, pBlock);
                              break;

                           case BLOCK_TYPE_BI:
                              EpgDbReloadAddBiBlock(dbc, pBlock);
                              break;

                           case BLOCK_TYPE_PI:
                              EpgDbReloadAddPiBlock(dbc, pBlock, pPrevNetwop);
                              break;

                           case BLOCK_TYPE_DEFECT_PI:
                              EpgDbReloadAddDefectPiBlock(dbc, pBlock);
                              break;

                           default:
                              EpgDbReloadAddGenericBlock(dbc, pBlock, pPrevGeneric);
                              break;
                        }
                     }
                     else
                        xfree(pBlock);
                  }
               }
               assert(EpgDbCheckChains(dbc));
               result = TRUE;
            }
            close(fd);
         }
         else
            dprintf1("EpgDb-Reload: db of requested provider %04X not found\n", cni);

         xfree(pFilename);
      }
      else
         debug0("EpgDb-Reload: db not empty");
   }
   else
      debug0("EpgDb-Reload: DB is locked - cannot modify");

   return result;
}

// ---------------------------------------------------------------------------
// Peek inside a dumped database: retrieve provider info
//
const EPGDBSAV_PEEK * EpgDbPeek( uint cni )
{
   EPGDBSAV_PEEK *pPeek;
   EPGDB_BLOCK *pBlock;
   uchar buffer[BLK_UNION_OFF];
   size_t size;
   int fd;
   uchar *pFilename;

   pPeek = NULL;
   pFilename = xmalloc(strlen(dbdir) + 1 + DUMP_NAME_MAX);
   sprintf(pFilename, "%s/" DUMP_NAME_FMT, dbdir, cni);
   fd = open(pFilename, O_RDONLY|O_BINARY);
   if (fd >= 0)
   {
      pPeek = (EPGDBSAV_PEEK *) xmalloc(sizeof(EPGDBSAV_PEEK));
      if (EpgDbReloadHeader(cni, fd, &pPeek->pageNo, &pPeek->tunerFreq))
      {
         pPeek->pBiBlock = NULL;
         pPeek->pAiBlock = NULL;
         while ((size = read(fd, buffer, BLK_UNION_OFF)) == BLK_UNION_OFF)
         {
            pBlock = (EPGDB_BLOCK *) xmalloc(((EPGDB_BLOCK *)buffer)->size + BLK_UNION_OFF);
            if (pBlock != NULL)
            {
               memcpy(pBlock, buffer, BLK_UNION_OFF);
               size = read(fd, (uchar *)pBlock + BLK_UNION_OFF, pBlock->size);
               if (size == pBlock->size)
               {
                  switch (pBlock->type)
                  {
                     case BLOCK_TYPE_AI:
                        pPeek->pAiBlock = pBlock;
                        break;

                     case BLOCK_TYPE_BI:
                        pPeek->pBiBlock = pBlock;
                        break;

                     default:
                        xfree(pBlock);
                        break;
                  }

                  // if all interesting data has been found -> exit loop
                  if ((pPeek->pBiBlock != NULL) && (pPeek->pAiBlock != NULL))
                     break;
               }
               else
                  xfree(pBlock);
            }
         }
      }
      else
      {
         xfree(pPeek);
         pPeek = NULL;
      }
      close(fd);
   }
   else
      dprintf1("EpgDb-Reload: db of requested provider %04X not found\n", cni);

   xfree(pFilename);

   return (const EPGDBSAV_PEEK *) pPeek;
}

// ---------------------------------------------------------------------------
// Free a PEEK struct
//
void EpgDbPeekDestroy( const EPGDBSAV_PEEK *pPeek )
{
   if (pPeek != NULL)
   {
      if (pPeek->pBiBlock != NULL)
         xfree(pPeek->pBiBlock);
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
   int    this_index;
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
              (sscanf(entry->d_name, DUMP_NAME_FMT, &cni) == 1) )
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
   bool bMore;
   int  this_index;

   best_cni = 0;
   this_index = -1;

   hFind = FindFirstFile(DUMP_NAME_PAT, &finddata);
   bMore = (hFind != (HANDLE) -1);
   while (bMore)
   {
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

   return best_cni;
#endif  //WIN32
}

// ---------------------------------------------------------------------------
// Update the tuner frequency in the dump file header
//
bool EpgDbDumpUpdateHeader( CPDBC dbc, uint cni, ulong freq )
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
              (head.dumpVersion <= DUMP_VERSION) &&
              (head.dumpVersion >= DUMP_COMPAT) &&
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
                  debug2("EpgDb-DumpUpdateHeader: write failed with %d, errno %d\n", size, errno);
            }
            else
               debug1("EpgDb-DumpUpdateHeader: seek failed with errno %d", errno);
         }
         else
            debug0("EpgDb-DumpUpdateHeader: invalid magic or cni in db");
      }
   }
   xfree(pFilename);
   return result;
}

