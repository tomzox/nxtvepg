/*
 *  XMLTV main module
 *
 *  Copyright (C) 2007-2011, 2020-2021 T. Zoerner
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
 *    which in return invoke callback functions in the database module
 *    for all recognized tokens.
 *
 *    This module also contains helper function which are used when
 *    processing command line parameter and database import via the GUI.
 *    They allow to check if a given file is an XMLTV file and to scan
 *    a directory for XMLTV files.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_XMLTV
#define DPRINTF_OFF

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#ifndef WIN32
#include <sys/types.h>
#include <unistd.h>
#else
#include <windows.h>
#include <wchar.h>
#endif

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgctxctl.h"

#include "xmltv/xml_cdata.h"
#include "xmltv/xmltv_tags.h"
#include "xmltv/xmltv_db.h"
#include "xmltv/xmltv_cni.h"
#include "xmltv/xmltv_main.h"


// ----------------------------------------------------------------------------
// local definitions
//
#ifdef WIN32
#define PATH_SEPARATOR       '\\'
#define PATH_SEPARATOR_STR   "\\"
#else
#define PATH_SEPARATOR       '/'
#define PATH_SEPARATOR_STR   "/"
#endif

// ----------------------------------------------------------------------------
// Return a human-redable error message
//
const char * Xmltv_TranslateErrorCode( uint detection )
{
   return XmltvTags_TranslateErrorCode((XMLTV_DETECTION) detection);
}

// ----------------------------------------------------------------------------
// Determine if document looks like XML by the error code
//
bool Xmltv_IsXmlDocument( uint detection )
{
   // return TRUE if at least one valie XML tag was seen (e.g. <?xml> tag)
   return ((detection & XMLTV_DETECTED_XML) != 0);
}

// ----------------------------------------------------------------------------
// Entry point
// - TODO: pass preferred language?
//
EPGDB_CONTEXT * Xmltv_Load( FILE * fp, uint provCni, const char * pProvName, time_t mtime, bool isPeek )
{
   EPGDB_CONTEXT * pDbContext = NULL;

   // initialize internal state
   XmltvDb_Init(provCni, mtime, isPeek);

   // parse the XMLTV file
   XmltvTags_StartScan(fp, TRUE);

   if (XMLTV_DETECTED_OK(XmltvTags_QueryDetection()))
   {
      pDbContext = XmltvDb_GetDatabase(pProvName);
   }

   XmltvDb_Destroy();

   return pDbContext;
}

// ----------------------------------------------------------------------------
// Check if a given file is an XMLTV file and determine its version
//
EPGDB_RELOAD_RESULT Xmltv_CheckHeader( const char * pFilename )
{
   XMLTV_DETECTION detection = XMLTV_DETECTED_OK;
   FILE * fp;
   EPGDB_RELOAD_RESULT result;

   fp = fopen(pFilename, "r");
   if (fp != NULL)
   {
      // when starting the scanner with version set to "unknown", all data is discarded and
      // the scanner stops as soon as a tag or attribute is found which identifies the version
      XmltvTags_StartScan(fp, FALSE);

      detection = XmltvTags_QueryDetection();

      if (XMLTV_DETECTED_OK(detection))
         result = EPGDB_RELOAD_OK;
      else
         result = (EPGDB_RELOAD_RESULT)(EPGDB_RELOAD_XML_MASK | detection);

      fclose(fp);
   }
   else
   {
      if (errno == EACCES)
         result = EPGDB_RELOAD_ACCESS;
      else
         result = EPGDB_RELOAD_EXIST;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Load data from an XML file
//
EPGDB_CONTEXT * Xmltv_CheckAndLoad( const char * pFilename, uint provCni,
                                    bool isPeek, uint * pErrCode, time_t * pMtime )
{
   EPGDB_CONTEXT * pDbContext = NULL;
   uint result;
   const char * pBaseName;
   FILE * fp;

   fp = fopen(pFilename, "r");
   if (fp != NULL)
   {
#ifndef WIN32
      struct stat st;
      if (fstat(fileno(fp), &st) == 0)
#else
      struct _stat st;
      if (_fstat(fileno(fp), &st) == 0)
#endif
         *pMtime = st.st_mtime;
      else
         *pMtime = 0;

      pBaseName = strrchr(pFilename, PATH_SEPARATOR);
      if (pBaseName != NULL)
         pBaseName += 1;
      else
         pBaseName = pFilename;

      pDbContext = Xmltv_Load(fp, provCni, pBaseName, *pMtime, isPeek);

      if (pDbContext != NULL)
         result = EPGDB_RELOAD_OK;
      else
         result = EPGDB_RELOAD_XML_MASK | XmltvTags_QueryDetection();

      fclose(fp);
   }
   else
   {
      if (errno == EACCES)
         result = EPGDB_RELOAD_ACCESS;
      else
         result = EPGDB_RELOAD_EXIST;
   }

   if (pErrCode != NULL)
      *pErrCode = result;

   return pDbContext;
}

// ----------------------------------------------------------------------------
#if 0
#include "epgdb/epgdbmgmt.h"
#include "epgctl/epgctxctl.h"
#include "epgui/uictrl.h"
#include "epgui/dumpraw.h"
void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, CONTEXT_RELOAD_ERR_HAND errHand, bool isNewDb ) { }
void UiControlMsg_NetAcqError( void ) { }
void UiControlMsg_AcqEvent( ACQ_EVENT acqEvent ) { }
EPGDB_CONTEXT * pUiDbContext;

extern int yydebug;

int main( int argc, char ** argv )
{
   EPGDB_CONTEXT * pDbContext;
   uint detection;
   FILE * fp;

   if (argc >= 2)
   {
#if DEBUG_SWITCH_XMLTV == ON
      if ((argc >= 3) && (strcmp(argv[2], "debug") == 0))
      {
         yydebug = 1;
      }
#endif

      fp = fopen(argv[1], "r");
      if (fp != NULL)
      {
         XmltvTags_StartScan(fp, TRUE);
         detection = XmltvTags_QueryDetection();
         if (XMLTV_DETECTED_OK(detection))
         {
            fseek(fp, 0, SEEK_SET);

            pDbContext = Xmltv_Load(fp, 0xfe, "XMLTV", 0, FALSE);

            EpgDbSavSetupDir("tmp0", NULL);
            pDbContext->modified = TRUE;
            EpgDbDump(pDbContext);
            EpgDbDestroy(pDbContext, FALSE);
         }
         else
         {
            fprintf(stderr, "Failed to load %s: %s\n", argv[1],
                            XmltvTags_TranslateErrorCode(detection));
         }
         fclose(fp);
      }
      else
         fprintf(stderr, "Failed to open file %s: %s\n", argv[1], strerror(errno));
   }
   else
      fprintf(stderr, "Usage: %s <xml-file>\n", argv[0]);

   chk_memleakage();

   return 0;
}
#endif

