/*
 *  Export Nextview database as "TAB-separated" text file
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *    This module contains functions that can write the network table,
 *    PDC themes table or all programme information into text files.
 *    The format of the output is TAB separated text, which is
 *    suitable for import into MySQL.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgctxctl.h"
#include "epgui/epgsetup.h"
#include "epgui/pdc_themes.h"
#include "epgui/pidescr.h"
#include "epgui/dumptext.h"


#define NETNAME_LENGTH 6
#define NETNAME_LENGTH_STR "6"
#define NETNAME_LENGTH0 (NETNAME_LENGTH+1)

// ----------------------------------------------------------------------------
// Print description texts or separators
//
static void DumpText_PiInfoTextCb( void * vp, const char * pDesc, bool addSeparator )
{
   PI_DESCR_BUF * pBuf = (PI_DESCR_BUF*) vp;
   const char * pNewline;

   if (pBuf != NULL)
   {
      if (addSeparator)
      {  // output separator between texts from different providers
         PiDescription_BufAppend(pBuf, " //%// ", -1);
      }

      // check for newline chars, they must be replaced, because one PI must
      // occupy exactly one line in the output file
      while ( (pNewline = strchr(pDesc, '\n')) != NULL )
      {
         // print text up to (and excluding) the newline
         PiDescription_BufAppend(pBuf, pDesc, pNewline - pDesc);
         PiDescription_BufAppend(pBuf, " // ", -1);
         // skip to text following the newline
         pDesc = pNewline + 1;
      }
      // write the segement behind the last newline
      PiDescription_BufAppend(pBuf, pDesc, -1);
   }
}

// ---------------------------------------------------------------------------
// Export PI block to MySQL
//
static void DumpText_Pi( PI_DESCR_BUF * pb, const PI_BLOCK * pPi, const EPGDB_CONTEXT * pDbContext )
{
   uchar hour, minute, day, month;
   uint  year;
   char  str_buf[128];
   const char *pStrSoundFormat;
   struct tm *pTm;
   time_t start_time;
   time_t stop_time;
   sint  len;

   if (pPi != NULL)
   {
      str_buf[sizeof(str_buf) - 1] = 0;

      len = sprintf(str_buf, "%u\t", pPi->netwop_no);
      PiDescription_BufAppend(pb, str_buf, len);

      start_time = pPi->start_time;
      pTm = localtime(&start_time);
      strftime(str_buf, sizeof(str_buf), "%Y-%m-%d\t%H:%M:00\t", pTm);
      PiDescription_BufAppend(pb, str_buf, -1);
      year = pTm->tm_year + 1900;

      stop_time = pPi->stop_time;
      pTm = localtime(&stop_time);
      strftime(str_buf, sizeof(str_buf), "%H:%M:00\t", pTm);
      PiDescription_BufAppend(pb, str_buf, -1);

      day    = (pPi->pil >> 15) & 0x1f;
      month  = (pPi->pil >> 11) & 0x0f;
      hour   = (pPi->pil >>  6) & 0x1f;
      minute =  pPi->pil        & 0x3f;

      if ((day > 0) && (month > 0) && (month <= 12) && (hour < 24) && (minute < 60))
      {
         len = sprintf(str_buf, "%04d-%02d-%02d %02d:%02d:00\t",
                                year, month, day, hour, minute);
         PiDescription_BufAppend(pb, str_buf, len);
      }
      else
         PiDescription_BufAppend(pb, "\\N\t", 3);  // MySQL NULL

      len = sprintf(str_buf, "%u\t%u\t", pPi->parental_rating *2, pPi->editorial_rating);
      PiDescription_BufAppend(pb, str_buf, len);

      switch (pPi->feature_flags & PI_FEATURE_SOUND_MASK)
      {
        case  PI_FEATURE_SOUND_MONO: pStrSoundFormat = "mono\t"; break;
        case  PI_FEATURE_SOUND_2CHAN: pStrSoundFormat = "2-chan\t"; break;
        case  PI_FEATURE_SOUND_STEREO: pStrSoundFormat = "stereo\t"; break;
        case  PI_FEATURE_SOUND_SURROUND: pStrSoundFormat = "surround\t"; break;
        default: pStrSoundFormat = "\t"; break;  // MySQL error value
      }
      PiDescription_BufAppend(pb, pStrSoundFormat, -1);

      len = sprintf(str_buf, "%c\t%c\t%c\t%c\t%c\t%c\t%c\t",
                       ((pPi->feature_flags & PI_FEATURE_FMT_WIDE) ? '1' : '0'),
                       ((pPi->feature_flags & PI_FEATURE_PAL_PLUS) ? '1' : '0'),
                       '0', /*((pPi->feature_flags & PI_FEATURE_DIGITAL) ? '1' : '0'),*/ // obsolete
                       ((pPi->feature_flags & PI_FEATURE_ENCRYPTED) ? '1' : '0'),
                       ((pPi->feature_flags & PI_FEATURE_LIVE) ? '1' : '0'),
                       ((pPi->feature_flags & PI_FEATURE_REPEAT) ? '1' : '0'),
                       ((pPi->feature_flags & PI_FEATURE_SUBTITLES) ? '1' : '0') );
      PiDescription_BufAppend(pb, str_buf, len);

      len = sprintf(str_buf, "%u\t%u\t%u\t%u\t%u\t%u\t%u\t",
              ((pPi->no_themes > 0) ? pPi->themes[0] : 0),
              ((pPi->no_themes > 1) ? pPi->themes[1] : 0),
              ((pPi->no_themes > 2) ? pPi->themes[2] : 0),
              ((pPi->no_themes > 3) ? pPi->themes[3] : 0),
              ((pPi->no_themes > 4) ? pPi->themes[4] : 0),
              ((pPi->no_themes > 5) ? pPi->themes[5] : 0),
              ((pPi->no_themes > 6) ? pPi->themes[6] : 0) );
      PiDescription_BufAppend(pb, str_buf, len);

      PiDescription_BufAppend(pb, PI_GET_TITLE(pPi), -1);
      PiDescription_BufAppend(pb, "\t", 1);

      PiDescription_AppendDescriptionText(pPi, DumpText_PiInfoTextCb, pb, EpgDbContextIsMerged(pDbContext));
      PiDescription_BufAppend(pb, "\n", 1);

      assert(str_buf[sizeof(str_buf) - 1] == 0);  // check for buffer overrun
   }
}

