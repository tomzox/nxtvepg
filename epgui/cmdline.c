/*
 *  Command line argument handling
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
 *    This module has procedures to parse, check and store arguments
 *    given on the command line.  Some general options are already
 *    processed here, too.  The code in this module is shared between
 *    GUI and stand-alone acquisition daemon.
 *
 *  Author: Tom Zoerner
 *          Win32 SetArgv() function taken from the Tcl/Tk library
 *
 *  $Id: cmdline.c,v 1.14 2008/10/12 19:56:24 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#ifdef WIN32
#include <windows.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/epgversion.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgctl/epgctxctl.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgversion.h"

#include "epgui/pidescr.h"
#include "epgui/dumptext.h"
#include "epgui/dumpxml.h"
#include "epgui/epgquery.h"
#include "epgui/daemon.h"
#include "xmltv/xmltv_cni.h"
#include "xmltv/xmltv_main.h"

#include "epgui/cmdline.h"


// global structure which holds all command line options
CMDLINE_OPTS mainOpts;

// global strings which hold the version info
char *epg_version_str = EPG_VERSION_STR;
char epg_rcs_id_str[] = EPG_VERSION_RCS_ID;



// ---------------------------------------------------------------------------
// Append a postfix string to the config file name
// - used to avoid overwriting the original rc file if it's from a newer version
//
void CmdLine_AddRcFilePostfix( const char * pPostfix )
{
   char * pTmp;

   if (mainOpts.rcfile != NULL)
   {
      if (pPostfix != NULL)
      {
         pTmp = xmalloc(strlen(mainOpts.rcfile) + 1 + strlen(pPostfix) + 1);
         strcpy(pTmp, mainOpts.rcfile);
         strcat(pTmp, ".");
         strcat(pTmp, pPostfix);

         xfree((void *) mainOpts.rcfile);
         mainOpts.rcfile = pTmp;
      }
      else
         debug0("CmdLine-AddRcFilePostfix: illegal NULL ptr param");
   }
   else
      fatal0("CmdLine-AddRcFilePostfix: NULL rcfile name");
}

// ---------------------------------------------------------------------------
// Query if the program was started in demo mode
//
bool IsDemoMode( void )
{
   return (mainOpts.pDemoDatabase != NULL);
}

// ---------------------------------------------------------------------------
// Print error message and exit
// - same to "usage" function, but without printing all the options
//
static void MainOptionError( const char *argv0, const char *argvn, const char * reason )
{
#ifdef WIN32
   char * pBuf;
#endif
   const char * const pUsageFmt =
                   "%s: %s: %s\n"
                   "Usage: %s [OPTIONS] [DATABASE]\n"
                   "Try '%s -help' or refer to the manual page for more information.\n";

#ifndef WIN32
   fprintf(stderr, pUsageFmt, argv0, reason, argvn, argv0, argv0);
#else
   pBuf = xmalloc(strlen(pUsageFmt) + strlen(argv0)*2 + strlen(reason) + strlen(argvn));
   sprintf(pBuf, pUsageFmt, argv0, reason, argvn, argv0, argv0);
   MessageBox(NULL, pBuf, "nxtvepg command line options error", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
   xfree(pBuf);
#endif

   exit(1);
}

// ---------------------------------------------------------------------------
// Print Usage and exit
//
static void Usage( const char *argv0, const char *argvn, const char * reason )
{
#ifdef WIN32
   char * pBuf;
#endif
   const char * const pUsageFmt =
                   "%s: %s: %s\n"
                   "Usage: %s [options] [database]\n"
                   "       -help       \t\t: this message\n"
                   "%s%s%s%s";

   const char * const pWmUsage =
                   #ifndef WIN32
                   "       -display <display>  \t: X11 server, if different from $DISPLAY\n"
                   "       -tvdisplay <display>\t: X11 server for TV application\n"
                   #endif
                   "       -geometry <geometry>\t: initial window position\n"
                   "       -iconic     \t\t: start with window minimized (iconified)\n";

   const char * const pCommonUsage =
                   "       -rcfile <path>      \t: path and file name of setup file\n"
                   "       -dbdir <path>       \t: directory where to store databases\n"
                   #ifndef WIN32
                   #ifdef EPG_DB_ENV
                   "                           \t: default: $" EPG_DB_ENV "/" EPG_DB_DIR "\n"
                   #else
                   "                           \t: default: " EPG_DB_DIR "\n"
                   #endif
                   #endif
                   "       -outfile <path>     \t: target file for export and other output\n"
                   "       -dump xml|html|pi|...\t: export database in various formats\n"
                   "       -epgquery <string>  \t: apply given filter command to export\n"
                   "       -provider <cni>     \t: network id of EPG provider (hex)\n"
                   "       -card <digit>       \t: index of TV card for acq (starting at 0)\n"
                   #ifndef WIN32
                   "       -dvbpid <number>    \t: Use DVB with the given pid (passive acq.)\n"
                   #endif
                   ;


   const char * const pGuiUsage =
                   "       -remctrl <cmd>      \t: remote control nxtvepg\n"
                   "       -demo <db-file>     \t: load database in demo mode\n"
                   "       -noacq              \t: don't start acquisition automatically\n"
                   "       -daemon             \t: don't open any windows; acquisition only\n";

   const char * const pDaemonUsage =
                   #ifdef USE_DAEMON
                   "       -daemonstop         \t: terminate background acquisition process\n"
                   "       -acqpassive         \t: force daemon to passive acquisition mode\n"
                   "       -acqonce <phase>    \t: stop acquisition after the given stage\n"
                   "       -nodetach           \t: daemon remains connected to tty\n"
                   #endif
                   "       -clock set|print    \t: set system clock from teletext clock\n"
                   "       -provscan <country> \t: run stand-alone EPG provider scan\n";

#ifndef WIN32
   if (mainOpts.daemonOnly)
      fprintf(stderr, pUsageFmt,
                      argv0, reason, argvn, argv0,
                      "", pCommonUsage, "", pDaemonUsage);
   else
      fprintf(stderr, pUsageFmt,
                      argv0, reason, argvn, argv0,
                      pWmUsage, pCommonUsage, pGuiUsage, pDaemonUsage);
#else
   pBuf = xmalloc(strlen(pUsageFmt) + strlen(pWmUsage) + strlen(pCommonUsage)
                  + strlen(pGuiUsage) + strlen(pDaemonUsage)
                  + strlen(argv0)*2 + strlen(reason) + strlen(argvn));
   if (mainOpts.daemonOnly)
      sprintf(pBuf, pUsageFmt, argv0, reason, argvn, argv0, "", pCommonUsage, "", pDaemonUsage);
   else
      sprintf(pBuf, pUsageFmt, argv0, reason, argvn, argv0, pWmUsage, pCommonUsage, pGuiUsage, pDaemonUsage);
   MessageBox(NULL, pBuf, "nxtvepg command line usage", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
   xfree(pBuf);
#endif

   exit(1);
}

#ifdef WIN32
/*
 *-------------------------------------------------------------------------
 *
 * setargv --     [This is taken from the wish main in the Tcl/Tk package]
 *
 *	Parse the Windows command line string into argc/argv.  Done here
 *	because we don't trust the builtin argument parser in crt0.  
 *	Windows applications are responsible for breaking their command
 *	line into arguments.
 *
 *	2N backslashes + quote -> N backslashes + begin quoted string
 *	2N + 1 backslashes + quote -> literal
 *	N backslashes + non-quote -> literal
 *	quote + quote in a quoted string -> single quote
 *	quote + quote not in quoted string -> empty string
 *	quote -> begin quoted string
 *
 * Results:
 *	Fills argcPtr with the number of arguments and argvPtr with the
 *	array of arguments.
 *
 * Parameters:
 *   int *argcPtr;        Filled with number of argument strings
 *   char ***argvPtr;     Filled with argument strings (malloc'd)
 *
 *--------------------------------------------------------------------------
 */
