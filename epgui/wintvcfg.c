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
 *    This module reads TV programme tables of well-known TV applications.
 *    It's used to extract TV channel lists for the EPG scan and a network
 *    name list for the network name configuration dialog.  See the enum
 *    below for a list of supported TV applications.
 *
 *  Author: Tom Zoerner
 *
 *    Some parser code fragments have been extracted from TV applications,
 *    so their respective copyright applies too. Please see the notes in
 *    functions headers below.
 *
 *  $Id: wintvcfg.c,v 1.24 2005/03/30 14:53:55 tom Exp tom $
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

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "epgdb/epgblock.h"
#include "epgvbi/tvchan.h"
#ifdef WIN32
#include "dsdrv/tvcard.h"
#include "epgvbi/winshm.h"
#include "dsdrv/wintuner.h"
#endif
#include "epgui/epgmain.h"
#include "epgui/wintvcfg.h"


// only used for TV app. simulator: set to FALSE to switch off filtering
// of 0 tuner frequencies and empty channel names
static bool doChanTabFilter;

// ----------------------------------------------------------------------------
// Structure that holds frequency count & table
// - dynamically growing if more channels than fit the default max. table size
//
// default allocation size for frequency table
#define CHAN_CLUSTER_SIZE  50

typedef struct
{
   uint    * pFreqTab;
   uint      maxCount;
   uint      fillCount;
} DYN_FREQ_BUF;

// ----------------------------------------------------------------------------
// File names to load config files of supported TV applications
//
#define REG_KEY_MORETV  "Software\\Feuerstein\\MoreTV"
#define REG_KEY_FREETV  "Software\\Free Software\\FreeTV"
#define REG_KEY_FREETV_CHANTAB  "Software\\Free Software\\FreeTV\\Tuner\\Stations"

#define KTV2_INI_FILENAME         "K!TV.ini"
#define KTV2_CHNTAB_PATH_KEYWORD  "NEXTVIEW_EPG_PATH_CHANNEL"

#ifndef WIN32  // for WIN32 this enum is defined in winshm.h
typedef enum
{
   TVAPP_NONE,
   TVAPP_XAWTV,
   TVAPP_XAWDECODE,
   TVAPP_XDTV,       // former xawdecode
   TVAPP_ZAPPING,
   TVAPP_COUNT
} TVAPP_NAME;
#endif

typedef struct
{
   const char  * pName;
   bool          needPath;
   void       (* pGetStationNames) ( Tcl_Interp * interp, const char * pChanTabPath );
   bool       (* pGetFreqTab) ( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pChanTabPath );
   const char  * pChanTabFile;
} TVAPP_LIST;

// forward declaration;
static const TVAPP_LIST tvAppList[TVAPP_COUNT];

// ----------------------------------------------------------------------------
// Check if a TV app is configured
//
static TVAPP_NAME WintvCfg_GetAppIdx( void )
{
   CONST84 char  * pTvAppIdx;
   int     appIdx;
   TVAPP_NAME result = TVAPP_NONE;

   pTvAppIdx = Tcl_GetVar(interp, "wintvapp_idx", TCL_GLOBAL_ONLY);
   if (pTvAppIdx != NULL)
   {
      if (Tcl_GetInt(interp, pTvAppIdx, &appIdx) == TCL_OK)
      {
         if (appIdx < TVAPP_COUNT)
         {
            result = appIdx;
         }
         else
            debug1("WintvCfg-Check: invalid TV app index %d in Tcl var 'wintvapp_idx'", appIdx);
      }
      else
         debug1("WintvCfg-Check: could not parse integer value in 'wintvapp_idx': '%s'", pTvAppIdx);
   }
   else
      debug0("WintvCfg-Check: Tcl var 'wintvapp_idx' not defined");

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
// - string must be freed by caller!
//
static char * WintvCfg_GetPath( const char * pBase, TVAPP_NAME appIdx )
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
// Helper func to append frequency values to a growing list
// - the list grows automatically when required
//
static void WintvCfg_AddFreqToBuf( DYN_FREQ_BUF * pFreqBuf, uint freq )
{
   uint * pOldFreqTab;

   assert(pFreqBuf->fillCount <= pFreqBuf->maxCount);

   // ignore zero frequencies (e.g. the TV app channel table may contain non-tuner input sources)
   if ( (freq != 0) || (doChanTabFilter == FALSE) )
   {
      if (pFreqBuf->fillCount == pFreqBuf->maxCount)
      {
         pOldFreqTab = pFreqBuf->pFreqTab;
         pFreqBuf->maxCount += CHAN_CLUSTER_SIZE;
         pFreqBuf->pFreqTab = xmalloc(pFreqBuf->maxCount * sizeof(uint));

         if (pOldFreqTab != NULL)
         {
            memcpy(pFreqBuf->pFreqTab, pOldFreqTab, pFreqBuf->fillCount * sizeof(uint));
            xfree(pOldFreqTab);
         }
      }

      pFreqBuf->pFreqTab[pFreqBuf->fillCount] = freq;
      pFreqBuf->fillCount += 1;
   }
}

// ----------------------------------------------------------------------------
// Helper func to add a channel name to the Tcl result list
//
static void WintvCfg_AddChannelName( Tcl_Interp * interp, char * pName )
{
   char *ps, *pc, *pe, c;

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
         *pe = '\0';
         pe--;
      }
   }

   if ( (*pName != 0) || (doChanTabFilter == FALSE) )
   {
      // append the name as-is to the result
      Tcl_AppendElement(interp, pName);

      if (doChanTabFilter)
      {
         // split multi-channel names at separator '/' and append each segment separately
         ps = pName;
         pe = strchr(ps, '/');
         if (pe != NULL)
         {
            do
            {
               pc = pe;
               // remove trailing white space
               while ((pc > ps) && (*(pc - 1) == ' '))
                   pc--;
               if (pc > ps)
               {
                  c = *pc;
                  *pc = 0;
                  Tcl_AppendElement(interp, ps);
                  *pc = c;
               }
               ps = pe + 1;
               // skip whitespace following the separator
               while (*ps == ' ')
                   ps++;
            }
            while ((pe = strchr(ps, '/')) != NULL);

            // add the segment after the last separator
            while (*ps == ' ')
               ps++;
            if (*ps != 0)
               Tcl_AppendElement(interp, ps);
         }
      }
   }
}

