/*
 *  Communication with external TV application via shared memory
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
 *    This module implements methods to allow communication with
 *    external applications (mainly TV viewing apps) under M$ Windows.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: winshmsrv.c,v 1.6 2002/05/10 14:59:42 tom Exp tom $
 */

#ifndef WIN32
#error "This module is intended only for Win32 systems"
#else

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <malloc.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/vbidecode.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"


#define EPG_SHM_FILE_NAME  "vbi_map.dat"

static const WINSHMSRV_CB * pWinShmSrvCb = NULL;
static volatile TVAPP_COMM * pTvShm = NULL;
static HANDLE map_fd = NULL;
static HANDLE shm_fd = 0;
static HANDLE epgEventHandle = NULL;
static HANDLE tvEventHandle = NULL;
static HANDLE epgMutexHandle = NULL;
static HANDLE shmMutexHandle = NULL;
static HANDLE msgThreadHandle = NULL;
static bool   stopMsgThread;
static uchar * pLastErrorText = NULL;


// ---------------------------------------------------------------------------
// Struct that holds state of SHM when last processed
// - TV app's settings are stored to detect which ones have changed
//   since the last TV app trigger
//
static struct
{
   uint8_t   tvAppAlive;
   uint8_t   tvReqTvCard;
   uint8_t   tvCardIdx;
   uint8_t   tvGrantTuner;
   //uint32_t  tvCurFreq;
   uint32_t  tvChanNameIdx;
} shmTvCache;

// ----------------------------------------------------------------------------
// Save text describing error cause
// - argument list has to be terminated with NULL pointer
// - to be displayed by the GUI to help the user fixing the problem
//
static void WinSharedMem_SetErrorText( DWORD errCode, const char * pText, ... )
{
   va_list argl;
   const char *argv[20];
   uint argc, sumlen, off, idx;

   // free the previous error text
   if (pLastErrorText != NULL)
   {
      debug0("WinSharedMem-SetErrorText: Warning: previous error text unprocessed - discarding");
      xfree(pLastErrorText);
      pLastErrorText = NULL;
   }

   // collect all given strings
   if (pText != NULL)
   {
      argc    = 1;
      argv[0] = pText;
      sumlen  = strlen(pText);

      va_start(argl, pText);
      while (argc < 20 - 1)
      {
         argv[argc] = va_arg(argl, char *);
         if (argv[argc] == NULL)
            break;

         sumlen += strlen(argv[argc]);
         argc += 1;
      }
      va_end(argl);

      // reserve additional space for system error code
      if (errCode != 0)
         sumlen += 100;

      // allocate memory for sum of all strings length
      pLastErrorText = xmalloc(sumlen + 1);

      // concatenate the strings
      off = 0;
      for (idx=0; idx < argc; idx++)
      {
         strcpy(pLastErrorText + off, argv[idx]);
         off += strlen(argv[idx]);
      }

      if (errCode != 0)
      {  // append system error message
         strcpy(pLastErrorText + off, ": ");
         off += 2;
         FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errCode, LANG_SYSTEM_DEFAULT,
                       pLastErrorText + off, 100 - 1, NULL);
         off += strlen(pLastErrorText + off);
         if ((pLastErrorText[off - 2] == '\r') && (pLastErrorText[off - 1] == '\n'))
         {  // remove CR/NL characters from the end of the string
            pLastErrorText[off - 2] = 0;
         }
      }

      debug1("%s", pLastErrorText);
   }
}

// ---------------------------------------------------------------------------
//                        G U I   I n t e r f a c e
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Retrieve and clear the last error message
// - this function should always be called when an error is indicated
// - the caller must free the allocated memory!
//
const uchar * WinSharedMem_GetErrorMsg( void )
{
   const uchar * pErrMsg = pLastErrorText;

   // clear the error message
   if (pLastErrorText != NULL)
   {
      pLastErrorText = NULL;
   }
   else
      debug0("WinSharedMem-GetErrorMsg: warning: no error message available");

   // memory must be freed by the caller!
   return pErrMsg;
}

// ---------------------------------------------------------------------------
// Get VPS/PDC from VBI buffer of connected TV app
//
bool WintvSharedMem_GetCniAndPil( uint * pCni, uint * pPil )
{
   bool result = FALSE;

   if ( (pTvShm != NULL) && (pTvShm->tvAppAlive) )
   {
      result = TtxDecode_GetCniAndPil(pCni, pPil, &pTvShm->vbiBuf);
   }
   return result;
}

