/*
 *  Export Nextview database in XMLTV format
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
 *    This module implements methods to export the database in XMLTV format.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: dumpxml.c,v 1.20 2007/12/29 15:20:15 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgctl/epgversion.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgsetup.h"
#include "epgui/rcfile.h"
#include "epgui/pidescr.h"
#include "epgui/pdc_themes.h"
#include "epgui/dumpxml.h"

#define IS_XML_DTD5(X) ((X) != DUMP_XMLTV_DTD_6)

#ifdef USE_UTF8
#define XML_ENCODING_NAME "\"UTF8\""
#else
#define XML_ENCODING_NAME "\"ISO-8859-1\""
#endif

// structure used to pass params through to description text output callback
typedef struct
{
   FILE         * fp;
   DUMP_XML_MODE  xmlDtdVersion;
} DUMP_XML_CB_INFO;

// ----------------------------------------------------------------------------
// Append a text string to the HTML output file, while quoting HTML special chars
// - string is terminated wither by zero or by strlen parameter;
//   special value "-1" can be used to ignore strlen
// - HTML special chars are: < > &  converted to: &lt; &gt; &amp;
//
void EpgDumpXml_HtmlWriteString( FILE *fp, const char * pText, sint strlen )
{
   char outbuf[256], *po;
   uint outlen;

   po = outbuf;
   outlen = sizeof(outbuf);
   while ((*pText != 0) && (strlen-- != 0))
   {
      if (outlen < 6)
      {  // buffer is almost full -> flush it
         fwrite(outbuf, sizeof(char), sizeof(outbuf) - outlen, fp);
         po = outbuf;
         outlen = sizeof(outbuf);
      }

      if (*pText == '<')
      {
         pText++;
         strcpy(po, "&lt;");
         po     += 4;
         outlen -= 4;
      }
      else if (*pText == '>')
      {
         pText++;
         strcpy(po, "&gt;");
         po     += 4;
         outlen -= 4;
      }
      else if (*pText == '&')
      {
         pText++;
         strcpy(po, "&amp;");
         po     += 5;
         outlen -= 5;
      }
      else
      {
         *(po++) = *(pText++);
         outlen -= 1;
      }
   }

   // flush the output buffer
   if (outlen != sizeof(outbuf))
   {
      fwrite(outbuf, sizeof(char), sizeof(outbuf) - outlen, fp);
   }
}

// ----------------------------------------------------------------------------
// Copy string with double quotes (") replaced with single quotes (')
// - for use inside HTML tags, e.g. <META source="...">
//
void EpgDumpXml_HtmlRemoveQuotes( const uchar * pStr, uchar * pBuf, uint maxOutLen )
{
   while ((*pStr != 0) && (maxOutLen > 1))
   {
      if (*pStr == '"')
      {
         *(pBuf++) = '\'';
         pStr++;
      }
      else
         *(pBuf++) = *(pStr++);

      maxOutLen -= 1;
   }

   if (maxOutLen > 0)
      *pBuf = 0;
}

// ----------------------------------------------------------------------------
// Print Short- and Long-Info texts for XMLTV
//
static void EpgDumpXml_AppendInfoTextCb( void *vp, const char * pDesc, bool addSeparator )
{
   DUMP_XML_CB_INFO * pCbInfo = vp;
   FILE * fp;
   const char * pNewline;

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
         EpgDumpXml_HtmlWriteString(fp, pDesc, pNewline - pDesc);

         if ( IS_XML_DTD5(pCbInfo->xmlDtdVersion) )
            fprintf(fp, "\n\n");
         else
            fprintf(fp, "</p>\n<p>");

         // skip to text following the newline
         pDesc = pNewline + 1;
      }
      // write the segment behind the last newline
      EpgDumpXml_HtmlWriteString(fp, pDesc, -1);
   }
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
                                       time_t then, DUMP_XML_MODE xmlDtdVersion )
{
   size_t len;
   sint   lto;
   char   ltoSign;

   if (xmlDtdVersion == DUMP_XMLTV_DTD_5_LTZ)
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
         sprintf(pBuf + len, " %c%02d%02d", ltoSign, lto/60, lto%60);
      }
   }
   else if (xmlDtdVersion == DUMP_XMLTV_DTD_5_GMT)
   {
      strftime(pBuf, maxLen, "%Y%m%d%H%M%S +0000", gmtime(&then));
   }
   else if (xmlDtdVersion == DUMP_XMLTV_DTD_6)
   {
      strftime(pBuf, maxLen, "%Y-%m-%dT%H:%M:%SZ", gmtime(&then));
   }
   else
   {
      debug1("EpgDumpXml-PrintTimestamp: invalid XMLTV DTD version %d", xmlDtdVersion);
      if (maxLen >= 1)
         *pBuf = 0;
   }
}

// ----------------------------------------------------------------------------
// Write XML file header
//
static void EpgDumpXml_WriteHeader( EPGDB_CONTEXT * pDbContext,
                                    const AI_BLOCK * pAiBlock, const OI_BLOCK * pOiBlock,
                                    FILE * fp, DUMP_XML_MODE xmlDtdVersion )
{
   uchar   src_str[200];
   uchar   start_str[50];
   time_t  lastAiUpdate;

   // content provider string
   src_str[0] = 0;
   if ( (EpgDbContextIsMerged(pDbContext) == FALSE) &&
        (EpgDbContextIsXmltv(pDbContext) == FALSE) )
   {
      EpgDumpXml_HtmlRemoveQuotes(AI_GET_NETWOP_NAME(pAiBlock, pAiBlock->thisNetwop), src_str, sizeof(src_str) - 2);
      strcat(src_str, "/");
   }
   EpgDumpXml_HtmlRemoveQuotes(AI_GET_SERVICENAME(pAiBlock), src_str + strlen(src_str), sizeof(src_str) - strlen(src_str));

   // date when data was captured
   lastAiUpdate = EpgDbGetAiUpdateTime(pDbContext);
   EpgDumpXml_PrintTimestamp(start_str, sizeof(start_str), lastAiUpdate, xmlDtdVersion);

   if ( IS_XML_DTD5(xmlDtdVersion) )
   {
      fprintf(fp, "<?xml version=\"1.0\" encoding=" XML_ENCODING_NAME "?>\n"
                  "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n"
                  "<tv generator-info-name=\"nxtvepg/" EPG_VERSION_STR "\" "
                       "generator-info-url=\"" NXTVEPG_URL "\" "
                       "source-info-name=\"");
      if ((EpgDbContextIsXmltv(pDbContext) == FALSE) || EpgDbContextIsMerged(pDbContext))
         fprintf(fp, "nexTView ");
      EpgDumpXml_HtmlWriteString(fp, src_str, -1);
      fprintf(fp, "\" date=\"%s\">\n", start_str);
   }
   else // XML DTD 6
   {
      fprintf(fp, "<?xml version=\"1.0\" encoding=" XML_ENCODING_NAME "?>\n"
                  "<!DOCTYPE tv SYSTEM \"xmltv-0.6.dtd\">\n"
                  "<tv>\n"
                  "<about date=\"%s\">\n"
                  "\t<copying>\n"
                  "\t\t<p>"
                  "Copyright by nexTView EPG content providers: ",
                  start_str);
      EpgDumpXml_HtmlWriteString(fp, src_str, -1);
      fprintf(fp, "</p>\n"
                  "\t</copying>\n"
                  "\t<source-info><link>\n"
                  "\t\t<text>");
      EpgDumpXml_HtmlWriteString(fp, AI_GET_SERVICENAME(pAiBlock), -1);
      fprintf(fp, "</text>\n");
      if ((pOiBlock != NULL) && (OI_HAS_HEADER(pOiBlock) || OI_HAS_MESSAGE(pOiBlock)))
      {
         fprintf(fp, "\t\t<blurb>\n");
         if (OI_HAS_HEADER(pOiBlock))
         {
            fprintf(fp, "\t\t\t<p>");
            EpgDumpXml_HtmlWriteString(fp, OI_GET_HEADER(pOiBlock), -1);
            fprintf(fp, "</p>\n");
         }
         if (OI_HAS_MESSAGE(pOiBlock))
         {
            fprintf(fp, "\t\t\t<p>");
            EpgDumpXml_HtmlWriteString(fp, OI_GET_MESSAGE(pOiBlock), -1);
            fprintf(fp, "</p>\n");
         }
         fprintf(fp, "\t\t</blurb>\n");
      }
      fprintf(fp, "\t</link></source-info>\n"
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
static void EpgDumpXml_WriteChannel( EPGDB_CONTEXT * pDbContext, const AI_BLOCK * pAiBlock,
                                     char ** pChnIds, uint netwopIdx,
                                     FILE * fp, DUMP_XML_MODE xmlDtdVersion )
{
   const uchar     * pNetname;
   const uchar     * pChnId;
   char cni_buf[16+3+1];
   uint cni;

   if (netwopIdx < pAiBlock->netwopCount)
   {
      pNetname = EpgSetup_GetNetName(pAiBlock, netwopIdx, NULL);
      cni = AI_GET_NET_CNI_N(pAiBlock, netwopIdx);

      // determine the XMLTV channel ID
      pChnIds[netwopIdx] = NULL;
      if ( IS_XMLTV_CNI(cni) )
      {
         // for imported XMLTV files: keep the original channel ID
         // XXX FIXME: this is not the original one if overriden by xmltv-etsi.map
         pChnId = RcFile_GetXmltvNetworkId(cni);
         if (pChnId != NULL)
            pChnIds[netwopIdx] = xstrdup(pChnId);
      }
      if (pChnIds[netwopIdx] == NULL)
      {
         // TODO: map numerical CNI to RFC2838 (e.g. "ard.de")
         sprintf(cni_buf, "CNI%04X", cni);
         pChnIds[netwopIdx] = xstrdup(cni_buf);
      }

      fprintf(fp, "<channel id=\"%s\">\n", pChnIds[netwopIdx]);

      if ( IS_XML_DTD5(xmlDtdVersion) == FALSE )
      {
         fprintf(fp, "\t<number num=\"%d\" />\n", netwopIdx);
      }

      fprintf(fp, "\t<display-name>");
      EpgDumpXml_HtmlWriteString(fp, pNetname, -1);
      fprintf(fp, "</display-name>\n"
                  "</channel>\n");
   }
   else
      debug2("EpgDumpXml-WriteChannel: invalid netwop index %d (>= %d)", netwopIdx, pAiBlock->netwopCount);
}

// ----------------------------------------------------------------------------
// Write a single programme description
//
static void EpgDumpXml_WriteProgramme( EPGDB_CONTEXT * pDbContext, const AI_BLOCK * pAiBlock,
                                       const PI_BLOCK * pPiBlock, char ** pChnIds,
                                       FILE * fp, DUMP_XML_MODE xmlDtdVersion )
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

      fprintf(fp, "<programme start=\"%s\" stop=\"%s\" %s channel=\"%s\">\n",
                  start_str, stop_str, tmp_str, pChnIds[pPiBlock->netwop_no]);
   }
   else // XML DTD 6
   {
      fprintf(fp, "<timeslot start=\"%s\" stop=\"%s\" channel=\"%s\"%s>\n",
                  start_str, stop_str, pChnIds[pPiBlock->netwop_no],
                  ((pPiBlock->feature_flags & PI_FEATURE_LIVE) ? " liveness=\"live\"" : ""));

      if (EpgDbGetVpsTimestamp(&vpsTime, pPiBlock->pil, pPiBlock->start_time))
      {
         // VPS/PDC timestamp is in localtime (hence no "Z" suffix)
         strftime(tmp_str,  sizeof(tmp_str), "%Y-%m-%dT%H:%M:%S", &vpsTime);
         fprintf(fp, "  <code-time system=\"vps\" start=\"%s\" />\n", tmp_str);
      }

      fprintf(fp, "  <programme id=\"C%04X.B%04X.S%d\"%s>\n",
                  AI_GET_NET_CNI_N(pAiBlock, pPiBlock->netwop_no),
                  pPiBlock->block_no, (int)pPiBlock->start_time,
                  ((pPiBlock->feature_flags & PI_FEATURE_REPEAT) ? " newness=\"repeat\"" : ""));
   }

   // programme title and description (quoting "<", ">" and "&" characters)
   fprintf(fp, "\t<title>");
   EpgDumpXml_HtmlWriteString(fp, PI_GET_TITLE(pPiBlock), -1);
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
      pThemeStr = PdcThemeGet(pPiBlock->themes[idx]);
      if (pThemeStr != NULL)
      {
         if ( IS_XML_DTD5(xmlDtdVersion) )
            fprintf(fp, "\t<category>");
         else
            fprintf(fp, "\t<category system=\"pdc\" code=\"%d\">", pPiBlock->themes[idx]);

         EpgDumpXml_HtmlWriteString(fp, pThemeStr, -1);

         fprintf(fp, "</category>\n");
      }
   }

   // attributes
   if ( IS_XML_DTD5(xmlDtdVersion) )
   {
      if ((pPiBlock->feature_flags & (PI_FEATURE_PAL_PLUS | PI_FEATURE_FMT_WIDE)) != 0)
      {
         fprintf(fp, "\t<video>\n\t\t<aspect>16:9</aspect>\n\t</video>\n");
      }

      if ((pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK) == PI_FEATURE_SOUND_STEREO)
      {
         fprintf(fp, "\t<audio>\n\t\t<stereo>stereo</stereo>\n\t</audio>\n");
      }
      else if ((pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK) == PI_FEATURE_SOUND_SURROUND)
      {
         fprintf(fp, "\t<audio>\n\t\t<stereo>surround</stereo>\n\t</audio>\n");
      }
      // else: 2-channel not supported by DTD 0.5 semantics

      if (pPiBlock->feature_flags & PI_FEATURE_REPEAT)
      {
         fprintf(fp, "\t<previously-shown />\n");
      }

      if (pPiBlock->feature_flags & PI_FEATURE_SUBTITLES)
      {
         fprintf(fp, "\t<subtitles type=\"teletext\"></subtitles>\n");
      }

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

      if ((pPiBlock->feature_flags & (PI_FEATURE_PAL_PLUS | PI_FEATURE_FMT_WIDE)) != 0)
      {
         fprintf(fp, "\t<video>\n");
         fprintf(fp, "\t\t<aspect x=\"16\" y=\"9\" />\n");
         if (pPiBlock->feature_flags & PI_FEATURE_PAL_PLUS)
            fprintf(fp, "\t\t<quality>PAL+</quality>\n");
         fprintf(fp, "\t</video>\n");
      }

      if ((pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK) == PI_FEATURE_SOUND_STEREO)
      {
         fprintf(fp, "\t<audio><stereo /></audio>\n");
      }
      else if ((pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK) == PI_FEATURE_SOUND_SURROUND)
      {
         fprintf(fp, "\t<audio><surround /></audio>\n");
      }
      else if ((pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK) == PI_FEATURE_SOUND_2CHAN)
      {
         fprintf(fp, "\t<audio channel=\"A\"><mono /></audio>\n");
         fprintf(fp, "\t<audio channel=\"B\"><mono /></audio>\n");
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
void EpgDumpXml_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp, DUMP_XML_MODE dumpMode )
{
   const AI_BLOCK  * pAiBlock;
   const OI_BLOCK  * pOiBlock;
   const PI_BLOCK  * pPiBlock;
   char * pChnIds[MAX_NETWOP_COUNT];
   uint  netwopIdx;

   if (fp != NULL)
   {
      EpgDbLockDatabase(pDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pDbContext);
      if (pAiBlock != NULL)
      {
         // get "OSD information" with service name and message
         pOiBlock = EpgDbGetOi(pDbContext, 0);

         // header with source info
         EpgDumpXml_WriteHeader(pDbContext, pAiBlock, pOiBlock, fp, dumpMode);

         // channel table
         for (netwopIdx = 0; netwopIdx < pAiBlock->netwopCount; netwopIdx++)
         {
            EpgDumpXml_WriteChannel(pDbContext, pAiBlock, pChnIds, netwopIdx, fp, dumpMode);
         }

         // loop across all PI and dump their info
         pPiBlock = EpgDbSearchFirstPi(pDbContext, NULL);
         while (pPiBlock != NULL)
         {
            EpgDumpXml_WriteProgramme(pDbContext, pAiBlock, pPiBlock, pChnIds, fp, dumpMode);

            pPiBlock = EpgDbSearchNextPi(pDbContext, NULL, pPiBlock);
         }

         // free channel IDs (allocated when writing channel table
         for (netwopIdx = 0; netwopIdx < pAiBlock->netwopCount; netwopIdx++)
            if (pChnIds[netwopIdx] != NULL)
               xfree(pChnIds[netwopIdx]);

         // footer
         fprintf(fp, "</tv>\n");
      }
      EpgDbLockDatabase(pDbContext, FALSE);
   }
   else
      fatal0("EpgDumpXml-Standalone: illegal NULL ptr param");
}

