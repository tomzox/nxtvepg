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
 *  $Id: epgblock.c,v 1.33 2001/04/01 19:42:12 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <time.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgctl/epgmain.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"


static uchar netwopAlphabets[MAX_NETWOP_COUNT];
static uchar providerAlphabet;

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

   time(&pBlock->acqTimestamp);
   pBlock->acqRepCount = 1;

   return pBlock;
}

// ----------------------------------------------------------------------------
// Check if a given year was a leap year
// - simplified, since 16-bit Julian works only up to year 2038 anyways
//
static bool IsLeapYear( uint year )
{
   return (((year % 100) % 4) == 0);
}

// ----------------------------------------------------------------------------
// Gets number of days of a month
//
static const uchar DAYS_PER_MONTH[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

static uchar GetDaysPerMonth(uchar month, uint year)
{
   uchar days;

   if ( (month >= 1) && (month <= 12) )
   {
      if ( IsLeapYear(year) == TRUE )
      {
         if ( month == 2 )
         {
            days = DAYS_PER_MONTH[month-1] + 1;
         }
         else
         {
            days = DAYS_PER_MONTH[month-1];
         }
      }
      else
      {
         days = DAYS_PER_MONTH[month-1];
      }
   }
   else
      days = 0;

   return days;
}

// ----------------------------------------------------------------------------
// Convert Julian date to Gregorian, i.e. day, month, year
//
static void GetDateFromJulian(struct tm *tm, uint julian)
{
   uint  year, daysPerYear;
   uchar month, daysPerMonth;
   
   julian -= (45000L-31);      // 1.1.1982
   
   year = 1982;
   while (1)  // must finish b/c of constant substraction in loop
   {
      if ( IsLeapYear(year) )
         daysPerYear = 366;
      else
         daysPerYear = 365;

      if (julian > daysPerYear)
         julian -= daysPerYear;
      else
         break;
      year += 1;
   }

   // Get the month and day of month from the day of year (starting 0 at Jan 1st)
   // - e.g. day #76 -> 16th March in leap year
   month = 1;
   while (1)
   {
      daysPerMonth = GetDaysPerMonth(month, year);

      if (julian > daysPerMonth)
         julian -= daysPerMonth;
      else
         break;
      month += 1;
   }
   assert(month <= 12);

   tm->tm_mday = julian;
   tm->tm_mon  = month - 1;     // -1 for mktime()
   tm->tm_year = year - 1900;   // -1900 for mktime()
}

// ----------------------------------------------------------------------------
// Cinvert a BCD coded time to MoD, i.e. "minutes of day"
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
//  - XXX TODO check validity
//
static void SetStartAndStopTime(uint bcdStart, uint julian, uint bcdStop, time_t *pStartTime, time_t *pStopTime)
{
   struct tm tm;
   uint startMoD, stopMoD;

   startMoD = EpgBlockBcdToMoD(bcdStart);
   stopMoD  = EpgBlockBcdToMoD(bcdStop);
   if (stopMoD < startMoD)
      stopMoD += 60*24;

   tm.tm_sec   = 0;
   tm.tm_min   = startMoD % 60;
   tm.tm_hour  = startMoD / 60;
   tm.tm_isdst = -1;           // let mktime() determine daylight at this date

   GetDateFromJulian(&tm, julian);

   // add UTC offset because mktime expects localtime but gets UTC
   // this is a gross hack that's only needed because there's no pendant to gmtime()
   #ifndef __NetBSD__ 
   *pStartTime = mktime(&tm) - timezone + 60*60 * tm.tm_isdst;
   #else 
   /* Seems like gmtoff already includes the dst offset, so we do not add tm.tm_isdst *60*60 */
   *pStartTime = mktime(&tm) + tm.tm_gmtoff;
   #endif 

   *pStopTime  = *pStartTime + (stopMoD - startMoD) * 60;
}


// ----------------------------------------------------------------------------
// The following character tables were taken from ALEVT-1.5.1
// Copyright (C) 1998,1999 Edgar Toernig (froese@gmx.de)
// - conforming to ETS 300 706, chapter 15.3: "Second G0 Set Designation and
//   National Option Set Selection", table 33.
// - XXX no support for non-latin-1 fonts yet
//
static const uchar natOptChars[8][16] =
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
    { '£', '$', '@', '«', '½', '»', '¬', '#', '­', '¼', '¦', '¾', '÷' }
};

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
//   are replace by special chars, depending on the country
// - in the following table -1 stands for ASCII chars, the other values
//   are to be taken as index in the national options table
//
static const char nationalOptionsMatrix[0x80] =
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
      result = natOptChars[alphabeth][ (uchar) nationalOptionsMatrix[val] ];
   }
   else
      result = val;

   return result;
}

