/*
 *  TV app. interaction with nxtvepg via shared memory
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
 *    This module is the counterpart to winshmsrv.c in nxtvepg,
 *    i.e. it's used on side of the TV application to comunicate
 *    with nxtvepg.
 *
 *    In the init function the module creates event handles that
 *    are shared between the TV and EPG applications.  It also
 *    creates a thread that starts waiting for triggers on the
 *    dedicated TV event handle.  When the presence of an EPG
 *    application is detected - either by receiving a trigger or
 *    by existance of the event handles before they were created
 *    in the init function - the module attaches to shared memory
 *    (which exclusively is created by the EPG app) and initializes
 *    the TV params, e.g. the application name.
 *
 *    While the TV and EPG apps are connected they both wait for
 *    triggers on their respective event handles.  A trigger is
 *    sent whenever any value in shared memory was changed. When
 *    a trigger is received, the receiving app has to compare all
 *    values in shared memory to find out which have changed. See
 *    winshm.h for a list of parameters in shared memory. To
 *    simplify the comparison there are index values for complex
 *    elements, which are incremented each time the elements are
 *    changed.  Whenever non-atomic values are changed of referenced,
 *    the shared memory mutex must be locked (i.e. "owned").
 *
 *    When debugging is enabled, i.e. compiler switches DEBUG_SWITCH_TVSIM
 *    set to ON in mytypes.h and DPRINTF_OFF commented out below, you can 
 *    watch the interaction with DebugView from sysinternals.com
 *
 *  Author: Tom Zoerner
 *
 *  $Id: winshmclnt.c,v 1.3 2002/05/04 18:23:01 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_TVSIM
//#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <malloc.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/vbidecode.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "epgvbi/winshmsrv.h"

#include "tvsim/winshmclnt.h"


// forward function declarations
static bool WinSharedMemClient_AttachShm( void );
static void WinSharedMemClient_DetachShm( void );

// ---------------------------------------------------------------------------
// Declaration and initialization of module global variables
//
// pointer to shared memory
static TVAPP_COMM * pTvShm = NULL;

// handle to shared memory
static HANDLE map_fd = NULL;

// handle to EPG app event: used to trigger the EPG app. after updates in shared memory
static HANDLE epgEventHandle = NULL;

// handle to TV app event: used to wait for triggers from the EPG app.
static HANDLE tvEventHandle = NULL;

// handle to TV mutex: used to make sure only one EPG client is running at the same time
static HANDLE tvMutexHandle = NULL;

// handle to shared memory mutex: used to synchronize access to shared memory
static HANDLE shmMutexHandle = NULL;

// handle to message thread: the thread waits for event on out TV event handle
static HANDLE msgThreadHandle = NULL;

// flag to tell the message receptor thread to terminate
static BOOL StopMsgThread;

// application parameters, passed down during initialization
static const WINSHMCLNT_TVAPP_INFO * pTvAppInfo;

// ---------------------------------------------------------------------------
// Struct that holds state of SHM when last processed
// - used to detect state changes, i.e. when the event is signalled, the
//   current state inside shm is compared with this cache; state changes
//   are equivalent to messages (e.g. incoming EPG info when epgProgInfoIdx
//   is incremented)
// - also used to cache parameters that were handed down from GUI,
//   to save them while detached; they are copied into SHM upon attach
// - the variables have the same meaning as the equally named ones in
//   shared memory, so see there for comments
//
typedef struct
{
   uint32_t  epgProgInfoIdx;
   uint32_t  epgCommandIdx;
   bool      tvGrantTuner;
   uint32_t  tvCurFreq;
   uint32_t  tvCurInput;
   uint32_t  epgReqFreq;
   uint32_t  epgReqInput;
} EPG_SHM_STATE;

static EPG_SHM_STATE epgShmCache;

// ---------------------------------------------------------------------------
// GUI request: Grant the tuner to the EPG app
// - if doGrant is TRUE, the EPG application is allowed to change input source
//   and tuner frequency to that of an EPG content provider
// - should be called whenever the user suspends TV display, i.e. no video
//   input is required by the TV application itself; the card can then be
//   used by the EPG App to update all it's databases
//
bool WinSharedMemClient_GrantTuner( bool doGrant )
{
   if ( (pTvShm != NULL) &&
        (epgShmCache.tvGrantTuner != doGrant) )
   {
      // wait for the semaphore and request "ownership"
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         pTvShm->tvGrantTuner = doGrant;
         epgShmCache.tvGrantTuner   = doGrant;

         // release the semaphore
         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WinSharedMemClient-GrantTuner: ReleaseMutex: %ld", GetLastError());

         if (doGrant)
         {  // EPG already requested a freq -> set it right away
            dprintf2("WinSharedMemClient-GrantTuner: setting EPG freq %d, input %d\n", epgShmCache.epgReqFreq, epgShmCache.epgReqInput);
            if (pTvAppInfo->pCbReqTuner != NULL)
               pTvAppInfo->pCbReqTuner(epgShmCache.epgReqInput, epgShmCache.epgReqFreq);
         }
         else
            dprintf0("WinSharedMemClient-GrantTuner: cleared tuner grant flag\n");

         // wake up the receiver
         if (SetEvent(epgEventHandle) == 0)
            debug1("WinSharedMemClient-GrantTuner: SetEvent: %ld", GetLastError());
      }
      else
         debug1("WinSharedMemClient-GrantTuner: WaitForSingleObject: %ld", GetLastError());
   }
   else
   {  // currently not connected -> save the state in the cache
      // the value will be copied into shared memory upon an attach
      epgShmCache.tvGrantTuner = doGrant;
   }
   return TRUE;
}

// ---------------------------------------------------------------------------
// GUI request: Notify the EPG peer about a TV channel change
// - the EPG app will reply with EPG info for the named station a.s.a.p
//
bool WinSharedMemClient_SetStation( const char * pChanName, uint cni, uint inputSrc, uint freq )
{
   if (pTvShm != NULL)
   {
      // wait for the semaphore and request "ownership"
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         // copy the values in the cache
         epgShmCache.tvCurFreq  = freq;
         epgShmCache.tvCurInput = inputSrc;

         pTvShm->tvCurFreq  = freq;
         pTvShm->tvCurInput = inputSrc;

         strncpy(pTvShm->tvChanName, pChanName, CHAN_NAME_MAX_LEN);
         pTvShm->tvChanName[CHAN_NAME_MAX_LEN - 1] = 0;

         // Note: if a non-zero CNI value is given, can be used by the EPG app
         // to identify the new channel instead of the channel name. This has
         // the advantage that the user does not have to manually match all
         // channel names.
         // The CNI can be obtained from VPS, PDC or teletext packet 8/30/1
         // during the initial channel scan; do NOT copy the currently received
         // CNI here; this is already passed to the EPG app and managed separately.
         // (note: if you obtain a CNI from P8/30/1 you must convert it to
         // equivalent VPS/PDC codes if available; see CniConvertP8301ToVps()
         // in epgvbi/cni_tables.c)
         pTvShm->tvChanCni = 0;
         // enable this only if you have CNIs in your TV app (the TV simulator does not)
         //pTvShm->tvChanCni = cni;

         pTvShm->tvChanNameIdx += 1;

         // release the semaphore
         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WinSharedMemClient-SetChannel: ReleaseMutex: %ld", GetLastError());

         // wake up the receiver
         if (SetEvent(epgEventHandle) == 0)
            debug1("WinSharedMemClient-SetChannel: SetEvent: %ld", GetLastError());
      }
      else
         debug1("WinSharedMemClient-SetChannel: WaitForSingleObject: %ld", GetLastError());
   }
   else
   {  // not attached -> save the values in the cache; required upon attach
      epgShmCache.tvCurFreq  = freq;
      epgShmCache.tvCurInput = inputSrc;
   }
   return TRUE;
}

// ---------------------------------------------------------------------------
// GUI request: Notify the EPG peer about a TV channel change
// - update current tuner freq and input source in shared memory; this function
//   does NOT request EPG info for that channel - if you need that, use the
//   function above
// - this function must be called whenever the tuner frequency is changed,
//   because the EPG application must known where it's receiving VBI data
//   from
//
bool WinSharedMemClient_SetInputFreq( uint inputSrc, uint freq )
{
   if (pTvShm != NULL)
   {
      // wait for the semaphore and request "ownership"
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         pTvShm->tvCurInput = inputSrc;
         pTvShm->tvCurFreq  = freq;

         // release the semaphore
         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WinSharedMemClient-SetInputFreq: ReleaseMutex: %ld", GetLastError());

         // wake up the receiver
         if (SetEvent(epgEventHandle) == 0)
            debug1("WinSharedMemClient-SetInputFreq: SetEvent: %ld", GetLastError());
      }
      else
         debug1("WinSharedMemClient-SetInputFreq: WaitForSingleObject: %ld", GetLastError());
   }
   return TRUE;
}

// ---------------------------------------------------------------------------
// Process changes in shared memory after the event was signalled
//
void WinSharedMemClient_HandleEpgEvent( void )
{
   dprintf0("WinSharedMemClient-HandleEpgCmd\n");

   if (pTvShm == NULL)
   {
      // received an event while shared memory not connected yet -> attempt to attach
      WinSharedMemClient_AttachShm();
      if (pTvShm != NULL)
      {  // attach successful, i.e. EPG application is up and running
         dprintf0("EPG app started - attached SHM\n");

         if (pTvAppInfo->pCbAttach != NULL)
            pTvAppInfo->pCbAttach(TRUE);
      }
      // else: failed to attach; this is ignored
   }

   if (pTvShm != NULL)
   {
      if (pTvShm->epgAppAlive == FALSE)
      {
         dprintf0("EPG app terminated - detaching SHM\n");
         // detach from shared memory and mutex; events remain
         WinSharedMemClient_DetachShm();

         if (pTvAppInfo->pCbAttach != NULL)
            pTvAppInfo->pCbAttach(FALSE);

         // from this point on pTvShm is NULL, so we must not check for more events
      }
      else
      {  // EPG app is running
         if (epgShmCache.epgProgInfoIdx != pTvShm->epgProgInfoIdx)
         {  // channel was updated
            if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
            {
               uint8_t   epgProgTitle[EPG_TITLE_MAX_LEN];
               uint32_t  epgStartTime;
               uint32_t  epgStopTime;
               uint8_t   epgPdcThemeCount;
               uint8_t   epgPdcThemes[7];

               // make a local copy of the received data
               // (the semaphore should NOT remain locked while GUI processes the data!)
               strncpy(epgProgTitle, pTvShm->epgProgTitle, EPG_TITLE_MAX_LEN);
               epgProgTitle[EPG_TITLE_MAX_LEN - 1] = 0;
               epgStartTime      = pTvShm->epgStartTime;
               epgStopTime       = pTvShm->epgStopTime;
               epgPdcThemeCount  = pTvShm->epgPdcThemeCount;;
               memcpy(epgPdcThemes, pTvShm->epgPdcThemes, 7);

               epgShmCache.epgProgInfoIdx = pTvShm->epgProgInfoIdx;

               if (ReleaseMutex(shmMutexHandle) == 0)
                  debug1("WinSharedMemClient-HandleEpgCmd: ReleaseMutex: %ld", GetLastError());

               dprintf1("EPG title received: \"%s\"\n", epgProgTitle);

               // GUI must check validity of received data (i.e. title empty or stop time smaller start time)
               if (pTvAppInfo->pCbUpdateProgInfo != NULL)
                  pTvAppInfo->pCbUpdateProgInfo(epgProgTitle, epgStartTime, epgStopTime, epgPdcThemeCount, epgPdcThemes);

            }
            else
               debug1("WinSharedMemClient-HandleEpgCmd: WaitForSingleObject: %ld", GetLastError());
         }

         if (epgShmCache.epgCommandIdx != pTvShm->epgCommandIdx)
         {
            if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
            {
               uint8_t   epgCommand[EPG_CMD_MAX_LEN];
               uint32_t  epgCmdArgc;

               memcpy(epgCommand, pTvShm->epgCommand, EPG_CMD_MAX_LEN);
               epgCmdArgc = pTvShm->epgCmdArgc;

               // signal EPG app that command is processed and we're ready for the next command
               pTvShm->tvCommandIdx = pTvShm->epgCommandIdx;

               epgShmCache.epgCommandIdx = pTvShm->epgCommandIdx;

               if (ReleaseMutex(shmMutexHandle) == 0)
                  debug1("WinSharedMemClient-HandleEpgCmd: ReleaseMutex: %ld", GetLastError());

               dprintf2("EPG command received (argc=%d): \"%s\"\n", epgCmdArgc, epgCommand);

               // GUI must parse the command vector (string list, separated by zeros)
               if (pTvAppInfo->pCbHandleEpgCmd != NULL)
                  pTvAppInfo->pCbHandleEpgCmd(epgCmdArgc, epgCommand);

               // notify EPG app that command is processed
               if (SetEvent(epgEventHandle) == 0)
                  debug1("WinSharedMemClient-HandleEpgCmd: SetEvent: %ld", GetLastError());
            }
            else
               debug1("WinSharedMemClient-HandleEpgCmd: WaitForSingleObject: %ld", GetLastError());
         }

         if (epgShmCache.epgReqInput != pTvShm->epgReqInput)
         {
            // no need to lock shm for a single 32-bit item
            epgShmCache.epgReqInput = pTvShm->epgReqInput;
            dprintf2("EPG requests input %d: granted=%d\n", epgShmCache.epgReqInput, epgShmCache.tvGrantTuner);

            if ( (epgShmCache.tvGrantTuner) && (pTvAppInfo->pCbReqTuner != NULL) )
               pTvAppInfo->pCbReqTuner(epgShmCache.epgReqInput, epgShmCache.epgReqFreq);
         }

         if (epgShmCache.epgReqFreq != pTvShm->epgReqFreq)
         {
            epgShmCache.epgReqFreq  = pTvShm->epgReqFreq;
            dprintf2("EPG requests freq %d: granted=%d\n", epgShmCache.epgReqFreq, epgShmCache.tvGrantTuner);

            if ( (epgShmCache.tvGrantTuner) && (pTvAppInfo->pCbReqTuner != NULL) )
               pTvAppInfo->pCbReqTuner(epgShmCache.epgReqInput, epgShmCache.epgReqFreq);
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Thread which waits for incoming messages sent by an EPG app
// - to avoid dealing with concurrency problems the thread only wakes up the
//   main thread and triggers it to call the above envent handler function
//
static DWORD WINAPI WinSharedMemClient_EventThread( LPVOID dummy )
{
   for (;;)
   {
      if (WaitForSingleObject(tvEventHandle, INFINITE) == WAIT_FAILED)
         break;

      if (StopMsgThread == FALSE)
      {
         // trigger the main thread to query shared memory status
         if (pTvAppInfo->pCbEpgEvent != NULL)
            pTvAppInfo->pCbEpgEvent();
      }
      else
         break;
   }
   return 0;
}

// ---------------------------------------------------------------------------
// Attach to shared memory
// - requires that the EPG application created the shared memory before;
//   if not, or any other error occurs, this function returns FALSE
//
static bool WinSharedMemClient_AttachShm( void )
{
   bool result = FALSE;

   shmMutexHandle = OpenMutex(MUTEX_ALL_ACCESS, FALSE, SHM_MUTEX_NAME);
   if (shmMutexHandle != NULL)
   {
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         map_fd = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, EPG_SHM_NAME);
         if (map_fd != NULL)
         {
            pTvShm = MapViewOfFileEx(map_fd, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(TVAPP_COMM), NULL);
            if (pTvShm != NULL)
            {
               if (pTvShm->epgAppAlive == TRUE)
               {
                  if (pTvShm->epgShmVersion <= EPG_SHM_VERSION)
                  {
                     // initialize TV app.'s params in shared memory
                     pTvShm->tvReqTvCard  = TRUE;
                     pTvShm->tvCardIdx    = TVAPP_CARD_REQ_ALL;
                     pTvShm->tvFeatures   = pTvAppInfo->tvFeatures;
                     strncpy(pTvShm->tvAppName, pTvAppInfo->pAppName, TVAPP_NAME_MAX_LEN);
                     pTvShm->tvGrantTuner = epgShmCache.tvGrantTuner;
                     pTvShm->tvCurFreq    = epgShmCache.tvCurFreq;
                     pTvShm->tvCurInput   = epgShmCache.tvCurInput;
                     pTvShm->tvAppAlive   = TRUE;

                     // initialize state cache with EPG app.'s params
                     epgShmCache.epgProgInfoIdx = pTvShm->epgProgInfoIdx;
                     epgShmCache.epgCommandIdx  = pTvShm->epgCommandIdx;
                     // do not copy the actual values here, to trigger an event in the handler
                     epgShmCache.epgReqFreq     = EPG_REQ_FREQ_NONE;
                     epgShmCache.epgReqInput    = EPG_REQ_INPUT_NONE;

                     if (SetEvent(epgEventHandle) != 0)
                     {
                        // set VBI buffer address (used by TTX/EPG decoder)
                        pVbiBuf = &pTvShm->vbiBuf;

                        result = TRUE;
                     }
                     else
                        debug1("WinSharedMemClient-AttachShm: SetEvent: %ld", GetLastError());
                  }
                  else
                     debug2("WinSharedMemClient-AttachShm: incompatible nxtvepg version 0x%x (expected <= 0x%x)", pTvShm->epgShmVersion, EPG_SHM_VERSION);
               }
               else
                  debug0("WinSharedMemClient-AttachShm: EPG app no longer alive");

               if (result == FALSE)
               {
                  UnmapViewOfFile(pTvShm);
                  pTvShm = NULL;
               }
            }
            else
               debug1("WinSharedMemClient-AttachShm: MapViewOfFileEx: %ld", GetLastError());

            if (result == FALSE)
            {
               CloseHandle(map_fd);
               map_fd = NULL;
            }
         }
         else
            debug1("WinSharedMemClient-AttachShm: OpenFileMapping: %ld", GetLastError());

         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WinSharedMemClient-AttachShm: ReleaseMutex: %ld", GetLastError());
      }
      else
         debug1("WintvSharedMemClient-AttachShm: WaitForSingleObject: %ld", GetLastError());

      if (result == FALSE)
      {
         CloseHandle(shmMutexHandle);
         shmMutexHandle = NULL;
      }
   }
   else
      debug1("WinSharedMemClient-AttachShm: OpenMutex " SHM_MUTEX_NAME ": %ld", GetLastError());

   return result;
}

// ---------------------------------------------------------------------------
// Detach from shared memory
//
static void WinSharedMemClient_DetachShm( void )
{
   // the VBI buffer is accessed by an async thread, so it must be invalidated before shm is freed
   pVbiBuf = NULL;

   if ((shmMutexHandle != NULL) && (CloseHandle(shmMutexHandle) == 0))
      debug1("WinSharedMemClient-DetachShm: CloseHandle shmMutexHandle: %ld", GetLastError());
   shmMutexHandle = NULL;

   if ((map_fd != NULL) && (CloseHandle(map_fd) == FALSE))
      debug1("WinSharedMemClient-DetachShm: CloseHandle map_fd: %ld", GetLastError());
   map_fd = NULL;

   if ((pTvShm != NULL) && (UnmapViewOfFile(pTvShm) == 0))
      debug1("WinSharedMemClient-DetachShm: UnmapViewOfFile: %ld", GetLastError());
   pTvShm = NULL;
}

// ---------------------------------------------------------------------------
// Initialize the module
// - has to be called exactly once during startup
//
bool WinSharedMemClient_Init( const WINSHMCLNT_TVAPP_INFO * pInitInfo )
{
   uint  idx;
   DWORD msgThreadId;
   bool  result = FALSE;

   if (pInitInfo != NULL)
   {
      // save TV application parameters
      pTvAppInfo = pInitInfo;

      // initialize the state cache for the TV app side
      memset(&epgShmCache, 0, sizeof(epgShmCache));
      epgShmCache.tvCurFreq  = EPG_REQ_FREQ_NONE;
      epgShmCache.tvCurInput = EPG_REQ_INPUT_NONE;

      tvMutexHandle = CreateMutex(NULL, TRUE, TV_MUTEX_NAME);
      if (tvMutexHandle != NULL)
      {
         if (GetLastError() != ERROR_ALREADY_EXISTS)
         {
            tvEventHandle = CreateEvent(NULL, FALSE, FALSE, TV_SHM_EVENT_NAME);
            if (tvEventHandle != NULL)
            {
               epgEventHandle = CreateEvent(NULL, FALSE, FALSE, EPG_SHM_EVENT_NAME);
               if (epgEventHandle != NULL)
               {
                  if (GetLastError() == ERROR_ALREADY_EXISTS)
                  {  // EPG app already running -> open event and signal the app

                     dprintf0("WinSharedMemClient-Init: EPG app already running, attaching SHM\n");
                     result = WinSharedMemClient_AttachShm();
                     if (result)
                     {
                        assert(pTvShm != NULL);  // pointer initialized by attach

                        // wait for the EPG app to free the driver
                        for (idx=0; (pTvShm->epgHasDriver) && (idx < 2); idx++)
                        {
                           if (SetEvent(epgEventHandle) == 0)
                              debug1("WinSharedMemClient-Init: SetEvent: %ld", GetLastError());

                           if (WaitForSingleObject(tvEventHandle, 1000) == WAIT_FAILED)
                              debug1("WinSharedMemClient-Init: WaitForSingleObject tvEventHandle: %ld", GetLastError());
                        }
                        dprintf1("WinSharedMemClient-Init: epgHasDriver=%d\n", pTvShm->epgHasDriver);
                     }
                     else
                     {  // EPG app not running
                        result = TRUE;
                     }
                  }
                  else
                  {  // event did not exist before (i.e. EPG app not running yet)
                     result = TRUE;
                  }

                  if (result)
                  {
                     StopMsgThread = FALSE;
                     msgThreadHandle = CreateThread(NULL, 0, WinSharedMemClient_EventThread, NULL, 0, &msgThreadId);
                     if (msgThreadHandle != NULL)
                     {
                        if ( (pTvShm != NULL) && (pTvAppInfo->pCbAttach != NULL) )
                           pTvAppInfo->pCbAttach(TRUE);
                     }
                     else
                     {
                        debug1("WinSharedMemClient-Init: CreateThread: %ld", GetLastError());
                        WinSharedMemClient_DetachShm();
                        result = FALSE;
                     }
                  }

                  if (result == FALSE)
                  {
                     if (CloseHandle(epgEventHandle) == 0)
                        debug1("WinSharedMemClient-Init: CloseHandle epgEventHandle: %ld", GetLastError());
                     epgEventHandle = NULL;
                  }
               }
               else
                  debug1("WinSharedMemClient-Init: CreateEvent " EPG_SHM_EVENT_NAME ": %ld", GetLastError());

               if (result == FALSE)
               {
                  if (CloseHandle(tvEventHandle) == 0)
                     debug1("WinSharedMemClient-Init: CloseHandle tvEventHandle: %ld", GetLastError());
                  tvEventHandle = NULL;
               }
            }
            else
               debug1("WinSharedMemClient-Init: CreateEvent " TV_SHM_EVENT_NAME ": %ld", GetLastError());

            // the TV mutex can be always be released - its existance is used to check if the TV app is alive
            if (ReleaseMutex(tvMutexHandle) == 0)
               debug1("WintvSharedMem-Init: ReleaseMutex TV: %ld", GetLastError());
         }
         else
            debug0("WinSharedMemClient-Init: CreateMutex: already exists: another TV app already running");

         if (result == FALSE)
         {
            CloseHandle(tvMutexHandle);
            tvMutexHandle = NULL;
         }
      }
      else
         debug1("WinSharedMemClient-Init: CreateMutex " TV_MUTEX_NAME ": %ld", GetLastError());
   }
   else
      debug0("WinSharedMemClient-Init: illegal NULL ptr param");

   return result;
}

// ---------------------------------------------------------------------------
// Free allocated resources
// - must be called once during shutdown
//
void WinSharedMemClient_Exit( void )
{
   // stop the message receptor thread & wait until it's terminated
   if (msgThreadHandle != NULL)
   {
      StopMsgThread = TRUE;
      if (tvEventHandle != NULL)
      {
         if (SetEvent(tvEventHandle) != 0)
         {  // successfully notified the thread - wait until it exits
            if (WaitForSingleObject(msgThreadHandle, INFINITE) == WAIT_FAILED)
               debug1("WinSharedMemClient-Exit: WaitForSingleObject msgThreadHandle: %ld", GetLastError());
         }
         else
            debug1("WinSharedMemClient-Exit: SetEvent tvEventHandle: %ld", GetLastError());
      }
      else
         debug0("WinSharedMemClient-Exit: tvEventHandle already NULL");

      if (CloseHandle(msgThreadHandle) == 0)
         debug1("WinSharedMemClient-Exit: CloseHandle msgThreadHandle: %ld", GetLastError());

      msgThreadHandle = NULL;
   }

   // if attached, notify the EPG process that we're shutting down
   if (pTvShm != NULL)
   {
      pTvShm->tvAppAlive = FALSE;

      if ((epgEventHandle != NULL) && (SetEvent(epgEventHandle) == 0))
         debug1("WinSharedMemClient-Exit: SetEvent " EPG_SHM_EVENT_NAME ": %ld", GetLastError());
   }

   // detach from shared memory
   WinSharedMemClient_DetachShm();

   // destroy the TV and EPG event handles
   if ((epgEventHandle != NULL) && (CloseHandle(epgEventHandle) == 0))
      debug1("WinSharedMemClient-Exit: CloseHandle epgEventHandle: %ld", GetLastError());
   if ((tvEventHandle != NULL) && (CloseHandle(tvEventHandle) == 0))
      debug1("WinSharedMemClient-Exit: CloseHandle tvEventHandle: %ld", GetLastError());
   epgEventHandle = NULL;
   tvEventHandle  = NULL;

   // destroy the TV app mutex (allow other clients to access the EPG app)
   if (tvMutexHandle != NULL)
   {
      if (CloseHandle(tvMutexHandle) == 0)
         debug1("WinSharedMemClient-Exit: CloseHandle tvMutexHandle: %ld", GetLastError());
      tvMutexHandle = NULL;
   }
}

