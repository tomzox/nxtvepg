/*
 *  Nextview block ASCII dump
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
 *    Optionally dumps the information contained in every incoming
 *    Nextview block as ASCII to stdout. This is very useful for
 *    debugging the transmitted data (which often contains errors
 *    or inconsistancies, like overlapping running times)
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgtxtdump.c,v 1.26 2002/06/23 10:21:34 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgtxtdump.h"
#include "epgui/pdc_themes.h"


#define NETNAME_LENGTH 6
#define NETNAME_LENGTH_STR "6"
#define NETNAME_LENGTH0 (NETNAME_LENGTH+1)

// ----------------------------------------------------------------------------
// status variable: turns output on or off
// - can be toggled by user interface or command line argument
//
static uchar epgTxtListBlocks = FALSE;
static const char * const pEpgTxtDumpHeader = "Nextview ASCII Dump\n";

// ----------------------------------------------------------------------------
// Print PI

// ----------------------------------------------------------------------------
// Decode PIL code into date and time of day
//
static bool DecodePil(uint pil, uchar *pHour, uchar *pMinute, uchar *pDay, uchar *pMonth)
{
   uint hour, minute, day, month;
   bool result;
   
   /*
   pil = (((ulong)(day & 0x1F)) << 15) +
         (((ulong)(month & 0x0F)) << 11) +
         (((ulong)(hour & 0x1F)) << 6) +
         ((ulong)(minute & 0x3F));
   */

   day    = (pil >> 15) & 0x1f;
   month  = (pil >> 11) & 0x0f;
   hour   = (pil >>  6) & 0x1f;
   minute =  pil        & 0x3f;

   if ((day > 0) && (month > 0) && (month <= 12) && (hour < 24) && (minute < 60))
   {
      *pHour   = hour;
      *pMinute = minute;
      *pDay    = day;
      *pMonth  = month;
      result = TRUE;
   }
   else
      result = FALSE;

   return result;
}

// ----------------------------------------------------------------------------
//#define COMPARE_PIL
#ifdef COMPARE_PIL
static uchar * GetPiPilStr( uint pil, PI_BLOCK *pPi )
#else
static uchar * GetPiPilStr( uint pil )
#endif
{
   uchar hour, minute, day, month;
   static uchar pilStr[12];

   if (DecodePil(pil, &hour, &minute, &day, &month))
      sprintf(pilStr, "%02d:%02d/%02d.%02d", hour, minute, day, month);
   else
      strcpy(pilStr, "INVAL");

   #ifdef COMPARE_PIL
   {
      struct tm *tm;
      tm = localtime(&pPi->start_time);
      if ((tm->tm_hour != hour) || (tm->tm_min != minute) ||
          (tm->tm_mon+1 != month) || (tm->tm_mday != day))
      {
         fprintf(fp, "XXXSTART!=%02d:%02d/%02d.%02d",
                     tm->tm_hour, tm->tm_min, tm->tm_mday, tm->tm_mon+1);
      }
   }
   #endif

   return pilStr;
}

// ---------------------------------------------------------------------------
// Print Programme Information

