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
 *    watch the message exchange with DebugView from sysinternals.com
 *
 *  Author: Tom Zoerner
 *
 *  $Id: winshmclnt.c,v 1.12 2020/06/17 19:39:40 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_TVSIM
//#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static volatile TVAPP_COMM * pTvShm = NULL;

// handle to shared memory
static HANDLE map_fd = NULL;

// handle to EPG app event: used to trigger the EPG app. after updates in shared memory
static HANDLE epgAcqEventHandle = NULL;
static HANDLE epgGuiEventHandle = NULL;

// handle to TV app event: used to wait for triggers from the EPG app.
static HANDLE tvEventHandle = NULL;

// handle to TV mutex: used to make sure only one EPG client is running at the same time
static HANDLE tvMutexHandle = NULL;

// handle to shared memory mutex: used to synchronize access to shared memory
static HANDLE shmMutexHandle = NULL;

// handle to message thread: the thread waits for event on out TV event handle
static HANDLE msgThreadHandle = NULL;

// flag to tell the message receptor thread to terminate
static volatile bool StopMsgThread;

// set to TRUE by the message receptor when a trigger is received
static volatile bool epgTriggerReceived;

// error message of last request or background operation
static char * pLastErrorText = NULL;

// application parameters, passed down during initialization
static const WINSHMCLNT_TVAPP_INFO * pTvAppInfo;

// ---------------------------------------------------------------------------
// Struct that holds state of SHM when last processed
// - used to detect state changes, i.e. when the event is signaled, the
//   current state inside shm is compared with this cache; state changes
//   are equivalent to messages (e.g. incoming EPG info when epgStationIdx
//   is incremented)
// - also used to cache parameters that were handed down from GUI,
//   to save them while detached; they are copied into SHM upon attach
// - the variables have the same meaning as the equally named ones in
//   shared memory, so see there for comments
//
typedef struct
{
   uint32_t  epgStationIdx;
   uint32_t  epgDataIdx;
   uint32_t  epgCommandIdx;
   bool      tvGrantTuner;
   uint8_t   tvCardIdx;
   uint32_t  tvCurIsTuner;
   uint32_t  tvCurFreq;
   uint32_t  tvCurNorm;
   uint32_t  epgReqFreq;
   uint32_t  epgReqNorm;
   uint32_t  epgReqInput;
} EPG_SHM_STATE;

static EPG_SHM_STATE epgShmCache;

