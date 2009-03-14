/*
 *  Process query message for remote EPG client
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
 *    Replies with programme data to remote queries sent by attached
 *    EPG clients (e.g. TV applications with integrated EPG browsers.)
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgquery.c,v 1.2 2008/10/12 20:00:29 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/syserrmsg.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgmain.h"
#include "epgui/pidescr.h"
#include "epgui/dumptext.h"
#include "epgui/epgsetup.h"
#include "epgui/epgquery.h"


static const char * const pFilterKeywords[] =
{
   "EXPIRE",
   "NETWOP_PRE",
   "AIR_TIMES",
   "NETIDX",
   "NETNAME",
   "NETCNI",
   "THEME",
   "SORTCRIT",
   "SERIES",
   "TITLE",
   "TITLE_WHOLE",
   "TEXT",
   "PROGIDX",
   "START",
   "DURATION",
   "PAR_RAT",
   "EDIT_RAT",
   "FEATURES",
   "LANGUAGES",
   "SUBTITLES",
   "VPS_PDC",
   "INVERT",
   NULL
};

typedef enum
{
   FKW_EXPIRE_TIME,
   FKW_NETWOP_PRE,
   FKW_AIR_TIMES,
   FKW_NETWOP,
   FKW_NETNAME,
   FKW_NETCNI,
   FKW_THEMES,
   FKW_SORTCRIT,
   FKW_SERIES,
   FKW_TITLE,
   FKW_TITLE_WHOLE,
   FKW_TEXT,
   FKW_PROGIDX,
   FKW_START,
   FKW_DURATION,
   FKW_PAR_RAT,
   FKW_EDIT_RAT,
   FKW_FEATURES,
   FKW_INVERT,
   FKW_COUNT
} EPG_FILTER_KEYWORD;

static const uint pFilterMasks[] =
{
   FILTER_EXPIRE_TIME, FILTER_NETWOP_PRE, FILTER_AIR_TIMES,
   FILTER_NETWOP, FILTER_NETWOP, FILTER_NETWOP,
   FILTER_THEMES, FILTER_SORTCRIT, FILTER_SERIES,
   FILTER_SUBSTR, FILTER_SUBSTR, FILTER_SUBSTR,
   FILTER_PROGIDX, FILTER_TIME_ONCE,
   FILTER_DURATION, FILTER_PAR_RAT, FILTER_EDIT_RAT, FILTER_FEATURES,
   FILTER_INVERT
};

static const char * const pFeatureKeywords[] =
{
   "MONO",
   "STEREO",
   "2CHAN",
   "SURROUND",
   "FULL",
   "WIDE",
   "PALPLUS",
   "ANALOG",
   "DIGITAL",
   "UNCRYPT",
   "CRYPT",
   "LIVE",
   "NEW",
   "REPEAT",
   "SUBTITLES",
   NULL
};

typedef enum
{
   FKW_FEAT_MONO,
   FKW_FEAT_STEREO,
   FKW_FEAT_2CHAN,
   FKW_FEAT_SURR,
   FKW_FEAT_FULL,
   FKW_FEAT_WIDE,
   FKW_FEAT_PALPLUS,
   FKW_FEAT_ANALOG,
   FKW_FEAT_DIGITAL,
   FKW_FEAT_UNCRYPT,
   FKW_FEAT_CRYPT,
   FKW_FEAT_LIVE,
   FKW_FEAT_NEW,
   FKW_FEAT_REPEAT,
   FKW_FEAT_SUBTXT,
   FKW_FEAT_COUNT
} EPG_FEAT_KEYWORD;


#define EPGQUERY_TIME_FMT_USAGE "YY-MM-DD.HH:MM:SS"

// ----------------------------------------------------------------------------
// Parse an enum keyword in the query
//
static bool EpgQuery_PopKeyword( const char * const * pKeyList, const char * pKeyWord,
                                 uint * pArgOff, int * pKeyIdx)
{
   int keyIdx;
   uint keyLen;
   bool result = FALSE;

   if ((pKeyList != NULL) && (pKeyWord != NULL))
   {
      pKeyWord += *pArgOff;
      keyIdx = 0;
      while (*pKeyList != NULL)
      {
         keyLen = strlen(*pKeyList);

         if ( (strncasecmp(*pKeyList, pKeyWord, keyLen) == 0) &&
              ((pKeyWord[keyLen] == '=') || (pKeyWord[keyLen] == ',') || (pKeyWord[keyLen] == 0)) )
         {
            if (pKeyWord[keyLen] != 0)
               *pArgOff += keyLen + 1;
            else
               *pArgOff += keyLen;
            *pKeyIdx = keyIdx;
            result = TRUE;
            break;
         }
         pKeyList += 1;
         keyIdx += 1;
      }

      ifdebug1((*pKeyWord != 0) && (*pKeyList == NULL), "EpgQuery-PopKeyword: invalid keyword '%s'", pKeyWord);
   }
   else
      fatal0("EpgQuery-PopKeyword: illegal NULL ptr param");

   return result;
}

// ----------------------------------------------------------------------------
// Parse a date & time string in the query
//
static bool EpgQuery_PopTime( const char * pArg, time_t * p_time_val, uint * pArgOff )
{
   struct tm t;
   struct tm * pt;
   time_t now;
   int  scan_pos1;
   int  scan_pos2;
   bool result = FALSE;

   if ((pArg != NULL) && (p_time_val != NULL))
   {
      if (sscanf(pArg + *pArgOff, " %u-%u-%u %n", &t.tm_year, &t.tm_mon, &t.tm_mday, &scan_pos1) >= 3)
      {
         memset(&t, 0, sizeof(t));
         t.tm_isdst = -1;

         t.tm_year -= 1900;
         t.tm_mon -= 1;

         switch (pArg[*pArgOff + scan_pos1])
         {
            case '.': case ':':
            case ',': case ';':
            case '-': case '/':
            case ' ': case '\t':
               if ( (sscanf(pArg + *pArgOff, " %u:%u:%u%n", &t.tm_hour, &t.tm_min, &t.tm_sec, &scan_pos2) >= 3) ||
                    (sscanf(pArg + *pArgOff, " %u:%u%n", &t.tm_hour, &t.tm_min, &scan_pos2) >= 2) )
               {
                  result = TRUE;
               }
               break;
            default:
               scan_pos2 = 0;
               result = TRUE;
               break;
         }
      }
      else if ( (sscanf(pArg + *pArgOff, " %u:%u:%u%n", &t.tm_hour, &t.tm_min, &t.tm_sec, &scan_pos2) >= 3) ||
                (sscanf(pArg + *pArgOff, " %u:%u%n", &t.tm_hour, &t.tm_min, &scan_pos2) >= 2) )
      {
         now = time(NULL);
         pt = localtime(&now);
         if (pt != NULL)
         {
            t.tm_year = pt->tm_year;
            t.tm_mon  = pt->tm_mon;
            t.tm_mday = pt->tm_mday;

            //if (pt->tm_hour * 60 + pt->tm_min > t.tm_hour * 60 + t.tm_min)
            //   t.tm_mday += 1;

            scan_pos1 = 0;
            result = TRUE;
         }
         else
            fatal0("EpgQuery-PopTime: unexpected failure of localtime()");
      }
      else
         debug1("EpgQuery-PopTime: parse error in date/time '%s'", pArg + *pArgOff);

      if (result)
      {
         *p_time_val = mktime(&t);
         if (*p_time_val != (time_t)(-1))
         {
            *pArgOff += scan_pos1 + scan_pos2;
         }
         else
            result = FALSE;
      }
      else
         debug1("EpgQuery-PopTime: invalid time spec in element '%s'", pArg);
   }
   else
      fatal0("EpgQuery-PopTime: illegal NULL ptr param");

   return result;
}

// ----------------------------------------------------------------------------
// Parse a single integer or an integer range "n - m"
// - white space around digits or separator is ignored
// - advances and returns the offset into the string acc. to consumed characters
//
static bool EpgQuery_PopIntRange( const char * pArg, uint * pArgOff, uint * p_int1, uint * p_int2 )
{
   int scan_pos;
   bool result = FALSE;

   if ((pArg != NULL) && (pArgOff != NULL) && (p_int1 != NULL))
   {
      while (*pArgOff == ' ')
         (*pArgOff)++;
      if (*pArgOff == ',')
         (*pArgOff)++;

      if ( (p_int2 != NULL) &&
           ( (sscanf(pArg + *pArgOff, " 0x%u - 0x%u %n", p_int1, p_int2, &scan_pos) >= 2) ||
             (sscanf(pArg + *pArgOff, " %u - %u %n", p_int1, p_int2, &scan_pos) >= 2) ))
      {
         // if needed swap values so that first is smaller than the second
         if (*p_int1 > * p_int2)
         {
            uint tmpVal = *p_int1;
            *p_int1 = *p_int2;
            *p_int2 = tmpVal;
         }
         *pArgOff += scan_pos;
         result = TRUE;
      }
      else if ( (sscanf(pArg + *pArgOff, " 0x%x %n", p_int1, &scan_pos) >= 1) ||
                (sscanf(pArg + *pArgOff, " %u %n", p_int1, &scan_pos) >= 1) )
      {
         if (p_int2 != NULL)
            *p_int2 = *p_int1;
         *pArgOff += scan_pos;
         result = TRUE;
      }
      else if (pArg[*pArgOff] != 0)
      {
         debug2("EpgQuery-PopIntRange: parse error in '%s' at offset %d", pArg, *pArgOff);
      }
   }
   else
      fatal0("EpgQuery-PopInt: illegal NULL ptr param");

   return result;
}

// ----------------------------------------------------------------------------
// Parse an integer value in the query
//
static bool EpgQuery_PopInt( const char * pArg, uint * pArgOff, uint * p_int )
{
   return EpgQuery_PopIntRange(pArg, pArgOff, p_int, NULL);
}

// ----------------------------------------------------------------------------
// Reset parameters for the given filter
// - only applicable to a subset of filter types
// - note: does not disable the filter
//
static void EpgQuery_FilterInit( FILTER_CONTEXT * fc, int filtType )
{
   dprintf1("EpgQuery-FilterInit: type %d\n", filtType);

   switch (filtType)
   {
      case FKW_NETWOP_PRE:
         EpgDbFilterInitNetwopPreFilter(fc);
         break;
      case FKW_AIR_TIMES:
         EpgDbFilterInitAirTimesFilter(fc);
         break;
      case FKW_NETWOP:
      case FKW_NETNAME:
      case FKW_NETCNI:
         EpgDbFilterInitNetwop(fc);
         break;
      case FKW_THEMES:
         EpgDbFilterInitThemes(fc, 0xff);
         break;
      case FKW_SORTCRIT:
         EpgDbFilterInitSortCrit(fc, 0xff);
         break;
      case FKW_SERIES:
         EpgDbFilterInitSeries(fc);
         break;

      case FKW_EXPIRE_TIME:
      case FKW_TITLE:
      case FKW_TITLE_WHOLE:
      case FKW_TEXT:
      case FKW_PROGIDX:
      case FKW_START:
      case FKW_DURATION:
      case FKW_PAR_RAT:
      case FKW_EDIT_RAT:
      case FKW_FEATURES:
      case FKW_INVERT:
         // no init required
         break;

      default:
         debug1("EpgQuery-FilterInit: unknown filter 0x%X", filtType);
         break;
   }
}

// ----------------------------------------------------------------------------
// Set parameters for the given filter
//
static bool EpgQuery_FilterSet( EPGDB_CONTEXT * pDbContext,
                                const AI_BLOCK * pAiBlock, FILTER_CONTEXT * fc,
                                uint filtType, const char * pArg, char ** ppErrStr )
{
   ulong time1;
   ulong time2;
   uint  idx;
   uint  int1;
   uint  int2;
   uint  count;
   uint  argOff;
   int   featType;
   bool  result = FALSE;

   switch (filtType)
   {
      case FKW_EXPIRE_TIME:
         argOff = 0;
         if (strcasecmp(pArg, "now") == 0)
         {
            time1 = time(NULL);
            time1 -= time1 % 60;
            dprintf1("EpgQuery-FilterSet: EXPIRE time now: %ld\n", (long)time1);
            EpgDbFilterSetExpireTime(fc, time1);
            argOff = 0 + 3;
            result = TRUE;
         }
         else if (EpgQuery_PopTime(pArg, &time1, &argOff))
         {
            dprintf3("EpgQuery-FilterSet: EXPIRE time '%s': %ld %s", pArg, (long)time1, ctime(&time1));
            EpgDbFilterSetExpireTime(fc, time1);
            result = TRUE;
         }
         else if (EpgQuery_PopInt(pArg, &argOff, &int1) && (pArg[argOff] == 0))
         {
            time1 = time(NULL) - int1 * 60;
            time1 -= time1 % 60;
            dprintf2("EpgQuery-FilterSet: EXPIRE time now -%d minutes: %ld\n", int1, (long)time1);
            EpgDbFilterSetExpireTime(fc, time1);
            result = TRUE;
         }
         else
            SystemErrorMessage_Set(ppErrStr, 0, "invalid EXPIRE time '", pArg, "' (expect 'now' or " EPGQUERY_TIME_FMT_USAGE ")", NULL);
         break;

      case FKW_NETWOP_PRE:
         if (pDbContext != NULL)
         {
            EpgSetup_SetNetwopPrefilter(pDbContext, fc);
         }
         result = (*pArg == 0);
         argOff = 0;
         break;

      case FKW_AIR_TIMES:
         //TODO not possible since this config is stored in the GUI section in the RC file (i.e. not loaded in dump mode)
         break;

      case FKW_NETWOP:
         argOff = 0;
         count = 0;
         while (EpgQuery_PopIntRange(pArg, &argOff, &int1, &int2))
         {
            dprintf3("EpgQuery-FilterSet: NETIDX '%s': %d-%d\n", pArg, int1, int2);
            for (idx = int1; idx <= int2; idx++)
            {
               if (idx < MAX_NETWOP_COUNT)
               {
                  EpgDbFilterSetNetwop(fc, idx);
                  count++;
               }
            }
         }
         result = (count > 0);
         break;

      case FKW_NETCNI:
         argOff = 0;
         count = 0;
         while (EpgQuery_PopIntRange(pArg, &argOff, &int1, &int2))
         {
            if (pAiBlock != NULL)
            {
               for (idx = 0; idx < pAiBlock->netwopCount; idx++) 
               {
                  if ( (AI_GET_NET_CNI_N(pAiBlock, idx) >= int1) &&
                       (AI_GET_NET_CNI_N(pAiBlock, idx) <= int2) )
                  {
                     dprintf4("EpgQuery-FilterSet: NETCNI '%s': 0x%04X-%04X -> %u\n", pArg, int1, int2, idx);
                     EpgDbFilterSetNetwop(fc, idx);
                     count++;
                     break;
                  }
               }
               ifdebug3(idx >= pAiBlock->netwopCount, "EpgQuery-FilterSet: unknown NETCNI '%s': 0x%04X-%04X", pArg, int1, int2);
            }
            else
               count++;
         }
         if ((count == 0) && (ppErrStr != NULL))
         {  // note: never reached: err msg is only used for syntax check
            SystemErrorMessage_Set(ppErrStr, 0, "no matching CNIs for ", pArg, NULL);
         }
         result = (count > 0);
         break;

      case FKW_NETNAME:
         if (pAiBlock != NULL)
         {
            for (idx = 0; idx < pAiBlock->netwopCount; idx++) 
            {
               bool isFromAi = FALSE;
               const char * pCfNetname = EpgSetup_GetNetName(pAiBlock, idx, &isFromAi);
               if (strcasecmp(pArg, pCfNetname) == 0)
               {
                  dprintf3("EpgQuery-FilterSet: NETNAME '%s': -> idx %u CNI:0x%04X\n", pArg, idx, AI_GET_NET_CNI_N(pAiBlock, idx));
                  EpgDbFilterSetNetwop(fc, idx);
                  result = TRUE;
                  break;
               }
            }
         }
         else
         {  // syntax check only
            result = TRUE;
         }
         argOff = strlen(pArg);
         break;

      case FKW_THEMES:
         // TODO: support classes
         // TODO: match theme by name (e.g. "movie")
         argOff = 0;
         count = 0;
         while (EpgQuery_PopIntRange(pArg, &argOff, &int1, &int2))
         {
            dprintf3("EpgQuery-FilterSet: THEME '%s': %d-%d\n", pArg, int1, int2);
            EpgDbFilterSetThemes(fc, int1, int2, 1);
            count += int2 - int1 + 1;
         }
         result = (count > 0);
         break;

      case FKW_SORTCRIT:
         argOff = 0;
         count = 0;
         while (EpgQuery_PopIntRange(pArg, &argOff, &int1, &int2))
         {
            dprintf3("EpgQuery-FilterSet: SORTCRIT '%s': %d-%d\n", pArg, int1, int2);
            EpgDbFilterSetSortCrit(fc, int1, int2, 1);
            count += int2 - int1 + 1;
         }
         result = (count > 0);
         break;

      case FKW_SERIES:
         argOff = 0;
         count = 0;
         while (EpgQuery_PopIntRange(pArg, &argOff, &int1, &int2))
         {
            dprintf3("EpgQuery-FilterSet: SERIES '%s': %d-%d\n", pArg, int1, int2);
            EpgDbFilterSetSeries(fc, int1, int2, TRUE);
            count += int2 - int1 + 1;
         }
         result = (count > 0);
         break;

      case FKW_DURATION:
         argOff = 0;
         if (EpgQuery_PopIntRange(pArg, &argOff, &int1, &int2))
         {
            dprintf3("EpgQuery-FilterSet: DURATION '%s': %d-%d\n", pArg, int1, int2);
            EpgDbFilterSetMinMaxDuration(fc, int1 * 60, int2 * 60);
            result = TRUE;
         }
         break;

      case FKW_PROGIDX:
         argOff = 0;
         if (EpgQuery_PopIntRange(pArg, &argOff, &int1, &int2))
         {
            dprintf3("EpgQuery-FilterSet: PROGIDX '%s': %d-%d\n", pArg, int1, int2);
            EpgDbFilterSetProgIdx(fc, int1, int2);
            result = TRUE;
         }
         break;

      case FKW_PAR_RAT:
         if (EpgQuery_PopInt(pArg, &argOff, &int1))
         {
            int1 /= 2;
            if (int1 > 8)
               int1 = 8;
            dprintf2("EpgQuery-FilterSet: PAR_RAT '%s': %d\n", pArg, int1);
            EpgDbFilterSetParentalRating(fc, int1);
            result = TRUE;
         }
         break;

      case FKW_EDIT_RAT:
         if (EpgQuery_PopInt(pArg, &argOff, &int1))
         {
            if (int1 >= 8)
               int1 = 7;
            dprintf2("EpgQuery-FilterSet: EDIT_RAT '%s': %d\n", pArg, int1);
            EpgDbFilterSetEditorialRating(fc, int1);
            result = TRUE;
         }
         break;

      case FKW_TITLE:
         /*                             scopeTitle, scopeDesc, matchCase, matchFull */
         EpgDbFilterSetSubStr(fc, pArg, TRUE,       FALSE,     FALSE,     FALSE);
         argOff = strlen(pArg);
         result = TRUE;
         break;
      case FKW_TITLE_WHOLE:
         EpgDbFilterSetSubStr(fc, pArg, TRUE,       FALSE,     FALSE,     TRUE);
         argOff = strlen(pArg);
         result = TRUE;
         break;
      case FKW_TEXT:
         EpgDbFilterSetSubStr(fc, pArg, TRUE,       TRUE,      FALSE,     FALSE);
         argOff = strlen(pArg);
         result = TRUE;
         break;

      case FKW_START:
         argOff = 0;
         if (EpgQuery_PopTime(pArg, &time1, &argOff))
         {
            if (pArg[argOff] == '-')
            {
               argOff += 1;
               result = EpgQuery_PopTime(pArg, &time2, &argOff);
            }
            else
            {
               time2 = time1 + 356*24*60*60;
               result = TRUE;
            }
            if (result)
            {
               dprintf4("EpgQuery-FilterSet: START time '%s': %ld-%ld %s", pArg, (long)time1, (long)time2, ctime(&time1));
               EpgDbFilterSetDateTimeBegin(fc, time1);
               EpgDbFilterSetDateTimeEnd(fc, time2);
            }
         }
         break;

      case FKW_FEATURES:
         count = 0;
         int1 = 0;
         int2 = 0;
         argOff = 0;
         while (EpgQuery_PopKeyword(pFeatureKeywords, pArg, &argOff, &featType))
         {
            switch (featType)
            {
               case FKW_FEAT_MONO:    int2 |= 0x03; int1 = (int1 & ~0x03) | 0x00; break;
               case FKW_FEAT_STEREO:  int2 |= 0x03; int1 = (int1 & ~0x03) | 0x02; break;
               case FKW_FEAT_2CHAN:   int2 |= 0x03; int1 = (int1 & ~0x03) | 0x01; break;
               case FKW_FEAT_SURR:    int2 |= 0x03; int1 = (int1 & ~0x03) | 0x03; break;

               case FKW_FEAT_FULL:    int2 |= 0x0c; int1 = (int1 & ~0x0c) | 0x00; break;
               case FKW_FEAT_WIDE:    int2 |= 0x0c; int1 = (int1 & ~0x04) | 0x04; break;
               case FKW_FEAT_PALPLUS: int2 |= 0x0c; int1 = (int1 & ~0x08) | 0x08; break;

               case FKW_FEAT_ANALOG:  int2 |= 0x10; int1 &= ~0x10; break;
               case FKW_FEAT_DIGITAL: int2 |= 0x10; int1 |= 0x10; break;

               case FKW_FEAT_UNCRYPT: int2 |= 0x20; int1 &= ~0x20; break;
               case FKW_FEAT_CRYPT:   int2 |= 0x20; int1 |= 0x20; break;

               case FKW_FEAT_LIVE:    int2 |= 0x40; int1 |= 0x40; break;
               case FKW_FEAT_NEW:     int2 |= 0xc0; int1 &= ~0xc0; break;
               case FKW_FEAT_REPEAT:  int2 = (int2 & ~0xc0) | 0x80; int1 |= 0x80; break;

               case FKW_FEAT_SUBTXT:  int2 |= 0x100; int1 |= 0x100; break;

               default: break;  // not reached due to while condition
            }
            dprintf3("EpgQuery-FilterSet: features '%s': 0x%x in mask %x\n", pArg, int1, int2);
            count += 1;
         }
         idx = EpgDbFilterGetNoFeatures(fc);
         if (idx + 1 < FEATURE_CLASS_COUNT)
         {
            EpgDbFilterSetFeatureFlags(fc, idx, int1, int2);
            EpgDbFilterSetNoFeatures(fc, idx + 1);
            result = (count > 0);
         }
         else if (ppErrStr != NULL)
         {
            SystemErrorMessage_Set(ppErrStr, 0, "too many feature classes", NULL);
         }
         break;

      case FKW_INVERT:
         // this filter has no options
         result = (*pArg == 0);
         argOff = 0;
         break;

      default:
         break;
   }
   if ((result != FALSE) && (pArg[argOff] != 0))
   {
      if (ppErrStr != NULL)
         SystemErrorMessage_Set(ppErrStr, 0, "trailing garbage at end of ", pFilterKeywords[filtType],
                                             " option '", pArg, "'", NULL);
      result = FALSE;
   }
   if ((result == FALSE) && (ppErrStr != NULL) && (*ppErrStr == NULL))
   {
      SystemErrorMessage_Set(ppErrStr, 0, "parse error on ", pFilterKeywords[filtType],
                                          " option '", pArg, "'", NULL);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Parse a query string
//
FILTER_CONTEXT * EpgQuery_Parse( EPGDB_CONTEXT * pDbContext, const char ** pQuery )
{
   const AI_BLOCK  * pAiBlock;
   FILTER_CONTEXT * fc = NULL;
   int filtType;
   uint  argIdx;
   uint  argOff;
   uint  mask;

   if ((pDbContext != NULL) && (pQuery != NULL))
   {
      EpgDbLockDatabase(pDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pDbContext);
      if (pAiBlock != NULL)
      {
         fc = EpgDbFilterCreateContext();
         mask = 0;
         argOff = 0;

         for (argIdx = 0; pQuery[argIdx] != NULL; argIdx++)
         {
            argOff = 0;
            if (EpgQuery_PopKeyword(pFilterKeywords, pQuery[argIdx], &argOff, &filtType))
            {
               // TODO for some types we need to check there's only one instance
               if ((mask & pFilterMasks[filtType]) == 0)
               {
                  EpgQuery_FilterInit(fc, filtType);
                  mask |= pFilterMasks[filtType];
               }

               EpgQuery_FilterSet(pDbContext, pAiBlock, fc,
                                  filtType, pQuery[argIdx] + argOff, NULL);
            }
         }

         if ((mask & FILTER_PERM) != 0)
         {
            EpgDbPreFilterEnable(fc, mask & FILTER_PERM);
         }
         if ((mask & FILTER_NONPERM) != 0)
         {
            EpgDbFilterEnable(fc, mask & FILTER_NONPERM);
         }
      }
      EpgDbLockDatabase(pDbContext, FALSE);
   }
   else
      fatal3("EpgQuery-Process: illegal NULL ptr params: %lx,%lx,%lx", (long)pDbContext, (long)fc, (long)pQuery);

   return fc;
}

// ----------------------------------------------------------------------------
// Check the syntax of a single query string on the command line
// - called while parsing the command line; returns FALSE if syntax incorrect
//
bool EpgQuery_CheckSyntax( const char * pArg, char ** ppErrStr )
{
   int  filtType;
   FILTER_CONTEXT * fc;
   uint argOff = 0;
   bool result = FALSE;

   if (ppErrStr != NULL)
      *ppErrStr = NULL;

   argOff = 0;
   if (EpgQuery_PopKeyword(pFilterKeywords, pArg, &argOff, &filtType))
   {
      fc = EpgDbFilterCreateContext();

      result = EpgQuery_FilterSet(NULL, NULL, fc, filtType, pArg + argOff, ppErrStr);

      EpgDbFilterDestroyContext(fc);
   }
   return result;
}