// ----------------------------------------------------------------------------
// Checks if the given time is outside or air times restrictions for the given CNI
// - returns TRUE if the time is inside contraints
//
bool WintvCfg_CheckAirTimes( uint cni )
{
   const char * pAirStr;
   char  cnibuf[12];
   time_t now;
   uint  nowMoD, startMoD, stopMoD;
   struct tm * pTm;
   bool  result = TRUE;

   sprintf(cnibuf, "0x%04X", cni);
   pAirStr = Tcl_GetVar2(interp, "cfnettimes", cnibuf, TCL_GLOBAL_ONLY);
   if (pAirStr != NULL)
   {
      if (sscanf(pAirStr, "%u,%u", &startMoD, &stopMoD) == 2)
      {
         now = time(NULL);
         pTm = localtime(&now);
         nowMoD = pTm->tm_min + (pTm->tm_hour * 60);

         if (startMoD < stopMoD)
         {
            result = (nowMoD >= startMoD) && (nowMoD < stopMoD);
         }
         else if (startMoD > stopMoD)
         {
            result = (nowMoD >= startMoD) || (nowMoD < stopMoD);
         }
         // else: start==stop: not filtered
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Iterate across multiple network names in a channel name, separated by slashes
// - gets pointer to a callback function which searches names in a database
//   (callback required to avoid dependency of this module on the EPG database
//   since the module is also used by tvsim)
//
uint WintvCfg_StationNameToCni( char * pName, uint MapName2Cni(const char * station) )
{
   char * ps, * pe, * pc;
   char  c;
   uint  cni;

   if (pName != NULL)
   {
      // append the name as-is to the result
      cni = MapName2Cni(pName);
      if ((cni != 0) && WintvCfg_CheckAirTimes(cni))
         goto found;

      ps = pName;
      pe = strchr(ps, '/');
      if (pe != NULL)
      {
         do
         {
            pc = pe;
            // remove trailing white space
            while ((pc > ps) && (*(pc - 1) == ' '))
                pc--;
            if (pc > ps)
            {
               c = *pc;
               *pc = 0;

               cni = MapName2Cni(ps);
               if ((cni != 0) && WintvCfg_CheckAirTimes(cni))
                  goto found;
               *pc = c;
            }
            ps = pe + 1;
            // skip whitespace following the separator
            while (*ps == ' ')
                ps++;
         }
         while ((pe = strchr(ps, '/')) != NULL);

         // add the segment after the last separator
         while (*ps == ' ')
            ps++;
         if (*ps != 0)
         {
            cni = MapName2Cni(ps);
            if ((cni != 0) && WintvCfg_CheckAirTimes(cni))
               goto found;
         }
      }
   }
   else
      debug0("WintvCfg-StationNameToCni: illegal NULL param");

   cni = 0;
found:
   return cni;
}


#ifdef WIN32
// ---------------------------------------------------------------------------
// Read frequency table from MoreTV registry keys
//
static bool WintvCfg_GetMoretvFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pChanTabPath )
{
   HKEY  hKey;
   DWORD dwSize, dwType;
   char  keyValStr[15];
   DWORD freq;
   uint  idx;
   BOOL  result;

   if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY_MORETV, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
   {
      idx = 0;
      while(TRUE)
      {
         idx += 1;
         sprintf(keyValStr, "%02dFrequenz", idx);

         dwSize = sizeof(freq);
         if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, (char *) &freq, &dwSize) == ERROR_SUCCESS) &&
              (dwType == REG_DWORD) && (dwSize == sizeof(freq)) )
         {
            WintvCfg_AddFreqToBuf(pFreqBuf, (freq * 4) / 25);
         }
         else
            break;
      }
      RegCloseKey(hKey);
      result = TRUE;
   }
   else
   {  // registry key not found -> warn the user
      eval_check(interp, "tk_messageBox -type ok -icon error -message {"
                         "MoreTV channel table not found in the registry. "
                         "Sorry, you'll have to configure a different TV application.}");
      result = FALSE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Read channel names from MoreTV registry keys
//
static void WintvCfg_GetMoretvStationNames( Tcl_Interp * interp, const char * pChanTabPath )
{
   HKEY  hKey;
   DWORD dwSize, dwType;
   uint  idx;
   char  keyValStr[15];
   char  name_buf[100];

   if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY_MORETV, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
   {
      idx = 0;
      while(TRUE)
      {
         idx += 1;
         sprintf(keyValStr, "%02dSendername", idx);

         dwSize = sizeof(name_buf) - 1;
         if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, name_buf, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ) )
         {
            if ((dwSize == 0) || (strcmp(name_buf, "---") == 0))
            {  // empty channel name - set to NULL string
               name_buf[0] = 0;
            }
            WintvCfg_AddChannelName(interp, name_buf);
         }
         else
            break;
      }
      RegCloseKey(hKey);
   }
}