// ----------------------------------------------------------------------------
// Save text describing error cause
// - argument list has to be terminated with NULL pointer
// - to be displayed by the GUI to help the user fixing the problem
//
static void WinSharedMemClient_SetErrorText( DWORD errCode, const char * pText, ... )
{
   va_list argl;
   const char *argv[20];
   uint argc, sumlen, off, idx;

   // free the previous error text
   if (pLastErrorText != NULL)
   {
      debug0("WinSharedMemClient-SetErrorText: Warning: previous error text unprocessed - discarding");
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
// Retrieve and clear the last error message
// - this function should always be called when an error is indicated
// - the caller must free the allocated memory!
//
const char * WinSharedMemClient_GetErrorMsg( void )
{
   const char * pErrMsg = pLastErrorText;

   // clear the error message
   if (pLastErrorText != NULL)
   {
      pLastErrorText = NULL;
   }
   else
      debug0("WinSharedMemClient-GetErrorMsg: warning: no error message available");

   // memory must be freed by the caller!
   return pErrMsg;
}

// ---------------------------------------------------------------------------
// Generate EPG version or shared memory size mismatch error message
//
static void WinSharedMemClient_VersionErrorMsg( uint32_t epgShmSize, uint32_t epgShmVersion )
{
   char  strBuf[50];

   if (epgShmVersion != EPG_SHM_VERSION)
   {
      sprintf(strBuf, "%d.%d.%d (expected %d.%d.%d)",
                      EPG_SHM_VERSION_MAJOR(epgShmVersion), EPG_SHM_VERSION_MINOR(epgShmVersion), EPG_SHM_VERSION_PATLEV(epgShmVersion),
                      EPG_SHM_VERSION_MAJOR(EPG_SHM_VERSION), EPG_SHM_VERSION_MINOR(EPG_SHM_VERSION), EPG_SHM_VERSION_PATLEV(EPG_SHM_VERSION));
      WinSharedMemClient_SetErrorText(0, "EPG attach failed: incompatible version ", strBuf, NULL);
   }
   else if (epgShmSize != sizeof(TVAPP_COMM))
   {
      sprintf(strBuf, "0x%x (expected 0x%x)", epgShmSize, sizeof(TVAPP_COMM));
      WinSharedMemClient_SetErrorText(0, "EPG attach failed: incompatible shared memory size ", strBuf, NULL);
   }
   else
      fatal0("WinSharedMemClient-VersionErrorMsg: invalid params");
}

// ---------------------------------------------------------------------------
// Wake up the receiver of a message
//
static bool WinSharedMemClient_TriggerEpg( void )
{
   bool result = TRUE;

   if ((epgAcqEventHandle != NULL) && (SetEvent(epgAcqEventHandle) == 0))
   {
      debug1("WinSharedMemClient-TriggerEpg: SetEvent " EPG_ACQ_SHM_EVENT_NAME ": %ld", GetLastError());
      result = FALSE;
   }

   if ( (pTvShm != NULL) &&
        (pTvShm->epgAppAlive == (EPG_APP_GUI | EPG_APP_DAEMON)) )
   {
      if ((epgGuiEventHandle != NULL) && (SetEvent(epgGuiEventHandle) == 0))
      {
         debug1("WinSharedMemClient-TriggerEpg: SetEvent " EPG_GUI_SHM_EVENT_NAME ": %ld", GetLastError());
         result = FALSE;
      }
   }
   return result;
}

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

         // wake up the receiver
         WinSharedMemClient_TriggerEpg();
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
// - the EPG app will reply with the given number of EPG records for the named
//   station (after VPS/PDC detection; starting with currently running programme)
// - nameLen is string length without the terminating zero;
//   can also be -1: in this case strlen is used (obviously the string must be
//   zero terminated in this case
//
bool WinSharedMemClient_SetStation( const char * pChanName, sint nameLen, uint cni,
                                    bool isTuner, uint freq, uint norm, uint epgPiCnt )
{
   if (pTvShm != NULL)
   {
      if (nameLen < 0)
         nameLen = strlen(pChanName);
      if (nameLen >= TV_CHAN_NAME_MAX_LEN)
         nameLen = TV_CHAN_NAME_MAX_LEN - 1;

      // wait for the semaphore and request "ownership"
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         // copy the values in the cache
         epgShmCache.tvCurIsTuner = isTuner;
         epgShmCache.tvCurFreq = freq;
         epgShmCache.tvCurNorm = norm;

         pTvShm->tvCurIsTuner = isTuner;
         pTvShm->tvCurFreq = freq;
         pTvShm->tvCurNorm = norm;

         memcpy((char *) pTvShm->tvChanName, pChanName, nameLen);
         pTvShm->tvChanName[nameLen] = 0;  // see above: there's enough space for the zero

         pTvShm->tvEpgQueryLen = nameLen;
         pTvShm->tvChanEpgPiCnt = epgPiCnt;

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

         pTvShm->tvStationIdx += 1;

         // release the semaphore
         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WinSharedMemClient-SetChannel: ReleaseMutex: %ld", GetLastError());

         // wake up the receiver
         WinSharedMemClient_TriggerEpg();
      }
      else
         debug1("WinSharedMemClient-SetChannel: WaitForSingleObject: %ld", GetLastError());
   }
   else
   {  // not attached -> save the values in the cache; required upon attach
      epgShmCache.tvCurIsTuner = isTuner;
      epgShmCache.tvCurFreq = freq;
      epgShmCache.tvCurNorm = norm;
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
//   from which frequency
//
bool WinSharedMemClient_SetInputFreq( bool isTuner, uint freq, uint norm )
{
   if (pTvShm != NULL)
   {
      // wait for the semaphore and request "ownership"
      if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
      {
         pTvShm->tvCurIsTuner = isTuner;
         pTvShm->tvCurFreq = freq;
         pTvShm->tvCurNorm = norm;

         // release the semaphore
         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WinSharedMemClient-SetInputFreq: ReleaseMutex: %ld", GetLastError());

         // wake up the receiver
         WinSharedMemClient_TriggerEpg();
      }
      else
         debug1("WinSharedMemClient-SetInputFreq: WaitForSingleObject: %ld", GetLastError());
   }
   return TRUE;
}

// ---------------------------------------------------------------------------
// Fetch program information from shared memory
// - note: GUI must check validity of received data
//   (i.e. title empty or stop time smaller start time)
// - the EPG side makes sure that no data is sent for an obsolete channel
//   after a channel change
//
char * WinSharedMemClient_GetProgInfo( void )
{
   char *pBuffer = NULL;

   if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
   {
      if (pTvShm->epgDataRespType == EPG_DATA_RESP_CHN)
      {
         if (pTvShm->epgDataLen > 0)
         {
            pBuffer = malloc(pTvShm->epgDataLen);
            memcpy(pBuffer, (char *)pTvShm->epgData, pTvShm->epgDataLen);
            // TODO: epgDataOff
         }
         else
            pBuffer = NULL;

         epgShmCache.epgStationIdx = pTvShm->epgStationIdx;
      }

      if (ReleaseMutex(shmMutexHandle) == 0)
         debug1("WinSharedMemClient-GetProgInfo: ReleaseMutex: %ld", GetLastError());

      dprintf3("EPG info received: %d bytes (station idx %d, type=%d)\n", pTvShm->epgDataLen, pTvShm->epgStationIdx, pTvShm->epgDataRespType);
   }
   else
      debug1("WinSharedMemClient-GetProgInfo: WaitForSingleObject: %ld", GetLastError());

   return pBuffer;
}

// ---------------------------------------------------------------------------
// Fetch EPG command vector from shared memory
//
bool WinSharedMemClient_GetCmdArgv( uint * pArgc, uint * pArgLen, char * pCmdBuf, uint cmdMaxLen )
{
   bool result = FALSE;

   if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
   {
      if ((pCmdBuf != NULL) && (cmdMaxLen > 0))
      {
         if (cmdMaxLen > EPG_CMD_MAX_LEN)
            cmdMaxLen = EPG_CMD_MAX_LEN;
         memcpy(pCmdBuf, (char *) pTvShm->epgCommand, cmdMaxLen);
      }
      if (pArgc != NULL)
         *pArgc = pTvShm->epgCmdArgc;
      if (pArgLen != NULL)
         *pArgLen = pTvShm->epgCmdArgLen;

      // signal EPG app that command is processed and we're ready for the next command
      pTvShm->tvCommandIdx = pTvShm->epgCommandIdx;

      epgShmCache.epgCommandIdx = pTvShm->epgCommandIdx;

      if (ReleaseMutex(shmMutexHandle) == 0)
         debug1("WinSharedMemClient-GetCmdArgv: ReleaseMutex: %ld", GetLastError());

      if ((pArgc != NULL) && (pCmdBuf != NULL) && (cmdMaxLen > 0))
         dprintf3("EPG command received (len=%d argc=%d): \"%s\"\n", *pArgLen, *pArgc, pCmdBuf);

      // notify EPG app that shared memory content has changed
      WinSharedMemClient_TriggerEpg();

      result = TRUE;
   }
   else
      debug1("WinSharedMemClient-GetCmdArgv: WaitForSingleObject: %ld", GetLastError());

   return result;
}

// ---------------------------------------------------------------------------
// Fetch requested input source and tuner frequency
//
bool WinSharedMemClient_GetInpFreq( uint * pInputSrc, uint * pFreq, uint * pNorm )
{
   bool result = FALSE;

   if (WaitForSingleObject(shmMutexHandle, INFINITE) != WAIT_FAILED)
   {
      epgShmCache.epgReqInput = pTvShm->epgReqInput;
      epgShmCache.epgReqFreq  = pTvShm->epgReqFreq;
      epgShmCache.epgReqNorm  = pTvShm->epgReqNorm;

      if (ReleaseMutex(shmMutexHandle) == 0)
         debug1("WinSharedMemClient-GetInpFreq: ReleaseMutex: %ld", GetLastError());

      if (pInputSrc != NULL)
         *pInputSrc = epgShmCache.epgReqInput;
      if (pFreq != NULL)
         *pFreq = epgShmCache.epgReqFreq;
      if (pNorm != NULL)
         *pNorm = epgShmCache.epgReqNorm;

      dprintf3("EPG requests input %d, freq %d, norm %d\n", epgShmCache.epgReqInput, epgShmCache.epgReqFreq, epgShmCache.epgReqNorm);
      result = TRUE;
   }
   else
      debug1("WinSharedMemClient-GetInpFreq: WaitForSingleObject: %ld", GetLastError());

   return result;
}

// ---------------------------------------------------------------------------
// Process changes in shared memory after the event was signaled
// - compares all shared memory elements with the previously cached value
// - if a change is detected it's reported by the return code; the caller
//   should invoke this function repeatedly until event NONE is returned
//
WINSHMCLNT_EVENT WinSharedMemClient_GetEpgEvent( void )
{
   bool  triggered;

   // check if we were triggered by the EPG app or if this is just a polling
   triggered = epgTriggerReceived;
   if (triggered)
   {  // note: only clear the var if it's set to avoid race condition with msg thread
      epgTriggerReceived = FALSE;
   }

   if (pTvShm == NULL)
   {
      // attempt an attach only if we received a trigger on our event handle
      if (triggered)
      {
         // received an event while shared memory not connected yet -> attempt to attach
         WinSharedMemClient_AttachShm();
         if (pTvShm != NULL)
         {  // attach successful, i.e. EPG application is up and running
            dprintf0("EPG app started - attached SHM\n");
            return SHM_EVENT_ATTACH;
         }
         else
         {  // failed to attach: report significant errors (like version conflicts)
            return SHM_EVENT_ATTACH_ERROR;
         }
      }
   }

   if (pTvShm != NULL)
   {
      if (pTvShm->epgAppAlive == FALSE)
      {
         dprintf0("EPG app terminated - detaching SHM\n");
         // detach from shared memory, close mutex handle; event handles remain open
         WinSharedMemClient_DetachShm();

         return SHM_EVENT_DETACH;
      }
      else
      {  // EPG app is running: check for changes in EPG controlled params

         if (epgShmCache.epgStationIdx != pTvShm->epgStationIdx)
         {  // EPG information was updated
            if (pTvShm->epgDataRespType != EPG_DATA_RESP_CHN)
            {  // station data was overwritten by query result
               epgShmCache.epgStationIdx = pTvShm->epgStationIdx;
            }
            else
               return SHM_EVENT_STATION_INFO;
         }

         if (epgShmCache.epgDataIdx != pTvShm->epgDataIdx)
         {  // reply to EPG query arrived
            if (pTvShm->epgDataRespType != EPG_DATA_RESP_QUERY)
            {  // query result was overwritten by station update
               epgShmCache.epgDataIdx = pTvShm->epgDataIdx;
            }
            else
               return SHM_EVENT_EPG_INFO;
         }

         if (epgShmCache.epgCommandIdx != pTvShm->epgCommandIdx)
         {
            return SHM_EVENT_CMD_ARGV;
         }

         if ( (epgShmCache.epgReqInput != pTvShm->epgReqInput) ||
              (epgShmCache.epgReqFreq != pTvShm->epgReqFreq) ||
              (epgShmCache.epgReqNorm != pTvShm->epgReqNorm) )
         {
            if (epgShmCache.tvGrantTuner)
            {
               return SHM_EVENT_INP_FREQ;
            }
         }
      }
   }
   return SHM_EVENT_NONE;
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
         dprintf0("WinSharedMemClient-EventThread: trigger\n");
         epgTriggerReceived = TRUE;

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
   DWORD errCode;
   bool  result = FALSE;

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
               if (pTvShm->epgAppAlive)
               {
                  if ( (pTvShm->epgShmVersion == EPG_SHM_VERSION) &&
                       (pTvShm->epgShmSize == sizeof(TVAPP_COMM)) )
                  {
                     // initialize TV app.'s params in shared memory
                     pTvShm->tvReqTvCard  = TRUE;
                     pTvShm->tvCardIdx    = epgShmCache.tvCardIdx;
                     pTvShm->tvFeatures   = pTvAppInfo->tvFeatures;
                     pTvShm->tvAppType    = pTvAppInfo->tvAppType;
                     strncpy((char *) pTvShm->tvAppName, pTvAppInfo->pAppName, TVAPP_NAME_MAX_LEN - 1);
                     pTvShm->tvAppName[TVAPP_NAME_MAX_LEN - 1] = 0;
                     strncpy((char *) pTvShm->tvAppPath, pTvAppInfo->tvAppPath, TVAPP_PATH_MAX_LEN - 1);
                     pTvShm->tvAppPath[TVAPP_PATH_MAX_LEN - 1] = 0;
                     pTvShm->tvGrantTuner = epgShmCache.tvGrantTuner;
                     pTvShm->tvCurIsTuner = epgShmCache.tvCurIsTuner;
                     pTvShm->tvCurFreq    = epgShmCache.tvCurFreq;
                     pTvShm->tvCurNorm    = epgShmCache.tvCurNorm;
                     pTvShm->tvAppAlive   = TRUE;

                     // initialize state cache with EPG app.'s params
                     epgShmCache.epgStationIdx  = pTvShm->epgStationIdx;
                     epgShmCache.epgDataIdx     = pTvShm->epgDataIdx;
                     epgShmCache.epgCommandIdx  = pTvShm->epgCommandIdx;
                     // do not copy the actual values here, to trigger an event in the handler
                     epgShmCache.epgReqFreq     = EPG_REQ_FREQ_NONE;
                     epgShmCache.epgReqInput    = EPG_REQ_INPUT_NONE;

                     if ( WinSharedMemClient_TriggerEpg() )
                     {
                        // set VBI buffer address (used by TTX/EPG decoder)
                        pVbiBuf = &pTvShm->vbiBuf;

                        result = TRUE;
                     }
                     else
                        WinSharedMemClient_SetErrorText(GetLastError(), "EPG attach failed: can't trigger EPG event", NULL);
                  }
                  else
                     WinSharedMemClient_VersionErrorMsg(pTvShm->epgShmSize, pTvShm->epgShmVersion);
               }
               else
                  debug0("WinSharedMemClient-AttachShm: EPG app no longer alive");

               if (result == FALSE)
               {
                  UnmapViewOfFile((void *) pTvShm);
                  pTvShm = NULL;
               }
            }
            else
            {  // failed to map shared memory -> map only first two words to check the version number
               errCode = GetLastError();
               pTvShm = MapViewOfFileEx(map_fd, FILE_MAP_ALL_ACCESS, 0, 0, 2 * sizeof(uint32_t), NULL);
               if (pTvShm  != NULL)
               {
                  if ( (pTvShm->epgShmSize != sizeof(TVAPP_COMM)) ||
                       (pTvShm->epgShmVersion != EPG_SHM_VERSION) )
                  {
                     WinSharedMemClient_VersionErrorMsg(pTvShm->epgShmSize, pTvShm->epgShmVersion);
                  }
                  else
                     WinSharedMemClient_SetErrorText(errCode, "EPG attach failed: can't map shared memory", NULL);

                  if (UnmapViewOfFile((void *) pTvShm) == 0)
                     debug1("WinSharedMemClient-AttachShm: UnmapViewOfFile: %ld", GetLastError());
                  pTvShm = NULL;
               }
               else
                  WinSharedMemClient_SetErrorText(GetLastError(), "EPG attach failed: can't map shared memory", NULL);
            }

            if (result == FALSE)
            {
               CloseHandle(map_fd);
               map_fd = NULL;
            }
         }
         else
            WinSharedMemClient_SetErrorText(GetLastError(), "EPG attach failed: can't open shared memory handle", NULL);

         if (ReleaseMutex(shmMutexHandle) == 0)
            debug1("WinSharedMemClient-AttachShm: ReleaseMutex: %ld", GetLastError());
      }
      else
         WinSharedMemClient_SetErrorText(GetLastError(), "EPG attach failed: can't lock shared memory mutex", NULL);

      if (result == FALSE)
      {
         CloseHandle(shmMutexHandle);
         shmMutexHandle = NULL;
      }
   }
   else
   {  // note: this error is not reported to the user because it's not necessarily an error;
      // we don't know what the EPG app was trying to tell us; it might just have shut down
      debug1("WinSharedMemClient-AttachShm: OpenMutex: %ld", GetLastError());
   }

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

   if ((pTvShm != NULL) && (UnmapViewOfFile((void *) pTvShm) == 0))
      debug1("WinSharedMemClient-DetachShm: UnmapViewOfFile: %ld", GetLastError());
   pTvShm = NULL;
}

