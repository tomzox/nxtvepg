/*
 *  Nextview EPG bit field decoder
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
 *    Converts Nextview bit fields of all types into C structures
 *    as defined in epgblock.h.  See ETS 300 707 (Nextview Spec.),
 *    chapters 10 to 11 for details.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgblock.c,v 1.47 2002/10/19 17:42:01 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <time.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgswap.h"


static uchar netwopAlphabets[MAX_NETWOP_COUNT];
static uchar providerAlphabet;

static time_t unixTimeBase1982;         // 1.1.1982 in UNIX time format
#define JULIAN_DATE_1982  (45000-30)    // 1.1.1982 in Julian date format


// ----------------------------------------------------------------------------
// Save the alphabets of all networks for string decoding
//
void EpgBlockSetAlphabets( const AI_BLOCK *pAiBlock )
{
   uchar netwop;

   if (pAiBlock != NULL)
   {
      for (netwop=0; netwop < pAiBlock->netwopCount; netwop++)
      {
         netwopAlphabets[netwop] = AI_GET_NETWOP_N(pAiBlock, netwop)->alphabet;
      }
      providerAlphabet = AI_GET_NETWOP_N(pAiBlock, pAiBlock->thisNetwop)->alphabet;
   }
   else
      debug0("EpgBlock-SetAlphabets: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Allocate a new block and initialize the common elements
//
EPGDB_BLOCK * EpgBlockCreate( uchar type, uint size )
{
   EPGDB_BLOCK *pBlock;

   pBlock = (EPGDB_BLOCK *) xmalloc(size + BLK_UNION_OFF);

   pBlock->pNextBlock       = NULL;
   pBlock->pPrevBlock       = NULL;
   pBlock->pNextNetwopBlock = NULL;
   pBlock->pPrevNetwopBlock = NULL;

   pBlock->type = type;
   pBlock->size = size;

   pBlock->version = 0xff;
   pBlock->stream = 0xff;

   pBlock->acqTimestamp =
   pBlock->updTimestamp = time(NULL);
   pBlock->acqRepCount  = 1;

   dprintf2("EpgBlock-Create: created block type=%d, (0x%lx)\n", type, (long)pBlock);
   return pBlock;
}

// ----------------------------------------------------------------------------
// Retrieve the Local Time Offset (LTO) at the given time
// - the LTO at the given time may be different from the current one
//   due to a change in daylight saving time inbetween
// - hence we compute it anew upon every invocation. Since it should only be
//   required for interactive GUI stuff performance is not considered
//
sint EpgLtoGet( time_t when )
{
   struct tm *pTm;
   sint lto;

   #if !defined(__NetBSD__) && !defined(__FreeBSD__)
   pTm = localtime(&when);
   lto = 60*60 * pTm->tm_isdst - timezone;
   #else
   pTm = gmtime(&when);
   pTm->tm_isdst = -1;
   lto = when - mktime(pTm);
   #endif

   //printf("LTO = %d min, %s/%s, off=%ld, daylight=%d\n", lto/60, tzname[0], tzname[1], timezone/60, tm->tm_isdst);

   return lto;
}

// ---------------------------------------------------------------------------
// Initialize timer conversion variables
//
void EpgLtoInit( void )
{
   struct tm tm, *pTm;

   // initialize time variables in the standard C library
   tzset();

   // determine UNIX time format of "January 1st 1982, 0:00 am"
   // (required for conversion from Julian date to UNIX epoch)
   tm.tm_mday  = 1;
   tm.tm_mon   = 1 - 1;
   tm.tm_year  = 1982 - 1900;
   tm.tm_sec   = 0;
   tm.tm_min   = 0;
   tm.tm_hour  = 0;
   tm.tm_isdst = FALSE;
   unixTimeBase1982 = mktime(&tm);

   // undo the local->UTC conversion that mktime unwantedly always does
   #if !defined(__NetBSD__) && !defined(__FreeBSD__)
   pTm = localtime(&unixTimeBase1982);
   unixTimeBase1982 += 60*60 * pTm->tm_isdst - timezone;
   #else
   pTm = gmtime(&unixTimeBase1982);
   pTm->tm_isdst = -1;
   unixTimeBase1982 += (unixTimeBase1982 - mktime(pTm));
   #endif
}

// ----------------------------------------------------------------------------
// Convert a BCD coded time to "minutes since daybreak" (MoD)
//
uint EpgBlockBcdToMoD( uint BCD )
{
   return ((BCD >> 12)*10 + ((BCD & 0x0F00) >> 8)) * 60 +
          ((BCD & 0x00F0) >> 4)*10 + (BCD & 0x000F);
}   

// ----------------------------------------------------------------------------
// Convert a Nextview start and stop time to the internal time format
// - if start == stop a duration of zero minutes is assumed. This is a
//   violation of the Nextview spec, however that's how several EPG
//   providers transmit it (and assuming 24h duration would tear a
//   large hole into the database)
//  - XXX TODO check validity?
//
static void SetStartAndStopTime(uint bcdStart, uint julian, uint bcdStop, PI_BLOCK * pPiBlock )
{
   time_t startDate;
   uint startMoD, stopMoD;

   startMoD = EpgBlockBcdToMoD(bcdStart);
   stopMoD  = EpgBlockBcdToMoD(bcdStop);
   if (stopMoD < startMoD)
      stopMoD += 60*24;

   if (julian > JULIAN_DATE_1982)
      startDate = unixTimeBase1982 + (julian - JULIAN_DATE_1982) * (24*60*60L);
   else
      startDate = unixTimeBase1982;

   pPiBlock->start_time = startDate + startMoD * 60;
   pPiBlock->stop_time  = startDate + stopMoD  * 60;
}

// ----------------------------------------------------------------------------
// The following character tables were taken from ALEVT-1.5.1
// Copyright (C) 1998,1999 Edgar Toernig (froese@gmx.de)
// - conforming to ETS 300 706, chapter 15.3: "Second G0 Set Designation and
//   National Option Set Selection", table 33.
// - XXX no support for non-latin-1 fonts yet
//
static const uchar natOptChars[][16] =
{
    // for latin-1 font
    // English (100%)
    { '£', '$', '@', '«', '½', '»', '¬', '#', '­', '¼', '¦', '¾', '÷' },
    // German (100%)
    { '#', '$', '§', 'Ä', 'Ö', 'Ü', '^', '_', '°', 'ä', 'ö', 'ü', 'ß' },
    // Swedish/Finnish/Hungarian (100%)
    { '#', '¤', 'É', 'Ä', 'Ö', 'Å', 'Ü', '_', 'é', 'ä', 'ö', 'å', 'ü' },
    // Italian (100%)
    { '£', '$', 'é', '°', 'ç', '»', '¬', '#', 'ù', 'à', 'ò', 'è', 'ì' },
    // French (100%)
    { 'é', 'ï', 'à', 'ë', 'ê', 'ù', 'î', '#', 'è', 'â', 'ô', 'û', 'ç' },
    // Portuguese/Spanish (100%)
    { 'ç', '$', '¡', 'á', 'é', 'í', 'ó', 'ú', '¿', 'ü', 'ñ', 'è', 'à' },
    // Czech/Slovak (60%)
    { '#', 'u', 'c', 't', 'z', 'ý', 'í', 'r', 'é', 'á', 'e', 'ú', 's' },
    // reserved (English mapping)
    { '£', '$', '@', '«', '½', '»', '¬', '#', '­', '¼', '¦', '¾', '÷' },
    // Polish (6%: all but '#' missing)
    { '#', 'n', 'a', 'Z', 'S', 'L', 'c', 'o', 'e', 'z', 's', 'l', 'z' },
    // German (100%)
    { '#', '$', '§', 'Ä', 'Ö', 'Ü', '^', '_', '°', 'ä', 'ö', 'ü', 'ß' },
    // Swedish/Finnish/Hungarian (100%)
    { '#', '¤', 'É', 'Ä', 'Ö', 'Å', 'Ü', '_', 'é', 'ä', 'ö', 'å', 'ü' },
    // Italian (100%)
    { '£', '$', 'é', '°', 'ç', '»', '¬', '#', 'ù', 'à', 'ò', 'è', 'ì' },
    // French (100%)
    { 'é', 'ï', 'à', 'ë', 'ê', 'ù', 'î', '#', 'è', 'â', 'ô', 'û', 'ç' },
    // reserved (English mapping)
    { '£', '$', '@', '«', '½', '»', '¬', '#', '­', '¼', '¦', '¾', '÷' },
    // Czech/Slovak 
    { '#', 'u', 'c', 't', 'z', 'ý', 'í', 'r', 'é', 'á', 'e', 'ú', 's' },
    // reserved (English mapping)
    { '£', '$', '@', '«', '½', '»', '¬', '#', '­', '¼', '¦', '¾', '÷' },
    // English 
    { '£', '$', '@', '«', '½', '»', '¬', '#', '­', '¼', '¦', '¾', '÷' },
    // German
    { '#', '$', '§', 'Ä', 'Ö', 'Ü', '^', '_', '°', 'ä', 'ö', 'ü', 'ß' },
    // Swedish/Finnish/Hungarian (100%)
    { '#', '¤', 'É', 'Ä', 'Ö', 'Å', 'Ü', '_', 'é', 'ä', 'ö', 'å', 'ü' },
    // Italian (100%)
    { '£', '$', 'é', '°', 'ç', '»', '¬', '#', 'ù', 'à', 'ò', 'è', 'ì' },
    // French (100%)
    { 'é', 'ï', 'à', 'ë', 'ê', 'ù', 'î', '#', 'è', 'â', 'ô', 'û', 'ç' },
    // Portuguese/Spanish (100%)
    { 'ç', '$', '¡', 'á', 'é', 'í', 'ó', 'ú', '¿', 'ü', 'ñ', 'è', 'à' },
    // Turkish (50%: 7 missing)
    { '#', 'g', 'I', 'S', 'Ö', 'Ç', 'Ü', 'G', 'i', 's', 'ö', 'ç', 'ü' },
    // reserved (English mapping)
    { '£', '$', '@', '«', '½', '»', '¬', '#', '­', '¼', '¦', '¾', '÷' }
};
#define NATOPT_ALPHA_COUNT (sizeof(natOptChars) / 16)

/*
    // for latin-2 font
    // Polish (100%)
    { '#', 'ñ', '±', '¯', '¦', '£', 'æ', 'ó', 'ê', '¿', '¶', '³', '¼' },
    // German (100%)
    { '#', '$', '§', 'Ä', 'Ö', 'Ü', '^', '_', '°', 'ä', 'ö', 'ü', 'ß' },
    // Estonian (100%)
    { '#', 'õ', '©', 'Ä', 'Ö', '®', 'Ü', 'Õ', '¹', 'ä', 'ö', '¾', 'ü' },
    // Lettish/Lithuanian (90%)
    { '#', '$', '©', 'ë', 'ê', '®', 'è', 'ü', '¹', '±', 'u', '¾', 'i' },
    // French (90%)
    { 'é', 'i', 'a', 'ë', 'ì', 'u', 'î', '#', 'e', 'â', 'ô', 'u', 'ç' },
    // Serbian/Croation/Slovenian (100%)
    { '#', 'Ë', 'È', 'Æ', '®', 'Ð', '©', 'ë', 'è', 'æ', '®', 'ð', '¹' },
    // Czech/Slovak (100%)
    { '#', 'ù', 'è', '»', '¾', 'ý', 'í', 'ø', 'é', 'á', 'ì', 'ú', '¹' },
    // Rumanian (95%)
    { '#', '¢', 'Þ', 'Â', 'ª', 'Ã', 'Î', 'i', 'þ', 'â', 'º', 'ã', 'î' },
};
*/


