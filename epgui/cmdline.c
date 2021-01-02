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
 *  $Id: cmdline.c,v 1.18 2020/06/30 06:32:28 tom Exp tom $
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
char *epg_version_str ATTRIBUTE_USED = EPG_VERSION_STR;
char epg_rcs_id_str[] ATTRIBUTE_USED = EPG_VERSION_RCS_ID;



// ---------------------------------------------------------------------------
// Append a postfix string to the config file name
// - used to avoid overwriting the original rc file if it's from a newer version
//
void CmdLine_AddRcFilePostfix( const char * pPostfix )
{
   const char * pRcFile;
   char * pTmp;

   if (pPostfix != NULL)
   {
      if (mainOpts.rcfile != NULL)
         pRcFile = mainOpts.rcfile;
      else
         pRcFile = mainOpts.defaultRcFile;

      if (pRcFile != NULL)
      {
         pTmp = xmalloc(strlen(pRcFile) + 1 + strlen(pPostfix) + 1);
         strcpy(pTmp, pRcFile);
         strcat(pTmp, ".");
         strcat(pTmp, pPostfix);

         if (mainOpts.rcfile != NULL)
            xfree((void *) mainOpts.rcfile);
         mainOpts.rcfile = pTmp;
      }
      else
         fatal0("CmdLine-AddRcFilePostfix: NULL rcfile name");
   }
   else
      debug0("CmdLine-AddRcFilePostfix: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Print error message and exit
// - same to "usage" function, but without printing all the options
//
static void MainOptionError( const char *argv0, const char *argvn, const char * reason )
{
#ifdef WIN32
   char * pBuf;
   int len, plen;
#endif
   const char * const pUsageFmt =
                   "%s: %s: %s\n"
                   "Usage: %s [OPTIONS] [DATABASE]\n"
                   "Try '%s -help' or refer to the manual page for more information.\n";

#ifndef WIN32
   fprintf(stderr, pUsageFmt, argv0, reason, argvn, argv0, argv0);
#else
   len = strlen(pUsageFmt) + strlen(argv0)*3 +
         strlen(reason) + strlen(argvn);
   pBuf = xmalloc(len + 1);
   plen = snprintf(pBuf, len, pUsageFmt, argv0, reason, argvn, argv0, argv0);
   assert(plen < len);
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
                   "       -dbdir <path>       \t: directory for teletext XMLTV files\n"
                   #ifndef WIN32
                   "                           \t: default: $HOME/.cache/nxtvepg\n"
                   #endif
                   "       -outfile <path>     \t: target file for export and other output\n"
                   "       -dump xml|html|pi|...\t: export database in various formats\n"
                   "       -epgquery <string>  \t: apply given filter command to export\n"
                   "       -card <digit>       \t: index of TV card for acq (starting at 0)\n"
                   ;


   const char * const pGuiUsage =
                   "       -remctrl <cmd>      \t: remote control nxtvepg\n"
                   "       -noacq              \t: don't start acquisition automatically\n"
                   "       -daemon             \t: don't open any windows; acquisition only\n";

   const char * const pDaemonUsage =
                   #ifdef USE_DAEMON
                   "       -daemonstop         \t: terminate background acquisition process\n"
                   "       -daemonquery        \t: print daemon acquisition status\n"
                   "       -acqpassive         \t: force daemon to passive acquisition mode\n"
                   "       -acqonce <phase>    \t: stop acquisition after the given stage\n"
                   "       -nodetach           \t: daemon remains connected to tty\n"
                   #endif
                   "       -clock set|print    \t: set system clock from teletext clock\n";

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
#ifdef WIN32
      if (argv0[len] == '\\')
#else
      if (argv0[len] == '/')
#endif
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
      else if (strcasecmp("xml5utc", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_XMLTV;
         mainOpts.optDumpSubMode = DUMP_XMLTV_DTD_5_GMT;
      }
      else if (strcasecmp("xml5ltz", pModeStr) == 0)
      {
         mainOpts.optDumpMode = EPG_DUMP_XMLTV;
         mainOpts.optDumpSubMode = DUMP_XMLTV_DTD_5_LTZ;
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
   uint errCode;

   mainOpts.pOptArgv0 = argv[0];

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
         else if (!strcmp(argv[argIdx], "-daemonquery"))
         {  // kill daemon process, then exit
            mainOpts.optDaemonMode = DAEMON_QUERY;
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
                  mainOpts.optAcqOnce = ACQMODE_PHASE_FULL;
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
               // use dup to allow changing apending suffix upon version conflict
               mainOpts.rcfile = xstrdup(argv[argIdx + 1]);
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
      else
      {  // database file arguments
         mainOpts.ppXmlDatabases = (const char * const *) &argv[argIdx];
         mainOpts.xmlDatabaseCnt = argc - argIdx;

         if (mainOpts.xmlDatabaseCnt > MAX_MERGED_DB_COUNT)
         {
            char str_buf[200];
            snprintf(str_buf, sizeof(str_buf), "too many XML files (max. %d)", MAX_MERGED_DB_COUNT);
            str_buf[sizeof(str_buf) - 1] = 0;
            MainOptionError(argv[0], argv[argIdx], str_buf);
         }

         for ( ; argIdx < argc; ++argIdx)
         {
            errCode = Xmltv_CheckHeader(argv[argIdx]);
            if (errCode != EPGDB_RELOAD_OK)
            {
               if (errCode == EPGDB_RELOAD_EXIST)
                  MainOptionError(argv[0], argv[argIdx], "database file does not exist");
               else if (errCode == EPGDB_RELOAD_ACCESS)
                  MainOptionError(argv[0], argv[argIdx], "database file cannot be opened");
               else
                  MainOptionError(argv[0], argv[argIdx], Xmltv_TranslateErrorCode(errCode & ~EPGDB_RELOAD_XML_MASK));
            }
         }
         // all remaining arguments are assumed to be file parameters
         break;
      }
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
      else if (mainOpts.xmlDatabaseCnt > 0)
         MainOptionError(argv[0], mainOpts.ppXmlDatabases[0], "no database files with -daemon/-clock");
   }
   else if (mainOpts.daemonOnly)
   {
      if (mainOpts.disableAcq)
         MainOptionError(argv[0], "-noacq", "Cannot be used with daemon executable");
      else if (IS_REMCTL_MODE(mainOpts))
         MainOptionError(argv[0], "-remctrl", "Cannot be used with daemon executable");
      else if (mainOpts.xmlDatabaseCnt > 0)
         MainOptionError(argv[0], mainOpts.ppXmlDatabases[0], "Database files not loaded by daemon executable");
   }
   else
   #endif
   {
      if (mainOpts.optAcqPassive)
         MainOptionError(argv[0], "-acqpassive", "Only meant for -daemon mode");
      if (mainOpts.optAcqOnce != ACQMODE_PHASE_COUNT)
         MainOptionError(argv[0], "-acqonce", "Only meant for -daemon mode");
   }

   if ( IS_DUMP_MODE(mainOpts) && (mainOpts.ppXmlDatabases == NULL) )
      MainOptionError(argv[0], "-dump", "Must also specify a database file to load");
   if ( !IS_DUMP_MODE(mainOpts) && (*mainOpts.optDumpFilter != NULL) )
      MainOptionError(argv[0], "-epgquery", "only useful together with -dump");
}

#ifndef WIN32
// ---------------------------------------------------------------------------
// Helper function for allocating a new string for a concatenation of strings
//
static char * CmdLine_ConcatPaths( const char * p1, const char * p2, const char * p3 )
{
   char * pStr;

   if (p3 != NULL)
   {
      pStr = xmalloc(strlen(p1) + 1 + strlen(p2) + 1 + strlen(p3) + 1);
      sprintf(pStr, "%s/%s/%s", p1, p2, p3);
   }
   else
   {
      pStr = xmalloc(strlen(p1) + 1 + strlen(p2) + 1);
      sprintf(pStr, "%s/%s", p1, p2);
   }
   return pStr;
}

// ---------------------------------------------------------------------------
// Create config and cache directories
//
static void CmdLine_CreateTree( const char * pPath, bool isDir )
{
   const char * pPathEnd;
   char * pTmp;

   if (access(pPath, F_OK) != 0)
   {
      pTmp = xmalloc(strlen(pPath) + 1);
      if (pPath[0] == '/')
         pPathEnd = strchr(pPath + 1, '/');
      else
         pPathEnd = strchr(pPath, '/');

      while (pPathEnd != NULL)
      {
         strncpy(pTmp, pPath, pPathEnd - pPath);
         pTmp[pPathEnd - pPath] = 0;

         if (access(pTmp, F_OK) != 0)
         {
            if (mkdir(pTmp, 0777) != 0)
            {
               fprintf(stderr, "nxtvepg: failed to set-up directory %s: %s\n",
                       pTmp, strerror(errno));
               break;
            }
            dprintf1("CmdLine-CreateTree: created dir %s\n", pTmp);
         }

         if (*pPathEnd != 0)
         {
            pPathEnd = strchr(pPathEnd + 1, '/');
            if ((pPathEnd == NULL) && isDir)
            {
               pPathEnd = pPath + strlen(pPath);
               isDir = FALSE;
            }
         }
         else
            pPathEnd = NULL;
      }
      xfree(pTmp);
   }
}
#endif // WIN32

static void CmdLine_CreateConfigDir( void )
{
#ifndef WIN32
   const char * pRcFile = ((mainOpts.rcfile != NULL) ? mainOpts.rcfile : mainOpts.defaultRcFile);
   const char * pDbDir = ((mainOpts.dbdir != NULL) ? mainOpts.dbdir : mainOpts.defaultDbDir);

   CmdLine_CreateTree(pRcFile, FALSE);
   CmdLine_CreateTree(pDbDir, TRUE);
#endif
}

// ---------------------------------------------------------------------------
// Initialize config struct with default values
//
static void CmdLine_SetDefaults( bool daemonOnly )
{
   memset(&mainOpts, 0, sizeof(mainOpts));

   mainOpts.daemonOnly = daemonOnly;
   mainOpts.dbdir = NULL;
   mainOpts.rcfile = NULL;
   mainOpts.videoCardIndex = -1;
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
   mainOpts.pOptArgv0 = NULL;
   mainOpts.optGuiPipe = -1;

#ifndef WIN32
   char * pEnvHome = getenv("HOME");

   char * pEnvConfig = getenv("XDG_CONFIG_HOME");
   if (pEnvConfig == NULL)
   {
      if (pEnvHome != NULL)
         pEnvConfig = CmdLine_ConcatPaths(pEnvHome, ".config", "nxtvepg/nxtvepgrc");
      else
         pEnvConfig = xstrdup("nxtvepgrc");
   }
   else
      pEnvConfig = CmdLine_ConcatPaths(pEnvConfig, "nxtvepg/nxtvepgrc", NULL);

   char * pEnvCache = getenv("XDG_CACHE_HOME");
   if (pEnvCache == NULL)
   {
      if (pEnvHome != NULL)
         pEnvCache = CmdLine_ConcatPaths(pEnvHome, ".cache", "nxtvepg");
      else
         pEnvCache = xstrdup(".");
   }
   else
      pEnvCache = CmdLine_ConcatPaths(pEnvCache, "nxtvepg", NULL);

   mainOpts.defaultRcFile = pEnvConfig;
   mainOpts.defaultDbDir = pEnvCache;

#else  // WIN32
   mainOpts.defaultRcFile = xstrdup("nxtvepg.ini");
   mainOpts.defaultDbDir = ".";
#endif
}

// ---------------------------------------------------------------------------
// Retrieval function for the XMLTV file name(s) listed on the command line
//
uint CmdLine_GetXmlFileNames( const char * const ** pppList )
{
   *pppList = mainOpts.ppXmlDatabases;
   return mainOpts.xmlDatabaseCnt;
}

// ---------------------------------------------------------------------------
// Free resources
//
void CmdLine_Destroy( void )
{
#ifndef WIN32
   if (mainOpts.defaultRcFile != NULL)
      xfree((void *) mainOpts.defaultRcFile);
   if (mainOpts.defaultDbDir != NULL)
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

   CmdLine_CreateConfigDir();

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