// ----------------------------------------------------------------------------
// Apply escape sequence to a string
// - only language-specific escapes and newlines are evaluated
// - at the same time white space is compressed
// - newline (explicit and implicit) is replaced by blank;
//   line breaks are inserted at time of output, depending on window width
//
static uchar * ApplyEscapes(const uchar *pText, uint textLen, const uchar *pEscapes, uchar escCount, uchar netwop)
{
   uchar *pout, *po;
   uint  escIdx, nextEsc;
   uint  i, linePos, strLen;
   bool  lastWhite;
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
      
         if (i == nextEsc)
         {
            data = pEscapes[2];
            switch (pEscapes[1] >> 2)
            {
               case 0x01:  // mosaic character: unsupported
                  *po = ' ';
                  break;
               case 0x08:  // change alphabet
                  if (data < 8)
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
                  // the newline is inserted in front of the current character
                  if (!lastWhite)
                  {
                     if (strLen > 1)
                     {
                        po[1] = *po;
                        *po++ = ' ';
                        strLen -= 1;
                     }
                     else
                     {
                        debug0("Apply-Escapes: output string length exceeded");
                        *po = ' ';
                     }
                  }
                  lastWhite = TRUE;
                  linePos = 0;
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
            if (!lastWhite)
            {
               po++;
               strLen -= 1;
            }
            lastWhite = TRUE;
         }
         else
         {
            po++;
            strLen -= 1;
            lastWhite = FALSE;
         }
         linePos += 1;

         if (linePos == 40)
         {  // add blank after last character of line
            if (lastWhite == FALSE)
            {
               if (strLen > 0)
               {
                  *(po++) = ' ';
                  strLen -= 1;
               }
               else
                  debug0("Apply-Escapes: output string length exceeded");
            }
            lastWhite = TRUE;
            linePos = 0;
         }
      }

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
            bitOff = (bitOff + 6 + 6 + 8) % 8;
            psd   += (bitOff + 6 + 6 + 8) / 8;
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
   uint piLen;
 
   psd = pCtrl;
   pi.block_no              = (psd[5]>>4) | (psd[6]<<4) | ((psd[7]&0x0f)<<12);
   pi.feature_flags         = (psd[7]>>4) | (psd[8]<<4);
   pi.netwop_no             = psd[9];
   SetStartAndStopTime(       psd[10] | (psd[11]<<8),
                              psd[12] | (psd[13]<<8),
                              psd[14] | (psd[15]<<8),
                              &pi.start_time, &pi.stop_time);
   pi.pil                   = psd[16] | (psd[17]<<8) | ((psd[18]&0x0f)<<16);
   pi.parental_rating       = psd[18] >> 4;;
   pi.editorial_rating      = psd[19] & 0x07;
   pi.no_themes             = (psd[19]>>3) & 0x07;
   pi.no_sortcrit           = (psd[19]>>6) | ((psd[20]&1)<<2);
   pi.no_descriptors        = (psd[20]>>1)&0x3f;
   pi.background_reuse      = psd[20]>>7;
   if (pi.no_themes > 0)
      memcpy(pi.themes, psd + 21, pi.no_themes);
   if (pi.no_sortcrit > 0)
      memcpy(pi.sortcrits, psd + 21 + pi.no_themes, pi.no_sortcrit);
   psd += 21 + pi.no_themes + pi.no_sortcrit;

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
      pi.long_info_type = 0;
   }
   else
   {
      shortInfoLen = psd[1 + psd[0]*3];
      if (shortInfoLen > 0)
        pShortInfo = ApplyEscapes(pCtrl+ctrlLen+2+titleLen, shortInfoLen, psd+1, psd[0], pi.netwop_no);
      else
        pShortInfo = NULL;
      psd += psd[0] * 3 + 2;
 
      pi.long_info_type   = psd[0] & 0x7;
      psd += 1;
      switch (pi.long_info_type)
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
      memcpy(PI_GET_TITLE(pPi), pTitle, strlen(pTitle) + 1);
      xfree(pTitle);
   }
   else
   {  // title string shall always be available -> set a 0-Byte
      PI_GET_TITLE(pPi)[0] = 0;
   }
   if (pShortInfo != NULL)
   {
      memcpy(PI_GET_SHORT_INFO(pPi), pShortInfo, strlen(pShortInfo) + 1);
      xfree(pShortInfo);
   }
   if (pLongInfo != NULL)
   {
      memcpy(PI_GET_LONG_INFO(pPi), pLongInfo, strlen(pLongInfo) + 1);
      xfree(pLongInfo);
   }
   if (pDescriptors != NULL)
   {
      memcpy(PI_GET_DESCRIPTORS(pPi), pDescriptors, pi.no_descriptors * sizeof(DESCRIPTOR));
      xfree(pDescriptors);
   }
  
   return pBlk;
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
   uint  lenSum, blockLen;
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
   ai.serviceNameLen = psd[7+14] & 0x1f;
   psd += 7 + 15;
   pst = pCtrl + ctrlLen + 2;

   pNetwops = xmalloc(ai.netwopCount * sizeof(AI_NETWOP));
   lenSum = 0;
   for (i=0; i<ai.netwopCount; i++)
   {
      if ((i & 1) == 0)
      {
         pNetwops[i].cni         = psd[0] | (psd[1] << 8);
         pNetwops[i].lto         = psd[2];
         pNetwops[i].dayCount    = psd[3] & 0x1f;
         pNetwops[i].nameLen     = (psd[3] >> 5) | ((psd[4] & 3) << 3);
         pNetwops[i].alphabet    = (psd[4] >> 2) | ((psd[5] & 1) << 6);
         pNetwops[i].startNo     = (psd[5] >> 1) | (psd[6] << 7) | ((psd[7] & 1) << 15);
         pNetwops[i].stopNo      = (psd[7] >> 1) | (psd[8] << 7) | ((psd[9] & 1) << 15);
         pNetwops[i].stopNoSwo   = (psd[9] >> 1) | (psd[10] << 7) | ((psd[11] & 1) << 15);
         pNetwops[i].addInfo     = (psd[11] >> 1) | ((psd[12] & 0xf) << 7);
         psd += 12;  // plus 4 Bit im folgenden Block
      }
      else
      {
         pNetwops[i].cni         = (psd[0] >> 4) | (psd[1] << 4) | ((psd[2] & 0x0f) << 12);
         pNetwops[i].lto         = (psd[2] >> 4) | (psd[3] << 4);
         pNetwops[i].dayCount    = (psd[3] >> 4) | ((psd[4] & 1) << 4);
         pNetwops[i].nameLen     = (psd[4] >> 1) & 0x1f;
         pNetwops[i].alphabet    = (psd[4] >> 6) | ((psd[5] & 0x1f) << 2);
         pNetwops[i].startNo     = (psd[5] >> 5) | (psd[6] << 3) | ((psd[7] & 0x1f) << 11);
         pNetwops[i].stopNo      = (psd[7] >> 5) | (psd[8] << 3) | ((psd[9] & 0x1f) << 11);
         pNetwops[i].stopNoSwo   = (psd[9] >> 5) | (psd[10] << 3) | ((psd[11] & 0x1f) << 11);
         pNetwops[i].addInfo     = (psd[11] >> 5) | (psd[12] << 3);
         psd += 13;  // inklusive 4 Bit vom vorherigen Block
      }
      lenSum += pNetwops[i].nameLen;
   }

   // concatenate the various parts of the block to a compound structure
   pst = pCtrl + ctrlLen + 2;
   blockLen = sizeof(AI_BLOCK) +
              (ai.netwopCount * sizeof(AI_NETWOP)) +
              (ai.serviceNameLen + 1) +
              (lenSum + ai.netwopCount);
   pBlk = EpgBlockCreate(BLOCK_TYPE_AI, blockLen);
   pAi = (AI_BLOCK *) &pBlk->blk.ai;  // remove const from pointer
   memcpy(pAi, &ai, sizeof(AI_BLOCK));
   blockLen = sizeof(AI_BLOCK);

   pAi->off_netwops = blockLen;
   memcpy(AI_GET_NETWOPS(pAi), pNetwops, ai.netwopCount * sizeof(AI_NETWOP));
   blockLen += ai.netwopCount * sizeof(AI_NETWOP);

   pAi->off_serviceNameStr = blockLen;
   memcpy(AI_GET_SERVICENAME(pAi), pst, ai.serviceNameLen);
   *((uchar *)pAi + blockLen + ai.serviceNameLen) = 0;
   blockLen += ai.serviceNameLen + 1;
   pst += ai.serviceNameLen;

   for (i=0; i<ai.netwopCount; i++)
   {
      AI_NETWOP *pNetwop;

      pNetwop = &AI_GET_NETWOPS(pAi)[i];
      pNetwop->off_name = blockLen;
      memcpy((uchar *)pAi + blockLen, pst, pNetwop->nameLen);
      *((uchar *)pAi + blockLen + pNetwop->nameLen) = 0;
      blockLen += pNetwop->nameLen + 1;
      pst += pNetwop->nameLen;
   }
   xfree(pNetwops);
   assert(blockLen == pBlk->size);

   // update the alphabet list for string decoding
   EpgBlockSetAlphabets(&pBlk->blk.ai);

   return pBlk;
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
      memcpy(OI_GET_HEADER(pOi), pHeader, strlen(pHeader) + 1);
      xfree(pHeader);
   }
   if (pMessage != NULL)
   {
      memcpy(OI_GET_MESSAGE(pOi), pMessage, strlen(pMessage) + 1);
      xfree(pMessage);
   }
   if (pDescriptors != NULL)
   {
      memcpy(OI_GET_DESCRIPTORS(pOi), pDescriptors, oi.no_descriptors * sizeof(DESCRIPTOR));
      xfree(pDescriptors);
   }

   return(pBlk);
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
      ni.off_events = blockLen;
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
   memcpy((void *)pNi, &ni, sizeof(NI_BLOCK));
   if (pNi->no_events > 0)
   {
      memcpy(NI_GET_EVENTS(pNi), ev, ni.no_events * sizeof(EVENT_ATTRIB));
   }
   if (pHeader != NULL)
   {
      memcpy(NI_GET_HEADER(pNi), pHeader, strlen(pHeader) + 1);
      xfree(pHeader);
   }
   if (pDescriptors != NULL)
   {
      memcpy(NI_GET_DESCRIPTORS(pNi), pDescriptors, ni.no_descriptors * sizeof(DESCRIPTOR));
      xfree(pDescriptors);
   }
   for (i=0; i<ni.no_events; i++)
   {
      if (tmp_evstr[i] != NULL)
      {
         memcpy(NI_GET_EVENT_STR(pNi, &ev[i]), tmp_evstr[i], strlen(tmp_evstr[i]) + 1);
         xfree(tmp_evstr[i]);
      }
   } 

   return(pBlk);
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
   memcpy((void *)pMi, &mi, sizeof(MI_BLOCK));
   if (pMessage != NULL)
   {
      memcpy(MI_GET_MESSAGE(pMi), pMessage, strlen(pMessage) + 1);
      xfree(pMessage);
   }
   if (pDescriptors != NULL)
   {
      memcpy(MI_GET_DESCRIPTORS(pMi), pDescriptors, mi.no_descriptors * sizeof(DESCRIPTOR));
      xfree(pDescriptors);
   }

   return(pBlk);
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
      bitOff = (bitOff + 6 + 4) % 8;
      psd   += (bitOff + 6 + 4) / 8;

      for (lang=0; lang < ld[desc].lang_count; lang++)
      {
         switch (bitOff)
         {
            case 0:
               ld[desc].lang[0][lang] = psd[0];
               ld[desc].lang[1][lang] = psd[1];
               ld[desc].lang[2][lang] = psd[2];
               break;

            case 2:
               ld[desc].lang[0][lang] = (psd[0] >> 2) | ((psd[1] & 0x03) << 6);
               ld[desc].lang[1][lang] = (psd[1] >> 2) | ((psd[2] & 0x03) << 6);
               ld[desc].lang[2][lang] = (psd[2] >> 2) | ((psd[3] & 0x03) << 6);
               break;

            case 4:
               ld[desc].lang[0][lang] = (psd[0] >> 4) | ((psd[1] & 0x0f) << 4);
               ld[desc].lang[1][lang] = (psd[1] >> 4) | ((psd[2] & 0x0f) << 4);
               ld[desc].lang[2][lang] = (psd[2] >> 4) | ((psd[3] & 0x0f) << 4);
               break;

            case 6:
               ld[desc].lang[0][lang] = (psd[0] >> 6) | ((psd[1] & 0x3f) << 2);
               ld[desc].lang[1][lang] = (psd[1] >> 6) | ((psd[2] & 0x3f) << 2);
               ld[desc].lang[2][lang] = (psd[2] >> 6) | ((psd[3] & 0x3f) << 2);
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
   memcpy((void*)pLi, &li, sizeof(LI_BLOCK));
   if (li.desc_no > 0)
   {
      memcpy((void*)LI_GET_DESC(pLi), ld, li.desc_no * sizeof(LI_DESC));
   }

   return(pBlk);
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
      bitOff = (bitOff + 6 + 4) % 8;
      psd   += (bitOff + 6 + 4) / 8;

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
         psd += 5;
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
   memcpy((void *)pTi, &ti, sizeof(TI_BLOCK));
   if (ti.desc_no > 0)
   {
      memcpy(TI_GET_DESC(pTi), std, ti.desc_no * sizeof(TI_DESC));
   }

   return(pBlk);
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

