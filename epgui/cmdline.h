/*
 *  Command line argument handling
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
 *  Description: see C source file.
 */

#ifndef __CMDLINE_H
#define __CMDLINE_H

// ---------------------------------------------------------------------------
// Type definitions
//
typedef enum
{
   EPG_DUMP_NONE,
   EPG_DUMP_TEXT,
   EPG_DUMP_XMLTV,
   EPG_DUMP_HTML,
   EPG_DUMP_COUNT
} EPG_DUMP_MODE;

typedef enum
{
   EPG_GUI_START,
   EPG_CLOCK_CTRL,
   DAEMON_RUN,
   DAEMON_QUERY,
   DAEMON_STOP
} EPG_DAEMON_MODE;

typedef enum
{
   REM_CTRL_NONE,
   REM_CTRL_QUIT,
   REM_CTRL_RAISE,
   REM_CTRL_ICONIFY,
   REM_CTRL_DEICONIFY,
   REM_CTRL_ACQ_ON,
   REM_CTRL_ACQ_OFF,
   REM_CTRL_COUNT
} EPG_REMCTRL_CMD;

typedef enum
{
   CLOCK_CTRL_NONE,
   CLOCK_CTRL_SET,
   CLOCK_CTRL_PRINT,
   CLOCK_CTRL_COUNT
} EPG_CLOCK_CTRL_MODE;

#define IS_DAEMON(X)          ((X).optDaemonMode == DAEMON_RUN)
#define IS_CLOCK_MODE(X)      ((X).optDaemonMode == EPG_CLOCK_CTRL)
#define IS_DUMP_MODE(X)       ((X).optDumpMode != EPG_DUMP_NONE)
#define IS_GUI_MODE(X)       (((X).optDaemonMode == EPG_GUI_START) && \
                              ((X).optDumpMode == EPG_DUMP_NONE) && \
                              ((X).optRemCtrl == REM_CTRL_NONE))
#define IS_REMCTL_MODE(X)     ((X).optRemCtrl != REM_CTRL_NONE)

#define OPT_DUMP_FILTER_MAX   20


// command line options
typedef struct
{
#ifdef WIN32
   const char * defaultDbDir;
   const char * defaultRcFile;
#else
   char       * defaultRcFile;
   char       * defaultDbDir;
   char       * pTvX11Display;
#endif
   char       * rcfile;
   char       * dbdir;
   int          videoCardIndex;
   bool         disableAcq;
   EPG_DAEMON_MODE optDaemonMode;
   EPG_DUMP_MODE optDumpMode;
   int          optDumpSubMode;
   EPG_CLOCK_CTRL_MODE optClockSubMode;
   const char * optDumpFilter[OPT_DUMP_FILTER_MAX];
   EPG_REMCTRL_CMD optRemCtrl;
   const char * pStdOutFileName;
#ifdef USE_DAEMON
   bool         optNoDetach;
#endif
   EPGACQ_PHASE optAcqOnce;
   bool         optAcqPassive;
   bool         startIconified;
   uint         xmlDatabaseCnt;
   const char * const * ppXmlDatabases;
   const char * pOptArgv0;
   int          optGuiPipe;
   bool         daemonOnly;
} CMDLINE_OPTS;


// ---------------------------------------------------------------------------
// Global variables
//
extern CMDLINE_OPTS mainOpts;

// software version in form of a string
extern const char *epg_version_str;
extern const char epg_rcs_id_str[];


// ---------------------------------------------------------------------------
// Interface to main modules
//
void CmdLine_Process( int argc, char * argv[], bool daemonOnly );
uint CmdLine_GetXmlFileNames( const char * const ** pppList );
void CmdLine_Destroy( void );

void CmdLine_AddRcFilePostfix( const char * pPostfix );

#ifdef WIN32
void CmdLine_WinApiSetArgv( int * argcPtr, char *** argvPtr );
#endif

#endif // __CMDLINE_H
