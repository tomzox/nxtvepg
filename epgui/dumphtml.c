/*
 *  Nextview GUI: Output of PI data in HTML
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
 *    This module implements methods to export PI in HTML.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: dumphtml.c,v 1.13 2020/06/17 19:32:20 tom Exp tom $
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
#include "epgui/epgmain.h"
#include "epgui/epgsetup.h"
#include "epgui/pdc_themes.h"
#include "epgui/pifilter.h"
#include "epgui/pibox.h"
#include "epgui/pidescr.h"
#include "epgui/pioutput.h"
#include "epgui/dumpxml.h"
#include "epgui/dumphtml.h"

// ----------------------------------------------------------------------------
// Print Short- and Long-Info texts or separators
// - note: in HTML output there is no special separator between descriptions
//   of different providers in a merged database, hence the "addSeparator"
//   parameter is not used
//
static void EpgDumpHtml_AppendInfoTextCb( void *vp, const char * pDesc, bool addSeparator )
{
   FILE * fp = (FILE *) vp;
   const char * pNewline;

   if ((fp != NULL) && (pDesc != NULL))
   {
      fprintf(fp, "<tr>\n<td colspan=\"3\" CLASS=\"textrow\">\n<p>\n");

      // replace newline characters with paragraph tags
      while ( (pNewline = strchr(pDesc, '\n')) != NULL )
      {
         // print text up to (and excluding) the newline
         EpgDumpXml_HtmlWriteString(fp, pDesc, pNewline - pDesc);
         fprintf(fp, "\n</p><p>\n");
         // skip to text following the newline
         pDesc = pNewline + 1;
      }
      // write the segement behind the last newline
      EpgDumpXml_HtmlWriteString(fp, pDesc, -1);

      fprintf(fp, "\n</p>\n</td>\n</tr>\n\n");
   }
}

// ----------------------------------------------------------------------------
// Append HTML for one PI
//
static void EpgDumpHtml_WritePi( FILE *fp, const PI_BLOCK * pPiBlock, const AI_BLOCK * pAiBlock )
{
   char date_str[20], start_str[20], stop_str[20], label_str[50];
   const char *pCfNetname;
   time_t start_time;
   time_t stop_time;
   bool isFromAi;

   start_time = pPiBlock->start_time;
   stop_time = pPiBlock->stop_time;

   strftime(date_str, sizeof(date_str), " %a %d.%m", localtime(&start_time));
   strftime(start_str, sizeof(start_str), "%H:%M", localtime(&start_time));
   strftime(stop_str, sizeof(stop_str), "%H:%M", localtime(&stop_time));
   strftime(label_str, sizeof(label_str), "%Y%m%d%H%M", localtime(&start_time));

   pCfNetname = EpgSetup_GetNetName(pAiBlock, pPiBlock->netwop_no, &isFromAi);

   // start HTML table for PI and append first row: running time, title, network name
   fprintf(fp, "<A NAME=\"TITLE_%02d_%s\">\n</A>\n"
               "<table CLASS=\"PI\" WIDTH=\"100%%\""
                      "BORDER=\"1\" FRAME=\"border\" RULES=\"all\">\n"
               "<tr>\n"
               "<td CLASS=\"titlerow\" WIDTH=\"20%%\">\n"
               "%s - %s\n"  // running time
               "</td>\n"
               "<td rowspan=\"2\" CLASS=\"titlerow\">\n",
               pPiBlock->netwop_no, label_str,
               start_str, stop_str);
   EpgDumpXml_HtmlWriteString(fp, PI_GET_TITLE(pPiBlock), -1);
   fprintf(fp, "\n"
               "</td>\n"
               "<td rowspan=\"2\" CLASS=\"titlerow\" WIDTH=\"20%%\">\n");
   if (isFromAi == FALSE)
   {
      Tcl_DString dstr;
      Tcl_DStringInit(&dstr);
      Tcl_ExternalToUtfDString(NULL, pCfNetname, -1, &dstr);
      EpgDumpXml_HtmlWriteString(fp, Tcl_DStringValue(&dstr), Tcl_DStringLength(&dstr));
      Tcl_DStringFree(&dstr);
   }
   else
   {
      EpgDumpXml_HtmlWriteString(fp, pCfNetname, -1);
   }
   fprintf(fp, "\n"
               "</td>\n"
               "</tr>\n"
               "<tr>\n"
               "<td CLASS=\"titlerow\" WIDTH=\"20%%\">\n"
               "%s\n"  // date
               "</td>\n"
               "</tr>\n\n",
               date_str
               );

   // start second row: themes & features
   fprintf(fp, "<tr><td colspan=\"3\" CLASS=\"featurerow\">\n");

   // append theme list
   PiDescription_AppendCompressedThemes(pPiBlock, comm, TCL_COMM_BUF_SIZE);
   EpgDumpXml_HtmlWriteString(fp, comm, -1);

   // append features list
   strcpy(comm, " (");
   PiDescription_AppendFeatureList(pPiBlock, comm + 2);
   if (comm[2] != 0)
   {
      strcat(comm, ")");
      EpgDumpXml_HtmlWriteString(fp, comm, -1);
   }
   fprintf(fp, "\n</td>\n</tr>\n\n");

   // start third row: description
   PiDescription_AppendShortAndLongInfoText(pPiBlock, EpgDumpHtml_AppendInfoTextCb, fp, EpgDbContextIsMerged(pUiDbContext));

   fprintf(fp, "</table>\n\n\n");
}

// ----------------------------------------------------------------------------
// HTML page templates
//
static const char * const html_head =
   "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n"
   "<HTML>\n"
   "<!--\n"
   "      Generated by nxtvepg at %s\n"
   "      Please respect the content provider's copyright!\n"
   "      Do not redistribute this document in the Internet!\n"
   "-->\n"
   "<HEAD>\n"
   "<META http-equiv=\"Content-Type\" content=\"text/html; charset=\"utf-8\">\n"
   "<META name=\"copyright\" content=\"%s\">\n"
   "<META name=\"description\" lang=\"en\" content=\"nexTView EPG: TV programme schedules\">\n"
   "<META name=\"generator\" content=\"nxtvepg/" EPG_VERSION_STR "\">\n"
   "<LINK rel=\"generator\" href=\"" NXTVEPG_URL "\">\n"
   "<TITLE>Nextview EPG</TITLE>\n"
   "<STYLE type=\"text/css\">\n"
   "   <!--\n"
   "   TABLE.PI { PADDING: 3; BORDER-WIDTH: 1px; MARGIN-BOTTOM: 30pt; }\n"
   "   TABLE.titlelist { BORDER-STYLE: NONE; }\n"
   "   BODY { FONT-FAMILY: Arial, Helvetica; }\n"
   "   .titlerow { TEXT-ALIGN: CENTER; FONT-WEIGHT: BOLD; BACKGROUND-COLOR: #e2e2e2; }\n"
   "   .featurerow { TEXT-ALIGN: CENTER; }\n"
   "   .textrow { }\n"
   "   .copyright { FONT-STYLE: italic; FONT-SIZE: smaller; MARGIN-TOP: 40pt; }\n"
   "   -->\n"
   "</STYLE>\n"
   "<LINK rel=\"stylesheet\" type=\"text/css\" href=\"nxtvhtml.css\">\n"
   "</HEAD>\n"
   "<BODY BGCOLOR=\"#f8f8f8\">\n\n";

static const char * const html_marker_cont_title =
   "<!-- APPEND LIST HERE / DO NOT DELETE THIS LINE -->\n";
static const char * const html_marker_cont_desc =
   "<!-- APPEND PROGRAMMES HERE / DO NOT DELETE THIS LINE -->\n";


typedef enum
{
   HTML_DUMP_NONE,
   HTML_DUMP_DESC,
   HTML_DUMP_TITLE,
   HTML_DUMP_END
} HTML_DUMP_TYPE;

// ----------------------------------------------------------------------------
// Open source and create destination HTML files
//
static void EpgDumpHtml_Create( const char * pFileName, bool optAppend, FILE ** fppSrc, FILE ** fppDst, const AI_BLOCK * pAiBlock )
{
   char * pBakName;
   FILE *fpSrc, *fpDst;
   bool abort = FALSE;

   fpDst = fpSrc = NULL;
   pBakName = NULL;

   if (optAppend)
   {
      // open the old file for reading - ignore if not exists
      fpSrc = fopen(pFileName, "r");
      if (fpSrc != NULL)
      {
         // exists -> rename old file to .bak and then create new output file
         pBakName = xmalloc(strlen(pFileName) + 10);
         strcpy(pBakName, pFileName);
         strcat(pBakName, ".bak");
         #ifndef WIN32
         if (rename(pFileName, pBakName) == -1)
         {
            sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumphtml "
                          "-message \"Failed to rename old HTML file '%s' to %s: %s\"",
                          pFileName, pBakName, strerror(errno));
            eval_check(interp, comm);
            Tcl_ResetResult(interp);
            abort = TRUE;
         }
         #else
         // WIN32 cannot rename opened files, hence the special handling
         fclose(fpSrc);
         // remove the backup file if it already exists
         if ( (remove(pBakName) != -1) || (errno == ENOENT) )
         {
            // rename the old file to the backup file name
            if (rename(pFileName, pBakName) != -1)
            {
               // finally open the old file again for reading
               fpSrc = fopen(pBakName, "r");
            }
            else
            {
               sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumphtml "
                             "-message \"Failed to rename old HTML file '%s' to %s: %s\"",
                             pFileName, pBakName, strerror(errno));
               eval_check(interp, comm);
               Tcl_ResetResult(interp);
               abort = TRUE;
            }
         }
         else
         {
            sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumphtml "
                          "-message \"Failed to remove old backup file %s: %s\"",
                          pBakName, strerror(errno));
            eval_check(interp, comm);
            Tcl_ResetResult(interp);
            abort = TRUE;
         }
         #endif
      }
   }

   if (abort == FALSE)
   {
      fpDst = fopen(pFileName, "w");
      if (fpDst == NULL)
      {  // access, create or truncate failed -> inform the user
         sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumphtml -message \"Failed to open file '%s' for writing: %s\"",
                       pFileName, strerror(errno));
         eval_check(interp, comm);
         Tcl_ResetResult(interp);
      }
   }

   if (pBakName != NULL)
      xfree(pBakName);

   *fppSrc = fpSrc;
   *fppDst = fpDst;
}

// ----------------------------------------------------------------------------
// Skip to next section
// - must be called in order for all the sections
// - the insertion point is marked by one of the HTML comments defined above
// - everything up to the point is copied into a second file
//   the marker itself is not copied; it must be written anew after the insertion
//
static void EpgDumpHtml_Skip( FILE * fpSrc, FILE * fpDst, HTML_DUMP_TYPE prevType, HTML_DUMP_TYPE nextType )
{
   #define HTML_COPY_BUF_SIZE  256
   char buffer[HTML_COPY_BUF_SIZE];
   const char * pMarker;
   bool skipLine;
   int len;

   // append the marker for the previous section
   if (prevType == HTML_DUMP_TITLE)
      fprintf(fpDst, "%s\n", html_marker_cont_title);
   else if (prevType == HTML_DUMP_DESC)
      fprintf(fpDst, "%s\n", html_marker_cont_desc);

   if (fpSrc != NULL)
   {
      // determine which marker to search for
      if (nextType == HTML_DUMP_TITLE)
         pMarker = html_marker_cont_title;
      else if (nextType == HTML_DUMP_DESC)
         pMarker = html_marker_cont_desc;
      else
         pMarker = NULL;

      skipLine = FALSE;
      // loop over all lines in the old file
      while (fgets(buffer, sizeof(buffer), fpSrc) != NULL)
      {
         if ((skipLine == FALSE) && (pMarker != NULL) && (strcmp(buffer, pMarker) == 0))
         {  // found the insertion point
            break;
         }

         // check if the line was too long for the buffer
         // if yes, the result of the next read must be skipped
         len = strlen(buffer);
         skipLine = ((len > 0) && (buffer[len - 1] != '\n'));

         // copy the line into the destination file
         fwrite(buffer, 1, len, fpDst);
      }
   }
   else
   {  // new file

      // close title section
      if (prevType == HTML_DUMP_TITLE)
         fprintf(fpDst, "</table>\n\n\n");

      // create head for requested section
      if (nextType == HTML_DUMP_TITLE)
         fprintf(fpDst, "<table CLASS=\"titlelist\" WIDTH=\"100%%\">\n");

      if (nextType == HTML_DUMP_DESC)
         fprintf(fpDst, "<P><BR></P>\n\n");
   }
}

// ----------------------------------------------------------------------------
// Finish the HTML output file
//
static void EpgDumpHtml_Close( FILE * fpSrc, FILE * fpDst, const AI_BLOCK * pAiBlock )
{
   if (fpDst != NULL)
   {
      if (fpSrc != NULL)
      {
         fclose(fpSrc);
      }
      fclose(fpDst);
   }
}

// ----------------------------------------------------------------------------
// Write HTML header
//
static void EpgDumpHtml_Header( FILE * fpSrc, FILE * fpDst, const AI_BLOCK * pAiBlock )
{
   time_t now = time(NULL);

   if (fpDst != NULL)
   {
      // only create for new file
      if (fpSrc == NULL)
      {
         // copy the service name while replacing double quotes
         EpgDumpXml_HtmlRemoveQuotes(AI_GET_SERVICENAME(pAiBlock), comm, TCL_COMM_BUF_SIZE);

         fprintf(fpDst, html_head, ctime(&now), comm);
      }
   }
   else
      fatal0("EpgDumpHtml-Header: illegal NULL ptr for source file");
}

// ----------------------------------------------------------------------------
// Write HTML header
//
static void EpgDumpHtml_Footer( FILE * fpSrc, FILE * fpDst, const AI_BLOCK * pAiBlock )
{
   if (fpDst != NULL)
   {
      if (fpSrc == NULL)
      {  // newly created file -> finish HTML page
         fprintf(fpDst, "<P CLASS=\"copyright\">\n&copy; Nextview EPG by ");
         EpgDumpXml_HtmlWriteString(fpDst, AI_GET_SERVICENAME(pAiBlock), -1);
         fprintf(fpDst, "\n</P>\n</BODY>\n</HTML>\n");

      }
   }
   else
      fatal0("EpgDumpHtml-Footer: illegal NULL ptr for source file");
}

// ----------------------------------------------------------------------------
// Dump HTML for one programme column
// - also supports user-defined columns
// - XXX TODO: use <SPAN> for colors and add support for all-column fore-/background colors
//
static void EpgDumpHtml_Title( FILE * fpDst, const PI_BLOCK * pPiBlock,
                               const PIBOX_COL_CFG * pColTab, uint colCount,
                               uint hyperlinkColIdx, bool optTextFmt )
{
   PIBOX_COL_TYPES type;
   Tcl_Obj  * pFmtObj;
   Tcl_Obj ** pFmtObjv;
   Tcl_Obj  * pImageObj;
   Tcl_Obj ** pImgObjv;
   Tcl_Obj  * pImgSpec;
   const char * pFmtStr;
   bool  hasBold, hasEm, hasStrike, hasColor;
   int   fmtObjc;
   int   imgObjc;
   int   charLen;
   uint  colIdx;
   uint  len;
   sint  fmtIdx;

   if ((fpDst != NULL) && (pPiBlock != NULL) && (pColTab != NULL))
   {
      fprintf(fpDst, "<tr>\n");

      // add table columns in the same configuration as for the internal listbox
      for (colIdx=0; colIdx < colCount; colIdx++)
      {
         fprintf(fpDst, "<td>\n");

         if (colIdx == hyperlinkColIdx)
         {  // if requested add hyperlink to the description on this column
            char label_str[50];
            time_t start_time = pPiBlock->start_time;
            strftime(label_str, sizeof(label_str), "%Y%m%d%H%M", localtime(&start_time));
            fprintf(fpDst, "<A HREF=\"#TITLE_%02d_%s\">\n", pPiBlock->netwop_no, label_str);
         }

         len  = 0;
         type = pColTab[colIdx].type;
         pImageObj = pFmtObj = NULL;

         if (type == PIBOX_COL_WEEKCOL)
            type = PIBOX_COL_WEEKDAY;  // XXX FIXME weekday colors not implemented for HTML yet

         if ((type == PIBOX_COL_USER_DEF) || (type == PIBOX_COL_REMINDER))
         {
            len = PiOutput_MatchUserCol(pPiBlock, &type, pColTab[colIdx].pDefObj,
                                        comm, TCL_COMM_BUF_SIZE, &charLen, &pImageObj, &pFmtObj);
         }
         if ((type != PIBOX_COL_USER_DEF) && (type != PIBOX_COL_REMINDER) && (type != PIBOX_COL_WEEKCOL))
            len = PiOutput_PrintColumnItem(pPiBlock, type, comm, TCL_COMM_BUF_SIZE, &charLen);

         if (pImageObj != NULL)
         {  // user-defined column consists of an image
            pImgSpec = Tcl_GetVar2Ex(interp, "pi_img", Tcl_GetString(pImageObj), TCL_GLOBAL_ONLY);
            if (pImgSpec != NULL)
            {
               if ( (Tcl_ListObjGetElements(interp, pImgSpec, &imgObjc, &pImgObjv) == TCL_OK) &&
                    (imgObjc == 2) )
               {
                  // note: there's intentionally no WIDTH and HEIGHT tags so that the user can use different images
                  fprintf(fpDst, "<IMG SRC=\"images/%s.png\" ALIGN=\"middle\" ALT=\"%s\">\n",
                                 Tcl_GetString(pImageObj), Tcl_GetString(pImageObj));
               }
            }
         }

         hasBold = hasEm = hasStrike = hasColor = FALSE;
         if (optTextFmt && (pFmtObj != NULL))
         {
            Tcl_ListObjGetElements(interp, pFmtObj, &fmtObjc, &pFmtObjv);
            for (fmtIdx=0; fmtIdx < fmtObjc; fmtIdx++)
            {
               pFmtStr = Tcl_GetString(pFmtObjv[fmtIdx]);

               if (strcmp(pFmtStr, "bold") == 0)
               {
                  fprintf(fpDst, "<B>");
                  hasBold = TRUE;
               }
               else if (strcmp(pFmtStr, "underline") == 0)
               {
                  fprintf(fpDst, "<EM>");
                  hasEm = TRUE;
               }
               else if (strcmp(pFmtStr, "overstrike") == 0)
               {
                  fprintf(fpDst, "<STRIKE>");
                  hasStrike = TRUE;
               }
               else if (strncmp(pFmtStr, "fg_RGB", 6) == 0)
               {
                  fprintf(fpDst, "<FONT COLOR=\"#%s\">", pFmtStr + 6);
                  hasColor = TRUE;
               }
               else if (strncmp(pFmtStr, "fg_", 3) == 0)
               {
                  fprintf(fpDst, "<FONT COLOR=\"%s\">", pFmtStr + 3);
                  hasColor = TRUE;
               }
            }
         }

         if (len > 0)
         {
            EpgDumpXml_HtmlWriteString(fpDst, comm, len);
         }

         if (hasColor)
            fprintf(fpDst, "</FONT>");
         if (hasStrike)
            fprintf(fpDst, "</STRIKE>");
         if (hasEm)
            fprintf(fpDst, "</EM>");
         if (hasBold)
            fprintf(fpDst, "</B>");

         fprintf(fpDst, "%s\n</td>\n", ((colIdx == hyperlinkColIdx) ? "</a>\n" : ""));
      }

      fprintf(fpDst, "</tr>\n\n");
   }
   else
      fatal3("EpgDumpHtml-Title: illegal NULL ptr param: fp=%d pPi=%d, pCol=%d", (fpDst != NULL), (pPiBlock != NULL), (pColTab != NULL));
}

// ----------------------------------------------------------------------------
// Dump programme titles and/or descriptions into file in HTML format
//
static void EpgDumpHtml_StartDump( FILE *fpSrc, FILE *fpDst,
                                   FILTER_CONTEXT * fc, const AI_BLOCK *pAiBlock,
                                   Tcl_Obj ** pColObjv,
                                   int doTitles, int doDesc, int optAppend, int optSelOnly,
                                   int optMaxCount, int hyperlinkColIdx, int optTextFmt, int colCount )
{
   const PI_BLOCK * pPiBlock;
   const PIBOX_COL_CFG * pColTab;
   sint piIdx;

   // add or skip & copy HTML page header
   EpgDumpHtml_Skip(fpSrc, fpDst, HTML_DUMP_NONE, HTML_DUMP_TITLE);
   if (doTitles)
   {  // add selected title or table with list of titles

      // create column config cache from specification in Tcl list
      pColTab = PiOutput_CfgColumnsCache(colCount, pColObjv);

      if (optSelOnly == FALSE)
         pPiBlock = EpgDbSearchFirstPi(pUiDbContext, fc);
      else
         pPiBlock = PiBox_GetSelectedPi();

      for (piIdx=0; ((piIdx < optMaxCount) || (optMaxCount < 0)) && (pPiBlock != NULL); piIdx++)
      {
         EpgDumpHtml_Title(fpDst, pPiBlock, pColTab, colCount, hyperlinkColIdx, optTextFmt);

         pPiBlock = EpgDbSearchNextPi(pUiDbContext, fc, pPiBlock);
      }
      // free column config cache
      PiOutput_CfgColumnsClear(pColTab, colCount);
   }

   // skip & copy existing descriptions if in append mode
   EpgDumpHtml_Skip(fpSrc, fpDst, HTML_DUMP_TITLE, HTML_DUMP_DESC);
   if (doDesc)
   {
      if (optSelOnly == FALSE)
         pPiBlock = EpgDbSearchFirstPi(pUiDbContext, fc);
      else
         pPiBlock = PiBox_GetSelectedPi();

      // add descriptions for all programmes matching the current filter
      for (piIdx=0; ((piIdx < optMaxCount) || (optMaxCount < 0)) && (pPiBlock != NULL); piIdx++)
      {
         EpgDumpHtml_WritePi(fpDst, pPiBlock, pAiBlock);

         pPiBlock = EpgDbSearchNextPi(pUiDbContext, fc, pPiBlock);
      }
   }
   EpgDumpHtml_Skip(fpSrc, fpDst, HTML_DUMP_DESC, HTML_DUMP_END);
}

// ----------------------------------------------------------------------------
// Start dump via GUI dialog
//
static int EpgDumpHtml_DumpDatabase( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_DumpHtml <file-name> <doTitles=0/1> <doDesc=0/1> "
                                                 "<append=0/1> <sel-only=0/1> <max-count> "
                                                 "<hyperlink=col-idx/-1> <text_fmt=0/1> <colsel>";
   const AI_BLOCK *pAiBlock;
   Tcl_DString  ds;
   Tcl_Obj ** pColObjv;
   char * pFileName;
   FILE *fpSrc, *fpDst;
   int  doTitles, doDesc, optAppend, optSelOnly, optMaxCount, hyperlinkColIdx, optTextFmt;
   int  colCount;
   int  result;

   if (objc != 1+9)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( (pFileName = Tcl_GetString(objv[1])) == NULL )
   {  // internal error: can not get filename string
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( (Tcl_GetBooleanFromObj(interp, objv[2], &doTitles) != TCL_OK) ||
             (Tcl_GetBooleanFromObj(interp, objv[3], &doDesc) != TCL_OK) ||
             (Tcl_GetBooleanFromObj(interp, objv[4], &optAppend) != TCL_OK) ||
             (Tcl_GetBooleanFromObj(interp, objv[5], &optSelOnly) != TCL_OK) ||
             (Tcl_GetIntFromObj    (interp, objv[6], &optMaxCount) != TCL_OK) ||
             (Tcl_GetIntFromObj    (interp, objv[7], &hyperlinkColIdx) != TCL_OK) ||
             (Tcl_GetBooleanFromObj(interp, objv[8], &optTextFmt) != TCL_OK) ||
             (Tcl_ListObjGetElements(interp, objv[9], &colCount, &pColObjv) != TCL_OK) )
   {  // one of the params is not boolean
      result = TCL_ERROR;
   }
   else
   {
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      if (pAiBlock != NULL)
      {
         pFileName = Tcl_UtfToExternalDString(NULL, pFileName, -1, &ds);
         EpgDumpHtml_Create(pFileName, optAppend, &fpSrc, &fpDst, pAiBlock);
         if (fpDst != NULL)
         {
            EpgDumpHtml_Header(fpSrc, fpDst, pAiBlock);
            EpgDumpHtml_StartDump(fpSrc, fpDst, pPiFilterContext, pAiBlock, pColObjv,
                                  doTitles, doDesc, optAppend,
                                  optSelOnly, optMaxCount, hyperlinkColIdx,
                                  optTextFmt, colCount);
            EpgDumpHtml_Footer(fpSrc, fpDst, pAiBlock);

            EpgDumpHtml_Close(fpSrc, fpDst, pAiBlock);
         }
         Tcl_DStringFree(&ds);
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Helper func: read integer from global Tcl var
//
static int EpgDumpHtml_ReadTclInt( Tcl_Interp *interp,
                                   CONST84 char * pName, int fallbackVal )
{
   Tcl_Obj  * pVarObj;
   int  value;

   if (pName != NULL)
   {
      pVarObj = Tcl_GetVar2Ex(interp, pName, NULL, TCL_GLOBAL_ONLY);
      if ( (pVarObj == NULL) ||
           (Tcl_GetIntFromObj(interp, pVarObj, &value) != TCL_OK) )
      {
         debug3("EpgDumpHtml-ReadTclInt: cannot read Tcl var %s (%s) - use default val %d", pName, ((pVarObj != NULL) ? Tcl_GetString(pVarObj) : "*undef*"), fallbackVal);
         value = fallbackVal;
      }
   }
   else
   {
      fatal0("EpgDumpHtml-ReadTclInt: illegal NULL ptr param");
      value = fallbackVal;
   }
   return value;
}

// ----------------------------------------------------------------------------
// Dump programme titles and/or descriptions via command-line
//
void EpgDumpHtml_Standalone( EPGDB_CONTEXT * pDbContext, FILTER_CONTEXT * fc,
                             FILE * fp, uint subMode )
{
   const AI_BLOCK *pAiBlock;
   Tcl_Obj  * pVarObj;
   Tcl_Obj ** pColObjv;
   int  optHtmlType, optAppend, optSel, optMaxCount, optHyperlinks, optTextFmt;
   int  /*optUseColsel,*/ colCount, hyperlinkColIdx;
   time_t now = time(NULL);

   optHtmlType = EpgDumpHtml_ReadTclInt(interp, "dumphtml_type", 0);
   optAppend = EpgDumpHtml_ReadTclInt(interp, "dumphtml_file_append", 0);
   optSel = EpgDumpHtml_ReadTclInt(interp, "dumphtml_sel", 0);
   optMaxCount = EpgDumpHtml_ReadTclInt(interp, "dumphtml_sel_count", 0);
   optHyperlinks = EpgDumpHtml_ReadTclInt(interp, "dumphtml_hyperlinks", 0);
   optTextFmt = EpgDumpHtml_ReadTclInt(interp, "dumphtml_text_fmt", 0);
   //optUseColsel = EpgDumpHtml_ReadTclInt(interp, "dumphtml_use_colsel", 0);

   if (optHyperlinks && (optHtmlType == 3))
      hyperlinkColIdx = 4; //TODO: set hyperCol [lsearch -exact $pilistbox_cols title]
   else
      hyperlinkColIdx = -1;

   // override options which are not supported in standalone mode
   optAppend = FALSE;
   if (subMode != 0)
   {
      optSel = 1;
      optMaxCount = subMode;
   }
   optTextFmt = FALSE;
   //optUseColsel = FALSE;

   sprintf(comm, "set pibox_type 0\n"
                 "set pilistbox_cols {weekday day_month_year time title netname}\n"
                 "array set colsel_tabs {weekday 99 day_month_year 99 time 99 title 99 netname 99}\n"
                 "C_PiOutput_CfgColumns");
   eval_check(interp, comm);

   //if (optUseColsel)
   //   pVarObj = Tcl_GetVar2Ex(interp, "dumphtml_colsel", NULL, TCL_GLOBAL_ONLY);
   //else
      pVarObj = Tcl_GetVar2Ex(interp, "pilistbox_cols", NULL, TCL_GLOBAL_ONLY);

   if ( (pVarObj != NULL) &&
        (Tcl_ListObjGetElements(interp, pVarObj, &colCount, &pColObjv) == TCL_OK) )
   {
      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      if (pAiBlock != NULL)
      {
         pPiFilterContext = EpgDbFilterCreateContext();
         EpgDbFilterSetExpireTime(pPiFilterContext, now - (now % 60));
         EpgDbPreFilterEnable(pPiFilterContext, FILTER_EXPIRE_TIME);

         EpgDumpHtml_Header(NULL, fp, pAiBlock);
         EpgDumpHtml_StartDump(NULL, fp, fc, pAiBlock,
                               pColObjv,
                               ((optHtmlType & 1) != 0),
                               ((optHtmlType & 2) != 0),
                               optAppend,
                               (optSel == 2),
                               ((optSel == 2) ? 1 : ((optSel == 0) ? -1 : optMaxCount)),
                               hyperlinkColIdx,
                               optTextFmt,
                               colCount);
         EpgDumpHtml_Footer(NULL, fp, pAiBlock);

         EpgDbFilterDestroyContext(pPiFilterContext);
         EpgDbLockDatabase(pUiDbContext, FALSE);
      }
   }
}

// ----------------------------------------------------------------------------
// Free resources allocated by this module (cleanup during program exit)
//
void EpgDumpHtml_Destroy( void )
{
}

// ----------------------------------------------------------------------------
// Create the Tcl/Tk commands provided by this module
// - this should be called only once during start-up
//
void EpgDumpHtml_Init( void )
{
   Tcl_CreateObjCommand(interp, "C_DumpHtml", EpgDumpHtml_DumpDatabase, (ClientData) NULL, NULL);
}

