/*
 *  XMLTV content processing
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
 *    This module contains the main entry point for parsing XMLTV files.
 *    The control flow is directly forwarded to the parser and scanner
 *    which in return invoke callback functions in this module for all
 *    recognized tokens, i.e. start (open) and close element tags,
 *    end of opening tag, attribute assignments and content data.
 *
 *    General explanation of callbacks:
 *
 *    Not all tags have the full set of callback functions, i.e. only those
 *    which are required to process the data are implemented. Which ones
 *    are required depends on the kind and organization of the data
 *    carried by the respective tag.  It also depends on the implementation
 *    of the callbacks below.  The callback functions are registered in
 *    the tag parser table in xmltv_tags.c; for unneeded callbacks a
 *    NULL pointer is registered.
 *
 *    The open function carries no data and just signals the start of
 *    processing of the respective tag.  It can be used to initialize the
 *    state machine which will process the data. Likewise, the close
 *    function just signals the end of processing for the tag, i.e. no
 *    more attributes or content will follow. At this point any temporarily
 *    stored data can be post-processed (if necessary) and committed to
 *    the database.
 *
 *    The attribute "set" functions are called in the order in which
 *    attribute assignments are found inside the tag in the XML file.
 *    (Note most of the attributes are declared optional in the XMLTV DTD,
 *    so the callbacks are not invoked for each tag; also the order of
 *    attributes inside a tag is undefined, i.e. depends on the generator
 *    of the XMLTV file; however an attribute may be defined at most once.)
 *
 *    The content data "add" function is called after all attributes have
 *    been processed. The parser already strips leading and trailing
 *    whitespace (for tags for which it's appropriate; in case of XMLTV
 *    that's all tags; trimming is enabled by a flag in the tables in
 *    xmltv_tags.c)  Also all escaped characters (&lt; for "<" etc.) are
 *    replaced.  Theoretically the data function could be called more than
 *    once if the content is interrupted by "child" tags; however the
 *    XMLTV DTD does not include such so-called mixed elements, so this
 *    will not occur.
 *
 *    The "attributes complete" callback is called when the opening tag
 *    is closed, i.e. inbetween processing attributes and content.  The
 *    callback function can return FALSE to indicate that the content and
 *    all child elements shuld be discarded at parser level. This feature
 *    can be used to skip over unwanted data; it's not used here though.
 *
 *    Note the same callbacks are used both for DTD version 0.5 and 0.6
 *    whenever applicable. In many cases information which was carried as
 *    attribute in DTD 0.5 is carries as content in 0.6 and vice versa.
 *    This explains some of the oddities below.
 *
 *    Building the programme database:
 *
 *    The callback functions fill temporary structures with programme
 *    parameters and data (or timeslot data respectively for XMLTV DTD 0.6)  
 *    The structures are forwarded to a database when the tag is closed.
 *    Also a channel table is generated and forwarded to the database at
 *    the end of the file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: xmltv_db.c,v 1.18 2011/01/05 19:29:38 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_XMLTV
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbmgmt.h"
#include "epgui/pidescr.h"

#include "xmltv/xml_cdata.h"
#include "xmltv/xml_hash.h"
#include "xmltv/xmltv_timestamp.h"
#include "xmltv/xmltv_themes.h"
#include "xmltv/xmltv_tags.h"
#include "xmltv/xmltv_cni.h"
#include "xmltv/xmltv_db.h"


typedef struct
{
   char       * p_disp_name;
   time_t       pi_min_time;
   time_t       pi_max_time;
   uint         cni;
} XMLTV_CHN;

typedef enum
{
   XMLTV_CAT_SYS_NONE,
   XMLTV_CAT_SYS_PDC,
   XMLTV_CAT_SYS_SORTCRIT
} XMLTV_CAT_SYS;

typedef enum
{
   XMLTV_CODE_NONE,
   XMLTV_CODE_VPS,
   XMLTV_CODE_PDC,
   XMLTV_CODE_SV,
   XMLTV_CODE_VP
} XMLTV_CODE_TIME_SYS;

typedef enum
{
   XMLTV_RATING_NONE,
   XMLTV_RATING_AGE,
   XMLTV_RATING_FSK,
   XMLTV_RATING_BBFC,
   XMLTV_RATING_MPAA
} XMLTV_RATING_SYS;

#define XMLTV_PI_CAT_MAX  40   // 3 languages

typedef struct
{
   uint         chn_count;
   uint         chn_tab_size;
   bool         chn_open;
   char       * p_chn_id_tmp;
   XMLTV_CHN  * p_chn_table;
   XML_HASH_PTR pChannelHash;

   XML_STR_BUF  source_info_name;
   XML_STR_BUF  source_info_url;
   XML_STR_BUF  source_data_url;
   XML_STR_BUF  gen_info_name;
   XML_STR_BUF  gen_info_url;
   XML_STR_BUF  * link_text_dest;
   XML_STR_BUF  * link_href_dest;

   XML_HASH_PTR pThemeHash;
   PI_BLOCK     pi;
   XML_STR_BUF  pi_title;
   XML_STR_BUF  pi_desc;
   XML_STR_BUF  pi_credits;
   XML_STR_BUF  pi_actors;

   uint         audio_cnt;
   uint         pi_aspect_x;
   uint         pi_aspect_y;
   uint         pi_star_rating_val;
   uint         pi_star_rating_max;
   XMLTV_CAT_SYS pi_cat_system;
   uint         pi_cat_code;
   uchar        pi_cat_count;
   uchar        pi_cats[XMLTV_PI_CAT_MAX];
   XMLTV_RATING_SYS pi_rating_sys;
   XMLTV_CODE_TIME_SYS pi_code_time_sys;
   XML_STR_BUF  pi_code_time_str;
   XML_STR_BUF  pi_code_sv;
   XML_STR_BUF  pi_code_vp;

   XMLTV_DTD_VERSION dtd;
   EPGDB_CONTEXT * pDbContext;
   bool         isPeek;
   bool         cniCtxInitDone;
   XMLTV_CNI_CTX cniCtx;
} PARSE_STATE;

static PARSE_STATE xds;

#ifndef VPS_PIL_CODE_SYSTEM
#define VPS_PIL_CODE_SYSTEM ((0 << 15) | (15 << 11) | (31 << 6) | 63)
#endif

// ----------------------------------------------------------------------------
// Match theme category string onto PDC theme categories
// - the passed string is converted into lower-case
// - themes are added to the PI in the state struct
//
static void Xmltv_ParseThemeString( char * pStr, XML_LANG_CODE lang )
{
   HASHED_THEMES * pCache;
   bool isNew;
   char * p;

   pCache = XmlHash_CreateEntry(xds.pThemeHash, pStr, &isNew);

   if (isNew)
   {
      // convert given theme category name to lower-case (Latin-1 only)
      p = pStr;
      while (*p != 0)
      {
         if (alphaNumTab[(uchar) *p] == ALNUM_UCHAR)
         {
            *p = tolower(*p);
         }
         p++;
      }

      // map theme strings against pre-defined PDC theme category names
      // note the result is cached inside the hash entries' payload storage
      switch (lang)
      {
         case XML_LANG_DE:
            Xmltv_ParseThemeStringGerman(pCache, pStr);
            break;
         case XML_LANG_FR:
            Xmltv_ParseThemeStringFrench(pCache, pStr);
            break;
         case XML_LANG_EN:
            Xmltv_ParseThemeStringEnglish(pCache, pStr);
            break;
         case XML_LANG_UNKNOWN:
         default:
            // language unknown -> try all languages until a match is found
            Xmltv_ParseThemeStringGerman(pCache, pStr);
            if (pCache->cat == 0)
               Xmltv_ParseThemeStringFrench(pCache, pStr);
            if (pCache->cat == 0)
               Xmltv_ParseThemeStringEnglish(pCache, pStr);
            break;
      }
   }

   // copy theme codes into cache
   if ((pCache->cat != 0) && (xds.pi_cat_count < XMLTV_PI_CAT_MAX))
   {
      xds.pi_cats[xds.pi_cat_count] = pCache->cat;
      xds.pi_cat_count += 1;

      if ((pCache->theme != 0) && (xds.pi_cat_count < XMLTV_PI_CAT_MAX))
      {
         xds.pi_cats[xds.pi_cat_count] = pCache->theme;
         xds.pi_cat_count += 1;
      }
   }
}

// ----------------------------------------------------------------------------
// Parse timestamp
//
static time_t XmltvDb_ParseTimestamp( char * pStr, uint len )
{
   time_t  tval;

   if (xds.dtd == XMLTV_DTD_6)
   {
      tval = parse_xmltv_date_v6(pStr, len);
   }
   else
   {
      tval = parse_xmltv_date_v5(pStr, len);
   }

   ifdebug1(tval == 0, "Xmltv-ParseTimestamp: parse error '%s'", pStr);

   return tval;
}

// ----------------------------------------------------------------------------
// Parse VPS/PDC timestamp
// - the value is in local time
// - the difference to regular timestamps is the returned format
//
static uint XmltvDb_ParseVpsPdc( const char * pStr )
{
   uint  month, mday, hour, minute;
   uint  pil;
   int nscan;
   int scan_pos;

   pil = VPS_PIL_CODE_SYSTEM;
   if (xds.dtd == XMLTV_DTD_6)
   {
      scan_pos = 0;
      // note: trailing 'Z' is omitted since VPS/PDC is localtime
      nscan = sscanf(pStr, "%*4u-%2u-%2uT%2u:%2u:%*2u%n", &month, &mday, &hour, &minute, &scan_pos);
      if ((nscan >= 4 /*6?*/) && (pStr[scan_pos] == 0))
      {
         // silently map invalid PIL time codes to the standard VPS system code
         if ( (minute < 60) && (hour < 24) &&
              (mday != 0) && (mday <= 31) && (month != 0) && (month <= 12) )
         {
            pil = (mday << 15) | (month << 11) | (hour << 6) | minute;
         }
      }
      else
         debug3("Xmltv-ParseVpsPdc: parse error '%s' after %d tokens (around char #%d)", pStr, nscan, scan_pos);
   }
   else
   {
      scan_pos = 0;
      // note: ignoring timezone since VPS/PDC should always be localtime
      nscan = sscanf(pStr, "%*4u%2u%2u%2u%2u%*2u%n", &month, &mday, &hour, &minute, &scan_pos);
      if ((nscan >= 4 /*6?*/) && ((pStr[scan_pos] == 0) || (pStr[scan_pos] == ' ')))
      {
         // silently map invalid PIL time codes to the standard VPS system code
         if ( (minute < 60) && (hour < 24) &&
              (mday != 0) && (mday <= 31) && (month != 0) && (month <= 12) )
         {
            pil = (mday << 15) | (month << 11) | (hour << 6) | minute;
         }
      }
      else
         debug3("Xmltv-ParseVpsPdc: parse error '%s' after %d tokens (around char #%d)", pStr, nscan, scan_pos);
   }
   return pil;
}

