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
 *    This module reads channel tables, INI files and registry of well-known
 *    Windows TV applications.  It's used to extract TV card configuration
 *    parameters as well as a frequency list for the EPG scan and a network
 *    name list for the network name configuration dialog.
 *
 *    See the enum below for a list of supported TV applications.  Not all
 *    of them are supported equally well (in particular closed-sources ones
 *    are not supported well very in regard to their TV card config).
 *
 *  Author: Tom Zoerner
 *
 *    Some parser code fragments have been extracted from TV applications,
 *    so their respective copyright applies too. Please see the notes in
 *    functions headers below.
 *
 *  $Id: wintvcfg.c,v 1.12 2002/12/01 15:26:12 tom Exp tom $
 */

#ifndef WIN32
#error "This module is intended only for Win32 systems"
#else

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <windows.h>
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
#include "epgvbi/wintuner.h"
#include "epgvbi/winshm.h"
#include "epgdb/epgblock.h"
#include "epgui/epgmain.h"
#include "epgui/wintvcfg.h"


// only used for TV app. simulator: set to FALSE to switch off filtering
// of 0 tuner frequencies and empty channel names
static bool doChanTabFilter;

// ----------------------------------------------------------------------------
// File names to load config files of supported TV applications
//
#define INI_FILE_DSCALER         "DScaler.ini"
#define INI_FILE_KTV             "k!tv-xp.ini"
#define INI_FILE_MULTIDEC        "Multidec.ini"

#define CHANTAB_FILE_DSCALER     "program.txt"
#define CHANTAB_FILE_KTV         "program.set"
#define CHANTAB_FILE_MULTIDEC    "Programm.set"

#define REG_KEY_MORETV  "Software\\Feuerstein\\MoreTV"
#define REG_KEY_FREETV  "Software\\Free Software\\FreeTV"
#define REG_KEY_FREETV_CHANTAB  "Software\\Free Software\\FreeTV\\Tuner\\Stations"


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

// ----------------------------------------------------------------------------
// Get path of the TV app configuration file
// - string must be freed by caller!
//
static char * WintvCfg_GetPath( const char * pBase, const char * pFileName )
{
   char * pPath = NULL;

   if (pFileName != NULL)
   {
      if ((pBase != NULL) && (*pBase != 0))
      {
         pPath = xmalloc(strlen(pBase) + strlen(pFileName) + 2);
         strcpy(pPath, pBase);
         strcat(pPath, "/");
         strcat(pPath, pFileName);
      }
      else
         debug0("WintvCfg-GetPath: no path defined for TV app");
   }
   else
      fatal0("WintvCfg-GetPath: illegal NULL ptr param");

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

// ---------------------------------------------------------------------------
// Read frequency table from MoreTV registry keys
//
static bool WintvCfg_GetMoretvFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pTvappPath )
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
static void WintvCfg_GetMoretvStationNames( Tcl_Interp * interp, const char * pTvappPath )
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
static bool WintvCfg_GetFreetvFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pTvappPath )
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
static void WintvCfg_GetFreetvStationNames( Tcl_Interp * interp, const char * pTvappPath )
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
static bool WintvCfg_GetMultidecFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pTvappPath )
{
   struct TProgrammAlt ProgrammAlt;
   struct TProgramm    Programm;
   char * pPath;
   size_t len;
   int  fd;
   bool result = FALSE;

   pPath = WintvCfg_GetPath(pTvappPath, CHANTAB_FILE_MULTIDEC);
   if (pPath != NULL)
   {
      fd = open(pPath, _O_RDONLY | _O_BINARY);
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
                       pPath, strerror(errno));
         eval_check(interp, comm);
      }
      xfree(pPath);
   }

   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the TV app configuration file
