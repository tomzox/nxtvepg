/*
 *  XMLTV provider and network ID mapping
 *
 *  Copyright (C) 2007-2011, 2020-2023 T. Zoerner
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

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgui/rcfile.h"

#include "xmltv/xml_cdata.h"
#include "xmltv/xml_hash.h"
#include "xmltv/xmltv_cni.h"

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
      pBuf = realpath(pPath, NULL);
      if (pBuf != NULL)
      {
         pPath = xstrdup(pBuf);
         free(pBuf);
      }
      else if (*pPath != '/')
      {
         // on UNIX a path is relative exactly if it doesn't start with a slash
         pBuf = (char*) xmalloc(strlen(pXmlCniCwd) + 1 + strlen(pPath) + 1);
         if (strncmp(pPath, "./", 2) == 0)
            sprintf(pBuf, "%s/%s", pXmlCniCwd, pPath + 2);
         else
            sprintf(pBuf, "%s/%s", pXmlCniCwd, pPath);
         pPath = pBuf;
      }
#else
      DWORD retLen;

      pBuf = (char*) xmalloc(10*1024 + 2);
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
// Initialize the context struct for a provider network CNI table
// - must be called once before a database is loaded
//
void XmltvCni_MapInit( XMLTV_CNI_CTX * pCniCtx, uint provCni )
{
   const RCFILE * pRc;
   uint * pCniVal;
   bool isNew;
   uint idx;

   assert((provCni & XMLTV_NET_CNI_MASK) == 0);

   pCniCtx->provCni = provCni;
   pCniCtx->freeCni = provCni + 1;
   pCniCtx->nameHash = XmlHash_Init();

   // load channel IDs for this provider into an hash array
   pRc = RcFile_Query();
   for (idx = 0; idx < pRc->xmltv_nets_count; idx++)
   {
      if (pRc->xmltv_nets[idx].prov_cni == provCni)
      {
         pCniVal = (uint*) XmlHash_CreateEntry(pCniCtx->nameHash, pRc->xmltv_nets[idx].chn_id, &isNew);
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
   bool  is_new;

   pCniVal = (uint*) XmlHash_CreateEntry(pCniCtx->nameHash, pChannelId, &is_new);
   if (is_new)
   {
      // TODO check for overflow in "++" modulo XMLTV_NET_CNI_MASK
      *pCniVal = pCniCtx->freeCni++;

      RcFile_AddXmltvNetwork(pCniCtx->provCni, *pCniVal, pChannelId);

      dprintf2("XmltvCni-MapNetCni: NEW %s -> CNI 0x%05X\n", pChannelId, *pCniVal);
   }
   return *pCniVal;
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
   pXmlCniCwd = (char*) xmalloc(10*1024+2);
   if (getcwd(pXmlCniCwd, 10*1024) == NULL)
      *pXmlCniCwd = 0;
   pXmlCniCwd = (char*) xrealloc(pXmlCniCwd, strlen(pXmlCniCwd) + 1);
   dprintf1("XmltvCni-Init: cwd:%s\n", pXmlCniCwd);
#endif
}