// ----------------------------------------------------------------------------
// Sort, unify and copy theme categories to PI's data struct
// - unify means removing redudant values
//
static void XmltvDb_PiAssignThemeCategories( void )
{
   uint  last;
   uint  cur;
   uint  idx;

   last = 0;
   while (1)
   {
      // search for the smallest remaining theme code (well yes, we're bubble sorting)
      cur = 0x100;
      for (idx = 0; idx < xds.pi_cat_count; idx++)
         if ((xds.pi_cats[idx] > last) && (xds.pi_cats[idx] < cur))
            cur = xds.pi_cats[idx];

      // if none found of the target theme list is full, we quit
      if (cur == 0x100)
         break;
      if (xds.pi.no_themes >= PI_MAX_THEME_COUNT)
         break;

      // append the theme code to the PI's theme array
      xds.pi.themes[xds.pi.no_themes] = cur;
      xds.pi.no_themes += 1;

      last = cur;
   }
}

// ----------------------------------------------------------------------------
// Generate service name for AI block (should be one line only)
//
static void XmlTv_BuildAiServiceName( XML_STR_BUF * pServiceName, const char * pProvName )
{
   //uchar * p;

   memset(pServiceName, 0, sizeof(*pServiceName));
   XmlCdata_AppendRaw(pServiceName, pProvName, strlen(pProvName));

#if 0
   // only use the first line
   p = strchr(XML_STR_BUF_GET_STR(*pServiceName), '\n');
   if (p != NULL)
      *p = 0;
   // replace control chars with space
   for (p = XML_STR_BUF_GET_STR(*pServiceName); *p != 0; p++)
   {
      if (*p < ' ')
         *p = ' ';
   }
#endif
}

// ----------------------------------------------------------------------------
// Generate service description for OI block
//
static void XmlTv_BuildOiMessages( XML_STR_BUF * pHeader, XML_STR_BUF * pMessage )
{
   memset(pHeader, 0, sizeof(*pHeader));
   memset(pMessage, 0, sizeof(*pMessage));

   // header
   XmlCdata_AppendRaw(pHeader, XML_STR_BUF_GET_STR(xds.source_info_name), XML_STR_BUF_GET_STR_LEN(xds.source_info_name));

   if (XML_STR_BUF_GET_STR_LEN(xds.source_info_url) > 0)
   {
      XmlCdata_AppendParagraph(pHeader, TRUE);
   }
   XmlCdata_AppendRaw(pMessage, XML_STR_BUF_GET_STR(xds.source_info_url), XML_STR_BUF_GET_STR_LEN(xds.source_info_url));

   // message
   XmlCdata_AppendRaw(pMessage, XML_STR_BUF_GET_STR(xds.source_data_url), XML_STR_BUF_GET_STR_LEN(xds.source_data_url));

   if ( (XML_STR_BUF_GET_STR_LEN(xds.gen_info_name) > 0) ||
        (XML_STR_BUF_GET_STR_LEN(xds.gen_info_url) > 0) )
   {
      XmlCdata_AppendParagraph(pMessage, TRUE);
      XmlCdata_AppendString(pMessage, "Generator: ");
      XmlCdata_AppendRaw(pMessage, XML_STR_BUF_GET_STR(xds.gen_info_name), XML_STR_BUF_GET_STR_LEN(xds.gen_info_name));
      XmlCdata_AppendParagraph(pMessage, FALSE);
      XmlCdata_AppendRaw(pMessage, XML_STR_BUF_GET_STR(xds.gen_info_url), XML_STR_BUF_GET_STR_LEN(xds.gen_info_url));
   }
}

