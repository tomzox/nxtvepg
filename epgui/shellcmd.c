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
 *  $Id: shellcmd.c,v 1.2 2003/06/28 11:23:39 tom Exp tom $
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
#endif

#include <tcl.h>
#include <tk.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgui/epgmain.h"
#include "epgui/pdc_themes.h"
#include "epgui/pibox.h"
#include "epgui/pidescr.h"
#include "epgui/shellcmd.h"

#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"
#define USERCMD_PREFIX_WINTV  "!wintv!"
#define USERCMD_PREFIX_WINTV_LEN  7


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
   uint   idx;
   struct tm vpsTime;
   time_t tdiff;
   time_t now = time(NULL);

   if (modifierLen >= sizeof(modbuf))
      modifierLen = sizeof(modbuf) - 1;

   if (strncmp(pKeyword, "start", keywordLen) == 0)
   {  // start time
      if (pModifier != NULL)
      {
         strncpy(modbuf, pModifier, modifierLen);
         modbuf[modifierLen] = 0;
      }
      else
         strcpy(modbuf, "%H:%M-%d.%m.%Y");
      strftime(strbuf, sizeof(strbuf), modbuf, localtime(&pPiBlock->start_time));
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "stop", keywordLen) == 0)
   {  // stop time
      if (pModifier != NULL)
      {
         strncpy(modbuf, pModifier, modifierLen);
         modbuf[modifierLen] = 0;
      }
      else
         strcpy(modbuf, "%H:%M-%d.%m.%Y");
      strftime(strbuf, sizeof(strbuf), modbuf, localtime(&pPiBlock->stop_time));
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "duration", keywordLen) == 0)
   {  // duration = stop - start time; by default in minutes
      if (now >= pPiBlock->stop_time)
         tdiff = 0;
      else if (now >= pPiBlock->start_time)
         tdiff = pPiBlock->stop_time - now;
      else
         tdiff = pPiBlock->stop_time - pPiBlock->start_time;
      if ((pModifier == NULL) || (*pModifier == 'm'))
         tdiff /= 60;
      sprintf(strbuf, "%d", (int) tdiff);
      ShellCmd_AppendString(pCmdBuf, strbuf);
   }
   else if (strncmp(pKeyword, "relstart", keywordLen) == 0)
   {  // relative start = start time - now; by default in minutes
      if (now >= pPiBlock->start_time)
         tdiff = 0;
      else
         tdiff = pPiBlock->start_time - now;
      if ((pModifier == NULL) || (*pModifier == 'm'))
         tdiff /= 60;
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

// ----------------------------------------------------------------------------
// Execute a user-defined command on the currently selected PI
// - invoked from the context menu
//
static int PiOutput_ExecUserCmd( ClientData ttp, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[] )
{
   const char * const pUsage = "Usage: C_ExecUserCmd <index>";
   const AI_BLOCK * pAiBlock;
   const PI_BLOCK * pPiBlock;
   Tcl_Obj    * pCtxVarObj;
   Tcl_Obj    * pCtxItemObj;
   const char * pUserCmd;
   Tcl_DString  ds;
   DYN_CHAR_BUF cmdbuf;
   int  userCmdIndex;
   int  result;

   if (objc != 2) 
   {  // parameter count is invalid
      Tcl_SetResult(interp, (char *)pUsage, TCL_STATIC);
      result = TCL_ERROR; 
   }  
   else if (Tcl_GetIntFromObj(interp, objv[1], &userCmdIndex) != TCL_OK)
   {  // illegal parameter format
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
         pCtxVarObj = Tcl_GetVar2Ex(interp, "ctxmencf", NULL, TCL_GLOBAL_ONLY);
         if (pCtxVarObj != NULL)
         {
            if ((Tcl_ListObjIndex(interp, pCtxVarObj, userCmdIndex * 2 + 1, &pCtxItemObj) == TCL_OK) && (pCtxItemObj != NULL))
            {
               pUserCmd = Tcl_UtfToExternalDString(NULL, Tcl_GetString(pCtxItemObj), -1, &ds);
               #ifdef WIN32
               if (strncmp(USERCMD_PREFIX_WINTV, pUserCmd, USERCMD_PREFIX_WINTV_LEN) == 0)
               {  // substitute and break strings apart at spaces, then pass argv to TV app
                  uint argc, idx;

                  pUserCmd += USERCMD_PREFIX_WINTV_LEN;
                  // skip spaces between !wintv! keyword and command name
                  while (*pUserCmd == ' ')
                     pUserCmd += 1;

                  cmdbuf.quoteShell = FALSE;
                  PiOutput_CmdSubstitute(pUserCmd, &cmdbuf, TRUE, pPiBlock, pAiBlock);
                  ShellCmd_AppendChar('\0', &cmdbuf);

                  argc = 0;
                  for (idx=0; idx < cmdbuf.off; idx++)
                     if (cmdbuf.strbuf[idx] == '\0')
                        argc += 1;

                  if (WintvSharedMem_SetEpgCommand(argc, cmdbuf.strbuf, cmdbuf.off) == FALSE)
                  {
                     bool isConnected;

                     isConnected = WintvSharedMem_IsConnected(NULL, 0, NULL);
                     if (isConnected == FALSE)
                     {
                        sprintf(comm, "tk_messageBox -type ok -icon error -parent . "
                                      "-message \"Cannot record: no TV application connected!\"");
                     }
                     else
                     {
                        sprintf(comm, "tk_messageBox -type ok -icon error -parent . "
                                      "-message \"Failed to send the record command to the TV app.\"");
                     }
                     eval_check(interp, comm);
                     Tcl_ResetResult(interp);
                  }
               }
               else
               #endif
               {  // substitute and quote variables in UNIX style, then execute the command
                  cmdbuf.quoteShell = TRUE;
                  PiOutput_CmdSubstitute(pUserCmd, &cmdbuf, FALSE, pPiBlock, pAiBlock);

                  // append a '&' to have the shell execute the command asynchronously
                  // in the background; else we would hang until the command finishes.
                  // XXX TODO must be adapted for WIN API
                  #ifndef WIN32
                  ShellCmd_AppendChar('&', &cmdbuf);
                  #endif

                  // finally null-terminate the string
                  ShellCmd_AppendChar('\0', &cmdbuf);

                  // execute the command
                  system(cmdbuf.strbuf);
               }
               Tcl_DStringFree(&ds);
            }
            else
               debug1("PiOutput-ExecUserCmd: user cmd #%d not found", userCmdIndex);
         }
      }
      EpgDbLockDatabase(pUiDbContext, FALSE);
      xfree(cmdbuf.strbuf);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Add user-defined commands to context menu
//
void ShellCmd_CtxMenuAddUserDef( const char * pMenu, bool addSeparator )
{
   Tcl_Obj  * pCtxVarObj;
   Tcl_Obj ** pCtxList;
   int  userCmdCount;
   sint idx;
   #ifdef WIN32
   bool isConnected;

   isConnected = WintvSharedMem_IsConnected(NULL, 0, NULL);
   #endif

   pCtxVarObj = Tcl_GetVar2Ex(interp, "ctxmencf", NULL, TCL_GLOBAL_ONLY);
   if ( (pCtxVarObj != NULL) &&
        (Tcl_ListObjGetElements(interp, pCtxVarObj, &userCmdCount, &pCtxList) == TCL_OK) &&
        (userCmdCount > 0) )
   {
      if (addSeparator)
      {
         sprintf(comm, "%s add separator\n", pMenu);
         eval_check(interp, comm);
      }

      for (idx=0; idx + 1 < userCmdCount; idx += 2)
      {
         sprintf(comm, "%s add command -label {%s} -command {C_ExecUserCmd %d}",
                       pMenu, Tcl_GetString(pCtxList[idx]), idx / 2);

         #ifdef WIN32
         if ( (strncmp(USERCMD_PREFIX_WINTV, Tcl_GetString(pCtxList[idx + 1]), USERCMD_PREFIX_WINTV_LEN) == 0) &&
              (isConnected == FALSE) )
         {
            strcat(comm, " -state disabled");
         }
         #endif
         eval_check(interp, comm);
      }
   }
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
   Tcl_CreateObjCommand(interp, "C_ExecUserCmd", PiOutput_ExecUserCmd, (ClientData) NULL, NULL);
}