// ---------------------------------------------------------------------------
// Return TRUE if a TV app currently is connected
//
bool WintvSharedMem_IsConnected( char * pAppName, uint maxNameLen, uint * pFeatures )
{
   bool result = FALSE;

   if ( (pTvShm != NULL) && (pTvShm->tvAppAlive) )
   {
      // check if name is requested
      if ( ((pAppName != NULL) && (maxNameLen > 0)) ||
           (pFeatures != NULL) )
      {
         // wait for the semaphore and request "ownership"
         if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
         {
            // must read shared memory again after locking the mutex
            if (pTvShm->tvAppAlive)
            {
               if ((pAppName != NULL) && (maxNameLen > 0))
               {
                  if (maxNameLen > TVAPP_NAME_MAX_LEN)
                     maxNameLen = TVAPP_NAME_MAX_LEN;

                  // copy the TV app name from shared memory into the supplied array
                  strncpy(pAppName, (char *) pTvShm->tvAppName, maxNameLen);
                  pAppName[maxNameLen - 1] = 0;
               }

               if (pFeatures != NULL)
               {  // copy TV feature support bitfield
                  *pFeatures = pTvShm->tvFeatures;
               }

               result = TRUE;
            }

            // release the semaphore
            if (ReleaseMutex(shmMutexHandle) == 0)
               debug1("WintvSharedMem-IsConnected: get TV app name: ReleaseMutex: %ld", GetLastError());
         }
         else
            debug1("WintvSharedMem-IsConnected: get TV app name: WaitForSingleObject: %ld", GetLastError());
      }
      else
      {  // no name or features requested -> no need to lock shm
         result = TRUE;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Send argument vector to TV app, i.e. list of strings
// - the command is a list of strings separated by 0; terminated by two 0
//
bool WintvSharedMem_SetEpgCommand( uint argc, const char * pArgStr, uint cmdlen )
{
   bool result = FALSE;

   if (pTvShm != NULL)
   {
      if (cmdlen < EPG_CMD_MAX_LEN)
      {
         // wait for the semaphore and request "ownership"
         if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
         {
            // copy the command into SHM - must not use strcpy because of zeros inside
            memcpy((char *) pTvShm->epgCommand, pArgStr, cmdlen);
            pTvShm->epgCmdArgc = argc;

            pTvShm->epgCommandIdx += 1;

            result = TRUE;

            // release the semaphore
            if (ReleaseMutex(shmMutexHandle) == 0)
               debug1("WintvSharedMem-SetEpgCmd: ReleaseMutex: %ld", GetLastError());

            // wake up the receiver
            SetEvent(tvEventHandle);
         }
         else
            debug1("WintvSharedMem-SetEpgCmd: WaitForSingleObject: %ld", GetLastError());
      }
      else
         debug2("WintvSharedMem-SetEpgCmd: command too long: %d (max %d)", cmdlen, EPG_CMD_MAX_LEN);
   }
   return result;
}

// ---------------------------------------------------------------------------
// Update EPG info
// - chanIdx must be the same that was returned during the channel name query
//   if it doesn't match the current channel index the info is discarded
//   (this happens if the channel is changed while the query is processed)
//
bool WintvSharedMem_SetEpgInfo( time_t start_time, time_t stop_time, const char * pTitle,
                                uchar themeCount, const uchar * pThemes, uint chanIdx )
{
   bool result = FALSE;

   if (pTvShm != NULL)
   {
      // wait for the semaphore and request "ownership"
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         if (chanIdx == pTvShm->tvChanNameIdx)
         {
            strncpy((char *) pTvShm->epgProgTitle, pTitle, EPG_TITLE_MAX_LEN);
            pTvShm->epgProgTitle[EPG_TITLE_MAX_LEN - 1] = 0;
            pTvShm->epgStartTime     = start_time;
            pTvShm->epgStopTime      = stop_time;
            if (pThemes != NULL)
            {
               memcpy((char *) pTvShm->epgPdcThemes, pThemes, 7);
               pTvShm->epgPdcThemeCount = themeCount;
            }
            else
               pTvShm->epgPdcThemeCount = 0;

            pTvShm->epgProgInfoIdx += 1;

            result = TRUE;
         }

         // release the semaphore
         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WintvSharedMem-SetEpgInfo: ReleaseMutex: %ld", GetLastError());

         // wake up the receiver
         SetEvent(tvEventHandle);
      }
      else
         debug1("WintvSharedMem-SetEpgInfo: WaitForSingleObject: %ld", GetLastError());
   }
   return result;
}

// ---------------------------------------------------------------------------
// Fetch channel name from shared memory
//
bool WintvSharedMem_QueryChanName( char * pTitle, uint maxLen, uint * pChanIdx )
{
   bool result = FALSE;
   char *ps, *pe;

   if (pTvShm != NULL)
   {
      // wait for the semaphore and request "ownership"
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         if (maxLen > CHAN_NAME_MAX_LEN)
           maxLen = CHAN_NAME_MAX_LEN;

         if (pTitle != NULL)
         {
            // skip any spaces at the start of the name
            ps = (char *) pTvShm->tvChanName;
            while ((*ps == ' ') || (*ps == '\t') )
               ps += 1;

            // chop any spaces at the end of the name
            if (*ps != 0)
            {
               pe = ps + strlen(ps) - 1;
               while ( (pe > ps) && ((*pe == ' ') || (*pe == '\t')) )
               {
                  pe--;
               }
            }
            else
               pe = ps;

            if (maxLen >= (pe - ps + 1 + 1))
            {
               strncpy((char *) pTitle, ps, pe - ps + 1);
               pTitle[pe - ps + 1] = 0;
            }
            else if (maxLen > 0)
            {
               strncpy((char *) pTitle, ps, maxLen);
               pTitle[maxLen - 1] = 0;
            }
         }

         // CNI in SHM is currently not used since no TV app supports it
         //*pCni = pTvShm->tvChanCni;

         if (pChanIdx != NULL)
            *pChanIdx = pTvShm->tvChanNameIdx;

         result = TRUE;

         // release the semaphore
         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WintvSharedMem-QueryChanName: ReleaseMutex: %ld", GetLastError());

         dprintf1("WintvSharedMem-QueryChanName: channel \"%s\"\n", pTitle);
      }
      else
         debug1("WintvSharedMem-QueryChanName: WaitForSingleObject: %ld", GetLastError());
   }
   return result;
}

// ---------------------------------------------------------------------------
//                      E v e n t   P r o c e s s i n g
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Attach to TV application
//
static void WinSharedMem_AttachTvapp( void )
{
   bool tvAppStarted;
   bool restarted = FALSE;
   bool acqEnabled, epgHasDriver;
   uint epgTvCardIdx;

   dprintf1("TV app started - req. card %x\n", pTvShm->tvCardIdx);

   BtDriver_GetState(&acqEnabled, &epgHasDriver, &epgTvCardIdx);

   if (acqEnabled && epgHasDriver)
   {
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         if ( (pTvShm->tvAppAlive) &&     // note: mutex was not locked during first check
              (pTvShm->tvReqTvCard) &&
              ( (pTvShm->tvCardIdx == epgTvCardIdx) ||
                (pTvShm->tvCardIdx == TVAPP_CARD_REQ_ALL) ))
         {  // driver attached, but TV app needs the same TV card -> detach

            acqEnabled = BtDriver_Restart();
            restarted = TRUE;
         }

         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WintvSharedMem-HandleTvCmd: ReleaseMutex: %ld", GetLastError());
      }
      else
         debug1("WintvSharedMem-HandleTvCmd: WaitForSingleObject: %ld", GetLastError());
   }

   if (restarted)
   {  // notify TV app that we've freed the driver
      SetEvent(tvEventHandle);
   }
   tvAppStarted = (shmTvCache.tvAppAlive == FALSE);

   shmTvCache.tvReqTvCard = pTvShm->tvReqTvCard;
   shmTvCache.tvCardIdx   = pTvShm->tvCardIdx;
   shmTvCache.tvAppAlive  = TRUE;

   if (tvAppStarted)
   {  // notify GUI that TV app status changed
      if (pWinShmSrvCb->pCbAttachTv != NULL)
         pWinShmSrvCb->pCbAttachTv(shmTvCache.tvAppAlive, acqEnabled);
   }
   else
   {  // notify GUI & acq control that acq mode changed
      if (pWinShmSrvCb->pCbTunerGrant != NULL)
         pWinShmSrvCb->pCbTunerGrant(shmTvCache.tvGrantTuner);
   }
}