// ---------------------------------------------------------------------------
// Initialize the module
// - has to be called exactly once during startup
// - create the resources which are required for inter-process communication
// - an already running EPG app can be detected by checking if the resources
//   already are created
// - if the EPG app is running, an attach is attempted right away; note: this
//   must be done immediately, because the BT8x8 driver must be freed;
//   a successful or failed attach is reported via the event pointer argument
// - returns TRUE on success;
//   returns FALSE upon fatal errors, i.e. the module will remain "dead"
//
bool WinSharedMemClient_Init( const WINSHMCLNT_TVAPP_INFO * pInitInfo,
                              uint cardIdx, WINSHMCLNT_EVENT * pEvent )
{
   WINSHMCLNT_EVENT  attachEvent = SHM_EVENT_NONE;
   uint  idx;
   bool  epgAppAlive;
   DWORD msgThreadId;
   bool  result = FALSE;

   if (pInitInfo != NULL)
   {
      // save TV application parameters
      pTvAppInfo = pInitInfo;

      // initialize the state cache for the TV app side
      memset(&epgShmCache, 0, sizeof(epgShmCache));
      epgShmCache.tvCardIdx  = cardIdx;
      epgShmCache.tvCurFreq  = EPG_REQ_FREQ_NONE;
      epgShmCache.tvCurIsTuner = FALSE;

      tvMutexHandle = CreateMutex(NULL, TRUE, TV_MUTEX_NAME);
      if (tvMutexHandle != NULL)
      {
         if (GetLastError() != ERROR_ALREADY_EXISTS)
         {
            tvEventHandle = CreateEvent(NULL, FALSE, FALSE, TV_SHM_EVENT_NAME);
            if (tvEventHandle != NULL)
            {
               epgAcqEventHandle = CreateEvent(NULL, FALSE, FALSE, EPG_ACQ_SHM_EVENT_NAME);
               epgGuiEventHandle = CreateEvent(NULL, FALSE, FALSE, EPG_GUI_SHM_EVENT_NAME);
               if ((epgAcqEventHandle != NULL) && (epgGuiEventHandle != NULL))
               {
                  epgAppAlive = (GetLastError() == ERROR_ALREADY_EXISTS);

                  StopMsgThread = FALSE;
                  epgTriggerReceived = FALSE;
                  msgThreadHandle = CreateThread(NULL, 0, WinSharedMemClient_EventThread, NULL, 0, &msgThreadId);
                  if (msgThreadHandle != NULL)
                  {
                     if (epgAppAlive)
                     {  // EPG app already running -> open event and signal the app

                        dprintf0("WinSharedMemClient-Init: EPG app already running, attaching SHM\n");
                        if ( WinSharedMemClient_AttachShm() )
                        {
                           assert(pTvShm != NULL);  // pointer initialized by attach

                           // wait for the EPG app to free the driver
                           for (idx=0; idx < 2; idx++)
                           {
                              if ( (pTvShm->epgHasDriver == FALSE) ||
                                   ((pTvShm->epgTvCardIdx != cardIdx) && (pTvShm->epgTvCardIdx != TVAPP_CARD_REQ_ALL)) )
                                 break;

                              WinSharedMemClient_TriggerEpg();

                              if (WaitForSingleObject(tvEventHandle, 1000) == WAIT_FAILED)
                                 debug1("WinSharedMemClient-Init: WaitForSingleObject tvEventHandle: %ld", GetLastError());
                           }
                           dprintf2("WinSharedMemClient-Init: epgHasDriver=%d epgTvCardIdx=%d\n", pTvShm->epgHasDriver, pTvShm->epgTvCardIdx);

                           attachEvent = SHM_EVENT_ATTACH;
                        }
                        else if (pLastErrorText != NULL)
                        {  // report attach failure (note that some types of errors are ignored)
                           attachEvent = SHM_EVENT_ATTACH_ERROR;
                        }
                     }

                     if (pEvent != NULL)
                        *pEvent = attachEvent;

                     // initialization completed successfully
                     // (the attach to an EPG app may have failed, but this is reported separately)
                     result = TRUE;
                  }
                  else
                     WinSharedMemClient_SetErrorText(GetLastError(), "EPG client init failed: can't create message receptor thread", NULL);

                  if (result == FALSE)
                  {
                     if (CloseHandle(epgAcqEventHandle) == 0)
                        debug1("WinSharedMemClient-Init: CloseHandle epgAcqEventHandle: %ld", GetLastError());
                     epgAcqEventHandle = NULL;
                     if (CloseHandle(epgGuiEventHandle) == 0)
                        debug1("WinSharedMemClient-Init: CloseHandle epgGuiEventHandle: %ld", GetLastError());
                     epgGuiEventHandle = NULL;
                  }
               }
               else
                  WinSharedMemClient_SetErrorText(GetLastError(), "EPG client init failed: can't create EPG event handles", NULL);

               if (result == FALSE)
               {
                  if (CloseHandle(tvEventHandle) == 0)
                     debug1("WinSharedMemClient-Init: CloseHandle tvEventHandle: %ld", GetLastError());
                  tvEventHandle = NULL;
               }
            }
            else
               WinSharedMemClient_SetErrorText(GetLastError(), "EPG client init failed: can't create TV event handle", NULL);

            // the TV mutex can be always be released - its existance is used to check if the TV app is alive
            if (ReleaseMutex(tvMutexHandle) == 0)
               debug1("WintvSharedMem-Init: ReleaseMutex TV: %ld", GetLastError());
         }
         else
            WinSharedMemClient_SetErrorText(0, "EPG client init failed: another TV client is already running", NULL);

         if (result == FALSE)
         {
            CloseHandle(tvMutexHandle);
            tvMutexHandle = NULL;
         }
      }
      else
         WinSharedMemClient_SetErrorText(GetLastError(), "EPG client init failed: can't create TV mutex", NULL);
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
   // free the error message text
   if (pLastErrorText != NULL)
   {
      xfree(pLastErrorText);
      pLastErrorText = NULL;
   }

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

      WinSharedMemClient_TriggerEpg();
   }

   // detach from shared memory
   WinSharedMemClient_DetachShm();

   // destroy the TV and EPG event handles
   if ((epgAcqEventHandle != NULL) && (CloseHandle(epgAcqEventHandle) == 0))
      debug1("WinSharedMemClient-Exit: CloseHandle epgAcqEventHandle: %ld", GetLastError());
   if ((epgGuiEventHandle != NULL) && (CloseHandle(epgGuiEventHandle) == 0))
      debug1("WinSharedMemClient-Exit: CloseHandle epgGuiEventHandle: %ld", GetLastError());
   if ((tvEventHandle != NULL) && (CloseHandle(tvEventHandle) == 0))
      debug1("WinSharedMemClient-Exit: CloseHandle tvEventHandle: %ld", GetLastError());
   epgAcqEventHandle = NULL;
   epgGuiEventHandle = NULL;
   tvEventHandle  = NULL;

   // destroy the TV app mutex (allow other clients to access the EPG app)
   if (tvMutexHandle != NULL)
   {
      if (CloseHandle(tvMutexHandle) == 0)
         debug1("WinSharedMemClient-Exit: CloseHandle tvMutexHandle: %ld", GetLastError());
      tvMutexHandle = NULL;
   }
}