// ----------------------------------------------------------------------------
// Assemble an OI block (OSD information)
//
static EPGDB_BLOCK * XmltvDb_BuildOi( void )
{
   EPGDB_BLOCK *pBlk;
   OI_BLOCK *pOi;
   uint blockLen;
   XML_STR_BUF oiHeader;
   XML_STR_BUF oiMessage;

   XmlTv_BuildOiMessages(&oiHeader, &oiMessage);

   // concatenate the various parts of the block to a compound structure
   // 1st step: sum up the length & compute the offsets of each element from the start
   blockLen = sizeof(OI_BLOCK) +
              XML_STR_BUF_GET_STR_LEN(oiHeader) + 1 +
              XML_STR_BUF_GET_STR_LEN(oiMessage) + 1;

   // 2nd step: copy elements one after each other, then free single elements
   pBlk = EpgBlockCreate(BLOCK_TYPE_OI, blockLen);
   pOi = (OI_BLOCK *) &pBlk->blk.oi;
   memset(pOi, 0, sizeof(OI_BLOCK));
   pOi->block_no = 0;
   blockLen = sizeof(OI_BLOCK);

   pOi->off_header = blockLen;
   pOi->header_size = XML_STR_BUF_GET_STR_LEN(oiHeader);
   strcpy((void *) OI_GET_HEADER(pOi), XML_STR_BUF_GET_STR(oiHeader));
   blockLen += XML_STR_BUF_GET_STR_LEN(oiHeader) + 1;

   pOi->off_message = blockLen;
   pOi->msg_size = XML_STR_BUF_GET_STR_LEN(oiMessage);
   strcpy((void *) OI_GET_MESSAGE(pOi), XML_STR_BUF_GET_STR(oiMessage));
   blockLen += XML_STR_BUF_GET_STR_LEN(oiMessage) + 1;

   XmlCdata_Free(&oiHeader);
   XmlCdata_Free(&oiMessage);
   assert(blockLen == pBlk->size);

   return(pBlk);
}

// ----------------------------------------------------------------------------
// Assemble an AI block (application inventory, i.e. channel table)
//
static EPGDB_BLOCK * XmltvDb_BuildAi( const char * pProvName )
{
   EPGDB_BLOCK *pBlk;
   AI_BLOCK *pAi;
   AI_NETWOP *pNetwops;
   uchar idx;
   XML_STR_BUF aiServiceName;
   uint  netNameLenSum;
   uint  blockLen;

   netNameLenSum = 0;
   for (idx = 0; idx < xds.chn_count; idx++)
   {
      netNameLenSum += strlen(xds.p_chn_table[idx].p_disp_name) + 1;
   }
   XmlTv_BuildAiServiceName(&aiServiceName, pProvName);

   blockLen = sizeof(AI_BLOCK) +
              (xds.chn_count * sizeof(AI_NETWOP)) +
              (XML_STR_BUF_GET_STR_LEN(aiServiceName) + 1) +
              netNameLenSum;
   pBlk = EpgBlockCreate(BLOCK_TYPE_AI, blockLen);
   pAi = (AI_BLOCK *) &pBlk->blk.ai;  // remove const from pointer
   memset(pAi, 0, sizeof(AI_BLOCK));
   pAi->netwopCount = xds.chn_count;
   pAi->thisNetwop = 0;               // unused for XMLTV sources
   blockLen = sizeof(AI_BLOCK);

   pAi->off_netwops = blockLen;
   pNetwops = (AI_NETWOP *) AI_GET_NETWOPS(pAi);  // cast to remove const
   memset(pNetwops, 0, pAi->netwopCount * sizeof(AI_NETWOP));
   blockLen += pAi->netwopCount * sizeof(AI_NETWOP);

   pAi->oiCount = 1;
   pAi->off_serviceNameStr = blockLen;
   strcpy((void *) AI_GET_SERVICENAME(pAi), XML_STR_BUF_GET_STR(aiServiceName));
   blockLen += XML_STR_BUF_GET_STR_LEN(aiServiceName) + 1;
   XmlCdata_Free(&aiServiceName);

   for (idx = 0; idx < xds.chn_count; idx++, pNetwops++)
   {
      pNetwops->netCni = xds.p_chn_table[idx].cni & XMLTV_NET_CNI_MASK;
      pNetwops->netCniMSB = xds.p_chn_table[idx].cni >> 16;
      pNetwops->lto = 120; // TODO
      pNetwops->dayCount = (xds.p_chn_table[idx].pi_max_time -
                            xds.p_chn_table[idx].pi_min_time + 23*60*60) / (24*60*60);
      pNetwops->alphabet = 1; // TODO

      pNetwops->startNo = 0x0000;
      pNetwops->stopNo = 0xffff;
      pNetwops->stopNoSwo = 0xffff;

      pNetwops->off_name = blockLen;
      strcpy((char *) pAi + blockLen, xds.p_chn_table[idx].p_disp_name);
      blockLen += strlen(xds.p_chn_table[idx].p_disp_name) + 1;
   }

   assert(blockLen == pBlk->size);

   return pBlk;
}

// ----------------------------------------------------------------------------
// Insert a new PI block into the database
// - the block is dropped if it overlaps with other PI of the same network
//
static bool XmltvDb_AddPiBlock( EPGDB_CONTEXT * dbc, EPGDB_BLOCK *pBlock )
{
   EPGDB_BLOCK *pWalk, *pPrev;
   uchar netwop;
   bool added;

   // when stop time is missing, fake it as start + 1 second (XXX FIXME choose start of next PI)
   if (pBlock->blk.pi.stop_time == 0)
      *(time32_t*)&pBlock->blk.pi.stop_time = pBlock->blk.pi.start_time + 1;

   if ( (pBlock->blk.pi.start_time != 0) &&
        (pBlock->blk.pi.start_time < pBlock->blk.pi.stop_time) )
   {
      netwop = pBlock->blk.pi.netwop_no;
      if ((netwop < xds.chn_count) && (netwop < MAX_NETWOP_COUNT))
      {
         dprintf4("ADD PI ptr=%lx: netwop=%d, blockno=%d, start=%ld\n", (ulong)pBlock, netwop, pBlock->blk.pi.block_no, pBlock->blk.pi.start_time);

         // search inside network chain for insertion point
         pWalk = dbc->pFirstNetwopPi[netwop];
         pPrev = NULL;

         while ( (pWalk != NULL) &&
                 (pWalk->blk.pi.start_time < pBlock->blk.pi.start_time) )
         {
            pPrev = pWalk;
            pWalk = pWalk->pNextNetwopBlock;
         }

         if ( ( (pWalk == NULL) ||
                (pWalk->blk.pi.start_time >= pBlock->blk.pi.stop_time) ) &&
              ( (pPrev == NULL) ||
                (pPrev->blk.pi.stop_time <= pBlock->blk.pi.start_time) ) )
         {
            assert((pPrev == NULL) || (pPrev->pNextNetwopBlock == pWalk));
            assert((pWalk == NULL) || (pWalk->pPrevNetwopBlock == pPrev));

            EpgDbLinkPi(dbc, pBlock, pPrev, pWalk);

            added = TRUE;
         }
         else
         {
            dprintf0("OVERLAP - dropping PI\n");
            added = FALSE;
         }
      }
      else
      {  // undefined network
         debug0("DEFECTIVE");
         added = FALSE;
      }
   }
   else
   {  // defective
      debug2("AddPi: invalid start or stop times: %d, %d", (int)pBlock->blk.pi.start_time, (int)pBlock->blk.pi.stop_time);
      added = FALSE;
   }

   return added;
}

// ----------------------------------------------------------------------------
// Assemble a PI block
//
static EPGDB_BLOCK * XmltvDb_BuildPi( void )
{
   EPGDB_BLOCK * pBlk;
   PI_BLOCK * pPi;
   uint  piLen;

   // concatenate the various parts of PI to a compound structure
   // 1st step: sum up the length & compute the offsets of each element from the start
   piLen = sizeof(PI_BLOCK);
   if (XML_STR_BUF_GET_STR_LEN(xds.pi_title) > 0)
   {
      xds.pi.off_title = piLen;
      piLen += XML_STR_BUF_GET_STR_LEN(xds.pi_title) + 1;
   }
   else
   {  // no title: set at least a 0-Byte
      xds.pi.off_title = piLen;
      piLen += 1;
   }
   if (XML_STR_BUF_GET_STR_LEN(xds.pi_desc) > 0)
   {
      xds.pi.off_short_info = piLen;
      piLen += XML_STR_BUF_GET_STR_LEN(xds.pi_desc) + 1;
   }
   else
      xds.pi.off_short_info = 0;

   // 2nd step: copy elements one after each other, then free single elements
   pBlk = EpgBlockCreate(BLOCK_TYPE_PI, piLen);
   pPi = (PI_BLOCK *) &pBlk->blk.pi;  // remove const from pointer
   memcpy(pPi, &xds.pi, sizeof(PI_BLOCK));

   strcpy((char *) PI_GET_TITLE(pPi), XML_STR_BUF_GET_STR(xds.pi_title));
   strcpy((char *) PI_GET_SHORT_INFO(pPi), XML_STR_BUF_GET_STR(xds.pi_desc));

   return pBlk;
}

