/*
 *  XMLTV main module
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: xmltv_main.c,v 1.6 2007/12/30 15:47:46 tom Exp tom $
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
#include <dirent.h>
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
#include "epgdb/epgdbsav.h"

#include "xmltv/xml_cdata.h"
#include "xmltv/xmltv_tags.h"
#include "xmltv/xmltv_db.h"
#include "xmltv/xmltv_cni.h"
#include "xmltv/xmltv_main.h"

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
EPGDB_CONTEXT * Xmltv_Load( FILE * fp, uint dtd, uint provCni, const char * pProvName, bool isPeek )
{
   EPGDB_CONTEXT * pDbContext;

   // initialize internal state
   XmltvDb_Init(dtd, provCni, isPeek);

   // parse the XMLTV file
   XmltvTags_StartScan(fp, dtd);

   pDbContext = XmltvDb_GetDatabase(pProvName);

   XmltvDb_Destroy();

   return pDbContext;
}

// ----------------------------------------------------------------------------
// Check if a given file is an XMLTV file and determine it's version
//
bool Xmltv_CheckHeader( const char * pFilename, uint * pDetection )
{
   XMLTV_DTD_VERSION dtd;
   XMLTV_DETECTION detection;
   FILE * fp;

   dtd = XMLTV_DTD_UNKNOWN;
   detection = 0;

   fp = fopen(pFilename, "r");
   if (fp != NULL)
   {
      // when starting the scanner with version set to "unknown", all data is discarded and
      // the scanner stops as soon as a tag or attribute is found which identifies the version
      XmltvTags_StartScan(fp, XMLTV_DTD_UNKNOWN);

      dtd = XmltvTags_QueryVersion(&detection);

      fclose(fp);
   }

   if (pDetection != NULL)
   {
      *pDetection = detection;
   }

   return (dtd != XMLTV_DTD_UNKNOWN);
}

// ----------------------------------------------------------------------------
// Load data from an XML file - auto-detect DTD version
//
EPGDB_CONTEXT * Xmltv_CheckAndLoad( const char * pFilename, uint provCni,
                                    bool isPeek, uint * pErrCode, time_t * pMtime )
{
   EPGDB_CONTEXT * pDbContext = NULL;
   XMLTV_DTD_VERSION dtd;
   XMLTV_DETECTION detection;
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

      XmltvTags_StartScan(fp, XMLTV_DTD_UNKNOWN);
      dtd = XmltvTags_QueryVersion(&detection);

      if (dtd != XMLTV_DTD_UNKNOWN)
      {
         fseek(fp, 0, SEEK_SET);

         pBaseName = strrchr(pFilename, PATH_SEPARATOR);
         if (pBaseName != NULL)
            pBaseName += 1;
         else
            pBaseName = pFilename;

         pDbContext = Xmltv_Load(fp, dtd, provCni, pBaseName, isPeek);

         if (pDbContext != NULL)
         {
            EpgDbSetAiUpdateTime(pDbContext, *pMtime);
         }
         result = EPGDB_RELOAD_OK;
      }
      else
      {
         result = EPGDB_RELOAD_XML_MASK | detection;
      }
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

// ---------------------------------------------------------------------------
// Get modification time of the given XML file
//
time_t Xmltv_GetMtime( const char * pFilename )
{
   time_t mtime = 0;
#ifndef WIN32
   struct stat st;
   if (stat(pFilename, &st) == 0)
#else
   struct _stat st;
   if (_stat(pFilename, &st) == 0)
#endif
   {
      mtime = st.st_mtime;
   }
   return mtime;
}

// ---------------------------------------------------------------------------
// Scans a given directory EPG databases
// - returns dynamically allocated struct which has to be freed by the caller
// - the struct holds a list which contains for each database the CNI
//   (extracted from the file name) and the file modification timestamp
//
void Xmltv_ScanDir( const char * pDirPath, const char * pExtension,
                    void (*pCb)(uint cni, const char * pPath, sint mtime) )
{
#ifndef WIN32
   DIR    *dir;
   struct dirent *entry;
   struct stat st;
   char   *pFilePath;
   uint   extlen, flen;
   XMLTV_DTD_VERSION dtd;
   FILE * fp;

   if ((pDirPath != NULL) && (pExtension != NULL) && (pCb != NULL))
   {
      extlen = strlen(pExtension);

      dir = opendir(pDirPath);
      if (dir != NULL)
      {
         while ((entry = readdir(dir)) != NULL)
         {
            flen = strlen(entry->d_name);
            if ( (extlen == 0) ||
                 ( (flen > extlen) &&
                   (strcmp(entry->d_name + flen - strlen(pExtension), pExtension) == 0) ))
            {
               pFilePath = xmalloc(strlen(pDirPath) + 1 + flen + 1);
               sprintf(pFilePath, "%s" PATH_SEPARATOR_STR "%s", pDirPath, entry->d_name);

               dprintf1("Xmltv-ScanDir: scanning %s\n", pFilePath);

               // get file status
               if (lstat(pFilePath, &st) == 0)
               {
                  // check if it's a regular file; reject directories, devices, pipes etc.
                  if (S_ISREG(st.st_mode))
                  {
                     fp = fopen(pFilePath, "r");
                     if (fp != NULL)
                     {
                        XmltvTags_StartScan(fp, XMLTV_DTD_UNKNOWN);
                        dtd = XmltvTags_QueryVersion(NULL);
                        if (dtd != XMLTV_DTD_UNKNOWN)
                        {
                           pCb(XmltvCni_MapProvider(pFilePath), pFilePath, st.st_mtime);
                        }
                        else
                           dprintf1("Xmltv-ScanDir: not XMLTV: %s (skipped)\n", pFilePath);

                        fclose(fp);
                     }
                     else
                        dprintf2("Xmltv-ScanDir: failed to open file: %s: %s\n", pFilePath, strerror(errno));
                  }
                  else
                     dprintf1("Xmltv-ScanDir: not a regular file: %s (skipped)\n", pFilePath);
               }
               else
                  dprintf2("Xmltv-ScanDir: failed stat %s: %s\n", pFilePath, strerror(errno));

               xfree(pFilePath);
            }
         }
         closedir(dir);
      }
      else
         fprintf(stderr, "failed to open XML directory %s: %s\n", pDirPath, strerror(errno));
   }
   else
      fatal0("Xmltv-ScanDir: illegal NULL ptr param");

#else // WIN32
   HANDLE hFind;
   WIN32_FIND_DATA finddata;
   SYSTEMTIME  systime;
   uchar *pDirPattern;
   struct tm tm;
   bool bMore;
   uint flen;
   char *pFilePath;
   XMLTV_DTD_VERSION dtd;
   FILE * fp;

   if ((pDirPath != NULL) && (pExtension != NULL) && (pCb != NULL))
   {
      pDirPattern = xmalloc(strlen(pDirPath) + 2 + strlen(pExtension) + 1);
      sprintf(pDirPattern, "%s\\*%s", pDirPath, pExtension);

      hFind = FindFirstFile(pDirPattern, &finddata);
      bMore = (hFind != (HANDLE) -1);
      while (bMore)
      {
         flen = strlen(finddata.cFileName);
         pFilePath = xmalloc(strlen(pDirPath) + 1 + flen + 1);
         sprintf(pFilePath, "%s" PATH_SEPARATOR_STR "%s", pDirPath, finddata.cFileName);

         fp = fopen(pFilePath, "r");
         if (fp != NULL)
         {
            XmltvTags_StartScan(fp, XMLTV_DTD_UNKNOWN);
            dtd = XmltvTags_QueryVersion(NULL);
            if (dtd != XMLTV_DTD_UNKNOWN)
            {
               FileTimeToSystemTime(&finddata.ftLastWriteTime, &systime);
               dprintf7("DB %s:  %02d:%02d:%02d - %02d.%02d.%04d\n", finddata.cFileName, systime.wHour, systime.wMinute, systime.wSecond, systime.wDay, systime.wMonth, systime.wYear);
               memset(&tm, 0, sizeof(tm));
               tm.tm_sec   = systime.wSecond;
               tm.tm_min   = systime.wMinute;
               tm.tm_hour  = systime.wHour;
               tm.tm_mday  = systime.wDay;
               tm.tm_mon   = systime.wMonth - 1;
               tm.tm_year  = systime.wYear - 1900;

               pCb(XmltvCni_MapProvider(pFilePath), pFilePath, mktime(&tm));
            }
            else
               dprintf1("Xmltv-ScanDir: not XMLTV: %s (skipped)\n", pFilePath);

            fclose(fp);
         }
         else
            dprintf2("Xmltv-ScanDir: failed to open file: %s: %s\n", pFilePath, strerror(errno));

         xfree(pFilePath);
         bMore = FindNextFile(hFind, &finddata);
      }
      FindClose(hFind);
      xfree(pDirPattern);
   }
#endif
}

// ----------------------------------------------------------------------------
#if 0
#include "epgdb/epgdbmgmt.h"
#include "epgctl/epgctxctl.h"
#include "epgui/uictrl.h"
#include "epgui/dumpraw.h"
void UiControlMsg_NewProvFreq( uint cni, uint freq ) { }
uint UiControlMsg_QueryProvFreq( uint cni ) { return 0; }
void UiControlMsg_ReloadError( uint cni, EPGDB_RELOAD_RESULT dberr, CONTEXT_RELOAD_ERR_HAND errHand, bool isNewDb ) { }
void UiControlMsg_MissingTunerFreq( uint cni ) { }
void UiControlMsg_NetAcqError( void ) { }
void UiControlMsg_AcqPassive( void ) { }
void UiControlMsg_AcqEvent( ACQ_EVENT acqEvent ) { }
bool UiControlMsg_AcqQueueOverflow( bool prepare ) { return FALSE; }
void EpgDumpRaw_IncomingBlock( const EPGDB_BLOCK_UNION * pUnion, BLOCK_TYPE type, uchar stream ) {}
void EpgDumpRaw_IncomingUnknown( BLOCK_TYPE type, uint size, uchar stream ) {}
EPGDB_CONTEXT * pUiDbContext;

extern int yydebug;

int main( int argc, char ** argv )
{
   EPGDB_CONTEXT * pDbContext;
   XMLTV_DTD_VERSION dtd;
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
         XmltvTags_StartScan(fp, XMLTV_DTD_UNKNOWN);
         dtd = XmltvTags_QueryVersion(&detection);
         if (dtd != XMLTV_DTD_UNKNOWN)
         {
            fseek(fp, 0, SEEK_SET);

            pDbContext = Xmltv_Load(fp, dtd, 0xfe, "XMLTV", FALSE);

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

