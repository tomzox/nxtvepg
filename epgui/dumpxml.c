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
 *  $Id: dumpxml.c,v 1.10 2004/10/31 17:01:13 tom Exp tom $
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

#define IS_XML_DTD5(X) ((X) != EPGTCL_XMLTV_DTD_6)

// 
typedef struct
{
   FILE   * fp;
   int      xmlDtdVersion;
} DUMP_XML_CB_INFO;

// ----------------------------------------------------------------------------
// Print Short- and Long-Info texts for XMLTV
//
static void EpgDumpXml_AppendInfoTextCb( void *vp, const char * pDesc, bool addSeparator )
{
   DUMP_XML_CB_INFO * pCbInfo = vp;
   FILE * fp;
   char * pNewline;

   if ((pCbInfo != NULL) && (pCbInfo->fp != NULL) && (pDesc != NULL))
   {
      fp = pCbInfo->fp;

      if (addSeparator)
      {
         if ( IS_XML_DTD5(pCbInfo->xmlDtdVersion) )
            fprintf(fp, "\n\n");
         else
            fprintf(fp, "</p></desc>\n\t<desc><p>");
      }

      // replace newline characters with paragraph tags
      while ( (pNewline = strchr(pDesc, '\n')) != NULL )
      {
         // print text up to (and excluding) the newline
         *pNewline = 0;  // XXX must not modify const string
         EpgDumpHtml_WriteString(fp, pDesc);

         if ( IS_XML_DTD5(pCbInfo->xmlDtdVersion) )
            fprintf(fp, "\n\n");
         else
            fprintf(fp, "</p>\n<p>");

         // skip to text following the newline
         pDesc = pNewline + 1;
      }
      // write the segment behind the last newline
      EpgDumpHtml_WriteString(fp, pDesc);
   }
}

