/*
 *  VBI driver interface for Linux
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
 *    This module works as interface between acquisition control and
 *    the bttv kernel driver. The actual data acquisition is done in
 *    a slave process; the main reason for that is that it has to
 *    work under real-time conditions, i.e. every 20 ms it *must*
 *    get the CPU to read in one frame's worth of VBI lines. In
 *    earlier driver/kernel versions also select(2) did not work on
 *    /dev/vbi so that multiplexing with other tasks like GUI was
 *    impossible.
 *
 *    Master and slave process communicate through shared memory.
 *    Next to control parameters that allow to pass commands like
 *    acquisition on/off it contains a ring buffer for teletext
 *    packets. The buffer is managed in the epgdbacq module. When
 *    you read the code in the module, always remember that some
 *    of it is executed in the master process, some in the slave.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: btdrv4linux.c,v 1.3 2000/12/26 15:51:56 tom Exp tom $
 */

#if !defined(linux) && !defined(__NetBSD__)
#error "This module is for Linux and NetBSD only"
#endif

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/resource.h>
//# define _WAITFLAGS_H              // conflicts with linux/wait.h
#include <sys/wait.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbacq.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/btdrv.h"

#ifdef __NetBSD__
# include <dev/ic/bt8xx.h>
# define VBI_MAXLINES 19
#else
# include "linux/videodev.h"
# define VBI_MAXLINES 16
#endif

#define VBINAME       "/dev/vbi"
#define VIDEONAME     "/dev/video"
#define PIDFILENAME   "/tmp/.vbi%u.pid"

#define VBI_LINENUM VBI_MAXLINES
#define VBI_BPL     2048
#define VBI_BPF     (VBI_LINENUM*2*VBI_BPL)

#define DEV_MAX_NAME_LEN 32

EPGACQ_BUF *pVbiBuf;

// vars used in both processes
static bool isVbiProcess;
static bool recvWakeUpSig;

// vars used in the acq slave process
static bool acqShouldExit;
static bool freeDevice;
static int vbiCardIndex;
static int vbi_fdin;
static int shmId;

// cars used in the control process
static int video_fd;


// ---------------------------------------------------------------------------
// Close video device
//
void BtDriver_CloseDevice( void )
{
   if (video_fd != -1)
   {
      close(video_fd);
      video_fd = -1;
   }
}

// ---------------------------------------------------------------------------
// Change the video input source
//
bool BtDriver_SetInputSource( int inputIdx, bool keepOpen, bool * pIsTuner )
{
   struct video_channel vchan;
   struct video_tuner vtuner;
   char devName[DEV_MAX_NAME_LEN];
   bool isTuner = FALSE;
   bool result = FALSE;

   if (video_fd == -1)
   {
      sprintf(devName, VIDEONAME "%u", pVbiBuf->cardIndex);
      video_fd = open(devName, O_RDWR);
   }
   if (video_fd != -1)
   {
      // get current config of the selected chanel
      memset(&vchan, 0, sizeof(vchan));
      vchan.channel = inputIdx;
      if (ioctl(video_fd, VIDIOCGCHAN, &vchan) == 0)
      {  // channel index is valid

         // select the channel as input
         vchan.channel = inputIdx;
         // XXX don't really know why I have to set norm AUTO here
         // XXX but else I have no VBI reception after booting
         // XXX must not use this with non-tuner inputs, or acq is always dead
         if (inputIdx == 0)
            vchan.norm = VIDEO_MODE_AUTO;
         if (ioctl(video_fd, VIDIOCSCHAN, &vchan) == 0)
         {
            if ( (vchan.type & VIDEO_TYPE_TV) && (vchan.flags & VIDEO_VC_TUNER) )
            {
               // query the settings of tuner #0
               memset(&vtuner, 0, sizeof(vtuner));
               if (ioctl(video_fd, VIDIOCGTUNER, &vtuner) == 0)
               {
                  if (vtuner.flags & VIDEO_TUNER_PAL)
                  {
                     vtuner.mode = VIDEO_MODE_PAL;
                     if (ioctl(video_fd, VIDIOCSTUNER, &vtuner) == 0)
                     {
                        isTuner = TRUE;
                        result = TRUE;
                     }
                     else
                        perror("VIDIOCSTUNER");
                  }
                  else
                     fprintf(stderr, "tuner supports no PAL\n");
               }
               else
                  perror("VIDIOCGTUNER");
            }
            else
            {  // not a tuner -> already done
               result = TRUE;
            }
         }
         else
            perror("VIDIOCSCHAN");
      }
      else
         perror("VIDIOCGCHAN");

      if ((keepOpen == FALSE) || (result == FALSE) || (isTuner == FALSE))
      {
         BtDriver_CloseDevice();
      }
   }
   else
      debug1("BtDriver-SetInputSource: could not open device %s", devName);

   if (pIsTuner != NULL)
      *pIsTuner = isTuner;

   return result;
}