// ---------------------------------------------------------------------------
// Read channel names from FreeTV registry keys
//
static bool WintvCfg_GetFreetvFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pChanTabPath )
{
   HKEY  hKey;
   DWORD dwSize, dwType;
   char  keyValStr[15];
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
            sprintf(keyValStr, "Frequency%d", chanIdx);

            dwSize = sizeof(freq);
            if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, (char *) &freq, &dwSize) == ERROR_SUCCESS) &&
                 (dwType == REG_DWORD) && (dwSize == sizeof(freq)) )
            {
               WintvCfg_AddFreqToBuf(pFreqBuf, (freq * 2) / 125);
            }
         }
         result = TRUE;
      }
      RegCloseKey(hKey);
   }

   if (result == FALSE)
   {
      eval_check(interp, "tk_messageBox -type ok -icon error -message {"
                         "FreeTV channel table not found in the registry. "
                         "Sorry, you'll have to configure a different TV application.}");
   }
   return result;
}

// ---------------------------------------------------------------------------
// Read channel names from FreeTV registry keys
//
static void WintvCfg_GetFreetvStationNames( Tcl_Interp * interp, const char * pChanTabPath )
{
   HKEY  hKey;
   DWORD dwSize, dwType;
   char  name_buf[100];
   char  keyValStr[15];
   uint  chanCount;
   uint  chanIdx;

   if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY_FREETV_CHANTAB, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
   {
      dwSize = sizeof(chanCount);
      if ( (RegQueryValueEx(hKey, "Count", 0, &dwType, (char *) &chanCount, &dwSize) == ERROR_SUCCESS) &&
           (dwType == REG_DWORD) && (dwSize == sizeof(chanCount)) )
      {
         for (chanIdx = 1; chanIdx <= chanCount; chanIdx++)
         {
            sprintf(keyValStr, "Name%d", chanIdx);

            dwSize = sizeof(name_buf) - 1;
            if ( (RegQueryValueEx(hKey, keyValStr, 0, &dwType, name_buf, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ) )
            {
               if (dwSize == 0)
               {  // empty channel name - set to NULL string
                  name_buf[0] = 0;
               }
               WintvCfg_AddChannelName(interp, name_buf);
            }
         }
      }
      RegCloseKey(hKey);
   }
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
static bool WintvCfg_GetMultidecFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pChanTabPath )
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
                  WintvCfg_AddFreqToBuf(pFreqBuf, Programm.freq * 2 / 125);
               }
            }
            else
            {  // old version -> restart at file offset 0
               _lseek(fd, 0, SEEK_SET);
               while (read(fd, &ProgrammAlt, sizeof(ProgrammAlt)) == sizeof(ProgrammAlt))
               {
                  WintvCfg_AddFreqToBuf(pFreqBuf, ProgrammAlt.freq * 2 / 125);
               }
            }
         }
         close(fd);

         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         sprintf(comm, "tk_messageBox -type ok -icon error -message {"
                       "Could not open the MultiDec channel table '%s': %s."
                       "Check your settings in the TV interaction configuration dialog.}",
                       pChanTabPath, strerror(errno));
         eval_check(interp, comm);
      }
   }

   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the TV app configuration file