static void EpgTxtDumpPi( FILE *fp, const PI_BLOCK * pPi, uchar stream, uchar version, const AI_BLOCK * pAi )
{
   const uchar * pThemeStr;
   const uchar * pGeneralStr;
   uchar start_str[40], stop_str[20];
   uchar netname[NETNAME_LENGTH+1];
   uchar *pStrSoundFormat;
   struct tm *pStart, *pStop;

   if (pPi != NULL)
   {
      pStart = localtime(&pPi->start_time);
      if (pStart != NULL)
         strftime(start_str, sizeof(start_str), "%a %d.%m.%Y %H:%M", pStart);
      else
         strcpy(start_str, "??");

      pStop  = localtime(&pPi->stop_time);
      if (pStop != NULL)
         strftime(stop_str, sizeof(stop_str), "%H:%M", pStop);
      else
         strcpy(stop_str, "??");

      if ((pAi != NULL) && (pPi->netwop_no < pAi->netwopCount))
      {
         strncpy(netname, AI_GET_NETWOP_NAME(pAi, pPi->netwop_no), NETNAME_LENGTH);
         netname[NETNAME_LENGTH] = 0;
      }
      else
         sprintf(netname, "#%d", pPi->netwop_no);

      fprintf(fp, "PI:%d #%04x %-" NETNAME_LENGTH_STR "s  %s-%s %-40s ",
                  stream + 1,
                  pPi->block_no,
                  netname,
                  start_str,
                  stop_str,
                  ((pPi->off_title != 0) ? PI_GET_TITLE(pPi) : (uchar *) "NULL")
      );

      switch(pPi->feature_flags & 0x03)
      {
        case  0: pStrSoundFormat = "mono"; break;
        case  1: pStrSoundFormat = "2chan"; break;
        case  2: pStrSoundFormat = "stereo"; break;
        case  3: pStrSoundFormat = "surround"; break;
        default: pStrSoundFormat = ""; break;
      }

      if (pPi->no_themes > 0)
         pThemeStr = PdcThemeGetWithGeneral(pPi->themes[0], &pGeneralStr, TRUE);
      else
         pGeneralStr = pThemeStr = "--";

      fprintf(fp, "     v%02x %s%s%s%s%s%s%s%s PR=%d ER=%d PIL=%s #s=%d #l=%d  #t=%d (%02x %s%s) #s=%d (%02x)\n",
                  version,
                  pStrSoundFormat,
                  ((pPi->feature_flags & 0x04) ? ",wide" : ""),
                  ((pPi->feature_flags & 0x08) ? ",PAL+" : ""),
                  ((pPi->feature_flags & 0x10) ? ",digital" : ""),
                  ((pPi->feature_flags & 0x20) ? ",encrypted" : ""),
                  ((pPi->feature_flags & 0x40) ? ",live" : ""),
                  ((pPi->feature_flags & 0x80) ? ",repeat" : ""),
                  ((pPi->feature_flags & 0x100) ? ",subtitles" : ""),
                  pPi->parental_rating *2,
                  pPi->editorial_rating,
                  GetPiPilStr(pPi->pil),
                  (PI_HAS_SHORT_INFO(pPi) ? strlen(PI_GET_SHORT_INFO(pPi)) : 0),
                  (PI_HAS_LONG_INFO(pPi) ? strlen(PI_GET_LONG_INFO(pPi)) : 0),
                  pPi->no_themes,
                  ((pPi->no_themes > 0) ? pPi->themes[0] : 0),
                  pThemeStr,
                  pGeneralStr,
                  pPi->no_sortcrit,
                  ((pPi->no_sortcrit > 0) ? pPi->sortcrits[0] : 0)
             );

      if (PI_HAS_SHORT_INFO(pPi))
         fprintf(fp, "     SHORT %s\n", PI_GET_SHORT_INFO(pPi));
      if (PI_HAS_LONG_INFO(pPi))
         fprintf(fp, "     LONG %s\n", PI_GET_LONG_INFO(pPi));
      if (pPi->background_reuse)
         fprintf(fp, "     SHORT/LONG: REUSE block %d\n", pPi->background_ref);
   }
}

// ---------------------------------------------------------------------------
// Print Application Information