// ----------------------------------------------------------------------------

// DTD 0.5 only (DTD 0.6 uses <link>)
void Xmltv_AboutSetSourceInfoUrl( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.source_info_url, pBuf);
}

// DTD 0.5 only (DTD 0.6 uses <link>)
void Xmltv_AboutSetSourceInfoName( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.source_info_name, pBuf);
}

void Xmltv_AboutSetSourceDataUrl( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.source_data_url, pBuf);
}

void Xmltv_SourceInfoOpen( void )
{
   xds.link_text_dest = &xds.source_info_name;
   xds.link_href_dest = &xds.source_info_url;
}

void Xmltv_SourceInfoClose( void )
{
   xds.link_text_dest = NULL;
   xds.link_href_dest = NULL;
}

void Xmltv_GenInfoOpen( void )
{
   xds.link_text_dest = &xds.gen_info_name;
   xds.link_href_dest = &xds.gen_info_url;
}

void Xmltv_GenInfoClose( void )
{
   xds.link_text_dest = NULL;
   xds.link_href_dest = NULL;
}

void Xmltv_AboutSetGenInfoName( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.gen_info_name, pBuf);
}

void Xmltv_AboutSetGenInfoUrl( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.gen_info_url, pBuf);
}

void Xmltv_LinkAddText( XML_STR_BUF * pBuf )
{
   if (xds.link_text_dest != NULL)
   {
      XmlCdata_AssignOrAppend(xds.link_text_dest, pBuf);
   }
}

void Xmltv_LinkHrefSet( XML_STR_BUF * pBuf )
{
   if (xds.link_href_dest != NULL)
   {
      XmlCdata_AssignOrAppend(xds.link_href_dest, pBuf);
   }
}

void Xmltv_LinkBlurbAddText( XML_STR_BUF * pBuf )
{
   if (xds.link_text_dest != NULL)
   {
      XmlCdata_AppendParagraph(xds.link_text_dest, TRUE);
      XmlCdata_AssignOrAppend(xds.link_text_dest, pBuf);
   }
}

// ----------------------------------------------------------------------------
// Add a new channel
//
void Xmltv_ChannelCreate( void )
{ 
   void * pTmp;
   uint size;

   assert(xds.chn_open == FALSE);

   if ((xds.p_chn_table == NULL) || (xds.chn_tab_size < xds.chn_count + 1))
   {
      size = (xds.chn_tab_size + 100) * sizeof(*xds.p_chn_table);
      pTmp = xmalloc(size);
      memset(pTmp, 0, size);
      if (xds.p_chn_table != NULL)
      {
         memcpy(pTmp, xds.p_chn_table, xds.chn_tab_size * sizeof(*xds.p_chn_table));
         xfree(xds.p_chn_table);
      }
      xds.p_chn_table = pTmp;
      // grow table in steps of 64 (64 should be enough for everyone)
      xds.chn_tab_size += 64;
   }
   xds.chn_open = TRUE;
}

void Xmltv_ChannelClose( void )
{
   bool   isNew;
   bool   result;
   uint * pChnIdx;

   assert(xds.chn_open);

   if (xds.chn_open)
   {
      result = FALSE;

      if (xds.cniCtxInitDone == FALSE)
      {
         // load the channel ID mapping table from file
         XmltvCni_MapInit(&xds.cniCtx, xds.pDbContext->provCni,
                          XML_STR_BUF_GET_STR(xds.source_info_name),
                          XML_STR_BUF_GET_STR(xds.source_data_url));
         xds.cniCtxInitDone = TRUE;
      }

      if ( (xds.p_chn_id_tmp != NULL) &&
           (xds.chn_count < MAX_NETWOP_COUNT) && // XXX FIXME should remove static limit on number of networks
           (xds.p_chn_table[xds.chn_count].p_disp_name != NULL) )
      {
         pChnIdx = XmlHash_CreateEntry(xds.pChannelHash, xds.p_chn_id_tmp, &isNew);
         if (isNew)
         {
            xds.p_chn_table[xds.chn_count].cni = XmltvCni_MapNetCni(&xds.cniCtx, xds.p_chn_id_tmp);

            dprintf4("Xmltv-ChannelClose: add channel #%d CNI 0x%04X '%s' (ID '%s')\n", xds.chn_count, xds.p_chn_table[xds.chn_count].cni, xds.p_chn_table[xds.chn_count].p_disp_name, xds.p_chn_id_tmp);

            xfree(xds.p_chn_id_tmp);
            xds.p_chn_id_tmp = NULL;

            *pChnIdx = xds.chn_count;
            xds.chn_count += 1;
            result = TRUE;
         }
         else
            debug2("Xmltv-ChannelClose: ID is not unique: '%s', display name '%s'", xds.p_chn_id_tmp, xds.p_chn_table[xds.chn_count].p_disp_name);
      }
      else
         debug2("Xmltv-ChannelClose: no ID or no display name (%lx,%lx)", (long)xds.p_chn_id_tmp, (long)xds.p_chn_table[xds.chn_count].p_disp_name);

      if (result == FALSE)
      {
         if (xds.p_chn_table[xds.chn_count].p_disp_name != NULL)
         {
            xfree(xds.p_chn_table[xds.chn_count].p_disp_name);
            xds.p_chn_table[xds.chn_count].p_disp_name = NULL;
         }
      }
      if (xds.p_chn_id_tmp != NULL)
      {
         xfree(xds.p_chn_id_tmp);
         xds.p_chn_id_tmp = NULL;
      }
      xds.chn_open = FALSE;
   }
   else
      fatal0("Xmltv-ChannelClose: not inside channel definition");
}

void Xmltv_ChannelSetId( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   assert(xds.chn_open);

   if (xds.chn_open)
   {
      if (xds.p_chn_id_tmp == NULL)
      {
         xds.p_chn_id_tmp = xstrdup(pStr);
      }
      else
         debug2("Xmltv-ChannelSetId: 2 channel IDs: %s and %s", xds.p_chn_id_tmp, pStr);
   }
}

void Xmltv_ChannelAddName( XML_STR_BUF * pBuf )
{
   assert(xds.chn_open);

   if (xds.chn_open)
   {
      if (xds.p_chn_table[xds.chn_count].p_disp_name == NULL)
      {
         xds.p_chn_table[xds.chn_count].p_disp_name = xstrdup(XML_STR_BUF_GET_STR(*pBuf));
      }
      else
         debug2("Xmltv-ChannelAddName: 2 channel IDs: %s and %s", xds.p_chn_table[xds.chn_count].p_disp_name, XML_STR_BUF_GET_STR(*pBuf));
   }
}

void Xmltv_ChannelAddUrl( XML_STR_BUF * pBuf )
{
}

// ----------------------------------------------------------------------------

void Xmltv_TsOpen( void )
{
   dprintf0("Xmltv-TsOpen\n");

   memset(&xds.pi, 0, sizeof(xds.pi));
   xds.pi.netwop_no = xds.chn_count;
   xds.pi.pil = VPS_PIL_CODE_SYSTEM;
   xds.audio_cnt = 0;
   xds.pi_cat_count = 0;

   XmlCdata_Reset(&xds.pi_title);
   XmlCdata_Reset(&xds.pi_desc);
   XmlCdata_Reset(&xds.pi_credits);
   XmlCdata_Reset(&xds.pi_actors);

   XmlCdata_Reset(&xds.pi_code_sv);
   XmlCdata_Reset(&xds.pi_code_vp);

   if (xds.isPeek)
   {
      XmlTags_ScanStop();
   }
}

