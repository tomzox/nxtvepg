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
 *  $Id: dumptext.c,v 1.12 2005/01/01 18:18:23 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <string.h>
#include <stdio.h>
#include <errno.h>
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
#include "epgui/pidescr.h"
#include "epgui/dumptext.h"


#define NETNAME_LENGTH 6
#define NETNAME_LENGTH_STR "6"
#define NETNAME_LENGTH0 (NETNAME_LENGTH+1)

// ----------------------------------------------------------------------------
// Print Short- and Long-Info texts or separators
//
static void DumpText_PiInfoTextCb( void * vp, const char * pDesc, bool addSeparator )
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
static void DumpText_Pi( FILE *fp, const PI_BLOCK * pPi, const EPGDB_CONTEXT * pDbContext )
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

      switch (pPi->feature_flags & PI_FEATURE_SOUND_MASK)
      {
        case  PI_FEATURE_SOUND_MONO: pStrSoundFormat = "mono"; break;
        case  PI_FEATURE_SOUND_2CHAN: pStrSoundFormat = "2-chan"; break;
        case  PI_FEATURE_SOUND_STEREO: pStrSoundFormat = "stereo"; break;
        case  PI_FEATURE_SOUND_SURROUND: pStrSoundFormat = "surround"; break;
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
              ((pPi->feature_flags & PI_FEATURE_FMT_WIDE) ? 1 : 0),
              ((pPi->feature_flags & PI_FEATURE_PAL_PLUS) ? 1 : 0),
              ((pPi->feature_flags & PI_FEATURE_DIGITAL) ? 1 : 0),
              ((pPi->feature_flags & PI_FEATURE_ENCRYPTED) ? 1 : 0),
              ((pPi->feature_flags & PI_FEATURE_LIVE) ? 1 : 0),
              ((pPi->feature_flags & PI_FEATURE_REPEAT) ? 1 : 0),
              ((pPi->feature_flags & PI_FEATURE_SUBTITLES) ? 1 : 0),
              ((pPi->no_themes > 0) ? pPi->themes[0] : 0),
              ((pPi->no_themes > 1) ? pPi->themes[1] : 0),
              ((pPi->no_themes > 2) ? pPi->themes[2] : 0),
              ((pPi->no_themes > 3) ? pPi->themes[3] : 0),
              ((pPi->no_themes > 4) ? pPi->themes[4] : 0),
              ((pPi->no_themes > 5) ? pPi->themes[5] : 0),
              ((pPi->no_themes > 6) ? pPi->themes[6] : 0),
              ((char*) (pPi->off_title != 0) ? PI_GET_TITLE(pPi) : (uchar *) "")
      );

      PiDescription_AppendShortAndLongInfoText(pPi, DumpText_PiInfoTextCb, fp, EpgDbContextIsMerged(pDbContext));
      fprintf(fp, "\n");
   }
}

// ---------------------------------------------------------------------------
// Export network table to MySQL
//
static void DumpText_Ai( FILE *fp, const AI_BLOCK * pAi )
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
static void DumpText_PdcThemes( FILE *fp )
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
static DUMP_TEXT_MODE DumpText_GetMode( const char * pModeStr )
{
   DUMP_TEXT_MODE  mode = DUMP_TEXT_COUNT;

   if (pModeStr != NULL)
   {
      if (strcasecmp("ai", pModeStr) == 0)
         mode = DUMP_TEXT_AI;
      else if (strcasecmp("pi", pModeStr) == 0)
         mode = DUMP_TEXT_PI;
      else if (strcasecmp("pdc", pModeStr) == 0)
         mode = DUMP_TEXT_PDC;
      else
         debug1("DumpText-GetMode: unknown mode: %s", pModeStr);
   }
   else
      debug0("DumpText-GetMode: illegal NULL ptr param");

   return mode;
}

// ---------------------------------------------------------------------------
// Export the complete database in "tab-seprarated" format for SQL import
//
void EpgDumpText_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp, DUMP_TEXT_MODE mode )
{
   const AI_BLOCK * pAi;
   const PI_BLOCK * pPi;

   EpgDbLockDatabase(pDbContext, TRUE);

   // Dump PDC theme list
   if (mode == DUMP_TEXT_PDC)
   {
      DumpText_PdcThemes(fp);
   }
   else
   {
      if (mode == DUMP_TEXT_AI)
      {  // Dump application information block
         pAi = EpgDbGetAi(pDbContext);
         if (pAi != NULL)
         {
            DumpText_Ai(fp, pAi);
         }
      }
      else
      {  // Dump programme information blocks
         pPi = EpgDbSearchFirstPi(pDbContext, NULL);
         while (pPi != NULL)
         {
            DumpText_Pi(fp, pPi, pDbContext);

            pPi = EpgDbSearchNextPi(pDbContext, NULL, pPi);
         }
      }
   }
   EpgDbLockDatabase(pDbContext, FALSE);
}

// ----------------------------------------------------------------------------
// Dump the database in TAB-separated format
//
static int EpgDumpText_Database( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_DumpTabsDatabase <file-name> <type>";
   DUMP_TEXT_MODE mode;
   const char * pFileName;
   Tcl_DString ds;
   FILE *fp;
   int result;

   if (objc != 1+2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( (pFileName = Tcl_GetString(objv[1])) == NULL )
   {  // internal error: can not get filename string
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      mode = DumpText_GetMode(Tcl_GetString(objv[2]));
      if (mode != DUMP_TEXT_COUNT)
      {
         if (Tcl_GetCharLength(objv[1]) > 0)
         {
            pFileName = Tcl_UtfToExternalDString(NULL, pFileName, -1, &ds);
            fp = fopen(pFileName, "w");
            if (fp != NULL)
            {  // file created successfully -> start dump
               EpgDumpText_Standalone(pUiDbContext, fp, mode);

               fclose(fp);
            }
            else
            {  // access, create or truncate failed -> inform the user
               sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumptabs -message \"Failed to open file '%s' for writing: %s\"",
                             Tcl_GetString(objv[1]), strerror(errno));
               eval_check(interp, comm);
               Tcl_ResetResult(interp);
            }
            Tcl_DStringFree(&ds);
         }
         else
         {  // no file name given -> dump to stdout
            EpgDumpText_Standalone(pUiDbContext, stdout, mode);
         }

         result = TCL_OK;
      }
      else
      {  // unsupported mode (internal error, since the GUI should use radio buttons)
         Tcl_SetResult(interp, "C_DumpTabsDatabase: illegal type keyword", TCL_STATIC);
         result = TCL_ERROR;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Free resources allocated by this module (cleanup during program exit)
//
void EpgDumpText_Destroy( void )
{
}

// ----------------------------------------------------------------------------
// Create the Tcl/Tk commands provided by this module
// - this should be called only once during start-up
//
void EpgDumpText_Init( void )
{
   Tcl_CreateObjCommand(interp, "C_DumpTabsDatabase", EpgDumpText_Database, (ClientData) NULL, NULL);
}