// ----------------------------------------------------------------------------
// Helper func: read integer from global Tcl var
// - DTD version constants are defined at Tcl script level
//
static int EpgDumpXml_QueryXmlDtdVersion( Tcl_Interp *interp )
{
   Tcl_Obj  * pVarObj;
   int  value;

   pVarObj = Tcl_GetVar2Ex(interp, "dumpxml_format", NULL, TCL_GLOBAL_ONLY);
   if ( (pVarObj == NULL) ||
        (Tcl_GetIntFromObj(interp, pVarObj, &value) != TCL_OK) )
   {
      debug1("EpgDumpXml-QueryLocaltimeSetting: failed to parse Tcl var 'dumpxml_format': %s", ((pVarObj != NULL) ? Tcl_GetString(pVarObj) : "*undef*"));
      value = EPGTCL_XMLTV_DTD_5_GMT;
   }
   return value;
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
                                       time_t then, bool xmlDtdVersion )
{
   size_t len;
   sint   lto;
   char   ltoSign;

   if (xmlDtdVersion == EPGTCL_XMLTV_DTD_5_LTZ)
   {
      // print times in localtime
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
   else if (xmlDtdVersion == EPGTCL_XMLTV_DTD_5_GMT)
   {
      strftime(pBuf, maxLen, "%Y%m%d%H%M%S +0000", gmtime(&then));
   }
   else if (xmlDtdVersion == EPGTCL_XMLTV_DTD_6)
   {
      strftime(pBuf, maxLen, "%Y-%m-%dT%H:%M:%SZ", gmtime(&then));
   }
   else
      debug1("EpgDumpXml-PrintTimestamp: invalid XMLTV DTD version %d", xmlDtdVersion);
}

// ----------------------------------------------------------------------------
// Write XML file header
//
static void EpgDumpXml_WriteHeader( EPGDB_CONTEXT * pDbContext, const AI_BLOCK * pAiBlock,
                                    FILE * fp, bool xmlDtdVersion )
{
   uchar   start_str[50];
   time_t  lastAiUpdate;

   // content provider string
   comm[0] = 0;
   if (EpgDbContextIsMerged(pDbContext) == FALSE)
   {
      EpgDumpHtml_RemoveQuotes(AI_GET_NETWOP_NAME(pAiBlock, pAiBlock->thisNetwop), comm, TCL_COMM_BUF_SIZE - 2);
      strcat(comm, "/");
   }
   EpgDumpHtml_RemoveQuotes(AI_GET_SERVICENAME(pAiBlock), comm + strlen(comm), TCL_COMM_BUF_SIZE - strlen(comm));

   // date when data was captured
   lastAiUpdate = EpgDbGetAiUpdateTime(pDbContext);
   EpgDumpXml_PrintTimestamp(start_str, sizeof(start_str), lastAiUpdate, xmlDtdVersion);

   if ( IS_XML_DTD5(xmlDtdVersion) )
   {
      fprintf(fp, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"
                  "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n"
                  "<tv generator-info-name=\"nxtvepg/" EPG_VERSION_STR "\" "
                       "generator-info-url=\"" NXTVEPG_URL "\" "
                       "source-info-name=\"nexTView ");
      EpgDumpHtml_WriteString(fp, comm);
      fprintf(fp, "\" date=\"%s\">\n", start_str);
   }
   else // XML DTD 6
   {
      fprintf(fp, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"
                  "<!DOCTYPE tv SYSTEM \"xmltv-0.6.dtd\">\n"
                  "<tv>\n"
                  "<about date=\"%s\">\n"
                  "\t<copying>\n"
                  "\t\t<p>\n"
                  "Copyright by nexTView EPG content providers: ", start_str);
      EpgDumpHtml_WriteString(fp, comm);
      fprintf(fp, "\n\t\t</p>\n"
                  "\t</copying>\n"
                  "\t<generator-info>\n"
                  "\t\t<link href=\"" NXTVEPG_URL "\">\n"
                  "\t\t\t<text>nxtvepg/" EPG_VERSION_STR "</text>\n"
                  "\t\t</link>\n"
                  "\t</generator-info>\n"
                  "</about>\n");
   }
}

// ----------------------------------------------------------------------------
// Write XML channel table
//
static void EpgDumpXml_WriteChannels( EPGDB_CONTEXT * pDbContext, const AI_BLOCK * pAiBlock,
                                      FILE * fp, bool xmlDtdVersion )
{
   const AI_NETWOP * pNetwop;
   const uchar     * pCfNetname;
   const char      * native;
   Tcl_DString       ds;
   uchar cni_str[10];
   uint  netwop;

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

      fprintf(fp, "<channel id=\"CNI%04X\">\n", pNetwop->cni);

      if ( IS_XML_DTD5(xmlDtdVersion) == FALSE )
      {
         fprintf(fp, "\t<number num=\"%d\" />\n", netwop);
      }

      fprintf(fp, "\t<display-name>");
      EpgDumpHtml_WriteString(fp, native);
      fprintf(fp, "</display-name>\n"
                  "</channel>\n");

      if (pCfNetname != NULL)
         Tcl_DStringFree(&ds);
      pNetwop += 1;
   }
}

