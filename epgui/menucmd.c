/*
 *  Nextview GUI: Execute commands and control status of the menu bar
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
 *    Provide callbacks for various commands in the menu bar.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: menucmd.c,v 1.49 2001/09/02 17:30:58 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmerge.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgscan.h"
#include "epgctl/epgctxmerge.h"
#include "epgui/epgmain.h"
#include "epgui/epgtxtdump.h"
#include "epgui/pilistbox.h"
#include "epgui/pifilter.h"
#include "epgui/statswin.h"
#include "epgui/menucmd.h"
#include "epgui/uictrl.h"
#include "epgui/xawtv.h"
#include "epgctl/epgctxctl.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/tvchan.h"
#include "epgvbi/btdrv.h"


static void MenuCmd_EpgScanHandler( ClientData clientData );

// ----------------------------------------------------------------------------
// Set the states of the entries in Control menu
//
static int SetControlMenuStates(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_SetControlMenuStates";
   uint acqCni, uiCni;
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      acqCni = EpgDbContextGetCni(pAcqDbContext);
      uiCni  = EpgDbContextGetCni(pUiDbContext);

      // enable "dump database" only if UI database has at least an AI block
      sprintf(comm, ".menubar.ctrl entryconfigure \"Dump raw database...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);
      sprintf(comm, ".menubar.ctrl entryconfigure \"Dump in HTML...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "timescales" only if UI database has AI block
      sprintf(comm, ".menubar.ctrl entryconfigure \"View timescales...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "acq timescales" only if acq running on different db than ui
      sprintf(comm, ".menubar.ctrl entryconfigure \"View acq timescales...\" -state %s\n",
                    ((acqCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "db stats" only if UI db has AI block
      sprintf(comm, ".menubar.ctrl entryconfigure \"View statistics...\" -state %s\n",
                    ((uiCni != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // enable "acq stats" only if acq running
      sprintf(comm, ".menubar.ctrl entryconfigure \"View acq statistics...\" -state %s\n",
                    ((pAcqDbContext != 0) ? "normal" : "disabled"));
      eval_check(interp, comm);

      // check button of "Enable Acq" if acq is running
      sprintf(comm, "set menuStatusStartAcq %d", (pAcqDbContext != NULL));
      eval_check(interp, comm);

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Toggle dump of incoming PI
//
static int MenuCmd_ToggleDumpStream(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ToggleDumpStream <boolean>";
   uint value;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetBoolean(interp, argv[1], &value))
   {  // string parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else
   {
      EpgTxtDump_Toggle();
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Toggle acquisition on/off
//
static int MenuCmd_ToggleAcq(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ToggleAcq <boolean>";
   int value;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetBoolean(interp, argv[1], &value))
   {  // string parameter is not a decimal integer
      result = TCL_ERROR;
   }
   else
   {
      if (EpgAcqCtl_Toggle(value) != value)
      {
         #ifndef WIN32  //is handled by bt-driver in win32
         sprintf(comm, "tk_messageBox -type ok -icon error "
                       "-message {Failed to %s acquisition. "
                                 "Close all other video applications and try again.}\n",
                       (value ? "start" : "stop"));
         eval_check(interp, comm);
         #endif
      }
      // update help message in listbox if database is empty
      UiControl_CheckDbState();
      // update statistics windows for UI to in- or exclude acq stats
      StatsWin_NewAi(pUiDbContext);

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Dump the complete database
//
static int MenuCmd_DumpDatabase(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_DumpDatabase <file-name> <pi=0/1> <xi=0/1> <ai=0/1>"
                                              " <ni=0/1> <oi=0/1> <mi=0/1> <li=0/1> <ti=0/1>";
   int do_pi, do_xi, do_ai, do_ni, do_oi, do_mi, do_li, do_ti;
   FILE *fp;
   int result;

   if (argc != 1+1+8)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( (Tcl_GetBoolean(interp, argv[2], &do_pi) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[3], &do_xi) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[4], &do_ai) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[5], &do_ni) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[6], &do_oi) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[7], &do_mi) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[8], &do_li) != TCL_OK) || 
             (Tcl_GetBoolean(interp, argv[9], &do_ti) != TCL_OK) )
   {  // one of the params is not boolean; error msg is already set
      result = TCL_ERROR;
   }
   else
   {
      if ((argv[1] != NULL) && (argv[1][0] != 0))
      {
         fp = fopen(argv[1], "w");
         if (fp == NULL)
         {  // access, create or truncate failed -> inform the user
            sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumpdb -message \"Failed to open file '%s' for writing: %s\"",
                          argv[1], strerror(errno));
            eval_check(interp, comm);
            Tcl_ResetResult(interp);
         }
      }
      else
         fp = stdout;

      if (fp != NULL)
      {
         EpgTxtDump_Database(pUiDbContext, fp, (bool)do_pi, (bool)do_xi, (bool)do_ai, (bool)do_ni,
                                               (bool)do_oi, (bool)do_mi, (bool)do_li, (bool)do_ti);
         if (fp != stdout)
            fclose(fp);
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Switch the provider for the browser
// - only used for the provider selection popup,
//   not for the initial selection nor for provider merging
//
static int MenuCmd_ChangeProvider(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ChangeProvider <cni>";
   int cni;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      // get the CNI of the selected provider
      if (Tcl_GetInt(interp, argv[1], &cni) == TCL_OK)
      {
         EpgContextCtl_Close(pUiDbContext);
         pUiDbContext = EpgContextCtl_Open(cni, CTX_RELOAD_ERR_REQ);

         // in case follow-ui acq mode is used, change the acq db too
         EpgAcqCtl_UiProvChange();

         StatsWin_ProvChange(DB_TARGET_UI);

         UiControl_AiStateChange(NULL);
         eval_check(interp, "C_ResetFilter all; ResetFilterState");

         UiControl_CheckDbState();
         PiListBox_Reset();

         // put the new CNI at the front of the selection order and update the config file
         sprintf(comm, "UpdateProvSelection 0x%04X\n", cni);
         eval_check(interp, comm);

         result = TCL_OK;
      }
      else
      {
         sprintf(comm, "C_ChangeProvider: expected hex CNI but got: %s", argv[1]);
         Tcl_SetResult(interp, comm, TCL_VOLATILE);
         result = TCL_ERROR;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Return service name and list of networks of the given database
// - used by provider selection popup
//
static int MenuCmd_GetProvServiceInfos(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetProvServiceInfos <cni>";
   const EPGDBSAV_PEEK *pPeek;
   EPGDB_RELOAD_RESULT dberr;
   int cni, netwop;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetInt(interp, argv[1], &cni) )
   {  // the parameter is not an integer
      result = TCL_ERROR;
   }
   else
   {
      pPeek = EpgDbPeek(cni, &dberr);
      if (pPeek != NULL)
      {
         // first element in return list is the service name
         Tcl_AppendElement(interp, AI_GET_SERVICENAME(&pPeek->pAiBlock->blk.ai));

         // second element is OI header
         if ((pPeek->pOiBlock != NULL) && OI_HAS_HEADER(&pPeek->pOiBlock->blk.oi))
            Tcl_AppendElement(interp, OI_GET_HEADER(&pPeek->pOiBlock->blk.oi));
         else
            Tcl_AppendElement(interp, "");

         // third element is OI message
         if ((pPeek->pOiBlock != NULL) && OI_HAS_MESSAGE(&pPeek->pOiBlock->blk.oi))
            Tcl_AppendElement(interp, OI_GET_MESSAGE(&pPeek->pOiBlock->blk.oi));
         else
            Tcl_AppendElement(interp, "");

         // append names of all netwops
         for ( netwop = 0; netwop < pPeek->pAiBlock->blk.ai.netwopCount; netwop++ ) 
         {
            Tcl_AppendElement(interp, AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, netwop));
         }

         EpgDbPeekDestroy(pPeek);
      }
      else
      {  // failed to peek into the db -> inform the user
         UiControlMsg_ReloadError(cni, dberr, CTX_RELOAD_ERR_ANY);
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Return the CNI of the database currently open for the browser
// - result is 0 if none is opened or 0x00ff for the merged db
//
static int MenuCmd_GetCurrentDatabaseCni(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetCurrentDatabaseCni";
   uint dbCni;
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {  // return the CNI of the currently used browser database
      dbCni = EpgDbContextGetCni(pUiDbContext);

      sprintf(comm, "0x%04X", dbCni);
      Tcl_SetResult(interp, comm, TCL_VOLATILE);

      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Get list of provider CNIs and names
// - for provider selection and merge popup
//
static int MenuCmd_GetProvCnisAndNames(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetProvCnisAndNames";
   const EPGDBSAV_PEEK *pPeek;
   EPGDB_RELOAD_RESULT dberr;
   uint index, cni;
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      index = 0;
      while ( (cni = EpgDbReloadScan(index)) != 0 )
      {
         pPeek = EpgDbPeek(cni, &dberr);
         if (pPeek != NULL)
         {
            sprintf(comm, "0x%04X", cni);
            Tcl_AppendElement(interp, comm);
            Tcl_AppendElement(interp, AI_GET_NETWOP_NAME(&pPeek->pAiBlock->blk.ai, pPeek->pAiBlock->blk.ai.thisNetwop));

            EpgDbPeekDestroy(pPeek);
         }
         else
         {  // failed to peek into the db -> inform the user
            UiControlMsg_ReloadError(cni, dberr, CTX_RELOAD_ERR_ANY);
         }
         index += 1;
      }
      result = TCL_OK;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Append list of networks in a AI to the result
// - as a side-effect the netwop names are stored into a TCL array
//
static void MenuCmd_AppendNetwopList( Tcl_Interp *interp, const AI_BLOCK * pAiBlock, char * pArrName, bool unify )
{
   uchar strbuf[15];
   uint  netwop;

   if (pAiBlock != NULL)
   {
      for ( netwop = 0; netwop < pAiBlock->netwopCount; netwop++ ) 
      {
         sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, netwop)->cni);

         // if requested check if the same CNI is already in the result
         // XXX hack: only works if name array is given
         if ( (unify == FALSE) || (pArrName[0] == 0) ||
              (Tcl_GetVar2(interp, pArrName, strbuf, 0) == NULL) )
         {
            // append the CNI in the format "0x0D94" to the TCL result list
            Tcl_AppendElement(interp, strbuf);

            // as a side-effect the netwop names are stored into a TCL array
            if (pArrName[0] != 0)
               Tcl_SetVar2(interp, pArrName, strbuf, AI_GET_NETWOP_NAME(pAiBlock, netwop), 0);
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Get List of netwop CNIs and array of names from AI for netwop selection
// - 1st argument selects the provider; 0 for the current browser database
// - 2nd argument names a Tcl array where to store the network names;
//   may be 0-string if not needed
// - optional 3rd argument may be "allmerged" if in case of a merged database
//   all CNIs of all source databases shall be returned
//
static int MenuCmd_GetAiNetwopList( ClientData ttp, Tcl_Interp *interp, int argc, char *argv[] )
{
   const char * const pUsage = "Usage: C_GetAiNetwopList <CNI> <varname> [allmerged]";
   const EPGDBSAV_PEEK *pPeek;
   EPGDB_CONTEXT * pDbContext;
   EPGDB_RELOAD_RESULT dberr;
   const AI_BLOCK *pAiBlock;
   int result, cni;

   if ((argc != 3) && ((argc != 4) || strcmp(argv[3], "allmerged")))
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      result = Tcl_GetInt(interp, argv[1], &cni);
      if (result == TCL_OK)
      {
         // clear the network names result array
         if (argv[2][0] != 0)
            Tcl_UnsetVar(interp, argv[2], 0);

         if ((cni == 0) || (cni == EpgDbContextGetCni(pUiDbContext)))
            pDbContext = pUiDbContext;
         else if (cni == EpgDbContextGetCni(pAcqDbContext))
            pDbContext = pAcqDbContext;
         else
            pDbContext = NULL;

         if ((pDbContext != NULL) && EpgDbContextIsMerged(pDbContext) && (argc == 4))
         {
            // special mode for merged db: return all CNIs from all source dbs
            uint dbIdx, provCount, provCniTab[MAX_MERGED_DB_COUNT];

            if (EpgContextMergeGetCnis(pUiDbContext, &provCount, provCniTab))
            {
               // peek into all merged dbs
               for (dbIdx=0; dbIdx < provCount; dbIdx++)
               {
                  pPeek = EpgDbPeek(provCniTab[dbIdx], &dberr);
                  if (pPeek != NULL)
                  {
                     pAiBlock = &pPeek->pAiBlock->blk.ai;
                     MenuCmd_AppendNetwopList(interp, pAiBlock, argv[2], TRUE);

                     EpgDbPeekDestroy(pPeek);
                  }
                  else
                  {  // failed to peek into the db -> inform the user
                     UiControlMsg_ReloadError(cni, dberr, CTX_RELOAD_ERR_ANY);
                  }
               }
            }
         }
         else if (pDbContext != NULL)
         {
            // db is already open -> just take the AI block from it
            EpgDbLockDatabase(pDbContext, TRUE);
            pAiBlock = EpgDbGetAi(pDbContext);
            MenuCmd_AppendNetwopList(interp, pAiBlock, argv[2], FALSE);
            EpgDbLockDatabase(pDbContext, FALSE);
         }
         else
         {
            // db is not open -> peek into it
            pPeek = EpgDbPeek(cni, &dberr);
            if (pPeek != NULL)
            {
               pAiBlock = &pPeek->pAiBlock->blk.ai;
               MenuCmd_AppendNetwopList(interp, pAiBlock, argv[2], FALSE);

               EpgDbPeekDestroy(pPeek);
            }
            else
            {  // failed to peek into the db -> inform the user
               UiControlMsg_ReloadError(cni, dberr, CTX_RELOAD_ERR_ANY);
            }
         }
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Retrieve CNI list from the merged database's user network selection
//
static int ProvMerge_ParseNetwopList( Tcl_Interp * interp, uint * pCniCount, uint * pCniTab )
{
   char **pCniArgv, **pSubLists;
   char * pTmpStr;
   int  * pCni;
   uint idx;
   int  result;

   pTmpStr = Tcl_GetVar2(interp, "cfnetwops", "0x00FF", TCL_GLOBAL_ONLY);
   if (pTmpStr != NULL)
   {
      // parse list of 2 lists, e.g. {{0x0dc1 0x0dc2} {0x0AC1 0x0AC2}}
      result = Tcl_SplitList(interp, pTmpStr, pCniCount, &pSubLists);
      if (result == TCL_OK)
      {
         if (*pCniCount == 2)
         {
            // parse CNI list, e.g. {0x0dc1 0x0dc2}
            result = Tcl_SplitList(interp, pSubLists[0], pCniCount, &pCniArgv);
            if (result == TCL_OK)
            {
               pCni = (int *) pCniTab;
               for (idx=0; (idx < *pCniCount) && (idx < MAX_NETWOP_COUNT) && (result == TCL_OK); idx++)
               {
                  result = Tcl_GetInt(interp, pCniArgv[idx], pCni++);
               }

               Tcl_Free((char *) pCniArgv);
            }
         }
         else
            result = TCL_ERROR;

         Tcl_Free((char *) pSubLists);
      }
   }
   else
      result = TCL_ERROR;

   return result;
}

// ----------------------------------------------------------------------------
// Parse the Tcl provider merge config string and convert it to attribute matrix
//
static int
ProvMerge_ParseConfigString( Tcl_Interp *interp, uint *pCniCount, uint * pCniTab, MERGE_ATTRIB_VECTOR_PTR pMax )
{
   char **pCniArgv, **pAttrArgv, **pIdxArgv;
   uint attrCount, idxCount, idx, idx2, ati, matIdx, cni;
   char * pTmpStr;
   int result;

   pTmpStr = Tcl_GetVar(interp, "prov_merge_cnis", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      // parse CNI list, format: {0x0d94 ...}
      result = Tcl_SplitList(interp, pTmpStr, pCniCount, &pCniArgv);
      if (result == TCL_OK)
      {
         if (*pCniCount > MAX_MERGED_DB_COUNT)
            *pCniCount = MAX_MERGED_DB_COUNT;

         for (idx=0; (idx < *pCniCount) && (result == TCL_OK); idx++)
         {
            result = Tcl_GetInt(interp, pCniArgv[idx], pCniTab + idx);
         }
         Tcl_Free((char *) pCniArgv);

         if ((result == TCL_OK) && (*pCniCount > 0))
         {  // continue only if all CNIs could be parsed

            // parse attribute list, format is pairs of keyword and index list: {key {3 1 2} ...}
            pTmpStr = Tcl_GetVar(interp, "prov_merge_cf", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
            if (pTmpStr != NULL)
            {
               result = Tcl_SplitList(interp, pTmpStr, &attrCount, &pAttrArgv);
               if (result == TCL_OK)
               {
                  // initialize the attribute matrix with default index order: 0,1,2,...,n-1,0xff,...
                  memset(pMax, 0xff, sizeof(MERGE_ATTRIB_MATRIX));
                  for (ati=0; ati < MERGE_TYPE_COUNT; ati++)
                     for (idx=0; idx < *pCniCount; idx++)
                        pMax[ati][idx] = idx;

                  for (ati=0; (ati+1 < attrCount) && (result == TCL_OK); ati+=2)
                  {
                     // translate keyword to index into attribute matrix
                     if      (strcmp(pAttrArgv[ati], "cftitle") == 0) matIdx = MERGE_TYPE_TITLE;
                     else if (strcmp(pAttrArgv[ati], "cfdescr") == 0) matIdx = MERGE_TYPE_DESCR;
                     else if (strcmp(pAttrArgv[ati], "cfthemes") == 0) matIdx = MERGE_TYPE_THEMES;
                     else if (strcmp(pAttrArgv[ati], "cfseries") == 0) matIdx = MERGE_TYPE_SERIES;
                     else if (strcmp(pAttrArgv[ati], "cfsortcrit") == 0) matIdx = MERGE_TYPE_SORTCRIT;
                     else if (strcmp(pAttrArgv[ati], "cfeditorial") == 0) matIdx = MERGE_TYPE_EDITORIAL;
                     else if (strcmp(pAttrArgv[ati], "cfparental") == 0) matIdx = MERGE_TYPE_PARENTAL;
                     else if (strcmp(pAttrArgv[ati], "cfsound") == 0) matIdx = MERGE_TYPE_SOUND;
                     else if (strcmp(pAttrArgv[ati], "cfformat") == 0) matIdx = MERGE_TYPE_FORMAT;
                     else if (strcmp(pAttrArgv[ati], "cfrepeat") == 0) matIdx = MERGE_TYPE_REPEAT;
                     else if (strcmp(pAttrArgv[ati], "cfsubt") == 0) matIdx = MERGE_TYPE_SUBT;
                     else if (strcmp(pAttrArgv[ati], "cfmisc") == 0) matIdx = MERGE_TYPE_OTHERFEAT;
                     else if (strcmp(pAttrArgv[ati], "cfvps") == 0) matIdx = MERGE_TYPE_VPS;
                     else matIdx = MERGE_TYPE_COUNT;

                     if (matIdx < MERGE_TYPE_COUNT)
                     {
                        // parse index list
                        result = Tcl_SplitList(interp, pAttrArgv[ati + 1], &idxCount, &pIdxArgv);
                        if (result== TCL_OK)
                        {
                           if (idxCount <= *pCniCount)
                           {
                              for (idx=0; (idx < idxCount) && (result == TCL_OK); idx++)
                              {
                                 result = Tcl_GetInt(interp, pIdxArgv[idx], &cni);
                                 if (result == TCL_OK)
                                 {
                                    for (idx2=0; idx2 < *pCniCount; idx2++)
                                       if (pCniTab[idx2] == cni)
                                          break;

                                    if (idx2 < *pCniCount)
                                       pMax[matIdx][idx] = idx2;
                                    else
                                    {
                                       sprintf(comm, "C_ProvMerge_Start: illegal cni: 0x%X for attrib %s", cni, pAttrArgv[ati]);
                                       Tcl_SetResult(interp, comm, TCL_VOLATILE);
                                       result = TCL_ERROR;
                                       break;
                                    }
                                 }
                              }
                              // clear rest of index array, since used db count might be smaller than db merge count
                              for ( ; idx < MAX_MERGED_DB_COUNT; idx++)
                              {
                                 pMax[matIdx][idx] = 0xff;
                              }
                           }
                           else
                           {
                              sprintf(comm, "C_ProvMerge_Start: illegal index count: %d > cni count %d for attrib %s", idxCount, *pCniCount, pAttrArgv[ati]);
                              Tcl_SetResult(interp, comm, TCL_VOLATILE);
                              result = TCL_ERROR;
                              break;
                           }
                        }
                     }
                     else
                     {  // illegal keyword in attribute list
                        sprintf(comm, "C_ProvMerge_Start: unknown attrib type: %s", pAttrArgv[ati]);
                        Tcl_SetResult(interp, comm, TCL_VOLATILE);
                        result = TCL_ERROR;
                        break;
                     }
                  }
                  Tcl_Free((char *) pAttrArgv);
               }
            }
            else
               result = TCL_ERROR;
         }
         else if (*pCniCount == 0)
         {
            Tcl_SetResult(interp, "C_ProvMerge_Start: CNI count 0 is illegal", TCL_STATIC);
            result = TCL_ERROR;
         }
      }
   }
   else
      result = TCL_ERROR;

   return result;
}

// ----------------------------------------------------------------------------
// Initiate the database merging
// - called from the 'Merge providers' popup menu
// - parameters are taken from global Tcl variables, because the same
//   update is required at startup
//
static int ProvMerge_Start(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_ProvMerge_Start";
   MERGE_ATTRIB_MATRIX max;
   uint provCniTab[MAX_MERGED_DB_COUNT];
   uint netwopCniTab[MAX_NETWOP_COUNT];
   uint provCount, netwopCount;
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if (ProvMerge_ParseConfigString(interp, &provCount, provCniTab, &max[0]) == TCL_OK)
      {
         ProvMerge_ParseNetwopList(interp, &netwopCount, netwopCniTab);

         EpgContextCtl_Close(pUiDbContext);
         pUiDbContext = EpgContextMerge(provCount, provCniTab, max, netwopCount, netwopCniTab);

         EpgAcqCtl_UiProvChange();

         StatsWin_ProvChange(DB_TARGET_UI);

         UiControl_AiStateChange(NULL);
         eval_check(interp, "C_ResetFilter all; ResetFilterState");

         UiControl_CheckDbState();
         PiListBox_Reset();

         // put the new CNI at the front of the selection order and update the config file
         sprintf(comm, "UpdateProvSelection 0x00FF\n");
         eval_check(interp, comm);

         result = TCL_OK;
      }
      else
         result = TCL_ERROR;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Open the initial database after program start
// - provider can be chosen by the -provider flag: warn if it does not exist
// - else try all dbs in list list of previously opened ones (saved in rc/ini file)
// - else scan the db directory or create an empty database
//
void OpenInitialDb( uint startUiCni )
{
   char **pCniArgv;
   char *pTmpStr;
   uint cni, provIdx, provCount;

   // prepare list of previously opened databases
   provCount = 0;
   pTmpStr = Tcl_GetVar(interp, "prov_selection", TCL_GLOBAL_ONLY);
   if (pTmpStr != NULL)
   {
      Tcl_SplitList(interp, pTmpStr, &provCount, &pCniArgv);
   }

   cni = 0;
   provIdx = 0;
   do
   {
      if (startUiCni != 0)
      {  // first use the CNI given on the command line
         cni = startUiCni;
      }
      else if (provIdx < provCount)
      {  // then try all providers given in the list of previously loaded databases
         if (Tcl_GetInt(interp, pCniArgv[provIdx], &cni) != TCL_OK)
         {
            debug2("OpenInitialDb: cannot parse CNI #%d: %s - skipping", provIdx, pCniArgv[provIdx]);
            provIdx += 1;
            continue;
         }
         provIdx += 1;
      }
      else
      {  // if everything above failed, open any database found in the dbdir or create an empty one
         // (the CNI 0 has a special handling in the context open function)
         cni = 0;
      }

      if (cni == 0x00ff)
      {  // special case: merged db
         MERGE_ATTRIB_MATRIX max;
         uint pProvCniTab[MAX_MERGED_DB_COUNT];
         uint netwopCniTab[MAX_NETWOP_COUNT];
         uint provCount, netwopCount;

         if (ProvMerge_ParseConfigString(interp, &provCount, pProvCniTab, &max[0]) == TCL_OK)
         {
            ProvMerge_ParseNetwopList(interp, &netwopCount, netwopCniTab);
            pUiDbContext = EpgContextMerge(provCount, pProvCniTab, max, netwopCount, netwopCniTab);
         }
      }
      else
      {  // normal database or none specified
         pUiDbContext = EpgContextCtl_Open(cni, CTX_RELOAD_ERR_REQ);
         if ( (cni != 0) && (EpgDbContextGetCni(pUiDbContext) != cni) )
         {  // failed to open the requested database

            // destroy database, since we will try the next CNI (or at last CNI 0)
            EpgContextCtl_Close(pUiDbContext);
            pUiDbContext = NULL;
         }
      }
      // clear the cmd-line CNI since it's already used
      startUiCni = 0;
   }
   while (pUiDbContext == NULL);

   // update rc/ini file with new CNI order
   if (cni != 0)
   {
      sprintf(comm, "UpdateProvSelection 0x%04X\n", cni);
      eval_check(interp, comm);

      StatsWin_UpdateDbStatusLine(NULL);
   }
}

// ----------------------------------------------------------------------------
// Fetch the acquisition mode parameters from Tcl vars and pass then to acq control
// - if no valid config is found, the default mode is used: Follow-UI
//
int SetAcquisitionMode( void )
{
   EPGACQ_MODE mode;
   char **pCniArgv;
   char * pTmpStr;
   uint idx, cniCount, cniTab[MAX_MERGED_DB_COUNT];
   int  result;

   pTmpStr = Tcl_GetVar(interp, "acq_mode", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      if      (strcmp(pTmpStr, "passive") == 0)      mode = ACQMODE_PASSIVE;
      else if (strcmp(pTmpStr, "external") == 0)     mode = ACQMODE_EXTERNAL;
      else if (strcmp(pTmpStr, "follow-ui") == 0)    mode = ACQMODE_FOLLOW_UI;
      else if (strcmp(pTmpStr, "cyclic_2") == 0)     mode = ACQMODE_CYCLIC_2;
      else if (strcmp(pTmpStr, "cyclic_012") == 0)   mode = ACQMODE_CYCLIC_012;
      else if (strcmp(pTmpStr, "cyclic_02") == 0)    mode = ACQMODE_CYCLIC_02;
      else if (strcmp(pTmpStr, "cyclic_12") == 0)    mode = ACQMODE_CYCLIC_12;
      else                                           mode = ACQMODE_COUNT;  //illegal value

      switch (mode)
      {
         case ACQMODE_PASSIVE:
         case ACQMODE_EXTERNAL:
         case ACQMODE_FOLLOW_UI:
            EpgAcqCtl_SelectMode(mode, 0, NULL);
            result = TCL_OK;
            break;

         case ACQMODE_CYCLIC_2:
         case ACQMODE_CYCLIC_012:
         case ACQMODE_CYCLIC_02:
         case ACQMODE_CYCLIC_12:
            // cyclic mode -> expect list of CNIs in format: {0x0d94, ...}
            pTmpStr = Tcl_GetVar(interp, "acq_mode_cnis", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
            if (pTmpStr != NULL)
            {
               result = Tcl_SplitList(interp, pTmpStr, &cniCount, &pCniArgv);
               if (result == TCL_OK)
               {
                  if (cniCount > MAX_MERGED_DB_COUNT)
                  {
                     debug2("SetAcquisitionMode: too many CNIs: %d - limiting to %d", cniCount, MAX_MERGED_DB_COUNT);
                     cniCount = MAX_MERGED_DB_COUNT;
                  }

                  for (idx=0; (idx < cniCount) && (result == TCL_OK); idx++)
                  {
                     result = Tcl_GetInt(interp, pCniArgv[idx], cniTab + idx);
                     ifdebug2(result!=TCL_OK, "SetAcquisitionMode: arg #%d in '%s' is not a CNI", idx, pCniArgv[idx]);
                  }
                  Tcl_Free((char *) pCniArgv);

                  if (result == TCL_OK)
                  {
                     EpgAcqCtl_SelectMode(mode, cniCount, cniTab);
                  }
               }
            }
            else
               result = TCL_ERROR;
            break;

         default:
            sprintf(comm, "SetAcquisitionMode: illegal mode: %s", pTmpStr);
            Tcl_SetResult(interp, comm, TCL_VOLATILE);
            result = TCL_ERROR;
            break;
      }
   }
   else
   {
      debug0("SetAcquisitionMode: Tcl var acq_mode not defined - using default mode");
      result = TCL_ERROR;
   }

   if (result == TCL_ERROR)
   {  // silently ignore the error and set the default acquisition mode
      EpgAcqCtl_SelectMode(ACQMODE_FOLLOW_UI, 0, NULL);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Select acquisition mode and choose provider for acq
// - called after change of acq mode via the popup menu
// - parameters are taken from global Tcl variables, because the same
//   update is required at startup
//
static int UpdateAcquisitionMode(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_UpdateAcquisitionMode";
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = SetAcquisitionMode();

      // update help message in listbox if database is empty
      UiControl_CheckDbState();
      // update statistics windows for acq stats
      StatsWin_NewAi(pUiDbContext);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Fetch the list of provider frequencies from the global Tcl variable
// - the list contains pairs of CNI and frequency
//
static int GetProvFreqTab( ulong ** pFreqTab, uint * pCount )
{
   const char *pTmpStr;
   char **pCniFreqArgv;
   ulong *freqTab;
   int idx, freqCount, tmp;
   int freqIdx;
   int result;

   // initialize result values
   freqIdx = 0;
   freqTab = NULL;

   pTmpStr = Tcl_GetVar(interp, "prov_freqs", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      result = Tcl_SplitList(interp, pTmpStr, &freqCount, &pCniFreqArgv);
      if ((result == TCL_OK) && (freqCount > 0))
      {
         freqTab = xmalloc(freqCount / 2 * sizeof(ulong));

         // retrieve the frequencies from the list, skip the CNIs
         for (idx = 0; (idx + 1 < freqCount) && (result == TCL_OK); idx += 2)
         {
            result = Tcl_GetInt(interp, pCniFreqArgv[idx + 1], &tmp);
            if (result == TCL_OK)
            {
               freqTab[freqIdx] = (ulong) tmp;
               freqIdx += 1;
            }
         }
         Tcl_Free((char *) pCniFreqArgv);

         if (result != TCL_OK)
         {  // discard the allocated list upon errors
            xfree(freqTab);
            freqTab = NULL;
         }
      }
      else
         result = TCL_ERROR;
   }
   else
      result = TCL_OK;

   *pFreqTab = freqTab;
   *pCount   = freqIdx;

   return result;
}

// ----------------------------------------------------------------------------
// Append a line to the EPG scan messages
//
static void MenuCmd_AddEpgScanMsg( const char * pMsg )
{
   if (Tcl_VarEval(interp, ".epgscan.all.fmsg.msg insert end {", pMsg, "\n}\n"
                           ".epgscan.all.fmsg.msg see {end linestart - 2 lines}\n",
                           NULL
                  ) != TCL_OK)
      debug0("MenuCmd_AddEpgScanMsg: Tcl/Tk cmd failed");
}

// ----------------------------------------------------------------------------
// Start EPG scan
// - during the scan the focus is forced into the .epgscan popup
//
static int MenuCmd_StartEpgScan(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_StartEpgScan <input source> <slow=0/1> <refresh=0/1> <xawtv=0/1>";
   ulong *freqTab;
   int freqCount;
   int inputSource, isOptionSlow, isOptionRefresh, isOptionXawtv;
   uint rescheduleMs;
   int result;

   if (argc != 5)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      if ( (Tcl_GetInt(interp, argv[1], &inputSource) == TCL_OK) &&
           (Tcl_GetInt(interp, argv[2], &isOptionSlow) == TCL_OK) &&
           (Tcl_GetInt(interp, argv[3], &isOptionRefresh) == TCL_OK) &&
           (Tcl_GetInt(interp, argv[4], &isOptionXawtv) == TCL_OK) )
      {
         freqTab = NULL;
         freqCount = 0;
         if (isOptionRefresh)
         {
            // the returned list is freed by the EPG scan
            if ( (GetProvFreqTab(&freqTab, &freqCount) != TCL_OK) ||
                 (freqTab == NULL) || (freqCount == 0) )
            {
               Tcl_AppendResult(interp, " - Internal error, aborting start of refresh scan.", NULL);
               return TCL_ERROR;
            }
         }
         #ifndef WIN32
         else if (isOptionXawtv)
         {
            if ( (Xawtv_GetFreqTab(&freqTab, &freqCount) != TCL_OK) ||
                 (freqTab == NULL) || (freqCount == 0) )
            {  // message-box with explanation was already displayed
               return TCL_OK;
            }
            isOptionRefresh = TRUE;
         }
         #endif

         // clear message window
         sprintf(comm, ".epgscan.all.fmsg.msg delete 1.0 end\n");
         eval_check(interp, comm);

         switch (EpgScan_Start(inputSource, isOptionSlow, isOptionRefresh, freqTab, freqCount, &rescheduleMs, &MenuCmd_AddEpgScanMsg))
         {
            case EPGSCAN_ACCESS_DEV_VIDEO:
            case EPGSCAN_ACCESS_DEV_VBI:
               sprintf(comm, "tk_messageBox -type ok -icon error "
                             "-message {Failed to open the video device. "
                                       #ifndef WIN32
                                       "Close all other video applications and try again."
                                       #endif
                                      "}\n");
               eval_check(interp, comm);
               break;

            case EPGSCAN_NO_TUNER:
               sprintf(comm, "tk_messageBox -type ok -icon error "
                             "-message {The input source you have set in the 'TV card input configuration' "
                                       "is not a TV tuner device. Either change that setting or exit the "
                                       "EPG scan and tune the providers you're interested in manually.}\n");
               eval_check(interp, comm);
               break;

            case EPGSCAN_OK:
               // grab focus, disable all command buttons except "Abort"
               sprintf(comm, "EpgScanButtonControl start\n");
               eval_check(interp, comm);
               // update PI listbox help message, if there's no db in the browser yet
               UiControl_CheckDbState();
               // Install the event handler
               Tcl_CreateTimerHandler(rescheduleMs, MenuCmd_EpgScanHandler, NULL);
               break;

            default:
               SHOULD_NOT_BE_REACHED;
               break;
         }
         result = TCL_OK;
      }
      else
         result = TCL_ERROR;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Stop EPG scan
// - called from UI after Abort button or when popup is destroyed
// - also called from scan handler when scan has finished
//
static int MenuCmd_StopEpgScan(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   if (argc > 0)
   {  // called from UI -> stop scan handler
      EpgScan_Stop();
   }

   // release grab, re-enable command buttons
   sprintf(comm, "EpgScanButtonControl stop\n");
   eval_check(interp, comm);

   UiControl_CheckDbState();

   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Modify speed of EPG scan while scan is running
// - ignored if called otherwise (speed is also passed in start command)
//
static int MenuCmd_SetEpgScanSpeed(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_SetEpgScanSpeed <slow=0/1>";
   int isOptionSlow;
   int result;

   if (argc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if ( Tcl_GetInt(interp, argv[1], &isOptionSlow) )
   {  // the parameter is not an integer
      result = TCL_ERROR;
   }
   else
   {
      EpgScan_SetSpeed(isOptionSlow);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Backgroundhandler for the EPG scan
//
static void MenuCmd_EpgScanHandler( ClientData clientData )
{
   uint rescheduleMs;

   if (EpgScan_IsActive())
      rescheduleMs = EpgScan_EvHandler();
   else
      rescheduleMs = 0;

   if (rescheduleMs > 0)
   {
      // update the progress bar
      sprintf(comm, ".epgscan.all.baro.bari configure -width %d\n", (int)(EpgScan_GetProgressPercentage() * 140.0));
      eval_check(interp, comm);

      // Install the event handler
      Tcl_CreateTimerHandler(rescheduleMs, MenuCmd_EpgScanHandler, NULL);
   }
   else
   {
      MenuCmd_StopEpgScan(NULL, interp, 0, NULL);

      // ring the bell when the scan has finished
      eval_check(interp, "bell");
      eval_check(interp, ".epgscan.all.baro.bari configure -width 140\n");
   }
}

// ----------------------------------------------------------------------------
// Get list of all tuner types
// - on Linux the tuner is already configured in the bttv driver
//
static int MenuCmd_GetTunerList(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   #ifdef WIN32
   const char * pName;
   uint idx;

   idx = 0;
   while (1)
   {
      pName = BtDriver_GetTunerName(idx);
      if (pName != NULL)
         Tcl_AppendElement(interp, (char *) pName);
      else
         break;
      idx += 1;
   }
   #endif
   return TCL_OK;
}

// ----------------------------------------------------------------------------
// Get list of all TV card names
//
static int MenuCmd_GetTvCardList(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_HwCfgGetTvCardList";
   const char * pName;
   uint idx;
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      #ifdef __NetBSD__
      // On NetBSD BtDriver_GetCardName fetches its data from a struct which is filled here
      BtDriver_ScanDevices(TRUE);
      #endif

      idx = 0;
      do
      {
         pName = BtDriver_GetCardName(idx);
         if (pName != NULL)
            Tcl_AppendElement(interp, (char *) pName);

         idx += 1;
      }
      while (pName != NULL);

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Get list of all input types
//
static int MenuCmd_GetInputList(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetInputList <card index>";
   const char * pName;
   int  cardIndex;
   uint idx;
   int  result;

   if ( (argc != 2) || (Tcl_GetInt(interp, argv[1], &cardIndex) != TCL_OK) )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      idx = 0;
      while (1)
      {
         pName = BtDriver_GetInputName(cardIndex, idx);

         if (pName != NULL)
            Tcl_AppendElement(interp, (char *) pName);
         else
            break;
         idx += 1;
      }

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Set the hardware config params
// - called at startup or after user configuration via the GUI
// - the card index can also be set via command line and is passed here
//   from main; a value of -1 means don't care
//
int SetHardwareConfig( Tcl_Interp *interp, int newCardIndex )
{
   char **pParamsArgv;
   char * pTmpStr;
   int idxCount, input, tuner, pll, prio, cardidx, ftable;
   int result;

   pTmpStr = Tcl_GetVar(interp, "hwcfg", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr == NULL)
      pTmpStr = Tcl_GetVar(interp, "hwcfg_default", TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
   if (pTmpStr != NULL)
   {
      result = Tcl_SplitList(interp, pTmpStr, &idxCount, &pParamsArgv);
      if (result == TCL_OK)
      {
         if (idxCount == 6)
         {
            if ( (Tcl_GetInt(interp, pParamsArgv[0], &input) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[1], &tuner) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[2], &pll) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[3], &prio) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[4], &cardidx) == TCL_OK) &&
                 (Tcl_GetInt(interp, pParamsArgv[5], &ftable) == TCL_OK) )
            {
               if ((newCardIndex >= 0) && (newCardIndex != cardidx))
               {
                  cardidx = newCardIndex;
                  sprintf(comm, "HardwareConfigUpdateCardIdx %d\n", cardidx);
                  eval_check(interp, comm);
               }

               // pass the frequency table selection to the TV-channel module
               TvChannels_SelectFreqTable(ftable);
               // pass the hardware config params to the driver
               BtDriver_Configure(cardidx, tuner, pll, prio);
               // pass the input selection to acquisition control
               EpgAcqCtl_SetInputSource(input);
            }
            else
               result = TCL_ERROR;
         }
         else
         {
            Tcl_SetResult(interp, "SetHardwareConfig: must get 6 params", TCL_STATIC);
            result = TCL_ERROR;
         }
      }
   }
   else
      result = TCL_ERROR;

   return result;
}

// ----------------------------------------------------------------------------
// Apply the user-configured hardware configuration
// - called when hwcfg popup was closed
//
static int MenuCmd_UpdateHardwareConfig(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_UpdateHardwareConfig";
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      result = SetHardwareConfig(interp, -1);

      // update statistics windows for acq stats
      StatsWin_NewAi(pAcqDbContext);
      // update help message in listbox if database is empty
      UiControl_CheckDbState();
   }
   return result;
}

// ----------------------------------------------------------------------------
// Return information about the system time zone
//
static int MenuCmd_GetTimeZone(ClientData ttp, Tcl_Interp *interp, int argc, char *argv[])
{
   const char * const pUsage = "Usage: C_GetTimeZone";
   int  result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      struct tm *tm;
      time_t now;
      sint lto;

      tzset();
      tm = localtime(&now);
      lto = EpgLtoGet(now);

      sprintf(comm, "%d %d {%s}", lto/60, tm->tm_isdst, (tm->tm_isdst ? tzname[1] : tzname[0]));
      Tcl_SetResult(interp, comm, TCL_VOLATILE);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Initialize the module
//
void MenuCmd_Init( bool isDemoMode )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_ToggleAcq", &cmdInfo) == 0)
   {  // Create callback functions
      Tcl_CreateCommand(interp, "C_ToggleAcq", MenuCmd_ToggleAcq, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ToggleDumpStream", MenuCmd_ToggleDumpStream, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_DumpDatabase", MenuCmd_DumpDatabase, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SetControlMenuStates", SetControlMenuStates, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_ChangeProvider", MenuCmd_ChangeProvider, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_GetProvServiceInfos", MenuCmd_GetProvServiceInfos, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetCurrentDatabaseCni", MenuCmd_GetCurrentDatabaseCni, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetProvCnisAndNames", MenuCmd_GetProvCnisAndNames, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_GetAiNetwopList", MenuCmd_GetAiNetwopList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_ProvMerge_Start", ProvMerge_Start, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_UpdateAcquisitionMode", UpdateAcquisitionMode, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_StartEpgScan", MenuCmd_StartEpgScan, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_StopEpgScan", MenuCmd_StopEpgScan, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_SetEpgScanSpeed", MenuCmd_SetEpgScanSpeed, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_HwCfgGetTvCardList", MenuCmd_GetTvCardList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_HwCfgGetInputList", MenuCmd_GetInputList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_HwCfgGetTunerList", MenuCmd_GetTunerList, (ClientData) NULL, NULL);
      Tcl_CreateCommand(interp, "C_UpdateHardwareConfig", MenuCmd_UpdateHardwareConfig, (ClientData) NULL, NULL);

      Tcl_CreateCommand(interp, "C_GetTimeZone", MenuCmd_GetTimeZone, (ClientData) NULL, NULL);

      if (isDemoMode)
      {  // create menu with warning labels and disable some menu commands
         sprintf(comm, "CreateDemoModePseudoMenu\n");
         eval_check(interp, comm);
      }
   }
   else
      debug0("MenuCmd-Init: commands were already created");
}