// ---------------------------------------------------------------------------
// Tune a given frequency
//
bool BtDriver_TuneChannel( ulong freq, bool keepOpen )
{
   char devName[DEV_MAX_NAME_LEN];
   bool result = FALSE;

   if (video_fd == -1)
   {
      sprintf(devName, VIDEONAME "%u", pVbiBuf->cardIndex);
      video_fd = open(devName, O_RDWR);
   }
   if (video_fd != -1)
   {
      // Set the tuner frequency
      if(ioctl(video_fd, VIDIOCSFREQ, &freq) == 0)
      {
         //printf("Vbi-TuneChannel: set to %.2f\n", (double)freq/16);

         result = TRUE;
      }
      else
         perror("VIDIOCSFREQ");

      if (keepOpen == FALSE)
      {
         BtDriver_CloseDevice();
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Get signal strength on current tuner frequency
//
bool BtDriver_IsVideoPresent( void )
{
   struct video_tuner vtuner;
   bool result = FALSE;

   if ( video_fd != -1 )
   {
      vtuner.tuner = 0;
      if (ioctl(video_fd, VIDIOCGTUNER, &vtuner) == 0)
      {
         //printf("BtDriver-GetSignalStrength: %u\n", vtuner.signal);
         result = (vtuner.signal >= 32768);
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Return name for given TV card
//
const char * BtDriver_GetCardName( uint cardIndex )
{
   const char * pName = NULL;
   struct video_capability vcapab;
   char devName[DEV_MAX_NAME_LEN];
   #define MAX_CARD_NAME_LEN 32
   static char name[MAX_CARD_NAME_LEN + 1];

   if (video_fd != -1)
   {
      BtDriver_CloseDevice();
   }
   sprintf(devName, VIDEONAME "%u", cardIndex);
   video_fd = open(devName, O_RDWR);

   if (video_fd != -1)
   {
      memset(&vcapab, 0, sizeof(vcapab));
      if (ioctl(video_fd, VIDIOCGCAP, &vcapab) == 0)
      {
         strncpy(name, vcapab.name, MAX_CARD_NAME_LEN);
         name[MAX_CARD_NAME_LEN] = 0;
         pName = (const char *) name;
      }
      else
         perror("ioctl VIDIOCGCAP");

      BtDriver_CloseDevice();
   }
   else if (errno == EBUSY)
   {  // device exists, but is busy -> must not return NULL
      sprintf(name, "#%u (video device busy)", cardIndex);
      pName = (const char *) name;
   }
   return pName;
}

// ---------------------------------------------------------------------------
// Return name for given input source index
// - has to be called repeatedly with incremented indices until NULL is returned
// - video device is kept open inbetween calls and only closed upon final call
//
const char * BtDriver_GetInputName( uint cardIndex, uint inputIdx )
{
   struct video_capability vcapab;
   struct video_channel vchan;
   char devName[DEV_MAX_NAME_LEN];
   const char * pName = NULL;
   #define MAX_INPUT_NAME_LEN 32
   static char name[MAX_INPUT_NAME_LEN + 1];

   if (video_fd == -1)
   {
      sprintf(devName, VIDEONAME "%u", cardIndex);
      video_fd = open(devName, O_RDWR);
   }

   if (video_fd != -1)
   {
      memset(&vcapab, 0, sizeof(vcapab));
      if (ioctl(video_fd, VIDIOCGCAP, &vcapab) == 0)
      {
         if (inputIdx < vcapab.channels)
         {
            vchan.channel = inputIdx;
            if (ioctl(video_fd, VIDIOCGCHAN, &vchan) == 0)
            {
               strncpy(name, vchan.name, MAX_INPUT_NAME_LEN);
               name[MAX_INPUT_NAME_LEN] = 0;
               pName = (const char *) name;
            }
            else
               perror("ioctl VIDIOCGCHAN");
         }
      }
      else
         perror("ioctl VIDIOCGCAP");
   }

   if ((pName == NULL) && (video_fd != -1))
   {
      BtDriver_CloseDevice();
   }

   return pName;
}

// ---------------------------------------------------------------------------
// Set parameters for acquisition
// - the card index is simply passed to the slave process, which switches
//   the device automatically if neccessary; if the device is busy, acquisition
//   is stopped
// - tuner type and PLL are already configured in the kernel
//   hence these parameters can be ignored in Linux
// - there isn't any need for priority adaptions, so that's not supported either
//
void BtDriver_Configure( int cardIndex, int tunerType, int pll, int prio )
{
   pVbiBuf->cardIndex = cardIndex;
}

#if 0
// ---------------------------------------------------------------------------
// Check if the video device is free
//
bool BtDriver_CheckDevice( void )
{
   char devName[DEV_MAX_NAME_LEN];
   int  fd;
   bool result;

   if (video_fd == -1)
   {
      sprintf(devName, VIDEONAME "%u", pVbiBuf->cardIndex);
      fd = open(devName, O_RDWR);
      if (fd != -1)
      {
         close(fd);
         result = TRUE;
      }
      else
         result = FALSE;
   }
   else
      result = TRUE;

   return result;
}
#endif


// ---------------------------------------------------------------------------
// The Acquisition process bows out on the usual signals
//
static void BtDriver_SignalHandler( int sigval )
{
   acqShouldExit = TRUE;
   signal(sigval, BtDriver_SignalHandler);
}

// ---------------------------------------------------------------------------
// Receive wake-up signal or ACK
//
static void BtDriver_SignalWakeUp( int sigval )
{
   // do nothing
   recvWakeUpSig = TRUE;
   signal(sigval, BtDriver_SignalWakeUp);
}

// ---------------------------------------------------------------------------
// Receive signal to free or take vbi device
//
static void BtDriver_SignalHangup( int sigval )
{
   freeDevice = TRUE;
   signal(sigval, BtDriver_SignalHangup);
}

// ---------------------------------------------------------------------------
// Wake-up the acq child process to start acquisition
// - the child signals back after it completed the operation
// - the status of the operation is in the isEnabled flag
//
bool BtDriver_StartAcq( void )
{
   bool result = FALSE;
   struct timeval tv;

   if ((pVbiBuf != NULL) && (pVbiBuf->vbiPid != -1))
   {
      recvWakeUpSig = FALSE;
      if (kill(pVbiBuf->vbiPid, SIGUSR1) != -1)
      {
         if ((recvWakeUpSig == FALSE) && (pVbiBuf->isEnabled == FALSE))
         {
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            select(0, NULL, NULL, NULL, &tv);
         }
         result = pVbiBuf->isEnabled;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition
// - not needed in UNIX, since acq main loop checks variable in shm
//
void BtDriver_StopAcq( void )
{
}

// ---------------------------------------------------------------------------
// Terminate the acquisition process
//
void BtDriver_Exit( void )
{
   struct shmid_ds stat;

   if (pVbiBuf != NULL)
   {
      if ((isVbiProcess == FALSE) && (pVbiBuf->vbiPid != -1))
      {  // this is the parent process -> kill the child process first
         if (kill(pVbiBuf->vbiPid, SIGTERM) == 0)
         {
            waitpid(pVbiBuf->vbiPid, NULL, 0);
         }
      }

      // mark the process as detached
      if (isVbiProcess == FALSE)
         pVbiBuf->epgPid = -1;
      else
         pVbiBuf->vbiPid = -1;

      // detatch and free the shared memory
      shmdt(pVbiBuf);
      if (shmctl(shmId, IPC_STAT, &stat) == 0)
      {
         if (stat.shm_nattch == 0)
         {  // the last one switches off the light
            shmctl(shmId, IPC_RMID, NULL);
         }
      }

      pVbiBuf = NULL;
   }
}

// ---------------------------------------------------------------------------
// Die if the parent is no longer alive
// - called upon VBI ring buffer overlow
//
void BtDriver_CheckParent( void )
{
   if (pVbiBuf != NULL)
   {
      if ((isVbiProcess == TRUE) && (pVbiBuf->epgPid != -1))
      {
         if ((kill(pVbiBuf->epgPid, 0) == -1) && (errno == ESRCH))
         {  // process no longer exists -> kill acquisition
            debug1("parent is dead - terminate acq pid %d", pVbiBuf->vbiPid);
            acqShouldExit = TRUE;
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Decode all VBI lines of the last seen frame
//
static void BtDriver_DecodeFrame( void )
{
   uchar data[VBI_BPF], *pData;
   slong stat;
   uint  line;

   stat = read(vbi_fdin, data, VBI_BPF);

   if ( stat > 0 )
   {
      pData = data;
      for (line=0; line < stat/VBI_BPL; line++)
      {
         VbiDecodeLine(pData, line, pVbiBuf->isEpgScan);
         pData += VBI_BPL;
      }
   }
   else if ((stat < 0) && (errno != EINTR) && (errno != EAGAIN))
   {
      debug0("vbi decode: read");
   }
}

// ---------------------------------------------------------------------------
// VBI decoder main loop
//
static void BtDriver_Main( void )
{
   char devName[DEV_MAX_NAME_LEN];
   struct timeval tv;
   FILE *fp;

   while (acqShouldExit == FALSE)
   {
      if (freeDevice)
      {  // hang-up signal received -> switch acq off
         pVbiBuf->isEnabled = FALSE;
         freeDevice = FALSE;
      }

      if (pVbiBuf->isEnabled)
      {
         if ((vbiCardIndex != pVbiBuf->cardIndex) && (vbi_fdin != -1))
         {
            sprintf(devName, PIDFILENAME, vbiCardIndex);
            unlink(devName);

            close(vbi_fdin);
            vbi_fdin = -1;
         }
         if (vbi_fdin == -1)
         {  // acq was switched on -> open device
            vbiCardIndex = pVbiBuf->cardIndex;
            sprintf(devName, VBINAME "%u", vbiCardIndex);
            vbi_fdin = open(devName, O_RDONLY);
            if (vbi_fdin == -1)
            {
               debug2("VBI open %s failed: errno=%d", devName, errno);
               pVbiBuf->isEnabled = FALSE;
            }
            else
            {  // open successful -> write pid in file
               sprintf(devName, PIDFILENAME, vbiCardIndex);
               fp = fopen(devName, "w");
               if (fp != NULL)
               {
                  fprintf(fp, "%d", pVbiBuf->vbiPid);
                  fclose(fp);
               }
            }
            kill(pVbiBuf->epgPid, SIGUSR1);
         }
         else
            BtDriver_DecodeFrame();
      }
      else
      {
         if (vbi_fdin != -1)
         {  // acq was switched off -> close device
            sprintf(devName, PIDFILENAME, vbiCardIndex);
            unlink(devName);

            close(vbi_fdin);
            vbi_fdin = -1;
         }
         // sleep until signal; check parent every 30 secs
         tv.tv_sec = 30;
         tv.tv_usec = 0;
         select(0, NULL, NULL, NULL, &tv);
         BtDriver_CheckParent();
      }
   }

   if (vbi_fdin != -1)
   {
      sprintf(devName, PIDFILENAME, vbiCardIndex);
      unlink(devName);

      close(vbi_fdin);
      vbi_fdin = -1;
   }
}

// ---------------------------------------------------------------------------
// Create the VBI slave process - also slave main loop
//
bool BtDriver_Init( void )
{
   struct timeval tv;
   int dbTaskPid;

   pVbiBuf = NULL;
   isVbiProcess = FALSE;
   video_fd = -1;

   shmId = shmget(IPC_PRIVATE, sizeof(EPGACQ_BUF), IPC_CREAT|IPC_EXCL|0600);
   if (shmId == -1)
   {  // failed to create a new segment with this key
      perror("shmget");
      return FALSE;
   }
   pVbiBuf = shmat(shmId, NULL, 0);
   if (pVbiBuf == (EPGACQ_BUF *) -1)
   {
      perror("shmat");
      return FALSE;
   }

   memset(pVbiBuf, 0, sizeof(EPGACQ_BUF));
   pVbiBuf->epgPid = getpid();

   recvWakeUpSig = FALSE;
   signal(SIGHUP,  SIG_IGN);
   signal(SIGUSR1, BtDriver_SignalWakeUp);
   dbTaskPid = fork();
   if (dbTaskPid == -1)
   {
      perror("fork");
      return FALSE;
   }
   else if (dbTaskPid != 0)
   {  // parent
      // wait until child is ready
      pVbiBuf->vbiPid = dbTaskPid;
      if (recvWakeUpSig == FALSE)
      {
         tv.tv_sec = 1;
         tv.tv_usec = 0;
         select(0, NULL, NULL, NULL, &tv);
      }
      // slightly lower priority to relatively raise prio of the acq process
      setpriority(PRIO_PROCESS, getpid(), getpriority(PRIO_PROCESS, getpid()) + 1);
      // install handler for HUP signal - notification to free vbi device
      return recvWakeUpSig;
   }
   else
   {
      isVbiProcess = TRUE;
      pVbiBuf->vbiPid = getpid();
      signal(SIGINT,  BtDriver_SignalHandler);
      signal(SIGTERM, BtDriver_SignalHandler);
      signal(SIGQUIT, BtDriver_SignalHandler);
      signal(SIGHUP,  BtDriver_SignalHangup);

      vbi_fdin = -1;
      acqShouldExit = FALSE;
      freeDevice = FALSE;

      // notify parent that child is ready
      kill(pVbiBuf->epgPid, SIGUSR1);

      // enter main loop
      BtDriver_Main();

      BtDriver_Exit();
      exit(0);
   }
}

