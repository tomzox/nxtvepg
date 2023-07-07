/*
 *  XMLTV content processing
 *
 *  Copyright (C) 2007-2011, 2020-2021, 2023 T. Zoerner
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
 *    Building the programme database:
 *
 *    The callback functions fill temporary structures with programme
 *    parameters and data.  The structures are forwarded to a database
 *    when the tag is closed.  Also a channel table is generated and
 *    forwarded to the database at the end of the file.
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
#include "epgdb/epgdbmgmt.h"
#include "epgui/pidescr.h"

#include "xmltv/xml_cdata.h"
#include "xmltv/xml_hash.h"
#include "xmltv/xmltv_timestamp.h"
#include "xmltv/xmltv_tags.h"
#include "xmltv/xmltv_cni.h"
#include "xmltv/xmltv_db.h"


typedef struct
{
   const char * p_disp_name;
   uint         cni;
} XMLTV_CHN;

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
   XMLTV_RATING_MPAA,
   XMLTV_RATING_PL,
} XMLTV_RATING_SYS;

typedef struct
{
   uint         id;
} HASHED_THEMES;

typedef struct
{
   uint         chn_count;
   uint         chn_tab_size;
   bool         chn_open;
   char       * p_chn_id_tmp;
   XMLTV_CHN  * p_chn_table;
   XML_HASH_PTR p_chn_hash;

   XML_STR_BUF  source_info_name;
   XML_STR_BUF  source_info_url;
   XML_STR_BUF  source_data_url;
   XML_STR_BUF  gen_info_name;
   XML_STR_BUF  gen_info_url;

   XML_HASH_PTR p_theme_hash;
   char**       p_theme_table;
   uint         theme_table_size;
   uint         theme_count;
   PI_BLOCK     pi;
   XML_STR_BUF  pi_title;
   XML_STR_BUF  pi_desc;
   XML_STR_BUF  pi_credits;
   XML_STR_BUF  pi_actors;
   EPG_LANG_CODE pi_title_lang;
   EPG_LANG_CODE pi_desc_lang;

   uint         pi_aspect_x;
   uint         pi_aspect_y;
   double       pi_star_rating_val;
   uint         pi_star_rating_max;
   XMLTV_RATING_SYS pi_rating_sys;
   XMLTV_CODE_TIME_SYS pi_code_time_sys;
   XML_STR_BUF  pi_code_time_str;
   XML_STR_BUF  pi_code_sv;
   XML_STR_BUF  pi_code_vp;

   EPGDB_CONTEXT * pDbContext;
   EPGDB_PI_BLOCK **pFirstNetwopPi;
   EPGDB_PI_BLOCK **pLastNetwopPi;
   time_t       mtime;
   bool         isPeek;
   bool         cniCtxInitDone;
   XMLTV_CNI_CTX cniCtx;
} PARSE_STATE;

static PARSE_STATE xds;

#ifndef VPS_PIL_CODE_SYSTEM
#define VPS_PIL_CODE_SYSTEM ((0 << 15) | (15 << 11) | (31 << 6) | 63)
#endif

// ----------------------------------------------------------------------------
// Parse timestamp
//
static time_t XmltvDb_ParseTimestamp( const char * pStr, uint len )
{
   time_t tval = parse_xmltv_date_v5(pStr, len);

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

   return pil;
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
// Generate source & generator description for AI block
//
static void XmlTv_BuildSourceInfoMessages( XML_STR_BUF * pHeader, XML_STR_BUF * pMessage )
{
   char datestr[99];
   int len;

   memset(pHeader, 0, sizeof(*pHeader));
   memset(pMessage, 0, sizeof(*pMessage));

   // header
   XmlCdata_AppendRaw(pHeader, XML_STR_BUF_GET_STR(xds.source_info_name), XML_STR_BUF_GET_STR_LEN(xds.source_info_name));

   // message
   XmlCdata_AppendRaw(pMessage, XML_STR_BUF_GET_STR(xds.source_info_url), XML_STR_BUF_GET_STR_LEN(xds.source_info_url));

   if (XML_STR_BUF_GET_STR_LEN(xds.source_data_url) > 0)
   {
      XmlCdata_AppendParagraph(pMessage, TRUE);
   }
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

   // producer date
   XmlCdata_AppendParagraph(pMessage, TRUE);
   XmlCdata_AppendString(pMessage, "Last update: ");
   len = strftime(datestr, sizeof(datestr), "%a %d.%m.%Y %H:%M:%S", localtime(&xds.mtime));
   XmlCdata_AppendRaw(pMessage, datestr, len);
}

// ----------------------------------------------------------------------------
// Assemble an AI block (application inventory, i.e. channel table)
//
static EPGDB_AI_BLOCK * XmltvDb_BuildAi( const char * pProvName )
{
   EPGDB_AI_BLOCK *pBlk;
   AI_BLOCK *pAi;
   AI_NETWOP *pNetwops;
   XML_STR_BUF aiServiceName;
   XML_STR_BUF sourceInfo;
   XML_STR_BUF genInfo;
   uint  aiServiceNameLen;
   uint  netNameLenSum;
   uint  sourceInfoLen;
   uint  genInfoLen;
   uint  blockLen;

   netNameLenSum = 0;
   for (uint idx = 0; idx < xds.chn_count; idx++)
   {
      netNameLenSum += strlen(xds.p_chn_table[idx].p_disp_name) + 1;
   }
   XmlTv_BuildAiServiceName(&aiServiceName, pProvName);
   aiServiceNameLen = XML_STR_BUF_GET_STR_LEN(aiServiceName) + 1;

   XmlTv_BuildSourceInfoMessages(&sourceInfo, &genInfo);
   sourceInfoLen = XML_STR_BUF_GET_STR_LEN(sourceInfo) + 1;
   genInfoLen = XML_STR_BUF_GET_STR_LEN(genInfo) + 1;

   blockLen = sizeof(AI_BLOCK) +
              (xds.chn_count * sizeof(AI_NETWOP)) +
              netNameLenSum +
              aiServiceNameLen +
              sourceInfoLen +
              genInfoLen;
   pBlk = EpgBlockCreateAi(blockLen, xds.mtime);
   pAi = (AI_BLOCK *) &pBlk->ai;  // remove const from pointer
   memset(pAi, 0, sizeof(AI_BLOCK));
   pAi->netwopCount = xds.chn_count;
   blockLen = sizeof(AI_BLOCK);

   pAi->off_netwops = blockLen;
   pNetwops = (AI_NETWOP *) AI_GET_NETWOPS(pAi);  // cast to remove const
   memset(pNetwops, 0, pAi->netwopCount * sizeof(AI_NETWOP));
   blockLen += pAi->netwopCount * sizeof(AI_NETWOP);

   pAi->off_serviceNameStr = blockLen;
   memcpy((char *)pAi + blockLen, XML_STR_BUF_GET_STR(aiServiceName), aiServiceNameLen);
   blockLen += aiServiceNameLen;

   pAi->off_sourceInfoStr = blockLen;
   memcpy((char *)pAi + blockLen, XML_STR_BUF_GET_STR(sourceInfo), sourceInfoLen);
   blockLen += sourceInfoLen;

   pAi->off_genInfoStr = blockLen;
   memcpy((char *)pAi + blockLen, XML_STR_BUF_GET_STR(genInfo), genInfoLen);
   blockLen += genInfoLen;

   XmlCdata_Free(&aiServiceName);
   XmlCdata_Free(&sourceInfo);
   XmlCdata_Free(&genInfo);

   for (uint idx = 0; idx < xds.chn_count; idx++, pNetwops++)
   {
      pNetwops->netCni = xds.p_chn_table[idx].cni;

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
static bool XmltvDb_AddPiBlock( EPGDB_CONTEXT * dbc, EPGDB_PI_BLOCK *pBlock )
{
   EPGDB_PI_BLOCK *pWalk;
   EPGDB_PI_BLOCK *pNext;
   uint netwop;
   bool added;

   // when stop time is missing, fake it as start + 1 second (XXX FIXME choose start of next PI)
   if (pBlock->pi.stop_time == 0)
      *(time32_t*)&pBlock->pi.stop_time = pBlock->pi.start_time + 1;  // cast to remove const

   netwop = pBlock->pi.netwop_no;
   if (netwop < xds.chn_count)
   {
      if ( (pBlock->pi.start_time != 0) &&
           (pBlock->pi.start_time < pBlock->pi.stop_time) )
      {
         dprintf4("ADD PI ptr=%lx: netwop=%d, start=%ld \"%s\"\n", (ulong)pBlock, netwop, pBlock->pi.start_time, PI_GET_TITLE(&pBlock->pi));

         // search for insertion point within network chain, which is sorted by start time;
         // start at the end of the chain, as data in XMLTV usually is already sorted this way.
         pWalk = xds.pLastNetwopPi[netwop];

         if (pWalk == NULL)
         {
            assert(xds.pFirstNetwopPi[netwop] == NULL);
            xds.pFirstNetwopPi[netwop] = pBlock;
            xds.pLastNetwopPi[netwop] = pBlock;
            added = TRUE;
         }
         else
         {
            while ( (pWalk != NULL) &&
                    (pWalk->pi.start_time > pBlock->pi.start_time) )
            {
               pWalk = pWalk->pPrevNetwopBlock;
            }

            if (pWalk == NULL)
               pNext = xds.pFirstNetwopPi[netwop];
            else
               pNext = pWalk->pNextNetwopBlock;

            if ( ( (pNext == NULL) ||
                   (pNext->pi.start_time >= pBlock->pi.stop_time) ) &&
                 ( (pWalk == NULL) ||
                   (pWalk->pi.stop_time <= pBlock->pi.start_time) ) )
            {
               // insert into the network pointer chain
               if (pWalk == NULL)
               {
                  pBlock->pNextNetwopBlock = xds.pFirstNetwopPi[netwop];
                  xds.pFirstNetwopPi[netwop] = pBlock;
               }
               else
               {
                  pBlock->pNextNetwopBlock = pWalk->pNextNetwopBlock;
                  pWalk->pNextNetwopBlock = pBlock;
               }

               if (pNext == NULL)
               {
                  xds.pLastNetwopPi[netwop] = pBlock;
               }
               else
               {
                  pNext->pPrevNetwopBlock = pBlock;
               }
               pBlock->pPrevNetwopBlock = pWalk;

               added = TRUE;
            }
            else
            {
               if ((pWalk != NULL) && (pWalk->pi.start_time < pBlock->pi.stop_time))
                  dprintf1("OVERLAP NEXT start:%ld - dropping PI\n", pWalk->pi.start_time);
               else
                  dprintf2("OVERLAP PREV start:%ld stop:%ld - dropping PI\n", pPrev->pi.start_time, pPrev->pi.stop_time);
               added = FALSE;
            }
         }
      }
      else
      {  // defective
         debug3("AddPi: netwop:%d invalid start or stop times: %d, %d", netwop, (int)pBlock->pi.start_time, (int)pBlock->pi.stop_time);
         added = FALSE;
      }
   }
   else
   {  // undefined network
      debug1("DEFECTIVE block dropped: undefined netwop %d", netwop);
      added = FALSE;
   }

   return added;
}

// ----------------------------------------------------------------------------
// Assemble a PI block
//
static EPGDB_PI_BLOCK * XmltvDb_BuildPi( void )
{
   EPGDB_PI_BLOCK * pBlk;
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
      xds.pi.off_desc_text = piLen;
      piLen += XML_STR_BUF_GET_STR_LEN(xds.pi_desc) + 1;
   }
   else
      xds.pi.off_desc_text = 0;

   // 2nd step: copy elements one after each other, then free single elements
   pBlk = EpgBlockCreatePi(piLen, xds.mtime);
   pPi = (PI_BLOCK *) &pBlk->pi;  // remove const from pointer
   memcpy(pPi, &xds.pi, sizeof(PI_BLOCK));

   // copy title string (always present)
   strcpy((char *) PI_GET_TITLE(pPi), XML_STR_BUF_GET_STR(xds.pi_title));

   // copy description text: only if present (else no memory was allocated above)
   if (XML_STR_BUF_GET_STR_LEN(xds.pi_desc) > 0)
      strcpy((char *) PI_GET_DESC_TEXT(pPi), XML_STR_BUF_GET_STR(xds.pi_desc));

   pPi->lang_title = xds.pi_title_lang;
   pPi->lang_desc = xds.pi_desc_lang;

   return pBlk;
}

// ----------------------------------------------------------------------------

void Xmltv_AboutSetSourceInfoUrl( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.source_info_url, pBuf);
   XmlCdata_NormalizeWhitespace(&xds.source_info_url);
}

void Xmltv_AboutSetSourceInfoName( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.source_info_name, pBuf);
   XmlCdata_NormalizeWhitespace(&xds.source_info_name);
}

void Xmltv_AboutSetSourceDataUrl( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.source_data_url, pBuf);
   XmlCdata_NormalizeWhitespace(&xds.source_data_url);
}

void Xmltv_AboutSetGenInfoName( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.gen_info_name, pBuf);
   XmlCdata_NormalizeWhitespace(&xds.gen_info_name);
}

void Xmltv_AboutSetGenInfoUrl( XML_STR_BUF * pBuf )
{
   XmlCdata_AssignOrAppend(&xds.gen_info_url, pBuf);
   XmlCdata_NormalizeWhitespace(&xds.gen_info_url);
}

void Xmltv_AboutSetDate( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   uint len = XML_STR_BUF_GET_STR_LEN(*pBuf);

   time_t mtime = XmltvDb_ParseTimestamp(pStr, len);
   if (mtime != 0)
      xds.mtime = mtime;
}

// ----------------------------------------------------------------------------
// Add a new channel
//
void Xmltv_ChannelCreate( void )
{
   assert(xds.chn_open == FALSE);

   if ((xds.p_chn_table == NULL) || (xds.chn_tab_size < xds.chn_count + 1))
   {
      size_t prev_size = xds.chn_tab_size;

      if (prev_size == 0)
         xds.chn_tab_size = 256;
      else
         xds.chn_tab_size *= 2;

      // grow channel name table
      xds.p_chn_table = (XMLTV_CHN*) xrealloc(xds.p_chn_table,
                                              xds.chn_tab_size * sizeof(xds.p_chn_table[0]));
      memset((xds.p_chn_table + prev_size), 0,
             (xds.chn_tab_size - prev_size) * sizeof(xds.p_chn_table[0]));

      // grow PI tables
      xds.pFirstNetwopPi = xrealloc(xds.pFirstNetwopPi,
                                    xds.chn_tab_size * sizeof(xds.pFirstNetwopPi[0]));
      memset((xds.pFirstNetwopPi + prev_size), 0,
             (xds.chn_tab_size - prev_size) * sizeof(xds.pFirstNetwopPi[0]));

      xds.pLastNetwopPi = xrealloc(xds.pLastNetwopPi,
                                    xds.chn_tab_size * sizeof(xds.pLastNetwopPi[0]));
      memset((xds.pLastNetwopPi + prev_size), 0,
             (xds.chn_tab_size - prev_size) * sizeof(xds.pLastNetwopPi[0]));
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
           (xds.p_chn_table[xds.chn_count].p_disp_name != NULL) )
      {
         pChnIdx = (uint*) XmlHash_CreateEntry(xds.p_chn_hash, xds.p_chn_id_tmp, &isNew);
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
      else if (xds.p_chn_id_tmp != NULL)
         debug1("Xmltv-ChannelClose: no display name for channel ID %s", xds.p_chn_id_tmp);
      else
         debug0("Xmltv-ChannelClose: missing ID attribute in channel tag");

      if (result == FALSE)
      {
         if (xds.p_chn_table[xds.chn_count].p_disp_name != NULL)
         {
            xfree((void*)xds.p_chn_table[xds.chn_count].p_disp_name);  // cast to remove const
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
      fatal0("Xmltv-ChannelClose: outside of channel definition");
}

void Xmltv_ChannelSetId( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   assert(xds.chn_open);

   if (xds.chn_open)
   {
      if (xds.p_chn_id_tmp == NULL)
      {
         xds.p_chn_id_tmp = xstrdup(pStr);
      }
      else
         debug2("Xmltv-ChannelSetId: multiple channel ID attributes in tag: %s and %s", xds.p_chn_id_tmp, pStr);
   }
   else
      debug0("Xmltv-ChannelSetId: outside of channel definition");
}

void Xmltv_ChannelAddName( XML_STR_BUF * pBuf )
{
   assert(xds.chn_open);

   if (xds.chn_open)
   {
      // Pick the first display name (TODO pick one base on lang preference)
      if (xds.p_chn_table[xds.chn_count].p_disp_name == NULL)
      {
         xds.p_chn_table[xds.chn_count].p_disp_name = xstrdup(XML_STR_BUF_GET_STR(*pBuf));
      }
      else
         dprintf2("Xmltv-ChannelAddName: multiple channel IDs: %s and %s", xds.p_chn_table[xds.chn_count].p_disp_name, XML_STR_BUF_GET_STR(*pBuf));
   }
   else
      debug0("Xmltv-ChannelAddName: outside of channel definition");
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
   xds.pi.parental_rating = PI_PARENTAL_UNDEFINED;
   xds.pi.editorial_rating = PI_EDITORIAL_UNDEFINED;

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
   EPGDB_PI_BLOCK * pBlk;

   dprintf0("Xmltv_TsClose\n");

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

   pBlk = XmltvDb_BuildPi();
   if (XmltvDb_AddPiBlock(xds.pDbContext, pBlk) == FALSE)
   {
      if (EpgDbAddDefectPi(xds.pDbContext, pBlk) == FALSE)
      {
         xfree(pBlk);
      }
   }
}

void Xmltv_TsSetChannel( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   uint * pChnIdx;

   pChnIdx = (uint*) XmlHash_SearchEntry(xds.p_chn_hash, pStr);
   if (pChnIdx != NULL)
   {
      dprintf2("Xmltv-TsSetChannel: %s: %d\n", pStr, *pChnIdx);
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
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   uint len = XML_STR_BUF_GET_STR_LEN(*pBuf);

   xds.pi.start_time = XmltvDb_ParseTimestamp(pStr, len);

   dprintf2("Xmltv-TsSetStartTime: start time %s: %d\n", pStr, (int)xds.pi.start_time);
}

void Xmltv_TsSetStopTime( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   uint len = XML_STR_BUF_GET_STR_LEN(*pBuf);

   xds.pi.stop_time = XmltvDb_ParseTimestamp(pStr, len);

   dprintf2("Xmltv-TsSetStopTime: stop time %s: %d\n", pStr, (int)xds.pi.stop_time);
}

// liveness    (live | joined | prerecorded) #IMPLIED
void Xmltv_TsSetFeatLive( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);

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
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);
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

void Xmltv_TsCodeTimeSetSystem( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);

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
   xds.pi_title_lang = XmltvTags_GetLanguage();
}

void Xmltv_PiEpisodeTitleAdd( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);

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
}

void Xmltv_PiCatClose( void )
{
}

// DTD 0.5: themes given as free text
void Xmltv_PiCatAddText( XML_STR_BUF * pBuf )
{
   char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   HASHED_THEMES * pCache;
   bool isNew;

   pCache = (HASHED_THEMES*) XmlHash_CreateEntry(xds.p_theme_hash, pStr, &isNew);

   if (isNew)
   {
      pCache->id = xds.theme_count;
      dprintf2("ADD theme #%d \"%s\"\n", pCache->id, pStr);

      if ((xds.p_theme_table == NULL) || (xds.theme_table_size < xds.theme_count + 1))
      {
         // grow theme table
         xds.theme_table_size = (xds.theme_table_size == 0) ? 256 : (xds.theme_table_size * 2);

         xds.p_theme_table = xrealloc(xds.p_theme_table,
                                      xds.theme_table_size * sizeof(xds.p_theme_table[0]));
      }
      xds.p_theme_table[xds.theme_count] = xstrdup(pStr);

      xds.theme_count += 1;
   }

   // add category ID to PI theme array
   if (xds.pi.no_themes < PI_MAX_THEME_COUNT)
   {
      xds.pi.themes[xds.pi.no_themes] = pCache->id;
      xds.pi.no_themes += 1;
   }
   else
   {
      debug1("Xmltv-PiCatAddText: PI theme array overflow: %s", pStr);
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

// DTD 0.5: aspect specified as PCDATA
void Xmltv_PiVideoAspectAddXY( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);
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
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);

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
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);

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

// DTD 0.5: "stereo-ness" specified as free text
void Xmltv_PiAudioStereoAdd( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);

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

// DTD 0.5 only: attribute type set as attribute
void Xmltv_PiSubtitlesSetType( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);

   if ( (strcmp(pStr, "teletext") == 0) ||
        (strcmp(pStr, "onscreen") == 0) )
   {
      xds.pi.feature_flags |= PI_FEATURE_SUBTITLES;
   }
   else
      debug1("Xmltv-PiSubtitlesSetType: unknown subtitles type '%s'", pStr);
}

void Xmltv_PiRatingSetSystem( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);

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
   else if (strcasecmp(pStr, "PL") == 0)
   {
      xds.pi_rating_sys = XMLTV_RATING_PL;
   }
   else
      debug1("Xmltv-PiRatingSetSystem: unknown system '%s'", pStr);
}

void Xmltv_PiRatingAddText( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   char * p;
   long age;

   // first check if it's a plain positive number, regardless of rating system
   age = strtol(pStr, &p, 10);
   if ((*pStr != 0) && ((*p == 0) || (*p == '+')) && (age >= 0) && (age <= 18))
   {
      // plain number -> assume age
      xds.pi.parental_rating = age;
   }
   else if (xds.pi_rating_sys == XMLTV_RATING_AGE)
   {
      if (strcasecmp(pStr, "general") == 0)
      {
         xds.pi.parental_rating = 0;
      }
      else
         debug1("Xmltv-PiRatingAddText: parse error in age: '%s'", pStr);
   }
   else if (xds.pi_rating_sys == XMLTV_RATING_FSK)
   {
      // official FSK ratings: "ohne Beschraenkung", "ab 6", "ab 12", "ab 16", "keine Freigabe"
      // http://www.fsk-online.de/

      int scan_pos;
      int nscan = sscanf(pStr, "ab %ld%n", &age, &scan_pos);
      if (   (nscan >= 1) && ((pStr[scan_pos] == 0) || (pStr[scan_pos] == ' '))
          && (age >= 0) && (age <= 18))
      {
         xds.pi.parental_rating = age;
      }
      else if (strstr(pStr, "ohne") != NULL)
         xds.pi.parental_rating = 0;
      else if ( (strstr(pStr, "keine") != NULL) && (strstr(pStr, "freigabe") != NULL) )
         xds.pi.parental_rating = 18;
      else
         debug1("Xmltv-PiRatingAddText: unknown FSK rating '%s'", pStr);
   }
   else if (xds.pi_rating_sys == XMLTV_RATING_BBFC)
   {
      // official BBFC certifications: U, PG, 12, 15, 18, R18
      // http://www.movie-ratings.net/movieratings_uk.shtml

      if ( (strcasecmp(pStr, "U") == 0) || (strcasecmp(pStr, "unrated") == 0) )
         xds.pi.parental_rating = 0;
      else if (strcasecmp(pStr, "PG") == 0)
         xds.pi.parental_rating = 6;
      else if ( (strcasecmp(pStr, "R18") == 0) || (strncasecmp(pStr, "X", 1) == 0) )
         xds.pi.parental_rating = 18;
      else
         debug1("Xmltv-PiRatingAddText: unknown BBFC rating '%s'", pStr);
   }
   else if (xds.pi_rating_sys == XMLTV_RATING_MPAA)
   {
      // official MPAA ratings: G, PG, PG-13, R, NC-17
      // http://www.mpaa.org/movieratings/

      if ( (strcasecmp(pStr, "G") == 0) || (strcasecmp(pStr, "general") == 0) )
         xds.pi.parental_rating = 0;
      else if (strcasecmp(pStr, "PG") == 0)
         xds.pi.parental_rating = 6;
      else if (strcasecmp(pStr, "PG-13") == 0)
         xds.pi.parental_rating = 13;
      else if (strcasecmp(pStr, "R") == 0)
         xds.pi.parental_rating = 17;
      else if (strcasecmp(pStr, "NC-17") == 0)
         xds.pi.parental_rating = 18;
      else if (strncasecmp(pStr, "X", 1) == 0)
         xds.pi.parental_rating = 18;
      else
         debug1("Xmltv-PiRatingAddText: unknown MPAA rating '%s'", pStr);
   }
   else if (xds.pi_rating_sys == XMLTV_RATING_PL)
   {
      // Using format "NN+" where NN is numerical age - no special handling needed
      debug1("Xmltv-PiRatingAddText: unknown PL rating '%s'", pStr);
   }
   else
   {
      debug1("Xmltv-PiRatingAddText: no system set for rating '%s'", pStr);
   }
}

static void Xmltv_PiStarRatingSet( double cval, uint max_val )
{
   if ((cval <= max_val) && (max_val > 0))
   {
      xds.pi.editorial_rating = (uint8_t)(cval + 0.5);
      xds.pi.editorial_max_val = max_val;
   }
   else
      debug2("Xmltv-PiStarRatingSet: weird values: %f / %d", cval, max_val);
}

// DTD 0.5 only: rating in format "n / m"
void Xmltv_PiStarRatingAddText( XML_STR_BUF * pBuf )
{
   const char * pStr = XML_STR_BUF_GET_STR(*pBuf);
   int nscan;
   uint max_val;
   double cval;

   nscan = sscanf(pStr, "%lf / %u", &cval, &max_val);
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

void Xmltv_PiDescOpen( void )
{
}

void Xmltv_PiDescClose( void )
{
}

void Xmltv_ParagraphAdd( XML_STR_BUF * pBuf )
{
   // FIXME: should replace single newlines with space
   // double-newline in DTD 0.5 should be interpreted as paragraph breaks

   XmlCdata_AppendParagraph(&xds.pi_desc, FALSE);

   XmlCdata_AssignOrAppend(&xds.pi_desc, pBuf);

   xds.pi_desc_lang = XmltvTags_GetLanguage();
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

// date when the programme originally was produced (not air date)
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

   if (xds.chn_count > 0)
   {
      xds.pDbContext->netwopCount = xds.chn_count;
      xds.pDbContext->pFirstNetwopPi = xmalloc(xds.chn_count * sizeof(xds.pDbContext->pFirstNetwopPi[0]));
      memset(xds.pDbContext->pFirstNetwopPi, 0, xds.chn_count * sizeof(xds.pDbContext->pFirstNetwopPi[0]));

      // combine PI of different networks into a single database
      EpgDbMergeLinkNetworkPi(xds.pDbContext, xds.pFirstNetwopPi);

      //TODO PI+AI: EpgBlockCheckConsistancy(pBlock)

      // free pointer tables, as data is now owned by DB context
      xfree(xds.pFirstNetwopPi);
      xds.pFirstNetwopPi = NULL;

      xfree(xds.pLastNetwopPi);
      xds.pLastNetwopPi = NULL;

#if DEBUG_GLOBAL_SWITCH == ON
      EpgDbCheckChains(xds.pDbContext);
#endif
   }

   xds.pDbContext->pThemes = xds.p_theme_table;
   xds.pDbContext->themeCount = xds.theme_count;
   xds.p_theme_table = NULL;

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
            xfree((void*)xds.p_chn_table[idx].p_disp_name);  // cast to remove const
      }
      xfree(xds.p_chn_table);
   }
   if (xds.p_chn_id_tmp != NULL)
   {
      xfree(xds.p_chn_id_tmp);
      xds.p_chn_id_tmp = NULL;
   }
   if (xds.pLastNetwopPi != NULL)
   {
      for (uint idx = 0; idx < xds.chn_count; ++idx)
      {
         EPGDB_PI_BLOCK *pWalk = xds.pLastNetwopPi[idx];
         while (pWalk != NULL)
         {
            EPGDB_PI_BLOCK * pNext = pWalk->pPrevNetwopBlock;
            xfree(pWalk);
            pWalk = pNext;
         }
      }
      xfree(xds.pLastNetwopPi);
      xds.pLastNetwopPi = NULL;
   }
   if (xds.pFirstNetwopPi != NULL)
   {
      xfree(xds.pFirstNetwopPi);
      xds.pFirstNetwopPi = NULL;
   }
   if (xds.p_theme_table != NULL)
   {
      for (uint idx = 0; idx < xds.theme_count; ++idx)
      {
         xfree(xds.p_theme_table[idx]);
      }
      xfree(xds.p_theme_table);
      xds.p_theme_table = NULL;
   }
   XmlHash_Destroy(xds.p_chn_hash, NULL);
   XmlHash_Destroy(xds.p_theme_hash, NULL);

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
   if (xds.cniCtxInitDone)
   {
      XmltvCni_MapDestroy(&xds.cniCtx);
   }
}

// ----------------------------------------------------------------------------
// Initialize the local module state
//
void XmltvDb_Init( uint provCni, time_t mtime, bool isPeek )
{
   memset(&xds, 0, sizeof(xds));
   xds.p_chn_hash = XmlHash_Init();
   xds.p_theme_hash = XmlHash_Init();

   // set string buffer size hints
   XmlCdata_Init(&xds.pi_title, 256);
   XmlCdata_Init(&xds.pi_code_time_str, 256);
   XmlCdata_Init(&xds.pi_code_sv, 256);
   XmlCdata_Init(&xds.pi_code_vp, 256);
   XmlCdata_Init(&xds.pi_desc, 4096);

   xds.mtime = mtime;
   xds.isPeek = isPeek;

   // create empty database
   xds.pDbContext = EpgDbCreate();
   xds.pDbContext->provCni = provCni;
}