static void EpgTxtDumpAi( FILE *fp, const AI_BLOCK * pAi, uchar stream )
{
   uint blockSum1, blockSum2;
   uint i;

   if (pAi != NULL)
   {
      fprintf(fp, "AI: \"%s\" version %d/%d, %d netwops, this=%d\n",
                  AI_GET_SERVICENAME(pAi), pAi->version, pAi->version_swo,
                  pAi->netwopCount, pAi->thisNetwop);

      fprintf(fp, "    stream 1: #NI=%d #OI=%d #MI=%d  stream 2: #NI=%d #OI=%d #MI=%d\n",
                  pAi->niCount, pAi->oiCount, pAi->miCount,
                  pAi->niCountSwo, pAi->oiCountSwo, pAi->miCountSwo);

      blockSum1 = blockSum2 = 0;
      for (i=0; i<pAi->netwopCount; i++)
      {
         const AI_NETWOP *pNetwop = &AI_GET_NETWOPS(pAi)[i];
         fprintf(fp, "    %2d: CNI=%04x,LTO=%d,days=%d,lang=%x,blocks %04x-%04x-%04x,addi=%03x  %s\n",
                     i, pNetwop->cni,
                     pNetwop->lto,
                     pNetwop->dayCount,
                     pNetwop->alphabet,
                     pNetwop->startNo,
                     pNetwop->stopNo,
                     pNetwop->stopNoSwo,
                     pNetwop->addInfo,
                     AI_GET_STR_BY_OFF(pAi, pNetwop->off_name));

         blockSum1 += EpgDbGetPiBlockCount(pNetwop->startNo, pNetwop->stopNo);
         blockSum2 += EpgDbGetPiBlockCount(pNetwop->startNo, pNetwop->stopNoSwo);
      }
      fprintf(fp, "    #PI blocks total: %d (%d stream 1 only)\n", blockSum2, blockSum1);
   }
}

// ---------------------------------------------------------------------------
// Print OSD Information

static void EpgTxtDumpOi( FILE *fp, const OI_BLOCK * pOi, uchar stream )
{
   if (pOi != NULL)
   {
      fprintf(fp, "OI:%d %04x #h=%d #m=%d ma=%d #d=%d %s\n",
                  stream + 1,
                  pOi->block_no,
                  pOi->header_size + 1,
                  pOi->msg_size + 1,
                  pOi->msg_attrib,
                  pOi->no_descriptors,
                  ((pOi->off_header != 0) ? OI_GET_HEADER(pOi) : (uchar *) "")
      );

      if (pOi->off_message != 0)
         fprintf(fp, "     MSG %s\n", OI_GET_MESSAGE(pOi));
   }
}

// ---------------------------------------------------------------------------
// Print Navigation Information

