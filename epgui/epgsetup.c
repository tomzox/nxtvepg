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
 *  $Id: epgsetup.c,v 1.1 2005/01/06 19:12:04 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbmerge.h"
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
#include "epgui/epgsetup.h"


// ----------------------------------------------------------------------------
// Retrieve CNI list from the merged database's user network selection
//
static bool EpgSetup_GetMergeDbNetwops( uint * pCniCount, uint * pCniTab )
{
   const RCFILE * pRc;

   pRc = RcFile_Query();

   *pCniCount = pRc->db.prov_merge_net_count;
   if (*pCniCount > MAX_NETWOP_COUNT)
      *pCniCount = MAX_NETWOP_COUNT;

   memcpy(pCniTab, pRc->db.prov_merge_netwops, pRc->db.prov_merge_net_count * sizeof(uint));

   return TRUE;
}

// ----------------------------------------------------------------------------
// Convert merge options in rc file into attribute matrix
// - for each datatype the user can configure an ordered list of providers
//   from which the data is taken during the merge
// - in the rc file a list of CNIs is stored; it' converted into indices into
//   the main provider list here
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
   return TRUE;
}

// ----------------------------------------------------------------------------
// Merge databases according to the current configuration
//
EPGDB_CONTEXT * EpgSetup_MergeDatabases( void )
{
   EPGDB_CONTEXT * pDbContext;
   MERGE_ATTRIB_MATRIX max;
   uint  pProvCniTab[MAX_MERGED_DB_COUNT];
   uint  provCount;
   uint  netwopCniTab[MAX_NETWOP_COUNT];
   uint  netwopCount;

   if ( (EpgSetup_GetMergeProviders(&provCount, pProvCniTab, &max[0]) ) &&
        (EpgSetup_GetMergeDbNetwops(&netwopCount, netwopCniTab) ) )
   {
      pDbContext = EpgContextMerge(provCount, pProvCniTab, max, netwopCount, netwopCniTab);

      // put the fake "Merge" CNI plus the CNIs of all merged providers
      // at the front of the provider selection order
      RcFile_UpdateMergedProvSelection();
   }
   else
   {
      UiControlMsg_ReloadError(0x00ff, EPGDB_RELOAD_MERGE, CTX_RELOAD_ERR_REQ, FALSE);
      pDbContext = NULL;
   }
   return pDbContext;
}

