/*
 *  Retrieve channel table and TV card config from TV app INI files
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
 *    This module reads TV programme tables of supported TV applications
 *    (see the enum below for a list of applications.) Despite the name
 *    of the module, it handles both UNIX and WIN apps.
 *
 *    The module is used to extract TV channel lists for the EPG scan and
 *    a network name list for the network name configuration dialog, among
 *    other things.  The module is also used by the daemon, hence the
 *    Tcl/Tk interface functions are in a separate module, called wintvui.c
 *
 *  Author: Tom Zoerner
 *
 *    Some parser code fragments have been extracted from TV applications,
 *    so their respective copyright applies too. Please see the notes in
 *    functions headers below.
 *
 *  $Id: wintvcfg.c,v 1.29 2008/01/12 18:45:01 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#ifdef WIN32
#include <windows.h>
#else
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/tvchan.h"
#include "epgvbi/syserrmsg.h"
#ifdef WIN32
#include "dsdrv/tvcard.h"
#include "epgvbi/winshm.h"
#include "dsdrv/wintuner.h"
#endif
#include "epgui/rcfile.h"
#include "epgui/epgmain.h"
#include "epgui/wintvcfg.h"


// ----------------------------------------------------------------------------
// Structure which is filled with a TV channel table read from a config file
// - dynamically growing if more channels than fit the default max. table size
// - channel names are concatenated (zero separated) into one buffer
//
// default allocation size for frequency table
#define CHAN_CLUSTER_SIZE  50

typedef struct
{
   uint         freq;
   uint         strOff;
   uint         sortIdx;
} TV_CHNTAB_ITEM;

typedef struct
{
   TV_CHNTAB_ITEM * pData;
   char           * pStrBuf;
   uint      maxItemCount;
   uint      itemCount;
   uint      strBufSize;
   uint      strBufOff;
} TV_CHNTAB_BUF;

#define CHNTAB_MISSING_FREQ    (~0u)
#define CHNTAB_MISSING_NAME    (~0u)

// ----------------------------------------------------------------------------
// File names to load config files of supported TV applications
//
#define REG_KEY_MORETV  "Software\\Feuerstein\\MoreTV"
#define REG_KEY_FREETV  "Software\\Free Software\\FreeTV"
#define REG_KEY_FREETV_CHANTAB  "Software\\Free Software\\FreeTV\\Tuner\\Stations"

#define KTV2_INI_FILENAME         "K!TV.ini"
#define KTV2_CHNTAB_PATH_KEYWORD  "NEXTVIEW_EPG_PATH_CHANNEL"

#define TVTIME_INI_FILENAME       "tvtime.xml"

typedef struct
{
   const char  * pName;
   bool          needPath;
   bool       (* pGetChanTab) ( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg );
   const char  * pChanTabFile;
} TVAPP_LIST;

// forward declaration;
static const TVAPP_LIST tvAppList[TVAPP_COUNT];

// ----------------------------------------------------------------------------
// Check if a TV app is configured
//
uint WintvCfg_GetAppIdx( void )
{
   int appIdx;
   uint result;

#ifdef WIN32
   appIdx = RcFile_Query()->tvapp.tvapp_win;
#else
   appIdx = RcFile_Query()->tvapp.tvapp_unix;
#endif

   if (appIdx < TVAPP_COUNT)
   {
      result = appIdx;
   }
   else
   {
      debug1("WintvCfg-Check: invalid TV app index %d in rc/INI file", appIdx);
      result = TVAPP_NONE;
   }
   return result;
}

// ----------------------------------------------------------------------------
// External interface to check if TV app is configured
//
bool WintvCfg_IsEnabled( void )
{
   return (WintvCfg_GetAppIdx() != TVAPP_NONE);
}


#ifdef WIN32
// ----------------------------------------------------------------------------
// Assemble path from K!TV 2.3+ configuration file
// - returns NULL pointer if ini file not found or doesn't contain path to channel tab
// - string must be freed by caller!
//
static char * WintvCfg_GetKtv2ChanTabPath( const char * pBase, const char * pFileName )
{
   char * pPath = NULL;
   char   sbuf[256] = "\0";
   char * eol_ptr = NULL;
   FILE * fp;
   bool   found = FALSE;

   if (pFileName != NULL)
   {
      if ((pBase != NULL) && (*pBase != 0))
      {
         pPath = xmalloc(strlen(pBase) + strlen(pFileName) + 2);
         strcpy(pPath, pBase);
         strcat(pPath, "/");
         strcat(pPath, pFileName);

         fp = fopen(pPath, "r");
         if (fp != NULL)
         {
            while (feof(fp) == FALSE)
            {
               sbuf[0] = '\0';
               fgets(sbuf, 255, fp);

               eol_ptr = strstr(sbuf, "\n");
               if (eol_ptr != NULL)
                  *eol_ptr = '\0';

               if (strnicmp(sbuf, KTV2_CHNTAB_PATH_KEYWORD "=", 26) == 0)
               {
                  found = TRUE;
                  break;
               }
            }
            fclose(fp);
         }
         xfree((void *)pPath);
         pPath = NULL;
      }
      else
         debug0("WintvCfg-GetKtv2ChanTabPath: no path defined for TV app");

      if (found)
      {
         pPath = xmalloc(strlen(pBase) + strlen(sbuf) - 26 + 2);
         strcpy(pPath, pBase);
         strcat(pPath, "/");
         strcat(pPath, sbuf + 26);
      }
   }
   else
      fatal0("WintvCfg-GetKtv2ChanTabPath: illegal NULL ptr param");

   return pPath;
}
#endif

// ----------------------------------------------------------------------------
// Assemble path to the TV app configuration file
// - the returned string must be freed by the caller!
//
char * WintvCfg_GetRcPath( const char * pBase, uint appIdx )
{
   char * pPath = NULL;
   const char * pFileName;

   if ((appIdx != TVAPP_NONE) && (appIdx < TVAPP_COUNT))
   {
      // on UNIX config files are usually located in the home directory
      if ((pBase == NULL) || (*pBase == 0))
         pBase = getenv("HOME");

      if ((pBase != NULL) && (*pBase != 0))
      {
#ifdef WIN32
         // special case K!TV: the station list file name is read from the main INI file
         if (appIdx == TVAPP_KTV)
         {
            pPath = WintvCfg_GetKtv2ChanTabPath(pBase, KTV2_INI_FILENAME);
         }
         // fall back to K!TV v1 if no channel tab found for path2
         if (pPath == NULL)
#endif
         {
            pFileName = tvAppList[appIdx].pChanTabFile;

            pPath = xmalloc(strlen(pBase) + strlen(pFileName) + 2);
            strcpy(pPath, pBase);
            strcat(pPath, "/");
            strcat(pPath, pFileName);
         }
      }
      else
         debug0("WintvCfg-GetPath: no path defined for TV app");
   }

   return pPath;
}

// ----------------------------------------------------------------------------
// Initialize a channel table buffer
//
static void WintvCfg_ChanTabInit( TV_CHNTAB_BUF * pChanTab )
{
   memset(pChanTab, 0, sizeof(*pChanTab));
}

// ----------------------------------------------------------------------------
// Release memory allocated in a channel table buffer
//
static void WintvCfg_ChanTabDestroy( TV_CHNTAB_BUF * pChanTab )
{
   if (pChanTab->pData != NULL)
      xfree(pChanTab->pData);

   if (pChanTab->pStrBuf != NULL)
      xfree(pChanTab->pStrBuf);
}

// ----------------------------------------------------------------------------
// Open a new item in the channel table buffer
//
static void WintvCfg_ChanTabItemOpen( TV_CHNTAB_BUF * pChanTab )
{
   TV_CHNTAB_ITEM * pOldData;

   assert(pChanTab->itemCount <= pChanTab->maxItemCount);

   // grow the table if necessary
   if (pChanTab->itemCount == pChanTab->maxItemCount)
   {
      pOldData = pChanTab->pData;
      pChanTab->maxItemCount += CHAN_CLUSTER_SIZE;
      pChanTab->pData = xmalloc(pChanTab->maxItemCount * sizeof(TV_CHNTAB_ITEM));

      if (pOldData != NULL)
      {
         memcpy(pChanTab->pData, pOldData, pChanTab->itemCount * sizeof(TV_CHNTAB_ITEM));
         xfree(pOldData);
      }
   }

   // initialize the new item
   pChanTab->pData[pChanTab->itemCount].freq   = CHNTAB_MISSING_FREQ;
   pChanTab->pData[pChanTab->itemCount].strOff = CHNTAB_MISSING_NAME;
   pChanTab->pData[pChanTab->itemCount].sortIdx = pChanTab->itemCount;
}

#ifndef WIN32 // currently not used for WIN32 TV apps - only tp avoid compiler warning
// ----------------------------------------------------------------------------
// Append a sorting index to the current item in the channel buffer
//
static void WintvCfg_ChanTabAddSortIdx( TV_CHNTAB_BUF * pChanTab, uint sortIdx )
{
   assert(pChanTab->itemCount < pChanTab->maxItemCount);

   pChanTab->pData[pChanTab->itemCount].sortIdx = sortIdx;
}
#endif // WIN32

// ----------------------------------------------------------------------------
// Append a frequency value to the current item in the channel buffer
//
static void WintvCfg_ChanTabAddFreq( TV_CHNTAB_BUF * pChanTab, uint freq )
{
   // open call must precede addition, hence there must be at least one free slot left
   assert(pChanTab->itemCount < pChanTab->maxItemCount);
   assert(pChanTab->pData[pChanTab->itemCount].freq == CHNTAB_MISSING_FREQ);

   if ((freq != CHNTAB_MISSING_FREQ) && (freq != 0))
   {
      pChanTab->pData[pChanTab->itemCount].freq = freq;
   }
}

// ----------------------------------------------------------------------------
// Append a channel name to the current item in the channel buffer
//
static void WintvCfg_ChanTabAddName( TV_CHNTAB_BUF * pChanTab, const char * pName )
{
   const char *pe;
   char * newbuf;
   uint len;

   // open call must precede addition, hence there must be at least one free slot left
   assert(pChanTab->itemCount < pChanTab->maxItemCount);

   if (pChanTab->pData[pChanTab->itemCount].strOff == CHNTAB_MISSING_NAME)
   {
      // skip any spaces at the start of the name
      while ((*pName == ' ') || (*pName == '\t') )
         pName += 1;

      // chop any spaces at the end of the name
      if (*pName != 0)
      {
         pe = pName + strlen(pName) - 1;
         while ( (pe > pName) &&
                 ((*pe == ' ') || (*pe == '\t')) )
         {
            pe--;
         }
         len = (pe + 1 - pName);
      }
      else
         len = 0;

      // grow string buffer if necessary
      if (pChanTab->strBufOff + len + 1 >= pChanTab->strBufSize)
      {
         newbuf = xmalloc(pChanTab->strBufSize + len + 2048);
         if (pChanTab->pStrBuf != NULL)
         {
            memcpy(newbuf, pChanTab->pStrBuf, pChanTab->strBufSize);
            xfree(pChanTab->pStrBuf);
         }
         pChanTab->pStrBuf = newbuf;
         pChanTab->strBufSize  += len + 2048;
      }

      // append string to buffer
      memcpy(pChanTab->pStrBuf + pChanTab->strBufOff, pName, len);
      pChanTab->pStrBuf[pChanTab->strBufOff + len] = 0;
      pChanTab->pData[pChanTab->itemCount].strOff = pChanTab->strBufOff;
      pChanTab->strBufOff += len + 1;
   }
   else
      debug3("WintvCfg-ChanTabAddName: duplicate name for channel #%d '%s': '%s'\n", pChanTab->itemCount, pChanTab->pStrBuf + pChanTab->pData[pChanTab->itemCount].strOff, pName);
}

// ----------------------------------------------------------------------------
// Commit the data for the current channel table item
// - must be called after open and frequency/name additions
// - increments the channel counter
//
static void WintvCfg_ChanTabItemClose( TV_CHNTAB_BUF * pChanTab )
{
   if (pChanTab->pData != NULL)
   {
      assert(pChanTab->itemCount < pChanTab->maxItemCount);

      // discard the item if not both frequency and name were added
      if ( (pChanTab->pData[pChanTab->itemCount].freq   != CHNTAB_MISSING_FREQ) &&
           (pChanTab->pData[pChanTab->itemCount].strOff != CHNTAB_MISSING_NAME) )
      {
         // replace missing data with default values: invalid frequency / empty name
         if (pChanTab->pData[pChanTab->itemCount].freq   == CHNTAB_MISSING_FREQ)
         {
            WintvCfg_ChanTabAddFreq(pChanTab, 0);
         }
         if (pChanTab->pData[pChanTab->itemCount].strOff == CHNTAB_MISSING_NAME)
         {
            WintvCfg_ChanTabAddName(pChanTab, "");
         }

         pChanTab->itemCount += 1;
      }
      else
      {
         dprintf3("WintvCfg-ChanTabItemClose: skip item #%d: no freq (%d) or name (%d)\n", pChanTab->itemCount, pChanTab->pData[pChanTab->itemCount].freq, pChanTab->pData[pChanTab->itemCount].strOff);
         if (pChanTab->pData[pChanTab->itemCount].strOff != CHNTAB_MISSING_NAME)
         {
            pChanTab->strBufOff = pChanTab->pData[pChanTab->itemCount].strOff;
         }
      }
   }
}

#ifndef WIN32 // currently not used for WIN32 TV apps - only tp avoid compiler warning
// ---------------------------------------------------------------------------
// Sort the channel table
//
static int WintvCfg_ChanTabSortCompare( const void * va, const void * vb )
{
   const TV_CHNTAB_ITEM * pItem1 = va;
   const TV_CHNTAB_ITEM * pItem2 = vb;
   int result;

   if (pItem1->sortIdx < pItem2->sortIdx)
      result = -1;
   else if (pItem1->sortIdx == pItem2->sortIdx)
      result = 0;
   else
      result = 1;

   return result;
}

static void WintvCfg_ChanTabSort( TV_CHNTAB_BUF * pChanTab )
{
   char * pNewStrBuf;
   char * pDstStr;
   uint idx;

   if (pChanTab->itemCount > 0)
   {
      // sort frequency table by sort indices (ascending)
      qsort( pChanTab->pData, pChanTab->itemCount, sizeof(pChanTab->pData[0]),
             &WintvCfg_ChanTabSortCompare);

      // get name table in same order as frequency table
      pNewStrBuf = xmalloc(pChanTab->strBufSize);
      pDstStr = pNewStrBuf;
      for (idx = 0; idx < pChanTab->itemCount; idx++)
      {
         strcpy(pDstStr, pChanTab->pStrBuf + pChanTab->pData[idx].strOff);
         pDstStr += strlen(pDstStr) + 1;
      }
      xfree(pChanTab->pStrBuf);
      pChanTab->pStrBuf = pNewStrBuf;
      assert(pDstStr - pNewStrBuf == pChanTab->strBufOff);
   }
}
#endif // WIN32

#ifdef WIN32
// ---------------------------------------------------------------------------
// Read frequency table from MoreTV registry keys
//
static bool WintvCfg_GetMoretvChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   HKEY  hKey;
   DWORD dwSize, dwType;
   char  keyValStr[20];
   char  name_buf[100];
   DWORD freq;
   uint  idx;
   BOOL  result;

   if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY_MORETV, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
   {
      idx = 0;
      while (TRUE)
      {
         idx += 1;

         sprintf(keyValStr, "%02dFrequenz", idx);
         dwSize = sizeof(freq);
         if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, (char *) &freq, &dwSize) == ERROR_SUCCESS) &&
              (dwType == REG_DWORD) && (dwSize == sizeof(freq)) )
         {
            sprintf(keyValStr, "%02dSendername", idx);
            dwSize = sizeof(name_buf) - 1;
            if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, name_buf, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ) )
            {
               WintvCfg_ChanTabItemOpen(pChanTab);

               WintvCfg_ChanTabAddFreq(pChanTab, (freq * 4) / 25);

               if ((dwSize == 0) || (strcmp(name_buf, "---") == 0))
               {  // empty channel name - set to NULL string
                  name_buf[0] = 0;
               }
               WintvCfg_ChanTabAddName(pChanTab, name_buf);

               WintvCfg_ChanTabItemClose(pChanTab);
            }
            else
               break;
         }
         else
            break;
      }
      RegCloseKey(hKey);
      result = TRUE;
   }
   else
   {  // registry key not found -> warn the user
      if (ppErrMsg != NULL)
         SystemErrorMessage_Set(ppErrMsg, 0, 
                                "MoreTV channel table not found in the registry. "
                                "Sorry, you'll have to configure a different TV application.", NULL);
      result = FALSE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Read channel names from FreeTV registry keys
//
static bool WintvCfg_GetFreetvChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   HKEY  hKey;
   DWORD dwSize, dwType;
   char  keyValStr[20];
   char  name_buf[100];
   DWORD freq;
   uint  chanCount;
   uint  chanIdx;
   bool  result = FALSE;

   if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY_FREETV_CHANTAB, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
   {
      dwSize = sizeof(chanCount);
      if ( (RegQueryValueEx(hKey, "Count", 0, &dwType, (char *) &chanCount, &dwSize) == ERROR_SUCCESS) &&
           (dwType == REG_DWORD) && (dwSize == sizeof(chanCount)) )
      {
         for (chanIdx = 1; chanIdx <= chanCount; chanIdx++)
         {
            WintvCfg_ChanTabItemOpen(pChanTab);

            sprintf(keyValStr, "Frequency%d", chanIdx);

            dwSize = sizeof(freq);
            if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, (char *) &freq, &dwSize) == ERROR_SUCCESS) &&
                 (dwType == REG_DWORD) && (dwSize == sizeof(freq)) )
            {
               WintvCfg_ChanTabAddFreq(pChanTab, (freq * 2) / 125);
            }

            sprintf(keyValStr, "Name%d", chanIdx);

            dwSize = sizeof(name_buf) - 1;
            if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, name_buf, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ) )
            {
               if (dwSize == 0)
               {  // empty channel name - set to NULL string
                  name_buf[0] = 0;
               }
               WintvCfg_ChanTabAddName(pChanTab, name_buf);
            }

            WintvCfg_ChanTabItemClose(pChanTab);
         }
         result = TRUE;
      }
      RegCloseKey(hKey);
   }

   if (result == FALSE)
   {
      if (ppErrMsg != NULL)
         SystemErrorMessage_Set(ppErrMsg, 0, 
                                "FreeTV channel table not found in the registry. "
                                "Sorry, you'll have to configure a different TV application.", NULL);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Structures which hold the channel table in Multidec's channel table file
// - the structures changed between version 5 and 6
// - code taken from multidec 6.4 by echter_espresso@hotmail.com
//
struct PIDFilters
{
   char           FilterName[5];
   unsigned char  FilterId;
   unsigned short PID;
};

struct TProgrammAlt
{
   char  Name[30];
   char  Anbieter[30];
   char  Land[30];
   unsigned long freq;
   char  Typ;
   BOOL  Tuner_Auto;
   BOOL  PMT_Auto;
   BOOL  PID_Auto;
   int   power;
   int   volt;
   int   afc;
   int   ttk;
   int   diseqc;
   uint  srate;
   int   qam;
   int   fec;
   int   norm;
   unsigned short  tp_id;
   unsigned short  Video_pid;
   unsigned short  Audio_pid;
   unsigned short  TeleText_pid;
   unsigned short  PMT_pid;
   unsigned short  PCR_pid;
   unsigned short  PMC_pid;
   unsigned short  SID_pid;
   unsigned short  AC3_pid;
   unsigned short  EMM_pid;
   unsigned short  ECM_pid;
   unsigned char   TVType;
   unsigned char   ServiceTyp;
   unsigned char   CA_ID;
   unsigned short  Temp_Audio;
   unsigned char   Buffer[10];
   unsigned short  Filteranzahl;
   struct PIDFilters Filters[12];
};

struct TCA_System
{
   unsigned short CA_Typ;
   unsigned short ECM;
   unsigned short EMM;
};

struct TProgramm
{
   char              Name[30];
   char              Anbieter[30];
   char              Land[30];
   unsigned long     freq;
   unsigned char     Typ;
   unsigned char     volt;
   unsigned char     afc;
   unsigned char     diseqc;
   unsigned int      srate;
   unsigned char     qam;
   unsigned char     fec;
   unsigned char     norm;
   unsigned short    tp_id;
   unsigned short    Video_pid;
   unsigned short    Audio_pid;
   unsigned short    TeleText_pid;
   unsigned short    PMT_pid;
   unsigned short    PCR_pid;
   unsigned short    PMC_pid;
   unsigned short    SID_pid;
   unsigned short    AC3_pid;
   unsigned char     TVType;
   unsigned char     ServiceTyp;
   unsigned char     CA_ID;
   unsigned short    Temp_Audio;
   unsigned short    Filteranzahl;
   struct PIDFilters Filters[12];
   unsigned short    CA_Anzahl;
   struct TCA_System CA_System[6];
   char    CA_Land[5];
   unsigned char Merker;
};

// ----------------------------------------------------------------------------
// Extract all channels from Multidec channel table file
// - code taken from multidec 6.4 by echter_espresso@hotmail.com
//
static bool WintvCfg_GetMultidecChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   struct TProgrammAlt ProgrammAlt;
   struct TProgramm    Programm;
   size_t len;
   int  fd;
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      fd = open(pChanTabPath, _O_RDONLY | _O_BINARY);
      if (fd != -1)
      {
         len = read(fd, &Programm, sizeof(Programm));
         if (len == sizeof(Programm))
         {
            if (strcmp(Programm.Name, "MultiDec 6.1") == 0)
            {
               while (read(fd, &Programm, sizeof(Programm)) == sizeof(Programm))
               {
                  WintvCfg_ChanTabItemOpen(pChanTab);
                  WintvCfg_ChanTabAddFreq(pChanTab, Programm.freq * 2 / 125);

                  Programm.Name[sizeof(Programm.Name) - 1] = 0;
                  WintvCfg_ChanTabAddName(pChanTab, Programm.Name);
                  WintvCfg_ChanTabItemClose(pChanTab);
               }
            }
            else
            {  // old version -> restart at file offset 0
               _lseek(fd, 0, SEEK_SET);
               while (read(fd, &ProgrammAlt, sizeof(ProgrammAlt)) == sizeof(ProgrammAlt))
               {
                  WintvCfg_ChanTabItemOpen(pChanTab);
                  WintvCfg_ChanTabAddFreq(pChanTab, ProgrammAlt.freq * 2 / 125);

                  Programm.Name[sizeof(Programm.Name) - 1] = 0;
                  WintvCfg_ChanTabAddName(pChanTab, Programm.Name);
                  WintvCfg_ChanTabItemClose(pChanTab);
               }
            }
         }
         close(fd);

         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         if (ppErrMsg != NULL)
            SystemErrorMessage_Set(ppErrMsg, errno, 
                                   "Check your settings in the TV interaction configuration dialog. "
                                   "Could not open the MultiDec channel table \"", pChanTabPath, "\": ", NULL);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the K!TV channel table
// - Parser code originally derived from QuenotteTV_XP_1103_Sources.zip; updated for 2.3
//   K!TV is Copyright (c) 2000-2005 Quenotte
//
static bool WintvCfg_GetKtvChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   char   sbuf[256];
   char   value[100];
   FILE * fp;
   long   freq;
   bool   currentSkipped;
   bool   isOpen;
   bool   result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         currentSkipped = FALSE;
         isOpen = FALSE;

         while (fgets(sbuf, sizeof(sbuf) - 1, fp) != NULL)
         {
            // K!TV version < 2
            if (sscanf(sbuf, "Freq: %ld", &freq) == 1)
            {
               if (isOpen)
               {
                  WintvCfg_ChanTabAddFreq(pChanTab, freq * 2 / 125);
               }
            }
            // K!TV version 2
            else if (sscanf(sbuf, "frequency %*[=:] %ld", &freq) >= 1)
            {
               if (isOpen)
               {
                  freq /= 1000;
                  WintvCfg_ChanTabAddFreq(pChanTab, freq * 2 / 125);
               }
            }
            else if (sscanf(sbuf, "name %*[=:] %99[^\n]", value) >= 1)
            {
               if (isOpen)
                  WintvCfg_ChanTabItemClose(pChanTab);

               if ( (currentSkipped == FALSE) && (strcasecmp(value, "Current") == 0) )
               {
                  currentSkipped = TRUE;
                  isOpen = FALSE;
               }
               else
               {
                  WintvCfg_ChanTabItemOpen(pChanTab);
                  WintvCfg_ChanTabAddName(pChanTab, value);
                  isOpen = TRUE;
               }
            }
         }
         fclose(fp);

         if (isOpen)
            WintvCfg_ChanTabItemClose(pChanTab);

         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         if (ppErrMsg != NULL)
            SystemErrorMessage_Set(ppErrMsg, errno, 
                                   "Check your settings in the TV interaction configuration dialog. "
                                   "Could not open the K!TV channel table \"", pChanTabPath, "\": ", NULL);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Extract all channels from DScaler "program.txt" channel table
// - Parser code taken from DScaler 3.10  (see http://dscaler.org/)
//   Copyright: 9 Novemeber 2000 - Michael Eskin, Conexant Systems
// - List is a simple text file with the following format:
//   Name <display_name>
//   Freq <frequency_KHz>
//   Name <display_name>
//   Freq <frequency_KHz>
//   ...
//
static bool WintvCfg_GetDscalerChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   char sbuf[256];
   uint   freq;
   FILE * fp;
   char * eol_ptr;
   bool isOpen;
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         isOpen = FALSE;
         while (feof(fp) == FALSE)
         {
            sbuf[0] = '\0';

            fgets(sbuf, 255, fp);

            eol_ptr = strstr(sbuf, ";");
            if (eol_ptr == NULL)
               eol_ptr = strstr(sbuf, "\n");
            if (eol_ptr != NULL)
               *eol_ptr = '\0';

            if (strnicmp(sbuf, "Name:", 5) == 0)
            {
               if (isOpen)
                  WintvCfg_ChanTabItemClose(pChanTab);
               WintvCfg_ChanTabItemOpen(pChanTab);
               WintvCfg_ChanTabAddName(pChanTab, sbuf + 5);
               isOpen = TRUE;
            }
            else if (isOpen)
            {
               // cope with old style frequencies
               if (strnicmp(sbuf, "Freq:", 5) == 0)
               {
                  freq = (uint) atol(sbuf + 5);
                  freq = (freq * 16) / 1000;  // MulDiv WTF!?
                  WintvCfg_ChanTabAddFreq(pChanTab, freq);
               }
               else if (strnicmp(sbuf, "Freq2:", 6) == 0)
               {
                  freq = atol(sbuf + 6);
                  WintvCfg_ChanTabAddFreq(pChanTab, freq);
               }
            }
         }
         fclose(fp);

         if (isOpen)
            WintvCfg_ChanTabItemClose(pChanTab);

         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         if (ppErrMsg != NULL)
            SystemErrorMessage_Set(ppErrMsg, errno, 
                                   "Check your settings in the TV interaction configuration dialog. "
                                   "Could not open the DScaler channel table \"", pChanTabPath, "\": ", NULL);
      }
   }
   return result;
}

#else  // not WIN32

// ----------------------------------------------------------------------------
// Extract all TV frequencies & norms from $HOME/.xawtv for the EPG scan
// - the parsing pattern used here is lifted directly from xawtv-3.41
// - station names are defined on single lines in brackets, e.g. [ARD]
//
static bool WintvCfg_GetXawtvChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   char line[256], tag[64], value[192];
   uint defaultNorm, attrNorm;
   sint defaultFine, attrFine;
   bool defaultIsTvInput, isTvInput;
   bool isOpen;
   uint freq;
   FILE * fp;
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         isOpen = FALSE;
         defaultNorm = attrNorm = VIDEO_MODE_PAL;
         defaultFine = attrFine = 0;
         defaultIsTvInput = isTvInput = TRUE;
         freq = 0;

         // read all lines of the file (ignoring section structure)
         while (fgets(line, 255, fp) != NULL)
         {
            if (line[0] == '\n' || line[0] == '#' || line[0] == '%')
               continue;

            // search for section headers, i.e. "[station]"
            if (sscanf(line,"[%99[^]]]", value) == 1)
            {
               // add the last section's data to the output list
               if (isOpen)
               {
                  if ((freq != 0) && isTvInput)
                  {
                     dprintf4("Xawtv channel: freq=%d fine=%d norm=%d, isTvInput=%d\n", freq, attrFine, attrNorm, isTvInput);
                     WintvCfg_ChanTabAddFreq(pChanTab, (attrNorm << 24) | (freq + attrFine));
                  }
                  WintvCfg_ChanTabItemClose(pChanTab);
               }

               // initialize variables for the new section
               freq = 0;
               attrFine = defaultFine;
               attrNorm = defaultNorm;
               isTvInput = defaultIsTvInput;
               dprintf1("Xawtv channel: name=%s\n", value);

               if ( (strcmp(value, "global") != 0) &&
                    (strcmp(value, "defaults") != 0) &&
                    (strcmp(value, "launch") != 0) )
               {
                  WintvCfg_ChanTabItemOpen(pChanTab);
                  WintvCfg_ChanTabAddName(pChanTab, value);
                  isOpen = TRUE;
               }
               else
                  isOpen = FALSE;
            }
            else if (sscanf(line," %63[^= ] = %191[^\n]", tag, value) == 2)
            {
               // search for channel assignment lines, e.g. " channel = SE15"
               if (strcasecmp(tag, "channel") == 0)
               {
                  freq = TvChannels_NameToFreq(value);
               }
               else if (strcasecmp(tag, "freq") == 0)
               {
                  freq = 16 * atof(value);
               }
               else if (strcasecmp(tag, "norm") == 0)
               {
                  if (strncasecmp(value, "pal", 3) == 0)
                     attrNorm = VIDEO_MODE_PAL;
                  else if (strncasecmp(value, "secam", 5) == 0)
                     attrNorm = VIDEO_MODE_SECAM;
                  else if (strncasecmp(value, "ntsc", 4) == 0)
                     attrNorm = VIDEO_MODE_NTSC;
                  else
                     debug1("Xawtv-GetFreqTab: unknown norm '%s' in .xawtvrc", value);
                  if (isOpen == FALSE)
                     defaultNorm = attrNorm;
               }
               else if (strcasecmp(tag, "fine") == 0)
               {
                  attrFine = atoi(value);
                  if (isOpen == FALSE)
                     defaultFine = attrFine;
               }
               else if (strcasecmp(tag, "input") == 0)
               {
                  isTvInput = (strcasecmp(value, "television") == 0);
                  if (isOpen == FALSE)
                     defaultIsTvInput = isTvInput;
               }
            }
            else
               debug1("Xawtv-GetFreqTab: parse error line \"%s\"", line);
         }
         fclose(fp);

         // add the last section's data to the output list
         if (isOpen)
         {
            if ((freq != 0) && isTvInput)
            {
               dprintf4("Xawtv channel: freq=%d fine=%d norm=%d, isTvInput=%d\n", freq, attrFine, attrNorm, isTvInput);
               WintvCfg_ChanTabAddFreq(pChanTab, (attrNorm << 24) | (freq + attrFine));
            }
            WintvCfg_ChanTabItemClose(pChanTab);
         }

         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         if (ppErrMsg != NULL)
            SystemErrorMessage_Set(ppErrMsg, errno, 
                                   "Check your settings in the TV interaction configuration dialog. "
                                   "Could not open channel table \"", pChanTabPath, "\": ", NULL);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Parse zapping config for channel names or frequencies
// - used both to parse for frequencies as well as for names: is freq buffer
//   pointer is NULL, names scan is assumed
//
static void WintvCfg_ParseZappingConf( FILE * fp, TV_CHNTAB_BUF * pChanTab )
{
   char line[256 + 1], tag[64];
   char * endp;
   uint subTreeLevel;
   sint len;
   long freq;
   long norm;

   subTreeLevel = 0;
   freq = 0;
   norm = 0;

   // read all lines of the file (ignoring section structure)
   while (fgets(line, sizeof(line)-1, fp) != NULL)
   {
      // skip until <subtree label="tuned_channels" type="directory">
      if ( (subTreeLevel == 0) &&
           (sscanf(line," < subtree label = \"%63[^\"]\" %*[^>]>%n", tag, &len) == 1) &&
           (strcasecmp(tag, "tuned_channels") == 0) )
      {
         subTreeLevel = 1;
      }
      else if ( (subTreeLevel == 1) &&
                (sscanf(line," < subtree label = \"%63[^\"]\" %*[^>]>%n", tag, &len) == 1) )
      {
         subTreeLevel += 1;
         freq = 0;
         norm = VIDEO_MODE_PAL;
         WintvCfg_ChanTabItemOpen(pChanTab);
      }
      else if ( (subTreeLevel == 2) &&
                (sscanf(line," < subtree label = \"%63[^\"]\" %*[^>]>%n", tag, &len) == 1) &&
                (strcasecmp(tag, "name") == 0) )
      {
         endp = strchr(line + len, '<');
         if (endp != NULL)
         {
            *endp = 0;
            dprintf1("Zapping channel name '%s'\n", line + len);
            WintvCfg_ChanTabAddName(pChanTab, line + len);
         }
      }
      else if ( (subTreeLevel == 2) &&
                (sscanf(line," < subtree label = \"%63[^\"]\" %*[^>]>%n", tag, &len) == 1) &&
                (strcasecmp(tag, "standard") == 0) )
      {
         norm = strtol(line + len, &endp, 10);
      }
      else if ( (subTreeLevel == 2) &&
                (sscanf(line," < subtree label = \"%63[^\"]\" %*[^>]>%n", tag, &len) == 1) &&
                (strcasecmp(tag, "freq") == 0) )
      {
         freq = strtol(line + len, &endp, 10);
      }
      else if ( (subTreeLevel >= 2) &&
                (sscanf(line," < subtree label = \"%*63[^\"]\" type = \"%63[^\"]\" >%n", tag, &len) == 1) &&
                (strcasecmp(tag, "directory") == 0) )
      {
         // skip subtrees inside channel definition
         subTreeLevel += 1;
      }
      else if ( (subTreeLevel >= 1) &&
                (sscanf(line," < / %63[^ >] >%n", tag, &len) == 1) &&
                (strcasecmp(tag, "subtree") == 0) )
      {
         subTreeLevel -= 1;

         if ( (subTreeLevel == 1) && (freq != 0) )
         {
            dprintf2("Zapping channel: freq=%ld norm=%ld\n", freq, norm);
            WintvCfg_ChanTabAddFreq(pChanTab, (norm << 24) | (freq * 16/1000));
            WintvCfg_ChanTabItemClose(pChanTab);
         }
         else if (subTreeLevel == 0)
         {
            break;
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Extract all TV frequencies & norms from zapping.conf for the EPG scan
//
static bool WintvCfg_GetZappingChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   FILE * fp;
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         WintvCfg_ParseZappingConf(fp, pChanTab);

         fclose(fp);
         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         if (ppErrMsg != NULL)
            SystemErrorMessage_Set(ppErrMsg, errno, 
                                   "Check your settings in the TV interaction configuration dialog. "
                                   "Could not open channel table \"", pChanTabPath, "\": ", NULL);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Map tvtime band and channel names to frequency value
// - frequency vales taken from tvchan.c
//
static uint WintvCfg_ParseTvtimeChannel( const char * band, const char * chan )
{
   uint tmp_freq;
   uint freq = 0;

   if (strcmp(band, "VHF E2-E12") == 0)
   {
      if (sscanf(chan, "E %d", &tmp_freq) == 1)
      {
         if ((tmp_freq >= 2) && (tmp_freq <= 4))
            freq = (int)(48.25 * 16) + ((tmp_freq - 2) * 7*16);
         else if ((tmp_freq >= 5) && (tmp_freq <= 12))
            freq = (int)(175.25 * 16) + ((tmp_freq - 5) * 7*16);
      }
   }
   else if (strcmp(band, "VHF S1-S41") == 0)
   {
      if (sscanf(chan, "S %d", &tmp_freq) == 1)
      {
         if ((tmp_freq >= 1) && (tmp_freq <= 10))
            freq = (int)(105.25 * 16) + ((tmp_freq - 1) * 7*16);
         else if ((tmp_freq >= 11) && (tmp_freq <= 20))
            freq = (int)(231.25 * 16) + ((tmp_freq - 11) * 7*16);
         else if ((tmp_freq >= 21) && (tmp_freq <= 41))
            freq = (int)(303.25 * 16) + ((tmp_freq - 21) * 7*16);
      }
   }
   else if (strcmp(band, "UHF") == 0)
   {
      if ((sscanf(chan, "U %d", &tmp_freq) == 1) &&
          (tmp_freq >= 21) && (tmp_freq <= 69))
      {
         freq = (int)(471.25 * 16) + ((tmp_freq - 21) * 8*16);
      }
   }
   // else: skip

   return freq;
}

// ----------------------------------------------------------------------------
// Parse TVTime station list for channel names and frequencies
//
static void WintvCfg_ParseTvtimeStations( FILE * fp, TV_CHNTAB_BUF * pChanTab, const char * pTableName )
{
   char line[512 + 1], tag[64];
   char * pAtt;
   uint subTreeLevel;
   sint len;
   uint defaultNorm;
   bool isActiveList;

   subTreeLevel = 0;
   isActiveList = FALSE;
   defaultNorm = VIDEO_MODE_PAL;

   // read all lines of the file (ignoring section structure)
   while (fgets(line, sizeof(line)-1, fp) != NULL)
   {
      // <list norm="NTSC" frequencies="us-cable" audio="bg">
      if ( (subTreeLevel == 0) &&
           (sscanf(line, " < stationlist %1[a-zA-Z]%n", tag, &len) == 1) )  // dummy string read
      {
         subTreeLevel = 1;
      }
      else if ( (subTreeLevel == 1) &&
                (sscanf(line, " < list %1[a-zA-Z]%n", tag, &len) == 1) )  // dummy string read
      {
         // check name of the frequency table
         if ( ((pAtt = strstr(line, "frequencies")) != NULL) &&
              (sscanf(pAtt, " frequencies = \"%63[^\"]\"]%n", tag, &len) == 1) )
         {
            if (strcmp(pTableName, tag) == 0)
            {
               if (sscanf(line, " norm = \"%63[^\"]\"]%n", tag, &len) == 1)
               {
                  if (strncmp(tag, "NTSC", 4) == 0)
                     defaultNorm = VIDEO_MODE_NTSC;
                  else if (strncmp(tag, "SECAM", 5) == 0)
                     defaultNorm = VIDEO_MODE_SECAM;
                  else
                     defaultNorm = VIDEO_MODE_PAL;
               }
               isActiveList = TRUE;
               dprintf2("WintvCfg-ParseTvtimeStations: start reading table '%s', default norm '%s'\n", pTableName, tag);
            }
            else
            {
               dprintf1("WintvCfg-ParseTvtimeStations: skipping deselected freq.table '%s'\n", tag);
               isActiveList = FALSE;
            }
            subTreeLevel = 2;
         }
         else
            debug1("WintvCfg-ParseTvtimeStations: XML parse error line '%s'", line);
      }
      else if ( (subTreeLevel == 2) &&
                (isActiveList) &&
                (sscanf(line, " < station %1[a-zA-Z]%n", tag, &len) == 1) )  // dummy string read
      {
         char band[64];
         char chan[64];
         int freq = 0;
         int norm = defaultNorm;
         int isActive = TRUE;
         int position = 0;
         int fine = 0;

         if ( ((pAtt = strstr(line, "position")) == NULL) ||
              (sscanf(pAtt, "position = \" %d \"%n", &position, &len) < 1) )
         {
            position = 0;
         }
         if ( ((pAtt = strstr(line, "active")) == NULL) ||
              (sscanf(pAtt, "active = \" %d \"%n", &isActive, &len) < 1) )
         {
            isActive = TRUE;
         }
         if ( ((pAtt = strstr(line, "finetune")) == NULL) ||
              (sscanf(pAtt, "finetune = \" %d \" %n", &fine, &len) < 1) )
         {
            fine = 0;
         }
         if ( ((pAtt = strstr(line, "norm")) == NULL) ||
              (sscanf(pAtt, "channel = \"%63[^\"]\"]%n", tag, &len) < 1) )
         {
            if (strncmp(tag, "NTSC", 4) == 0)
               norm = VIDEO_MODE_NTSC;
            else if (strncmp(tag, "SECAM", 5) == 0)
               norm = VIDEO_MODE_SECAM;
            else
               norm = VIDEO_MODE_PAL;
         }
         if ( ((pAtt = strstr(line, "band")) == NULL) ||
              (sscanf(pAtt, "band = \"%63[^\"]\"]%n", band, &len) < 1) )
         {
            band[0] = 0;
         }
         if ( ((pAtt = strstr(line, "channel")) == NULL) ||
              (sscanf(pAtt, "channel = \"%63[^\"]\"]%n", chan, &len) < 1) )
         {
            chan[0] = 0;
         }
         if ((band[0] != 0) && (chan[0] != 0))
         {
            freq = WintvCfg_ParseTvtimeChannel(band, chan);
         }
         if ( ((pAtt = strstr(line, "name")) == NULL) ||
              (sscanf(pAtt, "name = \"%63[^\"]\"]%n", tag, &len) < 1) )
         {
            tag[0] = 0;
         }
         dprintf5("WintvCfg-ParseTvtimeStations: '%s' active:%d pos:%d freq:%d norm:%d\n", tag, isActive, position, freq, norm);
         if (isActive && (freq != 0))
         {
            WintvCfg_ChanTabItemOpen(pChanTab);
            WintvCfg_ChanTabAddName(pChanTab, tag);
            WintvCfg_ChanTabAddFreq(pChanTab, (norm << 24) | (freq + fine));
            WintvCfg_ChanTabAddSortIdx(pChanTab, position);
            WintvCfg_ChanTabItemClose(pChanTab);
         }
      }
      else if ( (subTreeLevel == 2) &&
                (sscanf(line," < / %63[^ >] >%n", tag, &len) == 1) &&
                (strcmp(tag, "list") == 0) )
      {
         subTreeLevel = 1;
         if (isActiveList)
            break;
      }
      else if ( (subTreeLevel == 1) &&
                (sscanf(line," < / %63[^ >] >%n", tag, &len) == 1) &&
                (strcmp(tag, "stationlist") == 0) )
      {
         subTreeLevel = 0;
      }
   }
}

// ----------------------------------------------------------------------------
// Read name of the selected frequency table from the tvtime main config file
//
static char * WintvCfg_ParseTvtimeConfig( FILE * fp )
{
   char line[256 + 1], tag[64];
   char * pFreqTab;
   uint subTreeLevel;
   sint len;

   subTreeLevel = 0;
   pFreqTab = NULL;

   // read all lines of the file (ignoring section structure)
   while (fgets(line, sizeof(line)-1, fp) != NULL)
   {
      if ( (subTreeLevel == 0) &&
           (sscanf(line, " < tvtime %1[a-zA-Z]%n", tag, &len) == 1) )  // dummy string read
      {
         subTreeLevel = 1;
      }
      else if ( (subTreeLevel == 1) &&
                (sscanf(line, " < option name = \"Frequencies\" value = \"%63[^\"]\"]\"", tag) == 1) )  // dummy string read
      {
         dprintf1("WintvCfg-ParseTvtimeConfig: selected table '%s'\n", tag);
         pFreqTab = xstrdup(tag);
         break;
      }
   }
   return pFreqTab;
}

// ----------------------------------------------------------------------------
// Extract all TV frequencies & norms from tvtime config files
//
static bool WintvCfg_GetTvtimeChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   FILE * fp;
   char * pBase;
   char * pMainRc;
   char * pFreqTab;
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      // derive path to main config file from path to station list: replace file name (keep dir)
      pMainRc = xmalloc(strlen(pChanTabPath) + strlen(TVTIME_INI_FILENAME));
      pBase = strrchr(pChanTabPath, '/');
      if (pBase != NULL)
      {
         pBase += 1;
         strncpy(pMainRc, pChanTabPath, (pBase - pChanTabPath));
         strcpy(pMainRc + (pBase - pChanTabPath), TVTIME_INI_FILENAME);
      }
      else  // should never happen since TVTIME_INI_FILENAME includes a slash
         strcpy(pMainRc, TVTIME_INI_FILENAME);

      // open main config file to read name of frequency table
      fp = fopen(pMainRc, "r");
      if (fp != NULL)
      {
         pFreqTab = WintvCfg_ParseTvtimeConfig(fp);
         fclose(fp);

         // read station list
         fp = fopen(pChanTabPath, "r");
         if (fp != NULL)
         {
            if (pFreqTab != NULL)
               WintvCfg_ParseTvtimeStations(fp, pChanTab, pFreqTab);
            else
               WintvCfg_ParseTvtimeStations(fp, pChanTab, "europe");

            WintvCfg_ChanTabSort(pChanTab);

            fclose(fp);
            result = TRUE;
         }
         else
         {  // failed to open the station table
            if (ppErrMsg != NULL)
               SystemErrorMessage_Set(ppErrMsg, errno, 
                                      "Check your settings in the TV interaction configuration dialog. "
                                      "Could not open channel table \"", pChanTabPath, "\": ", NULL);
         }

         if (pFreqTab != NULL)
            xfree(pFreqTab);
      }
      else
      {  // failed to open the main config file
         if (ppErrMsg != NULL)
            SystemErrorMessage_Set(ppErrMsg, errno, 
                                   "Check your settings in the TV interaction configuration dialog. "
                                   "Could not open the tvtime config file \"", pMainRc, "\": ", NULL);
      }
      xfree(pMainRc);
   }
   return result;
}
#endif  // not WIN32

static const TVAPP_LIST tvAppList[TVAPP_COUNT] =
{
   { "none",     FALSE, NULL, "" },
#ifdef WIN32
   { "DScaler",  TRUE,  WintvCfg_GetDscalerChanTab,  "program.txt" },
   { "K!TV",     TRUE,  WintvCfg_GetKtvChanTab,      "program.set" },
   { "MultiDec", TRUE,  WintvCfg_GetMultidecChanTab, "Programm.set" },
   { "MoreTV",   FALSE, WintvCfg_GetMoretvChanTab,   "" },
   { "FreeTV",   FALSE, WintvCfg_GetFreetvChanTab,   "" },
#else
   { "Xawtv",    FALSE, WintvCfg_GetXawtvChanTab,    ".xawtv" },
   { "XawDecode",FALSE, WintvCfg_GetXawtvChanTab,    ".xawdecode/xawdecoderc" },
   { "XdTV",     FALSE, WintvCfg_GetXawtvChanTab,    ".xdtv/xdtvrc" },
   { "Zapping",  FALSE, WintvCfg_GetZappingChanTab,  ".zapping/zapping.conf" },
   { "TVTime",   FALSE, WintvCfg_GetTvtimeChanTab,   ".tvtime/stationlist.xml" },
#endif
};

// ----------------------------------------------------------------------------
// Query TV application parameters
// - to enumerate supported apps: call with increasing index param until result is FALSE
//
bool WintvCfg_QueryApp( uint appIdx, const char ** ppAppName, bool * pNeedPath )
{
   bool result = FALSE;

   if (appIdx < TVAPP_COUNT)
   {
      if (ppAppName != NULL)
         *ppAppName = tvAppList[appIdx].pName;
      if (pNeedPath != NULL)
         *pNeedPath = tvAppList[appIdx].needPath;
      result = TRUE;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Query current TV application
//
const char * WintvCfg_GetName( void )
{
   TVAPP_NAME appIdx = WintvCfg_GetAppIdx();

   if ((appIdx < TVAPP_COUNT) && (appIdx != TVAPP_NONE))
      return tvAppList[appIdx].pName;
   else
      return "TV app.";
}

// ----------------------------------------------------------------------------
// Query channel table for the given TV application
// - only used in the TV application interaction configuration dialog
//
bool WintvCfg_GetChanTab( uint appIdx, const char * pChanTabPath, char ** ppErrMsg,
                          char ** ppNameTab, uint ** ppFreqTab, uint * pCount )
{
   TV_CHNTAB_BUF chanTab;
   uint chanIdx;
   bool result = FALSE;

   WintvCfg_ChanTabInit(&chanTab);

   if (tvAppList[appIdx].pGetChanTab(&chanTab, pChanTabPath, ppErrMsg))
   {
      if (chanTab.itemCount == 0)
      {  // no channel assignments found in the file
         if (ppNameTab != NULL)
            *ppNameTab = NULL;
         if (ppFreqTab != NULL)
            *ppFreqTab = NULL;
         if (pCount != NULL)
            *pCount = 0;
      }
      else
      {
         if (pCount != NULL)
         {
            *pCount = chanTab.itemCount;
         }

         if (ppFreqTab != NULL)
         {
            *ppFreqTab = xmalloc(chanTab.itemCount * sizeof(uint));

            // copy the frequencies into a scalar array
            for (chanIdx = 0; chanIdx < chanTab.itemCount; chanIdx++)
            {
               if (chanTab.pData[chanIdx].freq != 0)
               {
                  (*ppFreqTab)[chanIdx] = chanTab.pData[chanIdx].freq;
               }
            }
         }

         if (ppNameTab != NULL)
         {
            *ppNameTab = chanTab.pStrBuf;
            chanTab.pStrBuf = NULL;
         }
      }
      result = TRUE;
   }
   // else: file open failed, errno contains reason

   WintvCfg_ChanTabDestroy(&chanTab);

   return result;
}

// ----------------------------------------------------------------------------
// Get TV channel names and frequencies
//
bool WintvCfg_GetFreqTab( char ** ppNameTab, uint ** ppFreqTab, uint * pCount, char ** ppErrMsg )
{
   const char * pTvAppPath;
   const char * pChanTabPath;
   int     appIdx;
   bool    result = FALSE;

   appIdx = WintvCfg_GetAppIdx();
   if (appIdx != TVAPP_NONE)
   {
#ifdef WIN32
      pTvAppPath = RcFile_Query()->tvapp.tvpath_win;
#else
      pTvAppPath = RcFile_Query()->tvapp.tvpath_unix;
#endif

      if ( (tvAppList[appIdx].needPath == FALSE) ||
           ((pTvAppPath != NULL) && (pTvAppPath[0] != 0)) )
      {
         pChanTabPath = WintvCfg_GetRcPath(pTvAppPath, appIdx);

         result = WintvCfg_GetChanTab(appIdx, pChanTabPath, ppErrMsg, ppNameTab, ppFreqTab, pCount);

         if (pChanTabPath != NULL)
            xfree((void *)pChanTabPath);
      }
      else
         debug1("WintvCfg-GetFreqTab: no TV app dir specified for app #%d", appIdx);
   }
   else
      debug0("WintvCfg-GetFreqTab: no TV app configured");

   return result;
}