void Xmltv_TsClose( void )
{
   EPGDB_BLOCK * pBlk;

   dprintf0("Xmltv_TsClose\n");

   if (xds.pi.netwop_no < xds.chn_count)
   {
      if ((xds.pi.start_time < xds.p_chn_table[xds.pi.netwop_no].pi_min_time) ||
          (xds.p_chn_table[xds.pi.netwop_no].pi_min_time == 0))
         xds.p_chn_table[xds.pi.netwop_no].pi_min_time = xds.pi.start_time;
      if (xds.pi.start_time > xds.p_chn_table[xds.pi.netwop_no].pi_max_time)
         xds.p_chn_table[xds.pi.netwop_no].pi_max_time = xds.pi.start_time;
   }

   // append credits section (director, actors, crew)
   if ( (XML_STR_BUF_GET_STR_LEN(xds.pi_actors) > 0) ||
        (XML_STR_BUF_GET_STR_LEN(xds.pi_credits) > 0) )
   {
      XmlCdata_AppendParagraph(&xds.pi_desc, FALSE);
      XmlCdata_AppendString(&xds.pi_desc, "Credits: ");

      if (XML_STR_BUF_GET_STR_LEN(xds.pi_credits) > 0)
      {
         XmlCdata_AppendRaw(&xds.pi_desc, XML_STR_BUF_GET_STR(xds.pi_credits), XML_STR_BUF_GET_STR_LEN(xds.pi_credits));
         if (XML_STR_BUF_GET_STR_LEN(xds.pi_actors) > 0)
            XmlCdata_AppendRaw(&xds.pi_desc, "; ", 2);
      }
      if (XML_STR_BUF_GET_STR_LEN(xds.pi_actors) > 0)
         XmlCdata_AppendRaw(&xds.pi_desc, XML_STR_BUF_GET_STR(xds.pi_actors), XML_STR_BUF_GET_STR_LEN(xds.pi_actors));
   }

   // append ShowV*ew and Video+ codes (for programming VCR)
   if ( (XML_STR_BUF_GET_STR_LEN(xds.pi_code_sv) > 0) ||
        (XML_STR_BUF_GET_STR_LEN(xds.pi_code_vp) > 0) )
   {
      XmlCdata_AppendParagraph(&xds.pi_desc, FALSE);
      if (XML_STR_BUF_GET_STR_LEN(xds.pi_code_sv) > 0)
      {
         XmlCdata_AppendString(&xds.pi_desc, "SV-Code: ");
         XmlCdata_AppendRaw(&xds.pi_desc, XML_STR_BUF_GET_STR(xds.pi_code_sv), XML_STR_BUF_GET_STR_LEN(xds.pi_code_sv));

         if (XML_STR_BUF_GET_STR_LEN(xds.pi_code_vp) > 0)
            XmlCdata_AppendRaw(&xds.pi_desc, "; ", 2);
      }
      if (XML_STR_BUF_GET_STR_LEN(xds.pi_code_vp) > 0)
      {
         XmlCdata_AppendString(&xds.pi_desc, "V+ Code: ");
         XmlCdata_AppendRaw(&xds.pi_desc, XML_STR_BUF_GET_STR(xds.pi_code_vp), XML_STR_BUF_GET_STR_LEN(xds.pi_code_vp));
      }
   }

   // sort, unify and copy collected theme categories
   XmltvDb_PiAssignThemeCategories();

   pBlk = XmltvDb_BuildPi();
   if (XmltvDb_AddPiBlock(xds.pDbContext, pBlk) == FALSE)
   {
      xfree(pBlk);
   }
}

void Xmltv_TsSetChannel( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   uint * pChnIdx;

   pChnIdx = XmlHash_SearchEntry(xds.pChannelHash, pStr);
   if (pChnIdx != NULL)
   {
      xds.pi.netwop_no = *pChnIdx;
   }
   else
   {
      // note: the PI will be dropped if the channel isn't defined BEFORE it's referenced
      debug1("Xmltv-TsSetChannel: unknown channel ID '%s'", pStr);
   }
}

void Xmltv_TsSetStartTime( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   uint len = XML_STR_BUF_GET_STR_LEN(*pBuf);

   xds.pi.start_time = XmltvDb_ParseTimestamp(pStr, len);

   dprintf2("Xmltv-TsSetStartTime: start time %s: %d\n", pStr, (int)xds.pi.start_time);
}

void Xmltv_TsSetStopTime( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   uint len = XML_STR_BUF_GET_STR_LEN(*pBuf);

   xds.pi.stop_time = XmltvDb_ParseTimestamp(pStr, len);

   dprintf2("Xmltv-TsSetStopTime: stop time %s: %d\n", pStr, (int)xds.pi.stop_time);
}

// liveness    (live | joined | prerecorded) #IMPLIED
void Xmltv_TsSetFeatLive( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if (strcasecmp(pStr, "live") == 0)
   {
      xds.pi.feature_flags |= PI_FEATURE_LIVE;
   }
}

void Xmltv_TsSetFeatCrypt( XML_STR_BUF * pBuf )
{
   xds.pi.feature_flags |= PI_FEATURE_ENCRYPTED;
}

// ----------------------------------------------------------------------------

static void XmltvDb_TsCodeTimeAssign( XMLTV_CODE_TIME_SYS system, XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   uint len = XML_STR_BUF_GET_STR_LEN(*pBuf);

   switch (system)
   {
      case XMLTV_CODE_VPS:
      case XMLTV_CODE_PDC:
         xds.pi.pil = XmltvDb_ParseVpsPdc(pStr);
         break;
      case XMLTV_CODE_SV:
         XmlCdata_Reset(&xds.pi_code_sv);
         XmlCdata_AppendRaw(&xds.pi_code_sv, pStr, len);
         break;
      case XMLTV_CODE_VP:
         XmlCdata_Reset(&xds.pi_code_vp);
         XmlCdata_AppendRaw(&xds.pi_code_vp, pStr, len);
         break;
      default:
         break;
   }
}

void Xmltv_TsCodeTimeOpen( void )
{
   xds.pi_code_time_sys = XMLTV_CODE_NONE;
   XmlCdata_Reset(&xds.pi_code_time_str);
}

void Xmltv_TsCodeTimeClose( void )
{
   XmltvDb_TsCodeTimeAssign(xds.pi_code_time_sys, &xds.pi_code_time_str);
}

// DTD 0.5 only
void Xmltv_TsCodeTimeSetVps( XML_STR_BUF * pBuf )
{
   XmltvDb_TsCodeTimeAssign(XMLTV_CODE_VPS, pBuf);
}

void Xmltv_TsCodeTimeSetPdc( XML_STR_BUF * pBuf )
{
   XmltvDb_TsCodeTimeAssign(XMLTV_CODE_PDC, pBuf);
}

void Xmltv_TsCodeTimeSetSV( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.pi_code_sv, pBuf);
}

void Xmltv_TsCodeTimeSetVP( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.pi_code_vp, pBuf);
}

// DTD 0.6
void Xmltv_TsCodeTimeSetStart( XML_STR_BUF * pBuf )
{
   if (xds.pi_code_time_sys == XMLTV_CODE_SV)
   {
      XmlCdata_Assign(&xds.pi_code_sv, pBuf);
   }
   else if (xds.pi_code_time_sys == XMLTV_CODE_VP)
   {
      XmlCdata_Assign(&xds.pi_code_vp, pBuf);
   }
   else
   {
      XmlCdata_Assign(&xds.pi_code_time_str, pBuf);
   }
}