void CmdLine_WinApiSetArgv( int * argcPtr, char *** argvPtr )
{
    char *cmdLine, *p, *arg, *argSpace;
    char **argv;
    int argc, size, inquote, copy, slashes;
    
    cmdLine = GetCommandLine();

    // Precompute an overly pessimistic guess at the number of arguments
    // in the command line by counting non-space spans.
    size = 2;
    for (p = cmdLine; *p != '\0'; p++) {
        if (isspace(*p)) {
            size++;
            while (isspace(*p)) {
                p++;
            }
            if (*p == '\0') {
                break;
            }
        }
    }
    argSpace = (char *) xmalloc((unsigned) (size * sizeof(char *) + strlen(cmdLine) + 1));
    argv = (char **) argSpace;
    argSpace += size * sizeof(char *);
    size--;

    p = cmdLine;
    for (argc = 0; argc < size; argc++) {
        argv[argc] = arg = argSpace;
        while (isspace(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        inquote = 0;
        slashes = 0;
        while (1) {
            copy = 1;
            while (*p == '\\') {
                slashes++;
                p++;
            }
            if (*p == '"') {
                if ((slashes & 1) == 0) {
                    copy = 0;
                    if ((inquote) && (p[1] == '"')) {
                        p++;
                        copy = 1;
                    } else {
                        inquote = !inquote;
                    }
                }
                slashes >>= 1;
            }

            while (slashes) {
                *arg = '\\';
                arg++;
                slashes--;
            }

            if ((*p == '\0') || (!inquote && isspace(*p))) {
                break;
            }
            if (copy != 0) {
                *arg = *p;
                arg++;
            }
            p++;
        }
        *arg = '\0';
        argSpace = arg + 1;
    }
    argv[argc] = NULL;

    *argcPtr = argc;
    *argvPtr = argv;
}

// ---------------------------------------------------------------------------
// Determine the working directory from executable file path and chdir there
// - used when a db is given on the command line
// - required because when the program is started by dropping a db onto the
//   executable the working dir is set to the desktop, which is certainly
//   not the right place to create the ini file
//
static void SetWorkingDirectoryFromExe( const char *argv0 )
{
   char *pDirPath;
   int len;

   // search backwards from the end for the begin of the file name
   len = strlen(argv0);
   while (--len >= 0)
   {
      if (argv0[len] == PATH_SEPARATOR)
      {
         pDirPath = xstrdup(argv0);
         pDirPath[len] = 0;
         // change to the directory
         if (chdir(pDirPath) != 0)
         {
            debug2("Cannot change working dir to %s: %s", pDirPath, strerror(errno));
         }
         xfree(pDirPath);
         break;
      }
   }
}
#endif  // WIN32

// ---------------------------------------------------------------------------
// Process -outfile option: redirect standard output to a file
//
static void CmdLine_SetOutfile( void )
{
   struct stat st;

   if (mainOpts.pStdOutFileName != NULL)
   {
      if ( stat(mainOpts.pStdOutFileName, &st) != 0 )
      {
         if (freopen(mainOpts.pStdOutFileName, "w", stdout) == NULL)
         {
            MainOptionError(mainOpts.pOptArgv0, strerror(errno), "failed to create output file");
         }
      }
      else
         MainOptionError(mainOpts.pOptArgv0, mainOpts.pStdOutFileName, "output file already exists");
   }
}

// ---------------------------------------------------------------------------
// Translate string into dump mode
//
static bool CmdLine_GetDumpMode( char * argv[], int argIdx )
{
   const char * pModeStr = argv[argIdx + 1];

   mainOpts.optDumpMode = EPG_DUMP_NONE;

   {
      if (strcasecmp("ai", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_TEXT;
         mainOpts.optDumpSubMode = DUMP_TEXT_AI;
      }
      else if (strcasecmp("pi", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_TEXT;
         mainOpts.optDumpSubMode = DUMP_TEXT_PI;
      }
      else if (strcasecmp("pdc", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_TEXT;
         mainOpts.optDumpSubMode = DUMP_TEXT_PDC;
      }
      else if (strcasecmp("xml", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_XMLTV;
         mainOpts.optDumpSubMode = DUMP_XMLTV_ANY;
      }
      else if (strcasecmp("xml5", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_XMLTV;
         mainOpts.optDumpSubMode = DUMP_XMLTV_DTD_5_GMT;
      }
      else if (strcasecmp("xml5ltz", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_XMLTV;
         mainOpts.optDumpSubMode = DUMP_XMLTV_DTD_5_LTZ;
      }
      else if (strcasecmp("xml6", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_XMLTV;
         mainOpts.optDumpSubMode = DUMP_XMLTV_DTD_6;
      }
      else if (strncasecmp("html", pModeStr, 4) == 0)
      {
         int nscan;
         if (strcasecmp("html", pModeStr) == 0)
         {
            mainOpts.optDumpMode = EPG_DUMP_HTML;
            mainOpts.optDumpSubMode = 0;
         }
         else if ( (sscanf(pModeStr, "html:%u%n", (int*)&mainOpts.optDumpSubMode, &nscan) >= 1) &&
                   (nscan >= strlen(pModeStr)) )
         {
            mainOpts.optDumpMode = EPG_DUMP_HTML;
         }
         else
            MainOptionError(argv[0], argv[argIdx + 1], "parse error in HTML dump format");
      }
      else if (strcasecmp("raw", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_RAW;
      }
      else
         MainOptionError(argv[0], argv[argIdx + 1], "illegal mode keyword for -dump");
   }

   return (mainOpts.optDumpMode != EPG_DUMP_NONE);
}

// ---------------------------------------------------------------------------
// Parse command line options
//
static void CmdLine_Parse( int argc, char * argv[] )
{
   int argIdx = 1;
#ifdef USE_XMLTV_IMPORT
   uint errCode;
#endif

   mainOpts.pOptArgv0 = argv[0];

   mainOpts.dbdir  = mainOpts.defaultDbDir;

   while (argIdx < argc)
   {
      if (argv[argIdx][0] == '-')
      {
         if (!strcmp(argv[argIdx], "-help"))
         {
            char versbuf[50];
            sprintf(versbuf, "(version %s)", epg_version_str);
            Usage(argv[0], versbuf, "the following command line options are available");
         }
         else if (!strcmp(argv[argIdx], "-noacq"))
         {  // do not enable acquisition
            mainOpts.disableAcq = TRUE;
            argIdx += 1;
         }
         #ifdef USE_DAEMON
         else if (!strcmp(argv[argIdx], "-daemon"))
         {  // suppress GUI
            mainOpts.optDaemonMode = DAEMON_RUN;
            argIdx += 1;
         }
         else if (!strcmp(argv[argIdx], "-daemonstop"))
         {  // kill daemon process, then exit
            mainOpts.optDaemonMode = DAEMON_STOP;
            argIdx += 1;
         }
         else if (!strcmp(argv[argIdx], "-nodetach"))
         {  // daemon stays in the foreground
            mainOpts.optNoDetach = TRUE;
            argIdx += 1;
         }
         else if (!strcmp(argv[argIdx], "-acqpassive"))
         {  // set passive acquisition mode (not saved to rc/ini file)
            mainOpts.optAcqPassive = TRUE;
            argIdx += 1;
         }
         else if (!strcmp(argv[argIdx], "-acqonce"))
         {  // do not enable acquisition
            if (argIdx + 1 < argc)
            {
               if (!strcmp(argv[argIdx + 1], "full"))
                  mainOpts.optAcqOnce = ACQMODE_PHASE_STREAM2;
               else if (!strcmp(argv[argIdx + 1], "near"))
                  mainOpts.optAcqOnce = ACQMODE_PHASE_STREAM1;
               else if (!strcmp(argv[argIdx + 1], "now"))
                  mainOpts.optAcqOnce = ACQMODE_PHASE_NOWNEXT;
               else
                  MainOptionError(argv[0], argv[argIdx + 1], "unknown mode keyword for -acqonce: expecting now, near or full");
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing mode keyword after");
         }
         #else  // not USE_DAEMON
         else if ( !strcmp(argv[argIdx], "-daemon") ||
                   !strcmp(argv[argIdx], "-acqpassive") )
         {
            MainOptionError(argv[0], argv[argIdx], "support for this option has been disabled");
         }
         #endif
         else if (!strcmp(argv[argIdx], "-dump"))
         {  // dump database and exit
            if (argIdx + 1 < argc)
            {
               if (mainOpts.optDumpMode == EPG_DUMP_NONE)
               {
                  CmdLine_GetDumpMode(argv, argIdx);
                  argIdx += 2;
               }
               else
                  MainOptionError(argv[0], argv[argIdx], "this option can be used only once");
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing mode keyword after");
         }
         else if (!strcmp(argv[argIdx], "-epgquery"))
         {  // set query filter for dump
            if (argIdx + 1 < argc)
            {
               char * pErrStr = NULL;
               uint queryIdx = 0;

               while ((queryIdx < OPT_DUMP_FILTER_MAX) && (mainOpts.optDumpFilter[queryIdx] != NULL))
                  queryIdx++;
               if (queryIdx < OPT_DUMP_FILTER_MAX)
               {
                  if ( EpgQuery_CheckSyntax(argv[argIdx + 1], &pErrStr) )
                  {
                     mainOpts.optDumpFilter[queryIdx] = argv[argIdx + 1];
                     argIdx += 2;
                  }
                  else
                  {
                     if (pErrStr != NULL)
                     {
                        MainOptionError(argv[0], pErrStr, "invalid -epqquery option");
                        xfree(pErrStr);
                     }
                     else
                        MainOptionError(argv[0], argv[argIdx + 1], "invalid -epqquery option");
                  }
               }
               else
                  MainOptionError(argv[0], argv[argIdx], "too many instances of");
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing filter keyword and option after");
         }
         else if (!strcmp(argv[argIdx], "-rcfile"))
         {
            if (argIdx + 1 < argc)
            {  // read file name of rc/ini file
               if (mainOpts.rcfile != NULL)
                  xfree((void *) mainOpts.rcfile);
               mainOpts.rcfile = xstrdup(argv[argIdx + 1]);
               mainOpts.isUserRcFile = TRUE;
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing file name after");
         }
         else if (!strcmp(argv[argIdx], "-dbdir"))
         {
            if (argIdx + 1 < argc)
            {  // read path of database directory
               mainOpts.dbdir = argv[argIdx + 1];
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing path name after");
         }
         else if (!strcmp(argv[argIdx], "-card"))
         {
            if (argIdx + 1 < argc)
            {  // read index of TV card device
               char *pe;
               ulong cardIdx = strtol(argv[argIdx + 1], &pe, 0);
               if ((pe != (argv[argIdx + 1] + strlen(argv[argIdx + 1]))) || (cardIdx > 9))
                  MainOptionError(argv[0], argv[argIdx+1], "invalid index for -card (range 0-9)");
               mainOpts.videoCardIndex = (int) cardIdx;
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing card index after");
         }
         else if (!strcmp(argv[argIdx], "-dvbpid"))
         {
            #ifndef WIN32
            if (argIdx + 1 < argc)
            {  // read PID for DVB
               char *pe;
               ulong dvbPid = strtol(argv[argIdx + 1], &pe, 0);
               if (pe != (argv[argIdx + 1] + strlen(argv[argIdx + 1])))
                  MainOptionError(argv[0], argv[argIdx+1], "invalid value for -dvbpid");
               mainOpts.dvbPid = (int) dvbPid;
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing DVB pid number after");
            #else
            MainOptionError(argv[0], argv[argIdx], "not supported on Windows platforms");
            #endif
         }
         else if ( !strcmp(argv[argIdx], "-provider") ||
                   !strcmp(argv[argIdx], "-prov") )
         {
            if (argIdx + 1 < argc)
            {  // read hexadecimal CNI of selected provider
               if (mainOpts.startUiCni != 0)
               {
                  MainOptionError(argv[0], argv[argIdx], "this option can be used only once");
               }
               if (strcmp(argv[argIdx + 1], "merged") == 0)
               {
                  mainOpts.startUiCni = MERGED_PROV_CNI;
               }
               else
               {
                  char *pe;
                  mainOpts.startUiCni = strtol(argv[argIdx + 1], &pe, 16);
                  if (pe != (argv[argIdx + 1] + strlen(argv[argIdx + 1])))
                     MainOptionError(argv[0], argv[argIdx+1], "invalid provider CNI (must be hexadecimal, e.g. 0x0d94 or d94, or 'merged')");
                  if (IS_NXTV_CNI(mainOpts.startUiCni) == FALSE)
                     MainOptionError(argv[0], argv[argIdx+1], "provider CNI is outside of allowed range 0001-FFFF");

               }
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing provider cni after");
         }
         else if (!strcmp(argv[argIdx], "-clock"))
         {  // extract current time from teletext
            if (argIdx + 1 < argc)
            {
               mainOpts.optDaemonMode = EPG_CLOCK_CTRL;
               if (strcasecmp("set", argv[argIdx + 1]) == 0)
                  mainOpts.optDumpSubMode = CLOCK_CTRL_SET;
               else if (strcasecmp("print", argv[argIdx + 1]) == 0)
                  mainOpts.optDumpSubMode = CLOCK_CTRL_PRINT;
               else
                  MainOptionError(argv[0], argv[argIdx + 1], "illegal mode keyword for -clock");
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing mode keyword after");
         }
         else if (!strcmp(argv[argIdx], "-provscan"))
         {  // extract current time from teletext
            if (argIdx + 1 < argc)
            {
               mainOpts.optDaemonMode = EPG_CL_PROV_SCAN;
               if ( (strcasecmp("de", argv[argIdx + 1]) == 0) ||
                    (strcasecmp("ch", argv[argIdx + 1]) == 0) ||
                    (strcasecmp("at", argv[argIdx + 1]) == 0) ||
                    (strcasecmp("be", argv[argIdx + 1]) == 0) )
                  mainOpts.optDumpSubMode = EPG_CLPROV_SCAN_EU;
               else if (strcasecmp("fr", argv[argIdx + 1]) == 0)
                  mainOpts.optDumpSubMode = EPG_CLPROV_SCAN_FR;
               else
                  MainOptionError(argv[0], argv[argIdx + 1], "unknwon country code for -provscan");
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing mode keyword after");
         }
         else if (!strcmp(argv[argIdx], "-outfile"))
         {  // install given file as target for stdout (esp. for dump modes)
            if (argIdx + 1 < argc)
            {
               mainOpts.pStdOutFileName = argv[argIdx + 1];
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing mode keyword after");
         }
         else if (!strcmp(argv[argIdx], "-demo"))
         {
            if (argIdx + 1 < argc)
            {  // save file name of demo database
               mainOpts.pDemoDatabase = argv[argIdx + 1];
               // check if file exists and check the file header
               if (EpgDbDumpCheckFileHeader(mainOpts.pDemoDatabase) == FALSE)
               {
                  MainOptionError(argv[0], strerror(errno), "cannot open demo database");
               }
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing database file name after");
         }
         else if ( !strcmp(argv[argIdx], "-iconic") )
         {  // start with iconified main window
            mainOpts.startIconified = TRUE;
            argIdx += 1;
         }
         else if ( !strcmp(argv[argIdx], "-geometry")
                   #ifndef WIN32
                   || !strcmp(argv[argIdx], "-display")
                   || !strcmp(argv[argIdx], "-name")
                   #endif
                 )
         {  // ignore arguments that are handled by Tk
            if (argIdx + 1 >= argc)
               MainOptionError(argv[0], argv[argIdx], "missing position argument after");
            argIdx += 2;
         }
         #ifndef WIN32
         else if ( !strcmp(argv[argIdx], "-tvdisplay") )
         {  // alternate display for TV application
            if (argIdx + 1 >= argc)
               MainOptionError(argv[0], argv[argIdx], "missing display name argument after");
            mainOpts.pTvX11Display = argv[argIdx + 1];
            argIdx += 2;
         }
         #endif
         else if ( !strcmp(argv[argIdx], "-remctrl") )
         {  // undocumented option (internal use only): pass fd of pipe to GUI for daemon start
            if (argIdx + 1 < argc)
            {  // read decimal fd (silently ignore errors)
               if (!strcmp(argv[argIdx + 1], "quit"))
                  mainOpts.optRemCtrl = REM_CTRL_QUIT;
               else if (!strcmp(argv[argIdx + 1], "raise"))
                  mainOpts.optRemCtrl = REM_CTRL_RAISE;
               else if (!strcmp(argv[argIdx + 1], "iconify"))
                  mainOpts.optRemCtrl = REM_CTRL_ICONIFY;
               else if (!strcmp(argv[argIdx + 1], "deiconify"))
                  mainOpts.optRemCtrl = REM_CTRL_DEICONIFY;
               else if (!strcmp(argv[argIdx + 1], "acqon"))
                  mainOpts.optRemCtrl = REM_CTRL_ACQ_ON;
               else if (!strcmp(argv[argIdx + 1], "acqoff"))
                  mainOpts.optRemCtrl = REM_CTRL_ACQ_OFF;
               else
                  MainOptionError(argv[0], argv[argIdx + 1], "unknown keyword");
               argIdx += 2;
            }
            else
               MainOptionError(argv[0], argv[argIdx], "missing command keyword after");
         }
         else if ( !strcmp(argv[argIdx], "-guipipe") )
         {  // undocumented option (internal use only): pass fd of pipe to GUI for daemon start
            if (argIdx + 1 < argc)
            {  // read decimal fd (silently ignore errors)
               char *pe;
               mainOpts.optGuiPipe = strtol(argv[argIdx + 1], &pe, 0);
               if (pe != (argv[argIdx + 1] + strlen(argv[argIdx + 1])))
                  mainOpts.optGuiPipe = -1;
               argIdx += 2;
            }
         }
         else
            MainOptionError(argv[0], argv[argIdx], "unknown command line option");
      }
      else if (argIdx + 1 == argc)
      {  // database file argument -> determine dbdir and provider from path
         if (mainOpts.pDemoDatabase != NULL)
            MainOptionError(argv[0], argv[argIdx], "already specified a database with -demo");

         if ( EpgDbDumpCheckFileHeader(argv[argIdx]) )
         {
            // automatically assume demo mode if content is OK, but name does not match the expected pattern
            if (EpgDbDumpGetDirAndCniFromArg(argv[argIdx], &mainOpts.dbdir, &mainOpts.startUiCni) == FALSE)
            {
               mainOpts.pDemoDatabase = argv[argIdx];
            }
         }
#ifdef USE_XMLTV_IMPORT
         else if ( Xmltv_CheckHeader(argv[argIdx], &errCode) )
         {
            mainOpts.pXmlDatabase = argv[argIdx];
         }
         else if ( Xmltv_IsXmlDocument(errCode) || (strstr(argv[argIdx], "xml") != NULL) )
         {
            MainOptionError(argv[0], argv[argIdx], Xmltv_TranslateErrorCode(errCode));
         }
#endif
         else
         {
            MainOptionError(argv[0], argv[argIdx], "unrecognized type of file");
         }
         argIdx += 1;
      }
      else
         MainOptionError(argv[0], argv[argIdx], "Too many arguments");
   }

   // Check for disallowed option combinations
   #ifdef USE_DAEMON
   if (IS_DAEMON(mainOpts) || IS_CLOCK_MODE(mainOpts))
   {
      if (mainOpts.disableAcq)
         MainOptionError(argv[0], "-noacq", "Cannot be combined with -daemon/-clock");
      else if (IS_DUMP_MODE(mainOpts))
         MainOptionError(argv[0], "-dump", "Cannot be combined with -daemon/-clock");
      else if (IS_REMCTL_MODE(mainOpts))
         MainOptionError(argv[0], "-remctrl", "Cannot be combined with -daemon/-clock");
   }
   else if (mainOpts.daemonOnly)
   {
      if (mainOpts.disableAcq)
         MainOptionError(argv[0], "-noacq", "Cannot be used with daemon executable");
      else if (IS_REMCTL_MODE(mainOpts))
         MainOptionError(argv[0], "-remctrl", "Cannot be used with daemon executable");
   }
   else
   #endif
   {
      if (mainOpts.optAcqPassive)
         MainOptionError(argv[0], "-acqpassive", "Only meant for -daemon mode");
      if (mainOpts.optAcqOnce != ACQMODE_PHASE_COUNT)
         MainOptionError(argv[0], "-acqonce", "Only meant for -daemon mode");
   }

   if ((mainOpts.startUiCni != 0) && mainOpts.optAcqPassive)
      MainOptionError(argv[0], "-provider", "Useless together with -acqpassive in daemon mode");
   if ( IS_DUMP_MODE(mainOpts) &&
        (mainOpts.startUiCni == 0) && (mainOpts.pXmlDatabase == NULL) )
      MainOptionError(argv[0], "-dump", "Must also specify -provider");
   if ( !IS_DUMP_MODE(mainOpts) && (*mainOpts.optDumpFilter != NULL) )
      MainOptionError(argv[0], "-epgquery", "only useful together with -dump");
   if ( !IS_GUI_MODE(mainOpts) && (mainOpts.pDemoDatabase != NULL))
      MainOptionError(argv[0], "-demo", "Cannot be combined with special modes");
#ifdef USE_XMLTV_IMPORT
   if ((mainOpts.pXmlDatabase != NULL) && (mainOpts.startUiCni != 0))
      MainOptionError(argv[0], "-prov", "Cannot be used together with XMLTV database");
#endif
}

// ---------------------------------------------------------------------------
// Initialize config struct with default values
//
static void CmdLine_SetDefaults( bool daemonOnly )
{
#ifndef WIN32
   char * pEnvPath;
   char * pTmp;
#endif

   memset(&mainOpts, 0, sizeof(mainOpts));

   mainOpts.daemonOnly = daemonOnly;

#ifndef WIN32
   pEnvPath = getenv("HOME");
   if (pEnvPath != NULL)
   {
      pTmp = xmalloc(strlen(pEnvPath) + 1 + strlen(".nxtvepgrc") + 1);
      strcpy(pTmp, pEnvPath);
      strcat(pTmp, "/.nxtvepgrc");
      mainOpts.rcfile = pTmp;
   }
   else
      mainOpts.rcfile = xstrdup(".nxtvepgrc");
   mainOpts.defaultDbDir  = NULL;
#else
   mainOpts.defaultDbDir  = ".";
   mainOpts.rcfile = xstrdup("nxtvepg.ini");
#endif
   mainOpts.dbdir  = NULL;
   mainOpts.videoCardIndex = -1;
   mainOpts.dvbPid = -1;
   mainOpts.disableAcq = FALSE;
   mainOpts.optDaemonMode = FALSE;
   mainOpts.optDumpMode = EPG_DUMP_NONE;
   mainOpts.pStdOutFileName = NULL;
   mainOpts.optRemCtrl = REM_CTRL_NONE;
#ifdef USE_DAEMON
   mainOpts.optNoDetach   = FALSE;
#endif
   mainOpts.optAcqOnce = ACQMODE_PHASE_COUNT;
   mainOpts.optAcqPassive = FALSE;
   mainOpts.startIconified = FALSE;
   mainOpts.startUiCni = 0;
   mainOpts.pDemoDatabase = NULL;
   mainOpts.pOptArgv0 = NULL;
   mainOpts.optGuiPipe = -1;

#ifndef WIN32
#ifdef EPG_DB_ENV
   pEnvPath = getenv(EPG_DB_ENV);
   if (pEnvPath != NULL)
   {
      mainOpts.defaultDbDir = xmalloc(strlen(pEnvPath) + strlen(EPG_DB_DIR) + 1+1);
      strcpy(mainOpts.defaultDbDir, pEnvPath);
      strcat(mainOpts.defaultDbDir, "/" EPG_DB_DIR);
   }
   else
#endif
   {
      mainOpts.defaultDbDir = xstrdup(EPG_DB_DIR);
   }
#endif  // not WIN32
}

// ---------------------------------------------------------------------------
// Genereric retrieval function for the provider cni
// - works for -prov and XML files given as command line arguments
//
uint CmdLine_GetStartProviderCni( void )
{
   uint cni;

   if (mainOpts.startUiCni != 0)
      cni = mainOpts.startUiCni;
#ifdef USE_XMLTV_IMPORT
   else if (mainOpts.pXmlDatabase != NULL)
      cni = XmltvCni_MapProvider(mainOpts.pXmlDatabase);
#endif
   else
      cni = 0;

   return cni;
}

// ---------------------------------------------------------------------------
// Free resources
//
void CmdLine_Destroy( void )
{
   #ifndef WIN32
   xfree((void *) mainOpts.defaultDbDir);
   #endif
   if (mainOpts.rcfile != NULL)
      xfree((void *) mainOpts.rcfile);
}

// ---------------------------------------------------------------------------
// Read and store arguments on the command line
// - note X11 commands are simply skipped here and processed by Tk later
// - upon errors usage is printed and the program is terminated
//
void CmdLine_Process( int argc, char * argv[], bool daemonOnly )
{
#ifdef WIN32
   SetWorkingDirectoryFromExe(argv[0]);
#endif

   CmdLine_SetDefaults(daemonOnly);

   CmdLine_Parse(argc, argv);

   if (daemonOnly && (mainOpts.optDaemonMode == EPG_GUI_START)
                  && (mainOpts.optDumpMode == EPG_DUMP_NONE) )
   {
      mainOpts.optDaemonMode = DAEMON_RUN;
   }
   else if (daemonOnly && (mainOpts.optDumpMode == EPG_DUMP_HTML))
   {
      // HTML export requires Tcl and several GUI modules
      MainOptionError(argv[0], "-dump html", "option unsupported non-GUI executable");
   }

   CmdLine_SetOutfile();
}