typedef struct {
   char *g0;
   char *latin1;
   char *latin2;
} DIACRIT;

static const DIACRIT diacrits[16] =
{
    /* none */		{ "#",
    			  "¤",
			  "$"					},
    /* grave - ` */	{ " aeiouAEIOU",
    			  "`àèìòùÀÈÌÒÙ",
			  "`aeiouAEIOU"				},
    /* acute - ' */	{ " aceilnorsuyzACEILNORSUYZ",
			  "'ácéílnórsúýzÁCÉÍLNÓRSÚÝZ",
			  "'áæéíåñóà¶úý¼ÁÆÉÍÅÑÓÀ¦ÚÝ¬"		},
    /* cirumflex - ^ */	{ " aeiouAEIOU",
    			  "^âêîôûÂÊÎÔÛ",
			  "^âeîôuÂEÎÔU"				},
    /* tilde - ~ */	{ " anoANO",
    			  "~ãñõÃÑÕ",
			  "~anoANO"				},
    /* ??? - ¯ */	{ "",
    			  "",
			  ""					},
    /* breve - u */	{ "aA",
    			  "aA",
			  "ãÃ"					},
    /* abovedot - · */	{ "zZ",
    			  "zZ",
			  "¿¯"					},
    /* diaeresis ¨ */	{ "aeiouAEIOU",
    			  "äëïöüÄËÏÖÜ",
			  "äëiöüÄËIÖÜ"				},
    /* ??? - . */	{ "",
    			  "",
			  ""					},
    /* ringabove - ° */	{ " auAU",
    			  "°åuÅU",
			  "°aùAÙ"				},
    /* cedilla - ¸ */	{ "cstCST",
    			  "çstÇST",
			  "çºþÇªÞ"				},
    /* ??? - _ */	{ " ",
    			  "_",
			  "_"					},
    /* dbl acute - " */	{ " ouOU",
    			  "\"ouOU",
			  "\"õûÕÛ"				},
    /* ogonek - \, */	{ "aeAE",
    			  "aeAE",
			  "±ê¡Ê"				},
    /* caron - v */	{ "cdelnrstzCDELNRSTZ",
			  "cdelnrstzCDELNRSTZ",
			  "èïìµòø¹»¾ÈÏÌ¥ÒØ©«®"			},
};

static const char g2map_latin1[] =
   /*0123456789abcdef*/
    " ¡¢£$¥#§¤'\"«    "
    "°±²³×µ¶·÷'\"»¼½¾¿"
    " `´^~   ¨.°¸_\"  "
    "_¹®©            "
    " ÆÐªH ILLØ ºÞTNn"
    "Kædðhiillø ßþtn\x7f";

/*
static const char g2map_latin2[] =
   //0123456789abcdef
    " icL$Y#§¤'\"<    "
    "°   ×u  ÷'\">    "
    " `´^~ ¢ÿ¨.°¸_½²·"
    "- RC            "
    "  ÐaH iL£O opTNn"
    "K ðdhiil³o ßptn\x7f";
*/

// ----------------------------------------------------------------------------
// Return a de-nationalized character from the G0 set
// - the G0 charset is basically ASCII, but at a few places the ASCII chars
//   are replaced by special chars, depending on the country
// - in the following table -1 stands for ASCII chars, the other values
//   are to be taken as index in the national options table
// - note: declared as signed char to allow use of negative values; ANSI-C
//   does not specify if char is signed or unsigned by default
//
static const signed char nationalOptionsMatrix[0x80] =
{
   /*          0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F  */
   /*0x00*/   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   /*0x10*/   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   /*0x20*/   -1, -1, -1,  0,  1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   /*0x30*/   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   /*0x40*/    2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   /*0x50*/   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  3,  4,  5,  6,  7,
   /*0x60*/    8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   /*0x70*/   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  9, 10, 11, 12, -1
};

static uchar GetG0Char(uchar val, uchar alphabeth)
{
   uchar result;

   if ( (val < 0x80) && (nationalOptionsMatrix[val] >= 0) )
   {
      if (alphabeth < NATOPT_ALPHA_COUNT)
         result = natOptChars[alphabeth][ (uchar) nationalOptionsMatrix[val] ];
      else
         result = ' ';
   }
   else
      result = val;

   return result;
}

// ----------------------------------------------------------------------------
// Apply escape sequence to a string
// - only language-specific escapes and newlines are evaluated
// - at the same time white space (multiple blanks etc.) is compressed
// - newline (explicit and implicit) is also replaced by blank, except for two
//   subsequent newlines or a completely empty line (indicates paragraph break)
//
static uchar * ApplyEscapes(const uchar *pText, uint textLen, const uchar *pEscapes, uchar escCount, uchar netwop)
{
   uchar *pout, *po;
   uint  escIdx, nextEsc;
   uint  i, linePos, strLen;
   bool  lastWhite;
   uchar newLine;
   uchar data, alphabet;

   // get default alphabet for the actual netwop
   if (netwop < MAX_NETWOP_COUNT)
      alphabet = netwopAlphabets[netwop];
   else
      alphabet = providerAlphabet;

   // string length may grow due to newline escapes -> reserve some estimated extra space
   strLen = textLen + escCount + 20;

   pout = (uchar *) xmalloc(strLen + 1);
   if (pout != NULL)
   {
      po = pout;
      lastWhite = TRUE;  //suppress blank at start of line
      newLine = 0;
      linePos = 0;

      escIdx = 0;
      if (escCount > 0)
         nextEsc = pEscapes[0] | ((pEscapes[1] & 0x03) << 8);
      else
         nextEsc = 0xffff;

      for (i=0; i<textLen && strLen>0; i++)
      {
         if (pText[i] < 0x20)
            *po = ' ';
         else
            *po = GetG0Char(pText[i], alphabet);
      
         while (i == nextEsc)
         {
            data = pEscapes[2];
            switch (pEscapes[1] >> 2)
            {
               case 0x01:  // mosaic character: unsupported
                  *po = ' ';
                  break;
               case 0x08:  // change alphabet
                  if (data < NATOPT_ALPHA_COUNT)
                     alphabet = data;
                  else
                     debug1("Apply-Escapes: unsupported alphabet %d", alphabet);
                  break;
               case 0x09:  // G0 character
                  if ((data >= 0x20) && (data < 0x80))
                     *po = GetG0Char(data, alphabet);
                  else
                     *po = ' ';
                  break;
               case 0x0A:  // CR/NL
                  if (newLine == 1)
                  {  // current line empty -> insert paragraph break into output
                     if (lastWhite && (po > pout))
                     {  // overwrite blank at the end of the line
                        *(po - 1) = '\n';
                     }
                     else if (strLen > 1)
                     {
                        po[1] = *po;
                        *(po++) = '\n';
                        strLen -= 1;
                     }
                     else
                        debug0("Apply-Escapes: output string length exceeded");
                     // set flag to avoid inserting multiple newlines (i.e. generating empty lines)
                     newLine = 2;
                  }
                  else if (lastWhite == FALSE)
                  {
                     if (strLen > 1)
                     {  // the blank (instead of newline) is inserted in front of the current character
                        po[1] = *po;
                        *po++ = ' ';
                        strLen -= 1;
                     }
                     else
                        debug0("Apply-Escapes: output string length exceeded");
                  }
                  lastWhite = TRUE;
                  linePos = 0;
                  // set flag to indicate that the new line is still empty
                  if (newLine == 0)
                     newLine = 1;
                  break;
               case 0x0F:  // G2 character
                  if ((data >= 0x20) && (data < 0x80))
                     *po = g2map_latin1[data - 0x20];
                  else
                     *po = ' ';
                  break;
               case 0x10:  // diacritical mark
               case 0x11:
               case 0x12:
               case 0x13:
               case 0x14:
               case 0x15:
               case 0x16:
               case 0x17:
               case 0x18:
               case 0x19:
               case 0x1a:
               case 0x1b:
               case 0x1c:
               case 0x1d:
               case 0x1e:
               case 0x1f:
               {
                  uint c = (pEscapes[1]>>2)&0x0f;
                  const char *pd = strchr(diacrits[c].g0, *po);
                  if (pd != NULL)
                     *po = diacrits[c].latin1[pd - diacrits[c].g0];
                  break;
               }
               default:
                  break;
            }
            pEscapes += 3;
            escIdx   += 1;
            if (escIdx < escCount)
               nextEsc = pEscapes[0] | ((pEscapes[1] & 0x03) << 8);
            else
               nextEsc = 0xffff;
         }

         if (*po == ' ')
         {
            if (lastWhite == FALSE)
            {
               po++;
               strLen -= 1;
               lastWhite = TRUE;
            }
         }
         else
         {
            po++;
            strLen -= 1;
            lastWhite = FALSE;
            // set flag to indicate this line is not empty
            newLine = 0;
         }
         linePos += 1;

         if (linePos == 40)
         {
            if (newLine == 1)
            {  // current line empty -> insert paragraph break into output
               if (lastWhite && (po > pout))
               {  // overwrite blank at the end of the line
                  *(po - 1) = '\n';
               }
               else if (strLen > 0)
               {
                  *(po++) = '\n';
                  strLen -= 1;
               }
               else
                  debug0("Apply-Escapes: output string length exceeded");
               // set flag to avoid inserting multiple newlines (i.e. generating empty lines)
               newLine = 2;
            }
            else if (lastWhite == FALSE)
            {  // add blank after last character of line
               if (strLen > 0)
               {
                  *(po++) = ' ';
                  strLen -= 1;
               }
               else
                  debug0("Apply-Escapes: output string length exceeded");
            }
            if (newLine == 0)
               newLine = 1;
            lastWhite = TRUE;
            linePos = 0;
         }
      }

      ifdebug1((strLen == 0) && (i < textLen), "Apply-Escapes: output string length exceeded: %d bytes remaining", textLen - i);
      ifdebug6((escIdx < escCount), "Apply-Escapes: only %d of %d escapes applied: nextExc=%d textIdx=%d escType=0x%x data=0x%x", escIdx, escCount, nextEsc, i, pEscapes[1] >> 2, pEscapes[2]);

      if ( (po > pout) && (*(po - 1) == ' ') )
      {  // remove blank from end of line
         po -= 1;
      }
      *po = 0;
   }

   return pout;
}