void Xmltv_TsCodeTimeSetSystem( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if (strcasecmp(pStr, "vps") == 0)
      xds.pi_code_time_sys = XMLTV_CODE_VPS;
   else if (strcasecmp(pStr, "pdc") == 0)
      xds.pi_code_time_sys = XMLTV_CODE_PDC;
   else if (strcasecmp(pStr, "showview") == 0)
      xds.pi_code_time_sys = XMLTV_CODE_SV;
   else if (strcasecmp(pStr, "videoplus") == 0)
      xds.pi_code_time_sys = XMLTV_CODE_VP;
   else
      debug1("Xmltv-TsCodeTimeSetSystem: unknown code-time systm '%s'", pStr);
}

// ----------------------------------------------------------------------------

void Xmltv_PiTitleAdd( XML_STR_BUF * pBuf )
{
   XmlCdata_Assign(&xds.pi_title, pBuf);
}

void Xmltv_PiEpisodeTitleAdd( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if ( (alphaNumTab[(uchar)*pStr] == ALNUM_UCHAR) ||
        (alphaNumTab[(uchar)*pStr] == ALNUM_LCHAR) )
   {
      XmlCdata_AppendRaw(&xds.pi_desc, "\"", 1);
      XmlCdata_AppendRaw(&xds.pi_desc, pStr, XML_STR_BUF_GET_STR_LEN(*pBuf));
      XmlCdata_AppendRaw(&xds.pi_desc, "\"", 1);
   }
   else
      XmlCdata_AssignOrAppend(&xds.pi_desc, pBuf);
}

// ----------------------------------------------------------------------------

void Xmltv_PiCatOpen( void )
{
   xds.pi_cat_system = XMLTV_CAT_SYS_NONE;
   xds.pi_cat_code = 0;
}

void Xmltv_PiCatClose( void )
{
   // TODO: warn if value is out of range
   if (xds.pi_cat_system == XMLTV_CAT_SYS_PDC)
   {
      if ( (xds.pi_cat_code != 0)  && (xds.pi_cat_code < 0x100) )
      {
         if (xds.pi_cat_count < XMLTV_PI_CAT_MAX)
         {
            // copy codes to an intermediate cache, because they are filtered later
            xds.pi_cats[xds.pi_cat_count] = xds.pi_cat_code;
            xds.pi_cat_count += 1;
         }
      }
   }
   else if (xds.pi_cat_system == XMLTV_CAT_SYS_SORTCRIT)
   {
      if ( (xds.pi.no_sortcrit < PI_MAX_SORTCRIT_COUNT) &&
           (xds.pi_cat_code != 0)  && (xds.pi_cat_code < 0x100) )
      {
         // copy codes into the PI data struct
         xds.pi.sortcrits[xds.pi.no_sortcrit] = xds.pi_cat_code;
         xds.pi.no_sortcrit += 1;
      }
   }
   else
   {
      //warn for DTD 0.6 only
      //debug0("Xmltv-PiCatClose: no type set");
   }
}

void Xmltv_PiCatSetType( XML_STR_BUF * pBuf )
{
   // ignored
}

void Xmltv_PiCatSetSystem( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if (strcasecmp(pStr, "pdc") == 0)
   {
      xds.pi_cat_system = XMLTV_CAT_SYS_PDC;
   }
   else if (strcasecmp(pStr, "nextview/sorting_criterion") == 0)
   {
      xds.pi_cat_system = XMLTV_CAT_SYS_SORTCRIT;
   }
   else
      debug1("Xmltv-PiCatSetSystem: unknown system: %s", pStr);
}

void Xmltv_PiCatSetCode( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   char  *p;
   long  code;

   code = strtol(pStr, &p, 0);
   if ((*pStr != 0) && (*p == 0))
   {
      xds.pi_cat_code = code;
   }
   // no warning - some standards may have non-numerical codes (e.g. "A410" example in the XMLTV DTD)
   //else
   //   debug1("Xmltv-PiCatSetCode: parse error '%s'", pStr);
}

// DTD 0.5: themes given as free text
// DTD 0.6: use text for unknown systems only
void Xmltv_PiCatAddText( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if ( (xds.pi_cat_system != XMLTV_CAT_SYS_PDC) &&
        (xds.pi_cat_system != XMLTV_CAT_SYS_SORTCRIT) )
   {
      Xmltv_ParseThemeString(pStr, XML_STR_BUF_GET_LANG(*pBuf));
   }
}

// ----------------------------------------------------------------------------

void Xmltv_PiVideoAspectOpen( void )
{
   xds.pi_aspect_x = 0;
   xds.pi_aspect_y = 0;
}

void Xmltv_PiVideoAspectClose( void )
{
   if ( (xds.pi_aspect_x != 0) && (xds.pi_aspect_y != 0) &&
        (3 * xds.pi_aspect_x > 4 * xds.pi_aspect_y) )  // equiv: x/y > 4/3
   {
      xds.pi.feature_flags |= PI_FEATURE_FMT_WIDE;
   }
}

void Xmltv_PiVideoAspectSetX( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   xds.pi_aspect_x = atoi(pStr);
}

void Xmltv_PiVideoAspectSetY( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   xds.pi_aspect_y = atoi(pStr);
}

// DTD 0.5: aspect specified as PCDATA
void Xmltv_PiVideoAspectAddXY( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   int nscan;
   int asp_x, asp_y;

   nscan = sscanf(pStr, "%u : %u", &asp_x, &asp_y);
   if (nscan >= 2)
   {
      xds.pi_aspect_x = asp_x;
      xds.pi_aspect_y = asp_y;
   }
   else
      debug1("Xmltv-VideoAspectAddXY: parse error '%s'", pStr);
}

void Xmltv_PiVideoColourAdd( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if (strcasecmp(pStr, "no") == 0)
   {
      xds.pi.feature_flags |= PI_FEATURE_VIDEO_BW;
   }
   else if (strcasecmp(pStr, "yes") == 0)
   {
      xds.pi.feature_flags &= ~PI_FEATURE_VIDEO_BW;
   }
   else
      debug1("Xmltv-PiVideoColourAdd: parse error '%s'", pStr);

}

void Xmltv_PiVideoQualityAdd( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if ( (strcasecmp(pStr, "PAL+") == 0) ||
        (strcasecmp(pStr, "PALplus") == 0) ||
        (strcasecmp(pStr, "PAL-plus") == 0) )
   {
      xds.pi.feature_flags |= PI_FEATURE_PAL_PLUS;
      xds.pi.feature_flags &= ~PI_FEATURE_VIDEO_HD;
   }
   else if (strncasecmp(pStr, "HD", 2) == 0)
   {
      xds.pi.feature_flags |= PI_FEATURE_VIDEO_HD;
   }
}

void Xmltv_PiAudioOpen( void )
{
}

void Xmltv_PiAudioClose( void )
{
   xds.audio_cnt += 1;

   // two mono channels -> so-called "2-channel" (usually different languages)
   if ( (xds.audio_cnt > 1) &&
        ((xds.pi.feature_flags & PI_FEATURE_SOUND_MASK) == PI_FEATURE_SOUND_MONO) )
   {
      xds.pi.feature_flags &= ~PI_FEATURE_SOUND_MASK;
      xds.pi.feature_flags |= PI_FEATURE_SOUND_2CHAN;
   }
}

