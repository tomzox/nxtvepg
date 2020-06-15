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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: cmdline.h,v 1.2 2005/01/08 15:16:14 tom Exp tom $
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
   EPG_DUMP_RAW,
   EPG_DUMP_COUNT
} EPG_DUMP_MODE;

typedef enum
{
   EPG_GUI_START,
   EPG_CLOCK_CTRL,
   EPG_CL_PROV_SCAN,
   DAEMON_RUN,
   DAEMON_STOP
} EPG_DAEMON_MODE;

#define IS_DAEMON(X)          ((X).optDaemonMode == DAEMON_RUN)
#define IS_CLOCK_MODE(X)      ((X).optDaemonMode == EPG_CLOCK_CTRL)
#define IS_DUMP_MODE(X)       ((X).optDumpMode != EPG_DUMP_NONE)
#define IS_GUI_MODE(X)       (((X).optDaemonMode == EPG_GUI_START) && ((X).optDumpMode == EPG_DUMP_NONE))


// command line options
typedef struct
{
#ifdef WIN32
   const char * defaultDbDir;
#else
   char       * defaultDbDir;
   char       * pTvX11Display;
#endif
   bool         isUserRcFile;
   const char * rcfile;
   const char * dbdir;
   int          videoCardIndex;
   bool         disableAcq;
   EPG_DAEMON_MODE optDaemonMode;
   EPG_DUMP_MODE optDumpMode;
   uint          optDumpSubMode;
   const char * pStdOutFileName;
#ifdef USE_DAEMON
   bool         optNoDetach;
#endif
   EPGACQ_PHASE optAcqOnce;
   bool         optAcqPassive;
   bool         startIconified;
   uint         startUiCni;
   const char * pDemoDatabase;
   const char * pOptArgv0;
   int          optGuiPipe;
#ifdef WIN32
   uint         optRemCtrl;
#endif
   bool         daemonOnly;
} CMDLINE_OPTS;


// ---------------------------------------------------------------------------
// Global variables
//
extern CMDLINE_OPTS mainOpts;

// software version in form of a string
extern char *epg_version_str;
extern char epg_rcs_id_str[];


// ---------------------------------------------------------------------------
// Interface to main modules
//
void CmdLine_Process( int argc, char * argv[], bool daemonOnly );
void CmdLine_Destroy( void );

void CmdLine_AddRcFilePostfix( const char * pPostfix );
bool IsDemoMode( void );

#ifdef WIN32
void CmdLine_WinApiSetArgv( int * argcPtr, char *** argvPtr );
#endif

#endif // __CMDLINE_H