// ----------------------------------------------------------------------------
// Decode descriptor loop for all block types
// - Bit start offsets:  PI,OI = 0;  NI = 4;  MI = 2
//
static DESCRIPTOR * DecodeDescriptorLoop( const uchar *psd, uchar count, uchar bitOff )
{
   DESCRIPTOR * pDescriptors;
   uint desc;

   if (count > 0)
   {
      pDescriptors = xmalloc(count * sizeof(DESCRIPTOR));
      if (pDescriptors != NULL)
      {
         for (desc=0; desc < count; desc++)
         {
            switch (bitOff)
            {
               case 0:
                  pDescriptors[desc].type   = psd[0] & 0x3f;
                  pDescriptors[desc].id     = (psd[0] >> 6) | ((psd[1] & 0x0f)<<2);
                  //pDescriptors[desc].eval = (psd[1] >> 4) | ((psd[2] & 0x0f)<<4);
                  break;

               case 2:
                  pDescriptors[desc].type   = psd[0] >> 2;
                  pDescriptors[desc].id     = psd[1] & 0x3f;
                  //pDescriptors[i].eval = (psd[1] >> 6) | ((psd[2] & 0x3f)<<2);
                  break;

               case 4:
                  pDescriptors[desc].type   = (psd[0] >> 4) | ((psd[1] & 0x03)<<4);
                  pDescriptors[desc].id     = psd[1] >> 2;
                  //pDescriptors[desc].eval = psd[2];
                  break;

               case 6:
                  pDescriptors[desc].type   = (psd[0] >> 6) | ((psd[1] & 0x0f)<<2);
                  pDescriptors[desc].id     = (psd[1] >> 4) | ((psd[2] & 0x03)<<4);
                  //pDescriptors[i].eval = (psd[2] >> 2) | ((psd[3] & 0x3f)<<2);
                  break;
            }
            psd   += (bitOff + 6 + 6 + 8) / 8;  // note: uses previous bitOff; psd must be set before bitOff is modified
            bitOff = (bitOff + 6 + 6 + 8) % 8;
         }
      }
   }
   else
   {
      pDescriptors = NULL;
   }

   return pDescriptors;
}

// ----------------------------------------------------------------------------
// Convert a PI block
//
EPGDB_BLOCK * EpgBlockConvertPi(const uchar *pCtrl, uint ctrlLen, uint strLen)
{
   EPGDB_BLOCK *pBlk;
   PI_BLOCK pi, *pPi;
   DESCRIPTOR *pDescriptors;
   const uchar *psd;
   int titleLen, shortInfoLen, longInfoLen;
   uchar *pTitle, *pShortInfo, *pLongInfo;
   uchar long_info_type;
   uint piLen, idx;
 
   psd = pCtrl;
   pi.block_no              = (psd[5]>>4) | (psd[6]<<4) | ((psd[7]&0x0f)<<12);
   pi.feature_flags         = (psd[7]>>4) | (psd[8]<<4);
   pi.netwop_no             = psd[9];
   SetStartAndStopTime(       psd[10] | (psd[11]<<8),
                              psd[12] | (psd[13]<<8),
                              psd[14] | (psd[15]<<8),
                              &pi);
   pi.pil                   = psd[16] | (psd[17]<<8) | ((psd[18]&0x0f)<<16);
   pi.parental_rating       = psd[18] >> 4;;
   pi.editorial_rating      = psd[19] & 0x07;
   pi.no_themes             = (psd[19]>>3) & 0x07;
   pi.no_sortcrit           = (psd[19]>>6) | ((psd[20]&1)<<2);
   pi.no_descriptors        = (psd[20]>>1)&0x3f;
   pi.background_reuse      = psd[20]>>7;
   psd += 21;

   for (idx=0; idx < pi.no_themes; idx++)
      pi.themes[idx] = *(psd++);
   for (idx=0; idx < pi.no_sortcrit; idx++)
      pi.sortcrits[idx] = *(psd++);

   pDescriptors = DecodeDescriptorLoop(psd, pi.no_descriptors, 0);
   psd += ((pi.no_descriptors * 5) + 1) / 2;
 
   titleLen = psd[1+psd[0]*3];
   pTitle = ApplyEscapes(pCtrl+ctrlLen+2, titleLen, psd+1, psd[0], pi.netwop_no);
   psd += 1 + psd[0] * 3 + 1;
 
   if (pi.background_reuse)
   {
      pi.background_ref     = psd[0] + (psd[1] << 8);
      pShortInfo = NULL;
      pLongInfo = NULL;
   }
   else
   {
      shortInfoLen = psd[1 + psd[0]*3];
      if (shortInfoLen > 0)
        pShortInfo = ApplyEscapes(pCtrl+ctrlLen+2+titleLen, shortInfoLen, psd+1, psd[0], pi.netwop_no);
      else
        pShortInfo = NULL;
      psd += psd[0] * 3 + 2;
 
      long_info_type = psd[0] & 0x7;
      psd += 1;
      switch (long_info_type)
      {
         case EPG_STR_TYPE_TRANSP_SHORT:
           longInfoLen = psd[psd[0]*3 + 1];
           break;
         case EPG_STR_TYPE_TRANSP_LONG:
           longInfoLen = psd[psd[0]*3 + 1] | ((psd[psd[0]*3 + 2] & 0x03) << 8);
           break;
         case EPG_STR_TYPE_TTX_STR:
         case EPG_STR_TYPE_TTX_RECT:
         case EPG_STR_TYPE_TTX_PAGE:
        default:
           // TTX references are unsupported
           longInfoLen = 0;
           break;
      }
      if (longInfoLen > 0)
        pLongInfo = ApplyEscapes(pCtrl+ctrlLen+2+titleLen+shortInfoLen, longInfoLen, psd+1, psd[0], pi.netwop_no);
      else
        pLongInfo = NULL;
   }

   // initialize values that are maintained elsewhere
   pi.block_no_in_ai = TRUE;
   pi.series_code    = 0;
 
   // concatenate the various parts of PI to a compound structure
   // 1st step: sum up the length & compute the offsets of each element from the start
   piLen = sizeof(PI_BLOCK);
   if (pTitle != NULL)
   {
      pi.off_title = piLen;
      piLen += strlen(pTitle) + 1;
   }
   else
   {  // no title: set at least a 0-Byte
      pi.off_title = piLen;
      piLen += 1;
   }
   if (pShortInfo != NULL)
   {
      pi.off_short_info = piLen;
      piLen += strlen(pShortInfo) + 1;
   }
   else
      pi.off_short_info = 0;
   if (pLongInfo != NULL)
   {
      pi.off_long_info = piLen;
      piLen += strlen(pLongInfo) + 1;
   }
   else
      pi.off_long_info = 0;
   if (pDescriptors != NULL)
   {
      pi.off_descriptors = piLen;
      piLen += pi.no_descriptors * sizeof(DESCRIPTOR);
   }
   else
   {
      pi.off_descriptors = 0;
      pi.no_descriptors = 0;
   }
 
   // 2nd step: copy elements one after each other, then free single elements
   pBlk = EpgBlockCreate(BLOCK_TYPE_PI, piLen);
   pPi = (PI_BLOCK *) &pBlk->blk.pi;  // remove const from pointer
   memcpy(pPi, &pi, sizeof(PI_BLOCK));
   if (pTitle != NULL)
   {
      memcpy((void *) PI_GET_TITLE(pPi), pTitle, strlen(pTitle) + 1);
      xfree(pTitle);
   }
   else
   {  // title string shall always be available -> set a 0-Byte
      ((char *) PI_GET_TITLE(pPi))[0] = 0;  // cast to remove const
   }
   if (pShortInfo != NULL)
   {
      memcpy((void *) PI_GET_SHORT_INFO(pPi), pShortInfo, strlen(pShortInfo) + 1);
      xfree(pShortInfo);
   }
   if (pLongInfo != NULL)
   {
      memcpy((void *) PI_GET_LONG_INFO(pPi), pLongInfo, strlen(pLongInfo) + 1);
      xfree(pLongInfo);
   }
   if (pDescriptors != NULL)
   {
      memcpy((void *) PI_GET_DESCRIPTORS(pPi), pDescriptors, pi.no_descriptors * sizeof(DESCRIPTOR));
      xfree(pDescriptors);
   }
  
   return pBlk;
}

