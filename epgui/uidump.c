/*
 *  Nextview GUI: Export of programme data in various formats
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
#include "epgdb/epgdbmerge.h"
#include "epgctl/epgacqctl.h"
#include "epgui/pibox.h"
#include "epgui/pidescr.h"
#include "epgui/cmdline.h"
#include "epgui/epgmain.h"
#include "epgui/epgsetup.h"
#include "epgui/epgquery.h"
#include "epgui/uictrl.h"
#include "epgui/menucmd.h"
#include "epgui/uidump.h"
#include "epgui/dumptext.h"
#include "epgui/dumphtml.h"
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
      default:
         fatal1("EpgDump-QueryXmlDtdVersion: invalid mode %d\n", value);
         dumpMode = DUMP_XMLTV_ANY;
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
         EpgDumpXml_Standalone(pUiDbContext, NULL, fpDst, dumpMode);
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
      else if (strcasecmp("themes", pModeStr) == 0)
         mode = DUMP_TEXT_THEMES;
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
               EpgDumpText_Standalone(pUiDbContext, NULL, fp, mode);

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
            EpgDumpText_Standalone(pUiDbContext, NULL, stdout, mode);
         }

         result = TCL_OK;
      }
      else
      {  // unsupported mode (internal error, since the GUI should use radio buttons)
         Tcl_SetResult(interp, (char*) "C_DumpTabsDatabase: illegal type keyword", TCL_STATIC);
         result = TCL_ERROR;
      }
   }

   return result;
}

#if 0  // unused code
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
#endif

// ---------------------------------------------------------------------------
// Helper function to append multiple UTF-8 strings within a Tcl object
//
static Tcl_Obj * ConcatStringsAsObj( const char * pStr, const char * pStr2, const char * pStr3 )
{
   Tcl_Obj * pObj = Tcl_NewStringObj(pStr, -1);

   if (pStr2 != NULL)
      Tcl_AppendToObj(pObj, pStr2, strlen(pStr2));

   if (pStr3 != NULL)
      Tcl_AppendToObj(pObj, pStr3, strlen(pStr3));

   return pObj;
}

// ----------------------------------------------------------------------------
// Show info about the currently selected item in pop-up window
// - used for debugging only
//
static int EpgDump_GetRawPi( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Dump_GetRawPi";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   const char * pCfNetname;
   const char * pThemeStr;
   char start_str[50], stop_str[50];
   time_t start_time;
   time_t stop_time;
   bool isFromAi;
   int index;
   int len;
   Tcl_Obj * pResultList;
   int result;
#define APPEND_1(A,L) Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj((A),(L)))
#define APPEND_3(A,B,C) Tcl_ListObjAppendElement(interp, pResultList, ConcatStringsAsObj((A),(B),(C)))
#define APPEND_ENC(E,A,B,C) Tcl_ListObjAppendElement(interp, pResultList, TranscodeToUtf8((E),(A),(B),(C)))

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
         // first two elements in result list are netwop and start time (as unique key)
         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(pPiBlock->netwop_no));
         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewIntObj(pPiBlock->start_time));

         // third element is the programme title
         APPEND_3(PI_GET_TITLE(pPiBlock), "\n", NULL);

         // following elements are the contents
         pCfNetname = EpgSetup_GetNetName(pAiBlock, pPiBlock->netwop_no, &isFromAi);
         APPEND_3("Network: \t", pCfNetname, "\n");

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
         APPEND_1(comm, len);

         const char *p;
         switch(pPiBlock->feature_flags & 0x03)
         {
           case  0: p = "mono"; break;
           case  1: p = "2chan"; break;
           case  2: p = "stereo"; break;
           case  3: p = "surround"; break;
           default: p = ""; break;
         }
         len = sprintf(comm, "Sound:\t%s\n", p);
         APPEND_1(comm, len);

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
         APPEND_1(comm, len);

         if (pPiBlock->parental_rating == PI_PARENTAL_UNDEFINED)
            len = sprintf(comm, "Parental rating:\tnone\n");
         else if (pPiBlock->parental_rating == 0)
            len = sprintf(comm, "Parental rating:\tgeneral\n");
         else
            len = sprintf(comm, "Parental rating:\t%d years and up\n", pPiBlock->parental_rating);
         APPEND_1(comm, len);

         if (pPiBlock->editorial_rating == PI_EDITORIAL_UNDEFINED)
            len = sprintf(comm, "Editorial rating:\tnone\n");
         else
            len = sprintf(comm, "Editorial rating:\t%d of %d\n",
                          pPiBlock->editorial_rating, pPiBlock->editorial_max_val);
         APPEND_1(comm, len);

         for (index=0; index < pPiBlock->no_themes; index++)
         {
            len = sprintf(comm, "Theme:\t%d = ", pPiBlock->themes[index]);
            pThemeStr = EpgDbGetThemeStr(pUiDbContext, pPiBlock->themes[index]);
            APPEND_3(comm, pThemeStr, "\n");
         }
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
      Tcl_SetObjResult(interp, pResultList);
   }

   return result;
}

// ---------------------------------------------------------------------------
// Database export via command line: route call to sub-modules
//
void EpgDump_Standalone( EPGDB_CONTEXT * pDbContext, FILE * fp,
                         int dumpMode, int dumpSubMode, const char ** pFilterStr )
{
   FILTER_CONTEXT * fc = NULL;

   if (*pFilterStr != NULL)
   {
      fc = EpgQuery_Parse(pDbContext, pFilterStr);
   }

   switch (dumpMode)
   {
      case EPG_DUMP_XMLTV:
         if (dumpSubMode == DUMP_XMLTV_ANY)
         {
            dumpSubMode = EpgDump_QueryXmlDtdVersion(interp);
         }
         EpgDumpXml_Standalone(pUiDbContext, fc, stdout, (DUMP_XML_MODE) dumpSubMode);
         break;

      case EPG_DUMP_TEXT:
         EpgDumpText_Standalone(pUiDbContext, fc, stdout, (DUMP_TEXT_MODE) dumpSubMode);
         break;

      case EPG_DUMP_HTML:
         EpgDumpHtml_Standalone(pUiDbContext, fc, stdout, dumpSubMode);
         break;

      default:
         fatal1("EpgDump-Standalone: unsupported dump mode %d\n", dumpMode);
         break;
   }

   if (fc != NULL)
   {
      EpgDbFilterDestroyContext(fc);
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

   Tcl_CreateObjCommand(interp, "C_Dump_GetRawPi", EpgDump_GetRawPi, (ClientData) NULL, NULL);

   EpgDumpHtml_Init();
}