// ---------------------------------------------------------------------------
// Export network table to MySQL
//
static void DumpText_Ai( PI_DESCR_BUF * pb, const AI_BLOCK * pAi )
{
   const AI_NETWOP *pNetwop;
   const char * pNetname;
   char str_buf[128];
   uint netwop;
   uint len;

   if (pAi != NULL)
   {
      pNetwop = AI_GET_NETWOPS(pAi);

      for (netwop=0; netwop < pAi->netwopCount; netwop++)
      {
         len = sprintf(str_buf, "%u\t%u\t%d\t%u\t%u\t%u\t",
                     netwop,
                     AI_GET_NET_CNI(pNetwop),
                     pNetwop->lto * 15,
                     pNetwop->dayCount,
                     pNetwop->language,
                     0 /*pNetwop->addInfo*/);
         PiDescription_BufAppend(pb, str_buf, len);

         // get user-configured network name
         pNetname = EpgSetup_GetNetName(pAi, netwop, NULL);

         PiDescription_BufAppend(pb, pNetname, -1);
         PiDescription_BufAppend(pb, "\n", 1);

         pNetwop += 1;
      }
   }
}

// ---------------------------------------------------------------------------
// Export PDC themes table to MySQL
//
static void DumpText_PdcThemes( PI_DESCR_BUF * pb )
{
   const char * pThemeStr_eng;
   const char * pThemeStr_ger;
   const char * pThemeStr_fra;
   char  str_buf[128];
   uint  idx;
   uint  len;

   for (idx=0; idx <= 128; idx++)
   {
      pThemeStr_eng = PdcThemeGetByLang(idx, 0);
      pThemeStr_ger = PdcThemeGetByLang(idx, 1);
      pThemeStr_fra = PdcThemeGetByLang(idx, 4);
      if ( (pThemeStr_eng != NULL) && (pThemeStr_ger != NULL) && (pThemeStr_fra != NULL) )
      {
         len = sprintf(str_buf, "%u\t%u\t",
                               idx, PdcThemeGetCategory(idx));
         PiDescription_BufAppend(pb, str_buf, len);

         PiDescription_BufAppend(pb, pThemeStr_eng, -1);
         PiDescription_BufAppend(pb, "\t", 1);

         PiDescription_BufAppend(pb, pThemeStr_ger, -1);
         PiDescription_BufAppend(pb, "\t", 1);

         PiDescription_BufAppend(pb, pThemeStr_fra, -1);
         PiDescription_BufAppend(pb, "\n", 1);
      }
   }
}