// ---------------------------------------------------------------------------
// Check reloaded PI block for gross consistancy errors
//
static bool EpgBlockCheckPi( EPGDB_BLOCK * pBlock )
{
   bool result = FALSE;
   const PI_BLOCK * pPi;

   pPi = &pBlock->blk.pi;

   if (pPi->netwop_no >= MAX_NETWOP_COUNT)
   {
      debug1("EpgBlock-CheckPi: illegal netwop %d", pPi->netwop_no);
   }
   else if (pPi->no_themes > PI_MAX_THEME_COUNT)
   {
      debug1("EpgBlock-CheckPi: illegal theme count %d", pPi->no_themes);
   }
   else if (pPi->no_sortcrit > PI_MAX_SORTCRIT_COUNT)
   {
      debug1("EpgBlock-CheckPi: illegal sortcrit count %d", pPi->no_sortcrit);
   }
   else if (pPi->off_title != sizeof(PI_BLOCK))
   {
      debug1("EpgBlock-CheckPi: illegal off_title=%d", pPi->off_title);
   }
   else if (pPi->stop_time < pPi->start_time)
   {
      // note: stop == start is allowed for "defective" blocks
      // but stop < start is never possible because the duration is transmitted in Nextview
      debug2("EpgBlock-CheckPi: illegal start/stop times: %ld, %ld", pPi->start_time, pPi->stop_time);
   }
   else if ( PI_HAS_SHORT_INFO(pPi) &&
             ( (pPi->off_short_info <= pPi->off_title) ||
               (pPi->off_short_info >= pBlock->size) ||
               // check if the title string is terminated by a null byte
               (*(PI_GET_SHORT_INFO(pPi) - 1) != 0) ))
   {
      debug2("EpgBlock-CheckPi: short info exceeds block size: off=%d, size=%d", pPi->off_short_info, pBlock->size + BLK_UNION_OFF);
   }
   else if ( PI_HAS_LONG_INFO(pPi) &&
             ( (pPi->off_long_info <= pPi->off_short_info) ||
               (pPi->off_long_info <= pPi->off_title) ||
               (pPi->off_long_info >= pBlock->size) ||
               // check if the title or short info string is terminated by a null byte
               (*(PI_GET_LONG_INFO(pPi) - 1) != 0) ))
   {
      debug2("EpgBlock-CheckPi: short info exceeds block size: off=%d, size=%d", pPi->off_long_info, pBlock->size + BLK_UNION_OFF);
   }
   else if ( (pPi->no_descriptors > 0) &&
             ( (pPi->off_descriptors <= pPi->off_long_info) ||
               (pPi->off_descriptors <= pPi->off_short_info) ||
               (pPi->off_descriptors <= pPi->off_title) ||
               (pPi->off_descriptors + (pPi->no_descriptors * sizeof(DESCRIPTOR)) != pBlock->size) ||
               // check if the title or short or long info string is terminated by a null byte
               (*(((uchar *)PI_GET_DESCRIPTORS(pPi)) - 1) != 0) ))
   {
      debug3("EpgBlock-CheckPi: descriptor count %d exceeds block length: off=%d, size=%d", pPi->no_descriptors, pPi->off_descriptors, pBlock->size + BLK_UNION_OFF);
   }
   else if ( (pPi->no_descriptors == 0) &&
             (*((uchar *) pBlock + pBlock->size + BLK_UNION_OFF - 1) != 0) )
   {
      debug0("EpgBlock-CheckPi: last string not terminated by 0 byte");
   }
   else
      result = TRUE;

   return result;
}

// ----------------------------------------------------------------------------
// Swap all PI block struct elements > 1 byte for non-endian-matching databases
// - the swap is done in-place
//
static bool EpgBlockSwapPi( EPGDB_BLOCK * pBlock )
{
   PI_BLOCK * pPi;

   pPi = (PI_BLOCK *) &pBlock->blk.pi;

   swap16(&pPi->block_no);
   swap32(&pPi->start_time);
   swap32(&pPi->stop_time);
   swap32(&pPi->pil);
   swap32(&pPi->series_code);
   swap16(&pPi->feature_flags);
   swap16(&pPi->background_ref);

   swap16(&pPi->off_title);
   swap16(&pPi->off_short_info);
   swap16(&pPi->off_long_info);
   swap16(&pPi->off_descriptors);

   return TRUE;
}

// ----------------------------------------------------------------------------
// Convert an AI block
//
EPGDB_BLOCK * EpgBlockConvertAi(const uchar *pCtrl, uint ctrlLen, uint strLen)
{
   EPGDB_BLOCK *pBlk;
   AI_BLOCK ai, *pAi;
   AI_NETWOP *pNetwops;
   uchar i;
   uchar netNameLen[MAX_NETWOP_COUNT];
   uint  serviceNameLen;
   uint  netNameLenSum, blockLen;
   const uchar *psd, *pst;

   psd = pCtrl;
   ai.version        = ((psd[5]>>4)|(psd[6]<<4)) & 0x3f;
   ai.version_swo    = (psd[6]>>2) & 0x3f;
   ai.niCount        = psd[7+ 0] | (psd[7+ 1] << 8);
   ai.oiCount        = psd[7+ 2] | (psd[7+ 3] << 8);
   ai.miCount        = psd[7+ 4] | (psd[7+ 5] << 8);
   ai.niCountSwo     = psd[7+ 6] | (psd[7+ 7] << 8);
   ai.oiCountSwo     = psd[7+ 8] | (psd[7+ 9] << 8);
   ai.miCountSwo     = psd[7+10] | (psd[7+11] << 8);
   ai.netwopCount    = psd[7+12];
   ai.thisNetwop     = psd[7+13];
   serviceNameLen    = psd[7+14] & 0x1f;
   psd += 7 + 15;
   pst = pCtrl + ctrlLen + 2;

   pNetwops = xmalloc(ai.netwopCount * sizeof(AI_NETWOP));
   netNameLenSum = 0;
   for (i=0; i<ai.netwopCount; i++)
   {
      if ((i & 1) == 0)
      {
         pNetwops[i].cni         = psd[0] | (psd[1] << 8);
         pNetwops[i].lto         = psd[2];
         pNetwops[i].dayCount    = psd[3] & 0x1f;
         netNameLen[i]           = (psd[3] >> 5) | ((psd[4] & 3) << 3);
         pNetwops[i].alphabet    = (psd[4] >> 2) | ((psd[5] & 1) << 6);
         pNetwops[i].startNo     = (psd[5] >> 1) | (psd[6] << 7) | ((psd[7] & 1) << 15);
         pNetwops[i].stopNo      = (psd[7] >> 1) | (psd[8] << 7) | ((psd[9] & 1) << 15);
         pNetwops[i].stopNoSwo   = (psd[9] >> 1) | (psd[10] << 7) | ((psd[11] & 1) << 15);
         pNetwops[i].addInfo     = (psd[11] >> 1) | ((psd[12] & 0xf) << 7);
         psd += 12;  // plus 4 Bit in the following block
      }
      else
      {
         pNetwops[i].cni         = (psd[0] >> 4) | (psd[1] << 4) | ((psd[2] & 0x0f) << 12);
         pNetwops[i].lto         = (psd[2] >> 4) | (psd[3] << 4);
         pNetwops[i].dayCount    = (psd[3] >> 4) | ((psd[4] & 1) << 4);
         netNameLen[i]           = (psd[4] >> 1) & 0x1f;
         pNetwops[i].alphabet    = (psd[4] >> 6) | ((psd[5] & 0x1f) << 2);
         pNetwops[i].startNo     = (psd[5] >> 5) | (psd[6] << 3) | ((psd[7] & 0x1f) << 11);
         pNetwops[i].stopNo      = (psd[7] >> 5) | (psd[8] << 3) | ((psd[9] & 0x1f) << 11);
         pNetwops[i].stopNoSwo   = (psd[9] >> 5) | (psd[10] << 3) | ((psd[11] & 0x1f) << 11);
         pNetwops[i].addInfo     = (psd[11] >> 5) | (psd[12] << 3);
         psd += 13;  // including 4 bit of the previous block
      }
      // initialize unused space in struct to allow comparison by memcmp()
      pNetwops[i].reserved_1 = 0;
      netNameLenSum += netNameLen[i] + 1;
   }

   // concatenate the various parts of the block to a compound structure
   pst = pCtrl + ctrlLen + 2;
   blockLen = sizeof(AI_BLOCK) +
              (ai.netwopCount * sizeof(AI_NETWOP)) +
              (serviceNameLen + 1) +
              netNameLenSum;
   pBlk = EpgBlockCreate(BLOCK_TYPE_AI, blockLen);
   pAi = (AI_BLOCK *) &pBlk->blk.ai;  // remove const from pointer
   memcpy(pAi, &ai, sizeof(AI_BLOCK));
   blockLen = sizeof(AI_BLOCK);

   pAi->off_netwops = blockLen;
   memcpy((void *) AI_GET_NETWOPS(pAi), pNetwops, ai.netwopCount * sizeof(AI_NETWOP));
   blockLen += ai.netwopCount * sizeof(AI_NETWOP);
   xfree(pNetwops);

   pAi->off_serviceNameStr = blockLen;
   memcpy((void *) AI_GET_SERVICENAME(pAi), pst, serviceNameLen);
   *((uchar *)pAi + blockLen + serviceNameLen) = 0;
   blockLen += serviceNameLen + 1;
   pst += serviceNameLen;

   pNetwops = (AI_NETWOP *) AI_GET_NETWOPS(pAi);  // cast to remove const
   for (i=0; i < ai.netwopCount; i++, pNetwops++)
   {
      pNetwops->off_name = blockLen;
      memcpy((char *) pAi + blockLen, pst, netNameLen[i]);
      *((uchar *)pAi + blockLen + netNameLen[i]) = 0;
      blockLen += netNameLen[i] + 1;
      pst += netNameLen[i];
   }
   assert(blockLen == pBlk->size);

   // update the alphabet list for string decoding
   EpgBlockSetAlphabets(&pBlk->blk.ai);

   return pBlk;
}

