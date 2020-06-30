/*
 *  XMLTV provider and network ID mapping
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
 *    This module maps channel identification strings provided in
 *    XMLTV source files to ETSI CNI codes.  It's also used to map
 *    paths and XMLTV channel names to pseudo-CNIs.  These mappings
 *    are stored in files to keep them constant across restarts and
 *    to allow to use them for configuration parameters in the
 *    GUI INI file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: xmltv_cni.c,v 1.8 2020/06/17 08:27:27 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_XMLTV
#define DPRINTF_OFF

#ifndef WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/cni_tables.h"
#include "epgui/rcfile.h"

#include "xmltv/xml_cdata.h"
#include "xmltv/xml_hash.h"
#include "xmltv/xmltv_cni.h"

#if !defined (XMLTV_CNI_MAP_PATH)  // should be defined in makefile
#define XMLTV_CNI_MAP_PATH "."
#endif
#define XMLTV_CNI_MAP_NAME "xmltv-etsi.map"

#ifndef WIN32
#define XMLTV_CNI_MAP_FULL_PATH XMLTV_CNI_MAP_PATH "/" XMLTV_CNI_MAP_NAME
#else
#define XMLTV_CNI_MAP_FULL_PATH XMLTV_CNI_MAP_PATH "\\" XMLTV_CNI_MAP_NAME
#endif

#ifndef WIN32
static char * pXmlCniCwd;
#endif

// ----------------------------------------------------------------------------
// Convert relative path to absolute path
// - the caller must free() the returned pointer, unless it's identical to
//   the passed argument
// 
static const char * XmltvCni_GetFullPath( const char * pPath )
{
   char * pBuf;

   if ((pPath != NULL) && (*pPath != 0))
   {
#ifndef WIN32
      // on UNIX a path is relative exactly if it doesn't start with a slash
      if (*pPath != '/')
      {
         pBuf = xmalloc(strlen(pXmlCniCwd) + 1 + strlen(pPath) + 1);
         if (strncmp(pPath, "./", 2) == 0)
            sprintf(pBuf, "%s/%s", pXmlCniCwd, pPath + 2);
         else
            sprintf(pBuf, "%s/%s", pXmlCniCwd, pPath);
         pPath = pBuf;
      }
#else
      DWORD retLen;

      pBuf = xmalloc(10*1024 + 2);
      if ((strncmp(pPath, "./", 2) == 0) || (strncmp(pPath, ".\\", 2) == 0))
         retLen = GetFullPathName(pPath + 2, 10*1024, pBuf, NULL);
      else
         retLen = GetFullPathName(pPath, 10*1024, pBuf, NULL);
      if ((retLen > 0) && (retLen < 10*1024))
      {
         dprintf2("XmltvCni-GetFullPath: '%s -> %s\n", pPath, pBuf);
         pPath = xrealloc(pBuf, retLen + 1);
      }
      else
      {
         debug3("XmltvCni-GetFullPath: GetFullPathName(%s) returned:%ld, error:%ld", pPath, retLen, GetLastError());
         xfree(pBuf);
      }
#endif
   }
   return pPath;
}

// ----------------------------------------------------------------------------
// Match source name against pattern in section header of map file
// - pattern may contain wildcard "*" at the start and end;
//   other reg-exp currently not supported
// - the pattern may be modified
//
static bool XmltvCni_MatchSectionName( char * pPattern, const char * pName )
{
   bool wildStart;
   bool wildEnd;
   uint patLen;
   uint namLen;
   bool match = FALSE;

   if ((pPattern != NULL) && (pName != NULL))
   {
      wildStart = wildEnd = FALSE;
      patLen = strlen(pPattern);
      namLen = strlen(pName);

      if (pPattern[0] == '*')
      {
         wildStart = TRUE;
         pPattern += 1;
         patLen -= 1;
      }
      if ((patLen > 1) && (pPattern[patLen - 1] == '*'))
      {
         wildEnd = TRUE;
         patLen -= 1;
         pPattern[patLen] = 0;
      }

      if (wildStart)
      {
         if (wildEnd)
         {
            match = (strstr(pName, pPattern) != NULL);
         }
         else
         {
            match = ((namLen >= patLen) &&
                     (strncmp(pName + namLen - patLen, pPattern, patLen) == 0));
         }
      }
      else
      {
         if (wildEnd)
         {
            match = (strncmp(pName, pPattern, patLen) == 0);
         }
         else
         {
            match = (strncmp(pName, pPattern, patLen) == 0) && (namLen == patLen);
         }
      }
      dprintf5("MATCH PAT '%s' in '%s': %d (wild:%d/%d)\n", pPattern, pName, match, wildStart, wildEnd);

   }
   return match;
}

// ----------------------------------------------------------------------------
// Read file which maps XMLTV channel IDs to ETSI CNIs
//
static void XmltvCni_LoadEtsiMap( XML_HASH_PTR pNameHash,
                                  const char * pSourceName, const char * pSourceUrl )
{
   const char * pFileName;
   char line[256], value[256];
   FILE * fp;
   uint * pCniVal;
   char * pl;
   sint cni;
   bool skip;
   bool isNew;

   fp = fopen((pFileName = XMLTV_CNI_MAP_NAME), "r");
   if (fp == NULL)
      fp = fopen((pFileName = XMLTV_CNI_MAP_FULL_PATH), "r");

   if (fp != NULL)
   {
      skip = TRUE;
      while (fgets(line, 255, fp) != NULL)
      {
         // skip empty lines and comments
         pl = line;
         while (*pl == ' ')
            pl++;
         if ((*pl == '\n') || (*pl == '\r') || (*pl == '#'))
            continue;

         if (sscanf(line,"[%99[^]]]", value) == 1)
         {
            if (strcmp(value, "GLOBAL") == 0)
            {
               skip = FALSE;
            }
            else
            {
               skip = !XmltvCni_MatchSectionName(value, pSourceName) &&
                      !XmltvCni_MatchSectionName(value, pSourceUrl);
            }
         }
         else if (sscanf(line," %x %250[^\n\r]", &cni, value) == 2)
         {
            if (cni != 0)
            {
               if (skip == FALSE)
               {
                  pCniVal = XmlHash_CreateEntry(pNameHash, value, &isNew);
                  if (isNew)
                  {
                     *pCniVal = cni;
                  }
                  else
                  {
                     debug2("XmltvCni-LoadEtsiMap: duplicate CNI 0x%04X for %s", cni, value);
                     fprintf(stderr, "%s: duplicate CNI '%04X' for %s\n", pFileName, cni, value);
                  }
               }
            }
            else
               debug0("XmltvCni-LoadEtsiMap: illegal CNI value 0 - skipping line");
         }
         else
            debug1("XmltvCni-LoadEtsiMap: skipping malformed line: %s", line);
      }

      fclose(fp);
   }
}

// ----------------------------------------------------------------------------
// Initialize the context struct for a provider network CNI table
// - must be called once before a database is loaded
//
void XmltvCni_MapInit( XMLTV_CNI_CTX * pCniCtx, uint provCni,
                       const char * pSourceName, const char * pSourceUrl )
{
   const RCFILE * pRc;
   uint * pCniVal;
   bool isNew;
   uint idx;

   assert(IS_XMLTV_CNI(provCni));
   assert((provCni & XMLTV_NET_CNI_MASK) == 0);

   pCniCtx->provCni = provCni;
   pCniCtx->freeCni = provCni + 1;
   pCniCtx->nameHash = XmlHash_Init();

   XmltvCni_LoadEtsiMap(pCniCtx->nameHash, pSourceName, pSourceUrl);

   // load channel IDs for this provider into an hash array
   pRc = RcFile_Query();
   for (idx = 0; idx < pRc->xmltv_nets_count; idx++)
   {
      if (pRc->xmltv_nets[idx].prov_cni == provCni)
      {
         pCniVal = XmlHash_CreateEntry(pCniCtx->nameHash, pRc->xmltv_nets[idx].chn_id, &isNew);
         if (isNew)
         {
            dprintf2("XmltvCni-MapInit: CNI 0x%05X '%s'\n", pRc->xmltv_nets[idx].net_cni, pRc->xmltv_nets[idx].chn_id);
            *pCniVal = pRc->xmltv_nets[idx].net_cni;

            if (*pCniVal >= pCniCtx->freeCni)
            {
               // new maximum
               pCniCtx->freeCni = *pCniVal + 1;
            }
         }
         else  // not an error if the user added an xmltv-etsi mapping (we still keep the old ID forever)
            debug3("XmltvCni-MapInit: duplicate channel ID '%s' have CNI 0x%05X, drop 0x%05X", pRc->xmltv_nets[idx].chn_id, *pCniVal, pRc->xmltv_nets[idx].net_cni);
      }
   }
}

// ----------------------------------------------------------------------------
// Free a context struct for a provider network CNI table
//
void XmltvCni_MapDestroy( XMLTV_CNI_CTX * pCniCtx )
{
   XmlHash_Destroy(pCniCtx->nameHash, NULL);
}

// ----------------------------------------------------------------------------
// Map an XMLTV channel ID string onto a CNI value
//
uint XmltvCni_MapNetCni( XMLTV_CNI_CTX * pCniCtx, const char * pChannelId )
{
   uint  * pCniVal;
   sint  scni;
   int   scan_pos;
   int   nscan;
   bool  is_new;

   assert(IS_XMLTV_CNI(pCniCtx->provCni));

   pCniVal = XmlHash_CreateEntry(pCniCtx->nameHash, pChannelId, &is_new);
   if (is_new)
   {
      nscan = sscanf(pChannelId, "CNI%04X%n", &scni, &scan_pos);
      if ( (nscan >= 1) && (scan_pos == 3+4) && (pChannelId[3+4] == 0) &&
           (scni != 0) && IS_NXTV_CNI(scni) )
      {
         // ID is already a CNI
         *pCniVal = (uint) scni;
      }
      else
      {
         // TODO check for overflow in "++" modulo XMLTV_NET_CNI_MASK
         *pCniVal = pCniCtx->freeCni++;

         RcFile_AddXmltvNetwork(pCniCtx->provCni, *pCniVal, pChannelId);
      }
      dprintf2("XmltvCni-MapNetCni: NEW %s -> CNI 0x%05X\n", pChannelId, *pCniVal);
   }
   return *pCniVal;
}

// ----------------------------------------------------------------------------
// Creates a table for mapping CNIs to XMLTV channel IDs during Nextview EPG export
// - uses the GLOBAL section of the XMLTV->CNI mapping table;
//   in case of duplicate CNIs the last definition is used
// - the caller must call the "free" function when done
//
bool XmltvCni_InitMapCni2Ids( XMLTV_CNI_REV_CTX * pCtx )
{
   const char * pFileName;
   char line[256], value[256];
   FILE * fp;
   char * pl;
   sint cni;
   bool skip;
   uint idx;

   pCtx->pMap = NULL;
   pCtx->maxMapLen = 0;
   pCtx->mapLen = 0;

   fp = fopen((pFileName = XMLTV_CNI_MAP_NAME), "r");
   if (fp == NULL)
      fp = fopen((pFileName = XMLTV_CNI_MAP_FULL_PATH), "r");

   if (fp != NULL)
   {
      pCtx->maxMapLen = 100;
      pCtx->pMap = xmalloc(pCtx->maxMapLen * sizeof(pCtx->pMap[0]));

      skip = TRUE;
      while (fgets(line, 255, fp) != NULL)
      {
         // skip empty lines and comments
         pl = line;
         while (*pl == ' ')
            pl++;
         if ((*pl == '\n') || (*pl == '\r') || (*pl == '#'))
            continue;

         if (sscanf(line,"[%99[^]]]", value) == 1)
         {
            skip = (strcmp(value, "GLOBAL") != 0) &&
                   (strcmp(value, "EXPORT|GLOBAL") != 0);
         }
         else if ( (sscanf(line," %x %250[^\n\r]", &cni, value) == 2) &&
                   (skip == FALSE) )
         {
            for (idx = 0; idx < pCtx->mapLen; idx++)
               if (pCtx->pMap[idx].cni == cni)
                  break;

            if (idx < pCtx->mapLen)
            {
               // duplicate name for this CNI - use the latter one
               debug3("XmltvCni-InitMapCni2Ids: duplicate CNI 0x%04X: have '%s' and '%s'", cni, pCtx->pMap[idx].pName, value);
               xfree(pCtx->pMap[idx].pName);
               pCtx->pMap[idx].pName = xstrdup(value);
            }
            else
            {
               if (pCtx->mapLen >= pCtx->maxMapLen)
               {
                  pCtx->maxMapLen += 100;
                  pCtx->pMap = xrealloc(pCtx->pMap, pCtx->maxMapLen * sizeof(pCtx->pMap[0]));
               }
               pCtx->pMap[pCtx->mapLen].pName = xstrdup(value);
               pCtx->pMap[pCtx->mapLen].cni = cni;
               pCtx->mapLen += 1;
            }
         }
      }

      fclose(fp);
   }

   return (pCtx->pMap != NULL);
}

// ----------------------------------------------------------------------------
// Frees the table for mapping CNIs to XMLTV channel IDs
//
void XmltvCni_FreeMapCni2Ids( XMLTV_CNI_REV_CTX * pCtx )
{
   uint idx;

   if (pCtx != NULL)
   {
      if (pCtx->pMap != NULL)
      {
         for (idx = 0; idx < pCtx->mapLen; idx++)
         {
            xfree(pCtx->pMap[idx].pName);
         }
         xfree(pCtx->pMap);
         pCtx->pMap = NULL;
      }
      pCtx->maxMapLen = 0;
      pCtx->mapLen = 0;
   }
}

// ----------------------------------------------------------------------------
// Map the given CNI value to a known XMLTV channel identifier string
// - returns NULL if there's no pre-defined ID for this CNI
//
const char * XmltvCni_MapCni2Ids( XMLTV_CNI_REV_CTX * pCtx, uint cni )
{
   const char * pName = NULL;
   uint  try;
   uint  idx;
   uint  cmpCni;
   uint  pdcCni;

   if (pCtx != NULL)
   {
      for (try = 0; (try <= 1) && (pName == NULL); try++)
      {
         for (idx = 0; idx < pCtx->mapLen; idx++)
         {
            cmpCni = pCtx->pMap[idx].cni;
            if (cmpCni == cni)
            {
               pName = pCtx->pMap[idx].pName;
               goto found;
            }

            if ((0xF0000 & cni) == 0)
            {
               switch (cmpCni >> 8)
               {
                  case 0x1D:  // country code for Germany
                  case 0x1A:  // country code for Autria
                  case 0x24:  // country code for Switzerland
                  case 0x77:  // country code for Ukraine
                     // discard the upper 4 bits of the country code
                     if ((cmpCni & 0x0FFF) == cni)
                     {
                        pName = pCtx->pMap[idx].pName;
                        goto found;
                     }
                     break;
                  default:
                     break;
               }
            }
         }
         pdcCni = CniConvertUnknownToPdc(cni);
         if (pdcCni == cni)
            break;
         cni = pdcCni;
      }
   }
found: ;
   return pName;
}

// ----------------------------------------------------------------------------
// Determine an unused CNI for a new provider
// - TODO: re-use gaps (e.g. by sorting)
//
static uint XmltvCni_GetFreeProvCni( void )
{
   const RCFILE * pRc;
   uint freeProvCni;
   uint idx;

   pRc = RcFile_Query();
   freeProvCni = XMLTV_PROV_CNI_BASE;

   for (idx = 0; idx < pRc->xmltv_prov_count; idx++)
   {
      if (pRc->xmltv_prov[idx].prov_cni >= freeProvCni)
      {
         freeProvCni = pRc->xmltv_prov[idx].prov_cni + XMLTV_PROV_CNI_DELTA;
      }
   }
   return freeProvCni;
}

// ----------------------------------------------------------------------------
// Map an XMLTV path onto a pseudo-CNI value
// - note XML "providers" never get CNIs in the 16-bit ETSI range
//
uint XmltvCni_MapProvider( const char * pXmlPath )
{
   const RCFILE * pRc;
   const char * pAbsPath;
   uint  idx;
   uint  cni = 0;

   pAbsPath = XmltvCni_GetFullPath(pXmlPath);

   pRc = RcFile_Query();
   for (idx = 0; idx < pRc->xmltv_prov_count; idx++)
   {
      if (strcmp(pRc->xmltv_prov[idx].path, pAbsPath) == 0)
      {
         cni = pRc->xmltv_prov[idx].prov_cni;
         break;
      }
   }

   // if not found -> create new CNI in rc file
   if (idx >= pRc->xmltv_prov_count)
   {
      cni = XmltvCni_GetFreeProvCni();

      RcFile_AddXmltvProvider(cni, pAbsPath);

      dprintf2("XmltvCni-MapProvider: assign CNI 0x%05X to '%s'\n", cni, pAbsPath);
   }

   if (pAbsPath != pXmlPath)
      xfree((void*)pAbsPath);

   return cni;
}

// ----------------------------------------------------------------------------
// Reverse look-up of provider XML path by giving an CNI
// - returns NULL if no path is mapped to the given CNI
// - else a pointer to internal storage is returned (must not be freed by the caller)
//
const char * XmltvCni_LookupProviderPath( uint provCni )
{
   const char * pXmlPath = NULL;
   const RCFILE * pRc;
   uint  idx;

   pRc = RcFile_Query();
   for (idx = 0; idx < pRc->xmltv_prov_count; idx++)
   {
      if (pRc->xmltv_prov[idx].prov_cni == provCni)
      {
         pXmlPath = pRc->xmltv_prov[idx].path;
         break;
      }
   }

   return pXmlPath;
}

// ----------------------------------------------------------------------------
// Free resources
//
void XmltvCni_Destroy( void )
{
#ifndef WIN32
   if (pXmlCniCwd != NULL)
   {
      xfree(pXmlCniCwd);
      pXmlCniCwd = NULL;
   }
   else
      debug0("XmltvCni-Destroy: duplicate call or uninitialized");
#endif
}

// ----------------------------------------------------------------------------
// Initialize the module
//
void XmltvCni_Init( void )
{
#ifndef WIN32
   // determine path to current directory once for conversions to absolute path
   pXmlCniCwd = xmalloc(10*1024+2);
   if (getcwd(pXmlCniCwd, 10*1024) == NULL)
      *pXmlCniCwd = 0;
   pXmlCniCwd = xrealloc(pXmlCniCwd, strlen(pXmlCniCwd) + 1);
   dprintf1("XmltvCni-Init: cwd:%s\n", pXmlCniCwd);
#endif
}

