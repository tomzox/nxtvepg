/*
 *  EPG teletext packet playback for debugging purposes
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
 *    This module contains is the main module of the small utility
 *    "vbiplay.exe" that plays back teletext pages of an EPG page that
 *    previously have been saved to a file with vbirec.exe
 *
 *  Author: Tom Zoerner
 *
 *  $Id: vbiplay.c,v 1.9 2007/12/31 18:47:28 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_TVSIM
#define DPRINTF_OFF

#include <windows.h>

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/winshm.h"
#include "tvsim/winshmclnt.h"


volatile EPGACQ_BUF * pVbiBuf;
static HANDLE playEventHandle;
static CRITICAL_SECTION m_cCrit;

// ---------------------------------------------------------------------------
// Callback for shared memory handler: invoked when EPG app attaches or detaches
// - when a EPG application attaches, playback is started
//
static void EpgAttach( bool attach )
{
   if (attach)
   {
      dprintf0("TvSimuMsg-Attach: start VBI playback\n");
      // trigger an event to wake up the main loop from it's wait function
      SetEvent(playEventHandle);
   }
   else
      dprintf0("TvSimuMsg-Attach: detached\n");
}

// ---------------------------------------------------------------------------
// Callback for shared memory handler: invoked when EPG attach fails
// - when a EPG application attaches, playback is started
//
static void EpgError( void )
{
   const uchar * pErrMsg;

   pErrMsg = WinSharedMemClient_GetErrorMsg();
   fprintf(stderr, "%s", ((pErrMsg != NULL) ? (char*)pErrMsg : "EPG client init failed"));

   if (pErrMsg != NULL)
      xfree((void *) pErrMsg);
}

// ---------------------------------------------------------------------------
// Trigger function for shared memory events
// - called whenever the connected EPG application changed a value in shared mem
// - this function is executed in a separated thread (which is used to wait for
//   EPG events) hence a semaphore is used to synchronize with the main loop
//
static void EpgEvent( void )
{
   WINSHMCLNT_EVENT curEvent;
   bool  shouldExit;

   EnterCriticalSection (&m_cCrit);
   shouldExit = FALSE;
   do
   {
      curEvent = WinSharedMemClient_GetEpgEvent();
      switch (curEvent)
      {
         case SHM_EVENT_ATTACH:
            EpgAttach(TRUE);
            break;

         case SHM_EVENT_DETACH:
            EpgAttach(FALSE);
            break;

         case SHM_EVENT_ATTACH_ERROR:
            EpgError();
            shouldExit = TRUE;
            break;

         case SHM_EVENT_STATION_INFO:
         case SHM_EVENT_EPG_INFO:
            // ignored
            break;

         case SHM_EVENT_CMD_ARGV:
            // ignored
            break;

         case SHM_EVENT_INP_FREQ:
            // ignored
            break;

         case SHM_EVENT_NONE:
            shouldExit = TRUE;
            break;

         default:
            debug1("TvSimu-IdleHandler: unknown EPG event %d - ignored", curEvent);
      }
   }
   while (shouldExit == FALSE);
   LeaveCriticalSection (&m_cCrit);
}

// ---------------------------------------------------------------------------
// structure which is passed to the shm client init function
// - only the trigger and attach callbacks are used by this app
//
static const WINSHMCLNT_TVAPP_INFO tvSimuInfo =
{
   "VBI playback",
   "",
   TVAPP_NONE,
   TVAPP_FEAT_TTX_FWD,

   EpgEvent,
};

// ---------------------------------------------------------------------------
// Playback & event loop
// - this loop waits for an EPG application to attach
// - while an app is attached is copies VBI lines from the file to shared mem
//
static void PlaybackVbi( int fdTtxFile )
{
   VBI_LINE * vbl;
   size_t rstat;
   bool doSleep;

   while (1)
   {
      doSleep = FALSE;
      EnterCriticalSection (&m_cCrit);
      if (pVbiBuf != NULL)
      {
         if (pVbiBuf->chanChangeCnf != pVbiBuf->chanChangeReq)
         {
            pVbiBuf->chanChangeCnf = pVbiBuf->chanChangeReq;
            pVbiBuf->reader_idx    = 0;
            pVbiBuf->writer_idx    = 0;
            memset((void *) &pVbiBuf->ttxStats, 0, sizeof(pVbiBuf->ttxStats));
         }

         if ( (pVbiBuf->epgEnabled) &&
              (pVbiBuf->reader_idx != ((pVbiBuf->writer_idx + 1) % TTXACQ_BUF_COUNT)) )
         {
            vbl = (VBI_LINE *) &pVbiBuf->line[pVbiBuf->writer_idx];

            rstat = read(fdTtxFile, vbl, sizeof(VBI_LINE));
            if ((rstat < 0) || (rstat < sizeof(VBI_LINE)))
            {  // reached end of file -> terminate the loop
               break;
            }
            pVbiBuf->mipPageNo = vbl->pageno;

            pVbiBuf->writer_idx = (pVbiBuf->writer_idx + 1) % TTXACQ_BUF_COUNT;
            pVbiBuf->ttxStats.ttxPkgCount  += 1;
            pVbiBuf->ttxStats.epgPkgCount  += 1;

            if (vbl->pkgno == 0)
            {
               VBI_LINE * pHead;
               pVbiBuf->ttxStats.epgPagCount  += 1;

               pVbiBuf->ttxHeader.write_ind += 1;
               pVbiBuf->ttxHeader.write_idx = (pVbiBuf->ttxHeader.write_idx + 1) % EPGACQ_ROLL_HEAD_COUNT;
               pHead = (VBI_LINE*) pVbiBuf->ttxHeader.ring_buf + pVbiBuf->ttxHeader.write_idx;
               memcpy((char *) pHead->data, vbl->data, sizeof(pHead->data));
               pHead->pageno = vbl->pageno;
               pHead->ctrl_lo = vbl->ctrl_lo;
               pHead->ctrl_hi = vbl->ctrl_hi;
            }
            //dprintf1("wrote pkg %d\n", vbl->pkgno);
         }
         else
         {  // wait for the client to process the lines in the buffer
            doSleep = TRUE;
         }
      }
      LeaveCriticalSection (&m_cCrit);

      if (pVbiBuf == NULL)
      {  // nxtvepg not attached -> wait for event
         dprintf0("Playback-Vbi: waiting for attach\n");
         WaitForSingleObject(playEventHandle, INFINITE);
      }
      else if (doSleep)
      {  // acq not enabled or buffer full -> wait some, then try again
         Sleep(100);
      }
   }
   dprintf0("TvSimuMsg-Attach: VBI playback finished\n");
}

// ---------------------------------------------------------------------------
// Main
//
int main( int argc, char ** argv )
{
   WINSHMCLNT_EVENT attachEvent;
   int  fdTtxFile;

   InitializeCriticalSection (&m_cCrit);

   fdTtxFile = open("ttx.dat", O_RDONLY|O_BINARY, 0666);
   if (fdTtxFile != -1)
   {
      playEventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
      if (playEventHandle != NULL)
      {
         pVbiBuf = NULL;
         dprintf0("VbiPlay-Main: init done, waiting for EPG app start\n");

         if (WinSharedMemClient_Init(&tvSimuInfo, TVAPP_CARD_REQ_ALL, &attachEvent))
         {
            if (attachEvent != SHM_EVENT_ATTACH_ERROR)
            {
               // report attach failures during initialization
               if (attachEvent == SHM_EVENT_ATTACH)
                  EpgAttach(TRUE);

               // enter the event loop
               PlaybackVbi(fdTtxFile);

               // done -> detach from shared memory
               WinSharedMemClient_Exit();
            }
            else
               EpgError();
         }
         else
         {
            EpgError();
         }
         CloseHandle(playEventHandle);
      }
      close(fdTtxFile);
   }
   else
      fprintf(stderr, "open %s: %s\n", "ttx.dat", strerror(errno));

   return 0;
}

