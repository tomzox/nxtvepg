/*
 *  Nextview EPG acquisition and database parameter setup
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
 *    The module is a loose collection of helper functions which pass
 *    parameters from the command line and/or config file (rc/ini file)
 *    the various EPG control layer modules.  The functions are used
 *    during initial start-up and re-configuration via GUI dialogs.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgsetup.c,v 1.12 2011/01/16 20:27:55 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmerge.h"
#include "epgdb/ttxgrab.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgctxmerge.h"
#include "epgctl/epgctxctl.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/cni_tables.h"
#include "epgvbi/btdrv.h"
#include "epgtcl/dlg_hwcfg.h"
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgui/rcfile.h"
#include "epgui/cmdline.h"
#include "epgui/wintvcfg.h"
#include "epgui/epgsetup.h"
#include "xmltv/xmltv_cni.h"

#define NORM_CNI(C)  (IS_NXTV_CNI(C) ? CniConvertUnknownToPdc(C) : (C))

// ----------------------------------------------------------------------------
// Determine a network's name
// 
const char * EpgSetup_GetNetName( const AI_BLOCK * pAiBlock, uint netIdx, bool * pIsFromAi )
{
   const char * pResult;
   bool isFromAi = FALSE;

   if ((pAiBlock != NULL) && (netIdx < pAiBlock->netwopCount))
   {
      // first check for user-configured name
      pResult = RcFile_GetNetworkName(AI_GET_NET_CNI_N(pAiBlock, netIdx));

      // if none found, get name from provider's network table
      if (pResult == NULL)
      {
         pResult = AI_GET_NETWOP_NAME(pAiBlock, netIdx);
         isFromAi = TRUE;
      }
   }
   else
   {
      debug1("EpgSetup-GetNetName: no AI block or invalid netwop #%d", netIdx);
      pResult = "";
   }

   if (pIsFromAi != NULL)
   {
      *pIsFromAi = isFromAi;
   }
   return pResult;
}

// ---------------------------------------------------------------------------
// Query or guess the language of texts in the database
//
uint EpgSetup_GetDefaultLang( EPGDB_CONTEXT * pDbContext )
{
   const AI_BLOCK * pAiBlock;
   uint lang = 7;

   if (pDbContext != NULL)
   {
#ifdef USE_XMLTV_IMPORT
      if (pDbContext->xmltv)
      {
         // TODO: guess from XMLTV:lang attribute
      }
      else
#endif
      {
         EpgDbLockDatabase(pDbContext, TRUE);
         pAiBlock = EpgDbGetAi(pDbContext);
         if (pAiBlock != NULL)
         {
            if (pAiBlock->thisNetwop < pAiBlock->netwopCount)
            {
               lang = AI_GET_NETWOP_N(pAiBlock, pAiBlock->thisNetwop)->alphabet;
            }
         }
         EpgDbLockDatabase(pDbContext, FALSE);
      }
   }
   return lang;
}

// ---------------------------------------------------------------------------
// Update network selection configuration after AI update, if necessary
//
void EpgSetup_UpdateProvCniTable( void )
{
   const RCFILE * pRc;
   const AI_BLOCK * pAiBlock;
   const AI_NETWOP *pNetwop;
   const RCFILE_NET_ORDER * pOrdList;
   const RCFILE_NET_ORDER * pSupList;
   uint  * pAiCni, * pNewOrdList;
   uint  newOrdCount;
   uint  netIdx;
   uint  aiIdx, rcIdx;
   uint  provCni, cni;
   bool  update;

   EpgDbLockDatabase(pUiDbContext, TRUE);
   pAiBlock = EpgDbGetAi(pUiDbContext);
   if ((pAiBlock != NULL) && (pAiBlock->netwopCount > 0))
   {
      // fetch CNI list from AI block in database
      pAiCni = xmalloc(sizeof(uint) * pAiBlock->netwopCount);
      pNetwop = AI_GET_NETWOPS(pAiBlock);
      for ( aiIdx = 0; aiIdx < pAiBlock->netwopCount; aiIdx++, pNetwop++ ) 
      {
         pAiCni[aiIdx] = AI_GET_NET_CNI(pNetwop);
      }
      provCni = EpgDbContextGetCni(pUiDbContext);

      // fetch network selection
      pRc = RcFile_Query();
      pOrdList = NULL;
      pSupList = NULL;
      update = FALSE;

      for (rcIdx = 0; rcIdx < pRc->net_order_count; rcIdx++)
      {
         if (pRc->net_order[rcIdx].prov_cni == provCni)
         {
            if (pRc->net_order[rcIdx].add_sub != 0)
               pOrdList = &pRc->net_order[rcIdx];
            else
               pSupList = &pRc->net_order[rcIdx];
         }
      }

      // step #1: collect user-selected networks in the user-defined order
      pNewOrdList = xmalloc(sizeof(uint) * pAiBlock->netwopCount);
      newOrdCount = 0;
      if (pOrdList != NULL)
      {
         for (netIdx = 0; netIdx < pOrdList->net_count; netIdx++)
         {
            cni = pOrdList->net_cnis[netIdx];
            for (aiIdx = 0; aiIdx < pAiBlock->netwopCount; aiIdx++)
            {
               if (pAiCni[aiIdx] == cni)
               {
                  break;
               }
               else if (NORM_CNI(pAiCni[aiIdx]) == NORM_CNI(cni))
               {
                  cni = pAiCni[aiIdx];
                  update = TRUE;
                  break;
               }
            }
            if (aiIdx < pAiBlock->netwopCount)
            {
               pAiCni[aiIdx] = ~0u;
               pNewOrdList[newOrdCount] = cni;
               newOrdCount += 1;
            }
            else
            {  // CNI no longer exists in AI
               update = TRUE;
            }
         }
      }

      if (provCni != MERGED_PROV_CNI)
      {
         // step #2: remove suppressed CNIs from the list
         if (pSupList != NULL)
         {
            for (netIdx = 0; netIdx < pSupList->net_count; netIdx++)
            {
               cni = pSupList->net_cnis[netIdx];
               for (aiIdx = 0; aiIdx < pAiBlock->netwopCount; aiIdx++)
                  if (NORM_CNI(pAiCni[aiIdx]) == NORM_CNI(cni))
                     break;
               if (aiIdx < pAiBlock->netwopCount)
               {
                  pAiCni[aiIdx] = ~0u;
               }
               else
               {  // CNI no longer exists in AI - ignored, keep the CNI in the list
               }
            }
         }

         // step #3: check for unreferenced CNIs and append them to the list
         for (aiIdx = 0; aiIdx < pAiBlock->netwopCount; aiIdx++)
         {
            if (pAiCni[aiIdx] != ~0u)
            {
               pNewOrdList[newOrdCount] = pAiCni[aiIdx];
               newOrdCount += 1;
               update = TRUE;
            }
         }
      }

      if (update)
      {
         RcFile_UpdateNetworkSelection(provCni, newOrdCount, pNewOrdList, 0, NULL);
      }

      xfree(pNewOrdList);
      xfree(pAiCni);
   }
   EpgDbLockDatabase(pUiDbContext, FALSE);
}

// ----------------------------------------------------------------------------
// Enable network prefilter to permanently exclude a subset of networks
// - called after provider selection and during acquisition after AI update
//
void EpgSetup_SetNetwopPrefilter( EPGDB_CONTEXT * dbc, FILTER_CONTEXT * fc )
{
   const AI_BLOCK * pAiBlock;
   const uint * pSupCnis;
   uint cniCount;
   uint netwop;
   uint cniIdx;
   uint cni;

   if ((dbc != NULL) && (fc != NULL))
   {
      RcFile_GetNetworkSelection(EpgDbContextGetCni(dbc), NULL, NULL, &cniCount, &pSupCnis);

      EpgDbLockDatabase(dbc, TRUE);
      pAiBlock = EpgDbGetAi(dbc);
      if (pAiBlock != NULL)
      {
         EpgDbFilterInitNetwopPreFilter(fc);
         EpgDbPreFilterEnable(fc, FILTER_NETWOP_PRE);

         for (netwop = 0; netwop < pAiBlock->netwopCount; netwop++) 
         {
            cni = AI_GET_NET_CNI_N(pAiBlock, netwop);
            for (cniIdx = 0; cniIdx < cniCount; cniIdx++) 
               if (pSupCnis[cniIdx] == cni)
                  break;
            // if this network's CNI is in the exclude list: add it to the pre-filter
            if (cniIdx < cniCount)
            {
               EpgDbFilterSetNetwopPreFilter(fc, netwop);
            }
         }
      }
      EpgDbLockDatabase(dbc, FALSE);
   }
   else
      fatal0("EpgSetup-SetNetwopPrefilter: illegal NULL pointer param");
}

// ---------------------------------------------------------------------------
// Build default network CNI list for merged DB if none is configured yet
// - include those CNIs which are selected
//   user hasn't had a chance to invoke the network selection -> must include
//   all CNIs of all providers
//
static bool EpgSetup_InitMergeDbNetwops( uint provCniCount, const uint * pProvCniTab,
                                         uint * pCniCount, uint * pCniTab )
{
   EPGDB_CONTEXT  * pPeek;
   const AI_BLOCK  * pAiBlock;
   const AI_NETWOP * pNetwops;
   const uint * pCfSelCni;
   uint cfNetCount;
   uint netIdx;
   uint dbIdx;
   uint prevIdx;
   uint netwopCount;
   uint cni;

   netwopCount = 0;
   for (dbIdx = 0; dbIdx < provCniCount; dbIdx++)
   {
      // search RC for the "include" network CNI list for this provider
      RcFile_GetNetworkSelection(pProvCniTab[dbIdx], &cfNetCount, &pCfSelCni, NULL, NULL);

      if (pCfSelCni != NULL)
      {
         for (netIdx = 0; (netIdx < cfNetCount) && (netwopCount < MAX_NETWOP_COUNT); netIdx++)
         {
            cni = pCfSelCni[netIdx];

            // check if the CNI already is in the generated table
            for (prevIdx = 0; prevIdx < netwopCount; prevIdx++)
               if (NORM_CNI(cni) == NORM_CNI(pCniTab[prevIdx]))
                  break;

            // if not found, append
            if (prevIdx >= netwopCount)
            {
               dprintf2("EpgSetup-InitMergeDbNetwops: add CNI 0x%04X of DB 0x%04X\n", cni, pProvCniTab[dbIdx]);
               pCniTab[netwopCount] = cni;
               netwopCount += 1;
            }
         }
      }
      else
      {  // no network list configured for this provider -> include all networks in the DB

         pPeek = EpgContextCtl_Peek(pProvCniTab[dbIdx], CTX_RELOAD_ERR_NONE);
         if (pPeek != NULL)
         {
            EpgDbLockDatabase(pPeek, TRUE);
            pAiBlock = EpgDbGetAi(pPeek);
            if (pAiBlock != NULL)
            {
               pNetwops = AI_GET_NETWOPS(pAiBlock);

               for (netIdx = 0; (netIdx < pAiBlock->netwopCount) && (netwopCount < MAX_NETWOP_COUNT); netIdx++, pNetwops++)
               {
                  cni = AI_GET_NET_CNI(pNetwops);

                  // check if the CNI already is in the generated table
                  for (prevIdx = 0; prevIdx < netwopCount; prevIdx++)
                     if (cni == pCniTab[prevIdx])
                        break;

                  // if not found, append
                  if (prevIdx >= netwopCount)
                  {
                     dprintf3("EpgSetup-InitMergeDbNetwops: add net #%d: CNI 0x%04X of DB 0x%04X\n", netIdx, cni, pProvCniTab[dbIdx]);
                     pCniTab[netwopCount] = cni;
                     netwopCount += 1;
                  }
               }
            }
            EpgDbLockDatabase(pPeek, FALSE);
            EpgContextCtl_ClosePeek(pPeek);
         }
         else
            debug1("EpgSetup-InitMergeDbNetwops: failed to peek DB 0x%04X (ignored)\n", pProvCniTab[dbIdx]);
      }
   }
   *pCniCount = netwopCount;
   return TRUE;
}

// ----------------------------------------------------------------------------
// Retrieve network CNI list for the merged database
// - it's not an error if no list is configured (yet);
//   (in this case all networks are included)
//
static bool EpgSetup_GetMergeDbNetwops( uint * pCniCount, uint * pCniTab )
{
   const uint * pCfSelCni;
   uint cfNetCount;
   bool result = FALSE;

   *pCniCount = 0;

   RcFile_GetNetworkSelection(MERGED_PROV_CNI, &cfNetCount, &pCfSelCni, NULL, NULL);
   if (pCfSelCni != NULL)
   {
      if (cfNetCount > 0)
      {
         *pCniCount = cfNetCount;
         if (*pCniCount > MAX_NETWOP_COUNT)
            *pCniCount = MAX_NETWOP_COUNT;

         memcpy(pCniTab, pCfSelCni, cfNetCount * sizeof(pCniTab[0]));
         result = TRUE;
      }
      else
         debug0("EpgSetup-GetMergeDbNetwops: warning: network CNI list for merged db is empty");
   }
   return result;
}

// ----------------------------------------------------------------------------
// Convert merge options in rc file into attribute matrix
// - for each datatype the user can configure an ordered list of providers
//   from which the data is taken during the merge
// - the rc file identifies providers by CNIs - those are converted into indices
//   here (i.e. indices into the main provider list)
//
static bool
EpgSetup_GetMergeProviders( uint *pCniCount, uint * pCniTab, MERGE_ATTRIB_VECTOR_PTR pMax )
{
   const RCFILE * pRc;
   uint idx, idx2, ati;

   pRc = RcFile_Query();

   *pCniCount = pRc->db.prov_merge_count;
   memcpy(pCniTab, pRc->db.prov_merge_cnis, pRc->db.prov_merge_count * sizeof(uint));

   for (ati=0; ati < MERGE_TYPE_COUNT; ati++)
   {
      // special case empty CNI list: first CNI is 0xFF in rc file's list
      if ( (pRc->db.prov_merge_opt_count[ati] == 1) &&
           (pRc->db.prov_merge_opts[ati][0] == 0xff) )
      {
         for (idx = 0; idx < *pCniCount; idx++)
            pMax[ati][idx] = 0xff;
      }
      else
      {
         if (pRc->db.prov_merge_opt_count[ati] == 0)
         {  // special case empty list -> default 1:1 mapping
            for (idx = 0; idx < *pCniCount; idx++)
               pMax[ati][idx] = idx;
         }
         else
         {
            for (idx = 0; idx < pRc->db.prov_merge_opt_count[ati]; idx++)
            {
               // search option CNI in provider CNI list
               for (idx2 = 0; idx2 < pRc->db.prov_merge_count; idx2++)
               {
                  if (pRc->db.prov_merge_cnis[idx2] == pRc->db.prov_merge_opts[ati][idx])
                  {
                     pMax[ati][idx] = idx2;
                     break;
                  }
               }
            }
            for ( ; idx < *pCniCount; idx++)
               pMax[ati][idx] = 0xff;
         }
      }
   }
   return (*pCniCount > 0);
}

// ----------------------------------------------------------------------------
// Merge EPG data in one or more databases into a new database
// - list of providers and networks is read from the config storage
//
EPGDB_CONTEXT * EpgSetup_MergeDatabases( void )
{
   EPGDB_CONTEXT * pDbContext;
   MERGE_ATTRIB_MATRIX max;
   uint  expireTime;
   uint  pProvCniTab[MAX_MERGED_DB_COUNT];
   uint  provCount;
   uint  netwopCniTab[MAX_NETWOP_COUNT];
   uint  netwopCount;
   uint  idx;
   time_t now;

   if ( (EpgSetup_GetMergeProviders(&provCount, pProvCniTab, &max[0]) ) &&
        (EpgSetup_GetMergeDbNetwops(&netwopCount, netwopCniTab) ||
         EpgSetup_InitMergeDbNetwops(provCount, pProvCniTab, &netwopCount, netwopCniTab)) )
   {
      expireTime = RcFile_Query()->db.piexpire_cutoff * 60;

      pDbContext = EpgContextMerge(provCount, pProvCniTab, max, expireTime,
                                   netwopCount, netwopCniTab);

      if (pDbContext != NULL)
      {
         // put the fake "Merge" CNI plus the CNIs of all merged providers
         // at the front of the provider selection order
         RcFile_UpdateMergedProvSelection();

         // update access time and count for all merged providers (XMLTV only)
         now = time(NULL);
         for (idx = 0; idx < provCount; idx++)
         {
            if (IS_XMLTV_CNI(pProvCniTab[idx]))
            {
               RcFile_UpdateXmltvProvAtime(pProvCniTab[idx], now, TRUE);
            }
         }
      }
   }
   else
   {
      UiControlMsg_ReloadError(MERGED_PROV_CNI, EPGDB_RELOAD_MERGE, CTX_RELOAD_ERR_REQ, FALSE);
      pDbContext = NULL;
   }
   return pDbContext;
}

// ----------------------------------------------------------------------------
// Open the initial database after program start
// - files can be chosen on the command line
// - else try all dbs in list list of previously opened ones (saved in rc/ini file)
// - else scan the db directory or create an empty database
//
void EpgSetup_OpenUiDb( void )
{
   const RCFILE * pRc;
   const char * const * pXmlFiles;
   uint provCnt;
   uint cni;

   // prepare list of previously opened databases
   pRc = RcFile_Query();

   // get list of names on the command line, if any
   provCnt = CmdLine_GetXmlFileNames(&pXmlFiles);

   if (provCnt > 1)
   {  // merge all files given on the command line
      uint cniList[MAX_MERGED_DB_COUNT];
      assert(provCnt <= MAX_MERGED_DB_COUNT);  // checked by command line parser

      for (uint idx = 0; idx < provCnt; ++idx)
      {
         cniList[idx] = XmltvCni_MapProvider(pXmlFiles[idx]);
      }
      RcFile_UpdateDbMergeCnis(cniList, provCnt);
      cni = MERGED_PROV_CNI;
   }
   else if (provCnt > 0)
   {
      cni = XmltvCni_MapProvider(pXmlFiles[0]);
   }
   else  // use previously selected provider
   {
      if (pRc->db.prov_sel_count > 0)
      {  // try the last used provider
         cni = pRc->db.prov_selection[0];
      }
      else
      {  // create an empty dummy database
         // (CNI value 0 has a special handling in the context open function)
         cni = 0;
      }
   }

   if (cni == MERGED_PROV_CNI)
   {  // special case: merged db
      pUiDbContext = EpgSetup_MergeDatabases();
      if (pUiDbContext == NULL)
         pUiDbContext = EpgContextCtl_OpenDummy();
   }
   else if (cni != 0)
   {  // regular database
      pUiDbContext = EpgContextCtl_Open(cni, FALSE, CTX_RELOAD_ERR_REQ);

      if (pUiDbContext == NULL)
      {
         pUiDbContext = EpgContextCtl_OpenDummy();
      }
      else if (EpgDbContextGetCni(pUiDbContext) == cni)
      {
         RcFile_UpdateXmltvProvAtime(cni, time(NULL), TRUE);
      }
   }
   else
   {  // no CNI specified -> dummy db
      pUiDbContext = EpgContextCtl_OpenDummy();
   }

   // note: the usual provider change events are not triggered here because
   // at the time this function is called the other modules are not yet initialized.
}

// ----------------------------------------------------------------------------
// Pass expire time delta configuration setting to database layer
// - executed during startup and when the user changes the setting
//
void EpgSetup_DbExpireDelay( void )
{
   uint  expireTime;

   expireTime = RcFile_Query()->db.piexpire_cutoff * 60;

   EpgContextCtl_SetPiExpireDelay(expireTime);
}

#ifdef USE_TTX_GRABBER
// ----------------------------------------------------------------------------
// Get XML file name table and frequencies for teletext grabber
// - note: the caller must free the returned pointers, if non-NULL
//
static bool EpgSetup_GetTtxConfig( uint * pCount, char ** ppNames, EPGACQ_TUNER_PAR ** ppFreq )
{
   const RCFILE * pRc;
   char * ps;
   uint chnIdx;
   bool result = FALSE;

   *pCount = 0;
   *ppFreq = NULL;
   *ppNames = NULL;

   pRc = RcFile_Query();

   if ( pRc->ttx.ttx_enable )
   {
      if (pRc->ttx.ttx_chn_count > 0)
      {
         if ( WintvCfg_GetFreqTab(ppNames, ppFreq, pCount, NULL) && (*pCount > 0) )
         {
            if (*pCount > pRc->ttx.ttx_chn_count)
            {
               *pCount = pRc->ttx.ttx_chn_count;
            }
            dprintf1("EpgSetup-GetTtxConfig: TTX acq config on %d channels\n", *pCount);

            ps = *ppNames;
            for (chnIdx = 0; chnIdx < *pCount; chnIdx++)
            {
               //dprintf3("TTX channel #%d: '%s' freq:%d\n", chnIdx, ps, (*ppFreq)[chnIdx]);

               while (*ps != 0)
               {
                  if ( ((*ps >= 'A') && (*ps <= 'Z')) ||
                       ((*ps >= 'a') && (*ps <= 'z')) ||
                       ((*ps >= '0') && (*ps <= '9')) )
                  {
                     // keep character
                     ps++;
                  }
                  else
                  {  // replace illegal character
                     *(ps++) = '_';
                  }
               }
               // skip terminating zero (strings in table are zero-separated)
               ps++;
            }
            result = TRUE;
         }
         else
            debug0("EpgSetup-GetTtxConfig: failed to read channel table, or len zero");
      }
      else
         debug0("EpgSetup-GetTtxConfig: TTX enabled but channel count zero");
   }
   return result;
}
#endif // USE_TTX_GRABBER

// ----------------------------------------------------------------------------
// Pass acq mode and CNI list to acquisition control
// - called after the user leaves acq mode dialog or client/server dialog
//   with "Ok", plus whenever the browser database provider is changed
//   -> acq ctl must check if parameters have changed before resetting acq
// - this function is not used by the acquisition daemon; here on client-side
//   network mode equals follow-ui because only content that's currently used
//   in the display should be forwarded from the server
// - if no valid config is found, the default mode is used: Follow-UI
//
void EpgSetup_AcquisitionMode( NETACQ_SET_MODE netAcqSetMode )
{
#ifdef USE_TTX_GRABBER
   const RCFILE * pRc;
   EPGACQ_TUNER_PAR * pTtxFreqs;
   char       * pTtxNames;
   uint         ttxFreqCount;
   bool         isNetAcqDefault;
   bool         doNetAcq;
   EPGACQ_DESCR acqState;
   EPGACQ_MODE  mode;

   pRc = RcFile_Query();
   if ((pRc != NULL) && (pRc->acq.acq_mode < ACQMODE_COUNT))
   {
      mode = pRc->acq.acq_mode;
   }
   else
   {  // unrecognized mode -> fall back to default mode
      mode = ACQMODE_CYCLIC_2;
   }

   // check if network client mode is enabled
   isNetAcqDefault = IsRemoteAcqDefault();
   switch (netAcqSetMode)
   {
      case NETACQ_DEFAULT:
         doNetAcq = isNetAcqDefault;
         break;
      case NETACQ_INVERT:
         doNetAcq = ! isNetAcqDefault;
         break;
      case NETACQ_KEEP:
         EpgAcqCtl_DescribeAcqState(&acqState);
         if (acqState.isNetAcq)
            doNetAcq = TRUE;
         else if (acqState.ttxGrabState == ACQDESCR_DISABLED)
            doNetAcq = isNetAcqDefault;
         else
            doNetAcq = FALSE;
         break;
      case NETACQ_YES:
         doNetAcq = TRUE;
         break;
      case NETACQ_NO:
         doNetAcq = FALSE;
         break;
      default:
         debug1("EpgSetup-AcquisitionMode: illegal net acq set mode %d", netAcqSetMode);
         doNetAcq = isNetAcqDefault;
         break;
   }
   if (doNetAcq)
      mode = ACQMODE_NETWORK;

   pTtxFreqs = NULL;
   pTtxNames = NULL;
   ttxFreqCount = 0;
   if (EpgSetup_GetTtxConfig(&ttxFreqCount, &pTtxNames, &pTtxFreqs))
   {
      // pass the params to the acquisition control module
      EpgAcqCtl_SelectMode(mode, ACQMODE_COUNT, ttxFreqCount, pTtxNames, pTtxFreqs,
                           pRc->ttx.ttx_start_pg, pRc->ttx.ttx_end_pg, pRc->ttx.ttx_duration);
   }

   if (pTtxNames != NULL)
      xfree(pTtxNames);
   if (pTtxFreqs != NULL)
      xfree(pTtxFreqs);
#endif
}

// ----------------------------------------------------------------------------
// Pass acq mode and CNI list to acquisition control
// - in Follow-UI mode the browser CNI must be determined here since
//   no db is opened for the browser
//
bool EpgSetup_DaemonAcquisitionMode( bool forcePassive, int maxPhase )
{
   bool result = FALSE;
#ifdef USE_TTX_GRABBER
#ifdef USE_DAEMON
   const RCFILE * pRc = RcFile_Query();
   EPGACQ_MODE   mode;
   EPGACQ_TUNER_PAR * pTtxFreqs;
   char        * pTtxNames;
   uint          ttxFreqCount;

   if (forcePassive)
   {  // -acqpassive given on command line
      mode = ACQMODE_PASSIVE;
   }
   else
   {  // else use acq mode from rc/ini file
      mode = pRc->acq.acq_mode;
      if (mode >= ACQMODE_COUNT)
         EpgNetIo_Logger(LOG_ERR, -1, 0, "acqmode parameters error", NULL);
   }

   if (mode < ACQMODE_COUNT)
   {
      pTtxFreqs = NULL;
      pTtxNames = NULL;
      ttxFreqCount = 0;
      if (EpgSetup_GetTtxConfig(&ttxFreqCount, &pTtxNames, &pTtxFreqs))
      {
         // pass the params to the acquisition control module
         result = EpgAcqCtl_SelectMode(mode, maxPhase,
                                       ttxFreqCount, pTtxNames, pTtxFreqs,
                                       pRc->ttx.ttx_start_pg,
                                       pRc->ttx.ttx_end_pg,
                                       pRc->ttx.ttx_duration);
      }

      if (pTtxNames != NULL)
         xfree(pTtxNames);
      if (pTtxFreqs != NULL)
         xfree(pTtxFreqs);
   }
#endif  // USE_DAEMON
#endif  // USE_TTX_GRABBER

   return result;
}

// ----------------------------------------------------------------------------
// Set the hardware config params
// - called at startup or after user configuration via the GUI
// - the card index can also be set via command line and is passed here
//   from main; a value of -1 means don't care
// - result indicates if driver is OK for acquisition start
//
bool EpgSetup_CardDriver( int newCardIndex )
{
   const RCFILE * pRc = RcFile_Query();
   RCFILE_TVCARD rcCard;
   int  drvType, cardIdx, input, prio, slicer;
   bool result = FALSE;

   drvType = pRc->tvcard.drv_type;
   cardIdx = pRc->tvcard.card_idx;
   input   = pRc->tvcard.input;
   prio    = pRc->tvcard.acq_prio;
   slicer  = pRc->tvcard.slicer_type;

   if ((newCardIndex >= 0) && (newCardIndex != cardIdx))
   {
      // different card idx passed via command line
      cardIdx = newCardIndex;

      // store index in RC file (note: must be done else GUI doesn't show correct card)
      rcCard = pRc->tvcard;
      rcCard.card_idx = cardIdx;
      RcFile_SetTvCard(&rcCard);

      UpdateRcFile(TRUE);
   }

   if (drvType == BTDRV_SOURCE_UNDEF)
   {
      drvType = BtDriver_GetDefaultDrvType();
   }

   if (drvType == BTDRV_SOURCE_NONE)
   {
      BtDriver_Configure(cardIdx, drvType, prio);
      EpgAcqCtl_Stop();
   }
   else
   {
      // pass the hardware config params to the driver
      if (BtDriver_Configure(cardIdx, drvType, prio))
      {
         // pass the input selection to acquisition control
         EpgAcqCtl_SetInputSource(input, slicer);
         result = TRUE;
      }
      else
      {
         debug1("EpgSetup-CardDriver: setup for cardIdx:%d failed\n", newCardIndex);
         EpgAcqCtl_Stop();
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Check if the selected TV card is configured properly
//
bool EpgSetup_CheckTvCardConfig( void )
{
   const RCFILE * pRc;
   uint  drvType, cardIdx, input;
   bool  result = FALSE;

   pRc = RcFile_Query();

   drvType = pRc->tvcard.drv_type;
   cardIdx = pRc->tvcard.card_idx;
   input = pRc->tvcard.input;

   if (drvType == BTDRV_SOURCE_UNDEF)
   {
      result = FALSE;
   }
   else if (drvType == BTDRV_SOURCE_NONE)
   {
      result = TRUE;
   }
   else
   {
      result = BtDriver_CheckCardParams(drvType, cardIdx, input);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Check if a TV card is available
//
bool EpgSetup_HasLocalTvCard( void )
{
   return (RcFile_Query()->tvcard.drv_type != BTDRV_SOURCE_NONE);
}

// ----------------------------------------------------------------------------
// Pass network connection and server configuration down to acq control
//
void EpgSetup_NetAcq( bool isServer )
{
#ifdef USE_DAEMON
   const RCFILE * pRc = RcFile_Query();

   // apply the parameters: pass them to the client/server module
   if (isServer)
   {
      // XXX TODO: remote_ctl
      EpgAcqServer_SetMaxConn(pRc->netacq.max_conn);
      EpgAcqServer_SetAddress(pRc->netacq.do_tcp_ip, pRc->netacq.pIpStr, pRc->netacq.pPort);
      EpgNetIo_SetLogging(pRc->netacq.sysloglev, pRc->netacq.fileloglev, pRc->netacq.pLogfileName);
   }
   else
      EpgAcqClient_SetAddress(pRc->netacq.pHostName, pRc->netacq.pPort);
#endif
}

// ----------------------------------------------------------------------------
// Query default acquisition mode: remote or local
//
bool IsRemoteAcqDefault( void )
{
   return RcFile_Query()->netacq.netacq_enable;
}

#ifdef USE_TTX_GRABBER
// ----------------------------------------------------------------------------
// Set the teletext grabber config params
// - called at startup or after user configuration via the GUI
//
void EpgSetup_TtxGrabber( void )
{
   const RCFILE * pRc = RcFile_Query();
   const char * pDbDir = ((mainOpts.dbdir != NULL) ? mainOpts.dbdir : mainOpts.defaultDbDir);

   TtxGrab_SetConfig(pDbDir, pRc->db.piexpire_cutoff, pRc->ttx.keep_ttx_data);
}

// ---------------------------------------------------------------------------
// Query if the given XMLTV file is target for acquisition
// - the given path must be absolute & already normalized,
//   as returned by XmltvCni_LookupProviderPath()
// - result does not indicate if working on the file currently
//
bool EpgSetup_QueryTtxPath( const char * pXmlPath )
{
   EPGACQ_TUNER_PAR * pTtxFreqs;
   char * pTtxNames;
   uint ttxFreqCount;
   uint idx;
   bool result = FALSE;

   if (EpgSetup_GetTtxConfig(&ttxFreqCount, &pTtxNames, &pTtxFreqs))
   {
      if (pTtxNames != NULL)
      {
         const char * pNames = pTtxNames;
         for (idx = 0; (idx < ttxFreqCount) && !result; idx++)
         {
            // note the path returned here is already normalized
            char * pTtxPath = TtxGrab_GetPath(pNames);
            if (pTtxPath != NULL)
               result = (strcmp(pTtxPath, pXmlPath) == 0);  // no "break": need xfree()

            while(*(pNames++) != 0)
               ;
            xfree(pTtxPath);
         }
      }
   }

   if (pTtxNames != NULL)
      xfree(pTtxNames);
   if (pTtxFreqs != NULL)
      xfree(pTtxFreqs);

   return result;
}
#endif // USE_TTX_GRABBER
