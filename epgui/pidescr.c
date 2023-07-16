/*
 *  Nextview GUI: PI description text output
 *
 *  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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
 *    This modules contains functions to build programme description texts
 *    for output in the description text window below the PI programme list
 *    and for database export.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tcl.h>  // Unicode support

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgmain.h"
#include "epgui/pidescr.h"


#define ISDIGIT_ASCII(C) (((C) >= '0') && ((C) <= '9'))

// ----------------------------------------------------------------------------
// Remove appended series counter from title string
// - effectively performs subsitution regexp: s/ \(\d+\)$//
//
const char * PiDescription_RemoveSeriesIndex( const char * pTitle, char * outbuf, uint maxLen )
{
   char * pe;
   uint  len;

   len = strlen(pTitle);
   if ((len >= 5) && (len < maxLen) && (*(pTitle + len - 1) == ')'))
   {  // found closing brace at end of string -> check for preceeding decimal number
      pe = (char *)pTitle + len - 1 - 1;  // cast to remove "const"
      len -= 1;

      while (TRUE)
      {
         if (ISDIGIT_ASCII(*pe))
         {  // at least one digit found
            len -= 1;
            pe -= 1;
            // skip preceeding digits
            while ((len > 0) && ISDIGIT_ASCII(*pe))
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
               break;
            }
            else if ((len > 2) && ((*pe == '-') || (*pe == '+') || (*pe == '/')))
            {
               len -= 1;
               pe -= 1;
               // redoo loop: skip (at least) one more preceding number
            }
            else
               break;
         }
         else
            break;
      }
   }
   return pTitle;
}

// ----------------------------------------------------------------------------
// Prepare title for alphabetical sorting
// - remove appended series counter from title string
// - move attribs "Der, Die, Das" to the end of the title for sorting
//
const char * PiDescription_DictifyTitle( const char * pTitle, uint lang, char * outbuf, uint maxLen )
{
   uint cut, len;

   // remove appended series counter from title string
   pTitle = PiDescription_RemoveSeriesIndex(pTitle, outbuf, maxLen);

   // check for attribs "Der, Die, Das" at the start of the string
   cut = 0;
   switch (lang)
   {
      case EPG_LANG_EN:
         // English
         if (((pTitle[0] == 'T') || (pTitle[0] == 't')) &&
             (pTitle[1] == 'h') && (pTitle[2] == 'e') && (pTitle[3] == ' '))
            cut = 4;
         else if (((pTitle[0] == 'A') || (pTitle[0] == 'a')) && (pTitle[1] == ' '))
            cut = 2;
         break;
      case EPG_LANG_DE:
         // German
         if ((pTitle[0] == 'D') || (pTitle[0] == 'd'))
         {
            if      ((pTitle[1] == 'e') && (pTitle[2] == 'r') && (pTitle[3] == ' '))
               cut = 4;
            else if ((pTitle[1] == 'i') && (pTitle[2] == 'e') && (pTitle[3] == ' '))
               cut = 4;
            else if ((pTitle[1] == 'a') && (pTitle[2] == 's') && (pTitle[3] == ' '))
               cut = 4;
         }
         else if ((pTitle[0] == 'E') || (pTitle[0] == 'e'))
         {
            if ((pTitle[1] == 'i') && (pTitle[2] == 'n'))
            {
               if (pTitle[3] == ' ')
                  cut = 4;
               else if ((pTitle[3] == 'e') && (pTitle[4] == ' '))
                  cut = 5;
            }
         }
         break;
      case EPG_LANG_IT:
         // Italian
         if (((pTitle[0] == 'U') || (pTitle[0] == 'u')) &&
             (pTitle[1] == 'n'))
         {
            if      ((pTitle[2] == 'a') && (pTitle[3] == ' '))
               cut = 4;
            else if ((pTitle[2] == 'o') && (pTitle[3] == ' '))
               cut = 4;
         }
         else if (((pTitle[0] == 'L') || (pTitle[0] == 'l')) &&
                  (pTitle[1] == 'a') && (pTitle[2] == ' '))
            cut = 3;
         break;
      case EPG_LANG_FR:
         // French
         if (((pTitle[0] == 'U') || (pTitle[0] == 'u')) &&
             (pTitle[1] == 'n'))
         {
            if (pTitle[2] == ' ')
               cut = 3;
            else if ((pTitle[2] == 'e') && (pTitle[3] == ' '))
               cut = 4;
         }
         else if ((pTitle[0] == 'L') || (pTitle[0] == 'l'))
         {
            if      ((pTitle[1] == 'a') && (pTitle[2] == ' '))
               cut = 3;
            else if ((pTitle[1] == 'e') && (pTitle[2] == ' '))
               cut = 3;
            else if (pTitle[1] == '\'')
               cut = 2;
            else if ((pTitle[1] == 'e') && (pTitle[2] == 's') && (pTitle[3] == ' '))
               cut = 4;
         }
         break;
      case EPG_LANG_PL:
         // Polish
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
         char buf[10];
         char *pe;

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
   Tcl_UniChar uniChar;
   uint oldUniLen = Tcl_UtfToUniChar(pTitle, &uniChar);
   if (Tcl_UniCharIsLower(uniChar))
   {
      if (pTitle != outbuf)
      {
         uint newUniLen = Tcl_UniCharToUtf(Tcl_UniCharToUpper(uniChar), outbuf);
         if (maxLen > newUniLen)
            strncpy(outbuf + newUniLen, pTitle + oldUniLen, maxLen - newUniLen);
         outbuf[maxLen - 1] = 0;
         pTitle = outbuf;
      }
      else
      {
         char tmpBuf[5];
         uint newUniLen = Tcl_UniCharToUtf(Tcl_UniCharToUpper(uniChar), tmpBuf);
         if (oldUniLen != newUniLen)
         {
            uint strLen = strlen(outbuf) + 1;
            if (strLen <= oldUniLen)
               memmove(outbuf + newUniLen, outbuf + oldUniLen, strlen(outbuf) - oldUniLen);
         }
         memcpy(outbuf, tmpBuf, newUniLen);
      }
   }

   return pTitle;
}

#if 0
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
static sint PiDescription_SearchLongInfo( const char * pShort, uint shortInfoLen,
                                          const char * pLong,  uint longInfoLen )
{
   const char * pNewline;
   const char * pt;
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
static char * PiDescription_UnifyShortLong( const char * pShort, uint shortInfoLen,
                                            const char * pLong,  uint longInfoLen )
{
   const char * pNewline;
   sint  outlen;
   sint  len;
   sint  nonRedLen;
   char * pOut = NULL;

   if ((pShort != NULL) && (pLong != NULL))
   {
      pOut = (char*) xmalloc(shortInfoLen + longInfoLen + 1 + 1);
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
#endif

#if 0
// ----------------------------------------------------------------------------
// Remove descriptions that are substrings of other info strings in the given list
//
static uint PiDescription_UnifyMergedInfo( char ** infoStrTab, uint infoCount )
{
   uchar c1, c2;
   char *pidx, *pcmp, *p1, *p2;
   uint idx, cmpidx;
   int len, cmplen;

   for (idx = 0; idx < infoCount; idx++)
   {
      pidx = infoStrTab[idx];
      while ( (*pidx != 0) && !isalnum(*pidx))  //TODO Unicode
         pidx += 1;
      len = strlen(pidx);

      for (cmpidx = 0; cmpidx < infoCount; cmpidx++)
      {
         if ((idx != cmpidx) && (infoStrTab[cmpidx] != NULL))
         {
            pcmp = infoStrTab[cmpidx];
            while ( (*pcmp != 0) && !isalnum(*pcmp) )
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
                        if (!isalnum(c1))
                           p1++;
                        else if (!isalnum(c2))
                           p2++;
                        else
                        {
                           if (toupper(c1) != toupper(c2))  //TODO Unicode
                           {
                              break;
                           }
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
// - Merged database needs special handling, because description texts of all
//   providers are concatenated into one string. Separator character is an
//   ASCII form-feed.
// - Returns the number of separate strings and puts their pointers into the array.
//   The caller must free the separated strings.
//
static uint PiDescription_SeparateMergedInfo( const PI_BLOCK * pPiBlock, char ** infoStrTab )
{
   const char *p, *ps;
   int   descTextLen;
   uint  count;

   p = PI_GET_DESC_TEXT(pPiBlock);
   count = 0;
   do
   {  // loop across all provider's descriptions

      // obtain start and length of this provider's description text
      descTextLen = 0;
      ps = p;
      while (*p)
      {
         if (*p == EPG_DB_MERGE_DESC_TEXT_SEP)
         {  // end of description text found
            descTextLen = p - ps;
            p++;
            break;
         }
         p++;
      }

      // only description text available; copy it into the array
      infoStrTab[count] = (char*) xmalloc(descTextLen + 1);
      memcpy(infoStrTab[count], ps, descTextLen);
      infoStrTab[count][descTextLen] = 0;

      count += 1;

   } while (*p);

   return count;
}
#endif

// ----------------------------------------------------------------------------
// Print description text
//
void PiDescription_AppendDescriptionText( const PI_BLOCK * pPiBlock,
                                          PiDescr_AppendInfoTextCb_Type AppendInfoTextCb,
                                          void * fp, bool isMerged )
{
   if (PI_HAS_DESC_TEXT(pPiBlock))
   {
#if 0 // Currently obsolete as XMLTV import does not support merging different channels
      if (isMerged)
      {
         char *infoStrTab[MAX_MERGED_DB_COUNT];
         uint infoCount, idx, added;

         // Merged database -> a separator image is added between description
         // texts of different providers.

         infoCount = PiDescription_SeparateMergedInfo(pPiBlock, infoStrTab);
         infoCount = PiDescription_UnifyMergedInfo(infoStrTab, infoCount);
         added = 0;

         for (idx=0; idx < infoCount; idx++)
         {
            if (infoStrTab[idx] != NULL)
            {
               // add the description string to the text widget insert command
               // - if not the only or first info -> insert separator image (a horizontal line)
               AppendInfoTextCb(fp, infoStrTab[idx], (added > 0));

               xfree(infoStrTab[idx]);
               added += 1;
            }
         }
      }
      else
#endif
      {
         AppendInfoTextCb(fp, PI_GET_DESC_TEXT(pPiBlock), FALSE);
      }
   }
}

// ----------------------------------------------------------------------------
// Print theme names into string
//
void PiDescription_AppendCompressedThemes( const PI_BLOCK *pPiBlock, char * outstr, uint maxlen )
{
   const char * pThemeStr;
   uint themeStrLen;
   char * po;

   if (maxlen > 0)
      outstr[0] = 0;

   po = outstr;
   for (uint idx=0; idx < pPiBlock->no_themes; idx++)
   {
      pThemeStr = EpgDbGetThemeStr(pUiDbContext, pPiBlock->themes[idx]);

      themeStrLen = strlen(pThemeStr);
      if (maxlen > themeStrLen + 2)
      {
         strcpy(po, pThemeStr);
         strcpy(po + themeStrLen, ", ");
         po     += themeStrLen + 2;
         maxlen -= themeStrLen + 2;
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
   switch (pPiBlock->feature_flags & PI_FEATURE_SOUND_MASK)
   {
      case  PI_FEATURE_SOUND_NONE: strcat(outstr, "none, "); break;
      case  PI_FEATURE_SOUND_MONO: strcat(outstr, "mono, "); break;
      case  PI_FEATURE_SOUND_STEREO: strcat(outstr, "stereo, "); break;
      case  PI_FEATURE_SOUND_2CHAN: strcat(outstr, "2-channel, "); break;
      case  PI_FEATURE_SOUND_SURROUND: strcat(outstr, "surround, "); break;
      case  PI_FEATURE_SOUND_DOLBY: strcat(outstr, "dolby, "); break;
      case  PI_FEATURE_SOUND_UNKNOWN:
      default: break;
   }

   if ((pPiBlock->feature_flags & PI_FEATURE_FMT_WIDE) != 0)
      strcat(outstr, "wide, ");
   if ((pPiBlock->feature_flags & PI_FEATURE_VIDEO_NONE) != 0)
      strcat(outstr, "radio, ");
   if ((pPiBlock->feature_flags & PI_FEATURE_VIDEO_BW) != 0)
      strcat(outstr, "b/w, ");
   if ((pPiBlock->feature_flags & PI_FEATURE_VIDEO_HD) != 0)
      strcat(outstr, "HDTV, ");

   if (pPiBlock->feature_flags & PI_FEATURE_NEW)
      strcat(outstr, "NEW, ");
   else if (pPiBlock->feature_flags & PI_FEATURE_PREMIERE)
      strcat(outstr, "premiere, ");
   else if (pPiBlock->feature_flags & PI_FEATURE_LAST_REP)
      strcat(outstr, "last repetition, ");
   else if (pPiBlock->feature_flags & PI_FEATURE_REPEAT)
      strcat(outstr, "repeated, ");

   if (pPiBlock->feature_flags & PI_FEATURE_SUBTITLE_MASK)
   {
      if ((pPiBlock->feature_flags & (PI_FEATURE_SUBTITLE_OSC | PI_FEATURE_SUBTITLE_SIGN)) ==
          (PI_FEATURE_SUBTITLE_OSC | PI_FEATURE_SUBTITLE_SIGN))
         strcat(outstr, "deaf-signed onscreen, ");
      else if (pPiBlock->feature_flags & PI_FEATURE_SUBTITLE_OSC)
         strcat(outstr, "subtitled onscreen, ");
      else if (pPiBlock->feature_flags & PI_FEATURE_SUBTITLE_SIGN)
         strcat(outstr, "deaf-signed, ");
      else if (pPiBlock->feature_flags & PI_FEATURE_SUBTITLE_TTX)
         strcat(outstr, "subtitled teletext, ");
      else
         strcat(outstr, "subtitled, ");
   }

   if (pPiBlock->editorial_rating != PI_EDITORIAL_UNDEFINED)
      sprintf(outstr + strlen(outstr), "rating: %d of %d, ",
              pPiBlock->editorial_rating, pPiBlock->editorial_max_val);

   if (pPiBlock->parental_rating == 0)
      strcat(outstr, "age: general, ");
   else if (pPiBlock->parental_rating != PI_PARENTAL_UNDEFINED)
      sprintf(outstr + strlen(outstr), "age: %d and up, ", pPiBlock->parental_rating);

   // remove last comma if nothing follows
   len = strlen(outstr);
   if ((len >= 2) && (outstr[len - 2] == ',') && (outstr[len - 1] == ' '))
      outstr[len - 2] = 0;
}

// ----------------------------------------------------------------------------
// Helper function to print text into buffer or stream
//
void PiDescription_BufAppend( PI_DESCR_BUF * pBuf, const char * pStr, sint len )
{
   char * pNewbuf;

   if (len < 0)
   {
      len = strlen(pStr);
   }

   if (pBuf->fp != NULL)
   {
      fwrite(pStr, sizeof(char), len, pBuf->fp);
   }
   else
   {
      if (pBuf->off + len + 1 > pBuf->size)
      {
         pNewbuf = (char*) xmalloc(pBuf->size + len + 2048);
         if (pBuf->pStrBuf != NULL)
         {
            memcpy(pNewbuf, pBuf->pStrBuf, pBuf->size);
            xfree(pBuf->pStrBuf);
         }
         pBuf->pStrBuf = pNewbuf;
         pBuf->size  += len + 2048;
      }

      memcpy(pBuf->pStrBuf + pBuf->off, pStr, len + 1);
      pBuf->off += len;
   }
}

