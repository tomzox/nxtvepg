/*
 *  Nextview GUI: PI description text output
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
 *    Implements display for programme descriptions in the main window
 *    and sub-functions for description text output for database export.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: pidescr.c,v 1.6 2003/06/28 11:22:44 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgmain.h"
#include "epgui/pdc_themes.h"
#include "epgui/pidescr.h"


// ----------------------------------------------------------------------------
// Array which keeps pre-allocated Tcl/Tk string objects
//
typedef enum
{
   TCLOBJ_WID_INFO,
   TCLOBJ_STR_INSERT,
   TCLOBJ_STR_END,
   TCLOBJ_STR_TITLE,
   TCLOBJ_STR_FEATURES,
   TCLOBJ_STR_BOLD,
   TCLOBJ_STR_PARAGRAPH,
   TCLOBJ_COUNT
} PIBOX_TCLOBJ;

static Tcl_Obj * tcl_obj[TCLOBJ_COUNT];

// ----------------------------------------------------------------------------
// Table to implement isalnum() for all latin fonts
//
const schar alphaNumTab[256] =
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

// ----------------------------------------------------------------------------
// Prepare title for alphabetical sorting
// - remove appended series counter from title string =~ s/ \(\d+\)$//
// - move attribs "Der, Die, Das" to the end of the title for sorting
//
const char * PiDescription_DictifyTitle( const char * pTitle, uchar lang, char * outbuf, uint maxLen )
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
// Search for short-info text in each paragraph of long info
// - short-info paragraph is completely omitted if it's repeated completely at
//   the start of a long-info paragraph (but the long-info paragraph may be longer);
//   This applies to the French provider M6
// - a short-info paragraph may also be only partially omitted, if a longer text
//   at the end of the paragraph is repeated at the start of a long-info paragraph;
//   This applies to the Germany provider RTL2 (they don't insert paragraph breaks
//   before the description text in short-info)
//
static sint PiDescription_SearchLongInfo( const uchar * pShort, uint shortInfoLen,
                                          const uchar * pLong,  uint longInfoLen )
{
   const uchar * pNewline;
   const uchar * pt;
   uint  len;
   uchar c;
   sint  result = shortInfoLen;

   while (longInfoLen > 0)
   {
      pNewline = pLong;
      while ((longInfoLen > 0) && (*pNewline != '\n'))
      {
         pNewline    += 1;
         longInfoLen -= 1;
      }

      len = pNewline - pLong;
      if (shortInfoLen < len)
      {
         len = shortInfoLen;
         pt = pShort;
      }
      else
         pt = pShort + (shortInfoLen - len);

      if (len > 30)
      {
         // min length is 30, because else single words might match
         c = *pLong;
         while (len-- > 30)
         {
            if (*(pt++) == c)
            {
               if (strncmp(pt, pLong + 1, len) == 0)
               {  // start of long info is identical to end of short info
                  // -> truncate short info to remove identical part
                  result = shortInfoLen - (len + 1);

                  // remove whitespace and separator characters from the end of the truncated text
                  pt = pShort + result - 1;
                  while ( (pt > pShort) &&
                          ((*pt == ' ') || (*pt == ':') || (*pt == '*') || (*pt == '-')) )
                  {
                     result -= 1;
                     pt -= 1;
                  }
                  dprintf1("PiDescription-SearchLongInfo: REMOVE %s\n", pShort + result);
                  // break inner and outer loop (hence the goto)
                  goto found_match;
               }
            }
         }
      }
      else if (shortInfoLen <= len)
      {  // very short paragraph -> only match if the short-info text is contained completely
         if (strncmp(pShort, pLong, shortInfoLen) == 0)
         {
            result = 0;
            goto found_match;
         }
      }

      // forward pointer to behind this paragraph
      pLong = pNewline;
      if (longInfoLen > 0)
      {  // skip newline character, unless at end of text
         pLong += 1;
         longInfoLen -= 1;
      }
   }

found_match:
   return result;
}

// ----------------------------------------------------------------------------
// Concatenate short with long info text and remove redundant paragraphs
// - each paragraph of short-info is compared with long-info;
// - the invoked compare function then compares the short-info paragraph separately
//   with each long-info paragraph
//
static uchar * PiDescription_UnifyShortLong( const uchar * pShort, uint shortInfoLen,
                                             const uchar * pLong,  uint longInfoLen )
{
   const uchar * pNewline;
   sint  outlen;
   sint  len;
   sint  nonRedLen;
   uchar * pOut = NULL;

   if ((pShort != NULL) && (pLong != NULL))
   {
      pOut = xmalloc(shortInfoLen + longInfoLen + 1 + 1);
      outlen = 0;

      while (shortInfoLen > 0)
      {
         pNewline = pShort;
         while ((shortInfoLen > 0) && (*pNewline != '\n'))
         {
            pNewline     += 1;
            shortInfoLen -= 1;
         }

         // ignore empty paragraphs (should never happen)
         if (pNewline != pShort)
         {
            // cut off trailing "..." for comparison
            len = pNewline - pShort;
            if ( (len > 3) &&
                 (pShort[len - 3] == '.') && (pShort[len - 2] == '.') && (pShort[len - 1] == '.') )
            {
               len -= 3;
            }
            // append the paragraph to the output buffer
            strncpy(pOut + outlen, pShort, len);
            pOut[outlen + len] = 0;

            // check if this paragraph is part of long-info
            nonRedLen = PiDescription_SearchLongInfo(pOut + outlen, len, pLong, longInfoLen);
            if (nonRedLen > 0)
            {  // not identical -> append this paragraph
               if ((nonRedLen == len) && (len < pNewline - pShort))
               {  // append any stuff which was cut off above
                  nonRedLen = pNewline - pShort;
                  strncpy(pOut + outlen + len, pShort + len, nonRedLen - len);
               }
               outlen += nonRedLen;
               // append newline character, unless this is the first paragraph
               // (note: no need to null-terminate the string here; done after the loop)
               if (shortInfoLen > 0)
                  pOut[outlen++] = '\n';
            }
            // forward pointer to behind this paragraph
            pShort = pNewline;
            if (shortInfoLen > 0)
            {  // skip newline character, unless at end of text
               pShort += 1;
               shortInfoLen -= 1;
            }
         }
         else  // (shortInfoLen > 0) is implied
         {
            pShort += 1;
            shortInfoLen -= 1;
         }
      }

      if ((outlen > 0) && (pOut[outlen - 1] != '\n'))
         pOut[outlen++] = '\n';

      strncpy(pOut + outlen, pLong, longInfoLen);
      pOut[outlen + longInfoLen] = 0;
   }
   else
      fatal2("PiDescription-UnifyShortLong: illegal NULL ptr params: %ld, %ld", (long)pShort, (long)pLong);

   return pOut;
}

// ----------------------------------------------------------------------------
// Remove descriptions that are substrings of other info strings in the given list
//
static uint PiDescription_UnifyMergedInfo( uchar ** infoStrTab, uint infoCount )
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
                        dprintf1("PiDescription-UnifyMergedInfo: remove %s\n", infoStrTab[idx]);
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
static uint PiDescription_SeparateMergedInfo( const PI_BLOCK * pPiBlock, uchar ** infoStrTab )
{
   const char *p, *ps, *pl;
   int   shortInfoLen, longInfoLen;
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
         {  // start of long info found
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
         // there's both short and long info -> redundancy removal
         infoStrTab[count] = PiDescription_UnifyShortLong(ps, shortInfoLen, pl, longInfoLen);
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
void PiDescription_AppendShortAndLongInfoText( const PI_BLOCK * pPiBlock,
                                               PiDescr_AppendInfoTextCb_Type AppendInfoTextCb,
                                               void * fp,
                                               bool isMerged )
{
   if ( PI_HAS_SHORT_INFO(pPiBlock) && PI_HAS_LONG_INFO(pPiBlock) )
   {
      const uchar * pShortInfo = PI_GET_SHORT_INFO(pPiBlock);
      const uchar * pLongInfo  = PI_GET_LONG_INFO(pPiBlock);
      uchar * pConcat;

      pConcat = PiDescription_UnifyShortLong(pShortInfo, strlen(pShortInfo), pLongInfo, strlen(pLongInfo));
      AppendInfoTextCb(fp, pConcat, FALSE);
      xfree((void *) pConcat);
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

         infoCount = PiDescription_SeparateMergedInfo(pPiBlock, infoStrTab);
         infoCount = PiDescription_UnifyMergedInfo(infoStrTab, infoCount);
         added = 0;

         for (idx=0; idx < infoCount; idx++)
         {
            if (infoStrTab[idx] != NULL)
            {
               // add the short info to the text widget insert command
               // - if not the only or first info -> insert separator image (a horizontal line)
               AppendInfoTextCb(fp, infoStrTab[idx], (added > 0));

               xfree(infoStrTab[idx]);
               added += 1;
            }
         }
      }
      else
      {
         AppendInfoTextCb(fp, PI_GET_SHORT_INFO(pPiBlock), FALSE);
      }
   }
   else if (PI_HAS_LONG_INFO(pPiBlock))
   {
      AppendInfoTextCb(fp, PI_GET_LONG_INFO(pPiBlock), FALSE);
   }
}

// ----------------------------------------------------------------------------
// Print PDC themes into string with removed redundancy
//
void PiDescription_AppendCompressedThemes( const PI_BLOCK *pPiBlock, char * outstr, uint maxlen )
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
void PiDescription_AppendFeatureList( const PI_BLOCK *pPiBlock, char * outstr )
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
// Insert a character string into a text widget at a given line
// - could be done simpler by Tcl_Eval() but using objects has the advantage
//   that no command line needs to be parsed so that a '}' in the string
//   does no harm.
//
static void PiDescription_InsertText( PIBOX_TCLOBJ widgetObjIdx, int trow, const char * pStr, PIBOX_TCLOBJ tagObjIdx )
{
   Tcl_Obj * objv[10];
   char   linebuf[15];
   uint   objc;

   // assemble a command vector, starting with the widget name/command
   objv[0] = tcl_obj[widgetObjIdx];
   objv[1] = tcl_obj[TCLOBJ_STR_INSERT];
   if (trow >= 0)
   {  // line number was specified; add 1 because first line is 1.0
      sprintf(linebuf, "%d.0", trow + 1);
      objv[2] = Tcl_NewStringObj(linebuf, -1);
      Tcl_IncrRefCount(objv[2]);
   }
   else
      objv[2] = tcl_obj[TCLOBJ_STR_END];
   objv[3] = Tcl_NewStringObj(pStr, -1);
   Tcl_IncrRefCount(objv[3]);
   objv[4] = tcl_obj[tagObjIdx];
   objc = 5;

   // execute the command vector
   if (Tcl_EvalObjv(interp, objc, objv, 0) != TCL_OK)
      debugTclErr(interp, "PiDescription-InsertText");

   // free temporary objects
   if (trow >= 0)
      Tcl_DecrRefCount(objv[2]);
   Tcl_DecrRefCount(objv[3]);
}

// ----------------------------------------------------------------------------
// Callback function for PiDescription-AppendShortAndLongInfoText
//
static void PiDescription_AppendInfoTextCb( void *fp, const char * pDesc, bool addSeparator )
{
   assert(fp == NULL);

   if (pDesc != NULL)
   {
      if (addSeparator)
      {  // separator between info texts of different providers
         sprintf(comm, ".all.pi.info.text insert end {\n\n} title\n"
                       ".all.pi.info.text image create {end - 2 line} -image bitmap_line\n");
         eval_check(interp, comm);
      }

      //Tcl_VarEval(interp, ".all.pi.info.text insert end {", pShortInfo, "}", NULL);
      PiDescription_InsertText(TCLOBJ_WID_INFO, -1, pDesc, TCLOBJ_STR_PARAGRAPH);
   }
}

// ----------------------------------------------------------------------------
// Display short and long info for the given PI block
//
void PiDescription_UpdateText( const PI_BLOCK * pPiBlock, bool keepView )
{  
   const AI_BLOCK *pAiBlock;
   const uchar *pCfNetname;
   uchar start_str[40], stop_str[20], cni_str[7];
   uchar view_buf[20];
   int len;
   
   if (keepView)
   {  // remember the viewable fraction of the text
      sprintf(comm, "lindex [.all.pi.info.text yview] 0\n");
      eval_check(interp, comm);
      strncpy(view_buf, Tcl_GetStringResult(interp), sizeof(view_buf));
      view_buf[sizeof(view_buf) - 1] = 0;
   }

   sprintf(comm, ".all.pi.info.text delete 1.0 end\n");
   eval_check(interp, comm);

   if (pPiBlock != NULL)
   {
      pAiBlock = EpgDbGetAi(pUiDbContext);

      if ((pAiBlock != NULL) && (pPiBlock->netwop_no < pAiBlock->netwopCount))
      {
         // top of the info window: programme title text
         strcpy(comm, PI_GET_TITLE(pPiBlock));
         strcat(comm, "\n");
         PiDescription_InsertText(TCLOBJ_WID_INFO, -1, comm, TCLOBJ_STR_TITLE);

         // now add a feature summary: start with theme list
         PiDescription_AppendCompressedThemes(pPiBlock, comm, TCL_COMM_BUF_SIZE);

         // append features list
         strcat(comm, " (");
         PiDescription_AppendFeatureList(pPiBlock, comm + strlen(comm));
         // remove opening bracket if nothing follows
         len = strlen(comm);
         if ((comm[len - 2] == ' ') && (comm[len - 1] == '('))
            strcpy(comm + len - 2, "\n");
         else
            strcpy(comm + len, ")\n");
         // append the feature string to the text widget content
         PiDescription_InsertText(TCLOBJ_WID_INFO, -1, comm, TCLOBJ_STR_FEATURES);

         // print network name, start- & stop-time, short info
         strftime(start_str, sizeof(start_str), " %a %d.%m., %H:%M", localtime(&pPiBlock->start_time));
         strftime(stop_str, sizeof(stop_str), "%H:%M", localtime(&pPiBlock->stop_time));

         sprintf(cni_str, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
         pCfNetname = Tcl_GetVar2(interp, "cfnetnames", cni_str, TCL_GLOBAL_ONLY);
         if (pCfNetname == NULL)
            pCfNetname = AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no);

         sprintf(comm, "%s, %s - %s: ", pCfNetname, start_str, stop_str);
         PiDescription_InsertText(TCLOBJ_WID_INFO, -1, comm, TCLOBJ_STR_BOLD);

         PiDescription_AppendShortAndLongInfoText(pPiBlock, PiDescription_AppendInfoTextCb, NULL, EpgDbContextIsMerged(pUiDbContext));
      }
      else
         debug1("PiDescription-UpdateDescr: no AI block or invalid netwop=%d", pPiBlock->netwop_no);
   }
   else
      fatal0("PiDescription-UpdateDescr: invalid NULL ptr param");

   if (keepView)
   {  // set the view back to its previous position
      sprintf(comm, ".all.pi.info.text yview moveto %s\n", view_buf);
      eval_check(interp, comm);
   }
}

// ----------------------------------------------------------------------------
// Free resources allocated by the listbox
//
void PiDescription_ClearText( void )
{
   sprintf(comm, ".all.pi.info.text delete 1.0 end\n");
   eval_check(interp, comm);
}

// ----------------------------------------------------------------------------
// Free resources allocated by the listbox
//
void PiDescription_Destroy( void )
{
   uint  idx;

   for (idx=0; idx < TCLOBJ_COUNT; idx++)
      Tcl_DecrRefCount(tcl_obj[idx]);
   memset(tcl_obj, 0, sizeof(tcl_obj));
}

// ----------------------------------------------------------------------------
// create the listbox and its commands
// - this should be called only once during start-up
//
void PiDescription_Init( void )
{
   uint  idx;

   memset(&tcl_obj, 0, sizeof(tcl_obj));
   tcl_obj[TCLOBJ_WID_INFO]      = Tcl_NewStringObj(".all.pi.info.text", -1);
   tcl_obj[TCLOBJ_STR_INSERT]    = Tcl_NewStringObj("insert", -1);
   tcl_obj[TCLOBJ_STR_END]       = Tcl_NewStringObj("end", -1);
   tcl_obj[TCLOBJ_STR_TITLE]     = Tcl_NewStringObj("title", -1);
   tcl_obj[TCLOBJ_STR_FEATURES]  = Tcl_NewStringObj("features", -1);
   tcl_obj[TCLOBJ_STR_BOLD]      = Tcl_NewStringObj("bold", -1);
   tcl_obj[TCLOBJ_STR_PARAGRAPH] = Tcl_NewStringObj("paragraph", -1);

   for (idx=0; idx < TCLOBJ_COUNT; idx++)
      Tcl_IncrRefCount(tcl_obj[idx]);
}

