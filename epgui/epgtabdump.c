/*
 *  Export Nextview database as "TAB-separated" text file
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgtabdump.c,v 1.5 2002/12/08 19:25:42 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <string.h>
#include <stdio.h>
#include <time.h>

#include <tcl.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgctxctl.h"
#include "epgui/pdc_themes.h"
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgui/menucmd.h"
#include "epgui/pioutput.h"
#include "epgui/epgtabdump.h"


#define NETNAME_LENGTH 6
#define NETNAME_LENGTH_STR "6"
#define NETNAME_LENGTH0 (NETNAME_LENGTH+1)

// ----------------------------------------------------------------------------
// Print Short- and Long-Info texts or separators
//
static void EpgTabDumpPiInfoTextCb( void * vp, const char * pDesc, bool addSeparator )
{
   FILE * fp = (FILE *) vp;
   char * pNewline;
   char  fmtBuf[15];

   if (fp != NULL)
   {
      if (addSeparator)
      {  // output separator between texts from different providers
         fprintf(fp, " //%%// ");
      }

      // check for newline chars, they must be replaced, because one PI must
      // occupy exactly one line in the output file
      while ( (pNewline = strchr(pDesc, '\n')) != NULL )
      {
         // print text up to (and excluding) the newline
         sprintf(fmtBuf, "%%.%ds // ", pNewline - pDesc);
         fprintf(fp, fmtBuf, pDesc);
         // skip to text following the newline
         pDesc = pNewline + 1;
      }
      // write the segement behind the last newline
      fprintf(fp, "%s", pDesc);
   }
}

// ---------------------------------------------------------------------------
// Export PI block to MySQL
//
static void EpgTabDumpPi( FILE *fp, const PI_BLOCK * pPi, const EPGDB_CONTEXT * pDbContext )
{
   const uchar * pShort;
   const uchar * pLong;
   uchar hour, minute, day, month;
   uchar pilStr[30];
   uchar start_str[40], stop_str[20];
   uchar *pStrSoundFormat;
   struct tm *pStart, *pStop;

   if (pPi != NULL)
   {
      pStart = localtime(&pPi->start_time);
      if (pStart != NULL)
         strftime(start_str, sizeof(start_str), "%Y-%m-%d\t%H:%M:00", pStart);
      else
         strcpy(start_str, "");

      pStop  = localtime(&pPi->stop_time);
      if (pStop != NULL)
         strftime(stop_str, sizeof(stop_str), "%H:%M:00", pStop);
      else
         strcpy(stop_str, "");

      day    = (pPi->pil >> 15) & 0x1f;
      month  = (pPi->pil >> 11) & 0x0f;
      hour   = (pPi->pil >>  6) & 0x1f;
      minute =  pPi->pil        & 0x3f;

      if ((day > 0) && (month > 0) && (month <= 12) && (hour < 24) && (minute < 60) &&
          (pStart != NULL))
      {
         sprintf(pilStr, "%04d-%02d-%02d %02d:%02d:00",
                         pStart->tm_year + 1900, month, day, hour, minute);
      }
      else
         strcpy(pilStr, "\\N");  // MySQL NULL

      switch(pPi->feature_flags & 0x03)
      {
        case  0: pStrSoundFormat = "mono"; break;
        case  1: pStrSoundFormat = "2-chan"; break;
        case  2: pStrSoundFormat = "stereo"; break;
        case  3: pStrSoundFormat = "surround"; break;
        default: pStrSoundFormat = ""; break;  // MySQL error value
      }

      if (PI_HAS_SHORT_INFO(pPi))
         pShort = PI_GET_SHORT_INFO(pPi);
      else
         pShort = "";

      if (PI_HAS_LONG_INFO(pPi))
         pLong = PI_GET_LONG_INFO(pPi);
      else
         pLong = "";

      fprintf(fp, "%u\t%s\t%s\t%s\t%u\t%u\t"           // netwop ... e-rat
                  "%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t"   // features
                  "%u\t%u\t%u\t%u\t%u\t%u\t%u\t"       // themes
                  "%s\t",                              // title
              pPi->netwop_no,
              start_str,
              stop_str,
              pilStr,
              pPi->parental_rating *2,
              pPi->editorial_rating,
              pStrSoundFormat,
              ((pPi->feature_flags & 0x04) ? 1 : 0),  // wide
              ((pPi->feature_flags & 0x08) ? 1 : 0),  // PAL+
              ((pPi->feature_flags & 0x10) ? 1 : 0),  // digital
              ((pPi->feature_flags & 0x20) ? 1 : 0),  // encrypted
              ((pPi->feature_flags & 0x40) ? 1 : 0),  // live
              ((pPi->feature_flags & 0x80) ? 1 : 0),  // repeat
              ((pPi->feature_flags & 0x100) ? 1 : 0),  // subtitled
              ((pPi->no_themes > 0) ? pPi->themes[0] : 0),
              ((pPi->no_themes > 1) ? pPi->themes[1] : 0),
              ((pPi->no_themes > 2) ? pPi->themes[2] : 0),
              ((pPi->no_themes > 3) ? pPi->themes[3] : 0),
              ((pPi->no_themes > 4) ? pPi->themes[4] : 0),
              ((pPi->no_themes > 5) ? pPi->themes[5] : 0),
              ((pPi->no_themes > 6) ? pPi->themes[6] : 0),
              ((char*) (pPi->off_title != 0) ? PI_GET_TITLE(pPi) : (uchar *) "")
      );

      PiOutput_AppendShortAndLongInfoText(pPi, EpgTabDumpPiInfoTextCb, fp, EpgDbContextIsMerged(pDbContext));
      fprintf(fp, "\n");
   }
}

// ---------------------------------------------------------------------------
// Export network table to MySQL
//
static void EpgTabDumpAi( FILE *fp, const AI_BLOCK * pAi )
{
   const AI_NETWOP *pNetwop;
   const uchar * pCfNetname;
   uchar cni_str[7];
   Tcl_DString ds;
   char * native;
   uint netwop;

   if (pAi != NULL)
   {
      pNetwop = AI_GET_NETWOPS(pAi);

      for (netwop=0; netwop < pAi->netwopCount; netwop++)
      {
         // get user-configured network name
         sprintf(cni_str, "0x%04X", pNetwop->cni);
         pCfNetname = Tcl_GetVar2(interp, "cfnetnames", cni_str, TCL_GLOBAL_ONLY);
         if (pCfNetname != NULL)
         {  // convert the String from Tcl internal format to Latin-1
            native = Tcl_UtfToExternalDString(NULL, pCfNetname, -1, &ds);
         }
         else
            native = NULL;

         fprintf(fp, "%u\t%u\t%d\t%u\t%u\t%u\t%s\n",
                     netwop,
                     pNetwop->cni,
                     pNetwop->lto * 15,
                     pNetwop->dayCount,
                     pNetwop->alphabet,
                     pNetwop->addInfo,
                     ((native != NULL) ? (char*) native : (char*) AI_GET_STR_BY_OFF(pAi, pNetwop->off_name)));

         if (pCfNetname != NULL)
            Tcl_DStringFree(&ds);

         pNetwop += 1;
      }
   }
}

// ---------------------------------------------------------------------------
// Export PDC themes table to MySQL
//
static void EpgTabDumpPdcThemes( FILE *fp )
{
   const uchar * pThemeStr_eng;
   const uchar * pThemeStr_ger;
   const uchar * pThemeStr_fra;
   uint          idx;

   for (idx=0; idx <= 128; idx++)
   {
      pThemeStr_eng = PdcThemeGetByLang(idx, 0);
      pThemeStr_ger = PdcThemeGetByLang(idx, 1);
      pThemeStr_fra = PdcThemeGetByLang(idx, 4);
      if ( (pThemeStr_eng != NULL) && (pThemeStr_ger != NULL) && (pThemeStr_fra != NULL) )
      {
         fprintf(fp, "%u\t%u\t%s\t%s\t%s\n",
                     idx, PdcThemeGetCategory(idx),
                     pThemeStr_eng, pThemeStr_ger, pThemeStr_fra);
      }
   }
}

// ---------------------------------------------------------------------------
// Translate string into dump mode
//
EPGTAB_DUMP_MODE EpgTabDump_GetMode( const char * pModeStr )
{
   EPGTAB_DUMP_MODE  mode = EPGTAB_DUMP_COUNT;

   if (pModeStr != NULL)
   {
      if (strcasecmp("ai", pModeStr) == 0)
         mode = EPGTAB_DUMP_AI;
      else if (strcasecmp("pi", pModeStr) == 0)
         mode = EPGTAB_DUMP_PI;
      else if (strcasecmp("pdc", pModeStr) == 0)
         mode = EPGTAB_DUMP_PDC;
      else if (strcasecmp("xml", pModeStr) == 0)
         mode = EPGTAB_DUMP_XML;
      else
         debug1("EpgTabDump-GetMode: unknown mode: %s", pModeStr);
   }
   else
      debug0("EpgTabDump-GetMode: illegal NULL ptr param");

   return mode;
}

// ---------------------------------------------------------------------------
// Export the complete database in "tab-seprarated" format for SQL import
//
void EpgTabDump_Database( EPGDB_CONTEXT * pDbContext, FILE * fp, EPGTAB_DUMP_MODE mode )
{
   const AI_BLOCK * pAi;
   const PI_BLOCK * pPi;

   EpgDbLockDatabase(pDbContext, TRUE);

   // Dump PDC theme list
   if (mode == EPGTAB_DUMP_PDC)
   {
      EpgTabDumpPdcThemes(fp);
   }
   else
   {
      if (mode == EPGTAB_DUMP_AI)
      {  // Dump application information block
         pAi = EpgDbGetAi(pDbContext);
         if (pAi != NULL)
         {
            EpgTabDumpAi(fp, pAi);
         }
      }
      else
      {  // Dump programme information blocks
         pPi = EpgDbSearchFirstPi(pDbContext, NULL);
         while (pPi != NULL)
         {
            EpgTabDumpPi(fp, pPi, pDbContext);

            pPi = EpgDbSearchNextPi(pDbContext, NULL, pPi);
         }
      }
   }
   EpgDbLockDatabase(pDbContext, FALSE);
}