// ---------------------------------------------------------------------------
// Detach from TV application
//
static void WinSharedMem_DetachTvapp( void )
{
   bool acqEnabled, epgHasDriver;

   BtDriver_GetState(&acqEnabled, &epgHasDriver, NULL);

   if (acqEnabled)
   {
      if (epgHasDriver == FALSE)
      {  // still in slave mode -> switch
         dprintf0("TV app terminated - quitting acq slave mode\n");
         if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
         {
            // stop slave-mode and load the actual Bt8x8 HW driver
            acqEnabled = BtDriver_Restart();

            if (ReleaseMutex(shmMutexHandle) == 0)
               debug1("WintvSharedMem-HandleTvCmd: ReleaseMutex: %ld", GetLastError());
         }
         else
            debug1("WintvSharedMem-HandleTvCmd: WaitForSingleObject: %ld", GetLastError());
      }
   }
   else
   {  // acq currently not enabled, so there's nothing to do
      dprintf0("TV app terminated (acq not enabled, no action req.)\n");
   }
   shmTvCache.tvAppAlive = FALSE;

   if (pWinShmSrvCb->pCbAttachTv != NULL)
      pWinShmSrvCb->pCbAttachTv(FALSE, acqEnabled);
}

// ---------------------------------------------------------------------------
// Check for TV app state changes in shared memory
// - invoked by the main thread after it was triggered by the message receptor
//   thread below
//
void WintvSharedMem_HandleTvCmd( void )
{
   dprintf0("WintvSharedMem-HandleTvCmd\n");

   if (pTvShm != NULL)
   {
      if (pTvShm->tvAppAlive)
      {
         // check if TV app was newly attached or req. a new TV card
         if ( (pTvShm->tvAppAlive  != shmTvCache.tvAppAlive) ||
              (pTvShm->tvReqTvCard != shmTvCache.tvReqTvCard) ||
              (pTvShm->tvCardIdx   != shmTvCache.tvCardIdx) )
         {
            WinSharedMem_AttachTvapp();
         }

         // check for channel change by TV app
         if (shmTvCache.tvChanNameIdx != pTvShm->tvChanNameIdx)
         {
            shmTvCache.tvChanNameIdx = pTvShm->tvChanNameIdx;
            // notify the GUI
            if (pWinShmSrvCb->pCbStationSelected != NULL)
               pWinShmSrvCb->pCbStationSelected();

            // reset VPS/PDC decoder
            TtxDecode_NotifyChannelChange(&pTvShm->vbiBuf);
         }

         // check if tuner was granted or reposessed
         if (shmTvCache.tvGrantTuner != pTvShm->tvGrantTuner)
         {
            shmTvCache.tvGrantTuner = pTvShm->tvGrantTuner;
            dprintf1("Tuner grant flag changed to %d\n", shmTvCache.tvGrantTuner);
            // notify the GUI
            if (pWinShmSrvCb->pCbTunerGrant != NULL)
               pWinShmSrvCb->pCbTunerGrant(shmTvCache.tvGrantTuner);
         }
      }
      else
      {
         // if TV app was alive before and is daed now -> detach
         if (shmTvCache.tvAppAlive)
         {
            WinSharedMem_DetachTvapp();
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Thread which waits for incoming messages sent by a connected TV app
// - to avoid neccessity of mutual exclusion no processing is done by
//   this thread; instead the main thread is triggered to check SHM
//   and handle incoming commands
//
static DWORD WINAPI WintvSharedMem_WaitCommEventThread( LPVOID dummy )
{
   for (;;)
   {
      if (WaitForSingleObject(epgEventHandle, INFINITE) == WAIT_FAILED)
      {
         debug1("WintvSharedMem-WaitCommEventThread: WaitForSingleObject: %ld", GetLastError());
         break;
      }

      if ((pTvShm != NULL) && (stopMsgThread == FALSE))
      {
         // wake up the main thread
         pWinShmSrvCb->pCbTvEvent();
      }
      else
      {  // nxtvepg is shutting down -> terminate the thread
         break;
      }
   }
   return 0;
}

// ---------------------------------------------------------------------------
//              I n t e r f a c e   t o   B t 8 x 8   d r i v e r
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Check if the configured card is busy by the TV app
// - if the card is free, it's registered in shared memory
// - called by the Bt8x8 driver when acq is started
//
bool WintvSharedMem_ReqTvCardIdx( uint cardIdx )
{
   bool result = FALSE;

   if (pTvShm != NULL)
   {
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         if ( (pTvShm->tvAppAlive  == FALSE) ||
              (pTvShm->tvReqTvCard == FALSE) ||
              ( (pTvShm->tvCardIdx != cardIdx) &&
                (pTvShm->tvCardIdx != TVAPP_CARD_REQ_ALL) ))
         {
            pTvShm->epgHasDriver = TRUE;
            pTvShm->epgTvCardIdx = cardIdx;
            result = TRUE;
         }

         // release the semaphore
         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WintvSharedMem-ReqTvCardIdx: ReleaseMutex: %ld", GetLastError());

         if (result)
         {  // status was changed -> wake up the receiver
            SetEvent(tvEventHandle);
         }
      }
      else
         debug1("WintvSharedMem-SetEpgCmd: WaitForSingleObject: %ld", GetLastError());
   }
   else
   {  // not connected to shared memory
      result = TRUE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Release the TV card
// - called by the Bt8x8 driver when acq is stopped during slave mode
// 
void WintvSharedMem_FreeTvCard( void )
{
   if ( (pTvShm != NULL) && (pTvShm->epgHasDriver) )
   {
      pTvShm->epgHasDriver = FALSE;
      pTvShm->epgReqInput  = EPG_REQ_INPUT_NONE;
      pTvShm->epgReqFreq   = EPG_REQ_FREQ_NONE;

      SetEvent(tvEventHandle);
   }
}

// ---------------------------------------------------------------------------
// Forward a request for a video input source to the TV app
// - called by the driver when in slave mode
//
bool WintvSharedMem_SetInputSrc( uint inputIdx )
{
   assert(pTvShm->epgHasDriver == FALSE);  // only useful in slave mode

   if ( (pTvShm != NULL) && (pTvShm->epgReqInput != inputIdx) )
   {
      dprintf1("WintvSharedMem-SetInputSrc: request new input %d\n", inputIdx);
      pTvShm->epgReqInput = inputIdx;
      SetEvent(tvEventHandle);
   }

   return pTvShm->tvGrantTuner;
}

// ---------------------------------------------------------------------------
// Forward a request for a TV tuner frequency to the TV app
// - called by the driver when in slave mode
//
bool WintvSharedMem_SetTunerFreq( uint freq )
{
   assert(pTvShm->epgHasDriver == FALSE);  // only useful in slave mode

   if ( (pTvShm != NULL) && (pTvShm->epgReqFreq != freq) )
   {
      dprintf1("WintvSharedMem-SetTunerFreq: request new freq %d\n", freq);
      pTvShm->epgReqFreq = freq;
      SetEvent(tvEventHandle);
   }

   return pTvShm->tvGrantTuner;
}

// ---------------------------------------------------------------------------
// Query the input source from which VBI originates
//
uint WintvSharedMem_GetInputSource( void )
{
   if (pTvShm != NULL)
      return pTvShm->tvCurInput;
   else
      return EPG_REQ_INPUT_NONE;
}

// ---------------------------------------------------------------------------
// Query the TV tuner frequency from which VBI originates
//
uint WintvSharedMem_GetTunerFreq( void )
{
   if (pTvShm != NULL)
      return pTvShm->tvCurFreq;
   else
      return EPG_REQ_FREQ_NONE;
}

// ---------------------------------------------------------------------------
// Retrieve address of the VBI buffer in shared memory
//
volatile EPGACQ_BUF * WintvSharedMem_GetVbiBuf( void )
{
   if (pTvShm != NULL)
      return &pTvShm->vbiBuf;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
//                   S t a r t u p   a n d   S h u t d o w n
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Create and initialize shared memory
//
static bool WintvSharedMem_Enable( void )
{
   DWORD msgThreadId;
   uint cardIdx;
   bool acqEnabled, epgHasDriver;
   bool shmMutexOwned = FALSE;
   bool tvAppAlive = FALSE;
   bool result = FALSE;

   // create an owned mutex: make sure there's only one EPG server active at the same time
   epgMutexHandle = CreateMutex(NULL, TRUE, EPG_MUTEX_NAME);
   if (epgMutexHandle != NULL)
   {
      // if the mutex already existed before we attempted to create/open it -> bow out
      if (GetLastError() != ERROR_ALREADY_EXISTS)
      {
         shmMutexHandle = CreateMutex(NULL, FALSE, SHM_MUTEX_NAME);
         if (shmMutexHandle != NULL)
         {
            if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
            {
               shmMutexOwned = TRUE;
               tvEventHandle = CreateEvent(NULL, FALSE, FALSE, TV_SHM_EVENT_NAME);
               if (tvEventHandle != NULL)
               {
                  epgEventHandle = CreateEvent(NULL, FALSE, FALSE, EPG_SHM_EVENT_NAME);
                  if (epgEventHandle != NULL)
                  {
                     // check if the TV is already running
                     tvAppAlive = (GetLastError() == ERROR_ALREADY_EXISTS);

                     ResetEvent(epgEventHandle);

                     shm_fd = CreateFile(EPG_SHM_FILE_NAME, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                                         FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE, NULL);
                     if (shm_fd != NULL)
                     {
                        map_fd = CreateFileMapping(shm_fd, NULL, PAGE_READWRITE, 0, sizeof(TVAPP_COMM), EPG_SHM_NAME);
                        if (map_fd != NULL)
                        {
                           pTvShm = MapViewOfFileEx(map_fd, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(TVAPP_COMM), NULL);
                           if (pTvShm != NULL)
                           {
                              memset((char *)pTvShm, 0, sizeof(TVAPP_COMM));
                              memset(&shmTvCache, 0, sizeof(shmTvCache));
                              pTvShm->epgShmSize     = sizeof(TVAPP_COMM);
                              pTvShm->epgShmVersion  = EPG_SHM_VERSION;
                              pTvShm->epgReqInput    = EPG_REQ_INPUT_NONE;
                              pTvShm->epgReqFreq     = EPG_REQ_FREQ_NONE;
                              pTvShm->epgAppAlive    = TRUE;

                              BtDriver_GetState(&acqEnabled, &epgHasDriver, &cardIdx);
                              pTvShm->epgHasDriver   = acqEnabled && epgHasDriver;
                              pTvShm->epgTvCardIdx   = cardIdx;

                              if (SetEvent(tvEventHandle) != 0)
                              {
                                 stopMsgThread = FALSE;
                                 msgThreadHandle = CreateThread(NULL, 0, WintvSharedMem_WaitCommEventThread, NULL, 0, &msgThreadId);
                                 if (msgThreadHandle != NULL)
                                 {
                                    shmMutexOwned = FALSE;
                                    if (ReleaseMutex(shmMutexHandle) == 0)
                                       debug1("WintvSharedMem-Init: ReleaseMutex: %ld", GetLastError());

                                    if (tvAppAlive)
                                    {
                                       dprintf0("WintvSharedMem-Init: TV app already running: wait for SHM init...\n");
                                       // give the TV app a chance to write it's status into shared memory
                                       if (WaitForSingleObject(epgEventHandle, 2000) == WAIT_FAILED)
                                          debug1("WintvSharedMem-Init: WaitForSingleObject " TV_SHM_EVENT_NAME ": %ld", GetLastError());

                                       if (pTvShm->tvAppAlive)
                                       {
                                          dprintf0("WintvSharedMem-Init: TV app initialized SHM - entering slave mode\n");
                                          WinSharedMem_AttachTvapp();
                                       }
                                    }
                                    // initialization completed successfully
                                    // (the attach to a TV app may have failed, but this is reported separately)
                                    result = TRUE;
                                 }
                                 else
                                    WinSharedMem_SetErrorText(GetLastError(), "TV interaction setup failed: can't create message receptor thread", NULL);
                              }
                              else
                                 WinSharedMem_SetErrorText(GetLastError(), "TV interaction setup failed: can't trigger TV event", NULL);

                              if (result == FALSE)
                              {
                                 pTvShm->epgAppAlive = FALSE;
                                 SetEvent(tvEventHandle);

                                 UnmapViewOfFile((void *)pTvShm);
                                 pTvShm = NULL;
                              }
                           }
                           else
                              WinSharedMem_SetErrorText(GetLastError(), "TV interaction setup failed: can't map shared memory", NULL);

                           if (result == FALSE)
                           {
                              CloseHandle(map_fd);
                              map_fd = NULL;
                           }
                        }
                        else
                           WinSharedMem_SetErrorText(GetLastError(), "TV interaction setup failed: can't create shared memory handle", NULL);

                        if (result == FALSE)
                        {
                           CloseHandle(shm_fd);
                           shm_fd = NULL;
                        }
                     }
                     else
                        WinSharedMem_SetErrorText(GetLastError(), "TV interaction setup failed: can't create file '" EPG_SHM_FILE_NAME "' for shared memory", NULL);

                     if (result == FALSE)
                     {
                        CloseHandle(epgEventHandle);
                        epgEventHandle = NULL;
                     }
                  }
                  else
                     WinSharedMem_SetErrorText(GetLastError(), "TV interaction setup failed: can't create EPG event handle", NULL);

                  if (result == FALSE)
                  {
                     CloseHandle(tvEventHandle);
                     tvEventHandle = NULL;
                  }
               }
               else
                  WinSharedMem_SetErrorText(GetLastError(), "TV interaction setup failed: can't create TV event handle", NULL);

               if (shmMutexOwned)
               {
                  if (ReleaseMutex(shmMutexHandle) == 0)
                     debug1("WintvSharedMem-Init: ReleaseMutex SHM: %ld", GetLastError());
               }
            }
            else
               WinSharedMem_SetErrorText(GetLastError(), "TV interaction setup failed: can't lock shared memory mutex", NULL);

            if (result == FALSE)
            {
               CloseHandle(shmMutexHandle);
               shmMutexHandle = NULL;
            }
         }
         else
            WinSharedMem_SetErrorText(GetLastError(), "TV interaction setup failed: can't create shared memory mutex", NULL);

         // the EPG mutex can be always be released - its existance is used to check if the EPG app is alive
         if (ReleaseMutex(epgMutexHandle) == 0)
            debug1("WintvSharedMem-Init: ReleaseMutex EPG: %ld", GetLastError());
      }
      else
         WinSharedMem_SetErrorText(0, "TV interaction setup failed: another EPG application is already running", NULL);

      if (result == FALSE)
      {
         CloseHandle(epgMutexHandle);
         epgMutexHandle = NULL;
      }
   }
   else
      WinSharedMem_SetErrorText(GetLastError(), "TV interaction init failed: can't create shared EPG application mutex", NULL);

   return result;
}

// ---------------------------------------------------------------------------
// Close shared memory
//
static void WintvSharedMem_Disable( void )
{
   // lock shared memory to avoid race with TV app
   if (shmMutexHandle != NULL)
      WaitForSingleObject(shmMutexHandle, 5000);

   // notify the TV app that we're going down
   if ((pTvShm != NULL) && (tvEventHandle != NULL))
   {
      pTvShm->epgAppAlive = FALSE;
      SetEvent(tvEventHandle);
   }

   // stop the message receptor thread (after setting epgAppAlive FALSE)
   if (msgThreadHandle != NULL)
   {
      stopMsgThread = TRUE;
      // wake up the thread
      SetEvent(epgEventHandle);
      // wait until the thread has terminated
      WaitForSingleObject(msgThreadHandle, INFINITE);
      CloseHandle(msgThreadHandle);
      msgThreadHandle = NULL;
   }

   if ((epgEventHandle != NULL) && (CloseHandle(epgEventHandle) == 0))
      debug1("WintvSharedMem-Exit: CloseHandle epgEventHandle: %ld", GetLastError());
   if ((tvEventHandle != NULL) && (CloseHandle(tvEventHandle) == 0))
      debug1("WintvSharedMem-Exit: CloseHandle tvEventHandle: %ld", GetLastError());
   epgEventHandle = NULL;
   tvEventHandle  = NULL;

   if ((map_fd != NULL) && (CloseHandle(map_fd) == FALSE))
      debug1("WintvSharedMem-Exit: CloseHandle map_fd: %ld", GetLastError());
   if ((shm_fd != NULL) && (CloseHandle(shm_fd) == FALSE))
      debug1("WintvSharedMem-Exit: CloseHandle shm_fd: %ld", GetLastError());
   map_fd = NULL;
   shm_fd = NULL;

   if ((pTvShm != NULL) && (UnmapViewOfFile((void *)pTvShm) == 0))
      debug1("WintvSharedMem-Exit: UnmapViewOfFile: %ld", GetLastError());
   pTvShm = NULL;

   if (shmMutexHandle != NULL)
   {
      if (ReleaseMutex(shmMutexHandle) == 0)
         debug1("WintvSharedMem-Exit: ReleaseMutex: %ld", GetLastError());

      if (CloseHandle(shmMutexHandle) == 0)
         debug1("WintvSharedMem-Exit: CloseHandle shmMutexHandle: %ld", GetLastError());
      shmMutexHandle = NULL;
   }

   if (epgMutexHandle != NULL)
   {
      if (CloseHandle(epgMutexHandle) == 0)
         debug1("WintvSharedMem-Exit: CloseHandle epgMutexHandle: %ld", GetLastError());
      epgMutexHandle = NULL;
   }
}

// ---------------------------------------------------------------------------
// En- or disable the service
// - returns TRUE if the operation was performed successfully
//   or if the module already was in the requested state
//
bool WintvSharedMem_StartStop( bool start, bool * pAcqEnabled )
{
   bool acqEnabled, epgHasDriver;
   bool result;

   BtDriver_GetState(&acqEnabled, &epgHasDriver, NULL);

   if ( (start) && (pTvShm == NULL) )
   {
      if (pWinShmSrvCb != NULL)
      {
         dprintf0("WintvSharedMem-StartStop: starting service\n");
         // note: Bt8x8 driver restart (e.g. change to slave mode) is automatically done during
         // attach if neccessary, so we don't have to care about it here

         result = WintvSharedMem_Enable();

         BtDriver_GetState(&acqEnabled, NULL, NULL);
      }
      else
      {
         fatal0("WintvSharedMem-StartStop: callbacks must be set first");
         result = FALSE;
      }
   }
   else if ( (start == FALSE) && (pTvShm != NULL) )
   {
      dprintf0("WintvSharedMem-StartStop: stopping service\n");
      // when acq is running in slave mode, it must be stopped
      // (note: cannot do a restart before of after - the shm must be disabled inbetween stop and start)
      if (acqEnabled && (epgHasDriver == FALSE))
         BtDriver_StopAcq();

      WintvSharedMem_Disable();

      if (acqEnabled && (epgHasDriver == FALSE))
      {
         // slave mode was terminated above - now start acq again with the actual Bt8x8 driver
         acqEnabled = BtDriver_StartAcq();

         // notify acq ctl that no TV app is connected anymore
         if (pWinShmSrvCb->pCbAttachTv != NULL)
            pWinShmSrvCb->pCbAttachTv(FALSE, acqEnabled);
      }

      result = TRUE;
   }
   else
      result = TRUE;

   if (pAcqEnabled != NULL)
      *pAcqEnabled = acqEnabled;

   return result;
}

// ---------------------------------------------------------------------------
// Initialize callbacks
// - this function must be called before the server is enabled
// - only a pointer is stored, so the caller must not free the struct
//   (or create it on the stack)
//
void WintvSharedMem_SetCallbacks( const WINSHMSRV_CB * pCb )
{
   pWinShmSrvCb = pCb;
}

// ---------------------------------------------------------------------------
// Clean up the driver module for exit
// - called once at program termination
//
void WintvSharedMem_Exit( void )
{
   // make sure the server is switched off
   WintvSharedMem_StartStop(FALSE, NULL);

   // free the error message text
   if (pLastErrorText != NULL)
   {
      xfree(pLastErrorText);
      pLastErrorText = NULL;
   }
}

// ---------------------------------------------------------------------------
// Initialize the driver module
// - called once at program start
//
bool WintvSharedMem_Init( void )
{
   pTvShm = NULL;
   memset(&shmTvCache, 0, sizeof(shmTvCache));
   return TRUE;
}

#endif  // WIN32
