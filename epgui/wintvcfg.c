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
 *  $Id: wintvcfg.c,v 1.31 2020/06/21 07:37:23 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#ifdef WIN32
#include <windows.h>
#else
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/dvb/frontend.h>  // for QUAM
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/tvchan.h"
#include "epgvbi/syserrmsg.h"
#ifdef WIN32
#include "epgvbi/winshm.h"
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
   EPGACQ_TUNER_PAR freq;
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

#define CHNTAB_MISSING_FREQ    EPGACQ_TUNER_NORM_COUNT
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

// ----------------------------------------------------------------------------
// Check if a TV app is configured
//
TVAPP_NAME WintvCfg_GetAppIdx( void )
{
   int appIdx;
   TVAPP_NAME result;

#ifdef WIN32
   appIdx = RcFile_Query()->tvapp.tvapp_win;
#else
   appIdx = RcFile_Query()->tvapp.tvapp_unix;
#endif

   if (appIdx < TVAPP_COUNT)
   {
      result = (TVAPP_NAME) appIdx;
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
         pPath = (char*) xmalloc(strlen(pBase) + strlen(pFileName) + 2);
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
         pPath = (char*) xmalloc(strlen(pBase) + strlen(sbuf) - 26 + 2);
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
      pChanTab->pData = (TV_CHNTAB_ITEM*) xmalloc(pChanTab->maxItemCount * sizeof(TV_CHNTAB_ITEM));

      if (pOldData != NULL)
      {
         memcpy(pChanTab->pData, pOldData, pChanTab->itemCount * sizeof(TV_CHNTAB_ITEM));
         xfree(pOldData);
      }
   }

   // initialize the new item
   pChanTab->pData[pChanTab->itemCount].freq.norm = CHNTAB_MISSING_FREQ;
   pChanTab->pData[pChanTab->itemCount].strOff = CHNTAB_MISSING_NAME;
   pChanTab->pData[pChanTab->itemCount].sortIdx = pChanTab->itemCount;
}

#ifndef WIN32 // currently not used for WIN32 TV apps - only to avoid compiler warning
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
static void WintvCfg_ChanTabAddFreq( TV_CHNTAB_BUF * pChanTab, uint freq, EPGACQ_TUNER_NORM norm )
{
   // open call must precede addition, hence there must be at least one free slot left
   assert(pChanTab->itemCount < pChanTab->maxItemCount);
   assert(pChanTab->pData[pChanTab->itemCount].freq.norm == CHNTAB_MISSING_FREQ);

   if (freq != 0)
   {
      pChanTab->pData[pChanTab->itemCount].freq.freq = freq;
      pChanTab->pData[pChanTab->itemCount].freq.norm = norm;
   }
   else
   {
      pChanTab->pData[pChanTab->itemCount].freq.freq = 0;
      pChanTab->pData[pChanTab->itemCount].freq.norm = EPGACQ_TUNER_EXTERNAL;
   }
}