// ----------------------------------------------------------------------------
// Write a single programme description
//
static void EpgDumpXml_WriteProgramme( EPGDB_CONTEXT * pDbContext, const AI_BLOCK * pAiBlock,
                                       const PI_BLOCK * pPiBlock, FILE * fp, int xmlDtdVersion )
{
   DUMP_XML_CB_INFO cbInfo;
   const char  * pThemeStr;
   struct tm     vpsTime;
   uint  idx;
   uchar start_str[50];
   uchar stop_str[50];
   uchar tmp_str[50];

   // start & stop times, channel ID
   EpgDumpXml_PrintTimestamp(start_str, sizeof(start_str), pPiBlock->start_time, xmlDtdVersion);
   EpgDumpXml_PrintTimestamp(stop_str, sizeof(stop_str), pPiBlock->stop_time, xmlDtdVersion);

   if ( IS_XML_DTD5(xmlDtdVersion) )
   {
      if (EpgDbGetVpsTimestamp(&vpsTime, pPiBlock->pil, pPiBlock->start_time))
      {
         int lto = EpgLtoGet(pPiBlock->start_time);
         strftime(tmp_str, sizeof(tmp_str) - 7, "pdc-start=\"%Y%m%d%H%M%S", &vpsTime);
         sprintf(tmp_str + strlen(tmp_str), " %+03d%02d\"", lto / (60*60), abs(lto / 60) % 60);
      }
      else
         tmp_str[0] = 0;

      fprintf(fp, "<programme start=\"%s\" stop=\"%s\" %s channel=\"CNI%04X\">\n",
                  start_str, stop_str, tmp_str, AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
   }
   else // XML DTD 6
   {
      fprintf(fp, "<timeslot start=\"%s\" stop=\"%s\" channel=\"CNI%04X\"%s>\n",
                  start_str, stop_str, AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni,
                  ((pPiBlock->feature_flags & PI_FEATURE_LIVE) ? " liveness=\"live\"" : ""));

      if (EpgDbGetVpsTimestamp(&vpsTime, pPiBlock->pil, pPiBlock->start_time))
      {
         // VPS/PDC timestamp is in localtime (hence no "Z" suffix)
         strftime(tmp_str,  sizeof(tmp_str), "%Y-%m-%dT%H:%M:%S", &vpsTime);
         fprintf(fp, "  <code-time system=\"vps\" start=\"%s\" />\n", tmp_str);
      }

      fprintf(fp, "  <programme id=\"C%04X.B%04X.S%d\"%s>\n",
                  AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni,
                  pPiBlock->block_no, (int)pPiBlock->start_time,
                  ((pPiBlock->feature_flags & PI_FEATURE_REPEAT) ? " newness=\"repeat\"" : ""));
   }

   // programme title and description (quoting "<", ">" and "&" characters)
   PiOutput_PrintColumnItem(pPiBlock, PIBOX_COL_TITLE, comm, TCL_COMM_BUF_SIZE);
   fprintf(fp, "\t<title>");
   EpgDumpHtml_WriteString(fp, comm);
   fprintf(fp, "</title>\n");
   if ( PI_HAS_SHORT_INFO(pPiBlock) || PI_HAS_LONG_INFO(pPiBlock) )
   {
      cbInfo.fp = fp;
      cbInfo.xmlDtdVersion = xmlDtdVersion;

      if ( IS_XML_DTD5(xmlDtdVersion) )
      {
         fprintf(fp, "\t<desc>");
         PiDescription_AppendShortAndLongInfoText(pPiBlock, EpgDumpXml_AppendInfoTextCb, &cbInfo, EpgDbContextIsMerged(pDbContext));
         fprintf(fp, "</desc>\n");
      }
      else
      {
         fprintf(fp, "\t<desc><p>");
         PiDescription_AppendShortAndLongInfoText(pPiBlock, EpgDumpXml_AppendInfoTextCb, &cbInfo, EpgDbContextIsMerged(pDbContext));
         fprintf(fp, "</p></desc>\n");
      }
   }

   // theme categories
   for (idx=0; idx < pPiBlock->no_themes; idx++)
   {
      if ( IS_XML_DTD5(xmlDtdVersion) )
         fprintf(fp, "\t<category>");
      else
         fprintf(fp, "\t<category system=\"pdc\" code=\"%d\">", pPiBlock->themes[idx]);

      pThemeStr = PdcThemeGet(pPiBlock->themes[idx]);
      if (pThemeStr != NULL)
         EpgDumpHtml_WriteString(fp, pThemeStr);

      fprintf(fp, "</category>\n");
   }

   // attributes
   if ( IS_XML_DTD5(xmlDtdVersion) )
   {
      PiOutput_PrintColumnItem(pPiBlock, PIBOX_COL_SOUND, comm, TCL_COMM_BUF_SIZE);
      if (comm[0] != 0)
         fprintf(fp, "\t<audio>\n\t\t<stereo>%s</stereo>\n\t</audio>\n", comm);

      PiOutput_PrintColumnItem(pPiBlock, PIBOX_COL_SUBTITLES, comm, TCL_COMM_BUF_SIZE);
      if (comm[0] != 0)
         fprintf(fp, "\t<subtitles type=\"teletext\" />\n");

      if (pPiBlock->parental_rating == 1)
         fprintf(fp, "\t<rating system=\"age\">\n\t\t<value>general</value>\n\t</rating>\n");
      else if (pPiBlock->parental_rating > 0)
         fprintf(fp, "\t<rating system=\"age\">\n\t\t<value>%d</value>\n\t</rating>\n",
                     pPiBlock->parental_rating * 2);
      if (pPiBlock->editorial_rating > 0)
         fprintf(fp, "\t<star-rating>\n\t\t<value>%d/7</value>\n\t</star-rating>\n",
                     pPiBlock->editorial_rating);

      fprintf(fp, "</programme>\n");
   }
   else
   {
      // sorting criteria
      for (idx=0; idx < pPiBlock->no_sortcrit; idx++)
      {
         fprintf(fp, "\t<category system=\"nextview/sorting_criterion\" code=\"%d\"></category>\n",
                     pPiBlock->sortcrits[idx]);
      }

      if ((pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK) == PI_FEATURE_SOUND_STEREO)
      {
         fprintf(fp, "\t<audio><stereo /></audio>\n");
      }
      else if ((pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK) == PI_FEATURE_SOUND_SURROUND)
      {
         fprintf(fp, "\t<audio><stereo /></audio>\n");
      }
      else if ((pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK) == PI_FEATURE_SOUND_2CHAN)
      {
         fprintf(fp, "\t<audio><mono channel=\"A\"/></audio>\n");
         fprintf(fp, "\t<audio><mono channel=\"B\"/></audio>\n");
      }

      if ((pPiBlock->feature_flags & (PI_FEATURE_PAL_PLUS | PI_FEATURE_FMT_WIDE)) != 0)
      {
         fprintf(fp, "\t<video>\n");
         fprintf(fp, "\t\t<aspect x=\"16\" y=\"9\" />\n");
         if (pPiBlock->feature_flags & PI_FEATURE_PAL_PLUS)
            fprintf(fp, "\t\t<quality>PAL+</quality>\n");
         fprintf(fp, "\t</video>\n");
      }

      if (pPiBlock->feature_flags & PI_FEATURE_SUBTITLES)
      {
         fprintf(fp, "\t<subtitles><teletext /></subtitles>\n");
      }

      if (pPiBlock->parental_rating == 1)
         fprintf(fp, "\t<classification system=\"age\">\n\t\t<text>general</text>\n\t</classification>\n");
      else if (pPiBlock->parental_rating > 0)
         fprintf(fp, "\t<classification system=\"age\">\n\t\t<text>%d</text>\n\t</classification>\n",
                     pPiBlock->parental_rating * 2);

      if (pPiBlock->editorial_rating > 0)
         fprintf(fp, "\t<star-rating stars=\"%d\" out-of=\"7\"></star-rating>\n",
                     pPiBlock->editorial_rating);

      fprintf(fp, "  </programme>\n</timeslot>\n");
   }
}

// ----------------------------------------------------------------------------
// Dump programme titles and/or descriptions into file in XMLTV format
//
void EpgDumpXml_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp )
{
   const AI_BLOCK  * pAiBlock;
   const PI_BLOCK  * pPiBlock;
   int   xmlDtdVersion;

   xmlDtdVersion = EpgDumpXml_QueryXmlDtdVersion(interp);

   if (fp != NULL)
   {
      EpgDbLockDatabase(pDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pDbContext);
      if (pAiBlock != NULL)
      {
         // header with source info
         EpgDumpXml_WriteHeader(pDbContext, pAiBlock, fp, xmlDtdVersion);

         // channel table
         EpgDumpXml_WriteChannels(pDbContext, pAiBlock, fp, xmlDtdVersion);

         // loop across all PI and dump their info
         pPiBlock = EpgDbSearchFirstPi(pDbContext, NULL);
         while (pPiBlock != NULL)
         {
            EpgDumpXml_WriteProgramme(pDbContext, pAiBlock, pPiBlock, fp, xmlDtdVersion);

            pPiBlock = EpgDbSearchNextPi(pDbContext, NULL, pPiBlock);
         }

         // footer
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