// - code taken from multidec 6.4 by echter_espresso@hotmail.com
//
static void WintvCfg_GetMultidecStationNames( Tcl_Interp * interp, const char * pTvappPath )
{
   struct TProgrammAlt ProgrammAlt;
   struct TProgramm    Programm;
   char * pPath;
   size_t len;
   int    fd;

   pPath = WintvCfg_GetPath(pTvappPath, CHANTAB_FILE_MULTIDEC);
   if (pPath != NULL)
   {
      fd = open(pPath, _O_RDONLY | _O_BINARY);
      if (fd != -1)
      {
         len = read(fd, &Programm, sizeof(Programm));
         if (len == sizeof(Programm))
         {
            if (strcmp(Programm.Name, "MultiDec 6.1") == 0)
            {
               while (read(fd, &Programm, sizeof(Programm)) == sizeof(Programm))
               {
                  WintvCfg_AddChannelName(interp, Programm.Name);
               }
            }
            else
            {  // old version -> restart at file offset 0
               _lseek(fd, 0, SEEK_SET);
               while (read(fd, &ProgrammAlt, sizeof(ProgrammAlt)) == sizeof(ProgrammAlt))
               {
                  WintvCfg_AddChannelName(interp, Programm.Name);
               }
            }
         }
         close(fd);
      }
      xfree(pPath);
   }
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the K!TV channel table
// - Parser code derived from QuenotteTV_XP_1103_Sources.zip
//   K!TV is Copyright (c) 2000-2002 Quenotte
//
static bool WintvCfg_GetKtvFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pTvappPath )
{
   char   sbuf[256];
   char * pPath;
   FILE * fp;
   long   freq;
   bool   result = FALSE;

   pPath = WintvCfg_GetPath(pTvappPath, CHANTAB_FILE_KTV);
   if (pPath != NULL)
   {
      fp = fopen(pPath, "r");
      if (fp != NULL)
      {
         while (fgets(sbuf, sizeof(sbuf) - 1, fp) != NULL)
         {
            if (sscanf(sbuf, "Freq: %ld", &freq) == 1)
            {
               WintvCfg_AddFreqToBuf(pFreqBuf, freq * 2 / 125);
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
                       pPath, strerror(errno));
         eval_check(interp, comm);
      }
      xfree(pPath);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the K!TV channel table
// - Parser code derived from QuenotteTV_XP_1103_Sources.zip
//   K!TV is Copyright (c) 2000-2002 Quenotte
//
static void WintvCfg_GetKtvStationNames( Tcl_Interp * interp, const char * pTvappPath )
{
   char   sbuf[256];
   char * pPath;
   FILE * fp;
   int    ch;
   uint   j;

   pPath = WintvCfg_GetPath(pTvappPath, CHANTAB_FILE_KTV);
   if (pPath != NULL)
   {
      fp = fopen(pPath, "r");
      if (fp != NULL)
      {
         while (fscanf(fp, "%255s ", sbuf) != EOF)
         {
            if (strnicmp(sbuf, "Name:", 5) == 0)
            {
               j = 0;
               while ((j < sizeof(sbuf) - 1) && ((ch = getc(fp)) != '\n'))
               {
                  if (ch == EOF)
                     goto error;

                   if (ch != '\r')
                      sbuf[j++] = (char)ch;
               }
               sbuf[j] = '\0';

               WintvCfg_AddChannelName(interp, sbuf);
            }
            else
            {  // not a name tag -> consume the remaining chars on the line
               if (fgets(sbuf, sizeof(sbuf) - 1, fp) == NULL)
                  goto error;
            }
         }

         error:
         fclose(fp);
      }
      xfree(pPath);
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
static bool WintvCfg_GetDscalerFreqTab( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pTvappPath )
{
   char sbuf[256];
   uint   freq;
   char * pPath;
   FILE * fp;
   char * eol_ptr;
   bool result = FALSE;

   pPath = WintvCfg_GetPath(pTvappPath, CHANTAB_FILE_DSCALER);
   if (pPath != NULL)
   {
      fp = fopen(pPath, "r");
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
                       pPath, strerror(errno));
         eval_check(interp, comm);
      }
      xfree(pPath);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Build list of all channel names defined in the TV app configuration file
// - Parser code taken from DScaler 3.10  (see http://dscaler.org/)
//   Copyright: 9 Novemeber 2000 - Michael Eskin, Conexant Systems
//
static void WintvCfg_GetDscalerStationNames( Tcl_Interp * interp, const char * pTvappPath )
{
   char sbuf[256];
   char * pPath;
   FILE * fp;
   char * eol_ptr;

   pPath = WintvCfg_GetPath(pTvappPath, CHANTAB_FILE_DSCALER);
   if (pPath != NULL)
   {
      fp = fopen(pPath, "r");
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
      xfree(pPath);
   }
}

// ----------------------------------------------------------------------------
// Open the INI file at the configured path or issue error message
//
static FILE * WinTvCfg_OpenIni( Tcl_Interp * interp, const char * pPath )
{
   char msg_buf[300];
   FILE * fp = NULL;

   if (pPath != NULL)
   {
      fp = fopen(pPath, "r");
      if (fp == NULL)
      {
         sprintf(msg_buf, "tk_messageBox -type ok -icon error -parent .hwcfg -message {"
                          "Failed to open the INI file '%s': %s}",
                          pPath, strerror(errno));
         eval_check(interp, msg_buf);
      }
   }
   else
      debug0("WinTvCfg-OpenIni: illegal NULL ptr param");

   return fp;
}

// ----------------------------------------------------------------------------
// Update PLL setting in HW config dialog window
//
static int WinTvCfg_UpdatePll( Tcl_Interp * interp, int pllType, const char * pSettingName )
{
   char msg_buf[300];

   if (pllType == -1)
   {  // no PLL defnition found in the INI file or registry
      sprintf(msg_buf, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
              "%s setting not found.\nSorry, you'll have to set it manually.}", pSettingName);
      eval_check(interp, msg_buf);
   }
   else if (pllType > 2)
   {  // illegal PLL setting: only 0,1,2 allowed
      sprintf(msg_buf, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
              "Unsupported %s setting found.\nSorry, you'll have to choose one manually.}", pSettingName);
      eval_check(interp, msg_buf);
      pllType = -1;
   }
   else
   {  // value ok -> assign it to the Tcl variable which is tied to the radio buttons
      sprintf(msg_buf, "%d", pllType);
      Tcl_SetVar(interp, "hwcfg_pll_sel", msg_buf, TCL_GLOBAL_ONLY);
   }
   return pllType;
}

// ----------------------------------------------------------------------------
// Update Tuner setting in HW config dialog window
//
static int WinTvCfg_UpdateTuner( Tcl_Interp * interp, int tunerType, const char * pMapping, int maxTunerType )
{
   char str_buf[300];

   if (tunerType < 0)
   {  // no tuner defnition found in the INI file or registry
      eval_check(interp, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
            "Tuner type setting not found.\nSorry, you'll have to set it manually.}");
   }
   else if ((pMapping != NULL) && (tunerType >= maxTunerType))
   {  // tuner index can not be mapped into nxtvepg's tuner table
      eval_check(interp, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
            "Unknown tuner type found.\nSorry, you'll have to choose one manually.}");
      tunerType = -1;
   }
   else
   {  // index ok -> map the TV app index into nxtvepg's tuner table
      if (pMapping != NULL)
         tunerType = pMapping[tunerType];

      if (tunerType == 0)
      {  // unidentified tuner -> handle just as unknown tuner
         eval_check(interp, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
               "Unknown tuner type found.\nSorry, you'll have to choose one manually.}");
         tunerType = -1;
      }
      else
      {
         // assign the value to the Tcl variable which holds the tuner index during the life-time of the dialog
         sprintf(str_buf, "%d", tunerType);
         Tcl_SetVar(interp, "hwcfg_tuner_sel", str_buf, TCL_GLOBAL_ONLY);

         // update the label in front of the popup menu with the name of the new tuner
         sprintf(str_buf, ".hwcfg.opt2.tuner.curname configure -text \"Tuner: [lindex $hwcfg_tuner_list %d]\"", tunerType);
         eval_check(interp, str_buf);
      }
   }
   return tunerType;
}

// ----------------------------------------------------------------------------
// Update input source setting in HW config dialog window
//
static int WinTvCfg_UpdateInputIdx( Tcl_Interp * interp, int inputIdx )
{
   char str_buf[200];

   if ((inputIdx >= 0) && (inputIdx <= 2))
   {
      // assign the value to the Tcl variable which holds the input index during the life-time of the dialog
      sprintf(str_buf, "%d", inputIdx);
      Tcl_SetVar(interp, "hwcfg_input_sel", str_buf, TCL_GLOBAL_ONLY);

      // update the label in front of the popup menu with the name of the new input source
      sprintf(str_buf, ".hwcfg.opt2.input.curname configure -text \"Video source: [lindex $hwcfg_input_list %d]\"", inputIdx);
      eval_check(interp, str_buf);
   }
   else
      inputIdx = -1;

   return inputIdx;
}

// ----------------------------------------------------------------------------
// Generate summary ofter INI file was (at least partly) successfully loaded
//
static void WintvCfg_IniSummary( Tcl_Interp * interp, int pllType, int tunerType, int inputIdx )
{
   char msg_buf[250];

   sprintf(msg_buf, "tk_messageBox -type ok -icon info -parent .hwcfg -message {"
                       "The following parameters were set: ");
   if (pllType != -1)
      strcat(msg_buf, "PLL, ");
   if (tunerType != -1)
      strcat(msg_buf, "Tuner type, ");
   if (inputIdx != -1)
      strcat(msg_buf, "Input source, ");

   if (msg_buf[strlen(msg_buf) - 2] == ',')
      msg_buf[strlen(msg_buf) - 2] = 0;

   strcat(msg_buf, "}");
   eval_check(interp, msg_buf);
}

// ----------------------------------------------------------------------------
// Definitions for MoreTV INI file
//
// map MoreTV tuner indices to nxtvepg indices
// (since this program is not open source and tuner names are ambiguous,
// I unfortunately could map only a few of the tuners)
static const uchar MoretvTunerMapping[] =
{
   1, 14, 20, 0, 7, 0, 0, 0, 0, 0
};
#define MORETV_TUNER_COUNT (sizeof(MoretvTunerMapping) / sizeof(*MoretvTunerMapping))

// ----------------------------------------------------------------------------
// Read MoreTV or FreeTV config from registry
// 
static bool WintvCfg_GetMoretvIni( Tcl_Interp * interp, TVAPP_NAME appIdx, const char * pTvappPath )
{
   const char * pKeyName;
   HKEY  hKey;
   DWORD dwSize, dwType;
   DWORD pllType, tunerType;
   BOOL  result;

   if (appIdx == TVAPP_MORETV)
      pKeyName = REG_KEY_MORETV;
   else
      pKeyName = REG_KEY_FREETV;

   if (RegOpenKeyEx(HKEY_CURRENT_USER, pKeyName, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
   {
      dwSize = sizeof(pllType);
      if ( (RegQueryValueEx(hKey, "PLL", 0, &dwType, (char *) &pllType, &dwSize) != ERROR_SUCCESS) ||
           (dwType != REG_DWORD) || (dwSize != sizeof(pllType)) )
      {
         pllType = (DWORD) -1;
      }

      if (appIdx == TVAPP_MORETV)
      {
         dwSize = sizeof(tunerType);
         if ( (RegQueryValueEx(hKey, "Tuner", 0, &dwType, (char *) &tunerType, &dwSize) != ERROR_SUCCESS) ||
              (dwType != REG_DWORD) || (dwSize != sizeof(tunerType)) )
         {
            tunerType = (DWORD) -1;
         }
      }
      else
      {  // unfortunately FreeTV has a very obscure tuner handling; only MoreTV cfg is useful
         tunerType = (DWORD) -1;
      }

      if ((pllType == -1) && (tunerType == -1))
      {
         eval_check(interp, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
               "None of the expected hardware settings found. Sorry, you'll have to set them manually.}");
      }
      else
      {
         pllType = WinTvCfg_UpdatePll(interp, pllType, "PLL initilization");
         tunerType = WinTvCfg_UpdateTuner(interp, tunerType, MoretvTunerMapping, MORETV_TUNER_COUNT);

         // input index is not defined in the registry -> always set to 0 (tuner)
         WinTvCfg_UpdateInputIdx(interp, 0);

         WintvCfg_IniSummary(interp, pllType, tunerType, -1);
      }

      RegCloseKey(hKey);

      result = TRUE;
   }
   else
      result = FALSE;

   return result;
}

// ----------------------------------------------------------------------------
// Definitions for MultiDec INI file
//
// map MultiDec TV tuner indices to nxtvepg indices
static const uchar MultidecTunerMapping[] =
{
   1, 2, 3, 4, 0, 5, 6, 7, 0
};
#define MULTIDEC_TUNER_COUNT (sizeof(MultidecTunerMapping) / sizeof(*MultidecTunerMapping))
#define MULTIDEC_TUNER_MANUAL  8

// ----------------------------------------------------------------------------
// Read Multidec INI file
// 
static bool WintvCfg_GetMultidecIni( Tcl_Interp * interp, TVAPP_NAME appIdx, const char * pTvappPath )
{
   char   line[256];
   char   tag[64];
   int    value;
   int    pllType, tunerType, inputIdx;
   int    thresh1, thresh2, VHF_L, VHF_H, UHF, config, IFPCoff;
   char * pPath;
   FILE * fp;
   bool   result = FALSE;

   pPath = WintvCfg_GetPath(pTvappPath, INI_FILE_MULTIDEC);
   if (pPath != NULL)
   {
      fp = WinTvCfg_OpenIni(interp, pPath);
      if (fp != NULL)
      {
         pllType   = -1;
         tunerType = -1;
         inputIdx  = -1;
         thresh1 = thresh2 = VHF_L = VHF_H = UHF = config = IFPCoff = -1;

         while (fgets(line, sizeof(line) - 1, fp) != NULL)
         {
            if (sscanf(line," %63[^= ] = %d", tag, &value) == 2)
            {
               if (strcmp(tag, "BT848-PLL") == 0)
               {
                  pllType = value;
               }
               else if (strcmp(tag, "TUNERTYP") == 0)
               {
                  tunerType = value;
               }
               else if (strcmp(tag, "VIDEOSOURCE") == 0)
               {
                  inputIdx = value;
               }
               else if (strcmp(tag, "TUNER_THRESH1") == 0)
                  thresh1 = value;
               else if (strcmp(tag, "TUNER_THRESH2") == 0)
                  thresh2 = value;
               else if (strcmp(tag, "TUNER_VHF_L") == 0)
                  VHF_L = value;
               else if (strcmp(tag, "TUNER_VHF_H") == 0)
                  VHF_H = value;
               else if (strcmp(tag, "TUNER_UHF") == 0)
                  UHF = value;
               else if (strcmp(tag, "TUNER_CONFIG") == 0)
                  config = value;
               else if (strcmp(tag, "TUNER_IFPCOFF") == 0)
                  IFPCoff = value;
            }
         }
         fclose(fp);
         result = TRUE;

         if ((pllType == -1) && (tunerType == -1) && (inputIdx == -1))
         {
            eval_check(interp, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
                  "None of the expected hardware settings found. Sorry, you'll have to set them manually.}");
         }
         else
         {
            pllType = WinTvCfg_UpdatePll(interp, pllType, "PLL initilization");

            if (tunerType == MULTIDEC_TUNER_MANUAL)
            {
               tunerType = Tuner_MatchByParams(thresh1, thresh2, VHF_L, VHF_H, UHF, config, IFPCoff);
               if (tunerType == 0)
                  eval_check(interp, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
                        "Manual tuner setting did not match any known tuner type. Sorry, you'll have to choose a tuner manually.}");

               // already converted tuner index, so no mapping is required
               tunerType = WinTvCfg_UpdateTuner(interp, tunerType, NULL, -1);
            }
            else
               tunerType = WinTvCfg_UpdateTuner(interp, tunerType, MultidecTunerMapping, MULTIDEC_TUNER_COUNT);

            inputIdx = WinTvCfg_UpdateInputIdx(interp, inputIdx);

            WintvCfg_IniSummary(interp, pllType, tunerType, inputIdx);
         }
      }
      xfree(pPath);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Definitions for DScaler INI file
//
enum ePLLFreq
{
   PLL_NONE = 0,
   PLL_28,
   PLL_35,
};

// only the PLL setting is required from the DScaler TV card table
static const uchar DScalerPllInit[] =
{
   PLL_28, PLL_NONE, PLL_NONE, PLL_NONE, PLL_NONE, PLL_NONE, PLL_NONE, PLL_NONE,
   PLL_NONE, PLL_NONE, PLL_28, PLL_NONE, PLL_NONE, PLL_28, PLL_NONE, PLL_NONE,
   PLL_28, PLL_NONE, PLL_NONE, PLL_NONE, PLL_NONE, PLL_28, PLL_NONE, PLL_NONE,
   PLL_28, PLL_NONE, PLL_NONE, PLL_NONE, PLL_NONE, PLL_NONE, PLL_28, PLL_NONE,
   PLL_NONE, PLL_28, PLL_28, PLL_28, PLL_28, PLL_28, PLL_28, PLL_28,
   PLL_NONE, PLL_28, PLL_28, PLL_NONE, PLL_NONE, PLL_28, PLL_28, PLL_35,
   PLL_28, PLL_NONE, PLL_NONE, PLL_NONE, PLL_NONE, PLL_NONE, PLL_28, PLL_NONE,
   PLL_NONE, PLL_28, PLL_28, PLL_28, PLL_28, PLL_28, PLL_28, PLL_28,
   PLL_28, PLL_28, PLL_28, PLL_35, PLL_NONE, PLL_28, PLL_NONE, PLL_28,
   PLL_28, PLL_NONE, PLL_28, PLL_28, PLL_28, PLL_28, PLL_28, PLL_28,
   PLL_28, PLL_28, PLL_28, PLL_28, PLL_28, PLL_28,
};
#define DSCALER_PLL_COUNT (sizeof(DScalerPllInit) / sizeof(*DScalerPllInit))

// map DScaler TV tuner indices to nxtvepg indices
static const uchar DScalerTunerMapping[] =
{
   /*  0 */ 0, 2, 3, 4, 5, 1,
   /*  6 */ 6,7,8,9,10,11,12,13,14,
   /* 15 */ 0, 0,
   /* 17 */ 15,16,17,
   /* 20 */ 18,19,20,21,22,23,24,25,26,27,28,29,30,
   /* 33 */ 33,
   /* 34 */ 31,0,32,34,35,36,37,38,39,
   /* 43 */ 33
};
#define DSCALER_TUNER_COUNT (sizeof(DScalerTunerMapping) / sizeof(*DScalerTunerMapping))

// ----------------------------------------------------------------------------
// Read DScaler INI file
// 
static bool WintvCfg_GetDscalerIni( Tcl_Interp * interp, TVAPP_NAME appIdx, const char * pTvappPath )
{
   char   line[256];
   char   section[100];
   char   tag[64];
   int    value;
   bool   isHwSect;
   int    cardType, pllType, tunerType, inputIdx;
   char * pPath;
   FILE * fp;
   bool   result = FALSE;

   pPath = WintvCfg_GetPath(pTvappPath, INI_FILE_DSCALER);
   if (pPath != NULL)
   {
      fp = WinTvCfg_OpenIni(interp, pPath);
      if (fp != NULL)
      {
         isHwSect  = FALSE;
         cardType  = -1;
         tunerType = -1;
         inputIdx  = -1;

         while (fgets(line, sizeof(line) - 1, fp) != NULL)
         {
            if (sscanf(line,"[%99[^]]]", section) == 1)
            {
               isHwSect = (strcmp(section, "Hardware") == 0);
            }
            else if (isHwSect)
            {  // current line is in "Hardware" section
               if (sscanf(line," %63[^= ] = %d", tag, &value) == 2)
               {
                  if (strcmp(tag, "CardType") == 0)
                  {
                     cardType = value;
                  }
                  else if (strcmp(tag, "TunerType") == 0)
                  {
                     tunerType = value;
                  }
                  else if (strcmp(tag, "VideoSource") == 0)
                  {
                     inputIdx = value;
                  }
               }
            }
         }
         fclose(fp);
         result = TRUE;

         if ((cardType == -1) && (tunerType == -1) && (inputIdx == -1))
         {
            eval_check(interp, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
                  "None of the expected hardware settings found. Sorry, you'll have to set them manually.}");
         }
         else
         {
            if (cardType == -1)
            {  // no card definition found in the INI file
               pllType = WinTvCfg_UpdatePll(interp, -1, "TV card");
            }
            else if (cardType >= DSCALER_PLL_COUNT)
            {  // invalid or unknown TV card -> map to invalid PLL code to trigger warning msg
               pllType = WinTvCfg_UpdatePll(interp, 3, "TV card");
            }
            else
            {  // card identified -> fetch PLL setting from table and update GUI
               pllType = WinTvCfg_UpdatePll(interp, DScalerPllInit[cardType], "TV card");
            }

            tunerType = WinTvCfg_UpdateTuner(interp, tunerType, DScalerTunerMapping, DSCALER_TUNER_COUNT);
            inputIdx = WinTvCfg_UpdateInputIdx(interp, inputIdx);

            WintvCfg_IniSummary(interp, pllType, tunerType, inputIdx);
         }
      }
      xfree(pPath);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Definitions for K!TV INI file
//
// map K!TV tuner indices to nxtvepg indices
static const uchar KtvTunerMapping[] =
{
   1,2,3,4,0,5,6,7,8
};
#define KTV_TUNER_COUNT (sizeof(KtvTunerMapping) / sizeof(*KtvTunerMapping))
#define KTV_TUNER_MANUAL  8
#define KTV_TUNER_MANUAL_NEW  40

// ----------------------------------------------------------------------------
// Read K!TV INI file
// 
static bool WintvCfg_GetKtvIni( Tcl_Interp * interp, TVAPP_NAME appIdx, const char * pTvappPath )
{
   char   line[256];
   char   section[100];
   char   tag[64];
   int    value;
   bool   isHwSect, isTunerSect, isMiscSect;
   int    pllType, tunerType, inputIdx;
   int    thresh1, thresh2, VHF_L, VHF_H, UHF, config, IFPCoff;
   bool   isNewTunerTable;
   char * pPath;
   FILE * fp;
   bool   result = FALSE;

   pPath = WintvCfg_GetPath(pTvappPath, INI_FILE_KTV);
   if (pPath != NULL)
   {
      fp = WinTvCfg_OpenIni(interp, pPath);
      if (fp != NULL)
      {
         isHwSect    = FALSE;
         isTunerSect = FALSE;
         isMiscSect  = FALSE;
         pllType     = -1;
         tunerType   = -1;
         inputIdx    = -1;
         thresh1 = thresh2 = VHF_L = VHF_H = UHF = config = IFPCoff = -1;
         isNewTunerTable = FALSE;

         while (fgets(line, sizeof(line) - 1, fp) != NULL)
         {
            if (sscanf(line,"[%99[^]]]", section) == 1)
            {
               isHwSect    = ((strcmp(section, "Carte") == 0) || (strcmp(section, "Card") == 0));
               isTunerSect = (strcmp(section, "Tuner") == 0);
               isMiscSect  = (strcmp(section, "Misc") == 0);
            }
            else if (isHwSect)
            {  // current line is in "Hardware" section
               if (sscanf(line," %63[^= ] = %d", tag, &value) == 2)
               {
                  if (strcmp(tag, "BT848_PLL") == 0)
                  {
                     pllType = value;
                  }
                  else if (strcmp(tag, "TUNER_TYPE") == 0)
                  {
                     tunerType = value;
                  }
                  else if (strcmp(tag, "VIDEO_SOURCE") == 0)
                  {
                     inputIdx = value;
                  }
               }
            }
            else if (isTunerSect)
            {
               if (sscanf(line," %63[^= ] = %d", tag, &value) == 2)
               {
                  if (strcmp(tag, "TUNER_THRESH1") == 0)
                     thresh1 = value;
                  else if (strcmp(tag, "TUNER_THRESH2") == 0)
                     thresh2 = value;
                  else if (strcmp(tag, "TUNER_VHF_L") == 0)
                     VHF_L = value;
                  else if (strcmp(tag, "TUNER_VHF_H") == 0)
                     VHF_H = value;
                  else if (strcmp(tag, "TUNER_UHF") == 0)
                     UHF = value;
                  else if (strcmp(tag, "TUNER_CONFIG") == 0)
                     config = value;
                  else if (strcmp(tag, "TUNER_IFPCOFF") == 0)
                     IFPCoff = value;
               }
            }
            else if (isMiscSect)
            {
               if (sscanf(line," VERSION = %63s", tag) == 1)
               {
                  // version entry was introduced with K!TV version 1.2.0.3
                  isNewTunerTable = TRUE;
               }
            }
         }
         fclose(fp);
         result = TRUE;

         if ((pllType == -1) && (tunerType == -1) && (inputIdx == -1))
         {
            eval_check(interp, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
                  "None of the expected hardware settings found. Sorry, you'll have to set them manually.}");
         }
         else
         {
            pllType = WinTvCfg_UpdatePll(interp, pllType, "PLL initialization");
            if (tunerType == ((isNewTunerTable == FALSE) ? KTV_TUNER_MANUAL : KTV_TUNER_MANUAL_NEW))
            {
               tunerType = Tuner_MatchByParams(thresh1, thresh2, VHF_L, VHF_H, UHF, config, IFPCoff);
               if (tunerType == 0)
                  eval_check(interp, "tk_messageBox -type ok -icon warning -parent .hwcfg -message {"
                        "Manual tuner setting did not match any known tuner type. Sorry, you'll have to choose a tuner manually.}");

               // already converted tuner index, so no mapping is required
               tunerType = WinTvCfg_UpdateTuner(interp, tunerType, NULL, -1);
            }
            else if (isNewTunerTable == FALSE)
               tunerType = WinTvCfg_UpdateTuner(interp, tunerType, KtvTunerMapping, KTV_TUNER_COUNT);
            else  // starting with version 1.2.0.3 K!TV uses the same tuner table as DScaler >= 3.0 with 40 entries + manual
               tunerType = WinTvCfg_UpdateTuner(interp, tunerType, DScalerTunerMapping, DSCALER_TUNER_COUNT);

            inputIdx = WinTvCfg_UpdateInputIdx(interp, inputIdx);

            WintvCfg_IniSummary(interp, pllType, tunerType, inputIdx);
         }
      }
      xfree(pPath);
   }
   return result;
}

// ----------------------------------------------------------------------------
//
typedef struct
{
   const char  * pName;
   bool          needPath;
   void       (* pGetStationNames) ( Tcl_Interp * interp, const char * pTvappPath );
   bool       (* pGetFreqTab) ( Tcl_Interp * interp, DYN_FREQ_BUF * pFreqBuf, const char * pTvappPath );
   bool       (* pGetIni) ( Tcl_Interp * interp, TVAPP_NAME appIdx, const char * pTvappPath );
} TVAPP_LIST;

static const TVAPP_LIST tvAppList[TVAPP_COUNT] =
{
   { "none",     FALSE, NULL, NULL, NULL },
   { "DScaler",  TRUE,  WintvCfg_GetDscalerStationNames,  WintvCfg_GetDscalerFreqTab,  WintvCfg_GetDscalerIni },
   { "K!TV",     TRUE,  WintvCfg_GetKtvStationNames,      WintvCfg_GetKtvFreqTab,      WintvCfg_GetKtvIni }, 
   { "MultiDec", TRUE,  WintvCfg_GetMultidecStationNames, WintvCfg_GetMultidecFreqTab, WintvCfg_GetMultidecIni }, 
   { "MoreTV",   FALSE, WintvCfg_GetMoretvStationNames,   WintvCfg_GetMoretvFreqTab,   WintvCfg_GetMoretvIni }, 
   { "FreeTV",   FALSE, WintvCfg_GetFreetvStationNames,   WintvCfg_GetFreetvFreqTab,   WintvCfg_GetMoretvIni }, 
};

// ----------------------------------------------------------------------------
// Return list of names of supported TV applications
//
static int WintvCfg_GetTvappList( ClientData ttp, Tcl_Interp * interp, int argc, CONST84 char *argv[] )
{
   uint  idx;
   int   result;

   if (argc != 1)
   {  // parameter count is invalid
      #if (DEBUG_SWITCH_TCL_BGERR == ON)
      Tcl_SetResult(interp, "C_Tvapp_GetTvappList: no parameters expected", TCL_STATIC);
      #endif
      result = TCL_ERROR;
   }
   else
   {
      for (idx=0; idx < TVAPP_COUNT; idx++)
      {
         Tcl_AppendElement(interp, tvAppList[idx].pName);
      }
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Extract channel names listed in TV app configuration file
//
static int WintvCfg_GetStationNames( ClientData ttp, Tcl_Interp * interp, int argc, CONST84 char *argv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_GetStationNames";
   const char * pTvAppPath;
   int     appIdx;
   int     result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      appIdx = WintvCfg_GetAppIdx();
      if (appIdx != TVAPP_NONE)
      {
         pTvAppPath = Tcl_GetVar(interp, "wintvapp_path", TCL_GLOBAL_ONLY);

         tvAppList[appIdx].pGetStationNames(interp, pTvAppPath);
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
   int     appIdx;
   bool    result = FALSE;

   appIdx = WintvCfg_GetAppIdx();
   if (appIdx != TVAPP_NONE)
   {
      pTvAppPath = Tcl_GetVar(interp, "wintvapp_path", TCL_GLOBAL_ONLY);

      if ( (tvAppList[appIdx].needPath == FALSE) ||
           ((pTvAppPath != NULL) && (pTvAppPath[0] != 0)) )
      {
         memset(&freqBuf, 0, sizeof(freqBuf));
         if (tvAppList[appIdx].pGetFreqTab(interp, &freqBuf, pTvAppPath))
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
static int WintvCfg_CfgNeedsPath( ClientData ttp, Tcl_Interp * interp, int argc, CONST84 char *argv[] )
{
   int   appIdx;
   bool  needPath;
   int   result;

   if ( (argc != 2) ||
        (Tcl_GetInt(interp, argv[1], &appIdx) != TCL_OK) )
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

      Tcl_SetResult(interp, (needPath ? "1" : "0"), TCL_STATIC);

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Tcl callback to check if a TV app is configured
//
static int WintvCfg_Enabled( ClientData ttp, Tcl_Interp *interp, int argc, CONST84 char *argv[] )
{
   const char * const pUsage = "Usage: C_Tvapp_Enabled";
   int result;

   if (argc != 1)
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR;
   }
   else
   {
      Tcl_SetResult(interp, ((WintvCfg_GetAppIdx() != TVAPP_NONE) ? "1" : "0"), TCL_STATIC);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Load configuration params from TV app ini file into the TV card dialog
// - the parameters are not applies to the driver yet, only loaded into Tcl vars
//
static int WintvCfg_TestChanTab( ClientData ttp, Tcl_Interp * interp, int argc, CONST84 char *argv[] )
{
   DYN_FREQ_BUF freqBuf;
   int    newAppIdx;
   int    result;

   if ( (argc != 3) ||
        (Tcl_GetInt(interp, argv[1], &newAppIdx) != TCL_OK) )
   {  // parameter count is invalid
      #if (DEBUG_SWITCH_TCL_BGERR == ON)
      Tcl_SetResult(interp, "Usage C_Tvapp_TestChanTab: <tvAppIdx> <path>", TCL_STATIC);
      #endif
      result = TCL_ERROR;
   }
   else
   {
      if (newAppIdx == TVAPP_NONE)
      {  // no TV app selected yet (i.e. option "none" selected)
         eval_check(interp, 
            "tk_messageBox -type ok -icon error -parent .xawtvcf -message {"
               "Please select a TV application from which to load the channel table.}");
      }
      else if (newAppIdx < TVAPP_COUNT)
      {
         if ((tvAppList[newAppIdx].needPath == FALSE) || (argv[2][0] != 0))
         {
            memset(&freqBuf, 0, sizeof(freqBuf));

            if ( tvAppList[newAppIdx].pGetFreqTab(interp, &freqBuf, argv[2]) )
            {
               if (freqBuf.fillCount > 0)
               {
                  sprintf(comm, "tk_messageBox -type ok -icon info -parent .xawtvcf -message {"
                                "Found %d channel names; please use the network name dialog in the "
                                "Configure menu to synchronize names between nxtvepg and %s}",
                                freqBuf.fillCount, tvAppList[newAppIdx].pName);
                  eval_check(interp, comm);
               }
               else
               {  // opened ok, but no channels found
                  sprintf(comm, "tk_messageBox -type ok -icon warning -parent .xawtvcf -message {"
                                "No channels found in the %s channel table!}", tvAppList[newAppIdx].pName);
                  eval_check(interp, comm);
               }
            }
            // else: file open failed -> user already informed

            if (freqBuf.pFreqTab != NULL)
               xfree(freqBuf.pFreqTab);
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

      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Load configuration params from TV app ini file into the TV card dialog
// - the parameters are not applied to the driver yet, only loaded into Tcl vars
//
static int WintvCfg_LoadHwConfig( ClientData ttp, Tcl_Interp * interp, int argc, CONST84 char *argv[] )
{
   int    newAppIdx;
   int    result;

   if ( (argc != 3) ||
        (Tcl_GetInt(interp, argv[1], &newAppIdx) != TCL_OK) )
   {  // parameter count is invalid
      #if (DEBUG_SWITCH_TCL_BGERR == ON)
      Tcl_SetResult(interp, "Usage C_Tvapp_LoadConfig: <tvAppIdx> <path>", TCL_STATIC);
      #endif
      result = TCL_ERROR;
   }
   else
   {
      if (newAppIdx == TVAPP_NONE)
      {  // no TV app selected yet (i.e. option "none" selected)
         eval_check(interp, 
            "tk_messageBox -type ok -icon error -parent .hwcfg -message {"
               "Please select a TV application from which to load the config.}");
      }
      else if (newAppIdx < TVAPP_COUNT)
      {
         if ((tvAppList[newAppIdx].needPath == FALSE) || (argv[2][0] != 0))
         {
            // load TV card settings from INI files (e.g. tuner & PLL types)
            // all user feedback messages are generated inside the parser functions
            tvAppList[newAppIdx].pGetIni(interp, newAppIdx, argv[2]);
         }
         else
         {  // no TV app dir specified -> abort with error msg
            char msg_buf[250];
            sprintf(msg_buf, "tk_messageBox -type ok -icon error -parent .hwcfg -message {"
               "You must specify the directory where %s is installed.}", tvAppList[newAppIdx].pName);
            eval_check(interp, msg_buf);
         }
      }
      else
         debug1("WintvCfg-LoadHwConfig: illegal app idx %d", newAppIdx);

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

   // Create callback functions
   Tcl_CreateCommand(interp, "C_Tvapp_Enabled", WintvCfg_Enabled, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_GetStationNames", WintvCfg_GetStationNames, (ClientData) NULL, NULL);

   Tcl_CreateCommand(interp, "C_Tvapp_GetTvappList", WintvCfg_GetTvappList, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_LoadHwConfig", WintvCfg_LoadHwConfig, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_TestChanTab", WintvCfg_TestChanTab, (ClientData) NULL, NULL);
   Tcl_CreateCommand(interp, "C_Tvapp_CfgNeedsPath", WintvCfg_CfgNeedsPath, (ClientData) NULL, NULL);

   // pass the name of the configured (not the actually connected) app to GUI
   eval_check(interp, "UpdateTvappName");
}

#endif  //WIN32