static void EpgTxtDumpNi( FILE *fp, const NI_BLOCK * pNi, uchar stream )
{
   const EVENT_ATTRIB *pEv;
   uchar off, pAts[400];
   uint i, j;

   if (pNi != NULL)
   {
      fprintf(fp, "NI:%d %04x #h=%d #m=%d ma=%d #d=%d %s\n",
                  stream + 1,
                  pNi->block_no,
                  pNi->header_size + 1,
                  pNi->msg_size + 1,
                  pNi->msg_attrib,
                  pNi->no_descriptors,
                  ((pNi->off_header != 0) ? NI_GET_HEADER(pNi) : (uchar *) "")
             );

      pEv = NI_GET_EVENTS(pNi);

      for (i=0; i<pNi->no_events; i++)
      {
         if (pEv[i].no_attribs > 0)
         {
            strcpy(pAts, " (");
            off = 2;
            for (j=0; j<pEv[i].no_attribs; j++)
            {
               off = strlen(pAts);
               switch (pEv[i].unit[j].kind)
               {
                  case EV_ATTRIB_KIND_REL_DATE:
                     sprintf(pAts+off, "Today+%02x", (uint)pEv[i].unit[j].data);
                     break;
                  case EV_ATTRIB_KIND_PROGNO_START:
                     sprintf(pAts+off, "ProgNo>=%02x", (uint)pEv[i].unit[j].data);
                     break;
                  case EV_ATTRIB_KIND_PROGNO_STOP:
                     sprintf(pAts+off, "ProgNo<=%02x", (uint)pEv[i].unit[j].data);
                     break;
                  case EV_ATTRIB_KIND_NETWOP:
                     sprintf(pAts+off, "Netwop=%02x", (uint)pEv[i].unit[j].data);
                     break;
                  case EV_ATTRIB_KIND_EDITORIAL:
                     sprintf(pAts+off, "Editor>=%02x", (uint)pEv[i].unit[j].data);
                     break;
                  case EV_ATTRIB_KIND_PARENTAL:
                     sprintf(pAts+off, "Parent>=%02x", (uint)pEv[i].unit[j].data*2);
                     break;
                  case EV_ATTRIB_KIND_START_TIME:
                     sprintf(pAts+off, "Start>=%04x", (uint)pEv[i].unit[j].data);
                     break;
                  case EV_ATTRIB_KIND_STOP_TIME:
                     sprintf(pAts+off, "Stop<=%04x", (uint)pEv[i].unit[j].data);
                     break;
                  case EV_ATTRIB_KIND_FEATURES:
                     sprintf(pAts+off, "Features &%03x=%03x", (uint)pEv[i].unit[j].data>>12, (uint)pEv[i].unit[j].data & 0xfff);
                     break;
                  case EV_ATTRIB_KIND_LANGUAGE:
                     sprintf(pAts+off, "Lang=%c%c%c", ((uchar)pEv[i].unit[j].data >>16)&0xff, ((uchar)pEv[i].unit[j].data>>8)&0xff, (uchar)pEv[i].unit[j].data &0xff);
                     break;
                  case EV_ATTRIB_KIND_SUBT_LANG:
                     sprintf(pAts+off, "Subtit=%c%c%c", ((uchar)pEv[i].unit[j].data >>16)&0xff, ((uchar)pEv[i].unit[j].data>>8)&0xff, (uchar)pEv[i].unit[j].data &0xff);
                     break;
                  default:
                     if ((pEv[i].unit[j].kind >= EV_ATTRIB_KIND_THEME) && (pEv[i].unit[j].kind <= (EV_ATTRIB_KIND_THEME + 7)))
                        sprintf(pAts+off, "Theme class #%d=%02x", pEv[i].unit[j].kind - EV_ATTRIB_KIND_THEME, (uint)pEv[i].unit[j].data);
                     else if ((pEv[i].unit[j].kind >= EV_ATTRIB_KIND_SORTCRIT) && (pEv[i].unit[j].kind <= (EV_ATTRIB_KIND_SORTCRIT + 7)))
                        sprintf(pAts+off, "Sortcrit class #%d=%02x", pEv[i].unit[j].kind - EV_ATTRIB_KIND_SORTCRIT, (uint)pEv[i].unit[j].data);
                     else
                        debug3("EpgTxtDumpNi: unknown kind %d in attrib %d of event %d", pEv[i].unit[j].kind, j, i);
                     break;
               }
               if (j+1 < pEv[i].no_attribs)
                  strcat(pAts, ", ");
            }
            strcat(pAts, ")");
         }
         else
            pAts[0] = 0;

         fprintf(fp, "     %d: %s #%04x #a=%d %s%s\n",
                     i,
                     ((pEv[i].next_type == 1) ? "NI" : "OI"),
                     pEv[i].next_id,
                     pEv[i].no_attribs,
                     ((pEv[i].off_evstr != 0) ? NI_GET_EVENT_STR(pNi, &pEv[i]) : (uchar*)"NULL"),
                     pAts
                );
      }
   }
}

// ---------------------------------------------------------------------------
// Print Message Information

static void EpgTxtDumpMi( FILE *fp, const MI_BLOCK * pMi, uchar stream )
{
   if (pMi != NULL)
   {
      fprintf(fp, "MI:%d %04x #d=%d %s\n",
                  stream + 1,
                  pMi->block_no,
                  pMi->no_descriptors,
                  ((pMi->off_message != 0) ? MI_GET_MESSAGE(pMi) : (uchar *) "")
             );
   }
}

// ---------------------------------------------------------------------------
// Print Language Information

static void EpgTxtDumpLi( FILE *fp, const LI_BLOCK * pLi, uchar stream )
{
   const LI_DESC *pLd;
   uint desc, lang;

   if (pLi != NULL)
   {
      fprintf(fp, "LI:%d %04x %d #desc=%d\n", stream + 1, pLi->block_no, pLi->netwop_no, pLi->desc_no);
      pLd = LI_GET_DESC(pLi);

      for (desc=0; desc < pLi->desc_no;desc++)
      {
         fprintf(fp, "     %d: ID=%d #lang=%d: ", desc, pLd[desc].id, pLd[desc].lang_count);

         for (lang=0; lang < pLd[desc].lang_count; lang++)
         {
            fprintf(fp, "%c%c%c,",
                        pLd[desc].lang[lang][0], pLd[desc].lang[lang][1], pLd[desc].lang[lang][2]);
         }
         fprintf(fp, "\n");
      }
   }
}

