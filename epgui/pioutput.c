/*
 *  Nextview GUI: Output of PI data in various formats
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
 *    This module implements methods to display PI data, e.g. programme
 *    title, running times, descrition and feature data. They are used
 *    by the programme listbox, short info window, PI popup window and
 *    the HTML dump.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: pioutput.c,v 1.43 2002/11/24 09:51:33 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef WIN32
#include <signal.h>
#endif

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
#include "epgui/pilistbox.h"
#include "epgui/pioutput.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"
#define USERCMD_PREFIX_WINTV  "!wintv!"
#define USERCMD_PREFIX_WINTV_LEN  7

// ----------------------------------------------------------------------------
// Array which keeps pre-allocated Tcl/Tk string objects
//
typedef enum
{
   TCLOBJ_WID_LIST,
   TCLOBJ_STR_INSERT,
   TCLOBJ_STR_NOW,
   TCLOBJ_STR_THEN,
   TCLOBJ_STR_TEXT_IDX,
   TCLOBJ_STR_TEXT_ANY,
   TCLOBJ_STR_LIST_ANY,
   TCLOBJ_COUNT
} PIBOX_TCLOBJ;

static Tcl_Obj * tcl_obj[TCLOBJ_COUNT];

// Definition of PI listbox column types - must match keyword list below
typedef enum
{
   PIBOX_COL_DAY,
   PIBOX_COL_DAY_MONTH,
   PIBOX_COL_DAY_MONTH_YEAR,
   PIBOX_COL_DESCR,
   PIBOX_COL_DURATION,
   PIBOX_COL_ED_RATING,
   PIBOX_COL_FORMAT,
   PIBOX_COL_LIVE_REPEAT,
   PIBOX_COL_NETNAME,
   PIBOX_COL_PAR_RATING,
   PIBOX_COL_PIL,
   PIBOX_COL_SOUND,
   PIBOX_COL_SUBTITLES,
   PIBOX_COL_THEME,
   PIBOX_COL_TIME,
   PIBOX_COL_TITLE,
   PIBOX_COL_WEEKDAY,
   PIBOX_COL_USER_DEF,
   PIBOX_COL_INVALID
} PIBOX_COL_TYPES;

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
   // "user_def_",
   // "invalid",
   NULL
};

// cache for PI listbox column configuration
typedef struct
{
   PIBOX_COL_TYPES  type;
   uint             width;
   Tcl_Obj        * pDefObj;
} PIBOX_COL_CFG;

// Emergency fallback for column configuration
// (should never be used because tab-stops and column header buttons will not match)
static const PIBOX_COL_CFG defaultPiboxCols[] =
{
   { PIBOX_COL_NETNAME,  60, NULL },
   { PIBOX_COL_TIME,     83, NULL },
   { PIBOX_COL_WEEKDAY,  30, NULL },
   { PIBOX_COL_TITLE,   266, NULL },
};
#define DEFAULT_PIBOX_COL_COUNT (sizeof(defaultPiboxCols) / sizeof(PIBOX_COL_CFG))

// pointer to a list of the currently configured column types
static const PIBOX_COL_CFG * pPiboxColCfg = defaultPiboxCols;
static uint                  piboxColCount = DEFAULT_PIBOX_COL_COUNT;

// struct to hold dynamically growing char buffer
typedef struct
{
   char   * strbuf;         // pointer to the allocated buffer
   uint     size;           // number of allocated bytes in the buffer
   uint     off;            // number of already used bytes
   bool     quoteShell;     // TRUE to enable UNIX quoting of spaces etc.
} DYN_CHAR_BUF;

// ----------------------------------------------------------------------------
// Table to implement isalnum() for all latin fonts
//
static const schar alphaNumTab[256] =
{
/* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0 */
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 1 */
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 2 */
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  /* 3 */ // 0 - 9
   0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  /* 4 */ // A - Z
   2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0,  /* 5 */
   0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 6 */ // a - z
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0,  /* 7 */
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 8 */
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 9 */
   0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  /* A */ // national
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  /* B */
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  /* C */
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  /* D */
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  /* E */
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  /* F */
/* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
};
#define ALNUM_NONE    0
#define ALNUM_DIGIT   1
#define ALNUM_UCHAR   2
#define ALNUM_LCHAR  -1
#define ALNUM_NATION  4

// ----------------------------------------------------------------------------
// Prepare title for alphabetical sorting
// - remove appended series counter from title string =~ s/ \(\d+\)$//
// - move attribs "Der, Die, Das" to the end of the title for sorting
//
const char * PiOutput_DictifyTitle( const char * pTitle, uchar lang, char * outbuf, uint maxLen )
{
   uchar *pe;
   uint cut, len;

   // remove appended series counter from title string =~ s/ \(\d+\)$//
   len = strlen(pTitle);
   if ((len >= 5) && (len < 100) && (*(pTitle + len - 1) == ')'))
   {  // found closing brace at end of string -> check for preceeding decimal number
      pe = (uchar *)pTitle + len - 1 - 1;
      len -= 1;
      if (isdigit(*pe))
      {  // at least one digit found
         len -= 1;
         pe -= 1;
         // skip preceeding digits
         while ((len > 0) && isdigit(*pe))
         {
            pe--;
            len--;
         }
         // check for the opening brace and a space before it
         if ((len > 2) && (*pe == '(') && (*(pe - 1) == ' '))
         {
            //dprintf2("EpgDbSeries-AddPi: stripping '%s' from title '%s'\n", pTitle + len - 2, pTitle);
            len -= 2;
            // copy the title string up to before the braces into the output buffer
            strncpy(outbuf, pTitle, len);
            outbuf[len] = 0;
            pTitle = outbuf;
         }
      }
   }

   // check for attribs "Der, Die, Das" at the start of the string
   cut = 0;
   switch (lang)
   {
      case 0:
         // English
         if ((tolower(pTitle[0]) == 't') && (tolower(pTitle[1]) == 'h') && (tolower(pTitle[2]) == 'e') && (pTitle[3] == ' '))
            cut = 4;
         else if ((tolower(pTitle[0]) == 'a') && (pTitle[1] == ' '))
            cut = 2;
         break;
      case 1:
         // German
         if (tolower(pTitle[0]) == 'd')
         {
            if      ((tolower(pTitle[1]) == 'e') && (tolower(pTitle[2]) == 'r') && (pTitle[3] == ' '))
               cut = 4;
            else if ((tolower(pTitle[1]) == 'i') && (tolower(pTitle[2]) == 'e') && (pTitle[3] == ' '))
               cut = 4;
            else if ((tolower(pTitle[1]) == 'a') && (tolower(pTitle[2]) == 's') && (pTitle[3] == ' '))
               cut = 4;
         }
         else if (tolower(pTitle[0]) == 'e')
         {
            if ((tolower(pTitle[1]) == 'i') && (tolower(pTitle[2]) == 'n'))
            {
               if (pTitle[3] == ' ')
                  cut = 4;
               else if ((pTitle[3] == 'e') && (pTitle[4] == ' '))
                  cut = 5;
            }
         }
         break;
      case 3:
         // Italian
         if ((tolower(pTitle[0]) == 'u') && (tolower(pTitle[1]) == 'n'))
         {
            if      ((tolower(pTitle[2]) == 'a') && (pTitle[3] == ' '))
               cut = 4;
            else if ((tolower(pTitle[2]) == 'o') && (pTitle[3] == ' '))
               cut = 4;
         }
         else if ((tolower(pTitle[0]) == 'l') && (tolower(pTitle[1]) == 'a') && (pTitle[2] == ' '))
            cut = 3;
         break;
      case 4:
         // French
         if ((tolower(pTitle[0]) == 'u') && (tolower(pTitle[1]) == 'n'))
         {
            if (pTitle[2] == ' ')
               cut = 3;
            else if ((tolower(pTitle[2]) == 'e') && (pTitle[3] == ' '))
               cut = 4;
         }
         else if (tolower(pTitle[0]) == 'l')
         {
            if      ((tolower(pTitle[1]) == 'a') && (pTitle[2] == ' '))
               cut = 3;
            else if ((tolower(pTitle[1]) == 'e') && (pTitle[2] == ' '))
               cut = 3;
            else if (tolower(pTitle[1]) == '\'')
               cut = 2;
            else if ((tolower(pTitle[1]) == 'e') && (tolower(pTitle[2]) == 's') && (pTitle[3] == ' '))
               cut = 4;
         }
         break;
      default:
         break;
   }

   // move attribs "Der, Die, Das" to the end of the title
   if (cut > 0)
   {
      if (pTitle != outbuf)
      {  // copy the title string to the output buffer while swapping attribs part and the rest
         strncpy(outbuf, pTitle + cut, maxLen);
         strcat(outbuf, ", ");
         len = strlen(outbuf);
         strncpy(outbuf + len, pTitle, cut);
         outbuf[len + cut] = 0;
         pTitle = outbuf;
      }
      else
      {  // title was already modified for brace removal -> is already copied in the output buffer
         // title string has to be modified "in-place"
         uchar buf[10];

         // save the attrib sub-string into a temporary buffer
         strncpy(buf, outbuf, cut);
         buf[cut] = 0;
         // shift the rest of the title string forward by the length of the attrib sub-string
         for (pe=outbuf + cut; *pe; pe++)
            *(pe - cut) = *pe;
         // append the attrib sub-string after a comma
         strcpy(pe - cut, ", ");
         strcpy(pe - cut + 2, buf);
      }
      // remove trailing whitespace
      len = strlen(outbuf);
      if ((len > 0) && (outbuf[len - 1] == ' '))
         outbuf[len - 1] = 0;
   }

   // force the new first title character to be uppercase (for sorting)
   if (alphaNumTab[(uint) pTitle[0]] == ALNUM_LCHAR)
   {
      if (pTitle != outbuf)
      {
         strncpy(outbuf, pTitle, maxLen);
         outbuf[maxLen - 1] = 0;
         pTitle = outbuf;
      }
      outbuf[0] = toupper(outbuf[0]);
   }

   return pTitle;
}

// ----------------------------------------------------------------------------
// Get complete VPS/PDC timestamp
//
static bool PiOutput_GetVpsTimestamp( struct tm * pVpsTime, uint pil, time_t startTime )
{
   bool result;

   memcpy(pVpsTime, localtime(&startTime), sizeof(*pVpsTime));
   // note that the VPS label represents local time
   pVpsTime->tm_sec  = 0;
   pVpsTime->tm_min  =  (pil      ) & 0x3F;
   pVpsTime->tm_hour =  (pil >>  6) & 0x1F;
   pVpsTime->tm_mon  = ((pil >> 11) & 0x0F) - 1; // range 0-11
   pVpsTime->tm_mday =  (pil >> 15) & 0x1F;
   // the rest of the elements (year, day-of-week etc.) stay the same as in
   // start_time; since a VPS label usually has the same date as the actual
   // start time this should work out well.

   result = ( (pVpsTime->tm_min < 60) && (pVpsTime->tm_hour < 24) &&
              (pVpsTime->tm_mon >= 0) && (pVpsTime->tm_mon < 12) &&
              (pVpsTime->tm_mday >= 1) && (pVpsTime->tm_mday <= 31) );

   return result;
}

// ----------------------------------------------------------------------------
// Get column type from keyword
// - maps a character string onto an enum
// - the resulting enum index is cached in the Tcl object
//
static PIBOX_COL_TYPES PiOutput_GetPiColumnType( Tcl_Obj * pKeyObj )
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
static uint PiOutput_MatchUserCol( const PI_BLOCK * pPiBlock, PIBOX_COL_TYPES * pType, Tcl_Obj * pMarkObj,
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
            if ((Tcl_ListObjIndex(interp, pFiltObjv[filtIdx], 3, &pScIdxObj) == TCL_OK) && (pScIdxObj != NULL))
            {
               if (Tcl_GetIntFromObj(interp, pScIdxObj, &scIdx) == TCL_OK)
               {
                  if ( (scIdx == -1) || (PiFilter_ContextCacheMatch(pPiBlock, scIdx)) )
                  {
                     // match found -> retrieve display format
                     if ((Tcl_ListObjIndex(interp, pFiltObjv[filtIdx], 0, &pTypeObj) == TCL_OK) && (pTypeObj != NULL) &&
                         (Tcl_ListObjIndex(interp, pFiltObjv[filtIdx], 1, &pValueObj) == TCL_OK) && (pValueObj != NULL) &&
                         (Tcl_ListObjIndex(interp, pFiltObjv[filtIdx], 2, ppFmtObj) == TCL_OK) && (*ppFmtObj != NULL))
                     {
                        if ( (Tcl_ListObjLength(interp, *ppFmtObj, &fmtCount) == TCL_OK) &&
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
                              *ppFmtObj = NULL;
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
static uint PiOutput_PrintColumnItem( const PI_BLOCK * pPiBlock, PIBOX_COL_TYPES type,
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
            switch(pPiBlock->feature_flags & 0x03)
            {
               case 1: pResult = "2-channel"; break;
               case 2: pResult = "stereo"; break;
               case 3: pResult = "surround"; break;
            }
            break;

         case PIBOX_COL_FORMAT:
            if (pPiBlock->feature_flags & 0x08)
               pResult = "PAL+";
            else if (pPiBlock->feature_flags & 0x04)
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
            if (pPiBlock->feature_flags & 0x40)
               pResult = "live";
            else if (pPiBlock->feature_flags & 0x80)
               pResult = "repeat";
            break;

         case PIBOX_COL_SUBTITLES:
            if (pPiBlock->feature_flags & 0x100)
               pResult = "ST";
            break;

         case PIBOX_COL_THEME:
            if (pPiBlock->no_themes > 0)
            {
               uchar theme;
               uint  themeIdx;

               if ( (pPiFilterContext != NULL) &&  // check req. when in cmd-line dump mode
                    (pPiFilterContext->enabledFilters & FILTER_THEMES) )
               {
                  // Search for the first theme that's not part of the filter setting.
                  // (It would be boring to print "movie" for all programmes, when there's
                  //  a movie theme filter; instead we print the first sub-theme, e.g. "sci-fi")
                  for (themeIdx=0; themeIdx < pPiBlock->no_themes; themeIdx++)
                  {  // ignore theme class
                     theme = pPiBlock->themes[themeIdx];
                     if ( (theme < 0x80) &&
                          PdcThemeIsDefined(theme) &&
                          ((pPiFilterContext->themeFilterField[theme] & pPiFilterContext->usedThemeClasses) == 0) )
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
static void PiOutput_CfgColumnsClear( const PIBOX_COL_CFG * pColTab, uint colCount )
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
static const PIBOX_COL_CFG * PiOutput_CfgColumnsCache( uint colCount, Tcl_Obj ** pColObjv )
{
   PIBOX_COL_CFG * pColTab;
   Tcl_Obj  * pTabObj;
   Tcl_Obj  * pColObj;
   char * pKeyword;
   uint  type;
   uint  colIdx;
   int   width;

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
         type = PiOutput_GetPiColumnType(pColObjv[colIdx]);

      width = 64;
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
      }

      pColTab[colIdx].type  = type;
      pColTab[colIdx].width = width;
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
   int        colCount;
   int   result;

   pTmpObj = Tcl_GetVar2Ex(interp, "pilistbox_cols", NULL, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
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
   {
      debugTclErr(interp, "PiOutput-CfgPiColumns: GetVar pilistbox-cols");
      result = TCL_ERROR;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Build and insert a PI listbox row into the text widget
// - a row consists of a configurable sequence of pre- and user-defined column types
// - pre-defined columns consist of simple text, but user-defined columns may
//   consist of an image or use additional formatting (e.g. bold or underlined text)
// - for efficiency, text with equivalent format is concatenated and inserted as one string
// - images are inserted into the text afterwards (but at most one id buffered)
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
   char      linebuf[15];
   char      imgCmdBuf[250];
   time_t    now;
   PIBOX_TCLOBJ    timeTag;
   PIBOX_COL_TYPES type;
   bool  isBoldFont;
   uint  dummy;
   uint  maxRowLen;
   uint  textcol;
   uint  len, off, maxlen;
   uint  idx;

   now = time(NULL);
   if (pPiBlock->start_time <= now)
      timeTag = TCLOBJ_STR_NOW;
   #if 0
   else if ((pPiBlock->stop_time <= now) && ((pPiFilterContext->enabledFilters & FILTER_EXPIRE_TIME) == FALSE))
      timeTag = TCLOBJ_STR_PAST;
   #endif
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

         if (type == PIBOX_COL_USER_DEF)
         {
            len = PiOutput_MatchUserCol(pPiBlock, &type, pPiboxColCfg[idx].pDefObj,
                                        comm + off, maxRowLen - off, &pImageObj, &pFmtObj);
         }

         if (type != PIBOX_COL_USER_DEF)
            len = PiOutput_PrintColumnItem(pPiBlock, type, comm + off, maxRowLen - off);

         if ( ((pLastFmtObj != pFmtObj) || ((pImageObj != NULL) && (imgCmdBuf[0] != 0))) && 
              (off > 0))
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
               textcol += 1;
            }
            if ((textcol + off == 0) && (off < maxRowLen))
            {  // in the first column a space character must be pre-pended because text-tags must be assigned
               // both left and right to the image position to define the background-color for tranparent images
               comm[off] = ' ';
               len = 1;
            }
            pImgSpec = Tcl_GetVar2Ex(interp, "pi_img", Tcl_GetString(pImageObj), TCL_GLOBAL_ONLY);
            if (pImgSpec != NULL)
            {
               if ( (Tcl_ListObjGetElements(interp, pImgSpec, &imgObjc, &pImgObjv) == TCL_OK) &&
                    (imgObjc == 2) &&
                    (Tcl_GetIntFromObj(interp, pImgObjv[1], &imgWidth) == TCL_OK) &&
                    ((imgWidth + 5 + len*5) < pPiboxColCfg[idx].width) )
               {
                  // build the image insert command in a buffer
                  sprintf(imgCmdBuf, ".all.pi.list.text image create %d.%d -image %s -padx %d",
                                      textrow+1, textcol + off + len, Tcl_GetString(pImgObjv[0]),
                                      (pPiboxColCfg[idx].width - (imgWidth + 2 + len*5)) / 2);
               }
            }
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
            if (pFmtObj != NULL)
            {
               Tcl_ListObjGetElements(interp, pFmtObj, &fmtCount, &pFmtObjv);
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
               if (alphaNumTab[(uint)comm[off + maxlen - 1]] == ALNUM_NONE)
                  maxlen -= 1;
               else if (alphaNumTab[(uint)comm[off + maxlen - 2]] == ALNUM_NONE)
                  maxlen -= 2;
               else if (alphaNumTab[(uint)comm[off + maxlen - 3]] == ALNUM_NONE)
                  maxlen -= 3;
            }
            len = maxlen;
         }
         off += len;

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
   }
   else
      debug1("PiOutput-PiListboxInsert: '%s' alloc failed", ((piboxFont == NULL) ? "pi_font" : "pi_bold_font"));

   if (piboxFont != NULL)
      Tk_FreeFont(piboxFont);
   if (piBoldFont != NULL)
      Tk_FreeFont(piBoldFont);
}

// ----------------------------------------------------------------------------
// Remove descriptions that are substrings of other info strings in the given list
//
static uint PiOutput_UnifyMergedInfo( uchar ** infoStrTab, uint infoCount )
{
   register uchar c1, c2;
   register schar ia1, ia2;
   uchar *pidx, *pcmp, *p1, *p2;
   uint idx, cmpidx;
   int len, cmplen;

   for (idx = 0; idx < infoCount; idx++)
   {
      pidx = infoStrTab[idx];
      while ( (*pidx != 0) && (alphaNumTab[*pidx] == ALNUM_NONE) )
         pidx += 1;
      len = strlen(pidx);

      for (cmpidx = 0; cmpidx < infoCount; cmpidx++)
      {
         if ((idx != cmpidx) && (infoStrTab[cmpidx] != NULL))
         {
            pcmp = infoStrTab[cmpidx];
            while ( (alphaNumTab[*pcmp] != 0) && (alphaNumTab[*pcmp] == ALNUM_NONE) )
               pcmp += 1;
            cmplen = strlen(pcmp);
            if (cmplen >= len)
            {
               cmplen -= len;

               while (cmplen-- >= 0)
               {
                  if (*(pcmp++) == *pidx)
                  {
                     p1 = pidx + 1;
                     p2 = pcmp;

                     while ( ((c1 = *p1) != 0) && ((c2 = *p2) != 0) )
                     {
                        ia1 = alphaNumTab[c1];
                        ia2 = alphaNumTab[c2];
                        if (ia1 == ALNUM_NONE)
                           p1++;
                        else if (ia2 == ALNUM_NONE)
                           p2++;
                        else
                        {
                           if ( (ia1 ^ ia2) < 0 )
                           {  // different case -> make both upper case
                              c1 &= ~0x20;
                              c2 &= ~0x20;
                           }
                           if (c1 != c2)
                              break;
                           else
                           {
                              p1++;
                              p2++;
                           }
                        }
                     }

                     if (*p1 == 0)
                     {  // found identical substring
                        dprintf1("PiOutput-UnifyMergedInfo: remove %s\n", infoStrTab[idx]);
                        xfree(infoStrTab[idx]);
                        infoStrTab[idx] = NULL;
                        break;
                     }
                  }
               }
               if (infoStrTab[idx] == NULL)
                  break;
            }
         }
      }
   }

   return infoCount;
}

// ----------------------------------------------------------------------------
// Build array of description strings for merged PI
// - Merged database needs special handling, because short and long infos
//   of all providers are concatenated into the short info string. Separator
//   between short and long info is a newline char. After each provider's
//   info there's a form-feed char.
// - Returns the number of separate strings and puts their pointers into the array.
//   The caller must free the separated strings.
//
static uint PiOutput_SeparateMergedInfo( const PI_BLOCK * pPiBlock, uchar ** infoStrTab )
{
   const char *p, *ps, *pl, *pt;
   uchar c;
   int   shortInfoLen, longInfoLen, len;
   uint  count;

   p = PI_GET_SHORT_INFO(pPiBlock);
   count = 0;
   do
   {  // loop across all provider's descriptions

      // obtain start and length of this provider's short and long info
      shortInfoLen = longInfoLen = 0;
      ps = p;
      pl = NULL;
      while (*p)
      {
         if (*p == '\n')
         {  // end of short info found
            shortInfoLen = p - ps;
            pl = p + 1;
         }
         else if (*p == 12)
         {  // end of short and/or long info found
            if (pl == NULL)
               shortInfoLen = p - ps;
            else
               longInfoLen = p - pl;
            p++;
            break;
         }
         p++;
      }

      if (pl != NULL)
      {
         // if there's a long info too, do the usual redundancy check

         if (shortInfoLen > longInfoLen)
         {
            len = longInfoLen;
            pt = ps + (shortInfoLen - longInfoLen);
         }
         else
         {
            len = shortInfoLen;
            pt = ps;
         }
         c = *pl;

         // min length is 30, because else single words might match
         while (len-- > 30)
         {
            if (*(pt++) == c)
            {
               if (!strncmp(pt, pl+1, len))
               {  // start of long info is identical to end of short info -> skip identical part in long info
                  pl          += len + 1;
                  longInfoLen -= len + 1;
                  break;
               }
            }
         }

         infoStrTab[count] = xmalloc(shortInfoLen + longInfoLen + 1 + 1);
         strncpy(infoStrTab[count], ps, shortInfoLen);

         // if short and long info were not merged, add a newline inbetween
         if (len <= 30)
         {
            infoStrTab[count][shortInfoLen] = '\n';
            shortInfoLen += 1;
         }
         // append the long info to the text widget insert command
         strncpy(infoStrTab[count] + shortInfoLen, pl, longInfoLen);

         infoStrTab[count][shortInfoLen + longInfoLen] = 0;
      }
      else
      {  // only short info available; copy it into the array
         infoStrTab[count] = xmalloc(shortInfoLen + 1);
         strncpy(infoStrTab[count], ps, shortInfoLen);
         infoStrTab[count][shortInfoLen] = 0;
      }
      count += 1;

   } while (*p);

   return count;
}

// ----------------------------------------------------------------------------
// Print Short-Info and Long-Info
//
void PiOutput_AppendShortAndLongInfoText( const PI_BLOCK *pPiBlock, PiOutput_AppendInfoTextCb_Type AppendInfoTextCb, void *fp, bool isMerged )
{
   if ( PI_HAS_SHORT_INFO(pPiBlock) && PI_HAS_LONG_INFO(pPiBlock) )
   {
      // remove identical substring from beginning of long info
      // (this feature has been added esp. for the German provider RTL-II)
      const uchar *ps = PI_GET_SHORT_INFO(pPiBlock);
      const uchar *pl = PI_GET_LONG_INFO(pPiBlock);
      uint len = strlen(ps);
      uchar c = *pl;

      // min length is 30, because else single words might match
      while (len-- > 30)
      {
         if (*(ps++) == c)
         {
            if (!strncmp(ps, pl+1, len))
            {
               pl += len + 1;
               break;
            }
         }
      }

      AppendInfoTextCb(fp, PI_GET_SHORT_INFO(pPiBlock), (len <= 30), pl);
   }
   else if (PI_HAS_SHORT_INFO(pPiBlock))
   {
      if (isMerged)
      {
         uchar *infoStrTab[MAX_MERGED_DB_COUNT];
         uint infoCount, idx, added;

         // Merged database -> for presentation the usual short/long info
         // combination is done, plus a separator image is added between
         // different provider's descriptions.

         infoCount = PiOutput_SeparateMergedInfo(pPiBlock, infoStrTab);
         infoCount = PiOutput_UnifyMergedInfo(infoStrTab, infoCount);
         added = 0;

         for (idx=0; idx < infoCount; idx++)
         {
            if (infoStrTab[idx] != NULL)
            {
               if (added > 0)
               {  // not the only or first info -> insert separator image (a horizontal line)
                  AppendInfoTextCb(fp, NULL, FALSE, NULL);
               }

               // add the short info to the text widget insert command
               AppendInfoTextCb(fp, infoStrTab[idx], FALSE, NULL);

               xfree(infoStrTab[idx]);
               added += 1;
            }
         }
      }
      else
      {
         AppendInfoTextCb(fp, PI_GET_SHORT_INFO(pPiBlock), FALSE, NULL);
      }
   }
   else if (PI_HAS_LONG_INFO(pPiBlock))
   {
      AppendInfoTextCb(fp, PI_GET_LONG_INFO(pPiBlock), FALSE, NULL);
   }
}

// ----------------------------------------------------------------------------
// Print PDC themes into string with removed redundancy
//
void PiOutput_AppendCompressedThemes( const PI_BLOCK *pPiBlock, char * outstr, uint maxlen )
{
   const char * pThemeStr;
   uint idx, theme, themeCat, themeStrLen;
   char * po;

   if (maxlen > 0)
      outstr[0] = 0;

   po = outstr;
   for (idx=0; idx < pPiBlock->no_themes; idx++)
   {
      theme = pPiBlock->themes[idx];
      if (theme > 0x80)
         theme = 0x80;

      pThemeStr = PdcThemeGet(theme);
      themeCat  = PdcThemeGetCategory(theme);

      if ( (pThemeStr != NULL) &&
           // current theme is general and next same category -> skip
           ( (themeCat != theme) ||
             (idx + 1 >= pPiBlock->no_themes) ||
             (themeCat != PdcThemeGetCategory(pPiBlock->themes[idx + 1])) ) &&
           // current theme is identical to next -> skip
           ( (idx + 1 >= pPiBlock->no_themes) ||
             (theme != pPiBlock->themes[idx + 1]) ))
      {
         themeStrLen = strlen(pThemeStr);
         if (maxlen > themeStrLen + 2)
         {
            strcpy(po, pThemeStr);
            strcpy(po + themeStrLen, ", ");
            po     += themeStrLen + 2;
            maxlen -= themeStrLen + 2;
         }
      }
   }
   // remove last comma if nothing follows
   if (outstr[0] != 0)
   {
      assert(po - outstr >= 2);  // all concats are followed by at least two chars: ", "
      assert( (*(po - 2) == ',') && (*(po - 1) == ' ') );

      *(po - 2) = 0;
   }
}

// ----------------------------------------------------------------------------
// Print feature list into string
//
void PiOutput_AppendFeatureList( const PI_BLOCK *pPiBlock, char * outstr )
{
   int len;

   outstr[0] = 0;
   switch(pPiBlock->feature_flags & 0x03)
   {
      case  1: strcat(outstr, "2-channel, "); break;
      case  2: strcat(outstr, "stereo, "); break;
      case  3: strcat(outstr, "surround, "); break;
   }

   if (pPiBlock->feature_flags & 0x04)
      strcat(outstr, "wide, ");
   if (pPiBlock->feature_flags & 0x08)
      strcat(outstr, "PAL+, ");
   if (pPiBlock->feature_flags & 0x10)
      strcat(outstr, "digital, ");
   if (pPiBlock->feature_flags & 0x20)
      strcat(outstr, "encrypted, ");
   if (pPiBlock->feature_flags & 0x40)
      strcat(outstr, "live, ");
   if (pPiBlock->feature_flags & 0x80)
      strcat(outstr, "repeat, ");
   if (pPiBlock->feature_flags & 0x100)
      strcat(outstr, "subtitles, ");

   if (pPiBlock->editorial_rating > 0)
      sprintf(outstr + strlen(outstr), "rating: %d of 1..7, ", pPiBlock->editorial_rating);

   if (pPiBlock->parental_rating == 1)
      strcat(outstr, "age: general, ");
   else if (pPiBlock->parental_rating > 0)
      sprintf(outstr + strlen(outstr), "age: %d and up, ", pPiBlock->parental_rating * 2);

   // remove last comma if nothing follows
   len = strlen(outstr);
   if ((len >= 2) && (outstr[len - 2] == ',') && (outstr[len - 1] == ' '))
      outstr[len - 2] = 0;
}

// ----------------------------------------------------------------------------
// Append a text string to the HTML output file, while quoting HTML special chars
// - HTML special chars are: < > &  converted to: &lt; &gt; &amp;
//
static void PiOutput_HtmlWriteString( FILE *fp, const char * pText )
{
   char outbuf[256], *po;
   uint outlen;

   po = outbuf;
   outlen = sizeof(outbuf);
   while (*pText)
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
// Print Short- and Long-Info texts or separators
//
static void PiOutput_HtmlAppendInfoTextCb( void *vp, const char * pShortInfo, bool insertSeparator, const char * pLongInfo )
{
   FILE * fp = (FILE *) vp;
   const char * pText;
   char * pNewline;
   uint  idx;

   if ((fp != NULL) && (pShortInfo != NULL))
   {
      fprintf(fp, "<tr>\n<td colspan=3 CLASS=\"textrow\">\n<p>\n");

      // use a pseudo-loop to process both the short and long info texts in the same way
      pText = pShortInfo;
      for (idx=0; idx < 2; idx++)
      {
         // replace newline characters with paragraph tags
         while ( (pNewline = strchr(pText, '\n')) != NULL )
         {
            // print text up to (and excluding) the newline
            *pNewline = 0;
            PiOutput_HtmlWriteString(fp, pText);
            fprintf(fp, "\n</p><p>\n");
            // skip to text following the newline
            pText = pNewline + 1;
         }
         PiOutput_HtmlWriteString(fp, pText);

         if (insertSeparator)
            fprintf(fp, "\n</p><p>\n");

         if (pLongInfo != NULL)
            pText = pLongInfo;
         else
            break;
      }
      fprintf(fp, "\n</p>\n</td>\n</tr>\n\n");
   }
}

// ----------------------------------------------------------------------------
// Copy string with double quotes (") replaced with single quotes (')
// - for use inside HTML tags, e.g. <META source="...">
//
static void PiOutput_HtmlRemoveQuotes( const uchar * pStr, uchar * pBuf, uint maxOutLen )
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
// Append HTML for one PI
//
static void PiOutput_HtmlDesc( FILE *fp, const PI_BLOCK * pPiBlock, const AI_BLOCK * pAiBlock )
{
   const uchar *pCfNetname;
   uchar date_str[20], start_str[20], stop_str[20], cni_str[7], label_str[50];

   strftime(date_str, sizeof(date_str), " %a %d.%m", localtime(&pPiBlock->start_time));
   strftime(start_str, sizeof(start_str), "%H:%M", localtime(&pPiBlock->start_time));
   strftime(stop_str, sizeof(stop_str), "%H:%M", localtime(&pPiBlock->stop_time));
   strftime(label_str, sizeof(label_str), "%Y%m%d%H%M", localtime(&pPiBlock->start_time));

   sprintf(cni_str, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
   pCfNetname = Tcl_GetVar2(interp, "cfnetnames", cni_str, TCL_GLOBAL_ONLY);
   if (pCfNetname == NULL)
      pCfNetname = AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no);

   // start HTML table for PI and append first row: running time, title, network name
   fprintf(fp, "<A NAME=\"TITLE_%02d_%s\">\n"
               "<table CLASS=PI COLS=3 WIDTH=\"100%%\">\n"
               "<tr>\n"
               "<td CLASS=\"titlerow\" WIDTH=\"20%%\">\n"
               "%s - %s\n"  // running time
               "</td>\n"
               "<td rowspan=2 CLASS=\"titlerow\">\n",
               pPiBlock->netwop_no, label_str,
               start_str, stop_str);
   PiOutput_HtmlWriteString(fp, PI_GET_TITLE(pPiBlock));
   fprintf(fp, "\n"
               "</td>\n"
               "<td rowspan=2 CLASS=\"titlerow\" WIDTH=\"20%%\">\n");
   PiOutput_HtmlWriteString(fp, pCfNetname);
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
   fprintf(fp, "<tr><td colspan=3 CLASS=\"featurerow\">\n");

   // append theme list
   PiOutput_AppendCompressedThemes(pPiBlock, comm, TCL_COMM_BUF_SIZE);
   PiOutput_HtmlWriteString(fp, comm);

   // append features list
   strcpy(comm, " (");
   PiOutput_AppendFeatureList(pPiBlock, comm + 2);
   if (comm[2] != 0)
   {
      strcat(comm, ")");
      PiOutput_HtmlWriteString(fp, comm);
   }
   fprintf(fp, "\n</td>\n</tr>\n\n");

   // start third row: description
   PiOutput_AppendShortAndLongInfoText(pPiBlock, PiOutput_HtmlAppendInfoTextCb, fp, EpgDbContextIsMerged(pUiDbContext));

   fprintf(fp, "</table>\n</A>\n\n\n");
}

// ----------------------------------------------------------------------------
// HTML page templates
//
static const uchar * const html_head =
   "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/HTMLlat1.ent\">\n"
   "<HTML>\n"
   "<!--\n"
   "      Generated by nxtvepg at %s\n"
   "      Please respect the content provider's copyright!\n"
   "      Do not redistribute this document in the Internet!\n"
   "-->\n"
   "<HEAD>\n"
   "<META http-equiv=\"Content-Type\" content=\"text/html; charset=iso8859-1\">\n"
   "<META name=\"copyright\" content=\"%s\">\n"
   "<META name=\"description\" lang=\"en\" content=\"nexTView EPG: TV programme schedules\">\n"
   "<META name=\"generator\" content=\"nxtvepg/" EPG_VERSION_STR "\" href=\"" NXTVEPG_URL "\">\n"
   "<TITLE>Nextview EPG</TITLE>\n"
   "<STYLE type=\"text/css\">\n"
   "   <!--\n"
   "   TABLE.PI { PADDING: 3; BORDER-STYLE: NONE; MARGIN-BOTTOM: 30pt; }\n"
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
   "<BODY BGCOLOR=#f8f8f8>\n\n";

static const uchar * const html_marker_cont_title =
   "<!-- APPEND LIST HERE / DO NOT DELETE THIS LINE -->\n";
static const uchar * const html_marker_cont_desc =
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
static void HtmlFileCreate( const char * pFileName, bool optAppend, FILE ** fppSrc, FILE ** fppDst, const AI_BLOCK * pAiBlock )
{
   char * pBakName;
   FILE *fpSrc, *fpDst;
   time_t now;
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
      if (fpDst != NULL)
      {
         if (fpSrc == NULL)
         {  // new file -> create HTML head
            now = time(NULL);

            // copy the service name while replacing double quotes
            PiOutput_HtmlRemoveQuotes(AI_GET_SERVICENAME(pAiBlock), comm, TCL_COMM_BUF_SIZE);

            fprintf(fpDst, html_head, ctime(&now), comm);
         }
      }
      else
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
static void HtmlFileSkip( FILE * fpSrc, FILE * fpDst, HTML_DUMP_TYPE prevType, HTML_DUMP_TYPE nextType )
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
static void HtmlFileClose( FILE * fpSrc, FILE * fpDst, const AI_BLOCK * pAiBlock )
{
   if (fpDst != NULL)
   {
      if (fpSrc == NULL)
      {  // newly created file -> finish HTML page
         fprintf(fpDst, "<P CLASS=\"copyright\">\n&copy; Nextview EPG by ");
         PiOutput_HtmlWriteString(fpDst, AI_GET_SERVICENAME(pAiBlock));
         fprintf(fpDst, "\n</P>\n</BODY>\n</HTML>\n");

      }
      else
      {  // nothing to do here - footer already copied from original file
         fclose(fpSrc);
      }
      fclose(fpDst);
   }
}

// ----------------------------------------------------------------------------
// Dump HTML for one programme column
// - also supports user-defined columns
//
static void PiOutput_HtmlTitle( FILE * fpDst, const PI_BLOCK * pPiBlock,
                                const PIBOX_COL_CFG * pColTab, uint colCount,
                                uint hyperlinkColIdx )
{
   PIBOX_COL_TYPES type;
   Tcl_Obj  * pFmtObj;
   Tcl_Obj ** pFmtObjv;
   Tcl_Obj  * pImageObj;
   Tcl_Obj ** pImgObjv;
   Tcl_Obj  * pImgSpec;
   bool  hasBold, hasEm, hasColor;
   int   fmtObjc;
   int   imgObjc;
   uint  colIdx;
   uint  len;
   uint  fmtIdx;

   if ((fpDst != NULL) && (pPiBlock != NULL) && (pColTab != NULL))
   {
      fprintf(fpDst, "<tr>\n");

      // add table columns in the same configuration as for the internal listbox
      for (colIdx=0; colIdx < colCount; colIdx++)
      {
         fprintf(fpDst, "<td>\n");

         if (colIdx == hyperlinkColIdx)
         {  // if requested add hyperlink to the description on this column
            uchar label_str[50];
            strftime(label_str, sizeof(label_str), "%Y%m%d%H%M", localtime(&pPiBlock->start_time));
            fprintf(fpDst, "<A HREF=\"#TITLE_%02d_%s\">\n", pPiBlock->netwop_no, label_str);
         }

         len  = 0;
         type = pColTab[colIdx].type;
         pImageObj = pFmtObj = NULL;

         if (type == PIBOX_COL_USER_DEF)
         {
            len = PiOutput_MatchUserCol(pPiBlock, &type, pColTab[colIdx].pDefObj,
                                        comm, TCL_COMM_BUF_SIZE, &pImageObj, &pFmtObj);
         }
         if (type != PIBOX_COL_USER_DEF)
            len = PiOutput_PrintColumnItem(pPiBlock, type, comm, TCL_COMM_BUF_SIZE);

         if (pImageObj != NULL)
         {  // user-defined column consists of an image
            pImgSpec = Tcl_GetVar2Ex(interp, "pi_img", Tcl_GetString(pImageObj), TCL_GLOBAL_ONLY);
            if (pImgSpec != NULL)
            {
               if ( (Tcl_ListObjGetElements(interp, pImgSpec, &imgObjc, &pImgObjv) == TCL_OK) &&
                    (imgObjc == 2) )
               {
                  // note: there's intentionally no WIDTH and HEIGHT tags so that the user can use different images
                  fprintf(fpDst, "<IMG SRC=\"images/%s.png\" JUSTIFY=\"center\">\n", Tcl_GetString(pImageObj));
               }
            }
         }

         hasBold = hasEm = hasColor = FALSE;
         if (pFmtObj != NULL)
         {
            Tcl_ListObjGetElements(interp, pFmtObj, &fmtObjc, &pFmtObjv);
            for (fmtIdx=0; fmtIdx < fmtObjc; fmtIdx++)
            {
               if (strcmp(Tcl_GetString(pFmtObjv[fmtIdx]), "bold") == 0)
               {
                  fprintf(fpDst, "<B>");
                  hasBold = TRUE;
               }
               else if (strcmp(Tcl_GetString(pFmtObjv[fmtIdx]), "underline") == 0)
               {
                  fprintf(fpDst, "<EM>");
                  hasEm = TRUE;
               }
               else
               {
                  fprintf(fpDst, "<FONT COLOR=\"%s\">", Tcl_GetString(pFmtObjv[fmtIdx]));
                  hasColor = TRUE;
               }
            }
         }

         if (len > 0)
            PiOutput_HtmlWriteString(fpDst, comm);

         if (hasColor)
            fprintf(fpDst, "</FONT>");
         if (hasEm)
            fprintf(fpDst, "</EM>");
         if (hasBold)
            fprintf(fpDst, "</B>");

         fprintf(fpDst, "%s\n</td>\n", ((colIdx == hyperlinkColIdx) ? "</a>\n" : ""));
      }

      fprintf(fpDst, "</tr>\n\n");
   }
   else
      fatal3("PiOutput-HtmlTitle: illegal NULL ptr param: fp=%d pPi=%d, pCol=%d", (fpDst != NULL), (pPiBlock != NULL), (pColTab != NULL));
}

// ----------------------------------------------------------------------------
// Dump programme titles and/or descriptions into file in HTML format
//
static int PiOutput_DumpHtml( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_DumpHtml <file-name> <doTitles=0/1> <doDesc=0/1> <append=0/1> <sel-only=0/1> <max-count> <hyperlink=col-idx/-1> <colsel>";
   const AI_BLOCK *pAiBlock;
   const PI_BLOCK *pPiBlock;
   const char * pFileName;
   Tcl_DString  ds;
   const PIBOX_COL_CFG * pColTab;
   Tcl_Obj ** pColObjv;
   FILE *fpSrc, *fpDst;
   uint piIdx;
   int  doTitles, doDesc, optAppend, optSelOnly, optMaxCount, optHyperlinks;
   int  colCount;
   int  result;

   if (objc != 1+8)
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
             (Tcl_GetIntFromObj    (interp, objv[7], &optHyperlinks) != TCL_OK) ||
             (Tcl_ListObjGetElements(interp, objv[8], &colCount, &pColObjv) != TCL_OK) )
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
         HtmlFileCreate(pFileName, optAppend, &fpSrc, &fpDst, pAiBlock);
         if (fpDst != NULL)
         {
            // add or skip & copy HTML page header
            HtmlFileSkip(fpSrc, fpDst, HTML_DUMP_NONE, HTML_DUMP_TITLE);
            if (doTitles)
            {  // add selected title or table with list of titles

               // create column config cache from specification in Tcl list
               pColTab = PiOutput_CfgColumnsCache(colCount, pColObjv);

               if (optSelOnly == FALSE)
                  pPiBlock = EpgDbSearchFirstPi(pUiDbContext, pPiFilterContext);
               else
                  pPiBlock = PiListBox_GetSelectedPi();

               for (piIdx=0; ((piIdx < optMaxCount) || (optMaxCount < 0)) && (pPiBlock != NULL); piIdx++)
               {
                  PiOutput_HtmlTitle(fpDst, pPiBlock, pColTab, colCount, optHyperlinks);

                  pPiBlock = EpgDbSearchNextPi(pUiDbContext, pPiFilterContext, pPiBlock);
               }
               // free column config cache
               PiOutput_CfgColumnsClear(pColTab, colCount);
            }

            // skip & copy existing descriptions if in append mode
            HtmlFileSkip(fpSrc, fpDst, HTML_DUMP_TITLE, HTML_DUMP_DESC);
            if (doDesc)
            {
               if (optSelOnly == FALSE)
                  pPiBlock = EpgDbSearchFirstPi(pUiDbContext, pPiFilterContext);
               else
                  pPiBlock = PiListBox_GetSelectedPi();

               // add descriptions for all programmes matching the current filter
               for (piIdx=0; ((piIdx < optMaxCount) || (optMaxCount < 0)) && (pPiBlock != NULL); piIdx++)
               {
                  PiOutput_HtmlDesc(fpDst, pPiBlock, pAiBlock);

                  pPiBlock = EpgDbSearchNextPi(pUiDbContext, pPiFilterContext, pPiBlock);
               }
            }
            HtmlFileSkip(fpSrc, fpDst, HTML_DUMP_DESC, HTML_DUMP_END);
            HtmlFileClose(fpSrc, fpDst, pAiBlock);
         }
         Tcl_DStringFree(&ds);
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Print Short- and Long-Info texts for XMLTV
//
static void PiOutput_XmlAppendInfoTextCb( void *vp, const char * pShortInfo, bool insertSeparator, const char * pLongInfo )
{
   FILE * fp = (FILE *) vp;
   const char * pText;
   char * pNewline;
   uint  idx;

   if ((fp != NULL) && (pShortInfo != NULL))
   {
      // use a pseudo-loop to process both the short and long info texts in the same way
      pText = pShortInfo;
      for (idx=0; idx < 2; idx++)
      {
         // replace newline characters with paragraph tags
         while ( (pNewline = strchr(pText, '\n')) != NULL )
         {
            // print text up to (and excluding) the newline
            *pNewline = 0;
            PiOutput_HtmlWriteString(fp, pText);
            fprintf(fp, "\n\n");
            // skip to text following the newline
            pText = pNewline + 1;
         }
         PiOutput_HtmlWriteString(fp, pText);

         if (insertSeparator)
            fprintf(fp, "\n\n");

         if (pLongInfo != NULL)
            pText = pLongInfo;
         else
            break;
      }
      fprintf(fp, "\n");
   }
}

// ----------------------------------------------------------------------------
// Dump programme titles and/or descriptions into file in XMLTV format
//
void PiOutput_DumpDatabaseXml( EPGDB_CONTEXT * pDbContext, FILE * fp )
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
   uint  netwop;
   uint  themeIdx, langIdx;
   uchar start_str[50];
   uchar stop_str[50];
   uchar vps_str[50];
   uchar cni_str[10];

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
         PiOutput_HtmlRemoveQuotes(AI_GET_NETWOP_NAME(pAiBlock, pAiBlock->thisNetwop), comm, TCL_COMM_BUF_SIZE - 2);
         strcat(comm, "/");
         PiOutput_HtmlRemoveQuotes(AI_GET_SERVICENAME(pAiBlock), comm + strlen(comm), TCL_COMM_BUF_SIZE - strlen(comm));
         PiOutput_HtmlWriteString(fp, comm);

         lastAiUpdate = EpgDbGetAiUpdateTime(pDbContext);
         strftime(start_str, sizeof(start_str), "%Y%m%d%H%M%S +0000", gmtime(&lastAiUpdate));
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
         pPiBlock = EpgDbSearchFirstPi(pDbContext, pPiFilterContext);
         while (pPiBlock != NULL)
         {
            // start & stop times, channel ID
            strftime(start_str, sizeof(start_str), "%Y%m%d%H%M%S +0000", gmtime(&pPiBlock->start_time));
            strftime(stop_str, sizeof(stop_str), "%Y%m%d%H%M%S +0000", gmtime(&pPiBlock->stop_time));
            if (PiOutput_GetVpsTimestamp(&vpsTime, pPiBlock->pil, pPiBlock->start_time))
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
            PiOutput_HtmlWriteString(fp, comm);
            fprintf(fp, "</title>\n");
            if ( PI_HAS_SHORT_INFO(pPiBlock) || PI_HAS_LONG_INFO(pPiBlock) )
            {
               fprintf(fp, "\t<desc>\n");
               PiOutput_AppendShortAndLongInfoText(pPiBlock, PiOutput_XmlAppendInfoTextCb, fp, EpgDbContextIsMerged(pDbContext));
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
                     PiOutput_HtmlWriteString(fp, pThemeStr);
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

            pPiBlock = EpgDbSearchNextPi(pDbContext, pPiFilterContext, pPiBlock);
         }
         fprintf(fp, "</tv>\n");
      }
      EpgDbLockDatabase(pDbContext, FALSE);
   }
   else
      fatal0("PiOutput-DumpDatabaseXml: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Dump programme titles and/or descriptions into file in XMLTV format
//
static int PiOutput_DumpXml( ClientData ttp, Tcl_Interp *interp,int objc, Tcl_Obj *CONST objv[] )
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
         PiOutput_DumpDatabaseXml(pUiDbContext, fpDst);
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
// show info about the currently selected item in pop-up window
//
static int PiOutput_PopupPi( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PopupPi <xcoo> <ycoo>";
   const AI_BLOCK * pAiBlock;
   const AI_NETWOP *pNetwop;
   const PI_BLOCK * pPiBlock;
   const DESCRIPTOR *pDesc;
   const char * pCfNetname;
   const uchar * pThemeStr;
   const uchar * pGeneralStr;
   uchar *p;
   uchar start_str[50], stop_str[50], cni_str[7];
   uchar ident[50];
   int xcoo, ycoo;
   int index;
   int result;

   if (objc != 3)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if ( (Tcl_GetIntFromObj(interp, objv[1], &xcoo) != TCL_OK) ||
             (Tcl_GetIntFromObj(interp, objv[2], &ycoo) != TCL_OK) )
   {  // parameter type invalid
      result = TCL_ERROR; 
   }
   else
   {
      result = TCL_OK; 

      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      pPiBlock = PiListBox_GetSelectedPi();
      if ((pAiBlock != NULL) && (pPiBlock != NULL))
      {
         pNetwop = AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no);

         sprintf(cni_str, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
         pCfNetname = Tcl_GetVar2(interp, "cfnetnames", cni_str, TCL_GLOBAL_ONLY);
         if (pCfNetname == NULL)
            pCfNetname = AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no);

         sprintf(ident, ".poppi_%d_%ld", pPiBlock->netwop_no, pPiBlock->start_time);

         sprintf(comm, "Create_PopupPi %s %d %d\n", ident, xcoo, ycoo);
         eval_check(interp, comm);

         sprintf(comm, "%s.text insert end {%s\n} title\n", ident, PI_GET_TITLE(pPiBlock));
         eval_check(interp, comm);

         sprintf(comm, "%s.text insert end {Network: \t%s\n} body\n", ident, pCfNetname);
         eval_check(interp, comm);

         sprintf(comm, "%s.text insert end {BlockNo:\t0x%04X in %04X-%04X-%04X\n} body\n", ident, pPiBlock->block_no, pNetwop->startNo, pNetwop->stopNo, pNetwop->stopNoSwo);
         eval_check(interp, comm);

         strftime(start_str, sizeof(start_str), "%a %d.%m %H:%M", localtime(&pPiBlock->start_time));
         strftime(stop_str, sizeof(stop_str), "%a %d.%m %H:%M", localtime(&pPiBlock->stop_time));
         sprintf(comm, "%s.text insert end {Start:\t%s\nStop:\t%s\n} body\n", ident, start_str, stop_str);
         eval_check(interp, comm);

         if (pPiBlock->pil != 0x7FFF)
         {
            sprintf(comm, "%s.text insert end {PIL:\t%02d.%02d. %02d:%02d\n} body\n", ident,
                    (pPiBlock->pil >> 15) & 0x1F,
                    (pPiBlock->pil >> 11) & 0x0F,
                    (pPiBlock->pil >>  6) & 0x1F,
                    (pPiBlock->pil      ) & 0x3F );
         }
         else
            sprintf(comm, "%s.text insert end {PIL:\tnone\n} body\n", ident);
         eval_check(interp, comm);

         switch(pPiBlock->feature_flags & 0x03)
         {
           case  0: p = "mono"; break;
           case  1: p = "2chan"; break;
           case  2: p = "stereo"; break;
           case  3: p = "surround"; break;
           default: p = ""; break;
         }
         sprintf(comm, "%s.text insert end {Sound:\t%s\n} body\n", ident, p);
         eval_check(interp, comm);
         if (pPiBlock->feature_flags & ~0x03)
         sprintf(comm, "%s.text insert end {Features:\t%s%s%s%s%s%s%s\n} body\n", ident,
                        ((pPiBlock->feature_flags & 0x04) ? " wide" : ""),
                        ((pPiBlock->feature_flags & 0x08) ? " PAL+" : ""),
                        ((pPiBlock->feature_flags & 0x10) ? " digital" : ""),
                        ((pPiBlock->feature_flags & 0x20) ? " encrypted" : ""),
                        ((pPiBlock->feature_flags & 0x40) ? " live" : ""),
                        ((pPiBlock->feature_flags & 0x80) ? " repeat" : ""),
                        ((pPiBlock->feature_flags & 0x100) ? " subtitles" : "")
                        );
         else
            sprintf(comm, "%s.text insert end {Features:\tnone\n} body\n", ident);
         eval_check(interp, comm);

         if (pPiBlock->parental_rating == 0)
            sprintf(comm, "%s.text insert end {Parental rating:\tnone\n} body\n", ident);
         else if (pPiBlock->parental_rating == 1)
            sprintf(comm, "%s.text insert end {Parental rating:\tgeneral\n} body\n", ident);
         else
            sprintf(comm, "%s.text insert end {Parental rating:\t%d years and up\n} body\n", ident, pPiBlock->parental_rating * 2);
         eval_check(interp, comm);

         if (pPiBlock->editorial_rating == 0)
            sprintf(comm, "%s.text insert end {Editorial rating:\tnone\n} body\n", ident);
         else
            sprintf(comm, "%s.text insert end {Editorial rating:\t%d of 1..7\n} body\n", ident, pPiBlock->editorial_rating);
         eval_check(interp, comm);

         for (index=0; index < pPiBlock->no_themes; index++)
         {
            pThemeStr = PdcThemeGetWithGeneral(pPiBlock->themes[index], &pGeneralStr, TRUE);
            sprintf(comm, "%s.text insert end {Theme:\t0x%02X %s%s\n} body\n",
                          ident, pPiBlock->themes[index], pThemeStr, pGeneralStr);
            eval_check(interp, comm);
         }

         for (index=0; index < pPiBlock->no_sortcrit; index++)
         {
            sprintf(comm, "%s.text insert end {Sorting Criterion:\t0x%02X\n} body\n", ident, pPiBlock->sortcrits[index]);
            eval_check(interp, comm);
         }

         pDesc = PI_GET_DESCRIPTORS(pPiBlock);
         for (index=0; index < pPiBlock->no_descriptors; index++)
         {
            switch (pDesc[index].type)
            {
               case LI_DESCR_TYPE:    sprintf(comm, "%s.text insert end {Descriptor:\tlanguage ID %d\n} body\n", ident, pDesc[index].id); break;
               case TI_DESCR_TYPE:    sprintf(comm, "%s.text insert end {Descriptor:\tsubtitle ID %d\n} body\n", ident, pDesc[index].id); break;
               case MERGE_DESCR_TYPE: sprintf(comm, "%s.text insert end {Descriptor:\tmerged from db #%d\n} body\n", ident, pDesc[index].id); break;
               default:               sprintf(comm, "%s.text insert end {Descriptor:\tunknown type=%d, ID=%d\n} body\n", ident, pDesc[index].type, pDesc[index].id); break;
            }
            eval_check(interp, comm);
         }

         sprintf(comm, "%s.text insert end {Acq. stream:\t%d\n} body\n", ident, EpgDbGetStream(pPiBlock));
         eval_check(interp, comm);

         sprintf(comm, "global poppedup_pi\n"
                       "$poppedup_pi.text configure -state disabled\n"
                       "$poppedup_pi.text configure -height [expr 1 + [$poppedup_pi.text index end]]\n"
                       "pack $poppedup_pi.text\n");
         eval_check(interp, comm);
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Append one character to the dynamically growing output buffer
//
static void PiOutput_ExtCmdAppendChar( char c, DYN_CHAR_BUF * pCmdBuf )
{
   char * newbuf;

   if (pCmdBuf->off == pCmdBuf->size)
   {
      newbuf = xmalloc(pCmdBuf->size + 2048);
      memcpy(newbuf, pCmdBuf->strbuf, pCmdBuf->size);
      xfree(pCmdBuf->strbuf);

      pCmdBuf->strbuf = newbuf;
      pCmdBuf->size  += 2048;
   }

   pCmdBuf->strbuf[pCmdBuf->off++] = c;
}

// ----------------------------------------------------------------------------
// Safely append a quoted string to a Bourne shell command list
// - the string will be enclosed by single quotes (not null terminated)
// - single quotes in the string are escaped
// - control characters (e.g. new line) are replaced by blank
//
static void PiOutput_ExtCmdAppend( DYN_CHAR_BUF * pCmdBuf, const char * ps )
{
   if (pCmdBuf->quoteShell)
   {
      PiOutput_ExtCmdAppendChar('\'', pCmdBuf);

      while (*ps != 0)
      {
         if (*ps == '\'')
         {  // single quote -> escape it
            ps++;
            PiOutput_ExtCmdAppendChar('\'', pCmdBuf);
            PiOutput_ExtCmdAppendChar('\\', pCmdBuf);
            PiOutput_ExtCmdAppendChar('\'', pCmdBuf);
            PiOutput_ExtCmdAppendChar('\'', pCmdBuf);
         }
         else if ( ((uint) *ps) < ' ' )
         {  // control character -> replace it with blank
            ps++;
            PiOutput_ExtCmdAppendChar(' ', pCmdBuf);
         }
         else
            PiOutput_ExtCmdAppendChar(*(ps++), pCmdBuf);
      }

      PiOutput_ExtCmdAppendChar('\'', pCmdBuf);
   }
   else
   {
      while (*ps != 0)
         PiOutput_ExtCmdAppendChar(*(ps++), pCmdBuf);
   }
}

// ----------------------------------------------------------------------------
// Callback function for PiOutput-AppendShortAndLongInfoText
//
static void PiOutput_ExtCmdAppendInfoTextCb( void *fp, const char * pShortInfo, bool insertSeparator, const char * pLongInfo )
{
   DYN_CHAR_BUF * pCmdBuf = (DYN_CHAR_BUF *) fp;

   if (pShortInfo != NULL)
   {
      if (pLongInfo != NULL)
      {
         PiOutput_ExtCmdAppend(pCmdBuf, pShortInfo);
         if (insertSeparator)
            PiOutput_ExtCmdAppend(pCmdBuf, " // ");
         PiOutput_ExtCmdAppend(pCmdBuf, pLongInfo);
      }
      else
         PiOutput_ExtCmdAppend(pCmdBuf, pShortInfo);
   }
   else
   {  // separator between info texts of different providers
      PiOutput_ExtCmdAppend(pCmdBuf, " // ");
   }
}

// ----------------------------------------------------------------------------
// Process a keyword and modifier in a user-defined command line
// - the result is written to the output string (not null terminated)
// - unknown keyword is replaced with ${keyword: unknown vaiable}
//
static void PiOutput_ExtCmdVariable( const PI_BLOCK *pPiBlock, const AI_BLOCK * pAiBlock,
                                     const char * pKeyword, uint keywordLen,
                                     const char * pModifier, uint modifierLen,
                                     DYN_CHAR_BUF * pCmdBuf )
{
   const char * pConst;
   char  modbuf[100];
   char  strbuf[200];
   uint   idx;
   struct tm vpsTime;
   time_t tdiff;
   time_t now = time(NULL);

   if (modifierLen >= sizeof(modbuf))
      modifierLen = sizeof(modbuf) - 1;

   if (strncmp(pKeyword, "start", keywordLen) == 0)
   {  // start time
      if (pModifier != NULL)
      {
         strncpy(modbuf, pModifier, modifierLen);
         modbuf[modifierLen] = 0;
      }
      else
         strcpy(modbuf, "%H:%M-%d.%m.%Y");
      strftime(strbuf, sizeof(strbuf), modbuf, localtime(&pPiBlock->start_time));
      PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "stop", keywordLen) == 0)
   {  // stop time
      if (pModifier != NULL)
      {
         strncpy(modbuf, pModifier, modifierLen);
         modbuf[modifierLen] = 0;
      }
      else
         strcpy(modbuf, "%H:%M-%d.%m.%Y");
      strftime(strbuf, sizeof(strbuf), modbuf, localtime(&pPiBlock->stop_time));
      PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "duration", keywordLen) == 0)
   {  // duration = stop - start time; by default in minutes
      if (now >= pPiBlock->stop_time)
         tdiff = 0;
      else if (now >= pPiBlock->start_time)
         tdiff = pPiBlock->stop_time - now;
      else
         tdiff = pPiBlock->stop_time - pPiBlock->start_time;
      if ((pModifier == NULL) || (*pModifier == 'm'))
         tdiff /= 60;
      sprintf(strbuf, "%d", (int) tdiff);
      PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "relstart", keywordLen) == 0)
   {  // relative start = start time - now; by default in minutes
      if (now >= pPiBlock->start_time)
         tdiff = 0;
      else
         tdiff = pPiBlock->start_time - now;
      if ((pModifier == NULL) || (*pModifier == 'm'))
         tdiff /= 60;
      sprintf(strbuf, "%d", (int) tdiff);
      PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "CNI", keywordLen) == 0)
   {  // network CNI (hexadecimal)
      sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
      PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "network", keywordLen) == 0)
   {  // network name
      sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
      pConst = Tcl_GetVar2(interp, "cfnetnames", strbuf, TCL_GLOBAL_ONLY);
      if (pConst != NULL)
         PiOutput_ExtCmdAppend(pCmdBuf, pConst);
      else
         PiOutput_ExtCmdAppend(pCmdBuf, AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no));
   }
   else if (strncmp(pKeyword, "title", keywordLen) == 0)
   {  // programme title string
      PiOutput_ExtCmdAppend(pCmdBuf, PI_GET_TITLE(pPiBlock));
   }
   else if (strncmp(pKeyword, "description", keywordLen) == 0)
   {  // programme description (short & long info)
      PiOutput_AppendShortAndLongInfoText(pPiBlock, PiOutput_ExtCmdAppendInfoTextCb, pCmdBuf, EpgDbContextIsMerged(pUiDbContext));
   }
   else if (strncmp(pKeyword, "themes", keywordLen) == 0)
   {  // PDC themes
      if ((pModifier == NULL) || (*pModifier == 't'))
      {  // output in cleartext
         PiOutput_AppendCompressedThemes(pPiBlock, strbuf, sizeof(strbuf));
         PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
      }
      else
      {  // numerical output
         for (idx=0; idx < pPiBlock->no_themes; idx++)
         {
            sprintf(strbuf, "%s%d", ((idx > 0) ? "," : ""), pPiBlock->themes[idx]);
            PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
         }
      }
   }
   else if ( (strncmp(pKeyword, "VPS", keywordLen) == 0) ||
             (strncmp(pKeyword, "PDC", keywordLen) == 0) )
   {  // VPS/PDC time code
      if (PiOutput_GetVpsTimestamp(&vpsTime, pPiBlock->pil, pPiBlock->start_time))
      {
         if (pModifier != NULL)
         {
            strncpy(modbuf, pModifier, modifierLen);
            modbuf[modifierLen] = 0;
         }
         else
            strcpy(modbuf, "%H:%M-%d.%m.%Y");
         strftime(strbuf, sizeof(strbuf), modbuf, &vpsTime);
         PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
      }
      else
      {  // invalid VPS/PDC label
         PiOutput_ExtCmdAppend(pCmdBuf, "none");
      }
   }
   else if (strncmp(pKeyword, "sound", keywordLen) == 0)
   {
      switch(pPiBlock->feature_flags & 0x03)
      {
         default:
         case 0: pConst = "mono/unknown"; break;
         case 1: pConst = "2-channel"; break;
         case 2: pConst = "stereo"; break;
         case 3: pConst = "surround"; break;
      }
      PiOutput_ExtCmdAppend(pCmdBuf, pConst);
   }
   else if (strncmp(pKeyword, "format", keywordLen) == 0)
   {
      if (pPiBlock->feature_flags & 0x08)
         PiOutput_ExtCmdAppend(pCmdBuf, "PALplus");
      else if (pPiBlock->feature_flags & 0x04)
         PiOutput_ExtCmdAppend(pCmdBuf, "wide");
      else
         PiOutput_ExtCmdAppend(pCmdBuf, "normal/unknown");
   }
   else if (strncmp(pKeyword, "digital", keywordLen) == 0)
   {
      PiOutput_ExtCmdAppend(pCmdBuf, (pPiBlock->feature_flags & 0x10) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "encrypted", keywordLen) == 0)
   {
      PiOutput_ExtCmdAppend(pCmdBuf, (pPiBlock->feature_flags & 0x20) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "live", keywordLen) == 0)
   {
      PiOutput_ExtCmdAppend(pCmdBuf, (pPiBlock->feature_flags & 0x40) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "repeat", keywordLen) == 0)
   {
      PiOutput_ExtCmdAppend(pCmdBuf, (pPiBlock->feature_flags & 0x80) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "subtitle", keywordLen) == 0)
   {
      PiOutput_ExtCmdAppend(pCmdBuf, (pPiBlock->feature_flags & 0x100) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "e_rating", keywordLen) == 0)
   {  // editorial rating (0 means "none")
      sprintf(strbuf, "%d", pPiBlock->editorial_rating);
      PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "p_rating", keywordLen) == 0)
   {  // parental  rating (0 means "none", 1 "all")
      sprintf(strbuf, "%d", pPiBlock->parental_rating * 2);
      PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
   }
   else
   {
      strcpy(strbuf, "${");
      strncpy(strbuf + 2, pKeyword, keywordLen);
      strbuf[2 + keywordLen] = 0;
      strcat(strbuf, ": unknown variable}");
      PiOutput_ExtCmdAppend(pCmdBuf, strbuf);
   }
}

// ----------------------------------------------------------------------------
// Substitute variables in UNIX Bourne Shell command line
// - at this point we have: one PI and one command specification string
// - now we parse the spec string and replace all ${variable:modifiers} by PI data
//
static void PiOutput_CmdSubstitute( const char * pUserCmd, DYN_CHAR_BUF * pCmdBuf, bool breakSpace,
                                    const PI_BLOCK * pPiBlock, const AI_BLOCK * pAiBlock )
{
   const char *ps, *pKeyword, *pModifier;
   uint keywordLen;
   uint modifierLen;

   ps = pUserCmd;
   while (*ps != 0)
   {
      if ( (*ps == '$') && (*(ps + 1) == '{') )
      {
         // found variable start -> search for closing brace
         pKeyword    = ps + 2;
         pModifier   = NULL;
         keywordLen  = 0;
         modifierLen = 0;

         ps += 2;
         while ( (*ps != 0) && (*ps != '}') )
         {
            // check for a modifier string appended to the variable name after ':'
            if ((*ps == ':') && (pModifier == NULL))
            {  // found -> break off the modstr from the varstr by replacing ':' with 0
               keywordLen = ps - pKeyword;
               pModifier  = ps + 1;
            }
            ps++;
         }
         if (*ps != 0)
         {  // found closing brace
            if (pModifier != NULL)
               modifierLen = ps - pModifier;
            else
               keywordLen  = ps - pKeyword;
            // replace the ${} construct with the actual content
            PiOutput_ExtCmdVariable(pPiBlock, pAiBlock,
                                    pKeyword, keywordLen,
                                    pModifier, modifierLen,
                                    pCmdBuf);
            ps += 1;
         }
         else
         {  // syntax error: no closing brace -> treat it as normal text
            // track back to the '$' and append it to the output
            ps = pKeyword - 2;
            PiOutput_ExtCmdAppendChar(*(ps++), pCmdBuf);
         }
      }
      else
      {  // outside of variable -> just append the character
         if ((*ps == ' ') && (breakSpace))
            PiOutput_ExtCmdAppendChar('\0', pCmdBuf);
         else
            PiOutput_ExtCmdAppendChar(*ps, pCmdBuf);
         ps += 1;
      }
   }
}

// ----------------------------------------------------------------------------
// Execute a user-defined command on the currently selected PI
// - invoked from the context menu
//
static int PiOutput_ExecUserCmd( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ExecUserCmd <index>";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   Tcl_Obj    * pCtxVarObj;
   Tcl_Obj    * pCtxItemObj;
   const char * pUserCmd;
   Tcl_DString  ds;
   DYN_CHAR_BUF cmdbuf;
   int  userCmdIndex;
   int  result;

   if (objc != 2) 
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if (Tcl_GetIntFromObj(interp, objv[1], &userCmdIndex) != TCL_OK)
   {  // illegal parameter format
      result = TCL_ERROR; 
   }
   else
   {
      result = TCL_OK; 

      // allocate temporary buffer for the command line to be built
      cmdbuf.size   = 2048;
      cmdbuf.off    = 0;
      cmdbuf.strbuf = xmalloc(cmdbuf.size);

      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      // query listbox for user-selected PI, if any
      pPiBlock = PiListBox_GetSelectedPi();
      if ((pAiBlock != NULL) && (pPiBlock != NULL))
      {
         pCtxVarObj = Tcl_GetVar2Ex(interp, "ctxmencf", NULL, TCL_GLOBAL_ONLY);
         if (pCtxVarObj != NULL)
         {
            if ((Tcl_ListObjIndex(interp, pCtxVarObj, userCmdIndex * 2 + 1, &pCtxItemObj) == TCL_OK) && (pCtxItemObj != NULL))
            {
               pUserCmd = Tcl_UtfToExternalDString(NULL, Tcl_GetString(pCtxItemObj), -1, &ds);
               #ifdef WIN32
               if (strncmp(USERCMD_PREFIX_WINTV, pUserCmd, USERCMD_PREFIX_WINTV_LEN) == 0)
               {  // substitute and break strings apart at spaces, then pass argv to TV app
                  uint argc, idx;

                  pUserCmd += USERCMD_PREFIX_WINTV_LEN;
                  // skip spaces between !wintv! keyword and command name
                  while (*pUserCmd == ' ')
                     pUserCmd += 1;

                  cmdbuf.quoteShell = FALSE;
                  PiOutput_CmdSubstitute(pUserCmd, &cmdbuf, TRUE, pPiBlock, pAiBlock);
                  PiOutput_ExtCmdAppendChar('\0', &cmdbuf);

                  argc = 0;
                  for (idx=0; idx < cmdbuf.off; idx++)
                     if (cmdbuf.strbuf[idx] == '\0')
                        argc += 1;

                  if (WintvSharedMem_SetEpgCommand(argc, cmdbuf.strbuf, cmdbuf.off) == FALSE)
                  {
                     bool isConnected;

                     isConnected = WintvSharedMem_IsConnected(NULL, 0, NULL);
                     if (isConnected == FALSE)
                     {
                        sprintf(comm, "tk_messageBox -type ok -icon error -parent . "
                                      "-message \"Cannot record: no TV application connected!\"");
                     }
                     else
                     {
                        sprintf(comm, "tk_messageBox -type ok -icon error -parent . "
                                      "-message \"Failed to send the record command to the TV app.\"");
                     }
                     eval_check(interp, comm);
                     Tcl_ResetResult(interp);
                  }
               }
               else
               #endif
               {  // substitute and quote variables in UNIX style, then execute the command
                  cmdbuf.quoteShell = TRUE;
                  PiOutput_CmdSubstitute(pUserCmd, &cmdbuf, FALSE, pPiBlock, pAiBlock);

                  // append a '&' to have the shell execute the command asynchronously
                  // in the background; else we would hang until the command finishes.
                  // XXX TODO must be adapted for WIN API
                  #ifndef WIN32
                  PiOutput_ExtCmdAppendChar('&', &cmdbuf);
                  #endif

                  // finally null-terminate the string
                  PiOutput_ExtCmdAppendChar('\0', &cmdbuf);

                  // execute the command
                  system(cmdbuf.strbuf);
               }
               Tcl_DStringFree(&ds);
            }
            else
               debug1("PiOutput-ExecUserCmd: user cmd #%d not found", userCmdIndex);
         }
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
      xfree(cmdbuf.strbuf);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Add user-defined commands to conext menu
//
void PiOutput_CtxMenuAddUserDef( const char * pMenu, bool addSeparator )
{
   Tcl_Obj  * pCtxVarObj;
   Tcl_Obj ** pCtxList;
   int  userCmdCount;
   uint idx;
   #ifdef WIN32
   bool isConnected;

   isConnected = WintvSharedMem_IsConnected(NULL, 0, NULL);
   #endif

   pCtxVarObj = Tcl_GetVar2Ex(interp, "ctxmencf", NULL, TCL_GLOBAL_ONLY);
   if ( (pCtxVarObj != NULL) &&
        (Tcl_ListObjGetElements(interp, pCtxVarObj, &userCmdCount, &pCtxList) == TCL_OK) &&
        (userCmdCount > 0) )
   {
      if (addSeparator)
      {
         sprintf(comm, "%s add separator\n", pMenu);
         eval_check(interp, comm);
      }

      for (idx=0; idx + 1 < userCmdCount; idx += 2)
      {
         sprintf(comm, "%s add command -label {%s} -command {C_ExecUserCmd %d}",
                       pMenu, Tcl_GetString(pCtxList[idx]), idx / 2);

         #ifdef WIN32
         if ( (strncmp(USERCMD_PREFIX_WINTV, Tcl_GetString(pCtxList[idx + 1]), USERCMD_PREFIX_WINTV_LEN) == 0) &&
              (isConnected == FALSE) )
         {
            strcat(comm, " -state disabled");
         }
         #endif
         eval_check(interp, comm);
      }
   }
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
void PiOutput_Create( void )
{
   Tcl_CmdInfo cmdInfo;
   uint  idx;

   if (Tcl_GetCommandInfo(interp, "C_PiOutput_CfgPiColumns", &cmdInfo) == 0)
   {
      Tcl_CreateObjCommand(interp, "C_PiOutput_CfgColumns", PiOutput_CfgPiColumns, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_DumpHtml", PiOutput_DumpHtml, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_DumpXml", PiOutput_DumpXml, (ClientData) NULL,NULL);
      Tcl_CreateObjCommand(interp, "C_PopupPi", PiOutput_PopupPi, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_ExecUserCmd", PiOutput_ExecUserCmd, (ClientData) NULL, NULL);

      memset(&tcl_obj, 0, sizeof(tcl_obj));
      tcl_obj[TCLOBJ_WID_LIST]      = Tcl_NewStringObj(".all.pi.list.text", -1);
      tcl_obj[TCLOBJ_STR_INSERT]    = Tcl_NewStringObj("insert", -1);
      tcl_obj[TCLOBJ_STR_NOW]       = Tcl_NewStringObj("now", -1);
      tcl_obj[TCLOBJ_STR_THEN]      = Tcl_NewStringObj("then", -1);
      tcl_obj[TCLOBJ_STR_TEXT_IDX]  = Tcl_NewStringObj("#####", -1);
      tcl_obj[TCLOBJ_STR_TEXT_ANY]  = Tcl_NewStringObj("", -1);
      tcl_obj[TCLOBJ_STR_LIST_ANY]  = Tcl_NewListObj(0, NULL);

      for (idx=0; idx < TCLOBJ_COUNT; idx++)
         Tcl_IncrRefCount(tcl_obj[idx]);

      eval_check(interp, "DownloadUserDefinedColumnFilters");
      eval_check(interp, "UpdatePiListboxColumns");
   }
   else
      fatal0("PiOutput-Create: commands were already created");
}