// - code taken from multidec 6.4 by echter_espresso@hotmail.com
//
static void WintvCfg_GetMultidecStationNames( Tcl_Interp * interp, const char * pChanTabPath )
{
   struct TProgrammAlt ProgrammAlt;
   struct TProgramm    Programm;
   size_t len;
   int    fd;

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
                  Programm.Name[sizeof(Programm.Name) - 1] = 0;
                  WintvCfg_AddChannelName(interp, Programm.Name);
               }
            }
            else
            {  // old version -> restart at file offset 0
               _lseek(fd, 0, SEEK_SET);
               while (read(fd, &ProgrammAlt, sizeof(ProgrammAlt)) == sizeof(ProgrammAlt))
               {
                  Programm.Name[sizeof(Programm.Name) - 1] = 0;
                  WintvCfg_AddChannelName(interp, Programm.Name);
               }
            }
         }
         close(fd);
      }
   }
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the K!TV channel table
// - Parser code originally derived from QuenotteTV_XP_1103_Sources.zip; updated for 2.3
//   K!TV is Copyright (c) 2000-2005 Quenotte
//
static bool WintvCfg_GetKtvFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pChanTabPath )
{
   char   sbuf[256];
   char   value[100];
   FILE * fp;
   long   freq;
   bool   currentSkipped;
   bool   skipNext;
   bool   result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         currentSkipped = FALSE;
         skipNext = FALSE;

         while (fgets(sbuf, sizeof(sbuf) - 1, fp) != NULL)
         {
            // K!TV version < 2
            if (sscanf(sbuf, "Freq: %ld", &freq) == 1)
            {
               WintvCfg_AddFreqToBuf(pFreqBuf, freq * 2 / 125);
            }
            // K!TV version 2
            else if (sscanf(sbuf, "frequency %*[=:] %ld", &freq) >= 1)
            {
               if (skipNext == FALSE)
               {
                  freq /= 1000;
                  WintvCfg_AddFreqToBuf(pFreqBuf, freq * 2 / 125);
               }
            }
            else if (sscanf(sbuf, "name %*[=:] %99[^\n]", value) >= 1)
            {
               if ( (currentSkipped == FALSE) && (strcasecmp(value, "Current") == 0) )
               {
                  currentSkipped = TRUE;
                  skipNext = TRUE;
               }
               else
                  skipNext = FALSE;
            }
         }
         fclose(fp);

         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         sprintf(comm, "tk_messageBox -type ok -icon error -message {"
                       "Could not open the K!TV channel table '%s': %s."
                       "Check your settings in the TV interaction configuration dialog.}",
                       pChanTabPath, strerror(errno));
         eval_check(interp, comm);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the K!TV channel table
// - Parser code for K!TV 2.3
//   K!TV Copyright (c) 2000-2005 by Quenotte
//
static void WintvCfg_GetKtvStationNames( Tcl_Interp * interp, const char * pChanTabPath )
{
   char   sbuf[256];
   char   value[100];
   FILE * fp = NULL;
   bool   currentSkipped = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         while (fgets(sbuf, sizeof(sbuf) - 1, fp) != NULL)
         {
            if (sscanf(sbuf, "name %*[=:] %99[^\n]", value) >= 1)
            {
               if ( (currentSkipped == FALSE) && (strcasecmp(value, "Current") == 0) )
               {
                  currentSkipped = TRUE;
               }
               else
               {
                  WintvCfg_AddChannelName(interp, value);
               }
            }
         }
         fclose(fp);
      }
   }
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
static bool WintvCfg_GetDscalerFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pChanTabPath )
{
   char sbuf[256];
   uint   freq;
   FILE * fp;
   char * eol_ptr;
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         while (feof(fp) == FALSE)
         {
            sbuf[0] = '\0';

            fgets(sbuf, 255, fp);

            eol_ptr = strstr(sbuf, ";");
            if (eol_ptr == NULL)
               eol_ptr = strstr(sbuf, "\n");
            if (eol_ptr != NULL)
               *eol_ptr = '\0';

            // cope with old style frequencies
            if (strnicmp(sbuf, "Freq:", 5) == 0)
            {
               freq = (uint) atol(sbuf + 5);
               freq = (freq * 16) / 1000;  // MulDiv WTF!?
               WintvCfg_AddFreqToBuf(pFreqBuf, freq);
            }
            else if (strnicmp(sbuf, "Freq2:", 6) == 0)
            {
               freq = atol(sbuf + 6);
               WintvCfg_AddFreqToBuf(pFreqBuf, freq);
            }
         }
         fclose(fp);

         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         sprintf(comm, "tk_messageBox -type ok -icon error -message {"
                       "Could not open the DScaler channel table '%s': %s."
                       "Check your settings in the TV interaction configuration dialog.}",
                       pChanTabPath, strerror(errno));
         eval_check(interp, comm);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the TV app configuration file
// - Parser code taken from DScaler 3.10  (see http://dscaler.org/)
//   Copyright: 9 Novemeber 2000 - Michael Eskin, Conexant Systems
//
static void WintvCfg_GetDscalerStationNames( Tcl_Interp * interp, const char * pChanTabPath )
{
   char sbuf[256];
   FILE * fp;
   char * eol_ptr;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
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
               WintvCfg_AddChannelName(interp, sbuf + 5);
            }
         }
         fclose(fp);
      }
   }
}
#else  // not WIN32