// ---------------------------------------------------------------------------
// Export a single programme in the database into a buffer in memory
// - data is appended to the buffer, so the buffer must be initialized
//
bool EpgDumpText_Single( EPGDB_CONTEXT * pDbContext, const PI_BLOCK * pPi, PI_DESCR_BUF * pb )
{
   const AI_BLOCK * pAi;
   const char * pNetname;
   bool  result = FALSE;

   EpgDbIsLocked(pDbContext);

   if ((pDbContext != NULL) && (pPi != NULL))
   {
      pAi = EpgDbGetAi(pDbContext);
      if (pAi != NULL)
      {
         // get user-configured network name
         pNetname = EpgSetup_GetNetName(pAi, pPi->netwop_no, NULL);

         PiDescription_BufAppend(pb, pNetname, -1);
         PiDescription_BufAppend(pb, "\t", 1);

         DumpText_Pi(pb, pPi, pDbContext);

         result = TRUE;
      }
      else
         debug0("EpgDumpText-Single: no AI in db");
   }
   else
      fatal2("EpgDumpText-Single: illegal NULL ptr param: %lX, %lX", (long)pDbContext, (long)pPi);

   return result;
}

// ---------------------------------------------------------------------------
// Export the complete database in "tab-seprarated" format for SQL import
//
void EpgDumpText_Standalone( EPGDB_CONTEXT * pDbContext, FILTER_CONTEXT * fc,
                             FILE * fp, DUMP_TEXT_MODE mode )
{
   const AI_BLOCK * pAi;
   const PI_BLOCK * pPi;
   PI_DESCR_BUF pbuf;

   memset(&pbuf, 0, sizeof(pbuf));
   pbuf.fp = fp;

   EpgDbLockDatabase(pDbContext, TRUE);

   // Dump PDC theme list
   if (mode == DUMP_TEXT_PDC)
   {
      DumpText_PdcThemes(&pbuf);
   }
   else if (mode == DUMP_TEXT_AI)
   {  // Dump application information block
      pAi = EpgDbGetAi(pDbContext);
      if (pAi != NULL)
      {
         DumpText_Ai(&pbuf, pAi);
      }
   }
   else if (mode == DUMP_TEXT_PI)
   {  // Dump programme information blocks
      pPi = EpgDbSearchFirstPi(pDbContext, fc);
      while (pPi != NULL)
      {
         DumpText_Pi(&pbuf, pPi, pDbContext);

         pPi = EpgDbSearchNextPi(pDbContext, fc, pPi);
      }
   }
   else
      debug1("EpgDumpText-Standalone: invalid mode:%d\n", mode);

   EpgDbLockDatabase(pDbContext, FALSE);
}