// ---------------------------------------------------------------------------
// Print Subtitle Information

static void EpgTxtDumpTi( FILE *fp, const TI_BLOCK * pTi, uchar stream )
{
   const TI_DESC *pStd;
   uint desc, subt;

   if (pTi != NULL)
   {
      fprintf(fp, "TI:%d %04x %d #desc=%d\n", stream + 1, pTi->block_no, pTi->netwop_no, pTi->desc_no);
      pStd = TI_GET_DESC(pTi);

      for (desc=0; desc < pTi->desc_no; desc++)
      {
         fprintf(fp, "     %d: ID=%d #subt=%d: ", desc, pStd[desc].id, pStd[desc].subt_count);

         for (subt=0; subt < pStd[desc].subt_count; subt++)
         {
            fprintf(fp, "%03X.%04X=%c%c%c,",
                        pStd[desc].subt[subt].page,
                        pStd[desc].subt[subt].subpage,
                        pStd[desc].subt[subt].lang[0],
                        pStd[desc].subt[subt].lang[1],
                        pStd[desc].subt[subt].lang[2]);
         }
         fprintf(fp, "\n");
      }
   }
}

// ---------------------------------------------------------------------------
// Print Bundle Information

static void EpgTxtDumpBi( FILE *fp, const BI_BLOCK * pBi, uchar stream )
{
   if (pBi != NULL)
   {
      fprintf(fp, "BI:0 EPG app-ID=%d\n", pBi->app_id);
   }
}

// ---------------------------------------------------------------------------
// Dump a single block of unknown type
//
void EpgTxtDump_UnknownBlock( BLOCK_TYPE type, uint size, uchar stream )
{
   if (epgTxtListBlocks)
   {
      fprintf(stdout, "Other: block type 0x%02X, stream=%d, size=%d\n", type, (int)stream, size);
   }
}

// ---------------------------------------------------------------------------
// Switch dump on or off
//
void EpgTxtDump_Toggle( void )
{
   epgTxtListBlocks = ! epgTxtListBlocks;
}