// ----------------------------------------------------------------------------
// Extract all TV frequencies & norms from $HOME/.xawtv for the EPG scan
//
static bool WintvCfg_GetXawtvFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pChanTabPath )
{
   char line[256], tag[64], value[192];
   uint defaultNorm, attrNorm;
   sint defaultFine, attrFine;
   bool defaultIsTvInput, isTvInput;
   bool isDefault;
   uint freq;
   FILE * fp;
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         isDefault = TRUE;
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
               dprintf4("Xawtv channel: freq=%d fine=%d norm=%d, isTvInput=%d\n", freq, attrFine, attrNorm, isTvInput);
               if ((freq != 0) && (isDefault == FALSE) && isTvInput)
                  WintvCfg_AddFreqToBuf(pFreqBuf, (attrNorm << 24) | (freq + attrFine));

               // initialize variables for the new section
               freq = 0;
               attrFine = defaultFine;
               attrNorm = defaultNorm;
               isTvInput = defaultIsTvInput;
               dprintf1("Xawtv channel: name=%s\n", value);
               isDefault = ((strcmp(value, "defaults") == 0) || (strcmp(value, "global") == 0));
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
                  if (isDefault)
                     defaultNorm = attrNorm;
               }
               else if (strcasecmp(tag, "fine") == 0)
               {
                  attrFine = atoi(value);
                  if (isDefault)
                     defaultFine = attrFine;
               }
               else if (strcasecmp(tag, "isTvInput") == 0)
               {
                  isTvInput = (strcasecmp(value, "television") == 0);
                  if (isDefault)
                     defaultIsTvInput = isTvInput;
               }
            }
            else
               debug1("Xawtv-GetFreqTab: parse error line \"%s\"", line);
         }
         fclose(fp);

         // add the last section's data to the output list
         if ((freq != 0) && (isDefault == FALSE) && isTvInput)
         {
            dprintf4("Xawtv channel: freq=%d fine=%d norm=%d, isTvInput=%d\n", freq, attrFine, attrNorm, isTvInput);
            WintvCfg_AddFreqToBuf(pFreqBuf, (attrNorm << 24) | (freq + attrFine));
         }

         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         sprintf(comm, "tk_messageBox -type ok -icon error -message {"
                       "Could not open channel table '%s': %s."
                       "Check your settings in the TV interaction configuration dialog.}",
                       pChanTabPath, strerror(errno));
         eval_check(interp, comm);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the .xawtv configuration file
// - the parsing pattern used here is lifted directly from xawtv-3.41
// - station names are defined on single lines in brackets, e.g. [ARD]
// - names containing '/' characters are assumed to be multi-network channels,
//   e.g. "[arte / kinderkanal]". Those are broken up around the separator and
//   each segment added separately to the output list
//
static void WintvCfg_GetXawtvStationNames( Tcl_Interp * interp, const char * pChanTabPath )
{
   char line[256], section[100];
   FILE * fp;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         // read all lines of the file (ignoring section structure)
         while (fgets(line, 255, fp) != NULL)
         {
            // search for section headers, i.e. "[station]"
            if (sscanf(line,"[%99[^]]]", section) == 1)
            {
               if ( (strcmp(section, "global") != 0) &&
                    (strcmp(section, "defaults") != 0) &&
                    (strcmp(section, "launch") != 0) )
               {
                  WintvCfg_AddChannelName(interp, section);
               }
            }
         }
         fclose(fp);
      }
   }
}

