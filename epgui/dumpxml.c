/*
 *  Nextview GUI: Output of programme data in XML
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
 *    This module implements methods to export PI in XML.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: dumpxml.c,v 1.4 2004/03/28 13:38:46 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <tcl.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgctl/epgversion.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbmerge.h"
#include "epgui/epgmain.h"
#include "epgui/pdc_themes.h"
#include "epgui/pifilter.h"
#include "epgui/pidescr.h"
#include "epgui/pioutput.h"
#include "epgui/dumphtml.h"
#include "epgui/dumpxml.h"
#include "epgtcl/dlg_dump.h"


// ----------------------------------------------------------------------------
// Print Short- and Long-Info texts for XMLTV
//
static void EpgDumpXml_AppendInfoTextCb( void *vp, const char * pDesc, bool addSeparator )
{
   FILE * fp = (FILE *) vp;
   char * pNewline;

   if ((fp != NULL) && (pDesc != NULL))
   {
      if (addSeparator)
         fprintf(fp, "\n\n");

      // replace newline characters with paragraph tags
      while ( (pNewline = strchr(pDesc, '\n')) != NULL )
      {
         // print text up to (and excluding) the newline
         *pNewline = 0;  // XXX must not modify const string
         EpgDumpHtml_WriteString(fp, pDesc);
         fprintf(fp, "\n\n");
         // skip to text following the newline
         pDesc = pNewline + 1;
      }
      // write the segement behind the last newline
      EpgDumpHtml_WriteString(fp, pDesc);

      fprintf(fp, "\n");
   }
}

// ----------------------------------------------------------------------------
// Helper func: read integer from global Tcl var
// - DTD version constants are defined at Tcl script level
//
static bool EpgDumpXml_QueryLocaltimeSetting( Tcl_Interp *interp )
{
   Tcl_Obj  * pVarObj;
   int  value;

   pVarObj = Tcl_GetVar2Ex(interp, "dumpxml_format", NULL, TCL_GLOBAL_ONLY);
   if ( (pVarObj == NULL) ||
        (Tcl_GetBooleanFromObj(interp, pVarObj, &value) != TCL_OK) )
   {
      debug1("EpgDumpXml-QueryLocaltimeSetting: failed to parse Tcl var 'dumpxml_format': %s", ((pVarObj != NULL) ? Tcl_GetString(pVarObj) : "*undef*"));
      value = EPGTCL_XMLTV_DTD_5;
   }
   return (value == EPGTCL_XMLTV_DTD_5_LTZ);
}

// ----------------------------------------------------------------------------
// Write timestamp into string
// - definition of date/time format in XMLTV DTD 0.5:
//   "All dates and times in this DTD follow the same format, loosely based
//   on ISO 8601.  They can be 'YYYYMMDDhhmmss' or some initial
//   substring, for example if you only know the year and month you can
//   have 'YYYYMM'.  You can also append a timezone to the end; if no
//   explicit timezone is given, UTC is assumed.  Examples:
//   '200007281733 BST', '200209', '19880523083000 +0300'.  (BST == +0100.)"
// - unfortunately the authors of some XMLTV parsers have overlooked this
//   paragraph, so we need as a work-around the possibility to export times
//   in the local time zone
//
static void EpgDumpXml_PrintTimestamp( char * pBuf, uint maxLen,
                                       time_t then, bool useLocaltime )
{
   size_t len;
   sint   lto;
   char   ltoSign;

   if (useLocaltime)
   {  // print times in localtime
      lto = EpgLtoGet(then) / 60;
      if (lto < 0)
      {
         lto = 0 - lto;
         ltoSign = '-';
      }
      else
         ltoSign = '+';

      len = strftime(pBuf, maxLen, "%Y%m%d%H%M%S", localtime(&then));
      if (len + 1 + 6 <= maxLen)
      {
         sprintf(pBuf + len, " %c%04d", ltoSign, lto);
      }
   }
   else
   {  // print all times in GMT
      strftime(pBuf, maxLen, "%Y%m%d%H%M%S +0000", gmtime(&then));
   }
}

// ----------------------------------------------------------------------------
// Dump programme titles and/or descriptions into file in XMLTV format
//
void EpgDumpXml_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp )
{
   const AI_BLOCK  * pAiBlock;
   const AI_NETWOP * pNetwop;
   const PI_BLOCK  * pPiBlock;
   const uchar     * pCfNetname;
   const char      * native;
   const char      * pThemeStr;
   Tcl_DString       ds;
   time_t            lastAiUpdate;
   struct tm         vpsTime;
   bool  useLocaltime;
   uint  netwop;
   uint  themeIdx, langIdx;
   uchar start_str[50];
   uchar stop_str[50];
   uchar vps_str[50];
   uchar cni_str[10];

   useLocaltime = EpgDumpXml_QueryLocaltimeSetting(interp);

   if (fp != NULL)
   {
      //PdcThemeSetLanguage(4);
      EpgDbLockDatabase(pDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pDbContext);
      if (pAiBlock != NULL)
      {
         // write file header
         fprintf(fp, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"
                     "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n"
                     "<tv generator-info-name=\"nxtvepg/" EPG_VERSION_STR "\" "
                          "generator-info-url=\"" NXTVEPG_URL "\" "
                          "source-info-name=\"nexTView ");
         comm[0] = 0;
         if (EpgDbContextIsMerged(pDbContext) == FALSE)
         {
            EpgDumpHtml_RemoveQuotes(AI_GET_NETWOP_NAME(pAiBlock, pAiBlock->thisNetwop), comm, TCL_COMM_BUF_SIZE - 2);
            strcat(comm, "/");
         }
         EpgDumpHtml_RemoveQuotes(AI_GET_SERVICENAME(pAiBlock), comm + strlen(comm), TCL_COMM_BUF_SIZE - strlen(comm));
         EpgDumpHtml_WriteString(fp, comm);

         lastAiUpdate = EpgDbGetAiUpdateTime(pDbContext);
         EpgDumpXml_PrintTimestamp(start_str, sizeof(start_str), lastAiUpdate, useLocaltime);
         fprintf(fp, "\" date=\"%s\">\n", start_str);

         // dump the network table
         pNetwop = AI_GET_NETWOPS(pAiBlock);
         for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
         {
            sprintf(cni_str, "0x%04X", pNetwop->cni);
            pCfNetname = Tcl_GetVar2(interp, "cfnetnames", cni_str, TCL_GLOBAL_ONLY);
            if (pCfNetname != NULL)
            {  // convert the String from Tcl internal format to Latin-1
               native = Tcl_UtfToExternalDString(NULL, pCfNetname, -1, &ds);
            }
            else
               native = AI_GET_STR_BY_OFF(pAiBlock, pNetwop->off_name);

            fprintf(fp, "<channel id=\"CNI%04X\">\n"
                        "\t<display-name>%s</display-name>\n"
                        "</channel>\n",
                        pNetwop->cni, native);

            if (pCfNetname != NULL)
               Tcl_DStringFree(&ds);
            pNetwop += 1;
         }

         // loop across all PI and dump their info
         pPiBlock = EpgDbSearchFirstPi(pDbContext, NULL);
         while (pPiBlock != NULL)
         {
            // start & stop times, channel ID
            EpgDumpXml_PrintTimestamp(start_str, sizeof(start_str), pPiBlock->start_time, useLocaltime);
            EpgDumpXml_PrintTimestamp(stop_str, sizeof(stop_str), pPiBlock->stop_time, useLocaltime);
            if (EpgDbGetVpsTimestamp(&vpsTime, pPiBlock->pil, pPiBlock->start_time))
            {
               int lto = EpgLtoGet(pPiBlock->start_time);
               strftime(vps_str, sizeof(vps_str) - 7, "pdc-start=\"%Y%m%d%H%M%S", &vpsTime);
               sprintf(vps_str + strlen(vps_str), " %+03d%02d\"", lto / (60*60), abs(lto / 60) % 60);
            }
            else
               vps_str[0] = 0;
            fprintf(fp, "<programme start=\"%s\" stop=\"%s\" %s channel=\"CNI%04X\">\n",
                        start_str, stop_str, vps_str, AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);

            // programme title and description (quoting "<", ">" and "&" characters)
            PiOutput_PrintColumnItem(pPiBlock, PIBOX_COL_TITLE, comm, TCL_COMM_BUF_SIZE);
            fprintf(fp, "\t<title>");
            EpgDumpHtml_WriteString(fp, comm);
            fprintf(fp, "</title>\n");
            if ( PI_HAS_SHORT_INFO(pPiBlock) || PI_HAS_LONG_INFO(pPiBlock) )
            {
               fprintf(fp, "\t<desc>\n");
               PiDescription_AppendShortAndLongInfoText(pPiBlock, EpgDumpXml_AppendInfoTextCb, fp, EpgDbContextIsMerged(pDbContext));
               fprintf(fp, "\t</desc>\n");
            }

            // theme categories
            for (themeIdx=0; themeIdx < pPiBlock->no_themes; themeIdx++)
            {
               #define THEME_LANG_COUNT 3
               const struct { uint id; const uchar * label; } langTab[THEME_LANG_COUNT] =
               { {0, "en"}, {1, "de"}, {4, "fr"} };

               for (langIdx=0; langIdx < THEME_LANG_COUNT; langIdx++)
               {
                  pThemeStr = PdcThemeGetByLang(pPiBlock->themes[themeIdx], langTab[langIdx].id);
                  if (pThemeStr != NULL)
                  {
                     fprintf(fp, "\t<category lang=\"%s\">", langTab[langIdx].label);
                     EpgDumpHtml_WriteString(fp, pThemeStr);
                     fprintf(fp, "</category>\n");
                  }
               }
            }

            // attributes
            PiOutput_PrintColumnItem(pPiBlock, PIBOX_COL_SOUND, comm, TCL_COMM_BUF_SIZE);
            if (comm[0] != 0)
               fprintf(fp, "\t<audio>\n\t\t<stereo>%s</stereo>\n\t</audio>\n", comm);
            PiOutput_PrintColumnItem(pPiBlock, PIBOX_COL_SUBTITLES, comm, TCL_COMM_BUF_SIZE);
            if (comm[0] != 0)
               fprintf(fp, "\t<subtitles type=\"teletext\" />\n");
            if (pPiBlock->parental_rating > 0)
               fprintf(fp, "\t<rating system=\"age\">\n\t\t<value>%d</value>\n\t</rating>\n",
                           pPiBlock->parental_rating * 2);
            if (pPiBlock->editorial_rating > 0)
               fprintf(fp, "\t<star-rating>\n\t\t<value>%d/7</value>\n\t</star-rating>\n",
                           pPiBlock->editorial_rating);

            fprintf(fp, "</programme>\n");

            pPiBlock = EpgDbSearchNextPi(pDbContext, NULL, pPiBlock);
         }
         fprintf(fp, "</tv>\n");
      }
      EpgDbLockDatabase(pDbContext, FALSE);
   }
   else
      fatal0("EpgDumpXml-Standalone: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Dump programme titles and/or descriptions into file in XMLTV format
//
static int EpgDumpXml_DumpDatabase( ClientData ttp, Tcl_Interp *interp,int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_DumpXml <file-name>";
   const char * pFileName;
   FILE       * fpDst;
   Tcl_DString  ds;
   int  result;

   if ( (objc != 1+1) || (pFileName = Tcl_GetString(objv[1])) == NULL )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pFileName = Tcl_UtfToExternalDString(NULL, pFileName, -1, &ds);

      // Open source and create destination XML file
      fpDst = fopen(pFileName, "w");
      if (fpDst != NULL)
      {
         EpgDumpXml_Standalone(pUiDbContext, fpDst);
         fclose(fpDst);
      }
      else
      {  // access, create or truncate failed -> notify the user
         sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumpxml -message \"Failed to open file '%s' for writing: %s\"",
                       pFileName, strerror(errno));
         eval_check(interp, comm);
         Tcl_ResetResult(interp);
      }

      Tcl_DStringFree(&ds);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Free resources allocated by this module (cleanup during program exit)
//
void EpgDumpXml_Destroy( void )
{
}

// ----------------------------------------------------------------------------
// Create the Tcl/Tk commands provided by this module
// - this should be called only once during start-up
//
void EpgDumpXml_Init( void )
{
   Tcl_CreateObjCommand(interp, "C_DumpXml", EpgDumpXml_DumpDatabase, (ClientData) NULL, NULL);
}

