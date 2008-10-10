/*
 *  Nextview GUI: Export of programme data in various formats
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
 *    This module is an interface inbetween the user interface and the
 *    database export modules.  It retrieves parameters from the GUI
 *    configuration file and opens the output files.  (It's split off
 *    from the dump modules to avoid dependencies on Tcl for these.)
 *
 *  Author: Tom Zoerner
 *
 *  $Id: uidump.c,v 1.6 2008/01/21 22:42:35 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <tcl.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgtscqueue.h"
#include "epgdb/epgdbmerge.h"
#include "epgctl/epgacqctl.h"
#include "epgui/pdc_themes.h"
#include "epgui/pibox.h"
#include "epgui/pidescr.h"
#include "epgui/cmdline.h"
#include "epgui/epgmain.h"
#include "epgui/epgsetup.h"
#include "epgui/uictrl.h"
#include "epgui/menucmd.h"
#include "epgui/uidump.h"
#include "epgui/dumptext.h"
#include "epgui/dumphtml.h"
#include "epgui/dumpraw.h"
#include "epgui/dumpxml.h"
#include "epgtcl/dlg_dump.h"

static Tcl_DString ds_netname;

// ----------------------------------------------------------------------------
// Helper func: read integer from global Tcl var
// - DTD version constants are defined at Tcl script level
//
static DUMP_XML_MODE EpgDump_QueryXmlDtdVersion( Tcl_Interp *interp )
{
   Tcl_Obj  * pVarObj;
   int  value;
   DUMP_XML_MODE dumpMode;

   pVarObj = Tcl_GetVar2Ex(interp, "dumpxml_format", NULL, TCL_GLOBAL_ONLY);
   if ( (pVarObj == NULL) ||
        (Tcl_GetIntFromObj(interp, pVarObj, &value) != TCL_OK) )
   {
      debug1("EpgDump-QueryXmlDtdVersion: failed to parse Tcl var 'dumpxml_format': %s", ((pVarObj != NULL) ? Tcl_GetString(pVarObj) : "*undef*"));
      value = EPGTCL_XMLTV_DTD_5_GMT;
   }

   switch( value )
   {
      case EPGTCL_XMLTV_DTD_5_GMT:
         dumpMode = DUMP_XMLTV_DTD_5_GMT;
         break;
      case EPGTCL_XMLTV_DTD_5_LTZ:
         dumpMode = DUMP_XMLTV_DTD_5_LTZ;
         break;
      case EPGTCL_XMLTV_DTD_6:
         dumpMode = DUMP_XMLTV_DTD_6;
         break;
      default:
         fatal1("EpgDump-QueryXmlDtdVersion: invalid mode %d\n", value);
         dumpMode = EPGTCL_XMLTV_DTD_5_GMT;
         break;
   }

   return dumpMode;
}

// ----------------------------------------------------------------------------
// Dump programme titles and/or descriptions into file in XMLTV format
//
static int EpgDump_Xml( ClientData ttp, Tcl_Interp *interp,int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_DumpXml <file-name>";
   const char * pFileName;
   FILE       * fpDst;
   Tcl_DString  ds;
   DUMP_XML_MODE dumpMode;
   int  result;

   if ( (objc != 1+1) || (pFileName = Tcl_GetString(objv[1])) == NULL )
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      pFileName = Tcl_UtfToExternalDString(NULL, pFileName, -1, &ds);

      dumpMode = EpgDump_QueryXmlDtdVersion(interp);

      // Open source and create destination XML file
      fpDst = fopen(pFileName, "w");
      if (fpDst != NULL)
      {
         EpgDumpXml_Standalone(pUiDbContext, fpDst, dumpMode);
         fclose(fpDst);
      }
      else
      {  // access, create or truncate failed -> notify the user
         sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumpxml -message \"Failed to open file '%s' for writing: %s\"",
                       pFileName, strerror(errno));
         eval_check(interp, comm);
         Tcl_ResetResult(interp);
      }

      Tcl_DStringFree(&ds);
      result = TCL_OK;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Translate string into dump mode
//
static DUMP_TEXT_MODE EpgDump_GetTextMode( const char * pModeStr )
{
   DUMP_TEXT_MODE  mode = DUMP_TEXT_COUNT;

   if (pModeStr != NULL)
   {
      if (strcasecmp("ai", pModeStr) == 0)
         mode = DUMP_TEXT_AI;
      else if (strcasecmp("pi", pModeStr) == 0)
         mode = DUMP_TEXT_PI;
      else if (strcasecmp("pdc", pModeStr) == 0)
         mode = DUMP_TEXT_PDC;
      else
         debug1("EpgDump-GetTextMode: unknown mode: %s", pModeStr);
   }
   else
      debug0("EpgDump-GetTextMode: illegal NULL ptr param");

   return mode;
}

// ----------------------------------------------------------------------------
// Dump the database in TAB-separated format
//
static int EpgDump_Text( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_DumpTabsDatabase <file-name> <type>";
   DUMP_TEXT_MODE mode;
   const char * pFileName;
   Tcl_DString ds;
   FILE *fp;
   int result;

   if (objc != 1+2)
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
      mode = EpgDump_GetTextMode(Tcl_GetString(objv[2]));
      if (mode != DUMP_TEXT_COUNT)
      {
         if (Tcl_GetCharLength(objv[1]) > 0)
         {
            pFileName = Tcl_UtfToExternalDString(NULL, pFileName, -1, &ds);
            fp = fopen(pFileName, "w");
            if (fp != NULL)
            {  // file created successfully -> start dump
               EpgDumpText_Standalone(pUiDbContext, fp, mode);

               fclose(fp);
            }
            else
            {  // access, create or truncate failed -> inform the user
               sprintf(comm, "tk_messageBox -type ok -icon error -parent .dumptabs -message \"Failed to open file '%s' for writing: %s\"",
                             Tcl_GetString(objv[1]), strerror(errno));
               eval_check(interp, comm);
               Tcl_ResetResult(interp);
            }
            Tcl_DStringFree(&ds);
         }
         else
         {  // no file name given -> dump to stdout
            EpgDumpText_Standalone(pUiDbContext, stdout, mode);
         }

         result = TCL_OK;
      }
      else
      {  // unsupported mode (internal error, since the GUI should use radio buttons)
         Tcl_SetResult(interp, "C_DumpTabsDatabase: illegal type keyword", TCL_STATIC);
         result = TCL_ERROR;
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Helper func: read boolean from global Tcl var
//
static bool EpgDump_ReadTclBool( Tcl_Interp *interp,
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
         debug3("EpgDump-ReadTclBool: cannot read Tcl var %s (%s) - use default val %d", pName, ((pVarObj != NULL) ? Tcl_GetString(pVarObj) : "*undef*"), fallbackVal);
         value = fallbackVal;
      }
   }
   else
   {
      fatal0("EpgDump-ReadTclBool: illegal NULL ptr param");
      value = fallbackVal;
   }
   return (bool) value;
}

// ----------------------------------------------------------------------------
// Dump the complete database (invoked via GUI)
//
static int EpgDump_RawDatabase( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_DumpRawDatabase <file-name>";
   const char * pFileName;
   Tcl_DString ds;
   FILE *fp;
   bool  do_pi, do_xi, do_ai, do_ni;
   bool  do_oi, do_mi, do_li, do_ti;
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
         do_pi = EpgDump_ReadTclBool(interp, "dumpdb_pi", 1);
         do_xi = EpgDump_ReadTclBool(interp, "dumpdb_xi", 1);
         do_ai = EpgDump_ReadTclBool(interp, "dumpdb_ai", 1);
         do_ni = EpgDump_ReadTclBool(interp, "dumpdb_ni", 1);
         do_oi = EpgDump_ReadTclBool(interp, "dumpdb_oi", 1);
         do_mi = EpgDump_ReadTclBool(interp, "dumpdb_mi", 1);
         do_li = EpgDump_ReadTclBool(interp, "dumpdb_li", 1);
         do_ti = EpgDump_ReadTclBool(interp, "dumpdb_ti", 1);

         EpgDumpRaw_Database(pUiDbContext, fp,
                             do_pi, do_xi, do_ai, do_ni,
                             do_oi, do_mi, do_li, do_ti);

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
static int EpgDump_RawStream( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ToggleDumpStream <boolean>";
   int value;
   int result;

   if (objc != 2)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else if (Tcl_GetBooleanFromObj(interp, objv[1], &value) != TCL_OK)
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
// Show info about the currently selected item in pop-up window
// - used for debugging only
//
static int EpgDump_GetRawPi( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Dump_GetRawPi";
   const AI_BLOCK * pAiBlock;
   const AI_NETWOP *pNetwop;
   const PI_BLOCK * pPiBlock;
   const DESCRIPTOR *pDesc;
   const char * pCfNetname;
   const uchar * pThemeStr;
   const uchar * pGeneralStr;
   uchar *p;
   uchar start_str[50], stop_str[50];
   time_t start_time;
   time_t stop_time;
   bool isFromAi;
   int index;
   int len;
   Tcl_Obj * pResultList;
   int result;
#define APPEND_ASCII(A,L) Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj((A),(L)))
#define APPEND_ENC(E,A,B,C) Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8((E),(A),(B),(C)))
#define APPEND_ENC2(E,A,B,C,D) Tcl_ListObjAppendElement(interp, pResultList, AppendToUtf8((E), TranscodeToUtf8((E),(A),(B),NULL),(C),(D)))

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);
      result = TCL_OK; 

      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      pPiBlock = PiBox_GetSelectedPi();
      if ((pAiBlock != NULL) && (pPiBlock != NULL))
      {
         pNetwop = AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no);

         // first two elements in result list are netwop and start time (as unique key)
         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(pPiBlock->netwop_no));
         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(pPiBlock->start_time));

         // third element is the programme title
         APPEND_ENC(EPG_ENC_NXTVEPG, NULL, PI_GET_TITLE(pPiBlock), "\n");

         // following elements are the contents
         pCfNetname = EpgSetup_GetNetName(pAiBlock, pPiBlock->netwop_no, &isFromAi);
         APPEND_ENC(EPG_ENC_NETNAME(isFromAi), "Network: \t", pCfNetname, "\n");

         len = sprintf(comm, "BlockNo:\t0x%04X in %04X-%04X-%04X\n", pPiBlock->block_no, pNetwop->startNo, pNetwop->stopNo, pNetwop->stopNoSwo);
         APPEND_ASCII(comm, len);

         start_time = pPiBlock->start_time;
         strftime(start_str, sizeof(start_str), "%a %d.%m %H:%M", localtime(&start_time));
         stop_time = pPiBlock->stop_time;
         strftime(stop_str, sizeof(stop_str), "%a %d.%m %H:%M", localtime(&stop_time));
         len = sprintf(comm, "Start:\t%s\nStop:\t%s\n", start_str, stop_str);
         APPEND_ENC(EPG_ENC_SYSTEM, NULL, comm, NULL);

         if (pPiBlock->pil != 0x7FFF)
         {
            len = sprintf(comm, "PIL:\t%02d.%02d. %02d:%02d\n",
                                (pPiBlock->pil >> 15) & 0x1F,
                                (pPiBlock->pil >> 11) & 0x0F,
                                (pPiBlock->pil >>  6) & 0x1F,
                                (pPiBlock->pil      ) & 0x3F );
         }
         else
         {
            len = sprintf(comm, "PIL:\tnone\n");
         }
         APPEND_ASCII(comm, len);

         switch(pPiBlock->feature_flags & 0x03)
         {
           case  0: p = "mono"; break;
           case  1: p = "2chan"; break;
           case  2: p = "stereo"; break;
           case  3: p = "surround"; break;
           default: p = ""; break;
         }
         len = sprintf(comm, "Sound:\t%s\n", p);
         APPEND_ASCII(comm, len);

         if (pPiBlock->feature_flags & ~0x03)
            len = sprintf(comm, "Features:\t%s%s%s%s%s%s%s\n",
                        ((pPiBlock->feature_flags & 0x04) ? " wide" : ""),
                        ((pPiBlock->feature_flags & 0x08) ? " PAL+" : ""),
                        ((pPiBlock->feature_flags & 0x10) ? " digital" : ""),
                        ((pPiBlock->feature_flags & 0x20) ? " encrypted" : ""),
                        ((pPiBlock->feature_flags & 0x40) ? " live" : ""),
                        ((pPiBlock->feature_flags & 0x80) ? " repeat" : ""),
                        ((pPiBlock->feature_flags & 0x100) ? " subtitles" : "")
                        );
         else
            len = sprintf(comm, "Features:\tnone\n");
         APPEND_ASCII(comm, len);

         if (pPiBlock->parental_rating == 0)
            len = sprintf(comm, "Parental rating:\tnone\n");
         else if (pPiBlock->parental_rating == 1)
            len = sprintf(comm, "Parental rating:\tgeneral\n");
         else
            len = sprintf(comm, "Parental rating:\t%d years and up\n", pPiBlock->parental_rating * 2);
         APPEND_ASCII(comm, len);

         if (pPiBlock->editorial_rating == 0)
            len = sprintf(comm, "Editorial rating:\tnone\n");
         else
            len = sprintf(comm, "Editorial rating:\t%d of 1..7\n", pPiBlock->editorial_rating);
         APPEND_ASCII(comm, len);

         for (index=0; index < pPiBlock->no_themes; index++)
         {
            pThemeStr = PdcThemeGetWithGeneral(pPiBlock->themes[index], &pGeneralStr, TRUE);
            len = sprintf(comm, "Theme:\t0x%02X ", pPiBlock->themes[index]);
            APPEND_ENC2(EPG_ENC_NXTVEPG, comm, pThemeStr, pGeneralStr, "\n");
         }

         for (index=0; index < pPiBlock->no_sortcrit; index++)
         {
            len = sprintf(comm, "Sorting Criterion:\t0x%02X\n", pPiBlock->sortcrits[index]);
            APPEND_ASCII(comm, len);
         }

         pDesc = PI_GET_DESCRIPTORS(pPiBlock);
         for (index=0; index < pPiBlock->no_descriptors; index++)
         {
            switch (pDesc[index].type)
            {
               case LI_DESCR_TYPE:    len = sprintf(comm, "Descriptor:\tlanguage ID %d\n", pDesc[index].id); break;
               case TI_DESCR_TYPE:    len = sprintf(comm, "Descriptor:\tsubtitle ID %d\n", pDesc[index].id); break;
               case MERGE_DESCR_TYPE: len = sprintf(comm, "Descriptor:\tmerged from db #%d\n", pDesc[index].id); break;
               default:               len = sprintf(comm, "Descriptor:\tunknown type=%d, ID=%d\n", pDesc[index].type, pDesc[index].id); break;
            }
            APPEND_ASCII(comm, len);
         }

         len = sprintf(comm, "Acq. stream:\t%d\n", EpgDbGetStream(pPiBlock));
         APPEND_ASCII(comm, len);
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
      Tcl_SetObjResult(interp, pResultList);
   }

   return result;
}

// ---------------------------------------------------------------------------
// Database export via command line: route call to sub-modules
//
void EpgDump_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp, int dumpMode, int dumpSubMode )
{
   switch (dumpMode)
   {
      case EPG_DUMP_XMLTV:
         if (dumpSubMode == DUMP_XMLTV_ANY)
         {
            dumpSubMode = EpgDump_QueryXmlDtdVersion(interp);
         }
         EpgDumpXml_Standalone(pUiDbContext, stdout, dumpSubMode);
         break;

      case EPG_DUMP_TEXT:
         EpgDumpText_Standalone(pUiDbContext, stdout, dumpSubMode);
         break;

      case EPG_DUMP_HTML:
         EpgDumpHtml_Standalone(pUiDbContext, stdout, dumpSubMode);
         break;

      case EPG_DUMP_RAW:
         EpgDumpRaw_Standalone(pUiDbContext, stdout);
         break;

      default:
         fatal1("EpgDump-Standalone: unsupported dump mode %d\n", dumpMode);
         break;
   }
}

// ----------------------------------------------------------------------------
// Free resources allocated by this module (cleanup during program exit)
//
void EpgDump_Destroy( void )
{
   Tcl_DStringFree(&ds_netname);

   EpgDumpHtml_Destroy();
}

// ----------------------------------------------------------------------------
// Create the Tcl/Tk commands provided by this module
// - this should be called only once during start-up
//
void EpgDump_Init( void )
{
   Tcl_DStringInit(&ds_netname);

   Tcl_CreateObjCommand(interp, "C_DumpTabsDatabase", EpgDump_Text, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_DumpXml", EpgDump_Xml, (ClientData) NULL, NULL);

   Tcl_CreateObjCommand(interp, "C_ToggleDumpStream", EpgDump_RawStream, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_DumpRawDatabase", EpgDump_RawDatabase, (ClientData) NULL, NULL);

   Tcl_CreateObjCommand(interp, "C_Dump_GetRawPi", EpgDump_GetRawPi, (ClientData) NULL, NULL);

   EpgDumpHtml_Init();
}