// DTD 0.5 only: "stereo-ness" specified as free text
void Xmltv_PiAudioStereoAdd( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if (strcasecmp(pStr, "mono") == 0)
   {
      xds.pi.feature_flags &= ~PI_FEATURE_SOUND_MASK;
      xds.pi.feature_flags |= PI_FEATURE_SOUND_MONO;
   }
   else if (strcasecmp(pStr, "stereo") == 0)
   {
      xds.pi.feature_flags &= ~PI_FEATURE_SOUND_MASK;
      xds.pi.feature_flags |= PI_FEATURE_SOUND_STEREO;
   }
   else if ( (strcasecmp(pStr, "surround") == 0) ||
             (strncasecmp(pStr, "quad", 4) == 0) )
   {
      xds.pi.feature_flags &= ~PI_FEATURE_SOUND_MASK;
      xds.pi.feature_flags |= PI_FEATURE_SOUND_SURROUND;
   }
   else if (strcasecmp(pStr, "bilingual") == 0)
   {
      xds.pi.feature_flags &= ~PI_FEATURE_SOUND_MASK;
      xds.pi.feature_flags |= PI_FEATURE_SOUND_2CHAN;
   }
   else
      debug1("Xmltv-PiAudioStereoAdd: unknown keyword '%s'", pStr);
}

// DTD 0.6: separate tags
void Xmltv_PiAudioMonoOpen( void )
{
   xds.pi.feature_flags &= ~PI_FEATURE_SOUND_MASK;
   xds.pi.feature_flags |= PI_FEATURE_SOUND_MONO;
}

void Xmltv_PiAudioStereoOpen( void )
{
   xds.pi.feature_flags &= ~PI_FEATURE_SOUND_MASK;
   xds.pi.feature_flags |= PI_FEATURE_SOUND_STEREO;
}

void Xmltv_PiAudioSurrOpen( void )
{
   xds.pi.feature_flags &= ~PI_FEATURE_SOUND_MASK;
   xds.pi.feature_flags |= PI_FEATURE_SOUND_SURROUND;
}

// DTD 0.5 only: attribute type set as attribute
void Xmltv_PiSubtitlesSetType( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if ( (strcmp(pStr, "teletext") == 0) ||
        (strcmp(pStr, "onscreen") == 0) )
   {
      xds.pi.feature_flags |= PI_FEATURE_SUBTITLES;
   }
   else
      debug1("Xmltv-PiSubtitlesSetType: unknown subtitles type '%s'", pStr);
}

// DTD 0.6 only: separate tags for different subtitle types
void Xmltv_PiSubtitlesOsd( void )
{
   xds.pi.feature_flags |= PI_FEATURE_SUBTITLES;
}

void Xmltv_PiSubtitlesTtx( void )
{
   xds.pi.feature_flags |= PI_FEATURE_SUBTITLES;
}

void Xmltv_PiSubtitlesSetPage( XML_STR_BUF * pBuf )
{
   // not supported by database struct
}

void Xmltv_PiRatingSetSystem( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if (strcasecmp(pStr, "age") == 0)
   {
      xds.pi_rating_sys = XMLTV_RATING_AGE;
   }
   else if (strcasecmp(pStr, "FSK") == 0)
   {
      xds.pi_rating_sys = XMLTV_RATING_FSK;
   }
   else if (strcasecmp(pStr, "BBFC") == 0)
   {
      xds.pi_rating_sys = XMLTV_RATING_BBFC;
   }
   else if (strcasecmp(pStr, "MPAA") == 0)
   {
      xds.pi_rating_sys = XMLTV_RATING_MPAA;
   }
   else
      debug1("Xmltv-PiRatingSetSystem: unknown system '%s'", pStr);
}

void Xmltv_PiRatingAddText( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   char * p;
   long age;

   // first check if it's a plain number, regardless of rating system
   age = strtol(pStr, &p, 0);
   if ((*pStr != 0) && (*p == 0))
   {
      // plain number -> assume age
      if (age <= 18)
         xds.pi.parental_rating = age / 2;
      else
         debug1("Xmltv-PiRatingAddText: weird age in rating: %ld", age);
   }
   else if (xds.pi_rating_sys == XMLTV_RATING_AGE)
   {
      if (strcasecmp(pStr, "general") == 0)
      {
         xds.pi.parental_rating = 1;
      }
      else
         debug1("Xmltv-PiRatingAddText: parse error in age: '%s'", pStr);
   }
   else if (xds.pi_rating_sys == XMLTV_RATING_FSK)
   {
      // official FSK ratings: "ohne Beschränkung", "ab 6", "ab 12", "ab 16", "keine Freigabe"
      // http://www.fsk-online.de/

      if (strstr(pStr, "ab 6") != NULL)
         xds.pi.parental_rating = 6/2;
      else if (strstr(pStr, "ab 12") != NULL)
         xds.pi.parental_rating = 12/2;
      else if (strstr(pStr, "ab 16") != NULL)
         xds.pi.parental_rating = 16/2;
      else if (strstr(pStr, "ab 18") != NULL)
         xds.pi.parental_rating = 18/2;
      else if (strstr(pStr, "ohne") != NULL)
         xds.pi.parental_rating = 1;
      else if ( (strstr(pStr, "keine") != NULL) && (strstr(pStr, "freigabe") != NULL) )
         xds.pi.parental_rating = 18/2;
      else if ( (strstr(pStr, "ohne") != NULL) && (strstr(pStr, "beschr") != NULL) )
         xds.pi.parental_rating = 1;
      else
         debug1("Xmltv-PiRatingAddText: unknown FSK rating '%s'", pStr);
   }
   else if (xds.pi_rating_sys == XMLTV_RATING_BBFC)
   {
      // official BBFC certifications: U, PG, 12, 15, 18, R18
      // http://www.movie-ratings.net/movieratings_uk.shtml

      if ( (strcasecmp(pStr, "U") == 0) || (strcasecmp(pStr, "unrated") == 0) )
         xds.pi.parental_rating = 1;
      else if (strcasecmp(pStr, "PG") == 0)
         xds.pi.parental_rating = 6/2;
      else if ( (strcasecmp(pStr, "R18") == 0) || (strncasecmp(pStr, "X", 1) == 0) )
         xds.pi.parental_rating = 18/2;
      else
         debug1("Xmltv-PiRatingAddText: unknown BBFC rating '%s'", pStr);
   }
   else if (xds.pi_rating_sys == XMLTV_RATING_MPAA)
   {
      // official MPAA ratings: G, PG, PG-13, R, NC-17
      // http://www.mpaa.org/movieratings/

      if ( (strcasecmp(pStr, "G") == 0) || (strcasecmp(pStr, "general") == 0) )
         xds.pi.parental_rating = 1;
      else if (strcasecmp(pStr, "PG") == 0)
         xds.pi.parental_rating = 6/2;
      else if (strcasecmp(pStr, "PG-13") == 0)
         xds.pi.parental_rating = 13/2;
      else if (strcasecmp(pStr, "R") == 0)
         xds.pi.parental_rating = 17/2;
      else if (strcasecmp(pStr, "NC-17") == 0)
         xds.pi.parental_rating = 18/2;
      else if (strncasecmp(pStr, "X", 1) == 0)
         xds.pi.parental_rating = 18/2;
      else
         debug1("Xmltv-PiRatingAddText: unknown MPAA rating '%s'", pStr);
   }
   else
   {
      debug1("Xmltv-PiRatingAddText: no system set for rating '%s'", pStr);
   }
}

static void Xmltv_PiStarRatingSet( uint cval, uint max_val )
{
   if ((cval <= max_val) && (max_val > 0))
   {
      // many rating systems use a scale 0..5, so we don't fiddle with that
      // other values are scaled to the nextview rating range 0..7
      if ((max_val >= 5) && (max_val <= 7))
         xds.pi.editorial_rating = cval;
      else
         xds.pi.editorial_rating = ((cval * 7 + (max_val/2)) / max_val);
   }
   else
      debug2("Xmltv-PiStarRatingSet: weird values: %d / %d", cval, max_val);
}

