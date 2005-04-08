/*
 *  Nextview GUI: Call external command with a programme's parameters
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
 *    This module implements methods to call external commands via the
 *    context menu (right-click on programme item) and pass them parameters
 *    of the currently selected programme item.  Intended use is for example
 *    and external scheduler or record application.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: shellcmd.c,v 1.10 2005/02/20 20:25:31 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <signal.h>
#else
#include <windows.h>
#endif

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/syserrmsg.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgmain.h"
#include "epgui/pdc_themes.h"
#include "epgui/pibox.h"
#include "epgui/pidescr.h"
#include "epgui/shellcmd.h"

#ifdef WIN32
# include "epgui/wintv.h"
# define USERCMD_PREFIX_TVAPP  "!wintv!"
# define USERCMD_PREFIX_TVAPP_LEN  7
#else
# include "epgui/xawtv.h"
# define USERCMD_PREFIX_TVAPP  "!xawtv!"
# define USERCMD_PREFIX_TVAPP_LEN  7
#endif


// keyword list to map strings to indices defined in enum below
// note: ordering not relevant, but new entries mut be handled in dlg_ctxmencf.tcl
static CONST84 char * pCtxMenuTypeKeywords[] =
{
   "exec.unix",
   "exec.win32",
   "tvapp.xawtv",
   "tvapp.wintv",
   "menu_separator",
   "menu_title",
   "pi_context.addfilt",
   "pi_context.undofilt",
   "pi_context.reminder_short",
   "pi_context.reminder_ext",
   (char *) NULL
};

typedef enum
{
   CTX_TYPE_EXEC_UNIX,
   CTX_TYPE_EXEC_WIN32,
   CTX_TYPE_TVAPP_XAWTV,
   CTX_TYPE_TVAPP_WINTV,
   CTX_TYPE_SEP,
   CTX_TYPE_TITLE,
   CTX_TYPE_ADDFILT,
   CTX_TYPE_UNDOFILT,
   CTX_TYPE_REMIND_SHORT,
   CTX_TYPE_REMIND_EXT,
   CTX_TYPE_COUNT
} CTXMEN_TYPE;

#ifndef WIN32
#define CTX_TYPE_EXEC_DEFAULT   CTX_TYPE_EXEC_UNIX
#define CTX_TYPE_TVAPP_DEFAULT  CTX_TYPE_TVAPP_XAWTV
#else
#define CTX_TYPE_EXEC_DEFAULT   CTX_TYPE_EXEC_WIN32
#define CTX_TYPE_TVAPP_DEFAULT  CTX_TYPE_TVAPP_WINTV
#endif

// struct to hold dynamically growing char buffer
typedef struct
{
   char   * strbuf;         // pointer to the allocated buffer
   uint     size;           // number of allocated bytes in the buffer
   uint     off;            // number of already used bytes
   bool     quoteShell;     // TRUE to enable UNIX quoting of spaces etc.
} DYN_CHAR_BUF;


// ----------------------------------------------------------------------------
// Append one character to the dynamically growing output buffer
//
static void ShellCmd_AppendChar( char c, DYN_CHAR_BUF * pCmdBuf )
{
   char * newbuf;

   if (pCmdBuf->off == pCmdBuf->size)
   {
      newbuf = xmalloc(pCmdBuf->size + 2048);
      memcpy(newbuf, pCmdBuf->strbuf, pCmdBuf->size);
      xfree(pCmdBuf->strbuf);

      pCmdBuf->strbuf = newbuf;
      pCmdBuf->size  += 2048;
   }

   pCmdBuf->strbuf[pCmdBuf->off++] = c;
}

// ----------------------------------------------------------------------------
// Safely append a quoted string to a Bourne shell command list
// - the string will be enclosed by single quotes (not null terminated)
// - single quotes in the string are escaped
// - control characters (e.g. new line) are replaced by blank
//
static void ShellCmd_AppendString( DYN_CHAR_BUF * pCmdBuf, const char * ps )
{
   if (pCmdBuf->quoteShell)
   {
#ifndef WIN32
      // quote the complete string, because there are just too many chars with
      // special meaning to the shell (e.g. $, &, |, ...) to escape them all
      ShellCmd_AppendChar('\'', pCmdBuf);

      while (*ps != 0)
      {
         if (*ps == '\'')
         {  // single quote -> escape it
            ps++;
            ShellCmd_AppendChar('\'', pCmdBuf);
            ShellCmd_AppendChar('\\', pCmdBuf);
            ShellCmd_AppendChar('\'', pCmdBuf);
            ShellCmd_AppendChar('\'', pCmdBuf);
         }
         else if ( ((uint) *ps) < ' ' )
         {  // control character -> replace it with blank
            ps++;
            ShellCmd_AppendChar(' ', pCmdBuf);
         }
         else
            ShellCmd_AppendChar(*(ps++), pCmdBuf);
      }

      ShellCmd_AppendChar('\'', pCmdBuf);
#else
      // WIN32: tere are much less control chars, so we escape only these
      while (*ps != 0)
      {
         if ((*ps == '\'') || (*ps == '\"') || (*ps == ' '))
         {  // escape the character
            ShellCmd_AppendChar('\\', pCmdBuf);
         }
         ShellCmd_AppendChar(*(ps++), pCmdBuf);
      }
#endif
   }
   else
   {
      while (*ps != 0)
         ShellCmd_AppendChar(*(ps++), pCmdBuf);
   }
}

// ----------------------------------------------------------------------------
// Callback function for PiOutput-AppendShortAndLongInfoText
//
static void ShellCmd_AppendStringInfoTextCb( void *fp, const char * pDesc, bool addSeparator )
{
   DYN_CHAR_BUF * pCmdBuf = (DYN_CHAR_BUF *) fp;
   char * pNewline;

   if ((pCmdBuf != NULL) && (pDesc != NULL))
   {
      if (addSeparator)
         ShellCmd_AppendString(pCmdBuf, " //// ");

      // replace newline characters with "paragraph separators"
      while ( (pNewline = strchr(pDesc, '\n')) != NULL )
      {
         // print text up to (and excluding) the newline
         *pNewline = 0;  // XXX must not modify const string
         ShellCmd_AppendString(pCmdBuf, pDesc);
         ShellCmd_AppendString(pCmdBuf, " // ");
         // skip to text following the newline
         pDesc = pNewline + 1;
      }
      // write the segement behind the last newline
      ShellCmd_AppendString(pCmdBuf, pDesc);
   }
}

// ----------------------------------------------------------------------------
// Process a keyword and modifier in a user-defined command line
// - the result is written to the output string (not null terminated)
// - unknown keyword is replaced with ${keyword: unknown vaiable}
//
static void PiOutput_ExtCmdVariable( const PI_BLOCK *pPiBlock, const AI_BLOCK * pAiBlock,
                                     const char * pKeyword, uint keywordLen,
                                     const char * pModifier, uint modifierLen,
                                     DYN_CHAR_BUF * pCmdBuf )
{
   const char * pConst;
   char  modbuf[100];
   char  strbuf[200];
   char  timeOpBuf[1+1];
   uint  idx;
   int   timeOpVal;
   struct tm vpsTime;
   time_t tdiff;
   time_t now = time(NULL);

   if (modifierLen >= sizeof(modbuf))
      modifierLen = sizeof(modbuf) - 1;
   timeOpVal = 0;

   if ( (strncmp(pKeyword, "start", keywordLen) == 0) ||
        (sscanf(pKeyword, "start%1[+-]%d", timeOpBuf, &timeOpVal) == 2) )
   {  // start time, possible +/- a given offset
      if (pModifier != NULL)
      {
         strncpy(modbuf, pModifier, modifierLen);
         modbuf[modifierLen] = 0;
      }
      else
         strcpy(modbuf, "%H:%M-%d.%m.%Y");

      if (timeOpBuf[0] == '+')
         tdiff = pPiBlock->start_time + (timeOpVal * 60);
      else
         tdiff = pPiBlock->start_time - (timeOpVal * 60);

      strftime(strbuf, sizeof(strbuf), modbuf, localtime(&tdiff));
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else if ( (strncmp(pKeyword, "stop", keywordLen) == 0) ||
             (sscanf(pKeyword, "stop%1[+-]%d", timeOpBuf, &timeOpVal) == 2) )
   {  // stop time, possible +/- a given offset
      if (pModifier != NULL)
      {
         strncpy(modbuf, pModifier, modifierLen);
         modbuf[modifierLen] = 0;
      }
      else
         strcpy(modbuf, "%H:%M-%d.%m.%Y");

      if (timeOpBuf[0] == '+')
         tdiff = pPiBlock->stop_time + (timeOpVal * 60);
      else
         tdiff = pPiBlock->stop_time - (timeOpVal * 60);

      strftime(strbuf, sizeof(strbuf), modbuf, localtime(&tdiff));
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else if ( (strncmp(pKeyword, "duration", keywordLen) == 0) ||
             (sscanf(pKeyword, "duration%1[+-]%d", timeOpBuf, &timeOpVal) == 2) )
   {  // duration = stop - start time; by default in minutes
      if (now >= pPiBlock->stop_time)
         tdiff = 0;
      else if (now >= pPiBlock->start_time)
         tdiff = pPiBlock->stop_time - now;
      else
         tdiff = pPiBlock->stop_time - pPiBlock->start_time;

      if ((pModifier == NULL) || (*pModifier == 'm'))
         tdiff /= 60;

      if (timeOpBuf[0] == '+')
         tdiff += timeOpVal;
      else
         tdiff -= timeOpVal;

      sprintf(strbuf, "%d", (int) tdiff);
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else if ( (strncmp(pKeyword, "relstart", keywordLen) == 0) ||
             (sscanf(pKeyword, "relstart%1[+-]%d", timeOpBuf, &timeOpVal) == 2) )
   {  // relative start = start time - now; by default in minutes
      if (now >= pPiBlock->start_time)
         tdiff = 0;
      else
         tdiff = pPiBlock->start_time - now;
      if ((pModifier == NULL) || (*pModifier == 'm'))
         tdiff /= 60;

      if (timeOpBuf[0] == '+')
         tdiff += timeOpVal;
      else
         tdiff -= timeOpVal;

      sprintf(strbuf, "%d", (int) tdiff);
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "CNI", keywordLen) == 0)
   {  // network CNI (hexadecimal)
      sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "network", keywordLen) == 0)
   {  // network name
      sprintf(strbuf, "0x%04X", AI_GET_NETWOP_N(pAiBlock, pPiBlock->netwop_no)->cni);
      pConst = Tcl_GetVar2(interp, "cfnetnames", strbuf, TCL_GLOBAL_ONLY);
      if (pConst != NULL)
         ShellCmd_AppendString(pCmdBuf, pConst);
      else
         ShellCmd_AppendString(pCmdBuf, AI_GET_NETWOP_NAME(pAiBlock, pPiBlock->netwop_no));
   }
   else if (strncmp(pKeyword, "title", keywordLen) == 0)
   {  // programme title string
      ShellCmd_AppendString(pCmdBuf, PI_GET_TITLE(pPiBlock));
   }
   else if (strncmp(pKeyword, "description", keywordLen) == 0)
   {  // programme description (short & long info)
      PiDescription_AppendShortAndLongInfoText(pPiBlock, ShellCmd_AppendStringInfoTextCb, pCmdBuf, EpgDbContextIsMerged(pUiDbContext));
   }
   else if (strncmp(pKeyword, "themes", keywordLen) == 0)
   {  // PDC themes
      if ((pModifier == NULL) || (*pModifier == 't'))
      {  // output in cleartext
         PiDescription_AppendCompressedThemes(pPiBlock, strbuf, sizeof(strbuf));
         ShellCmd_AppendString(pCmdBuf, strbuf);
      }
      else
      {  // numerical output
         for (idx=0; idx < pPiBlock->no_themes; idx++)
         {
            sprintf(strbuf, "%s%d", ((idx > 0) ? "," : ""), pPiBlock->themes[idx]);
            ShellCmd_AppendString(pCmdBuf, strbuf);
         }
      }
   }
   else if ( (strncmp(pKeyword, "VPS", keywordLen) == 0) ||
             (strncmp(pKeyword, "PDC", keywordLen) == 0) )
   {  // VPS/PDC time code
      if (EpgDbGetVpsTimestamp(&vpsTime, pPiBlock->pil, pPiBlock->start_time))
      {
         if (pModifier != NULL)
         {
            strncpy(modbuf, pModifier, modifierLen);
            modbuf[modifierLen] = 0;
         }
         else
            strcpy(modbuf, "%H:%M-%d.%m.%Y");
         strftime(strbuf, sizeof(strbuf), modbuf, &vpsTime);
         ShellCmd_AppendString(pCmdBuf, strbuf);
      }
      else
      {  // invalid VPS/PDC label
         ShellCmd_AppendString(pCmdBuf, "none");
      }
   }
   else if (strncmp(pKeyword, "sound", keywordLen) == 0)
   {
      switch(pPiBlock->feature_flags & 0x03)
      {
         default:
         case 0: pConst = "mono/unknown"; break;
         case 1: pConst = "2-channel"; break;
         case 2: pConst = "stereo"; break;
         case 3: pConst = "surround"; break;
      }
      ShellCmd_AppendString(pCmdBuf, pConst);
   }
   else if (strncmp(pKeyword, "format", keywordLen) == 0)
   {
      if (pPiBlock->feature_flags & 0x08)
         ShellCmd_AppendString(pCmdBuf, "PALplus");
      else if (pPiBlock->feature_flags & 0x04)
         ShellCmd_AppendString(pCmdBuf, "wide");
      else
         ShellCmd_AppendString(pCmdBuf, "normal/unknown");
   }
   else if (strncmp(pKeyword, "digital", keywordLen) == 0)
   {
      ShellCmd_AppendString(pCmdBuf, (pPiBlock->feature_flags & 0x10) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "encrypted", keywordLen) == 0)
   {
      ShellCmd_AppendString(pCmdBuf, (pPiBlock->feature_flags & 0x20) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "live", keywordLen) == 0)
   {
      ShellCmd_AppendString(pCmdBuf, (pPiBlock->feature_flags & 0x40) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "repeat", keywordLen) == 0)
   {
      ShellCmd_AppendString(pCmdBuf, (pPiBlock->feature_flags & 0x80) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "subtitle", keywordLen) == 0)
   {
      ShellCmd_AppendString(pCmdBuf, (pPiBlock->feature_flags & 0x100) ? "yes" : "no");
   }
   else if (strncmp(pKeyword, "e_rating", keywordLen) == 0)
   {  // editorial rating (0 means "none")
      sprintf(strbuf, "%d", pPiBlock->editorial_rating);
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "p_rating", keywordLen) == 0)
   {  // parental  rating (0 means "none", 1 "all")
      sprintf(strbuf, "%d", pPiBlock->parental_rating * 2);
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else
   {
      strcpy(strbuf, "${");
      strncpy(strbuf + 2, pKeyword, keywordLen);
      strbuf[2 + keywordLen] = 0;
      strcat(strbuf, ": unknown variable}");
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
}

// ----------------------------------------------------------------------------
// Substitute variables in UNIX Bourne Shell command line
// - at this point we have: one PI and one command specification string
// - now we parse the spec string and replace all ${variable:modifiers} by PI data
//
static void PiOutput_CmdSubstitute( const char * pUserCmd, DYN_CHAR_BUF * pCmdBuf, bool breakSpace,
                                    const PI_BLOCK * pPiBlock, const AI_BLOCK * pAiBlock )
{
   const char *ps, *pKeyword, *pModifier;
   uint keywordLen;
   uint modifierLen;

   ps = pUserCmd;
   while (*ps != 0)
   {
      if ( (*ps == '$') && (*(ps + 1) == '{') )
      {
         // found variable start -> search for closing brace
         pKeyword    = ps + 2;
         pModifier   = NULL;
         keywordLen  = 0;
         modifierLen = 0;

         ps += 2;
         while ( (*ps != 0) && (*ps != '}') )
         {
            // check for a modifier string appended to the variable name after ':'
            if ((*ps == ':') && (pModifier == NULL))
            {  // found -> break off the modstr from the varstr by replacing ':' with 0
               keywordLen = ps - pKeyword;
               pModifier  = ps + 1;
            }
            ps++;
         }
         if (*ps != 0)
         {  // found closing brace
            if (pModifier != NULL)
               modifierLen = ps - pModifier;
            else
               keywordLen  = ps - pKeyword;
            // replace the ${} construct with the actual content
            PiOutput_ExtCmdVariable(pPiBlock, pAiBlock,
                                    pKeyword, keywordLen,
                                    pModifier, modifierLen,
                                    pCmdBuf);
            ps += 1;
         }
         else
         {  // syntax error: no closing brace -> treat it as normal text
            // track back to the '$' and append it to the output
            ps = pKeyword - 2;
            ShellCmd_AppendChar(*(ps++), pCmdBuf);
         }
      }
      else
      {  // outside of variable -> just append the character
         if ((*ps == ' ') && (breakSpace))
            ShellCmd_AppendChar('\0', pCmdBuf);
         else
            ShellCmd_AppendChar(*ps, pCmdBuf);
         ps += 1;
      }
   }
}

#ifdef WIN32
// ----------------------------------------------------------------------------
// Windows helper function to have the system execute a command
// - the command is started withtout a console window
// - note on WIN32 the command is started asynchronously in the background
//
static void PiOutput_Win32SystemExec( Tcl_Interp *interp, const char * pCmdStr )
{
   STARTUPINFO          startup;
   PROCESS_INFORMATION  proc_info;
   char  * pError;

   memset(&startup, 0, sizeof(startup));
   memset(&proc_info, 0, sizeof(proc_info));

   if (CreateProcess(NULL, (char *) pCmdStr, NULL, NULL, FALSE,
                     CREATE_NO_WINDOW, NULL, NULL, &startup, &proc_info) == FALSE)
   {
      // failed to start the command -> display popup message
      pError = NULL;
      SystemErrorMessage_Set(&pError, GetLastError(), "Failed to execute command: ", NULL);
      if (pError != NULL)
      {
         sprintf(comm, "tk_messageBox -type ok -icon error -parent . -message {%s}", pError);
         eval_check(interp, comm);
         Tcl_ResetResult(interp);
         xfree(pError);
      }
   }
}
#endif  // WIN32

// ----------------------------------------------------------------------------
// Execute a user-defined command on the currently selected PI
// - invoked from the context menu
//
static int PiOutput_ExecUserCmd( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ExecUserCmd <type> <cmd>";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   const char * pUserCmd;
   Tcl_DString  ds;
   DYN_CHAR_BUF cmdbuf;
   int   type;
   int   result;

   if (objc != 3) 
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if (Tcl_GetIndexFromObj(interp, objv[1], pCtxMenuTypeKeywords,
                                "menu entry type", TCL_EXACT, &type) != TCL_OK)
   {
      result = TCL_ERROR; 
   }
   else
   {
      result = TCL_OK; 

      // allocate temporary buffer for the command line to be built
      cmdbuf.size   = 2048;
      cmdbuf.off    = 0;
      cmdbuf.strbuf = xmalloc(cmdbuf.size);

      EpgDbLockDatabase(pUiDbContext, TRUE);
      pAiBlock = EpgDbGetAi(pUiDbContext);
      // query listbox for user-selected PI, if any
      pPiBlock = PiBox_GetSelectedPi();
      if ((pAiBlock != NULL) && (pPiBlock != NULL))
      {
         pUserCmd = Tcl_UtfToExternalDString(NULL, Tcl_GetString(objv[2]), -1, &ds);

         switch(type)
         {
#ifdef WIN32
            case CTX_TYPE_TVAPP_WINTV:
#else
            case CTX_TYPE_TVAPP_XAWTV:
#endif
               // substitute and break strings apart at spaces, then pass argv to TV app
               cmdbuf.quoteShell = FALSE;
               PiOutput_CmdSubstitute(pUserCmd, &cmdbuf, TRUE, pPiBlock, pAiBlock);
               ShellCmd_AppendChar('\0', &cmdbuf);

               #ifdef WIN32
               Wintv_SendCmdArgv(interp, cmdbuf.strbuf, cmdbuf.off);
               #else
               Xawtv_SendCmdArgv(interp, cmdbuf.strbuf, cmdbuf.off);
               #endif
               break;

#ifdef WIN32
            case CTX_TYPE_EXEC_WIN32:
#else
            case CTX_TYPE_EXEC_UNIX:
#endif
               // substitute and quote variables in UNIX style, then execute the command
               cmdbuf.quoteShell = TRUE;
               PiOutput_CmdSubstitute(pUserCmd, &cmdbuf, FALSE, pPiBlock, pAiBlock);

               #ifndef WIN32
               // append a '&' to have the shell execute the command asynchronously
               // in the background; else we would hang until the command finishes.
               ShellCmd_AppendChar('&', &cmdbuf);

               // finally null-terminate the string
               ShellCmd_AppendChar('\0', &cmdbuf);

               // execute the command
               system(cmdbuf.strbuf);

               #else  // WIN32
               ShellCmd_AppendChar('\0', &cmdbuf);
               PiOutput_Win32SystemExec(interp, cmdbuf.strbuf);
               #endif
               break;

            default:
               debug1("PiOutput-ExecUserCmd: invalid type %s", Tcl_GetString(objv[1]));
               break;
         }
         Tcl_DStringFree(&ds);
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
      xfree(cmdbuf.strbuf);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Parse a user-defined command with substitutions for the given PI
// - invoked for reminder scripts
//
Tcl_Obj * PiOutput_ParseScript( Tcl_Interp *interp, Tcl_Obj * pCmdObj,
                                const PI_BLOCK * pPiBlock )
{
   const AI_BLOCK * pAiBlock;
   const char * pUserCmd;
   Tcl_DString  ds;
   Tcl_Obj    * pResultList;
   DYN_CHAR_BUF cmdbuf;
   CTXMEN_TYPE  cmdType;

   assert(EpgDbIsLocked(pUiDbContext));

   // allocate temporary buffer for the command line to be built
   cmdbuf.size   = 2048;
   cmdbuf.off    = 0;
   cmdbuf.strbuf = xmalloc(cmdbuf.size);

   pResultList = Tcl_NewListObj(0, NULL);

   pAiBlock = EpgDbGetAi(pUiDbContext);
   if ((pAiBlock != NULL) && (pPiBlock != NULL))
   {
      pUserCmd = Tcl_UtfToExternalDString(NULL, Tcl_GetString(pCmdObj), -1, &ds);
      if (strncmp(USERCMD_PREFIX_TVAPP, pUserCmd, USERCMD_PREFIX_TVAPP_LEN) == 0)
      {  // substitute and break strings apart at spaces

         // skip !wintv! keyword and following white space
         pUserCmd += USERCMD_PREFIX_TVAPP_LEN;
         while (*pUserCmd == ' ')
            pUserCmd += 1;

         cmdbuf.quoteShell = FALSE;
         PiOutput_CmdSubstitute(pUserCmd, &cmdbuf, TRUE, pPiBlock, pAiBlock);
         ShellCmd_AppendChar('\0', &cmdbuf);

         cmdType = CTX_TYPE_TVAPP_DEFAULT;
      }
      else
      {  // substitute variables and quote spaces etc.
         cmdbuf.quoteShell = TRUE;
         PiOutput_CmdSubstitute(pUserCmd, &cmdbuf, FALSE, pPiBlock, pAiBlock);

         // append a '&' to have the shell execute the command asynchronously
         // in the background; else we would hang until the command finishes.
         #ifndef WIN32
         ShellCmd_AppendChar('&', &cmdbuf);
         #endif

         ShellCmd_AppendChar('\0', &cmdbuf);

         cmdType = CTX_TYPE_EXEC_DEFAULT;
      }
      Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(pCtxMenuTypeKeywords[cmdType], -1));
      Tcl_DStringFree(&ds);

      Tcl_ListObjAppendElement(interp, pResultList,
                               Tcl_NewByteArrayObj(cmdbuf.strbuf, cmdbuf.off));
   }
   xfree(cmdbuf.strbuf);

   return pResultList;
}

// ----------------------------------------------------------------------------
// Pass an already parsed script to the system or a TV app
// - argument is a 2-element list: 1st element contains type, 2nd the string
//
static int PiOutput_ExecParsedScript( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ExecParsedScript <type> <script>";
   const char * pCmdStr;
   int    ctrl;
   int    cmdLen;
   int    result;

   if ( (objc != 1+2) ||
        (Tcl_GetIndexFromObj(interp, objv[1], pCtxMenuTypeKeywords, "keyword", TCL_EXACT, &ctrl) != TCL_OK) ||
        ((pCmdStr = Tcl_GetByteArrayFromObj(objv[2], &cmdLen)) == NULL) )
   {
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      switch (ctrl)
      {
#ifdef WIN32
         case CTX_TYPE_TVAPP_WINTV:
            Wintv_SendCmdArgv(interp, pCmdStr, cmdLen);
            break;

         case CTX_TYPE_EXEC_WIN32:
            PiOutput_Win32SystemExec(interp, pCmdStr);
            break;
#else
         case CTX_TYPE_TVAPP_XAWTV:
            Xawtv_SendCmdArgv(interp, pCmdStr, cmdLen);
            break;

         case CTX_TYPE_EXEC_UNIX:
            system(pCmdStr);
            break;
#endif

         default:
            debug1("PiOutput-ExecParsedScript: invalid keyword %s", Tcl_GetString(objv[1]));
            break;
      }
      result = TCL_OK; 
   }
   return result;
}

// ----------------------------------------------------------------------------
// Get numeric menu entry type from keyword string
//
static int ContextMenu_GetTypeList( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ContextMenu_GetTypeList";
   Tcl_Obj       * pResultList;
   CONST84 char ** pKeyword;
   int   result;

   if (objc != 1) 
   {  // wrong # of args for this TCL cmd -> display error msg
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else
   {
      pResultList = Tcl_NewListObj(0, NULL);
      pKeyword = &pCtxMenuTypeKeywords[0];
      while (*pKeyword != NULL)
      {
         Tcl_ListObjAppendElement(interp, pResultList, Tcl_NewStringObj(*pKeyword, -1));
         pKeyword += 1;
      }
      Tcl_SetObjResult(interp, pResultList);
      result = TCL_OK;
   }
   return result;
}

// ----------------------------------------------------------------------------
// Free resources allocated by this module (cleanup during program exit)
//
void ShellCmd_Destroy( void )
{
}

// ----------------------------------------------------------------------------
// Create the Tcl/Tk commands provided by this module
// - this should be called only once during start-up
//
void ShellCmd_Init( void )
{
   Tcl_CmdInfo cmdInfo;

   if (Tcl_GetCommandInfo(interp, "C_ExecUserCmd", &cmdInfo) == 0)
   {
      Tcl_CreateObjCommand(interp, "C_ExecUserCmd", PiOutput_ExecUserCmd, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_ExecParsedScript", PiOutput_ExecParsedScript, (ClientData) NULL, NULL);
      Tcl_CreateObjCommand(interp, "C_ContextMenu_GetTypeList", ContextMenu_GetTypeList, (ClientData) NULL, NULL);
   }
   else
      debug0("ShellCmd-Init: commands are already created");
}