#ifndef WIN32 // currently not used for WIN32 TV apps - only to avoid compiler warning
static void WintvCfg_ChanTabAddDvbFreq( TV_CHNTAB_BUF * pChanTab, const EPGACQ_TUNER_PAR *  freq )
{
   // open call must precede addition, hence there must be at least one free slot left
   assert(pChanTab->itemCount < pChanTab->maxItemCount);
   assert(pChanTab->pData[pChanTab->itemCount].freq.norm == CHNTAB_MISSING_FREQ);
   assert(EPGACQ_TUNER_NORM_IS_DVB(freq->norm));

   pChanTab->pData[pChanTab->itemCount].freq = *freq;
}
#endif // WIN32

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
         newbuf = (char*) xmalloc(pChanTab->strBufSize + len + 2048);
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
      debug3("WintvCfg-ChanTabAddName: duplicate name for channel #%d '%s': '%s'", pChanTab->itemCount, pChanTab->pStrBuf + pChanTab->pData[pChanTab->itemCount].strOff, pName);
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
      if ( (pChanTab->pData[pChanTab->itemCount].freq.norm != CHNTAB_MISSING_FREQ) &&
           (pChanTab->pData[pChanTab->itemCount].strOff != CHNTAB_MISSING_NAME) )
      {
         // replace missing data with default values: invalid frequency / empty name
         if (pChanTab->pData[pChanTab->itemCount].freq.norm == CHNTAB_MISSING_FREQ)
         {
            WintvCfg_ChanTabAddFreq(pChanTab, 0, EPGACQ_TUNER_EXTERNAL);
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

#ifndef WIN32 // currently not used for WIN32 TV apps - only to avoid compiler warning
// ---------------------------------------------------------------------------
// Sort the channel table
//
static int WintvCfg_ChanTabSortCompare( const void * va, const void * vb )
{
   const TV_CHNTAB_ITEM * pItem1 = (const TV_CHNTAB_ITEM *) va;
   const TV_CHNTAB_ITEM * pItem2 = (const TV_CHNTAB_ITEM *) vb;
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
      pNewStrBuf = (char*) xmalloc(pChanTab->strBufSize);
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

static void WintvCfg_ChanTabAddDvb( TV_CHNTAB_BUF * pChanTab, const char * pName, const EPGACQ_TUNER_PAR * par )
{
   WintvCfg_ChanTabItemOpen(pChanTab);
   WintvCfg_ChanTabAddName(pChanTab, pName);
   WintvCfg_ChanTabAddDvbFreq(pChanTab, par);
   WintvCfg_ChanTabItemClose(pChanTab);
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
         if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, (BYTE*) &freq, &dwSize) == ERROR_SUCCESS) &&
              (dwType == REG_DWORD) && (dwSize == sizeof(freq)) )
         {
            sprintf(keyValStr, "%02dSendername", idx);
            dwSize = sizeof(name_buf) - 1;
            if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, (BYTE*)name_buf, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ) )
            {
               WintvCfg_ChanTabItemOpen(pChanTab);

               WintvCfg_ChanTabAddFreq(pChanTab, (freq * 4) / 25, EPGACQ_TUNER_NORM_PAL);

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
      if ( (RegQueryValueEx(hKey, "Count", 0, &dwType, (BYTE*) &chanCount, &dwSize) == ERROR_SUCCESS) &&
           (dwType == REG_DWORD) && (dwSize == sizeof(chanCount)) )
      {
         for (chanIdx = 1; chanIdx <= chanCount; chanIdx++)
         {
            WintvCfg_ChanTabItemOpen(pChanTab);

            sprintf(keyValStr, "Frequency%d", chanIdx);

            dwSize = sizeof(freq);
            if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, (BYTE*) &freq, &dwSize) == ERROR_SUCCESS) &&
                 (dwType == REG_DWORD) && (dwSize == sizeof(freq)) )
            {
               WintvCfg_ChanTabAddFreq(pChanTab, (freq * 2) / 125, EPGACQ_TUNER_NORM_PAL);
            }

            sprintf(keyValStr, "Name%d", chanIdx);

            dwSize = sizeof(name_buf) - 1;
            if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, (BYTE*) name_buf, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ) )
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
                  WintvCfg_ChanTabAddFreq(pChanTab, Programm.freq * 2 / 125, EPGACQ_TUNER_NORM_PAL);

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
                  WintvCfg_ChanTabAddFreq(pChanTab, ProgrammAlt.freq * 2 / 125, EPGACQ_TUNER_NORM_PAL);

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
                  WintvCfg_ChanTabAddFreq(pChanTab, freq * 2 / 125, EPGACQ_TUNER_NORM_PAL);
               }
            }
            // K!TV version 2
            else if (sscanf(sbuf, "frequency %*[=:] %ld", &freq) >= 1)
            {
               if (isOpen)
               {
                  freq /= 1000;
                  WintvCfg_ChanTabAddFreq(pChanTab, freq * 2 / 125, EPGACQ_TUNER_NORM_PAL);
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
                  WintvCfg_ChanTabAddFreq(pChanTab, freq, EPGACQ_TUNER_NORM_PAL);
               }
               else if (strnicmp(sbuf, "Freq2:", 6) == 0)
               {
                  freq = atol(sbuf + 6);
                  WintvCfg_ChanTabAddFreq(pChanTab, freq, EPGACQ_TUNER_NORM_PAL);
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
// Helper function for parsing text lines of channels.conf
// - Given "maxCnt" should be +1 abouve max expected too catch excessive fields
// - Replaces all ':' in the given string with zero bytes and fills the given
//   array with pointers to the thus generated sub-strings
// - Newline char is stripped from the last field
//
static uint WintvCfg_SplitStringColon( char * sbuf, char ** fields, uint maxCnt )
{
   char * sep = sbuf;
   int fieldIdx;

   fields[0] = sbuf;
   for (fieldIdx = 1; fieldIdx < maxCnt; ++fieldIdx)
   {
      sep = strchr(sep, ':');
      if (sep == NULL)
         break;
      *(sep++) = 0;
      fields[fieldIdx] = sep;
   }

   // trim trailing newline char from last field
   char *p1 = fields[fieldIdx - 1];
   char *p2 = p1 + strlen(p1) - 1;
   while ((p2 >= p1) && isspace(*p2))
      *(p2--) = 0;

   // detect excessive fields above maxCnt
   if (strchr(fields[fieldIdx - 1], ':') != NULL)
      fieldIdx += 1;

   return fieldIdx;
}

// ----------------------------------------------------------------------------
// Extract all channels from VDR "channels.conf" table
// - Format: http://www.vdr-wiki.de/wiki/index.php/Channels.conf
// - List is a colon-separated table with one line per channel; each line contains:
//   name:freq:param:signal source:symbol rate:VPID:APID:TPID:CAID:SID:NID:TID:RID
//
typedef enum
{
   CCNF_VDR_NAME,
   CCNF_VDR_FREQUENCY,
   CCNF_VDR_PARAM,
   CCNF_VDR_SIG_SRC,
   CCNF_VDR_SYMBOL_RATE,
   CCNF_VDR_PID_VIDEO,
   CCNF_VDR_PID_AUDIO,
   CCNF_VDR_PID_TTX,
   CCNF_VDR_PID_CA,
   CCNF_VDR_SERVICE_ID,
   CCNF_VDR_NID,
   CCNF_VDR_TID,
   CCNF_VDR_RID,
   CCNF_VDR_COUNT
} T_FIELDS_VDR;

static bool WintvCfg_GetVdrChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   char sbuf[2048];
   FILE * fp;
   char * fields[CCNF_VDR_COUNT + 1];
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         while (feof(fp) == FALSE)
         {
            sbuf[0] = '\0';

            if (fgets(sbuf, 2048-1, fp) != NULL)
            {
               if (WintvCfg_SplitStringColon(sbuf, fields, CCNF_VDR_COUNT + 1) == CCNF_VDR_COUNT)
               {
                  // strip bouquet name from network name
                  char * p1 = strchr(fields[CCNF_VDR_NAME], ';');
                  if (p1 != NULL)
                     *p1 = 0;

                  EPGACQ_TUNER_PAR par;
                  memset(&par, 0, sizeof(par));

                  if (strcmp(fields[CCNF_VDR_SIG_SRC], "C") == 0)
                     par.norm = EPGACQ_TUNER_NORM_DVB_C;
                  else if (strcmp(fields[CCNF_VDR_SIG_SRC], "T") == 0)
                     par.norm = EPGACQ_TUNER_NORM_DVB_T;
                  else
                     par.norm = EPGACQ_TUNER_NORM_DVB_S;

                  par.freq = atol(fields[CCNF_VDR_FREQUENCY]);
                  while (par.freq <= 1000000)
                     par.freq *= 1000;

                  par.ttxPid     = atol(fields[CCNF_VDR_PID_TTX]);  // implicitly ignore ";" and following
                  par.serviceId  = atol(fields[CCNF_VDR_SYMBOL_RATE]);

                  par.symbolRate = atol(fields[CCNF_VDR_SYMBOL_RATE]) * 1000;
                  par.modulation = QAM_AUTO;
                  par.inversion  = INVERSION_AUTO;
                  par.codeRate   = FEC_AUTO;
                  par.codeRateLp = FEC_AUTO;
                  par.bandwidth  = BANDWIDTH_AUTO;
                  par.transMode  = TRANSMISSION_MODE_AUTO;
                  par.guardBand  = GUARD_INTERVAL_AUTO;
                  par.hierarchy  = HIERARCHY_AUTO;

                  // skip radio channels etc.
                  if (atol(fields[CCNF_VDR_PID_VIDEO]) != 0)
                  {
                     char * field_par = fields[CCNF_VDR_PARAM];
                     char * field_end = field_par + strlen(field_par);
                     while (field_par < field_end)
                     {
                        char op = *(field_par++);
                        char * ends;
                        long val;

                        switch (op)
                        {
                           // specifiers that are followed by a decimal value
                           case 'I':
                           case 'C':
                           case 'D':
                           case 'M':
                           case 'B':
                           case 'T':
                           case 'G':
                           case 'Y':
                           case 'S':
                              val = strtol(field_par, &ends, 10);
                              if (ends != field_par)
                              {
                                 field_par = ends;

                                 if (op == 'M')
                                 {
                                    switch (val)
                                    {
                                       case 2:  par.modulation = QPSK; break;
                                       case 5:  par.modulation = PSK_8; break;
                                       case 6:  par.modulation = APSK_16; break;
                                       case 7:  par.modulation = APSK_32; break;
                                       case 10: par.modulation = VSB_8; break;
                                       case 11: par.modulation = VSB_16; break;
                                       case 12: par.modulation = DQPSK; break;

                                       case 16:  par.modulation = QAM_16; break;
                                       case 32:  par.modulation = QAM_32; break;
                                       case 64:  par.modulation = QAM_64; break;
                                       case 128:  par.modulation = QAM_128; break;
                                       case 256:  par.modulation = QAM_256; break;
                                       case 998:  // 998 acc. to w_scan
                                       case 999:  par.modulation = QAM_AUTO; break;
                                       default:
                                          fprintf(stderr, "WARNING: invalid modulation value %ld in VDR: %s", val, sbuf);
                                    }
                                 }
                                 else if (op == 'B')
                                 {
                                    switch (val)
                                    {
                                       case 8:  par.bandwidth = BANDWIDTH_8_MHZ; break;
                                       case 7:  par.bandwidth = BANDWIDTH_7_MHZ; break;
                                       case 6:  par.bandwidth = BANDWIDTH_6_MHZ; break;
                                       case 5:  par.bandwidth = BANDWIDTH_5_MHZ; break;
                                       case 10:  par.bandwidth = BANDWIDTH_10_MHZ; break;
                                       case 1712: par.bandwidth = BANDWIDTH_1_712_MHZ; break;
                                       case 999:  par.bandwidth = BANDWIDTH_AUTO; break;
                                       default:
                                          fprintf(stderr, "WARNING: invalid bandwidth value %ld in VDR: %s", val, sbuf);
                                    }
                                 }
                                 else if (op == 'T')
                                 {
                                    switch (val)
                                    {
                                       case 2:   par.transMode = TRANSMISSION_MODE_2K; break;
                                       case 8:   par.transMode = TRANSMISSION_MODE_8K; break;
                                       case 4:   par.transMode = TRANSMISSION_MODE_4K; break;
                                       case 1:   par.transMode = TRANSMISSION_MODE_1K; break;
                                       case 16:  par.transMode = TRANSMISSION_MODE_16K; break;
                                       case 32:  par.transMode = TRANSMISSION_MODE_32K; break;
                                       case 999: par.transMode = TRANSMISSION_MODE_AUTO; break;
                                       default:
                                          fprintf(stderr, "WARNING: invalid transmission mode value %ld in VDR: %s", val, sbuf);
                                    }
                                 }
                                 else if (op == 'I')
                                 {
                                    switch (val)
                                    {
                                       case 0:   par.inversion = INVERSION_OFF; break;
                                       case 1:   par.inversion = INVERSION_ON; break;
                                       case 999: par.inversion = INVERSION_AUTO; break;
                                       default:
                                          fprintf(stderr, "WARNING: invalid inversion value %ld in VDR: %s", val, sbuf);
                                    }
                                 }
                                 else if ((op == 'C') || (op == 'C'))
                                 {
                                    int conv = 0;
                                    switch (val)
                                    {
                                       case 0: conv = FEC_NONE; break;
                                       case 12: conv = FEC_1_2; break;
                                       case 23: conv = FEC_2_3; break;
                                       case 25: conv = FEC_2_5; break;
                                       case 34: conv = FEC_3_4; break;
                                       case 35: conv = FEC_3_5; break;
                                       case 45: conv = FEC_4_5; break;
                                       case 56: conv = FEC_5_6; break;
                                       case 67: conv = FEC_6_7; break;
                                       case 78: conv = FEC_7_8; break;
                                       case 89: conv = FEC_8_9; break;
                                       case 910: conv = FEC_9_10; break;
                                       case 999: conv = FEC_AUTO; break;
                                       default:
                                          fprintf(stderr, "WARNING: invalid code rate value %ld in VDR: %s", val, sbuf);
                                    }
                                    if (op == 'C')
                                       par.codeRate = conv;
                                    else
                                       par.codeRateLp = conv;
                                 }
                                 else if (op == 'G')
                                 {
                                    switch (val)
                                    {
                                       case 32:    par.guardBand = GUARD_INTERVAL_1_32; break;
                                       case 16:    par.guardBand = GUARD_INTERVAL_1_16; break;
                                       case 8:     par.guardBand = GUARD_INTERVAL_1_8; break;
                                       case 4:     par.guardBand = GUARD_INTERVAL_1_4; break;
                                       case 128:   par.guardBand = GUARD_INTERVAL_1_128; break;
                                       case 19128: par.guardBand = GUARD_INTERVAL_19_128; break;
                                       case 19256: par.guardBand = GUARD_INTERVAL_19_256; break;
                                       case 999:   par.guardBand = GUARD_INTERVAL_AUTO; break;
                                       default:
                                          fprintf(stderr, "WARNING: invalid guard band value %ld in VDR: %s", val, sbuf);
                                    }
                                 }
                                 else if (op == 'Y')
                                 {
                                    switch (val)
                                    {
                                       case 0:    par.hierarchy = HIERARCHY_NONE; break;
                                       case 1:    par.hierarchy = HIERARCHY_1; break;
                                       case 2:    par.hierarchy = HIERARCHY_2; break;
                                       case 4:    par.hierarchy = HIERARCHY_4; break;
                                       case 999:   par.hierarchy = HIERARCHY_AUTO; break;
                                       default:
                                          fprintf(stderr, "WARNING: invalid guard band value %ld in VDR: %s", val, sbuf);
                                    }
                                 }
                                 else if (op == 'S')
                                 {
                                    switch (val)
                                    {
                                       case 0:   // DVB-S or DVB-T
                                          break;
                                       case 1:   // DVB-S2 or DVB-T2
                                          if (par.norm == EPGACQ_TUNER_NORM_DVB_T)
                                             par.norm = EPGACQ_TUNER_NORM_DVB_T2;
                                          else if (par.norm == EPGACQ_TUNER_NORM_DVB_S)
                                             par.norm = EPGACQ_TUNER_NORM_DVB_S2;
                                          break;
                                       default:
                                          fprintf(stderr, "WARNING: invalid DVB-S generation %ld in VDR: %s", val, sbuf);
                                    }
                                 }
                              }
                              else
                              {
                                 fprintf(stderr, "WARNING: missing value for param '%c' in VDR: %s", op, sbuf);
                              }
                              break;

                           // parameters that are not followed by value
                           case 'H':
                           case 'V':
                           case 'R':
                           case 'L':
                              // POLARIZATION_HORIZONTAL
                              // POLARIZATION_VERTICAL
                              // POLARIZATION_CIRCULAR_LEFT
                              // POLARIZATION_CIRCULAR_RIGHT
                              break;

                           // ignore whitespace
                           case ' ':
                           case '\t':
                              break;
                           default:
                              fprintf(stderr, "WARNING: invalid param '%c' in VDR: %s", op, sbuf);
                              break;
                        }
                     }

                     WintvCfg_ChanTabAddDvb(pChanTab, fields[CCNF_VDR_NAME], &par);
                  }
               }
               else
               {
                  fprintf(stderr, "WARNING: Malformed VDR channels.conf entry skipped: %s\n", sbuf);
               }
            }
            else
               break;
         }
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
// Conversion helper functions for Mplayer configuration
//
#if 0
static int WintvCfg_MplayerPolarization( const char * pName, char ** pError )
{
   if (strncmp(pName, "h") == 0)
      return POLARIZATION_HORIZONTAL;
   if (strncmp(pName, "v") == 0)
      return POLARIZATION_VERTICAL;
   if (strncmp(pName, "l") == 0)
      return POLARIZATION_CIRCULAR_LEFT;
   if (strncmp(pName, "r") == 0)
      return POLARIZATION_CIRCULAR_RIGHT;
   *pError = "invalid POLARIZATION";
   return 0;
}
#endif

static int WintvCfg_MplayerInversion( const char * pName, const char ** pError )
{
   if (strcmp(pName, "INVERSION_AUTO") == 0)
       return INVERSION_AUTO;
   if (strcmp(pName, "INVERSION_OFF") == 0)
       return INVERSION_OFF;
   if (strcmp(pName, "INVERSION_ON") == 0)
       return INVERSION_ON;
   *pError = "invalid INVERSION";
   return 0;
}

static int WintvCfg_MplayerCoderate( const char * pName, const char ** pError )
{
   if (strcmp(pName, "FEC_AUTO") == 0)
      return FEC_AUTO;
   if (strcmp(pName, "FEC_NONE") == 0)
       return FEC_NONE;
   if (strcmp(pName, "FEC_2_5") == 0)
       return FEC_2_5;
   if (strcmp(pName, "FEC_1_2") == 0)
       return FEC_1_2;
   if (strcmp(pName, "FEC_3_5") == 0)
       return FEC_3_5;
   if (strcmp(pName, "FEC_2_3") == 0)
       return FEC_2_3;
   if (strcmp(pName, "FEC_3_4") == 0)
       return FEC_3_4;
   if (strcmp(pName, "FEC_4_5") == 0)
       return FEC_4_5;
   if (strcmp(pName, "FEC_5_6") == 0)
       return FEC_5_6;
   if (strcmp(pName, "FEC_6_7") == 0)
       return FEC_6_7;
   if (strcmp(pName, "FEC_7_8") == 0)
       return FEC_7_8;
   if (strcmp(pName, "FEC_8_9") == 0)
       return FEC_8_9;
   if (strcmp(pName, "FEC_9_10") == 0)
       return FEC_9_10;
   *pError = "invalid FEC code rate";
   return 0;
}

static int WintvCfg_MplayerModulation( const char * pName, const char ** pError )
{
   if (strcmp(pName, "QAM_AUTO") == 0)
       return QAM_AUTO;
   if (strcmp(pName, "QPSK") == 0)
       return QPSK;
   if (strcmp(pName, "QAM_16") == 0)
       return QAM_16;
   if (strcmp(pName, "QAM_32") == 0)
       return QAM_32;
   if (strcmp(pName, "QAM_64") == 0)
       return QAM_64;
   if (strcmp(pName, "QAM_128") == 0)
       return QAM_128;
   if (strcmp(pName, "QAM_256") == 0)
       return QAM_256;
   if (strcmp(pName, "VSB_8") == 0)
       return VSB_8;
   if (strcmp(pName, "VSB_16") == 0)
       return VSB_16;
   if (strcmp(pName, "PSK_8") == 0)
       return PSK_8;
   if (strcmp(pName, "APSK_16") == 0)
       return APSK_16;
   if (strcmp(pName, "APSK_32") == 0)
       return APSK_32;
   if (strcmp(pName, "DQPSK") == 0)
       return DQPSK;
   if (strcmp(pName, "QAM_4_NR") == 0)
       return QAM_4_NR;
   *pError = "invalid modulation";
   return 0;
}

static int WintvCfg_MplayerBandwidth( const char * pName, const char ** pError )
{
   if (strcmp(pName, "BANDWIDTH_AUTO") == 0)
      return BANDWIDTH_AUTO;
   if (strcmp(pName, "BANDWIDTH_8_MHZ") == 0)
       return 8000000;
   if (strcmp(pName, "BANDWIDTH_7_MHZ") == 0)
       return 7000000;
   if (strcmp(pName, "BANDWIDTH_6_MHZ") == 0)
       return 6000000;
   if (strcmp(pName, "BANDWIDTH_5_MHZ") == 0)
       return 5000000;
   if (strcmp(pName, "BANDWIDTH_10_MHZ") == 0)
       return 10000000;
   if (strcmp(pName, "BANDWIDTH_1_712_MHZ") == 0)
       return 1712000;
   *pError = "invalid bandwidth";
   return 0;
}

static int WintvCfg_MplayerTransmissionMode( const char * pName, const char ** pError )
{
   if (strcmp(pName, "TRANSMISSION_MODE_AUTO") == 0)
      return TRANSMISSION_MODE_AUTO;
   if (strcmp(pName, "TRANSMISSION_MODE_1K") == 0)
       return TRANSMISSION_MODE_1K;
   if (strcmp(pName, "TRANSMISSION_MODE_2K") == 0)
       return TRANSMISSION_MODE_2K;
   if (strcmp(pName, "TRANSMISSION_MODE_4K") == 0)
       return TRANSMISSION_MODE_4K;
   if (strcmp(pName, "TRANSMISSION_MODE_8K") == 0)
       return TRANSMISSION_MODE_8K;
   if (strcmp(pName, "TRANSMISSION_MODE_16K") == 0)
       return TRANSMISSION_MODE_16K;
   if (strcmp(pName, "TRANSMISSION_MODE_32K") == 0)
       return TRANSMISSION_MODE_32K;
   if (strcmp(pName, "TRANSMISSION_MODE_C1") == 0)
       return TRANSMISSION_MODE_C1; 
   if (strcmp(pName, "TRANSMISSION_MODE_C3780") == 0)
       return TRANSMISSION_MODE_C3780;
   *pError = "invalid transmission mode";
   return 0;
}

static int WintvCfg_MplayerGuardInterval( const char * pName, const char ** pError )
{
   if (strcmp(pName, "GUARD_INTERVAL_AUTO") == 0)
      return GUARD_INTERVAL_AUTO;
   if (strcmp(pName, "GUARD_INTERVAL_1_32") == 0)
       return GUARD_INTERVAL_1_32;
   if (strcmp(pName, "GUARD_INTERVAL_1_16") == 0)
       return GUARD_INTERVAL_1_16;
   if (strcmp(pName, "GUARD_INTERVAL_1_8") == 0)
       return GUARD_INTERVAL_1_8;
   if (strcmp(pName, "GUARD_INTERVAL_1_4") == 0)
       return GUARD_INTERVAL_1_4;
   if (strcmp(pName, "GUARD_INTERVAL_1_128") == 0)
       return GUARD_INTERVAL_1_128;
   if (strcmp(pName, "GUARD_INTERVAL_19_128") == 0)
       return GUARD_INTERVAL_19_128;
   if (strcmp(pName, "GUARD_INTERVAL_19_256") == 0)
       return GUARD_INTERVAL_19_256;
   if (strcmp(pName, "GUARD_INTERVAL_PN420") == 0)
       return GUARD_INTERVAL_PN420;
   if (strcmp(pName, "GUARD_INTERVAL_PN595") == 0)
       return GUARD_INTERVAL_PN595;
   if (strcmp(pName, "GUARD_INTERVAL_PN945") == 0)
       return GUARD_INTERVAL_PN945;
   *pError = "invalid guard interval";
   return 0;
}

static int WintvCfg_MplayerHierarchy( const char * pName, const char ** pError )
{
   if (strcmp(pName, "HIERARCHY_AUTO") == 0)
      return HIERARCHY_AUTO;
   if (strcmp(pName, "HIERARCHY_NONE") == 0)
       return HIERARCHY_NONE;
   if (strcmp(pName, "HIERARCHY_1") == 0)
       return HIERARCHY_1;
   if (strcmp(pName, "HIERARCHY_2") == 0)
       return HIERARCHY_2;
   if (strcmp(pName, "HIERARCHY_4") == 0)
       return HIERARCHY_4;
   *pError = "invalid hierarchy";
   return 0;
}

static int WintvCfg_Atoi( const char * pName, const char ** pError )
{
   char * pEnd;
   long longVal = strtol(pName, &pEnd, 0);
   long intVal = longVal;
   if ((pEnd != pName) && (*pEnd == 0) && (intVal == longVal))
      return intVal;
   *pError = "expected numerical value";
   return 0;
}

static long WintvCfg_Atol( const char * pName, const char ** pError )
{
   char * pEnd;
   long longVal = strtol(pName, &pEnd, 0);
   if ((pEnd != pName) && (*pEnd == 0))
      return longVal;
   *pError = "expected numerical value";
   return 0;
}

// ----------------------------------------------------------------------------
// Extract all channels from mplayer "channels.conf" table
// - Format differs between DVB-C, DVB-S, DVB-T2: different number of columns
// - See dump_mplayer.c in https://www.gen2vdr.de/wirbel/w_scan/archiv.html
//
// DVB-C:  NAME:FREQUENCY:INVERSION:SYMBOL_RATE:FEC:MODULATION:PID_VIDEO:PID_AUDIO:SERVICE_ID
// DVB-S:  NAME:FREQUENCY:POLARIZATION:ROTOR_POS:SYMBOL_RATE:PID_VIDEO:PID_AUDIO:SERVICE_ID
// DVB-T2: NAME:FREQUENCY:INVERSION:BANDWIDTH:SYMBOL_RATE:SYMBOL_RATE_LP:MODULATION:TRANSMISSION:GUARD_BAND:HIEARCHY:PID_VIDEO:PID_AUDIO:SERVICE_ID
//
// Examples:
// DVB-C:  name:610000000:INVERSION_AUTO:6900000:FEC_NONE:QAM_64:1511:1512:50122
// DVB-S:  name:11727:h:0:27500:109:209:9
// DVB-T2: name:474000000:INVERSION_AUTO:BANDWIDTH_8_MHZ:FEC_AUTO:FEC_AUTO:QAM_256:TRANSMISSION_MODE_32K:GUARD_INTERVAL_1_128:HIERARCHY_AUTO:6601:6602:17540
//
static bool WintvCfg_GetMplayerChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   EPGACQ_TUNER_PAR par;
   char sbuf[2048];
   FILE * fp;
   char * fields[13+1];  // MAX(8, 7, 13)
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         while (feof(fp) == FALSE)
         {
            const char * error = NULL;
            memset(&par, 0, sizeof(par));
            sbuf[0] = '\0';

            if (fgets(sbuf, 2048-1, fp) != NULL)
            {
               uint fieldCnt = WintvCfg_SplitStringColon(sbuf, fields, 13+1);

               if (fieldCnt == 9)
               {
                  // skip radio channels etc.
                  if (atol(fields[6]) != 0)  // PID_VIDEO
                  {
                     // DVB-C:  NAME:FREQUENCY:INVERSION:SYMBOL_RATE:FEC:MODULATION:PID_VIDEO:PID_AUDIO:SERVICE_ID
                     par.norm       = EPGACQ_TUNER_NORM_DVB_C;
                     par.freq       = WintvCfg_Atol(fields[1], &error);
                     par.inversion  = WintvCfg_MplayerInversion(fields[2], &error);
                     par.symbolRate = WintvCfg_Atoi(fields[3], &error);
                     par.codeRate   = WintvCfg_MplayerCoderate(fields[4], &error);
                     par.modulation = WintvCfg_MplayerModulation(fields[5], &error);
                     par.serviceId  = WintvCfg_Atoi(fields[8], &error);

                     if (error == NULL)
                        WintvCfg_ChanTabAddDvb(pChanTab, fields[0], &par);
                     else
                        fprintf(stderr, "WARNING: Error parsing DVB-C config: %s: %s\n", error, sbuf);
                  }
               }
               else if (fieldCnt == 8)
               {
                  // skip radio channels etc.
                  if (atol(fields[5]) != 0)  // PID_VIDEO
                  {
                     // DVB-S:  NAME:FREQUENCY:POLARIZATION:ROTOR_POS:SYMBOL_RATE:PID_VIDEO:PID_AUDIO:SERVICE_ID
                     par.norm       = EPGACQ_TUNER_NORM_DVB_S;
                     par.freq       = WintvCfg_Atol(fields[1], &error);
                     //par.polarization = WintvCfg_MplayerPolarization(fields[2], &error);
                     //par.rotorPos = WintvCfg_WintvCfg_Atoi(fields[3], &error);
                     par.symbolRate = WintvCfg_Atoi(fields[4], &error);
                     par.serviceId  = WintvCfg_Atoi(fields[7], &error);
                     par.inversion  = INVERSION_AUTO;
                     par.modulation = QAM_AUTO;
                     par.codeRate   = FEC_AUTO;

                     if (error == NULL)
                        WintvCfg_ChanTabAddDvb(pChanTab, fields[0], &par);
                     else
                        fprintf(stderr, "WARNING: Error parsing DVB-S config: %s: %s\n", error, sbuf);
                  }
               }
               else if (fieldCnt == 13)
               {
                  // skip radio channels etc.
                  if (atol(fields[10]) != 0)  // PID_VIDEO
                  {
                     // DVB-T2: NAME:FREQUENCY:INVERSION:BANDWIDTH:SYMBOL_RATE:SYMBOL_RATE_LP:MODULATION:TRANSMISSION:GUARD_BAND:HIEARCHY:PID_VIDEO:PID_AUDIO:SERVICE_ID
                     par.norm       = EPGACQ_TUNER_NORM_DVB_T2;
                     par.freq       = WintvCfg_Atol(fields[1], &error);
                     par.inversion  = WintvCfg_MplayerInversion(fields[2], &error);
                     par.bandwidth  = WintvCfg_MplayerBandwidth(fields[3], &error);
                     par.codeRate   = WintvCfg_MplayerCoderate(fields[4], &error);
                     par.codeRateLp = WintvCfg_MplayerCoderate(fields[5], &error);
                     par.modulation = WintvCfg_MplayerModulation(fields[6], &error);
                     par.transMode  = WintvCfg_MplayerTransmissionMode(fields[7], &error);
                     par.guardBand  = WintvCfg_MplayerGuardInterval(fields[8], &error);
                     par.hierarchy  = WintvCfg_MplayerHierarchy(fields[9], &error);
                     par.serviceId  = WintvCfg_Atoi(fields[12], &error);

                     if (error == NULL)
                        WintvCfg_ChanTabAddDvb(pChanTab, fields[0], &par);
                     else
                        fprintf(stderr, "WARNING: Error parsing DVB-T config: %s: %s\n", error, sbuf);
                  }
               }
               else
               {
                  fprintf(stderr, "WARNING: Malformed mplayer channels.conf entry skipped (unexpected field count): %s\n", sbuf);
               }
            }
            else
               break;
         }
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
// Extract all TV frequencies & norms from $HOME/.xawtv for the EPG scan
// - the parsing pattern used here is lifted directly from xawtv-3.41
// - station names are defined on single lines in brackets, e.g. [ARD]
//
static bool WintvCfg_GetXawtvChanTab( TV_CHNTAB_BUF * pChanTab, const char * pChanTabPath, char ** ppErrMsg )
{
   char line[256], tag[64], value[192];
   EPGACQ_TUNER_NORM defaultNorm, attrNorm;
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
         defaultNorm = attrNorm = EPGACQ_TUNER_NORM_PAL;
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
                     WintvCfg_ChanTabAddFreq(pChanTab, freq + attrFine, attrNorm);
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
                     attrNorm = EPGACQ_TUNER_NORM_PAL;
                  else if (strncasecmp(value, "secam", 5) == 0)
                     attrNorm = EPGACQ_TUNER_NORM_SECAM;
                  else if (strncasecmp(value, "ntsc", 4) == 0)
                     attrNorm = EPGACQ_TUNER_NORM_NTSC;
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
               WintvCfg_ChanTabAddFreq(pChanTab, freq + attrFine, attrNorm);
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
   EPGACQ_TUNER_NORM norm;

   subTreeLevel = 0;
   freq = 0;
   norm = EPGACQ_TUNER_NORM_PAL;

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
         norm = EPGACQ_TUNER_NORM_PAL;
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
         norm = (EPGACQ_TUNER_NORM) strtol(line + len, &endp, 10);
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
            dprintf2("Zapping channel: freq=%ld norm=%d\n", freq, norm);
            WintvCfg_ChanTabAddFreq(pChanTab, freq * 16/1000, norm);
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
   EPGACQ_TUNER_NORM defaultNorm;
   bool isActiveList;

   subTreeLevel = 0;
   isActiveList = FALSE;
   defaultNorm = EPGACQ_TUNER_NORM_PAL;

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
                     defaultNorm = EPGACQ_TUNER_NORM_NTSC;
                  else if (strncmp(tag, "SECAM", 5) == 0)
                     defaultNorm = EPGACQ_TUNER_NORM_SECAM;
                  else
                     defaultNorm = EPGACQ_TUNER_NORM_PAL;
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
            debug1("WintvCfg-ParseTvtimeStations: XML parse error line '%.100s'", line);
      }
      else if ( (subTreeLevel == 2) &&
                (isActiveList) &&
                (sscanf(line, " < station %1[a-zA-Z]%n", tag, &len) == 1) )  // dummy string read
      {
         char band[64];
         char chan[64];
         int freq = 0;
         EPGACQ_TUNER_NORM norm = defaultNorm;
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
               norm = EPGACQ_TUNER_NORM_NTSC;
            else if (strncmp(tag, "SECAM", 5) == 0)
               norm = EPGACQ_TUNER_NORM_SECAM;
            else
               norm = EPGACQ_TUNER_NORM_PAL;
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
            WintvCfg_ChanTabAddFreq(pChanTab, freq + fine, norm);
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
   const char * pBase;
   char * pMainRc;
   char * pFreqTab;
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      // derive path to main config file from path to station list: replace file name (keep dir)
      pMainRc = (char*) xmalloc(strlen(pChanTabPath) + strlen(TVTIME_INI_FILENAME));
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
   { "VDR",      TRUE,  WintvCfg_GetVdrChanTab,      "channels.conf" },
   { "mplayer",  TRUE,  WintvCfg_GetMplayerChanTab,  "channels.conf" },
   { "Kaffeine", TRUE,  WintvCfg_GetMplayerChanTab,  "channels.conf" },
   { "Totem",    TRUE,  WintvCfg_GetMplayerChanTab,  "channels.conf" },
   { "Xine",     TRUE,  WintvCfg_GetMplayerChanTab,  "channels.conf" },

   { "Xawtv",    FALSE, WintvCfg_GetXawtvChanTab,    ".xawtv" },
   { "XawDecode",FALSE, WintvCfg_GetXawtvChanTab,    ".xawdecode/xawdecoderc" },
   { "XdTV",     FALSE, WintvCfg_GetXawtvChanTab,    ".xdtv/xdtvrc" },
   { "Zapping",  FALSE, WintvCfg_GetZappingChanTab,  ".zapping/zapping.conf" },
   { "TVTime",   FALSE, WintvCfg_GetTvtimeChanTab,   ".tvtime/stationlist.xml" },
#endif
};

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

            pPath = (char*) xmalloc(strlen(pBase) + strlen(pFileName) + 2);
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
                          char ** ppNameTab, EPGACQ_TUNER_PAR ** ppFreqTab, uint * pCount )
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
            *ppFreqTab = (EPGACQ_TUNER_PAR*) xmalloc(chanTab.itemCount * sizeof(EPGACQ_TUNER_PAR));

            // copy the frequencies into a scalar array
            for (chanIdx = 0; chanIdx < chanTab.itemCount; chanIdx++)
            {
               (*ppFreqTab)[chanIdx] = chanTab.pData[chanIdx].freq;
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
// Extract a channel name from the table returned by WintvCfg-GetFreqTab()
//
void WintvCfg_ExtractName( const char * pNameTab, uint count, uint chanIdx, char * pBuf, uint bufSize )
{
   const char * pName = pNameTab;

   if ((pBuf != NULL) && (bufSize > 0))
   {
      if (chanIdx < count)
      {
         for (uint idx = 0; idx < chanIdx; idx++)
         {
            while(*(pName++) != 0)
               ;
         }

         strncpy(pBuf, pName, bufSize - 1);
         if ((strlen(pName) >= bufSize) && (bufSize > 3))
            strcpy(pBuf + bufSize - (3+1), "...");
         pBuf[bufSize - 1] = 0;
      }
      else
      {
         fatal2("WintvCfg-ExtractName: illegal idx:%d >=%d", chanIdx, count);
         pBuf[0] = 0;
      }
   }
   else
      fatal2("WintvCfg-ExtractName: illegal params %p, %d", pBuf, bufSize);
}

// ----------------------------------------------------------------------------
// Get TV channel names and frequencies
//
bool WintvCfg_GetFreqTab( char ** ppNameTab, EPGACQ_TUNER_PAR ** ppFreqTab, uint * pCount, char ** ppErrMsg )
{
   const char * pTvAppPath;
   const char * pChanTabPath;
   TVAPP_NAME appIdx;
   bool    result = FALSE;

   appIdx = WintvCfg_GetAppIdx();
   if (appIdx != TVAPP_NONE)
   {
      // keep WIN and UNIX paths separate to support use of multi-boot
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