// ---------------------------------------------------------------------------
// Dump a single block
//
void EpgTxtDump_Block( const EPGDB_BLOCK_UNION * pUnion, BLOCK_TYPE type, uchar stream )
{
   if (pUnion != NULL)
   {
      if (epgTxtListBlocks)
      {
         switch (type)
         {
            case BLOCK_TYPE_BI:
               EpgTxtDumpBi(stdout, &pUnion->bi, stream);
               break;
            case BLOCK_TYPE_AI:
               EpgTxtDumpAi(stdout, &pUnion->ai, stream);
               break;
            case BLOCK_TYPE_PI:
               EpgTxtDumpPi(stdout, &pUnion->pi, stream, 0xff, NULL);
               break;
            case BLOCK_TYPE_NI:
               EpgTxtDumpNi(stdout, &pUnion->ni, stream);
               break;
            case BLOCK_TYPE_OI:
               EpgTxtDumpOi(stdout, &pUnion->oi, stream);
               break;
            case BLOCK_TYPE_MI:
               EpgTxtDumpMi(stdout, &pUnion->mi, stream);
               break;
            case BLOCK_TYPE_LI:
               EpgTxtDumpLi(stdout, &pUnion->li, stream);
               break;
            case BLOCK_TYPE_TI:
               EpgTxtDumpTi(stdout, &pUnion->ti, stream);
               break;
            default:
               debug1("EpgTxtDump-Block: unknown block type %d\n", type);
               break;
         }
      }
   }
   else
      debug0("EpgTxtDump-Block: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Dump the complete database
//
void EpgTxtDump_Database( EPGDB_CONTEXT *pDbContext, FILE *fp,
                          bool do_pi, bool do_xi, bool do_ai, bool do_ni,
                          bool do_oi, bool do_mi, bool do_li, bool do_ti )
{
   const AI_BLOCK * pAi;
   const NI_BLOCK * pNi;
   const OI_BLOCK * pOi;
   const MI_BLOCK * pMi;
   const LI_BLOCK * pLi;
   const TI_BLOCK * pTi;
   const PI_BLOCK * pPi;
   uint  blockno, count;
   uchar netwop;

   EpgDbLockDatabase(pDbContext, TRUE);

   // write file header
   fprintf(fp, "%s", pEpgTxtDumpHeader);

   pAi = EpgDbGetAi(pDbContext);
   if (pAi != NULL)
   {
      // Dump application information block
      if (do_ai)
         EpgTxtDumpAi(fp, pAi, 0);

      if (do_ni)
      {
         count = pAi->niCount + pAi->niCountSwo;
         for (blockno=1; blockno <= count; blockno++)
         {
            pNi = EpgDbGetNi(pDbContext, blockno);
            if (pNi != NULL)
               EpgTxtDumpNi(fp, pNi, EpgDbGetStream(pNi));
         }
      }

      // Dump OSD information blocks
      if (do_oi)
      {
         count = pAi->oiCount + pAi->oiCountSwo;
         for (blockno=1; blockno <= count; blockno++)
         {
            pOi = EpgDbGetOi(pDbContext, blockno);
            if (pOi != NULL)
               EpgTxtDumpOi(fp, pOi, EpgDbGetStream(pOi));
         }
      }

      // Dump message information blocks
      if (do_mi)
      {
         count = pAi->miCount + pAi->miCountSwo;
         for (blockno=1; blockno <= count; blockno++)
         {
            pMi = EpgDbGetMi(pDbContext, blockno);
            if (pMi != NULL)
               EpgTxtDumpMi(fp, pMi, EpgDbGetStream(pMi));
         }
      }

      // Dump language information blocks
      if (do_li)
      {
         pLi = EpgDbGetLi(pDbContext, 0x8000, 0);
         if (pLi != NULL)
            EpgTxtDumpLi(fp, pLi, EpgDbGetStream(pLi));

         for (netwop=0; netwop < pAi->netwopCount; netwop++)
         {
            pLi = EpgDbGetLi(pDbContext, 0, netwop);
            if (pLi != NULL)
               EpgTxtDumpLi(fp, pLi, EpgDbGetStream(pLi));
         }
      }

      // Dump subtitle information blocks
      if (do_ti)
      {
         pTi = EpgDbGetTi(pDbContext, 0x8000, 0);
         if (pTi != NULL)
            EpgTxtDumpTi(fp, pTi, EpgDbGetStream(pTi));

         for (netwop=0; netwop < pAi->netwopCount; netwop++)
         {
            pTi = EpgDbGetTi(pDbContext, 0, netwop);
            if (pTi != NULL)
               EpgTxtDumpTi(fp, pTi, EpgDbGetStream(pTi));
         }
      }

      // Dump programme information blocks
      if (do_pi)
      {
         pPi = EpgDbSearchFirstPi(pDbContext, NULL);
         while (pPi != NULL)
         {
            EpgTxtDumpPi(fp, pPi, EpgDbGetStream(pPi), EpgDbGetVersion(pPi), pAi);
            pPi = EpgDbSearchNextPi(pDbContext, NULL, pPi);
         }
      }

      // Dump defective programme information blocks
      if (do_xi)
      {
         pPi = EpgDbGetFirstObsoletePi(pDbContext);
         while (pPi != NULL)
         {
            EpgTxtDumpPi(fp, pPi, EpgDbGetStream(pPi), EpgDbGetVersion(pPi), pAi);
            pPi = EpgDbGetNextObsoletePi(pDbContext, pPi);
         }
      }
   }
   EpgDbLockDatabase(pDbContext, FALSE);
}