// DTD 0.5 only: rating in format "n / m"
void Xmltv_PiStarRatingAddText( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   int nscan;
   uint cval, max_val;

   nscan = sscanf(pStr, "%u / %u", &cval, &max_val);
   if (nscan >= 2)
   {
      xds.pi_star_rating_max = max_val;
      xds.pi_star_rating_val = cval;
   }
   else
      debug1("Xmltv-PiStarRatingAddText: parse error '%s' (not in 'n/m' format)", pStr);
}

void Xmltv_PiStarRatingOpen( void )
{
   xds.pi_star_rating_max = 0;
   xds.pi_star_rating_val = 0;
}

void Xmltv_PiStarRatingClose( void )
{
   Xmltv_PiStarRatingSet(xds.pi_star_rating_val, xds.pi_star_rating_max);
}

// DTD 0.6 only: rating value and max. values as attributes
void Xmltv_PiStarRatingSetValue( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   char * p;
   long val;

   val = strtol(pStr, &p, 0);
   if ((*pStr != 0) && (*p == 0))
   {
      xds.pi_star_rating_val = val;
   }
   else
      debug1("Xmltv-PiStarRatingSetValue: parse error '%s'", pStr);
}

void Xmltv_PiStarRatingSetMax( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   char * p;
   long val;

   val = strtol(pStr, &p, 0);
   if ((*pStr != 0) && (*p == 0))
   {
      xds.pi_star_rating_max = val;
   }
   else
      debug1("Xmltv-PiStarRatingSetMax: parse error '%s'", pStr);
}

void Xmltv_PiDescOpen( void )
{
}

void Xmltv_PiDescClose( void )
{
}

void Xmltv_ParagraphCreate( void )
{
}

void Xmltv_ParagraphClose( void )
{
}

void Xmltv_ParagraphAdd( XML_STR_BUF * pBuf )
{
   // FIXME: should replace single newlines with space
   // double-newline in DTD 0.5 should be interpreted as paragraph breaks

   XmlCdata_AppendParagraph(&xds.pi_desc, FALSE);

   XmlCdata_AssignOrAppend(&xds.pi_desc, pBuf);
}

// ----------------------------------------------------------------------------

void Xmltv_PiCreditsOpen( void )
{
   XmlCdata_Reset(&xds.pi_credits);
   XmlCdata_Reset(&xds.pi_actors);
}

void Xmltv_PiCreditsClose( void )
{
}

static void Xmltv_PiCreditsAppend( const char * pKind, XML_STR_BUF * pBuf )
{
   if (XML_STR_BUF_GET_STR_LEN(xds.pi_credits) > 0)
      XmlCdata_AppendRaw(&xds.pi_credits, ", ", 2);
   XmlCdata_AppendString(&xds.pi_credits, pKind);
   XmlCdata_AssignOrAppend(&xds.pi_credits, pBuf);
}

void Xmltv_PiCreditsAddDirector( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("directed by: ", pBuf);
}

void Xmltv_PiCreditsAddActor( XML_STR_BUF * pBuf )
{
   if (XML_STR_BUF_GET_STR_LEN(xds.pi_actors) == 0)
      XmlCdata_AppendString(&xds.pi_actors, "With: ");
   else
      XmlCdata_AppendRaw(&xds.pi_actors, ", ", 2);
   XmlCdata_AppendRaw(&xds.pi_actors, XML_STR_BUF_GET_STR(*pBuf), XML_STR_BUF_GET_STR_LEN(*pBuf));
}

void Xmltv_PiCreditsAddWriter( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("written by: ", pBuf);
}

void Xmltv_PiCreditsAddAdapter( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("adapted by: ", pBuf);
}

void Xmltv_PiCreditsAddProducer( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("produced by: ", pBuf);
}

void Xmltv_PiCreditsAddExecProducer( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("exec. producer: ", pBuf);
}

void Xmltv_PiCreditsAddPresenter( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("presented by: ", pBuf);
}

void Xmltv_PiCreditsAddCommentator( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("commentator: ", pBuf);
}

void Xmltv_PiCreditsAddNarrator( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("narrator: ", pBuf);
}

void Xmltv_PiCreditsAddCompany( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("producer: ", pBuf);
}

void Xmltv_PiCreditsAddGuest( XML_STR_BUF * pBuf )
{
   Xmltv_PiCreditsAppend("guest: ", pBuf);
}

void Xmltv_PiDateAdd( XML_STR_BUF * pBuf )
{
}

// ----------------------------------------------------------------------------
// Retrieve database
// - afterwards the caller is responsible to free the memory used by database
//
EPGDB_CONTEXT * XmltvDb_GetDatabase( const char * pProvName )
{
   EPGDB_CONTEXT * pDbContext;

   // finally generate inventory block with channel table
   xds.pDbContext->pAiBlock = XmltvDb_BuildAi(pProvName);
   xds.pDbContext->pFirstGenericBlock[BLOCK_TYPE_OI] = XmltvDb_BuildOi();

#if DEBUG_GLOBAL_SWITCH == ON
   EpgDbCheckChains(xds.pDbContext);
#endif

   pDbContext = xds.pDbContext;
   xds.pDbContext = NULL;

   return pDbContext;
}

// ----------------------------------------------------------------------------
// Free resources
//
void XmltvDb_Destroy( void )
{
   uint  idx;

   if (xds.p_chn_table != NULL)
   {
      // channel table list & dup'ed channel ID and display name strings
      // note: check one more than count in case there's an unclosed channel tag
      for (idx = 0; (idx < xds.chn_count + 1) && (idx < xds.chn_tab_size); idx++)
      {
         if (xds.p_chn_table[idx].p_disp_name != NULL)
            xfree(xds.p_chn_table[idx].p_disp_name);
      }
      xfree(xds.p_chn_table);
   }
   if (xds.p_chn_id_tmp != NULL)
   {
      xfree(xds.p_chn_id_tmp);
      xds.p_chn_id_tmp = NULL;
   }
   XmlHash_Destroy(xds.pChannelHash, NULL);
   XmlHash_Destroy(xds.pThemeHash, NULL);

   // pi title and desc cache
   XmlCdata_Free(&xds.pi_title);
   XmlCdata_Free(&xds.pi_desc);
   XmlCdata_Free(&xds.pi_credits);
   XmlCdata_Free(&xds.pi_actors);

   XmlCdata_Free(&xds.pi_code_time_str);
   XmlCdata_Free(&xds.pi_code_sv);
   XmlCdata_Free(&xds.pi_code_vp);

   XmlCdata_Free(&xds.source_info_name);
   XmlCdata_Free(&xds.source_info_url);
   XmlCdata_Free(&xds.source_data_url);
   XmlCdata_Free(&xds.gen_info_name);
   XmlCdata_Free(&xds.gen_info_url);

   if (xds.pDbContext != NULL)
   {
      EpgDbDestroy(xds.pDbContext, FALSE);
      xds.pDbContext = NULL;
   }
   XmltvCni_MapDestroy(&xds.cniCtx);
}

// ----------------------------------------------------------------------------
// Initialize the local module state
//
void XmltvDb_Init( XMLTV_DTD_VERSION dtd, uint provCni, bool isPeek )
{
   memset(&xds, 0, sizeof(xds));
   xds.pChannelHash = XmlHash_Init();
   xds.pThemeHash = XmlHash_Init();

   // set string buffer size hints
   XmlCdata_Init(&xds.pi_title, 256);
   XmlCdata_Init(&xds.pi_code_time_str, 256);
   XmlCdata_Init(&xds.pi_code_sv, 256);
   XmlCdata_Init(&xds.pi_code_vp, 256);
   XmlCdata_Init(&xds.pi_desc, 4096);

   xds.dtd = dtd;
   xds.isPeek = isPeek;

   // create empty database
   xds.pDbContext = EpgDbCreate();
   xds.pDbContext->xmltv = TRUE;
   xds.pDbContext->provCni = provCni;
}