// ----------------------------------------------------------------------------
// Parse zapping config for channel names or frequencies
// - used both to parse for frequencies as well as for names: is freq buffer
//   pointer is NULL, names scan is assumed
//
static void WintvCfg_ParseZappingConf( Tcl_Interp * interp, FILE * fp, DYN_FREQ_BUF * pFreqBuf )
{
#define BUFLEN 256
   char line[BUFLEN + 1], tag[64];
   char * endp;
   uint subTreeLevel;
   sint len;
   long freq;
   long norm;

   subTreeLevel = 0;
   freq = 0;
   norm = 0;

   // read all lines of the file (ignoring section structure)
   while (fgets(line, BUFLEN, fp) != NULL)
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
      }
      else if ( (subTreeLevel == 2) &&
                (sscanf(line," < subtree label = \"%63[^\"]\" %*[^>]>%n", tag, &len) == 1) &&
                (strcasecmp(tag, "name") == 0) )
      {
         if (pFreqBuf == NULL)
         {
            endp = strchr(line + len, '<');
            if (endp != NULL)
            {
               *endp = 0;
               dprintf1("Zapping channel name '%s'\n", line + len);
               WintvCfg_AddChannelName(interp, line + len);
            }
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

         if ( (subTreeLevel == 1) && (freq != 0) && (pFreqBuf != NULL) )
         {
            dprintf2("Zapping channel: freq=%d norm=%d\n", freq, norm);
            WintvCfg_AddFreqToBuf(pFreqBuf, (norm << 24) | (freq * 16/1000));
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
static bool WintvCfg_GetZappingFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pChanTabPath )
{
   FILE * fp;
   bool result = FALSE;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         WintvCfg_ParseZappingConf(interp, fp, pFreqBuf);

         fclose(fp);
         result = TRUE;
      }
      else
      {  // file open failed -> warn the user
         sprintf(comm, "tk_messageBox -type ok -icon error -message {"
                       "Could not open channel table '%s': %s."
                       "Check your settings in the TV interaction configuration dialog.}",
                       pChanTabPath, strerror(errno));
         eval_check(interp, comm);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the zapping configuration file
//
static void WintvCfg_GetZappingStationNames( Tcl_Interp * interp, const char * pChanTabPath )
{
   FILE * fp;

   if (pChanTabPath != NULL)
   {
      fp = fopen(pChanTabPath, "r");
      if (fp != NULL)
      {
         WintvCfg_ParseZappingConf(interp, fp, NULL);

         fclose(fp);
      }
   }
}
#endif  // not WIN32

static const TVAPP_LIST tvAppList[TVAPP_COUNT] =
{
   { "none",     FALSE, NULL, NULL, "" },
#ifdef WIN32
   { "DScaler",  TRUE,  WintvCfg_GetDscalerStationNames,  WintvCfg_GetDscalerFreqTab,  "program.txt" },
   { "K!TV",     TRUE,  WintvCfg_GetKtvStationNames,      WintvCfg_GetKtvFreqTab,      "program.set" },
   { "MultiDec", TRUE,  WintvCfg_GetMultidecStationNames, WintvCfg_GetMultidecFreqTab, "Programm.set" },
   { "MoreTV",   FALSE, WintvCfg_GetMoretvStationNames,   WintvCfg_GetMoretvFreqTab,   "" },
   { "FreeTV",   FALSE, WintvCfg_GetFreetvStationNames,   WintvCfg_GetFreetvFreqTab,   "" },
#else
   { "Xawtv",    FALSE, WintvCfg_GetXawtvStationNames,    WintvCfg_GetXawtvFreqTab,    ".xawtv" },
   { "XawDecode",FALSE, WintvCfg_GetXawtvStationNames,    WintvCfg_GetXawtvFreqTab,    ".xawdecode/xawdecoderc" },
   { "XdTV",     FALSE, WintvCfg_GetXawtvStationNames,    WintvCfg_GetXawtvFreqTab,    ".xdtv/xdtvrc" },
   { "Zapping",  FALSE, WintvCfg_GetZappingStationNames,  WintvCfg_GetZappingFreqTab,  ".zapping/zapping.conf" },
#endif
};

// ----------------------------------------------------------------------------
// Return list of names of supported TV applications
//
static int WintvCfg_GetTvappList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   Tcl_Obj * pResultList;
   uint  idx;
   int   result;

   if (objc != 1)
   {  // parameter count is invalid
      #if (DEBUG_SWITCH_TCL_BGERR == ON)
      Tcl_SetResult(interp, "C_Tvapp_GetTvappList: no parameters expected", TCL_STATIC);
      #endif
      result = TCL_ERROR;
   }
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);

      for (idx=0; idx < TVAPP_COUNT; idx++)
      {
         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(tvAppList[idx].pName, -1));
      }
      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Return default TV application
// - searches for the first TV app in table for which a config file is present
//
static int WintvCfg_GetDefaultApp( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
#ifndef WIN32
   const char * pChanTabPath;
   struct stat fstat;
   time_t max_ts;
   uint   max_idx;
   uint   appIdx;
#endif
   int   result;

   if (objc != 1)
   {  // parameter count is invalid
      #if (DEBUG_SWITCH_TCL_BGERR == ON)
      Tcl_SetResult(interp, "C_Tvapp_GetDefaultApp: no parameters expected", TCL_STATIC);
      #endif
      result = TCL_ERROR;
   }
   else
   {
#ifndef WIN32
      max_idx = TVAPP_NONE;
      max_ts = 0;
      for (appIdx=0; appIdx < TVAPP_COUNT; appIdx++)
      {
         pChanTabPath = WintvCfg_GetPath(NULL, appIdx);
         if (pChanTabPath != NULL)
         {
            if ( (stat(pChanTabPath, &fstat) == 0) &&
                 S_ISREG(fstat.st_mode) &&
                 (fstat.st_mtime > max_ts) )
            {
               max_ts  = fstat.st_mtime;
               max_idx = appIdx;
            }
            xfree((void *)pChanTabPath);
         }
      }

      if (max_idx != TVAPP_NONE)
      {
         Tcl_SetObjResult(interp, Tcl_NewIntObj(max_idx));
      }
      else
#endif
         Tcl_SetObjResult(interp, Tcl_NewIntObj(0));

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Extract channel names listed in TV app configuration file
//
static int WintvCfg_GetStationNames( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_GetStationNames";
   const char * pTvAppPath;
   const char * pChanTabPath;
   int     appIdx;
   int     result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      appIdx = WintvCfg_GetAppIdx();
      if (appIdx != TVAPP_NONE)
      {
         pTvAppPath   = Tcl_GetVar(interp, "wintvapp_path", TCL_GLOBAL_ONLY);
         pChanTabPath = WintvCfg_GetPath(pTvAppPath, appIdx);
         if (pChanTabPath != NULL)
         {
            tvAppList[appIdx].pGetStationNames(interp, pChanTabPath);

            xfree((void *)pChanTabPath);
         }
      }

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Extract all tuner frequencies from th TV app's channel table
//
bool WintvCfg_GetFreqTab( Tcl_Interp * interp, uint ** ppFreqTab, uint * pCount )
{
   DYN_FREQ_BUF freqBuf;
   const char * pTvAppPath;
   const char * pChanTabPath;
   int     appIdx;
   bool    result = FALSE;

   appIdx = WintvCfg_GetAppIdx();
   if (appIdx != TVAPP_NONE)
   {
      pTvAppPath   = Tcl_GetVar(interp, "wintvapp_path", TCL_GLOBAL_ONLY);

      if ( (tvAppList[appIdx].needPath == FALSE) ||
           ((pTvAppPath != NULL) && (pTvAppPath[0] != 0)) )
      {
         pChanTabPath = WintvCfg_GetPath(pTvAppPath, appIdx);

         memset(&freqBuf, 0, sizeof(freqBuf));
         if (tvAppList[appIdx].pGetFreqTab(interp, &freqBuf, pChanTabPath))
         {
            if (freqBuf.fillCount == 0)
            {  // no channel assignments found in the file -> warn the user and abort
               sprintf(comm, "tk_messageBox -type ok -icon error -message {"
                             "No channel assignments found. Please disable option 'Use %s'}",
                             tvAppList[appIdx].pName);
               eval_check(interp, comm);

               if (freqBuf.pFreqTab != NULL)
                  xfree(freqBuf.pFreqTab);
               freqBuf.pFreqTab = NULL;
            }

            *ppFreqTab = freqBuf.pFreqTab;
            *pCount    = freqBuf.fillCount;
            result = TRUE;
         }
         // else: file open failed -> user already informed

         if (pChanTabPath != NULL)
            xfree((void *)pChanTabPath);
      }
      else
      {  // no TV app dir specified -> abort with error msg
         sprintf(comm, "tk_messageBox -type ok -icon error -message {"
                       "No directory was specified where %s is installed."
                       "Check your settings in the TV interaction configuration dialog.}",
                       tvAppList[appIdx].pName);
         eval_check(interp, comm);
      }
   }
   else
   {  // internal error - caller should have checked before if TV app is configured
      debug0("WintvCfg-GetFreqTab: no TV app configured");
   }

   return result;
}

// ----------------------------------------------------------------------------
// Load configuration params from TV app ini file into the dialog
//
static int WintvCfg_CfgNeedsPath( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   int   appIdx;
   bool  needPath;
   int   result;

   if ( (objc != 2) ||
        (Tcl_GetIntFromObj(interp, objv[1], &appIdx) != TCL_OK) )
   {  // parameter count is invalid
      #if (DEBUG_SWITCH_TCL_BGERR == ON)
      Tcl_SetResult(interp, "Usage C_Tvapp_CfgNeedsPath: <tvAppIdx>", TCL_STATIC);
      #endif
      result = TCL_ERROR;
   }
   else
   {
      if (appIdx < TVAPP_COUNT)
      {
         needPath = tvAppList[appIdx].needPath;
      }
      else
         needPath = FALSE;

      Tcl_SetObjResult(interp, Tcl_NewBooleanObj(needPath));

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Tcl callback to check if a TV app is configured
//
static int WintvCfg_Enabled( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_Enabled";
   bool is_enabled;
   int result;

   if (objc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      is_enabled = (WintvCfg_GetAppIdx() != TVAPP_NONE);

      Tcl_SetObjResult(interp, Tcl_NewBooleanObj(is_enabled));
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Load configuration params from TV app ini file into the TV card dialog
// - the parameters are not applies to the driver yet, only loaded into Tcl vars
//
static int WintvCfg_TestChanTab( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   DYN_FREQ_BUF freqBuf;
   const char * pChanTabPath;
   int    newAppIdx;
   int    chnCount;
   int    result;

   if ( (objc != 3) ||
        (Tcl_GetIntFromObj(interp, objv[1], &newAppIdx) != TCL_OK) )
   {  // parameter count is invalid
      #if (DEBUG_SWITCH_TCL_BGERR == ON)
      Tcl_SetResult(interp, "Usage C_Tvapp_TestChanTab: <tvAppIdx> <path>", TCL_STATIC);
      #endif
      result = TCL_ERROR;
   }
   else
   {
      chnCount = -1;

      if (newAppIdx == TVAPP_NONE)
      {  // no TV app selected yet (i.e. option "none" selected)
         eval_check(interp,
            "tk_messageBox -type ok -icon error -parent .xawtvcf -message {"
               "Please select a TV application from which to load the channel table.}");
      }
      else if (newAppIdx < TVAPP_COUNT)
      {
         if ((tvAppList[newAppIdx].needPath == FALSE) || (Tcl_GetString(objv[2]) != NULL))
         {
            memset(&freqBuf, 0, sizeof(freqBuf));

            pChanTabPath = WintvCfg_GetPath(Tcl_GetString(objv[2]), newAppIdx);
            if ( (pChanTabPath != NULL) &&
                 (tvAppList[newAppIdx].pGetFreqTab(interp, &freqBuf, pChanTabPath)) )
            {
               // sucessfully opened: return number of found names
               chnCount = freqBuf.fillCount;
            }
            // else: file open failed -> user already informed

            if (freqBuf.pFreqTab != NULL)
               xfree(freqBuf.pFreqTab);
            if (pChanTabPath != NULL)
               xfree((void *)pChanTabPath);
         }
         else
         {  // no TV app directory specified -> abort with error msg
            sprintf(comm, "tk_messageBox -type ok -icon error -parent .xawtvcf -message {"
                          "You must specify the directory where %s is installed.}",
                          tvAppList[newAppIdx].pName);
            eval_check(interp, comm);
         }
      }
      else
         debug1("WintvCfg-TestChanTab: illegal app idx %d", newAppIdx);

      Tcl_SetObjResult(interp, Tcl_NewIntObj(chnCount));
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Shut the module down: free resources
//
void WintvCfg_Destroy( void )
{
}

// ----------------------------------------------------------------------------
// Initialize the module
//
void WintvCfg_Init( bool enableChanTabFilter )
{
   doChanTabFilter = enableChanTabFilter;

   Tcl_CreateObjCommand(interp, "C_Tvapp_Enabled", WintvCfg_Enabled, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_GetStationNames", WintvCfg_GetStationNames, (ClientData) NULL, NULL);

   Tcl_CreateObjCommand(interp, "C_Tvapp_GetTvappList", WintvCfg_GetTvappList, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_TestChanTab", WintvCfg_TestChanTab, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_CfgNeedsPath", WintvCfg_CfgNeedsPath, (ClientData) NULL, NULL);
   Tcl_CreateObjCommand(interp, "C_Tvapp_GetDefaultApp", WintvCfg_GetDefaultApp, (ClientData) NULL, NULL);

   // pass the name of the configured (not the actually connected) app to GUI
   eval_check(interp, "UpdateTvappName");
}