// ---------------------------------------------------------------------------
// Check an AI block for consistancy errors in counters, offsets or value ranges
//
static bool EpgBlockCheckAi( EPGDB_BLOCK * pBlock )
{
   const AI_BLOCK  * pAi;
   const AI_NETWOP * pNetwop;
   const uchar *pBlockEnd, *pName, *pPrevName;
   uchar netwop;
   bool result = FALSE;

   pAi       = &pBlock->blk.ai;
   pBlockEnd = (uchar *) pBlock + pBlock->size + BLK_UNION_OFF;

   if ((pAi->netwopCount == 0) || (pAi->netwopCount > MAX_NETWOP_COUNT))
   {
      debug1("EpgBlock-CheckAi: illegal netwop count %d", pAi->netwopCount);
   }
   else if (pAi->thisNetwop >= pAi->netwopCount)
   {
      debug2("EpgBlock-CheckAi: this netwop %d >= count %d", pAi->thisNetwop, pAi->netwopCount);
   }
   else if (pAi->off_netwops != sizeof(AI_BLOCK))
   {
      debug1("EpgBlock-CheckAi: off_netwops=%d illegal", pAi->off_netwops);
   }
   else if (pAi->off_serviceNameStr != sizeof(AI_BLOCK) + (pAi->netwopCount * sizeof(AI_NETWOP)))
   {
      debug1("EpgBlock-CheckAi: off_serviceNameStr=%d illegal", pAi->off_serviceNameStr);
   }
   else if (pAi->off_serviceNameStr >= pBlock->size)
   {
      // note: this check implies the check for netwop list end > block size
      debug3("EpgBlock-CheckAi: service name off=%d or netwop list (count %d) exceeds block length %d", pAi->off_serviceNameStr, pAi->netwopCount, pBlock->size);
   }
   else
   {
      result = TRUE;

      // check the name string offsets the netwop array
      pNetwop   = AI_GET_NETWOPS(pAi);
      pPrevName = AI_GET_SERVICENAME(pAi);
      for (netwop=0; netwop < pAi->netwopCount; netwop++, pNetwop++)
      {
         pName = AI_GET_STR_BY_OFF(pAi, pNetwop->off_name);
         if ( (pName <= pPrevName) ||
              (pName >= pBlockEnd) ||
              (*(pName - 1) != 0) )
         {
            debug2("EpgBlock-CheckAi: netwop name #%d has illegal offset %d", netwop, pNetwop->off_name);
            result = FALSE;
            break;
         }
         pPrevName = pName;
      }

      if (result && (*(pBlockEnd - 1) != 0))
      {
        debug0("EpgBlock-CheckAi: last netwop name not 0 terminated");
        result = FALSE;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Swap all AI block struct elements > 1 byte for non-endian-matching databases
// - called before consistancy check, hence used offsets and counters must be checked
// - the swap is done in-place
//
static bool EpgBlockSwapAi( EPGDB_BLOCK * pBlock )
{
   AI_BLOCK  * pAi;
   AI_NETWOP * pNetwop;
   uchar netwop;
   bool  result = FALSE;

   pAi = (AI_BLOCK *) &pBlock->blk.ai;

   swap16(&pAi->niCount);
   swap16(&pAi->oiCount);
   swap16(&pAi->miCount);
   swap16(&pAi->niCountSwo);
   swap16(&pAi->oiCountSwo);
   swap16(&pAi->miCountSwo);

   swap16(&pAi->off_serviceNameStr);
   swap16(&pAi->off_netwops);

   if ( (pAi->off_netwops == sizeof(AI_BLOCK)) &&
        (pAi->netwopCount > 0) && (pAi->netwopCount <= MAX_NETWOP_COUNT) &&
        (sizeof(AI_BLOCK) + (pAi->netwopCount * sizeof(AI_NETWOP)) <= pBlock->size) )
   {
      pNetwop = (AI_NETWOP *) AI_GET_NETWOPS(pAi);
      for (netwop=0; netwop < pAi->netwopCount; netwop++, pNetwop++)
      {
         swap16(&pNetwop->cni);
         swap16(&pNetwop->startNo);
         swap16(&pNetwop->stopNo);
         swap16(&pNetwop->stopNoSwo);
         swap16(&pNetwop->addInfo);

         swap16(&pNetwop->off_name);
      }
      result = TRUE;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Convert an OI block
//
EPGDB_BLOCK * EpgBlockConvertOi(const uchar *pCtrl, uint ctrlLen, uint strLen)
{
   EPGDB_BLOCK *pBlk;
   OI_BLOCK oi, *pOi;
   DESCRIPTOR *pDescriptors;
   const uchar *psd, *pst;
   uchar *pHeader, *pMessage;
   uint msgLen, headerLen, blockLen;
 
   psd = pCtrl;
   pst = pCtrl + ctrlLen + 2;
   oi.block_no        = (psd[5]>>4) | (psd[6]<<4) | ((psd[7]&0x0f)<<12);
   oi.msg_attrib      = (psd[7]>>4) | ((psd[8]&0x0f)<<4);
   oi.header_size     = (psd[8]>>4)&0x07;
   oi.msg_size        = (psd[8]>>7) | ((psd[9]&3)<<1);
   oi.no_descriptors  = psd[9]>>2;
   psd += 10;
 
   if (oi.block_no == 0)
   {
      const uchar *pEsc = psd;
      msgLen = psd[psd[0]*3 + 1] + ((psd[psd[0]*3 + 2] & 0x03)<<8);
      psd += psd[0] * 3 + 1 + 2;
      headerLen = psd[psd[0]*3 + 1];
      pMessage = ApplyEscapes(pst + headerLen, msgLen, pEsc+1, pEsc[0], 0xff);
   }
   else
   {
      pMessage = NULL;
   }

   headerLen = psd[psd[0]*3 + 1];
   pHeader = ApplyEscapes(pst, headerLen, psd+1, psd[0], 0xff);
   psd += psd[0] * 3 + 2;

   pDescriptors = DecodeDescriptorLoop(psd, oi.no_descriptors, 0);

   // concatenate the various parts of the block to a compound structure
   // 1st step: sum up the length & compute the offsets of each element from the start
   blockLen = sizeof(OI_BLOCK);
   if (pHeader != NULL)
   {
      oi.off_header = blockLen;
      blockLen += strlen(pHeader) + 1;
   }
   else
      oi.off_header = 0;
   if (pMessage != NULL)
   {
      oi.off_message = blockLen;
      blockLen += strlen(pMessage) + 1;
   }
   else
      oi.off_message = 0;
   if (pDescriptors != NULL)
   {
      oi.off_descriptors = blockLen;
      blockLen += oi.no_descriptors * sizeof(DESCRIPTOR);
   }
   else
   {
      oi.off_descriptors = 0;
      oi.no_descriptors = 0;
   }

   // 2nd step: copy elements one after each other, then free single elements
   pBlk = EpgBlockCreate(BLOCK_TYPE_OI, blockLen);
   pOi = (OI_BLOCK *) &pBlk->blk.oi;
   memcpy(pOi, &oi, sizeof(OI_BLOCK));
   if (pHeader != NULL)
   {
      memcpy((void *) OI_GET_HEADER(pOi), pHeader, strlen(pHeader) + 1);
      xfree(pHeader);
   }
   if (pMessage != NULL)
   {
      memcpy((void *) OI_GET_MESSAGE(pOi), pMessage, strlen(pMessage) + 1);
      xfree(pMessage);
   }
   if (pDescriptors != NULL)
   {
      memcpy((void *) OI_GET_DESCRIPTORS(pOi), pDescriptors, oi.no_descriptors * sizeof(DESCRIPTOR));
      xfree(pDescriptors);
   }

   return(pBlk);
}

// ---------------------------------------------------------------------------
// Check an OI block for consistancy errors in counters, offsets or value ranges
//
static bool EpgBlockCheckOi( EPGDB_BLOCK * pBlock )
{
   const OI_BLOCK * pOi;
   bool result = FALSE;

   pOi = &pBlock->blk.oi;

   if (OI_HAS_HEADER(pOi) && (pOi->off_header != sizeof(OI_BLOCK)))
   {
      debug1("EpgBlock-CheckOi: illegal off_header=%d", pOi->off_header);
   }
   else if ( OI_HAS_MESSAGE(pOi) &&
             ((pOi->off_message <= pOi->off_header) || (pOi->off_header >= pBlock->size)) )
   {
      debug2("EpgBlock-CheckOi: message exceeds block size: off=%d, size=%d", pOi->off_message, pBlock->size + BLK_UNION_OFF);
   }
   else if ( (pOi->no_descriptors > 0) &&
             ( (pOi->off_descriptors <= pOi->off_header) ||
               (pOi->off_descriptors <= pOi->off_message) ||
               (pOi->off_descriptors + (pOi->no_descriptors * sizeof(DESCRIPTOR)) != pBlock->size) ))
   {
      debug3("EpgBlock-CheckOi: descriptor count %d exceeds block length: off=%d, size=%d", pOi->no_descriptors, pOi->off_descriptors, pBlock->size + BLK_UNION_OFF);
   }
   else
      result = TRUE;

   return result;
}

// ----------------------------------------------------------------------------
// Swap all OI block struct elements > 1 byte for non-endian-matching databases
// - the swap is done in-place
//
static bool EpgBlockSwapOi( EPGDB_BLOCK * pBlock )
{
   OI_BLOCK * pOi;

   pOi = (OI_BLOCK *) &pBlock->blk.oi;

   swap16(&pOi->block_no);

   swap16(&pOi->off_header);
   swap16(&pOi->off_message);
   swap16(&pOi->off_descriptors);

   return TRUE;
}

// ----------------------------------------------------------------------------
// Convert a NI block
//
EPGDB_BLOCK * EpgBlockConvertNi(const uchar *pCtrl, uint ctrlLen, uint strLen)
{
   EPGDB_BLOCK *pBlk;
   NI_BLOCK ni;
   const NI_BLOCK *pNi;
   uchar *tmp_evstr[NI_MAX_EVENT_COUNT];
   DESCRIPTOR *pDescriptors;
   EVENT_ATTRIB ev[NI_MAX_EVENT_COUNT];
   const uchar *psd, *pStr;
   uchar *pHeader;
   uint i, j, len, blockLen;

   psd = pCtrl;
   pStr = pCtrl+ctrlLen+2;
   ni.block_no        = (psd[5]>>4) | (psd[6]<<4) | ((psd[7]&0x0f)<<12);
   ni.header_size     = (psd[7]>>4)&0x03;
   ni.no_events       = ((psd[7]>>6)&0x03) | ((psd[8]&0x03)<<2);
   ni.msg_size        = (psd[8]>>2)&0x07;
   ni.no_descriptors  = (psd[8]>>6) | (psd[9]&0x0f);
   psd += 9;
   pDescriptors = DecodeDescriptorLoop(psd, ni.no_descriptors, 4);
   psd += 1 + ni.no_descriptors * 5 / 2;

   len = psd[psd[0]*3 + 1];
   pHeader = ApplyEscapes(pStr, len, psd+1, psd[0], 0xff);
   psd += psd[0] * 3 + 2;
   pStr += len;

   ni.msg_attrib = psd[0];
   psd += 1;

   for (i=0; i<ni.no_events; i++)
   {
      ev[i].next_id      = psd[0] | (psd[1] << 8);
      ev[i].next_type    = psd[2] & 0x0f;
      ev[i].no_attribs   = psd[2] >> 4;
      psd += 3;
      for (j=0; j<ev[i].no_attribs; j++)
      {
         ev[i].unit[j].kind = psd[0];
         if (ev[i].unit[j].kind <= 0x7F)
         {
            ev[i].unit[j].data = psd[1];
            psd += 1+1;
         }
         else if (ev[i].unit[j].kind <= 0xBF)
         {
            ev[i].unit[j].data = psd[1] | (psd[2]<<8);
            psd += 1+2;
         }
         else if (ev[i].unit[j].kind <= 0xDF)
         {
            ev[i].unit[j].data = psd[1] | (psd[2]<<8) | (psd[3]<<16);
            psd += 1+3;
         }
         else
         {
            ev[i].unit[j].data = psd[1] | (psd[2]<<8) | (psd[3]<<16) | (psd[4]<<24);
            psd += 1+4;
         }
      }

      len = psd[psd[0]*3 + 1];
      tmp_evstr[i] = ApplyEscapes(pStr, len, psd+1, psd[0], 0xff);
      psd += psd[0] * 3 + 2;
      pStr += len;
   }

   // concatenate the various parts of the block to a compound structure
   // 1st step: sum up the length & compute the offsets of each element from the start
   blockLen = sizeof(NI_BLOCK);
   if (ni.no_events > 0)
      ni.off_events = ((blockLen + 3) & ~3);  // requires alignment for 32-bit element in struct
   else
      ni.off_events = 0;
   blockLen += ni.no_events * sizeof(EVENT_ATTRIB);
   if (pHeader != NULL)
   {
      ni.off_header = blockLen;
      blockLen += strlen(pHeader) + 1;
   }
   else
      ni.off_header = 0;
   if (pDescriptors != NULL)
   {
      ni.off_descriptors = blockLen;
      blockLen += ni.no_descriptors * sizeof(DESCRIPTOR);
   }
   else
      ni.off_descriptors = 0;
   for (i=0; i<ni.no_events; i++)
   {
      if (tmp_evstr[i] != NULL)
      {
         ev[i].off_evstr = blockLen;
         blockLen += strlen(tmp_evstr[i]) + 1;
      }
      else
         ev[i].off_evstr = 0;
   }

   // 2nd step: copy elements one after each other, then free single elements
   pBlk = EpgBlockCreate(BLOCK_TYPE_NI, blockLen);
   pNi = &pBlk->blk.ni;
   memcpy((void *) pNi, &ni, sizeof(NI_BLOCK));
   if (pNi->no_events > 0)
   {
      memcpy((void *) NI_GET_EVENTS(pNi), ev, ni.no_events * sizeof(EVENT_ATTRIB));
   }
   if (pHeader != NULL)
   {
      memcpy((void *) NI_GET_HEADER(pNi), pHeader, strlen(pHeader) + 1);
      xfree(pHeader);
   }
   if (pDescriptors != NULL)
   {
      memcpy((void *) NI_GET_DESCRIPTORS(pNi), pDescriptors, ni.no_descriptors * sizeof(DESCRIPTOR));
      xfree(pDescriptors);
   }
   for (i=0; i<ni.no_events; i++)
   {
      if (tmp_evstr[i] != NULL)
      {
         memcpy((void *) NI_GET_EVENT_STR(pNi, &ev[i]), tmp_evstr[i], strlen(tmp_evstr[i]) + 1);
         xfree(tmp_evstr[i]);
      }
   } 

   return(pBlk);
}

// ---------------------------------------------------------------------------
// Check a NI block for consistancy errors in counters, offsets or value ranges
//
static bool EpgBlockCheckNi( EPGDB_BLOCK * pBlock )
{
   bool result = FALSE;
   const NI_BLOCK * pNi;
   const EVENT_ATTRIB * pEv;
   const uchar * pBlockEnd;
   uint  ev_idx;

   pNi       = &pBlock->blk.ni;
   pBlockEnd = (uchar *) pBlock + pBlock->size + BLK_UNION_OFF;

   if (pNi->no_events > NI_MAX_EVENT_COUNT)
   {
      debug1("EpgBlock-CheckNi: too many events: %d", pNi->no_events);
   }
   else if ( (pNi->no_events != 0) && 
             ( (pNi->off_events == 0) ||
               (((uchar *)&NI_GET_EVENTS(pNi)[pNi->no_events]) > pBlockEnd) ))
   {
      debug3("EpgBlock-CheckNi: events array missing (expected %d) or exceeds block size: off=%d, size=%d", pNi->no_events, pNi->off_events, pBlock->size + BLK_UNION_OFF);
   }
   else if (NI_HAS_HEADER(pNi) && (NI_GET_HEADER(pNi) >= pBlockEnd))
   {
      debug2("EpgBlock-CheckNi: header exceeds block size: off=%d, size=%d", pNi->off_header, pBlock->size + BLK_UNION_OFF);
   }
   else if ( (pNi->no_descriptors > 0) &&
             ( (pNi->off_descriptors == 0) ||
               (((uchar *)&NI_GET_DESCRIPTORS(pNi)[pNi->no_descriptors]) > pBlockEnd) ))
   {
      debug3("EpgBlock-CheckNi: descriptor count %d exceeds block length: off=%d, size=%d", pNi->no_descriptors, pNi->off_descriptors, pBlock->size + BLK_UNION_OFF);
   }
   else
   {
      result = TRUE;

      if (pNi->off_events != 0)
      {
         pEv = NI_GET_EVENTS(pNi);
         for (ev_idx=0; ev_idx < pNi->no_events; ev_idx++, pEv++)
         {
            if (pEv->no_attribs > NI_MAX_ATTRIB_COUNT)
            {
               debug2("EpgBlock-CheckNi: too many attribs: %d in ev %d", pEv->no_attribs, ev_idx);
               result = FALSE;
               break;
            }
            else if ((pEv->off_evstr != 0) && (NI_GET_EVENT_STR(pNi, pEv) >= pBlockEnd))
            {
               debug3("EpgBlock-CheckNi: event str %d exceeds block length: off=%d, size=%d", ev_idx, pEv->off_evstr, pBlock->size + BLK_UNION_OFF);
               result = FALSE;
               break;
            }
         }
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Swap all NI block struct elements > 1 byte for non-endian-matching databases
// - called before consistancy check, hence used offsets and counters must be checked
// - the swap is done in-place
//
static bool EpgBlockSwapNi( EPGDB_BLOCK * pBlock )
{
   NI_BLOCK       * pNi;
   EVENT_ATTRIB   * pEv;
   EV_ATTRIB_DATA * pEvUnit;
   uint  ev_idx;
   uint  att_idx;
   bool  result = FALSE;

   pNi = (NI_BLOCK *) &pBlock->blk.ni;

   swap16(&pNi->block_no);
   swap16(&pNi->msg_attrib);

   swap16(&pNi->off_events);
   swap16(&pNi->off_header);
   swap16(&pNi->off_descriptors);

   if (pNi->off_events != 0)
   {
      if ( (pNi->no_events <= NI_MAX_EVENT_COUNT) &&
           (pNi->off_events + (pNi->no_events * sizeof(EVENT_ATTRIB)) <= pBlock->size) )
      {
         pEv = (EVENT_ATTRIB *) NI_GET_EVENTS(pNi);
         result = TRUE;

         for (ev_idx=0; ev_idx < pNi->no_events; ev_idx++, pEv++)
         {
            swap16(&pEv->next_id);
            swap16(&pEv->off_evstr);

            if (pEv->no_attribs <= NI_MAX_ATTRIB_COUNT)
            {
               pEvUnit = pEv->unit;
               for (att_idx=0; att_idx < pEv->no_attribs; att_idx++, pEvUnit++)
               {
                  swap32(&pEvUnit->data);
               }
            }
            else
            {
               result = FALSE;
               break;
            }
         }
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Convert a MI block
//
EPGDB_BLOCK * EpgBlockConvertMi(const uchar *pCtrl, uint ctrlLen, uint strLen)
{
   EPGDB_BLOCK *pBlk;
   MI_BLOCK mi;
   const MI_BLOCK *pMi;
   DESCRIPTOR *pDescriptors;
   const uchar *psd, *pStr;
   uchar *pMessage;
   uint  len, blockLen;

   psd = pCtrl;
   pStr = pCtrl+ctrlLen+2;
   mi.block_no        = (psd[5]>>4) | (psd[6]<<4) | ((psd[7]&0x0f)<<12);
   mi.no_descriptors  = (psd[7]>>4) | (psd[8]&0x03);
   psd += 8;
   pDescriptors = DecodeDescriptorLoop(psd, mi.no_descriptors, 2);
   psd += 1 + mi.no_descriptors * 5 / 2;

   len = psd[psd[0]*3 + 1] | ((psd[psd[0]*3 + 2] & 0x03)<<8);
   pMessage = ApplyEscapes(pStr, len, psd+1, psd[0], 0xff);
   psd += psd[0] * 3 + 2;
   pStr += len;

   // concatenate the various parts of the block to a compound structure
   // 1st step: sum up the length & compute the offsets of each element from the start
   blockLen = sizeof(MI_BLOCK);
   if (pMessage != NULL)
   {
      mi.off_message = blockLen;
      blockLen += strlen(pMessage) + 1;
   }
   else
      mi.off_message = 0;
   if (pDescriptors != NULL)
   {
      mi.off_descriptors = blockLen;
      blockLen += mi.no_descriptors * sizeof(DESCRIPTOR);
   }
   else
      mi.off_descriptors = 0;

   // 2nd step: copy elements one after each other, then free single elements
   pBlk = EpgBlockCreate(BLOCK_TYPE_MI, blockLen);
   pMi = &pBlk->blk.mi;
   memcpy((void *) pMi, &mi, sizeof(MI_BLOCK));
   if (pMessage != NULL)
   {
      memcpy((void *) MI_GET_MESSAGE(pMi), pMessage, strlen(pMessage) + 1);
      xfree(pMessage);
   }
   if (pDescriptors != NULL)
   {
      memcpy((void *) MI_GET_DESCRIPTORS(pMi), pDescriptors, mi.no_descriptors * sizeof(DESCRIPTOR));
      xfree(pDescriptors);
   }

   return(pBlk);
}

// ---------------------------------------------------------------------------
// Check a MI block for consistancy errors in counters, offsets or value ranges
//
static bool EpgBlockCheckMi( EPGDB_BLOCK * pBlock )
{
   bool result = FALSE;
   const MI_BLOCK * pMi;

   pMi = &pBlock->blk.mi;

   if (MI_HAS_MESSAGE(pMi) && (pMi->off_message != sizeof(MI_BLOCK)))
   {
      debug1("EpgBlock-CheckMi: illegal off_message=%d", pMi->off_message);
   }
   else if ( (pMi->no_descriptors > 0) &&
             ( (pMi->off_descriptors <= pMi->off_message) ||
               (pMi->off_descriptors + (pMi->no_descriptors * sizeof(DESCRIPTOR)) != pBlock->size) ))
   {
      debug3("EpgBlock-CheckMi: descriptor count %d exceeds block length: off=%d, size=%d", pMi->no_descriptors, pMi->off_descriptors, pBlock->size + BLK_UNION_OFF);
   }
   else
      result = TRUE;

   return result;
}

// ----------------------------------------------------------------------------
// Swap all MI block struct elements > 1 byte for non-endian-matching databases
// - called before consistancy check, hence used offsets and counters must be checked
// - the swap is done in-place
//
static bool EpgBlockSwapMi( EPGDB_BLOCK * pBlock )
{
   MI_BLOCK * pMi;

   pMi = (MI_BLOCK *) &pBlock->blk.mi;

   swap16(&pMi->block_no);

   swap16(&pMi->off_message);
   swap16(&pMi->off_descriptors);

   return TRUE;
}

// ----------------------------------------------------------------------------
// Convert a LI block
//
EPGDB_BLOCK * EpgBlockConvertLi(const uchar *pCtrl, uint ctrlLen, uint strLen)
{
   EPGDB_BLOCK *pBlk;
   LI_BLOCK li;
   const LI_BLOCK *pLi;
   LI_DESC ld[LI_MAX_DESC_COUNT];
   const uchar *psd;
   uint  desc, lang, bitOff, blockLen;

   psd = pCtrl;
   li.block_no        = (psd[5]>>4) | (psd[6]<<4) | ((psd[7]&0x0f)<<12);
   li.netwop_no       = (psd[7]>>4) | (psd[8]&0x0f);
   li.desc_no         = (psd[8]>>4) | (psd[9]&0x03);
   psd += 9;
   bitOff = 2;

   for (desc=0; desc < li.desc_no;desc++)
   {
      switch (bitOff)
      {
         case 0:
            ld[desc].id         = psd[0] & 0x3f;
            ld[desc].lang_count = (psd[0] >> 6) | ((psd[1] & 0x03) << 2);
            break;

         case 2:
            ld[desc].id         = psd[0] >> 2;
            ld[desc].lang_count = psd[1] & 0x0F;
            break;

         case 4:
            ld[desc].id         = (psd[0] >> 4) | ((psd [1] & 0x03) << 4);
            ld[desc].lang_count = (psd[1] >> 2) & 0x0f;
            break;

         case 6:
            ld[desc].id         = (psd[0] >> 6) | ((psd [1] & 0x0f) << 2);
            ld[desc].lang_count = psd[1] >> 4;
            break;
      }
      psd   += (bitOff + 6 + 4) / 8;  // note: uses previous bitOff; psd must be set before bitOff is modified
      bitOff = (bitOff + 6 + 4) % 8;

      for (lang=0; lang < ld[desc].lang_count; lang++)
      {
         switch (bitOff)
         {
            case 0:
               ld[desc].lang[lang][0] = psd[0];
               ld[desc].lang[lang][1] = psd[1];
               ld[desc].lang[lang][2] = psd[2];
               break;

            case 2:
               ld[desc].lang[lang][0] = (psd[0] >> 2) | ((psd[1] & 0x03) << 6);
               ld[desc].lang[lang][1] = (psd[1] >> 2) | ((psd[2] & 0x03) << 6);
               ld[desc].lang[lang][2] = (psd[2] >> 2) | ((psd[3] & 0x03) << 6);
               break;

            case 4:
               ld[desc].lang[lang][0] = (psd[0] >> 4) | ((psd[1] & 0x0f) << 4);
               ld[desc].lang[lang][1] = (psd[1] >> 4) | ((psd[2] & 0x0f) << 4);
               ld[desc].lang[lang][2] = (psd[2] >> 4) | ((psd[3] & 0x0f) << 4);
               break;

            case 6:
               ld[desc].lang[lang][0] = (psd[0] >> 6) | ((psd[1] & 0x3f) << 2);
               ld[desc].lang[lang][1] = (psd[1] >> 6) | ((psd[2] & 0x3f) << 2);
               ld[desc].lang[lang][2] = (psd[2] >> 6) | ((psd[3] & 0x3f) << 2);
               break;
         }
         psd += 3;
      }
   }

   // concatenate the various parts of the block to a compound structure
   // 1st step: sum up the length & compute the offsets of each element from the start
   blockLen = sizeof(LI_BLOCK);
   li.off_desc = blockLen;
   blockLen += li.desc_no * sizeof(LI_DESC);

   // 2nd step: copy elements one after each other
   pBlk = EpgBlockCreate(BLOCK_TYPE_LI, blockLen);
   pLi = &pBlk->blk.li;
   memcpy((void *) pLi, &li, sizeof(LI_BLOCK));
   if (li.desc_no > 0)
   {
      memcpy((void *) LI_GET_DESC(pLi), ld, li.desc_no * sizeof(LI_DESC));
   }

   return(pBlk);
}

// ---------------------------------------------------------------------------
// Check a LI block for consistancy errors in counters, offsets or value ranges
//
static bool EpgBlockCheckLi( EPGDB_BLOCK * pBlock )
{
   bool result = FALSE;
   const LI_BLOCK * pLi;
   const LI_DESC  * pLd;
   uint desc;

   pLi = &pBlock->blk.li;

   if (pLi->desc_no > LI_MAX_DESC_COUNT)
   {
      debug1("EpgBlock-CheckLi: illegal descriptor count %d", pLi->desc_no);
   }
   else if (pBlock->size != sizeof(LI_BLOCK) + pLi->desc_no * sizeof(LI_DESC))
   {
      debug2("EpgBlock-CheckLi: illegal block size %d for %d descriptors", pBlock->size, pLi->desc_no);
   }
   else if (pLi->desc_no > 0)
   {
      if (pLi->off_desc == sizeof(LI_BLOCK))
      {
         result = TRUE;

         pLd = LI_GET_DESC(pLi);
         for (desc=0; desc < pLi->desc_no; desc++, pLd++)
         {
            if (pLd->lang_count > LI_MAX_LANG_COUNT)
            {
               debug2("EpgBlock-CheckLi: illegal lang count %d in desc #%d", pLd->lang_count, desc);
               result = FALSE;
               break;
            }
         }
      }
      else
         debug2("EpgBlock-CheckLi: illegal descriptor array offset %d for desc_no=%d", pLi->desc_no, pLi->off_desc);
   }
   else
      result = TRUE;

   return result;
}

// ----------------------------------------------------------------------------
// Swap all LI block struct elements > 1 byte for non-endian-matching databases
// - called before consistancy check, hence used offsets and counters must be checked
// - the swap is done in-place
//
static bool EpgBlockSwapLi( EPGDB_BLOCK * pBlock )
{
   LI_BLOCK * pLi;

   pLi = (LI_BLOCK *) &pBlock->blk.li;

   swap16(&pLi->block_no);
   swap16(&pLi->off_desc);

   return TRUE;
}

// ----------------------------------------------------------------------------
// Convert a TI block
//
EPGDB_BLOCK * EpgBlockConvertTi(const uchar *pCtrl, uint ctrlLen, uint strLen)
{
   EPGDB_BLOCK *pBlk;
   TI_BLOCK ti;
   const TI_BLOCK *pTi;
   TI_DESC std[TI_MAX_DESC_COUNT];
   const uchar *psd;
   uchar buf[3];
   uint  desc, subt, bitOff, blockLen;

   psd = pCtrl;
   ti.block_no        = (psd[5]>>4) | (psd[6]<<4) | ((psd[7]&0x0f)<<12);
   ti.netwop_no       = (psd[7]>>4) | (psd[8]&0x0f);
   ti.desc_no         = (psd[8]>>4) | (psd[9]&0x03);
   psd += 9;
   bitOff = 2;

   for (desc=0; desc < ti.desc_no;desc++)
   {
      switch (bitOff)
      {
         case 0:
            std[desc].id         = psd[0] & 0x3f;
            std[desc].subt_count = (psd[0] >> 6) | ((psd[1] & 0x03) << 2);
            break;

         case 2:
            std[desc].id         = psd[0] >> 2;
            std[desc].subt_count = psd[1] & 0x0F;
            break;

         case 4:
            std[desc].id         = (psd[0] >> 4) | ((psd [1] & 0x03) << 4);
            std[desc].subt_count = (psd[1] >> 2) & 0x0f;
            break;

         case 6:
            std[desc].id         = (psd[0] >> 6) | ((psd [1] & 0x0f) << 2);
            std[desc].subt_count = psd[1] >> 4;
            break;
      }
      psd   += (bitOff + 6 + 4) / 8;  // note: uses previous bitOff; psd must be set before bitOff is modified
      bitOff = (bitOff + 6 + 4) % 8;

      for (subt=0; subt < std[desc].subt_count; subt++)
      {
         switch (bitOff)
         {
            case 0:
               std[desc].subt[subt].lang[0] = psd[0];
               std[desc].subt[subt].lang[1] = psd[1];
               std[desc].subt[subt].lang[2] = psd[2];
               buf[0] = psd[3];
               buf[1] = psd[4];
               buf[2] = psd[5];
               break;

            case 2:
               std[desc].subt[subt].lang[0] = (psd[0] >> 2) | ((psd[1] & 0x03) << 6);
               std[desc].subt[subt].lang[1] = (psd[1] >> 2) | ((psd[2] & 0x03) << 6);
               std[desc].subt[subt].lang[2] = (psd[2] >> 2) | ((psd[3] & 0x03) << 6);
               buf[0] = (psd[3] >> 2) | ((psd[4] & 0x03) << 6);
               buf[1] = (psd[4] >> 2) | ((psd[5] & 0x03) << 6);
               buf[2] = (psd[5] >> 2) | ((psd[6] & 0x03) << 6);
               break;

            case 4:
               std[desc].subt[subt].lang[0] = (psd[0] >> 4) | ((psd[1] & 0x0f) << 4);
               std[desc].subt[subt].lang[1] = (psd[1] >> 4) | ((psd[2] & 0x0f) << 4);
               std[desc].subt[subt].lang[2] = (psd[2] >> 4) | ((psd[3] & 0x0f) << 4);
               buf[0] = (psd[3] >> 4) | ((psd[4] & 0x0f) << 4);
               buf[1] = (psd[4] >> 4) | ((psd[5] & 0x0f) << 4);
               buf[2] = (psd[5] >> 4) | ((psd[6] & 0x0f) << 4);
               break;

            case 6:
               std[desc].subt[subt].lang[0] = (psd[0] >> 6) | ((psd[1] & 0x3f) << 2);
               std[desc].subt[subt].lang[1] = (psd[1] >> 6) | ((psd[2] & 0x3f) << 2);
               std[desc].subt[subt].lang[2] = (psd[2] >> 6) | ((psd[3] & 0x3f) << 2);
               buf[0] = (psd[3] >> 6) | ((psd[4] & 0x3f) << 2);
               buf[1] = (psd[4] >> 6) | ((psd[5] & 0x3f) << 2);
               buf[2] = (psd[5] >> 6) | ((psd[6] & 0x3f) << 2);
               break;
         }
         // decode TTX page reference according to ETS 300 707 chap. 11.3.2
         std[desc].subt[subt].page = (uint)buf[0] | ((uint)(buf[1] & 0x80) << 1) | ((uint)(buf[2] & 0xC0) << 3);
         std[desc].subt[subt].subpage = (uint)(buf[1] & 0x7F) |  ((uint)(buf[2] & 0x3F) << 8);
         psd += 6;
      }
   }

   // concatenate the various parts of the block to a compound structure
   // 1st step: sum up the length & compute the offsets of each element from the start
   blockLen = sizeof(TI_BLOCK);
   ti.off_desc = blockLen;
   blockLen += ti.desc_no * sizeof(TI_DESC);

   // 2nd step: copy elements one after each other
   pBlk = EpgBlockCreate(BLOCK_TYPE_TI, blockLen);
   pTi = &pBlk->blk.ti;
   memcpy((void *) pTi, &ti, sizeof(TI_BLOCK));
   if (ti.desc_no > 0)
   {
      memcpy((void *) TI_GET_DESC(pTi), std, ti.desc_no * sizeof(TI_DESC));
   }

   return(pBlk);
}

// ---------------------------------------------------------------------------
// Check a TI block for consistancy errors in counters, offsets or value ranges
//
static bool EpgBlockCheckTi( EPGDB_BLOCK * pBlock )
{
   bool result = FALSE;
   const TI_BLOCK * pTi;
   const TI_DESC  * pTd;
   uint desc;

   pTi = &pBlock->blk.ti;

   if (pTi->desc_no > TI_MAX_DESC_COUNT)
   {
      debug1("EpgBlock-CheckTi: illegal descriptor count %d", pTi->desc_no);
   }
   else if (pBlock->size != sizeof(TI_BLOCK) + pTi->desc_no * sizeof(TI_DESC))
   {
      debug2("EpgBlock-CheckTi: illegal block size %d for %d descriptors", pBlock->size, pTi->desc_no);
   }
   else if (pTi->desc_no > 0)
   {
      if (pTi->off_desc == sizeof(TI_BLOCK))
      {
         result = TRUE;

         pTd = TI_GET_DESC(pTi);
         for (desc=0; desc < pTi->desc_no; desc++, pTd++)
         {
            if (pTd->subt_count > TI_MAX_LANG_COUNT)
            {
               debug2("EpgBlock-CheckTi: illegal lang count %d in desc #%d", pTd->subt_count, desc);
               result = FALSE;
               break;
            }
         }
      }
      else
         debug2("EpgBlock-CheckTi: illegal descriptor array offset %d for desc_no=%d", pTi->desc_no, pTi->off_desc);
   }
   else
      result = TRUE;

   return result;
}

// ----------------------------------------------------------------------------
// Swap all TI block struct elements > 1 byte for non-endian-matching databases
// - called before consistancy check, hence used offsets and counters must be checked
// - the swap is done in-place
//
static bool EpgBlockSwapTi( EPGDB_BLOCK * pBlock )
{
   TI_BLOCK * pTi;
   TI_DESC  * pTd;
   TI_SUBT  * pTs;
   uint       desc;
   uint       subt;
   bool       result = FALSE;

   pTi = (TI_BLOCK *) &pBlock->blk.ti;

   swap16(&pTi->block_no);
   swap16(&pTi->off_desc);

   if (pTi->desc_no > 0)
   {
      if (pTi->off_desc == sizeof(TI_BLOCK))
      {
         pTd = (TI_DESC *) TI_GET_DESC(pTi);
         result = TRUE;

         for (desc=0; desc < pTi->desc_no; desc++, pTd++)
         {
            if (pTd->subt_count <= TI_MAX_LANG_COUNT)
            {
               pTs = pTd->subt;
               for (subt = 0; subt < pTd->subt_count; subt++, pTs++)
               {
                  swap16(&pTs->page);
                  swap16(&pTs->subpage);
               }
            }
            else
            {
               result = FALSE;
               break;
            }
         }
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Convert a BI block
//
EPGDB_BLOCK * EpgBlockConvertBi(const uchar *pCtrl, uint ctrlLen)
{
   EPGDB_BLOCK *pBlk;
   BI_BLOCK *pBi;
   uint idx, app_count, app_type;

   app_count = pCtrl[3];
   pCtrl += 4;
   pBlk = NULL;

   for (idx=0; idx < app_count; idx++)
   {
      app_type  = (uint)pCtrl[0] | (pCtrl[1] << 8);
      if (app_type == 0)  // 0 is the app type allocated for Nextview
         break;
      pCtrl += 2;
   }

   pBlk = EpgBlockCreate(BLOCK_TYPE_BI, sizeof(BI_BLOCK));
   pBi = (BI_BLOCK *) &pBlk->blk.bi;  // remove const from pointer

   if (idx < app_count)
      pBi->app_id = idx + 1;
   else
      pBi->app_id = EPG_ILLEGAL_APPID;

   return pBlk;
}

// ---------------------------------------------------------------------------
// Check consistancy of an EPG block
// - this check is required when loading a block from a file or through the
//   network, as it might contain errors due to undetected version conflicts or
//   data corruption; the application should not crash due to any such errors
// - checks for errors in counters, offsets or value ranges
//
bool EpgBlockCheckConsistancy( EPGDB_BLOCK * pBlock )
{
   bool result;

   if (pBlock != NULL)
   {
      switch (pBlock->type)
      {
         case BLOCK_TYPE_BI:
            debug0("EpgBlock-CheckBlock: BI block encountered - should never be in the db");
            result = FALSE;
            break;
         case BLOCK_TYPE_AI:
            result = EpgBlockCheckAi(pBlock);
            break;
         case BLOCK_TYPE_NI:
            result = EpgBlockCheckNi(pBlock);
            break;
         case BLOCK_TYPE_OI:
            result = EpgBlockCheckOi(pBlock);
            break;
         case BLOCK_TYPE_MI:
            result = EpgBlockCheckMi(pBlock);
            break;
         case BLOCK_TYPE_LI:
            result = EpgBlockCheckLi(pBlock);
            break;
         case BLOCK_TYPE_TI:
            result = EpgBlockCheckTi(pBlock);
            result = TRUE;
            break;
         case BLOCK_TYPE_PI:
            result = EpgBlockCheckPi(pBlock);
            break;
         default:
            debug1("EpgBlock-CheckBlock: illegal block type %d", pBlock->type);
            result = FALSE;
            break;
      }
   }
   else
   {
      debug0("EpgBlock-CheckBlock: illegal NULL ptr arg");
      result = FALSE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Swap all EPG block struct elements > 1 byte for non-endian-matching databases
//
bool EpgBlockSwapEndian( EPGDB_BLOCK * pBlock )
{
   bool result;

   if (pBlock != NULL)
   {
      swap32(&pBlock->size);
      swap16(&pBlock->origBlkLen);
      swap16(&pBlock->parityErrCnt);
      swap32(&pBlock->updTimestamp);
      swap32(&pBlock->acqTimestamp);
      swap16(&pBlock->acqRepCount);
      swap32(&pBlock->type);

      switch (pBlock->type)
      {
         case BLOCK_TYPE_BI:
            debug0("EpgBlock-SwapEndian: BI block encountered - should never be in the db");
            result = FALSE;
            break;
         case BLOCK_TYPE_AI:
            result = EpgBlockSwapAi(pBlock);
            break;
         case BLOCK_TYPE_NI:
            result = EpgBlockSwapNi(pBlock);
            break;
         case BLOCK_TYPE_OI:
            result = EpgBlockSwapOi(pBlock);
            break;
         case BLOCK_TYPE_MI:
            result = EpgBlockSwapMi(pBlock);
            break;
         case BLOCK_TYPE_LI:
            result = EpgBlockSwapLi(pBlock);
            break;
         case BLOCK_TYPE_TI:
            result = EpgBlockSwapTi(pBlock);
            result = TRUE;
            break;
         case BLOCK_TYPE_PI:
            result = EpgBlockSwapPi(pBlock);
            break;
         default:
            debug1("EpgBlock-SwapEndian: illegal block type %d", pBlock->type);
            result = FALSE;
            break;
      }
   }
   else
   {
      debug0("EpgBlock-SwapEndian: illegal NULL ptr arg");
      result = FALSE;
   }

   return result;
}

