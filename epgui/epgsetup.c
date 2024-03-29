/*
 *  Nextview EPG acquisition and database parameter setup
 *
 *  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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
#include "epgvbi/btdrv.h"
#include "epgtcl/dlg_hwcfg.h"
#include "epgui/epgmain.h"
#include "epgui/uictrl.h"
#include "epgui/rcfile.h"
#include "epgui/cmdline.h"
#include "epgui/wintvcfg.h"
#include "epgui/epgsetup.h"
#include "xmltv/xmltv_cni.h"

#ifdef USE_TTX_GRABBER
static bool EpgSetup_GetTtxConfig( uint * pCount, const char ** ppNames, const EPGACQ_TUNER_PAR ** ppFreq );
#endif

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
// - ATTN: differing language between networks is not considered here
//
uint EpgSetup_GetDefaultLang( EPGDB_CONTEXT * pDbContext )
{
   const PI_BLOCK * pPiBlock;
   FILTER_CONTEXT * fc;
   uint lang = EPG_LANG_UNKNOWN;

   if (pDbContext != NULL)
   {
      EpgDbLockDatabase(pDbContext, TRUE);
      fc = EpgDbFilterCreateContext();

      pPiBlock = EpgDbSearchFirstPi(pDbContext, fc);
      while (pPiBlock != NULL)
      {
         if (pPiBlock->lang_title != EPG_LANG_UNKNOWN)
         {
            lang = pPiBlock->lang_title;
            break;
         }
         if (pPiBlock->lang_desc != EPG_LANG_UNKNOWN)
         {
            lang = pPiBlock->lang_desc;
            break;
         }
         pPiBlock = EpgDbSearchNextPi(pDbContext, fc, pPiBlock);
      }
      EpgDbFilterDestroyContext(fc);
      EpgDbLockDatabase(pDbContext, FALSE);
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
      pAiCni = (uint*) xmalloc(sizeof(uint) * pAiBlock->netwopCount);
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
      pNewOrdList = (uint*) xmalloc(sizeof(uint) * pAiBlock->netwopCount);
      newOrdCount = 0;
      if (pOrdList != NULL)
      {
         for (netIdx = 0; netIdx < pOrdList->net_count; netIdx++)
         {
            cni = pOrdList->net_cnis[netIdx];
            for (aiIdx = 0; aiIdx < pAiBlock->netwopCount; aiIdx++)
               if (pAiCni[aiIdx] == cni)
                  break;

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
      dprintf3("EpgSetup-UpdateProvCniTable: prov:%X nets:%d -> user-selected:%d\n", pUiDbContext->provCni, pAiBlock->netwopCount, newOrdCount);

      // also needed for merged DB after adding additional providers
      //if (provCni != MERGED_PROV_CNI)
      {
         // step #2: invalidate suppressed CNIs in the AI network list
         if (pSupList != NULL)
         {
            for (netIdx = 0; netIdx < pSupList->net_count; netIdx++)
            {
               cni = pSupList->net_cnis[netIdx];
               for (aiIdx = 0; aiIdx < pAiBlock->netwopCount; aiIdx++)
                  if (pAiCni[aiIdx] == cni)
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
      dprintf3("EpgSetup-UpdateProvCniTable: prov:%X plus non-sup, minus sup:%d -> %d netwops\n", pUiDbContext->provCni, ((pSupList != NULL) ? pSupList->net_count : 0), newOrdCount);

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
         EpgDbFilterInitNetwopPreFilter(fc, pAiBlock->netwopCount);
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

// ----------------------------------------------------------------------------
// Retrieve network CNI list for the merged database
// - it's not an error if no list is configured (yet);
//   (in this case all networks are included)
//
static bool EpgSetup_GetMergeDbNetwops( uint provCniCount, const uint * pProvCniTab,
                                        uint * pCniCount, uint ** ppCniTab )
{
   const uint * pCfSelCni;
   const uint * pCfSupCni;
   uint cfNetCount;
   uint cfSupCount;
   EPGDB_CONTEXT  * pPeek;
   const AI_BLOCK  * pAiBlock;
   const AI_NETWOP * pNetwops;
   uint * pCniTab;
   uint cniTabCapacity;
   uint prevIdx;
   uint netwopCount;
   uint cni;

   pCniTab = NULL;
   netwopCount = 0;
   cniTabCapacity = 0;
   RcFile_GetNetworkSelection(MERGED_PROV_CNI, &cfNetCount, &pCfSelCni, &cfSupCount, &pCfSupCni);
   if ((cfNetCount > 0) && (pCfSelCni != NULL))
   {
      // copy network selection list of merge config
      netwopCount = cfNetCount;
      cniTabCapacity = netwopCount;
      pCniTab = (uint*) xmalloc(cniTabCapacity * sizeof(pCniTab[0]));
      memcpy(pCniTab, pCfSelCni, cniTabCapacity * sizeof(pCniTab[0]));
   }

   // add networks from all merged DBs that are not explicitly suppressed in merge config
   for (uint dbIdx = 0; dbIdx < provCniCount; dbIdx++)
   {
      pPeek = EpgContextCtl_Peek(pProvCniTab[dbIdx], CTX_RELOAD_ERR_NONE);
      if (pPeek != NULL)
      {
         EpgDbLockDatabase(pPeek, TRUE);
         pAiBlock = EpgDbGetAi(pPeek);
         if (pAiBlock != NULL)
         {
            pNetwops = AI_GET_NETWOPS(pAiBlock);

            for (uint netIdx = 0; netIdx < pAiBlock->netwopCount; netIdx++, pNetwops++)
            {
               cni = AI_GET_NET_CNI(pNetwops);

               // skip the CNI if in the merge provider suppression list
               for (prevIdx = 0; prevIdx < cfSupCount; prevIdx++)
                  if (cni == pCfSupCni[prevIdx])
                     break;

               if (prevIdx >= cfSupCount)
               {
                  // skip the CNI if already in the generated table
                  for (prevIdx = 0; prevIdx < netwopCount; prevIdx++)
                     if (cni == pCniTab[prevIdx])
                        break;

                  if (prevIdx >= netwopCount)
                  {
                     if (netwopCount >= cniTabCapacity)
                     {
                        cniTabCapacity = ((cniTabCapacity == 0) ? 256 : (cniTabCapacity * 2));
                        pCniTab = (uint*) xrealloc(pCniTab, cniTabCapacity * sizeof(pCniTab[0]));
                     }
                     dprintf3("EpgSetup-InitMergeDbNetwops: add net #%d: CNI 0x%04X of DB 0x%04X\n", netIdx, cni, pProvCniTab[dbIdx]);
                     pCniTab[netwopCount] = cni;
                     netwopCount += 1;
                  }
               }
            }
         }
         EpgDbLockDatabase(pPeek, FALSE);
         EpgContextCtl_ClosePeek(pPeek);
      }
      else
         debug1("EpgSetup-InitMergeDbNetwops: failed to peek DB 0x%04X (ignored)", pProvCniTab[dbIdx]);
   }
   *pCniCount = netwopCount;
   *ppCniTab = pCniTab;

   return TRUE;
}

// ----------------------------------------------------------------------------
// Append teletext providers to the given CNI list
// - returns all providers in TTX config for which an XMLTV file exists
//   and which are not already in the list
//
void EpgSetup_AppendTtxProviders( uint *pCniCount, uint * pCniTab )
{
   const EPGACQ_TUNER_PAR * pTtxFreqs = NULL;
   const char * pTtxNames = NULL;
   uint ttxFreqCount = 0;
   uint idx2;

   if (EpgSetup_GetTtxConfig(&ttxFreqCount, &pTtxNames, &pTtxFreqs))
   {
      const char * pNames = pTtxNames;
      for (uint idx = 0; (idx < ttxFreqCount) && (*pCniCount < MAX_MERGED_DB_COUNT); ++idx)
      {
         char * pTtxPath = TtxGrab_GetPath(pTtxFreqs[idx].serviceId, pNames);
         uint provCni = EpgContextCtl_StatProvider(pTtxPath);
         if (provCni != 0)
         {
            for (idx2 = 0; idx2 < *pCniCount; ++idx2)
               if (pCniTab[idx2] == provCni)
                  break;
            if (idx2 >= *pCniCount)
            {
               pCniTab[*pCniCount] = provCni;
               *pCniCount += 1;
            }
         }
         dprintf2("EpgSetup-AppendTtxProviders: 0x%X %s\n", provCni, pTtxPath);

         while(*(pNames++) != 0)
            ;
         xfree(pTtxPath);
      }
   }
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

   if (pRc->db.auto_merge_ttx)
   {
      // append XMLTV files produced by teletext grabber
      EpgSetup_AppendTtxProviders(pCniCount, pCniTab);
   }

   memset(pMax, 0xff, MERGE_TYPE_COUNT * sizeof(MERGE_ATTRIB_VECTOR));

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
EPGDB_CONTEXT * EpgSetup_MergeDatabases( int errHand )
{
   EPGDB_CONTEXT * pDbContext;
   MERGE_ATTRIB_MATRIX max;
   uint  pProvCniTab[MAX_MERGED_DB_COUNT];
   uint  provCount;
   uint  * pNetwopCniTab = NULL;
   uint  netwopCount;
   uint  idx;
   time_t now;

   if ( EpgSetup_GetMergeProviders(&provCount, pProvCniTab, &max[0]) &&
        EpgSetup_GetMergeDbNetwops(provCount, pProvCniTab, &netwopCount, &pNetwopCniTab) )
   {
      pDbContext = EpgContextMerge(provCount, pProvCniTab, max, netwopCount, pNetwopCniTab, errHand);

      if (pDbContext != NULL)
      {
         // put the fake "Merge" CNI plus the CNIs of all merged providers
         // at the front of the provider selection order
         RcFile_UpdateMergedProvSelection();

         // update access time and count for all merged providers
         now = time(NULL);
         for (idx = 0; idx < provCount; idx++)
         {
            RcFile_UpdateXmltvProvAtime(pProvCniTab[idx], now, TRUE);
         }
      }
   }
   else
   {
      UiControlMsg_ReloadError(MERGED_PROV_CNI, EPGDB_RELOAD_MERGE, errHand, FALSE);
      pDbContext = NULL;
   }

   if (pNetwopCniTab != NULL)
      xfree(pNetwopCniTab);

   return pDbContext;
}

// ----------------------------------------------------------------------------
// Open the initial database after program start
// - one or more XMLTV files can be specified on the command line
// - else try the last opened one (saved in rc/ini file)
// - else start with an empty database
//
void EpgSetup_OpenUiDb( void )
{
   const RCFILE * pRc;
   const char * const * pXmlFiles;
   CONTEXT_RELOAD_ERR_HAND errHand;
   uint provCnt;
   uint provCni;

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
      provCni = MERGED_PROV_CNI;
      errHand = CTX_RELOAD_ERR_REQ;
   }
   else if (provCnt > 0)
   {  // load single file given on the command line
      provCni = XmltvCni_MapProvider(pXmlFiles[0]);
      errHand = CTX_RELOAD_ERR_REQ;
   }
   else if (pRc->db.prov_sel_count > 0)
   {  // try the last used provider (may be merged or single db)
      provCni = pRc->db.prov_selection[0];
      errHand = CTX_RELOAD_ERR_DFLT;
   }
   else
   {  // create an empty dummy database
      // (CNI value 0 has a special handling in the context open function)
      errHand = CTX_RELOAD_ERR_NONE;
      provCni = 0;
   }

   if (provCni == MERGED_PROV_CNI)
   {  // special case: merged db
      pUiDbContext = EpgSetup_MergeDatabases(errHand);
      if (pUiDbContext == NULL)
         pUiDbContext = EpgContextCtl_OpenDummy();
   }
   else if (provCni != 0)
   {  // regular database
      pUiDbContext = EpgContextCtl_Open(provCni, FALSE, errHand);

      if (pUiDbContext == NULL)
      {
         pUiDbContext = EpgContextCtl_OpenDummy();
      }
      else if (EpgDbContextGetCni(pUiDbContext) == provCni)
      {
         RcFile_UpdateXmltvProvAtime(provCni, time(NULL), TRUE);
      }
   }
   else
   {  // no CNI specified -> dummy db
      pUiDbContext = EpgContextCtl_OpenDummy();
   }

   // note: the usual provider change events are not triggered here because
   // at the time this function is called the other modules are not yet initialized.
}

#ifdef USE_TTX_GRABBER
// ----------------------------------------------------------------------------
// Get XML file name table and frequencies for teletext grabber
// - note: the caller must free the returned pointers, if non-NULL
//
static bool EpgSetup_GetTtxConfig( uint * pCount, const char ** ppNames, const EPGACQ_TUNER_PAR ** ppFreq )
{
   const TVAPP_CHAN_TAB * pChanTab;
   const RCFILE * pRc;
   bool result = FALSE;

   *pCount = 0;
   *ppFreq = NULL;
   *ppNames = NULL;

   pRc = RcFile_Query();

   if ( pRc->ttx.ttx_enable )
   {
      if (pRc->ttx.ttx_chn_count > 0)
      {
         pChanTab = WintvCfg_GetFreqTab(NULL);
         if ( (pChanTab != NULL) && (pChanTab->chanCount > 0) )
         {
            if (pChanTab->chanCount > pRc->ttx.ttx_chn_count)
               *pCount = pRc->ttx.ttx_chn_count;
            else
               *pCount = pChanTab->chanCount;

            *ppNames = pChanTab->pNameTab;
            *ppFreq = pChanTab->pFreqTab;

            dprintf2("EpgSetup-GetTtxConfig: TTX acq config on %d of %d channels\n", *pCount, pChanTab->chanCount);
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
// Pass acq mode and channel list to acquisition control
// - called upon start of acq and after the user leaves Teletext grabber or
//   client/server cfg dialog with "Ok"
// - acq ctl reset acq only if parameters have changed
// - this function is not used by the acquisition daemon
//
bool EpgSetup_AcquisitionMode( NETACQ_SET_MODE netAcqSetMode )
{
   bool result = FALSE;
#ifdef USE_TTX_GRABBER
   const RCFILE * pRc;
   const EPGACQ_TUNER_PAR * pTtxFreqs;
   const char * pTtxNames;
   uint         ttxFreqCount;
   bool         isNetAcqDefault;
   bool         doNetAcq;
   EPGACQ_DESCR acqState;
   EPGACQ_MODE  mode;

   pRc = RcFile_Query();
   if ((pRc != NULL) && (pRc->acq.acq_mode < ACQMODE_COUNT))
      mode = (EPGACQ_MODE) pRc->acq.acq_mode;
   else // unrecognized mode -> fall back to default mode
      mode = ACQMODE_CYCLIC_2;

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

   if (EpgSetup_GetTtxConfig(&ttxFreqCount, &pTtxNames, &pTtxFreqs))
   {
      // pass the params to the acquisition control module
      result = EpgAcqCtl_SelectMode(mode, ACQMODE_PHASE_COUNT,
                                    ttxFreqCount, pTtxNames, pTtxFreqs,
                                    pRc->ttx.ttx_start_pg, pRc->ttx.ttx_end_pg,
                                    pRc->ttx.ttx_duration);
   }
#endif
   return result;
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
   const EPGACQ_TUNER_PAR * pTtxFreqs;
   const char  * pTtxNames;
   uint          ttxFreqCount;

   if (forcePassive)
   {  // -acqpassive given on command line
      mode = ACQMODE_PASSIVE;
   }
   else
   {  // else use acq mode from rc/ini file
      mode = (EPGACQ_MODE) pRc->acq.acq_mode;
      if (mode >= ACQMODE_COUNT)
         EpgNetIo_Logger(LOG_ERR, -1, 0, "acqmode parameters error", NULL);
   }

   if (mode < ACQMODE_COUNT)
   {
      if (EpgSetup_GetTtxConfig(&ttxFreqCount, &pTtxNames, &pTtxFreqs))
      {
         // pass the params to the acquisition control module
         result = EpgAcqCtl_SelectMode(mode, (EPGACQ_PHASE) maxPhase,
                                       ttxFreqCount, pTtxNames, pTtxFreqs,
                                       pRc->ttx.ttx_start_pg, pRc->ttx.ttx_end_pg,
                                       pRc->ttx.ttx_duration);
      }
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
      BtDriver_Configure(cardIdx, BTDRV_SOURCE_NONE, prio);
      EpgAcqCtl_Stop();
   }
   else
   {
      // pass the hardware config params to the driver
      if (BtDriver_Configure(cardIdx, (BTDRV_SOURCE_TYPE) drvType, prio))
      {
         // pass the input selection to acquisition control
         EpgAcqCtl_SetInputSource(input, slicer);
         result = TRUE;
      }
      else
      {
         debug1("EpgSetup-CardDriver: setup for cardIdx:%d failed", newCardIndex);
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
      // equivalent to EpgSetup-CardDriver()
      drvType = BtDriver_GetDefaultDrvType();
   }

   if (drvType == BTDRV_SOURCE_NONE)
   {
      result = FALSE;
   }
   else
   {
      result = BtDriver_CheckCardParams((BTDRV_SOURCE_TYPE)drvType, cardIdx, input);
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
// Query if the EPG grabber is configured to work for the GUI database
//
bool EpgSetup_IsAcqWorkingForUiDb( void )
{
   uint dbIdx;
   uint provCniTab[MAX_MERGED_DB_COUNT];
   uint provCount;
   const EPGACQ_TUNER_PAR * pTtxFreqs;
   const char * pTtxNames;
   uint ttxFreqCount;
   uint idx;
   bool acqWorksOnUi = FALSE;

   if ( EpgDbContextIsMerged(pUiDbContext) )
   {
      provCount = 0;
      EpgContextMergeGetCnis(pUiDbContext, &provCount, provCniTab);
   }
   else
   {
      provCount = 1;
      provCniTab[0] = EpgDbContextGetCni(pUiDbContext);
   }

   if (EpgSetup_GetTtxConfig(&ttxFreqCount, &pTtxNames, &pTtxFreqs))
   {
      for (dbIdx=0; (dbIdx < provCount) && !acqWorksOnUi; dbIdx++)
      {
         // note the path returned here is already normalized
         const char * pXmlPath = XmltvCni_LookupProviderPath(provCniTab[dbIdx]);
         if (pXmlPath != NULL)
         {
            const char * pNames = pTtxNames;
            for (idx = 0; (idx < ttxFreqCount) && !acqWorksOnUi; idx++)
            {
               // note the path returned here is already normalized
               char * pTtxPath = TtxGrab_GetPath(pTtxFreqs[idx].serviceId, pNames);
               if (pTtxPath != NULL)
                  if ((strcmp(pTtxPath, pXmlPath) == 0))  // no "break": need xfree()
                     acqWorksOnUi = TRUE;

               while(*(pNames++) != 0)
                  ;

               xfree(pTtxPath);
            }
         }
      }
   }

   return acqWorksOnUi;
}
#endif // USE_TTX_GRABBER
