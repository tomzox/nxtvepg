/*
 *  Nextview GUI: Display of PI schedule elements
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
 *    This module implements methods to display PI data as elements in a
 *    linear list (CList) or grid and is used by the PI listbox, PI netbox
 *    and HTML export.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: pioutput.c,v 1.53 2004/05/31 14:34:43 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tcl.h>
#include <tk.h>

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
#include "epgui/pibox.h"
#include "epgui/pidescr.h"
#include "epgui/pioutput.h"
#include "epgtcl/dlg_udefcols.h"
#include "epgtcl/mainwin.h"


// ----------------------------------------------------------------------------
// Array which keeps pre-allocated Tcl/Tk objects
// - some are modified during output functions, but it's still faster than
//   allocating the objects newly for every PI element
//
typedef enum
{
   TCLOBJ_WID_LIST,
   TCLOBJ_WID_NET,
   TCLOBJ_STR_INSERT,
   TCLOBJ_STR_NOW,
   TCLOBJ_STR_THEN,
   TCLOBJ_STR_PAST,
   TCLOBJ_STR_TEXT_IDX,
   TCLOBJ_STR_TEXT_ANY,
   TCLOBJ_STR_TEXT_FMT,
   TCLOBJ_STR_LIST_ANY,
   TCLOBJ_COUNT
} PIBOX_TCLOBJ;

static Tcl_Obj * tcl_obj[TCLOBJ_COUNT];

static const char * const pColTypeKeywords[] =
{
   "day",
   "day_month",
   "day_month_year",
   "description",
   "duration",
   "ed_rating",
   "format",
   "live_repeat",
   "netname",
   "par_rating",
   "pil",
   "sound",
   "subtitles",
   "theme",
   "time",
   "title",
   "weekday",
   "weekcol",
   "reminder",
   // "user_def_",
   // "invalid",
   NULL
};

// Emergency fallback for column configuration
// (should never be used because tab-stops and column header buttons will not match)
static const PIBOX_COL_CFG defaultPiboxCols[] =
{
   { PIBOX_COL_NETNAME,  60, FALSE, NULL },
   { PIBOX_COL_TIME,     83, FALSE, NULL },
   { PIBOX_COL_WEEKDAY,  30, FALSE, NULL },
   { PIBOX_COL_TITLE,   266, FALSE, NULL },
};
#define DEFAULT_PIBOX_COL_COUNT (sizeof(defaultPiboxCols) / sizeof(PIBOX_COL_CFG))

// pointer to a list of the currently configured column types
static const PIBOX_COL_CFG * pPiboxColCfg = defaultPiboxCols;
static uint                  piboxColCount = DEFAULT_PIBOX_COL_COUNT;

// ----------------------------------------------------------------------------
// Get column type from keyword
// - maps a character string onto an enum
// - the resulting enum index is cached in the Tcl object
//
PIBOX_COL_TYPES PiOutput_GetPiColumnType( Tcl_Obj * pKeyObj )
{
   int type;

   if (pKeyObj != NULL)
   {
      if (Tcl_GetIndexFromObj(interp, pKeyObj, (CONST84 char **)pColTypeKeywords, "column type", TCL_EXACT, &type) != TCL_OK)
      {
         debug1("PiOutput-GetPiColumnType: unknown type '%s'", Tcl_GetString(pKeyObj));
         type = PIBOX_COL_INVALID;
      }
   }
   else
   {
      fatal0("PiOutput-GetPiColumnType: illegal NULL ptr param");
      type = PIBOX_COL_INVALID;
   }

   return (PIBOX_COL_TYPES) type;
}

// ----------------------------------------------------------------------------
// User-defined column: search matching shortcut & retrieve it's display parameters
// - the column definition is stored in the global Tcl array usercols;
//   a pointer to that object is kept in the static config cache
// - loop across all shortcuts until a match is found or the special "no match" entry
// - if a match is found, return the text or image or alternatively change the column type
//   to a pre-defined type (then the text is determined by evaluating that attribute)
// - also returns a list object with formatting options
//
uint PiOutput_MatchUserCol( const PI_BLOCK * pPiBlock, PIBOX_COL_TYPES * pType, Tcl_Obj * pMarkObj,
                            uchar * pOutBuffer, uint maxLen, Tcl_Obj ** ppImageObj, Tcl_Obj ** ppFmtObj )
{
   Tcl_Obj ** pFiltObjv;
   Tcl_Obj  * pScIdxObj;
   Tcl_Obj  * pTypeObj, * pValueObj;
   uint len;
   int  ucolType;
   int  filtCount, filtIdx;
   int  scIdx;
   int  fmtCount;

   len = 0;
   if (pMarkObj != NULL)
   {
      if (Tcl_ListObjGetElements(interp, pMarkObj, &filtCount, &pFiltObjv) == TCL_OK)
      {
         for (filtIdx=0; filtIdx < filtCount; filtIdx++)
         {
            if ((Tcl_ListObjIndex(interp, pFiltObjv[filtIdx], EPGTCL_UCF_CTXCACHE_IDX, &pScIdxObj) == TCL_OK) && (pScIdxObj != NULL))
            {
               if (Tcl_GetIntFromObj(interp, pScIdxObj, &scIdx) == TCL_OK)
               {
                  if ( (scIdx == -1) || (PiFilter_ContextCacheMatch(pPiBlock, scIdx)) )
                  {
                     // match found -> retrieve display format
                     if ((Tcl_ListObjIndex(interp, pFiltObjv[filtIdx], EPGTCL_UCF_TYPE_IDX, &pTypeObj) == TCL_OK) && (pTypeObj != NULL) &&
                         (Tcl_ListObjIndex(interp, pFiltObjv[filtIdx], EPGTCL_UCF_VALUE_IDX, &pValueObj) == TCL_OK) && (pValueObj != NULL) &&
                         (Tcl_ListObjIndex(interp, pFiltObjv[filtIdx], EPGTCL_UCF_FMT_IDX, ppFmtObj) == TCL_OK) && (*ppFmtObj != NULL))
                     {
                        if ( (Tcl_ListObjLength(interp, *ppFmtObj, &fmtCount) != TCL_OK) ||
                             (fmtCount == 0) )
                        {
                           *ppFmtObj = NULL;
                        }

                        if (Tcl_GetIntFromObj(interp, pTypeObj, &ucolType) == TCL_OK)
                        {
                           if (ucolType == 0)
                           {  // fixed text output
                              len = strlen(Tcl_GetString(pValueObj));
                              strncpy(pOutBuffer, Tcl_GetString(pValueObj), maxLen);
                              if ((len >= maxLen) && (maxLen > 0))
                              {
                                 len = maxLen;
                                 pOutBuffer[maxLen - 1] = 0;
                              }
                              else if (maxLen == 0)
                                 len = 0;
                           }
                           else if (ucolType == 1)
                           {  // image
                              if (ppImageObj != NULL)
                                 *ppImageObj = pValueObj;
                           }
                           else if (ucolType == 2)
                           {  // map onto standard column output type
                              *pType = PiOutput_GetPiColumnType(pValueObj);
                           }
                        }
                     }
                     break;
                  }
               }
            }
         }
      }
   }
   return len;
}

// ----------------------------------------------------------------------------
// Print PI listing table element into string
//
uint PiOutput_PrintColumnItem( const PI_BLOCK * pPiBlock, PIBOX_COL_TYPES type,
                               uchar * pOutBuffer, uint maxLen )
{
   const uchar * pResult;
   const AI_BLOCK *pAiBlock;
   struct tm ttm;
   uint outlen;

   assert(EpgDbIsLocked(pUiDbContext));
   outlen = 0;

   if ((pOutBuffer != NULL) && (pPiBlock != NULL))
   {
      pResult = NULL;
      switch (type)
      {
         case PIBOX_COL_NETNAME:
            pAiBlock = EpgDbGetAi(pUiDbContext);
            if ((pAiBlock != NULL) && (pPiBlock->netwop_no < pAiBlock->netwopCount))
            {
               uchar buf[7];
               sprintf(buf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
               pResult = Tcl_GetVar2(interp, "cfnetnames", buf, TCL_GLOBAL_ONLY);
               if (pResult == NULL)
                  pResult = AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no);
            }
            else
               debug1("PiOutput-PrintColumnItem: no AI block or invalid netwop #%d", pPiBlock->netwop_no);
            break;
            
         case PIBOX_COL_TIME:
            memcpy(&ttm, localtime(&pPiBlock->start_time), sizeof(struct tm));
            outlen  = strftime(pOutBuffer, maxLen, "%H:%M - ", &ttm);
            outlen += strftime(pOutBuffer + outlen, maxLen - outlen, "%H:%M",  localtime(&pPiBlock->stop_time));
            break;

         case PIBOX_COL_DURATION:
            if (maxLen >= 5+5+1)
            {
               uint durMins = (pPiBlock->stop_time - pPiBlock->start_time) / 60;
               sprintf(pOutBuffer, "%02d:%02d", durMins / 60, durMins % 60);
               outlen = 5;
            }
            break;

         case PIBOX_COL_WEEKDAY:
            memcpy(&ttm, localtime(&pPiBlock->start_time), sizeof(struct tm));
            outlen = strftime(pOutBuffer, maxLen, "%a", &ttm);
            break;

         case PIBOX_COL_DAY:
            memcpy(&ttm, localtime(&pPiBlock->start_time), sizeof(struct tm));
            outlen = strftime(pOutBuffer, maxLen, "%d.", &ttm);
            break;

         case PIBOX_COL_DAY_MONTH:
            memcpy(&ttm, localtime(&pPiBlock->start_time), sizeof(struct tm));
            outlen = strftime(pOutBuffer, maxLen, "%d.%m.", &ttm);
            break;

         case PIBOX_COL_DAY_MONTH_YEAR:
            memcpy(&ttm, localtime(&pPiBlock->start_time), sizeof(struct tm));
            outlen = strftime(pOutBuffer, maxLen, "%d.%m.%Y", &ttm);
            break;

         case PIBOX_COL_TITLE:
            pResult = PI_GET_TITLE(pPiBlock);
            break;

         case PIBOX_COL_DESCR:
            pResult = (PI_HAS_LONG_INFO(pPiBlock) ? "l" : 
                         (PI_HAS_SHORT_INFO(pPiBlock) ? "s" : "-"));
            break;

         case PIBOX_COL_PIL:
            if ((pPiBlock->pil != 0x7fff) && (maxLen >= 12+1))
            {
               sprintf(pOutBuffer, "%02d:%02d/%02d.%02d.",
                       (pPiBlock->pil >>  6) & 0x1F,
                       (pPiBlock->pil      ) & 0x3F,
                       (pPiBlock->pil >> 15) & 0x1F,
                       (pPiBlock->pil >> 11) & 0x0F);
               outlen = 12;
            }
            break;

         case PIBOX_COL_SOUND:
            switch(pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK)
            {
               case PI_FEATURE_SOUND_2CHAN: pResult = "2-channel"; break;
               case PI_FEATURE_SOUND_STEREO: pResult = "stereo"; break;
               case PI_FEATURE_SOUND_SURROUND: pResult = "surround"; break;
            }
            break;

         case PIBOX_COL_FORMAT:
            if (pPiBlock->feature_flags & PI_FEATURE_PAL_PLUS)
               pResult = "PAL+";
            else if (pPiBlock->feature_flags & PI_FEATURE_FMT_WIDE)
               pResult = "wide";
            break;

         case PIBOX_COL_ED_RATING:
            if ((pPiBlock->editorial_rating > 0) && (maxLen >= 3+1))
            {
               sprintf(pOutBuffer, "%2d", pPiBlock->editorial_rating);
               outlen = strlen(pOutBuffer);
            }
            break;

         case PIBOX_COL_PAR_RATING:
            if (pPiBlock->parental_rating == 1)
               pResult = "all";
            else if ((pPiBlock->parental_rating > 0) && (maxLen >= 3+1))
            {
               sprintf(pOutBuffer, "%2d", pPiBlock->parental_rating * 2);
               outlen = strlen(pOutBuffer);
            }
            break;

         case PIBOX_COL_LIVE_REPEAT:
            if (pPiBlock->feature_flags & PI_FEATURE_LIVE)
               pResult = "live";
            else if (pPiBlock->feature_flags & PI_FEATURE_REPEAT)
               pResult = "repeat";
            break;

         case PIBOX_COL_SUBTITLES:
            if (pPiBlock->feature_flags & PI_FEATURE_SUBTITLES)
               pResult = "ST";
            break;

         case PIBOX_COL_THEME:
            if (pPiBlock->no_themes > 0)
            {
               uchar theme;
               uint  themeIdx;

               if ( (pPiFilterContext != NULL) &&  // check req. when in cmd-line dump mode
                    EpgDbFilterIsEnabled(pPiFilterContext, FILTER_THEMES) )
               {
                  // Search for the first theme that's not part of the filter setting.
                  // (It would be boring to print "movie" for all programmes, when there's
                  //  a movie theme filter; instead we print the first sub-theme, e.g. "sci-fi")
                  for (themeIdx=0; themeIdx < pPiBlock->no_themes; themeIdx++)
                  {
                     theme = pPiBlock->themes[themeIdx];
                     if ( (theme < 0x80) &&
                          PdcThemeIsDefined(theme) &&
                          !EpgDbFilterIsThemeFiltered(pPiFilterContext, theme) )
                     {
                        break;
                     }
                  }
               }
               else
                  themeIdx = PI_MAX_THEME_COUNT;

               if (themeIdx >= pPiBlock->no_themes)
               {  // no filter enabled or nothing found above
                  // -> select the "most significant" theme: lowest PDC index
                  uint minThemeIdx = PI_MAX_THEME_COUNT;
                  for (themeIdx=0; themeIdx < pPiBlock->no_themes; themeIdx++)
                  {
                     theme = pPiBlock->themes[themeIdx];
                     if ( (theme >= 0x80) || PdcThemeIsDefined(theme) )
                     {
                        if ( (minThemeIdx == PI_MAX_THEME_COUNT) ||
                             (theme < pPiBlock->themes[minThemeIdx]) )
                        {
                           minThemeIdx = themeIdx;
                        }
                     }
                  }
                  if (minThemeIdx < pPiBlock->no_themes)
                     themeIdx = minThemeIdx;
                  else
                     themeIdx = 0;
               }

               theme = pPiBlock->themes[themeIdx];
               if (theme > 0x80)
               {  // replace individual series code with general series code
                  theme = PDC_THEME_SERIES;
               }
               pResult = PdcThemeGet(theme);
            }
            break;

         case PIBOX_COL_WEEKCOL:
         case PIBOX_COL_REMINDER:
         case PIBOX_COL_USER_DEF:
            break;

         case PIBOX_COL_INVALID:
         default:
            debug1("PiOutput-PrintColumnItem: invalid type %d", type);
            break;
      }

      // copy the result into the output buffer (& limit to max. buffer length)
      if ((pResult != NULL) && (maxLen > 0))
      {
         strncpy(pOutBuffer, pResult, maxLen);

         outlen = strlen(pResult);
         if (maxLen < outlen)
         {
            debug3("PiOutput-PrintColumnItem: output buffer too small: %d, need %d for '%s'", maxLen, outlen, pResult);
            pOutBuffer[maxLen - 1] = 0;
            outlen = maxLen - 1;
         }
      }
      else if (outlen < maxLen)
      {
         // make sure the output buffer is 0 terminated
         // (note: cannot determine here if the string was truncated above, hence no debug output)
         pOutBuffer[outlen] = 0;
      }
      else if (maxLen > 0)
      {
         pOutBuffer[maxLen - 1] = 0;
         outlen = maxLen - 1;
      }
      else
         outlen = 0;
   }
   else
      fatal0("PiOutput-PrintColumnItem: illegal NULL ptr param");

   return outlen;
}

// ----------------------------------------------------------------------------
// Helper function: free resources of columns config cache
//
void PiOutput_CfgColumnsClear( const PIBOX_COL_CFG * pColTab, uint colCount )
{
   uint  colIdx;

   for (colIdx=0; colIdx < colCount; colIdx++)
   {
      if (pColTab[colIdx].pDefObj != NULL)
         Tcl_DecrRefCount(pColTab[colIdx].pDefObj);
   }
   xfree((void *) pColTab);
}

// ----------------------------------------------------------------------------
// Build columns config cache
//
const PIBOX_COL_CFG * PiOutput_CfgColumnsCache( uint colCount, Tcl_Obj ** pColObjv )
{
   PIBOX_COL_CFG * pColTab;
   Tcl_Obj  * pTabObj;
   Tcl_Obj  * pColObj;
   char * pKeyword;
   uint  type;
   uint  colIdx;
   int   width;
   int   skipNl;

   pColTab = xmalloc((colCount + 1) * sizeof(PIBOX_COL_CFG));

   for (colIdx=0; colIdx < colCount; colIdx++)
   {
      pColTab[colIdx].pDefObj = NULL;
      pKeyword = Tcl_GetString(pColObjv[colIdx]);

      if (strncmp(pKeyword, "user_def_", 9) == 0)
      {  // cache reference to user-defined column display definition object
         pColTab[colIdx].pDefObj = Tcl_GetVar2Ex(interp, "usercols", pKeyword + 9, TCL_GLOBAL_ONLY);
         if (pColTab[colIdx].pDefObj != NULL)
            Tcl_IncrRefCount(pColTab[colIdx].pDefObj);
         else
            debug2("PiOutput-CfgColumnsCache: usercols(%s) undefined: cold colIdx %d", pKeyword + 9, colIdx);
         type = PIBOX_COL_USER_DEF;
      }
      else
      {
         type = PiOutput_GetPiColumnType(pColObjv[colIdx]);

         if (type == PIBOX_COL_REMINDER)
         {
            pColTab[colIdx].pDefObj = Tcl_GetVar2Ex(interp, "rem_col_fmt", NULL, TCL_GLOBAL_ONLY);
            if (pColTab[colIdx].pDefObj != NULL)
               Tcl_IncrRefCount(pColTab[colIdx].pDefObj);
            else
               debug1("PiOutput-CfgColumnsCache: no fmt for reminder col %d", colIdx);
         }
      }

      skipNl = FALSE;
      width  = 64;
      if (type != PIBOX_COL_INVALID)
      {
         // determine width of the theme column from the global Tcl list
         pTabObj = Tcl_GetVar2Ex(interp, "colsel_tabs", pKeyword, TCL_GLOBAL_ONLY);
         if (pTabObj != NULL)
         {
            if ( (Tcl_ListObjIndex(interp, pTabObj, 0, &pColObj) == TCL_OK) && (pColObj != NULL) &&
                 (Tcl_GetIntFromObj(interp, pColObj, &width) == TCL_OK) )
            {
               // width retrieved from Tcl list -> substract some pixels as gap to next column
               width -= 10;
            }
            else
               debugTclErr(interp, "PiOutput-CfgColumnsCache: lindex or GetInt colsel-tabs(keyword)");
         }
         else
            debug1("PiOutput-CfgColumnsCache: Tcl var 'colsel_tabs(%s)' undefined", pKeyword);

         // determine "no newline" flag from the global Tcl array
         pTabObj = Tcl_GetVar2Ex(interp, "pinetbox_rows_nonl", pKeyword, TCL_GLOBAL_ONLY);
         if (pTabObj != NULL)
         {
            if (Tcl_GetBooleanFromObj(interp, pTabObj, &skipNl) != TCL_OK)
               debugTclErr(interp, "PiOutput-CfgColumnsCache: lindex or GetBool pinetbox_rows_nonl(keyword)");
         }
      }

      pColTab[colIdx].type        = type;
      pColTab[colIdx].width       = width;
      pColTab[colIdx].skipNewline = skipNl;
   }

   return pColTab;
}

// ----------------------------------------------------------------------------
// Configure browser listing columns
// - Additionally the tab-stops in the text widget must be defined for the
//   width of the respective columns: Tcl/Tk proc UpdatePiListboxColumns
// - Also, the listbox must be refreshed
//
static int PiOutput_CfgPiColumns( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const PIBOX_COL_CFG * pColTab;
   Tcl_Obj  * pTmpObj;
   Tcl_Obj ** pColObjv;
   int        box_type;
   int        colCount;
   int   result = TCL_ERROR;

   pTmpObj = Tcl_GetVar2Ex(interp, "pibox_type", NULL, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpObj != NULL)
   {
      if (Tcl_GetIntFromObj(interp, pTmpObj, &box_type) == TCL_OK)
      {
         if (box_type == 0)
            pTmpObj = Tcl_GetVar2Ex(interp, "pilistbox_cols", NULL, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
         else
            pTmpObj = Tcl_GetVar2Ex(interp, "pinetbox_rows", NULL, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);

         if (pTmpObj != NULL)
         {
            result = Tcl_ListObjGetElements(interp, pTmpObj, &colCount, &pColObjv);
            if (result == TCL_OK)
            {
               pColTab = PiOutput_CfgColumnsCache(colCount, pColObjv);

               // discard the previous configuration
               if (pPiboxColCfg != defaultPiboxCols)
                  PiOutput_CfgColumnsClear(pPiboxColCfg, piboxColCount);

               // set the new configuration
               pPiboxColCfg  = pColTab;
               piboxColCount = colCount;
            }
            else
               debugTclErr(interp, "PiOutput-CfgPiColumns: GetElem #0 pilistbox-cols");
         }
         else
            debugTclErr(interp, "PiOutput-CfgPiColumns: GetVar pilistbox-cols");
      }
      else
         debugTclErr(interp, "PiOutput-CfgPiColumns: parse pibox_type");
   }
   else
      debug0("PiOutput-CfgPiColumns: pibox_type undefined");

   return result;
}

// ----------------------------------------------------------------------------
// Build and insert a PI listbox row into the text widget
// - a row consists of a configurable sequence of pre- and user-defined column types
// - pre-defined columns consist of simple text, but user-defined columns may
//   consist of an image or use additional formatting (e.g. bold or underlined text)
// - for efficiency, text with equivalent format is concatenated and inserted as one string
// - images are inserted into the text afterwards (but at most one is buffered)
// - column (left) alignment is implemented by use of TAB characters; all column
//   texts must be measured and cut off to not exceed the column max width
//
void PiOutput_PiListboxInsert( const PI_BLOCK *pPiBlock, uint textrow )
{
   Tk_Font   piboxFont;
   Tk_Font   piBoldFont;
   Tcl_Obj * fontNameObj;
   Tcl_Obj * pFmtObj, * pImageObj;
   Tcl_Obj * objv[10];
   Tcl_Obj * pLastFmtObj;
   Tcl_Obj * pBgFmtObj;
   char      linebuf[15];
   char      imgCmdBuf[250];
   PIBOX_TCLOBJ    timeTag;
   PIBOX_COL_TYPES type;
   bool  isBoldFont;
   uint  dummy;
   uint  maxRowLen;
   uint  textcol;
   uint  len, off, maxlen;
   uint  idx;
   time_t curTime;

   curTime = EpgGetUiMinuteTime();
   if (pPiBlock->stop_time <= curTime)
      timeTag = TCLOBJ_STR_PAST;
   else if (pPiBlock->start_time <= curTime)
      timeTag = TCLOBJ_STR_NOW;
   else
      timeTag = TCLOBJ_STR_THEN;

   // assemble a command vector, starting with the widget name/command
   objv[0] = tcl_obj[TCLOBJ_WID_LIST];
   objv[1] = tcl_obj[TCLOBJ_STR_INSERT];
   objv[2] = tcl_obj[TCLOBJ_STR_TEXT_IDX];
   objv[3] = tcl_obj[TCLOBJ_STR_TEXT_ANY];
   objv[4] = tcl_obj[TCLOBJ_STR_LIST_ANY];

   maxRowLen = TCL_COMM_BUF_SIZE;
   textcol = 0;
   off = 0;
   pLastFmtObj = NULL;
   imgCmdBuf[0] = 0;
   isBoldFont = FALSE;
   piboxFont = piBoldFont = NULL;
   pBgFmtObj = NULL;

   // open the (proportional) fonts which are used to display the text
   fontNameObj = Tcl_GetVar2Ex(interp, "pi_font", NULL, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (fontNameObj != NULL)
      piboxFont = Tk_AllocFontFromObj(interp, Tk_MainWindow(interp), fontNameObj);
   else
      debugTclErr(interp, "PiOutput-PiListboxInsert: variable 'pi_font' undefined");

   fontNameObj = Tcl_GetVar2Ex(interp, "pi_bold_font", NULL, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (fontNameObj != NULL)
      piBoldFont = Tk_AllocFontFromObj(interp, Tk_MainWindow(interp), fontNameObj);
   else
      debugTclErr(interp, "PiOutput-PiListboxInsert: variable 'pi_bold_font' undefined");

   if ( (piboxFont != NULL) && (piBoldFont != NULL) )
   {
      // loop across all configured columns
      for (idx=0; idx < piboxColCount; idx++)
      {
         type = pPiboxColCfg[idx].type;
         len  = 0;
         pImageObj = pFmtObj = NULL;

         if ((type == PIBOX_COL_USER_DEF) || (type == PIBOX_COL_REMINDER))
         {
            len = PiOutput_MatchUserCol(pPiBlock, &type, pPiboxColCfg[idx].pDefObj,
                                        comm + off, maxRowLen - off, &pImageObj, &pFmtObj);
         }

         if ((type != PIBOX_COL_USER_DEF) && (type != PIBOX_COL_REMINDER) && (type != PIBOX_COL_WEEKCOL))
            len = PiOutput_PrintColumnItem(pPiBlock, type, comm + off, maxRowLen - off);

         if ( (off > 0) &&
              ( (type == PIBOX_COL_WEEKCOL) ||
                (pLastFmtObj != pFmtObj) ||
                ((pImageObj != NULL) && (imgCmdBuf[0] != 0)) ))
         {  // format change or image to be inserted -> display the text currently in the buffer
            Tcl_SetStringObj(objv[3], comm, off);
            if (Tcl_EvalObjv(interp, 5, objv, 0) != TCL_OK)
               debugTclErr(interp, "PiOutput-PiListboxInsert");
            textcol += off;

            memmove(comm, comm + off, len);
            off = 0;
         }

         if (pImageObj != NULL)
         {  // user-defined column consists of an image
            Tcl_Obj ** pImgObjv;
            Tcl_Obj  * pImgSpec;
            int        imgObjc, imgWidth;

            if (imgCmdBuf[0] != 0)
            {  // display the image currently in the buffer
               eval_global(interp, imgCmdBuf);
               imgCmdBuf[0] = 0;
               textcol += 1;
            }
            if ((off == 0) && (off < maxRowLen))
            {  // in the first column a space character must be pre-pended because text-tags must be assigned
               // both left and right to the image position to define the background-color for tranparent images
               comm[off] = ' ';
               len = 1;
            }
            pImgSpec = Tcl_GetVar2Ex(interp, "pi_img", Tcl_GetString(pImageObj), TCL_GLOBAL_ONLY);
            if (pImgSpec != NULL)
            {
               if ( (Tcl_ListObjGetElements(interp, pImgSpec, &imgObjc, &pImgObjv) == TCL_OK) &&
                    (imgObjc == EPGTCL_PIMG_IDX_COUNT) &&
                    (Tcl_GetIntFromObj(interp, pImgObjv[EPGTCL_PIMG_WIDTH_IDX], &imgWidth) == TCL_OK) &&
                    ((imgWidth + 5 + len*5) < pPiboxColCfg[idx].width) )
               {
                  // build the image insert command in a buffer
                  sprintf(imgCmdBuf, ".all.pi.list.text image create %d.%d -image %s -padx %d",
                                      textrow+1, textcol + off + len, Tcl_GetString(pImgObjv[0]),
                                      (pPiboxColCfg[idx].width - (imgWidth + 2 + len*5)) / 2);
               }
            }
         }

         if (type == PIBOX_COL_WEEKCOL)
         {
            // special handling for weekday color column: no text, just bg color
            // (note: any remaining text in the cache has already been written out above)
            Tcl_Obj * TmpObjv[2];
            int oldCount, spaceWidth;
            uint sumWidth;
            struct tm * ptm;

            sprintf(linebuf, "%d.%d", textrow+1, textcol);
            Tcl_SetStringObj(objv[2], linebuf, -1);

            Tk_MeasureChars(piboxFont, " ", 1, -1, 0, &spaceWidth);
            if (spaceWidth == 0)  // fail-safety
               spaceWidth = 4;
            len = 0;
            for (sumWidth=spaceWidth; sumWidth < pPiboxColCfg[idx].width; sumWidth += spaceWidth)
               comm[len++] = ' ';
            Tcl_SetStringObj(objv[3], comm, len);

            ptm = localtime(&pPiBlock->start_time);
            sprintf(linebuf, "ag_day%d", (ptm->tm_wday + 1) % 7);
            Tcl_SetStringObj(tcl_obj[TCLOBJ_STR_TEXT_FMT], linebuf, 6+1);
            TmpObjv[0] = tcl_obj[TCLOBJ_STR_TEXT_FMT];
            TmpObjv[1] = tcl_obj[timeTag];
            Tcl_ListObjLength(interp, objv[4], &oldCount);
            Tcl_ListObjReplace(interp, objv[4], 0, oldCount, 2, TmpObjv);

            if (Tcl_EvalObjv(interp, 5, objv, 0) != TCL_OK)
               debugTclErr(interp, "PiOutput-PiListboxInsert");
            textcol += len;
            pLastFmtObj = pFmtObj = NULL;
            len = off = 0;
         }

         if (off == 0)
         {  // text cache empty -> initialize it
            // (note: even if the column is empty, at least a tab or newline is appended)
            Tcl_Obj ** pFmtObjv;
            int oldCount, fmtCount;
            Tcl_ListObjLength(interp, objv[4], &oldCount);

            // line number was specified; add 1 because first line is 1.0
            sprintf(linebuf, "%d.%d", textrow+1, textcol);
            Tcl_SetStringObj(objv[2], linebuf, -1);
            if ( (pFmtObj != NULL) &&
                 (Tcl_ListObjGetElements(interp, pFmtObj, &fmtCount, &pFmtObjv) == TCL_OK) &&
                 (fmtCount > 0) )
            {
               if (strncmp(Tcl_GetString(pFmtObjv[fmtCount - 1]), "bg_", 3) == 0)
               {  // background color for entire column: must be the last format element in list
                  pBgFmtObj = pFmtObjv[fmtCount - 1];
                  fmtCount -= 1;
               }
               Tcl_ListObjReplace(interp, objv[4], 0, oldCount, fmtCount, pFmtObjv);
               Tcl_ListObjAppendElement(interp, objv[4], tcl_obj[timeTag]);
               isBoldFont = (strcmp(Tcl_GetString(pFmtObjv[0]), "bold") == 0);
            }
            else
               Tcl_ListObjReplace(interp, objv[4], 0, oldCount, 1, &tcl_obj[timeTag]);
            pLastFmtObj = pFmtObj;
         }

         if (len > 0)
         {
            // check how many chars of the string fit the column width
            maxlen = Tk_MeasureChars(isBoldFont ? piBoldFont : piboxFont, comm + off, len, pPiboxColCfg[idx].width, 0, &dummy);

            // strip word fragments from the end of shortened theme strings
            if ((type == PIBOX_COL_THEME) && (maxlen < len) && (maxlen > 3))
            {
               if (alphaNumTab[(uchar)comm[off + maxlen - 1]] == ALNUM_NONE)
                  maxlen -= 1;
               else if (alphaNumTab[(uchar)comm[off + maxlen - 2]] == ALNUM_NONE)
                  maxlen -= 2;
               else if (alphaNumTab[(uchar)comm[off + maxlen - 3]] == ALNUM_NONE)
                  maxlen -= 3;
            }
            len = maxlen;
            off += len;
         }

         // append TAB or NEWLINE character to the column text
         if (off < maxRowLen)
            comm[off++] = ((idx + 1 < piboxColCount) ? '\t' : '\n');

      } // end of loop across all columns

      if (off > 0)
      {  // display remaining text in the buffer
         Tcl_SetStringObj(objv[3], comm, off);
         if (Tcl_EvalObjv(interp, 5, objv, 0) != TCL_OK)
            debugTclErr(interp, "PiOutput-PiListboxInsert");
      }
      if (imgCmdBuf[0] != 0)
      {  // display the last image
         eval_global(interp, imgCmdBuf);
      }
      if (pBgFmtObj != NULL)
      {  // set background color for entire column, if present in any column
         sprintf(imgCmdBuf, ".all.pi.list.text tag add {%s} %d.0 %d.0",
                            Tcl_GetString(pBgFmtObj), textrow+1, textrow+2);
         eval_global(interp, imgCmdBuf);
      }
   }
   else
      debug1("PiOutput-PiListboxInsert: '%s' alloc failed", ((piboxFont == NULL) ? "pi_font" : "pi_bold_font"));

   if (piboxFont != NULL)
      Tk_FreeFont(piboxFont);
   if (piBoldFont != NULL)
      Tk_FreeFont(piBoldFont);
}

// ----------------------------------------------------------------------------
// Insert a programme into a network column starting at the given row
// - returns the number of text lines which were inserted
//
uint PiOutput_PiNetBoxInsert( const PI_BLOCK * pPiBlock, uint colIdx, sint textRow )
{
   Tk_Font   piboxFont;
   Tk_Font   piBoldFont;
   Tcl_Obj * fontNameObj;
   Tcl_Obj * pFmtObj, * pImageObj;
   Tcl_Obj * objv[10];
   Tcl_Obj * pLastFmtObj;
   Tcl_Obj * pBgFmtObj;
   char      linebuf[15];
   char      imgCmdBuf[250];
   PIBOX_COL_TYPES type;
   uint  maxRowLen;
   uint  len, off;
   uint  rowIdx;
   uint  bufCol;
   uint  bufHeight;
   uint  imgRow;
   PIBOX_TCLOBJ    timeTag;
   time_t curTime;

   curTime = EpgGetUiMinuteTime();
   if (pPiBlock->stop_time <= curTime)
      timeTag = TCLOBJ_STR_PAST;
   else if (pPiBlock->start_time <= curTime)
      timeTag = TCLOBJ_STR_NOW;
   else
      timeTag = TCLOBJ_STR_THEN;

   Tcl_SetStringObj(tcl_obj[TCLOBJ_WID_NET], comm, sprintf(comm, ".all.pi.list.nets.n_%d", colIdx));

   objv[0] = tcl_obj[TCLOBJ_WID_NET];
   objv[1] = tcl_obj[TCLOBJ_STR_INSERT];
   objv[2] = tcl_obj[TCLOBJ_STR_TEXT_IDX];
   objv[3] = tcl_obj[TCLOBJ_STR_TEXT_ANY];
   objv[4] = tcl_obj[TCLOBJ_STR_LIST_ANY];

   maxRowLen = TCL_COMM_BUF_SIZE;
   bufCol = 0;
   bufHeight = 0;
   off = 0;
   pLastFmtObj = NULL;
   pBgFmtObj = NULL;
   imgCmdBuf[0] = 0;
   imgRow = 0;
   piboxFont = piBoldFont = NULL;

   // open the (proportional) fonts which are used to display the text
   fontNameObj = Tcl_GetVar2Ex(interp, "pi_font", NULL, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (fontNameObj != NULL)
      piboxFont = Tk_AllocFontFromObj(interp, Tk_MainWindow(interp), fontNameObj);
   else
      debugTclErr(interp, "PiOutput-PiListboxInsert: variable 'pi_font' undefined");

   fontNameObj = Tcl_GetVar2Ex(interp, "pi_bold_font", NULL, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (fontNameObj != NULL)
      piBoldFont = Tk_AllocFontFromObj(interp, Tk_MainWindow(interp), fontNameObj);
   else
      debugTclErr(interp, "PiOutput-PiListboxInsert: variable 'pi_bold_font' undefined");

   if ( (piboxFont != NULL) && (piBoldFont != NULL) )
   {
      // loop across all configured elements
      for (rowIdx=0; rowIdx < piboxColCount; rowIdx++)
      {
         type = pPiboxColCfg[rowIdx].type;
         len  = 0;
         pImageObj = pFmtObj = NULL;

         if ((type == PIBOX_COL_USER_DEF) || (type == PIBOX_COL_REMINDER))
         {
            len = PiOutput_MatchUserCol(pPiBlock, &type, pPiboxColCfg[rowIdx].pDefObj,
                                        comm + off, maxRowLen - off, &pImageObj, &pFmtObj);
         }

         if ((type != PIBOX_COL_USER_DEF) && (type != PIBOX_COL_REMINDER) && (type != PIBOX_COL_WEEKCOL))
            len = PiOutput_PrintColumnItem(pPiBlock, type, comm + off, maxRowLen - off);

         if ( (len == 0) && (pImageObj == NULL) &&
              (type != PIBOX_COL_WEEKCOL) &&
              ((bufCol == 0) || pPiboxColCfg[rowIdx].skipNewline) )
         {  // no output & no newline -> skip this element
            continue;
         }

         if ( (off > 0) &&
              ( (pLastFmtObj != pFmtObj) ||
                (type == PIBOX_COL_WEEKCOL) ||
                ((pImageObj != NULL) && (imgCmdBuf[0] != 0))) )
         {  // format change or image to be inserted -> display the text currently in the buffer
            Tcl_SetStringObj(objv[3], comm, off);
            if (Tcl_EvalObjv(interp, 5, objv, 0) != TCL_OK)
               debugTclErr(interp, "PiOutput-PiListboxInsert");

            memmove(comm, comm + off, len);
            off = 0;
         }

         if (pImageObj != NULL)
         {  // user-defined column consists of an image
            Tcl_Obj ** pImgObjv;
            Tcl_Obj  * pImgSpec;
            int        imgObjc;

            if (imgCmdBuf[0] != 0)
            {  // display the image currently in the buffer
               eval_global(interp, imgCmdBuf);
               imgCmdBuf[0] = 0;
               if (imgRow == bufHeight)
                  bufCol += 1;
            }
            if ((bufHeight == 0) && (bufCol == 0) && (off < maxRowLen))
            {  // in the first column a space character must be pre-pended because text-tags must be assigned
               // both left and right to the image position to define the background-color for tranparent images
               comm[off] = ' ';
               len = 1;
            }
            pImgSpec = Tcl_GetVar2Ex(interp, "pi_img", Tcl_GetString(pImageObj), TCL_GLOBAL_ONLY);
            if (pImgSpec != NULL)
            {
               if ( (Tcl_ListObjGetElements(interp, pImgSpec, &imgObjc, &pImgObjv) == TCL_OK) && (imgObjc == EPGTCL_PIMG_IDX_COUNT) )
               {
                  // build the image insert command in a buffer
                  sprintf(imgCmdBuf, ".all.pi.list.nets.n_%d image create %d.%d -image %s -padx 2",
                                      colIdx, textRow + bufHeight + 1, bufCol + len, Tcl_GetString(pImgObjv[EPGTCL_PIMG_NAME_IDX]));
                  imgRow = bufHeight;
               }
            }
         }

         if (type == PIBOX_COL_WEEKCOL)
         {
            // special handling for weekday color column: no text, just bg color
            // (note: any remaining text in the cache has already been written out above)
            Tcl_Obj * TmpObjv[2];
            int oldCount;
            struct tm * ptm;

            sprintf(linebuf, "%d.%d", textRow + bufHeight + 1, bufCol);
            Tcl_SetStringObj(objv[2], linebuf, -1);

            len = 2;
            Tcl_SetStringObj(objv[3], "  ", len);

            ptm = localtime(&pPiBlock->start_time);
            sprintf(linebuf, "ag_day%d", (ptm->tm_wday + 1) % 7);
            Tcl_SetStringObj(tcl_obj[TCLOBJ_STR_TEXT_FMT], linebuf, 6+1);
            TmpObjv[0] = tcl_obj[TCLOBJ_STR_TEXT_FMT];
            TmpObjv[1] = tcl_obj[timeTag];
            Tcl_ListObjLength(interp, objv[4], &oldCount);
            Tcl_ListObjReplace(interp, objv[4], 0, oldCount, 2, TmpObjv);

            if (Tcl_EvalObjv(interp, 5, objv, 0) != TCL_OK)
               debugTclErr(interp, "PiOutput-PiListboxInsert");
            pLastFmtObj = pFmtObj = NULL;
            bufCol += len;
            len = off = 0;
         }

         if (off == 0)
         {  // text cache empty -> initialize it
            // (note: even if the column is empty, at least a blank or newline is appended)
            Tcl_Obj ** pFmtObjv;
            int oldCount, fmtCount;
            Tcl_ListObjLength(interp, objv[4], &oldCount);

            sprintf(linebuf, "%d.%d", textRow + bufHeight + 1, bufCol);
            Tcl_SetStringObj(objv[2], linebuf, -1);
            if ( (pFmtObj != NULL) &&
                 (Tcl_ListObjGetElements(interp, pFmtObj, &fmtCount, &pFmtObjv) == TCL_OK) &&
                 (fmtCount > 0) )
            {
               if (strncmp(Tcl_GetString(pFmtObjv[fmtCount - 1]), "bg_", 3) == 0)
               {  // background color for entire element: must be the last format element in list
                  pBgFmtObj = pFmtObjv[fmtCount - 1];
                  fmtCount -= 1;
               }
               Tcl_ListObjReplace(interp, objv[4], 0, oldCount, fmtCount, pFmtObjv);
               Tcl_ListObjAppendElement(interp, objv[4], tcl_obj[timeTag]);
            }
            else
               Tcl_ListObjReplace(interp, objv[4], 0, oldCount, 1, &tcl_obj[timeTag]);
            pLastFmtObj = pFmtObj;
         }

         off    += len;
         bufCol += len;

         // append separator after the element: newline or space
         if (off < maxRowLen)
         {
            if ( (pPiboxColCfg[rowIdx].skipNewline == FALSE) ||
                 (rowIdx + 1 >= piboxColCount) )
            {  // append NEWLINE character to the element (a newline is forced after the last one)
               comm[off++] = '\n';
               bufHeight += 1;
               bufCol = 0;
            }
            else
            {  // append space
               comm[off++] = ' ';
               bufCol += 1;
            }
         }

      } // end of loop across all columns

      if (off > 0)
      {  // display remaining text in the buffer
         Tcl_SetStringObj(objv[3], comm, off);
         if (Tcl_EvalObjv(interp, 5, objv, 0) != TCL_OK)
            debugTclErr(interp, "PiOutput-PiListboxInsert");
      }
      if (imgCmdBuf[0] != 0)
      {  // display the last image
         eval_global(interp, imgCmdBuf);
      }
   }

   // append an empty line (as gap betwen PI) make sure the element is at least 3 lines high
   do
   {
      int oldCount;
      Tcl_ListObjLength(interp, objv[4], &oldCount);
      Tcl_ListObjReplace(interp, objv[4], 0, oldCount, 0, NULL);
      Tcl_SetStringObj(objv[3], "\n", 1);
      sprintf(linebuf, "%d.0", textRow + bufHeight + 1);
      Tcl_SetStringObj(objv[2], linebuf, -1);
      if (Tcl_EvalObjv(interp, 5, objv, 0) != TCL_OK)
         debugTclErr(interp, "PiOutput-PiListboxInsert");
      bufHeight += 1;
   }
   while (bufHeight < 3);

   if (pBgFmtObj != NULL)
   {  // set background color for all lines except the gap
      sprintf(imgCmdBuf, ".all.pi.list.nets.n_%d tag add {%s} %d.0 %d.0",
                         colIdx, Tcl_GetString(pBgFmtObj), textRow + 1, textRow + bufHeight);
      eval_global(interp, imgCmdBuf);
   }

   if (piboxFont != NULL)
      Tk_FreeFont(piboxFont);
   if (piBoldFont != NULL)
      Tk_FreeFont(piBoldFont);

   return bufHeight;
}

// ----------------------------------------------------------------------------
// Free resources allocated by this module (cleanup during program exit)
//
void PiOutput_Destroy( void )
{
   uint  idx;

   if (pPiboxColCfg != defaultPiboxCols)
      PiOutput_CfgColumnsClear(pPiboxColCfg, piboxColCount);
   pPiboxColCfg  = defaultPiboxCols;
   piboxColCount = DEFAULT_PIBOX_COL_COUNT;

   for (idx=0; idx < TCLOBJ_COUNT; idx++)
      Tcl_DecrRefCount(tcl_obj[idx]);
   memset(tcl_obj, 0, sizeof(tcl_obj));
}

// ----------------------------------------------------------------------------
// Create the Tcl/Tk commands provided by this module
// - this should be called only once during start-up
//
void PiOutput_Init( void )
{
   Tcl_CmdInfo cmdInfo;
   uint  idx;

   if (Tcl_GetCommandInfo(interp, "C_PiOutput_CfgPiColumns", &cmdInfo) == 0)
   {
      Tcl_CreateObjCommand(interp, "C_PiOutput_CfgColumns", PiOutput_CfgPiColumns, (ClientData) NULL, NULL);

      memset(&tcl_obj, 0, sizeof(tcl_obj));
      tcl_obj[TCLOBJ_WID_LIST]      = Tcl_NewStringObj(".all.pi.list.text", -1);
      tcl_obj[TCLOBJ_WID_NET]       = Tcl_NewStringObj(".all.pi.nets.n_####", -1);
      tcl_obj[TCLOBJ_STR_INSERT]    = Tcl_NewStringObj("insert", -1);
      tcl_obj[TCLOBJ_STR_NOW]       = Tcl_NewStringObj("now", -1);
      tcl_obj[TCLOBJ_STR_PAST]      = Tcl_NewStringObj("past", -1);
      tcl_obj[TCLOBJ_STR_THEN]      = Tcl_NewStringObj("then", -1);
      tcl_obj[TCLOBJ_STR_TEXT_IDX]  = Tcl_NewStringObj("#####", -1);
      tcl_obj[TCLOBJ_STR_TEXT_ANY]  = Tcl_NewStringObj("", -1);
      tcl_obj[TCLOBJ_STR_TEXT_FMT]  = Tcl_NewStringObj("", -1);
      tcl_obj[TCLOBJ_STR_LIST_ANY]  = Tcl_NewListObj(0, NULL);

      for (idx=0; idx < TCLOBJ_COUNT; idx++)
         Tcl_IncrRefCount(tcl_obj[idx]);
   }
   else
      fatal0("PiOutput-Create: commands were already created");
}

