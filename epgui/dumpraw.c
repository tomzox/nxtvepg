/*
 *  Nextview EPG database raw dump
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
 *    Dumps the information contained in every incoming
 *    Nextview block as ASCII to stdout. This is very useful for
 *    debugging the transmitted data (which often contains errors
 *    or inconsistancies, like overlapping running times)
 *
 *  Author: Tom Zoerner
 *
 *  $Id: dumpraw.c,v 1.29 2004/08/07 14:13:32 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbmerge.h"
#include "epgui/epgmain.h"
#include "epgui/pdc_themes.h"
#include "epgui/pibox.h"
#include "epgui/dumpraw.h"


#define NETNAME_LENGTH 6
#define NETNAME_LENGTH_STR "6"
#define NETNAME_LENGTH0 (NETNAME_LENGTH+1)

// ----------------------------------------------------------------------------
// status variable: turns output on or off
// - can be toggled by user interface or command line argument
//
static bool dumpRawIncoming;
static const char * const pDumpRawHeader = "Nextview ASCII Dump\n";

// ----------------------------------------------------------------------------
// Print PI

// ----------------------------------------------------------------------------
// Decode PIL code into date and time of day
//
static char * GetPiPilStr( const PI_BLOCK * pPi, uchar * pOutStr, uint maxlen )
{
   struct tm vpsTime;

   if (EpgDbGetVpsTimestamp(&vpsTime, pPi->pil, pPi->start_time))
   {
      strftime(pOutStr, maxlen, "%Y-%m-%d.%H:%M:%S", &vpsTime);
   }
   else
      strncpy(pOutStr, "INVAL", maxlen);

   return pOutStr;
}

// ---------------------------------------------------------------------------
// Print Programme Information

static void EpgDumpRaw_Pi( FILE *fp, const PI_BLOCK * pPi, uchar stream, uchar version, const AI_BLOCK * pAi )
{
   const uchar * pThemeStr;
   const uchar * pGeneralStr;
   uchar start_str[40], stop_str[20];
   uchar pilStr[50];
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
                  GetPiPilStr(pPi, pilStr, sizeof(pilStr)),
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

static void EpgDumpRaw_Ai( FILE *fp, const AI_BLOCK * pAi, uchar stream )
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

static void EpgDumpRaw_Oi( FILE *fp, const OI_BLOCK * pOi, uchar stream )
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

static void EpgDumpRaw_Ni( FILE *fp, const NI_BLOCK * pNi, uchar stream )
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
                        debug3("EpgDumpRaw_Ni: unknown kind %d in attrib %d of event %d", pEv[i].unit[j].kind, j, i);
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

static void EpgDumpRaw_Mi( FILE *fp, const MI_BLOCK * pMi, uchar stream )
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

static void EpgDumpRaw_Li( FILE *fp, const LI_BLOCK * pLi, uchar stream )
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

static void EpgDumpRaw_Ti( FILE *fp, const TI_BLOCK * pTi, uchar stream )
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

static void EpgDumpRaw_Bi( FILE *fp, const BI_BLOCK * pBi, uchar stream )
{
   if (pBi != NULL)
   {
      fprintf(fp, "BI:0 EPG app-ID=%d\n", pBi->app_id);
   }
}

// ---------------------------------------------------------------------------
// Dump a single block of unknown type
//
void EpgDumpRaw_IncomingUnknown( BLOCK_TYPE type, uint size, uchar stream )
{
   if (dumpRawIncoming)
   {
      fprintf(stdout, "Other: block type 0x%02X, stream=%d, size=%d\n", type, (int)stream, size);
   }
}

// ---------------------------------------------------------------------------
// Dump a single block
//
void EpgDumpRaw_IncomingBlock( const EPGDB_BLOCK_UNION * pUnion, BLOCK_TYPE type, uchar stream )
{
   if (pUnion != NULL)
   {
      if (dumpRawIncoming)
      {
         switch (type)
         {
            case BLOCK_TYPE_BI:
               EpgDumpRaw_Bi(stdout, &pUnion->bi, stream);
               break;
            case BLOCK_TYPE_AI:
               EpgDumpRaw_Ai(stdout, &pUnion->ai, stream);
               break;
            case BLOCK_TYPE_PI:
               EpgDumpRaw_Pi(stdout, &pUnion->pi, stream, 0xff, NULL);
               break;
            case BLOCK_TYPE_NI:
               EpgDumpRaw_Ni(stdout, &pUnion->ni, stream);
               break;
            case BLOCK_TYPE_OI:
               EpgDumpRaw_Oi(stdout, &pUnion->oi, stream);
               break;
            case BLOCK_TYPE_MI:
               EpgDumpRaw_Mi(stdout, &pUnion->mi, stream);
               break;
            case BLOCK_TYPE_LI:
               EpgDumpRaw_Li(stdout, &pUnion->li, stream);
               break;
            case BLOCK_TYPE_TI:
               EpgDumpRaw_Ti(stdout, &pUnion->ti, stream);
               break;
            default:
               debug1("EpgDumpRaw-Block: unknown block type %d\n", type);
               break;
         }
      }
   }
   else
      debug0("EpgDumpRaw-Block: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Switch dump on or off
//
void EpgDumpRaw_Toggle( void )
{
   dumpRawIncoming = ! dumpRawIncoming;
}

// ----------------------------------------------------------------------------
// Helper func: read boolean from global Tcl var
//
static bool EpgDumpRaw_ReadTclBool( Tcl_Interp *interp,
                                    CONST84 char * pName, bool fallbackVal )
{
   Tcl_Obj  * pVarObj;
   int  value;

   if (pName != NULL)
   {
      pVarObj = Tcl_GetVar2Ex(interp, pName, NULL, TCL_GLOBAL_ONLY);
      if ( (pVarObj == NULL) ||
           (Tcl_GetBooleanFromObj(interp, pVarObj, &value) != TCL_OK) )
      {
         debug3("EpgDumpRaw-ReadTclBool: cannot read Tcl var %s (%s) - use default val %d", pName, ((pVarObj != NULL) ? Tcl_GetString(pVarObj) : "*undef*"), fallbackVal);
         value = fallbackVal;
      }
   }
   else
   {
      fatal0("EpgDumpRaw-ReadTclBool: illegal NULL ptr param");
      value = fallbackVal;
   }
   return (bool) value;
}

// ---------------------------------------------------------------------------
// Dump the complete database
//
static void EpgDumpRaw_Database( EPGDB_CONTEXT *pDbContext, FILE *fp )
{
   bool  do_pi, do_xi, do_ai, do_ni;
   bool  do_oi, do_mi, do_li, do_ti;
   const AI_BLOCK * pAi;
   const NI_BLOCK * pNi;
   const OI_BLOCK * pOi;
   const MI_BLOCK * pMi;
   const LI_BLOCK * pLi;
   const TI_BLOCK * pTi;
   const PI_BLOCK * pPi;
   uint  blockno, count;
   uchar netwop;

   do_pi = EpgDumpRaw_ReadTclBool(interp, "dumpdb_pi", 1);
   do_xi = EpgDumpRaw_ReadTclBool(interp, "dumpdb_xi", 1);
   do_ai = EpgDumpRaw_ReadTclBool(interp, "dumpdb_ai", 1);
   do_ni = EpgDumpRaw_ReadTclBool(interp, "dumpdb_ni", 1);
   do_oi = EpgDumpRaw_ReadTclBool(interp, "dumpdb_oi", 1);
   do_mi = EpgDumpRaw_ReadTclBool(interp, "dumpdb_mi", 1);
   do_li = EpgDumpRaw_ReadTclBool(interp, "dumpdb_li", 1);
   do_ti = EpgDumpRaw_ReadTclBool(interp, "dumpdb_ti", 1);

   EpgDbLockDatabase(pDbContext, TRUE);

   // write file header
   fprintf(fp, "%s", pDumpRawHeader);

   pAi = EpgDbGetAi(pDbContext);
   if (pAi != NULL)
   {
      // Dump application information block
      if (do_ai)
         EpgDumpRaw_Ai(fp, pAi, 0);

      if (do_ni)
      {
         count = pAi->niCount + pAi->niCountSwo;
         for (blockno=1; blockno <= count; blockno++)
         {
            pNi = EpgDbGetNi(pDbContext, blockno);
            if (pNi != NULL)
               EpgDumpRaw_Ni(fp, pNi, EpgDbGetStream(pNi));
         }
      }

      // Dump OSD information blocks
      if (do_oi)
      {
         count = pAi->oiCount + pAi->oiCountSwo;
         // note: block #0 has a special meaning and may not be transmitted
         //       hence it may not be included in the count, hence the <=
         for (blockno=0; blockno <= count; blockno++)
         {
            pOi = EpgDbGetOi(pDbContext, blockno);
            if (pOi != NULL)
               EpgDumpRaw_Oi(fp, pOi, EpgDbGetStream(pOi));
         }
      }

      // Dump message information blocks
      if (do_mi)
      {
         count = pAi->miCount + pAi->miCountSwo;
         for (blockno=0; blockno <= count; blockno++)
         {
            pMi = EpgDbGetMi(pDbContext, blockno);
            if (pMi != NULL)
               EpgDumpRaw_Mi(fp, pMi, EpgDbGetStream(pMi));
         }
      }

      // Dump language information blocks
      if (do_li)
      {
         pLi = EpgDbGetLi(pDbContext, 0x8000, 0);
         if (pLi != NULL)
            EpgDumpRaw_Li(fp, pLi, EpgDbGetStream(pLi));

         for (netwop=0; netwop < pAi->netwopCount; netwop++)
         {
            pLi = EpgDbGetLi(pDbContext, 0, netwop);
            if (pLi != NULL)
               EpgDumpRaw_Li(fp, pLi, EpgDbGetStream(pLi));
         }
      }

      // Dump subtitle information blocks
      if (do_ti)
      {
         pTi = EpgDbGetTi(pDbContext, 0x8000, 0);
         if (pTi != NULL)
            EpgDumpRaw_Ti(fp, pTi, EpgDbGetStream(pTi));

         for (netwop=0; netwop < pAi->netwopCount; netwop++)
         {
            pTi = EpgDbGetTi(pDbContext, 0, netwop);
            if (pTi != NULL)
               EpgDumpRaw_Ti(fp, pTi, EpgDbGetStream(pTi));
         }
      }

      // Dump programme information blocks
      if (do_pi)
      {
         pPi = EpgDbSearchFirstPi(pDbContext, NULL);
         while (pPi != NULL)
         {
            EpgDumpRaw_Pi(fp, pPi, EpgDbGetStream(pPi), EpgDbGetVersion(pPi), pAi);
            pPi = EpgDbSearchNextPi(pDbContext, NULL, pPi);
         }
      }

      // Dump defective programme information blocks
      if (do_xi)
      {
         pPi = EpgDbGetFirstObsoletePi(pDbContext);
         while (pPi != NULL)
         {
            EpgDumpRaw_Pi(fp, pPi, EpgDbGetStream(pPi), EpgDbGetVersion(pPi), pAi);
            pPi = EpgDbGetNextObsoletePi(pDbContext, pPi);
         }
      }
   }
   EpgDbLockDatabase(pDbContext, FALSE);
}

// ----------------------------------------------------------------------------
// Dump the complete database (invoked via command line)
//
void EpgDumpRaw_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp )
{
   EpgDumpRaw_Database(pDbContext, fp);
}

// ----------------------------------------------------------------------------
// Dump the complete database (invoked via GUI)
//
static int EpgDumpRaw_DumpRawDatabase( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_DumpRawDatabase <file-name>";
   const char * pFileName;
   Tcl_DString ds;
   FILE *fp;
   int result;

   if (objc != 1+1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( (pFileName = Tcl_GetString(objv[1])) == NULL )
   {  // internal error: can not get filename string
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (Tcl_GetCharLength(objv[1]) > 0)
      {
         pFileName = Tcl_UtfToExternalDString(NULL, pFileName, -1, &ds);
         fp = fopen(pFileName, "w");
         if (fp == NULL)
         {  // access, create or truncate failed -> inform the user
            sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumpdb -message \"Failed to open file '%s' for writing: %s\"",
                          Tcl_GetString(objv[1]), strerror(errno));
            eval_check(interp, comm);
            Tcl_ResetResult(interp);
         }
         Tcl_DStringFree(&ds);
      }
      else
         fp = stdout;

      if (fp != NULL)
      {
         EpgDumpRaw_Database(pUiDbContext, fp);

         if (fp != stdout)
            fclose(fp);
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Toggle dump of incoming PI
//
static int EpgDumpRaw_ToggleDumpStream( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ToggleDumpStream <boolean>";
   int value;
   int result;

   if (objc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetBooleanFromObj(interp, objv[1], &value))
   {  // string parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else
   {
      EpgDumpRaw_Toggle();
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Show debug info about the currently selected item in pop-up window
//
static int EpgDumpRaw_PopupPi( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_PopupPi <widget> <xcoo> <ycoo>";
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
   int index;
   int result;

   if (objc != 4)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = TCL_OK; 

      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      pPiBlock = PiBox_GetSelectedPi();
      if ((pAiBlock != NULL) && (pPiBlock != NULL))
      {
         pNetwop = AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no);

         sprintf(cni_str, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
         pCfNetname = Tcl_GetVar2(interp, "cfnetnames", cni_str, TCL_GLOBAL_ONLY);
         if (pCfNetname == NULL)
            pCfNetname = AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no);

         sprintf(ident, ".poppi_%d_%ld", pPiBlock->netwop_no, pPiBlock->start_time);

         sprintf(comm, "Create_PopupPi %s %s %s %s\n", ident,
                 Tcl_GetString(objv[1]), Tcl_GetString(objv[2]), Tcl_GetString(objv[3]));
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
// Free resources allocated by this module (cleanup during program exit)
//
void EpgDumpRaw_Destroy( void )
{
}

// ----------------------------------------------------------------------------
// Create the Tcl/Tk commands provided by this module
//
void EpgDumpRaw_Init( void )
{
   dumpRawIncoming = FALSE;

   Tcl_CreateObjCommand(interp, "C_ToggleDumpStream", EpgDumpRaw_ToggleDumpStream, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_DumpRawDatabase", EpgDumpRaw_DumpRawDatabase, (ClientData) NULL, NULL);

   Tcl_CreateObjCommand(interp, "C_PopupPi", EpgDumpRaw_PopupPi, (ClientData) NULL, NULL);
}