// ----------------------------------------------------------------------------
// Open the initial database after program start
// - provider can be chosen by the -provider flag: warn if it does not exist
// - else try all dbs in list list of previously opened ones (saved in rc/ini file)
// - else scan the db directory or create an empty database
//
void EpgSetup_OpenUiDb( uint startUiCni )
{
   const RCFILE * pRc;
   uint cni, provIdx;

   // prepare list of previously opened databases
   pRc = RcFile_Query();

   cni = 0;
   provIdx = 0;
   do
   {
      if (startUiCni != 0)
      {  // first use the CNI given on the command line
         cni = startUiCni;
      }
      else if (provIdx < pRc->db.prov_sel_count)
      {  // then try all providers given in the list of previously loaded databases
         cni = pRc->db.prov_selection[provIdx];
         provIdx += 1;
      }
      else
      {  // if everything above failed, open any database found in the dbdir or create an empty one
         // (the CNI 0 has a special handling in the context open function)
         cni = 0;
      }

      if (cni == 0x00ff)
      {  // special case: merged db
         pUiDbContext = EpgSetup_MergeDatabases();
      }
      else if (cni != 0)
      {  // regular database
         pUiDbContext = EpgContextCtl_Open(cni, CTX_FAIL_RET_NULL, CTX_RELOAD_ERR_REQ);
      }
      else
      {  // no CNI left in list -> load any db or use dummy
         pUiDbContext = EpgContextCtl_OpenAny(CTX_RELOAD_ERR_REQ);
         cni = EpgDbContextGetCni(pUiDbContext);
      }

      // clear the cmd-line CNI since it's already been used
      startUiCni = 0;
   }
   while (pUiDbContext == NULL);

   // update rc/ini file with new CNI order
   if ((cni != 0) && (EpgDbContextIsMerged(pUiDbContext) == FALSE))
   {
      RcFile_UpdateProvSelection(cni);
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

// ----------------------------------------------------------------------------
// If the UI db is empty, move the UI CNI to the front of the list
// - only if a manual acq mode is configured
//
static void EpgSetup_SortAcqCniList( uint cniCount, uint * cniTab )
{
   FILTER_CONTEXT  * pfc;
   const PI_BLOCK  * pPiBlock;
   uint uiCni;
   uint idx, mergeIdx;
   uint mergeCniCount, mergeCniTab[MAX_MERGED_DB_COUNT];
   sint startIdx;

   uiCni = EpgDbContextGetCni(pUiDbContext);
   if ((uiCni != 0) && (cniCount > 1))
   {  // provider present -> check for PI

      EpgDbLockDatabase(pUiDbContext, TRUE);
      // create a filter context with only an expire time filter set
      pfc = EpgDbFilterCreateContext();
      EpgDbFilterSetExpireTime(pfc, time(NULL));
      EpgDbPreFilterEnable(pfc, FILTER_EXPIRE_TIME);

      // check if there are any non-expired PI in the database
      pPiBlock = EpgDbSearchFirstPi(pUiDbContext, pfc);
      if (pPiBlock == NULL)
      {  // no PI in database
         if (EpgDbContextIsMerged(pUiDbContext) == FALSE)
         {
            for (startIdx=0; startIdx < (sint)cniCount; startIdx++)
               if (cniTab[startIdx] == uiCni)
                  break;
         }
         else
         {  // Merged database
            startIdx = -1;
            if (EpgContextMergeGetCnis(pUiDbContext, &mergeCniCount, mergeCniTab))
            {
               // check if the current acq CNI is one of the merged
               for (mergeIdx=0; mergeIdx < mergeCniCount; mergeIdx++)
                  if (cniTab[0] == mergeCniTab[mergeIdx])
                     break;
               if (mergeIdx >= mergeCniCount)
               {  // current CNI is not part of the merge -> search if any other is
                  for (mergeIdx=0; (mergeIdx < mergeCniCount) && (startIdx == -1); mergeIdx++)
                     for (idx=0; (idx < cniCount) && (startIdx == -1); idx++)
                        if (cniTab[idx] == mergeCniTab[mergeIdx])
                           startIdx = idx;
               }
            }
         }

         if ((startIdx > 0) && (startIdx < (sint)cniCount))
         {  // move the UI CNI to the front of the list
            dprintf2("EpgSetup-SortAcqCniList: moving provider 0x%04X from idx %d to 0\n", cniTab[startIdx], startIdx);
            uiCni = cniTab[startIdx];
            for (idx=1; idx <= (uint)startIdx; idx++)
               cniTab[idx] = cniTab[idx - 1];
            cniTab[0] = uiCni;
         }
      }
      EpgDbFilterDestroyContext(pfc);
      EpgDbLockDatabase(pUiDbContext, FALSE);
   }
}

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
   const RCFILE * pRc;
   uint         cniCount;
   uint         cniTab[MAX_MERGED_DB_COUNT];
   bool         isNetAcqDefault;
   bool         doNetAcq;
   EPGACQ_DESCR acqState;
   EPGACQ_MODE  mode;

   pRc = RcFile_Query();
   if ((pRc != NULL) && (pRc->acq.acq_mode < ACQMODE_COUNT) &&
                        (pRc->acq.acq_cni_count < MAX_MERGED_DB_COUNT))
   {
      mode = pRc->acq.acq_mode;
      cniCount = pRc->acq.acq_cni_count;
      memcpy(cniTab, pRc->acq.acq_cnis, sizeof(cniTab[0]) * pRc->acq.acq_cni_count);
   }
   else
   {  // unrecognized mode or other param error -> fall back to default mode
      mode = ACQMODE_FOLLOW_UI;
      cniCount = 0;
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
         else if (acqState.state == ACQDESCR_DISABLED)
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

   if ((mode == ACQMODE_FOLLOW_UI) || (mode == ACQMODE_NETWORK))
   {
      if ( EpgDbContextIsMerged(pUiDbContext) )
      {
         if (EpgContextMergeGetCnis(pUiDbContext, &cniCount, cniTab) == FALSE)
         {  // error
            cniCount = 0;
         }
      }
      else
      {
         cniTab[0] = EpgDbContextGetCni(pUiDbContext);
         cniCount  = ((cniTab[0] != 0) ? 1 : 0);
      }
   }

   // move browser provider's CNI to the front if the db is empty
   EpgSetup_SortAcqCniList(cniCount, cniTab);

   // pass the params to the acquisition control module
   EpgAcqCtl_SelectMode(mode, ACQMODE_COUNT, cniCount, cniTab);
}

// ----------------------------------------------------------------------------
// Pass acq mode and CNI list to acquisition control
// - in Follow-UI mode the browser CNI must be determined here since
//   no db is opened for the browser
//
bool EpgSetup_DaemonAcquisitionMode( uint cmdLineCni, bool forcePassive, int maxPhase )
{
   const RCFILE * pRc = RcFile_Query();
   EPGACQ_MODE   mode;
   const uint  * pProvList;
   uint cniCount, cniTab[MAX_MERGED_DB_COUNT];
   bool result = FALSE;

   if (cmdLineCni != 0)
   {  // CNI given on command line with -provider option
      assert(forcePassive == FALSE);  // checked by argv parser
      mode      = ACQMODE_CYCLIC_2;
      cniCount  = 1;
      cniTab[0] = cmdLineCni;

      // Merged database -> retrieve CNI list
      if (cniTab[0] == 0x00ff)
      {
         if (pRc->db.prov_merge_count > 0)
         {
            cniCount = pRc->db.prov_merge_count;
            memcpy(cniTab, pRc->db.prov_merge_cnis, sizeof(cniTab));
         }
         else
         {
            EpgNetIo_Logger(LOG_ERR, -1, 0, "no network list found for merged database", NULL);
            mode = ACQMODE_COUNT;
         }
      }
   }
   else if (forcePassive)
   {  // -acqpassive given on command line
      mode      = ACQMODE_PASSIVE;
      cniCount  = 0;
   }
   else
   {  // else use acq mode from rc/ini file
      mode = pRc->acq.acq_mode;
      switch (mode)
      {
         case ACQMODE_CYCLIC_2:
         case ACQMODE_CYCLIC_012:
         case ACQMODE_CYCLIC_02:
         case ACQMODE_CYCLIC_12:
            cniCount = pRc->acq.acq_cni_count;
            memcpy(cniTab, pRc->acq.acq_cnis, sizeof(cniTab));
            break;
         default:
            cniCount = 0;
            break;
      }
      if (mode == ACQMODE_FOLLOW_UI)
      {
         if (pRc->db.prov_sel_count > 0)
         {
            cniCount = pRc->db.prov_sel_count;
            memcpy(cniTab, pRc->db.prov_selection, sizeof(cniTab));
         }
         else
         {  // last used provider not known (e.g. right after the initial scan) -> use all providers
            pProvList = EpgContextCtl_GetProvList(&cniCount);
            if (pProvList != NULL)
            {
               if (cniCount > MAX_MERGED_DB_COUNT)
                  cniCount = MAX_MERGED_DB_COUNT;
               memcpy(cniTab, pProvList, cniCount * sizeof(uint));
               xfree((void *) pProvList);
            }
            else
            {  // no providers known yet -> set count to zero, acq starts in passive mode
               cniCount = 0;
            }
         }
         if ((cniCount > 0) && (cniTab[0] == 0x00ff))
         {
            // Merged database -> retrieve CNI list
            if (pRc->db.prov_merge_count > 0)
            {
               cniCount = pRc->db.prov_merge_count;
               memcpy(cniTab, pRc->db.prov_merge_cnis, sizeof(cniTab));
            }
            else
            {
               EpgNetIo_Logger(LOG_ERR, -1, 0, "no network list found for merged database", NULL);
               mode = ACQMODE_COUNT;
            }
         }
      }
      else if (mode >= ACQMODE_COUNT)
      {
         EpgNetIo_Logger(LOG_ERR, -1, 0, "acqmode parameters error", NULL);
      }
   }

   if (mode < ACQMODE_COUNT)
   {
      // pass the params to the acquisition control module
      result = EpgAcqCtl_SelectMode(mode, maxPhase, cniCount, cniTab);
   }
   else
      result = FALSE;

   return result;
}

// ----------------------------------------------------------------------------
// Set the hardware config params
// - called at startup or after user configuration via the GUI
// - the card index can also be set via command line and is passed here
//   from main; a value of -1 means don't care
//
void EpgSetup_CardDriver( int newCardIndex )
{
   const RCFILE * pRc = RcFile_Query();
   int  cardIdx, input, prio, slicer;
   int  chipType, cardType, tuner, pll, wdmStop;

   cardIdx = pRc->tvcard.card_idx;
   input   = pRc->tvcard.input;
   prio    = pRc->tvcard.acq_prio;
   slicer  = pRc->tvcard.slicer_type;
   wdmStop = pRc->tvcard.wdm_stop;

   chipType = EPGTCL_PCI_ID_UNKNOWN;
   cardType = tuner = pll = 0;

   if ((newCardIndex >= 0) && (newCardIndex != cardIdx))
   {
      // different card idx passed via command line
      cardIdx = newCardIndex;

      //UpdateRcFile(TRUE);
   }

#ifdef WIN32
   if (cardIdx < pRc->tvcard.winsrc_count)
   {
      // retrieve card specific parameters
      chipType = pRc->tvcard.winsrc[cardIdx][EPGTCL_TVCF_CHIP_IDX];
      cardType = pRc->tvcard.winsrc[cardIdx][EPGTCL_TVCF_CARD_IDX];
      tuner    = pRc->tvcard.winsrc[cardIdx][EPGTCL_TVCF_TUNER_IDX];
      pll      = pRc->tvcard.winsrc[cardIdx][EPGTCL_TVCF_PLL_IDX];
   }
   else
      debug2("EpgSetup-CardDriver: no config for card #%d (have %d cards)", cardIdx, pRc->tvcard.winsrc_count);
#endif

   // pass the hardware config params to the driver
   if (BtDriver_Configure(cardIdx, prio, chipType, cardType, tuner, pll, wdmStop))
   {
      // pass the input selection to acquisition control
      EpgAcqCtl_SetInputSource(input, slicer);
   }
   else
      EpgAcqCtl_Stop();
}

// ----------------------------------------------------------------------------
// Check if the selected TV card is configured properly
//
#ifdef WIN32
bool EpgSetup_CheckTvCardConfig( void )
{
   const RCFILE * pRc;
   uint  cardIdx, input;
   uint  chipType, cardType, tuner, pll;
   bool  result = FALSE;

   pRc = RcFile_Query();

   cardIdx = pRc->tvcard.card_idx;
   input = pRc->tvcard.input;

   if (cardIdx < pRc->tvcard.winsrc_count)
   {
      // retrieve card specific parameters
      chipType = pRc->tvcard.winsrc[cardIdx][EPGTCL_TVCF_CHIP_IDX];
      cardType = pRc->tvcard.winsrc[cardIdx][EPGTCL_TVCF_CARD_IDX];
      tuner    = pRc->tvcard.winsrc[cardIdx][EPGTCL_TVCF_TUNER_IDX];
      pll      = pRc->tvcard.winsrc[cardIdx][EPGTCL_TVCF_PLL_IDX];

      result = BtDriver_CheckCardParams(cardIdx, chipType, cardType, tuner, pll, input);
   }

   return result;
}
#endif

// ----------------------------------------------------------------------------
// Read network connection and server parameters from Tcl variables
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

