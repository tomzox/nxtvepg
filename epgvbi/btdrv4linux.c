/*
 *  VBI driver interface for Linux and NetBSD
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
 *    a slave process; the main reason is that it has to work under
 *    real-time conditions, i.e. every 20 ms it *must* get the CPU
 *    to read in one frame's worth of VBI lines. Also, in earlier
 *    driver/kernel versions select(2) did not work on /dev/vbi
 *    so that multiplexing with other tasks like GUI was impossible.
 *
 *    Master and slave process communicate through shared memory.
 *    It contains control parameters that allow to pass commands
 *    like acquisition on/off, plus a ring buffer for teletext
 *    packets. This buffer is managed in the epgdbacq module. When
 *    you read the code in the module, always remember that some
 *    of it is executed in the master process, some in the slave.
 *
 *    Support for NetBSD required some awkward hacking, since the
 *    bktr driver supports VBI capturing only while the video device
 *    is open. Also, for other formats than PAL the VBI device
 *    has to be opened *before* the video device. This port attempts
 *    to work around these shortcomings, however there are some
 *    cases where the acquisition will stall. For more info ask
 *    Mario or complain directly to the bktr authors.
 *
 *
 *  Authors:
 *
 *    Linux:  Tom Zoerner
 *    NetBSD: Mario Kemper <magick@bundy.zhadum.de>
 *
 *  $Id: btdrv4linux.c,v 1.21 2002/02/16 18:29:51 tom Exp tom $
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
#include <sys/wait.h>

#ifdef USE_THREADS
# include <pthread.h>
#endif

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/btdrv.h"

#ifdef __NetBSD__
# include <sys/mman.h>
# include <dev/ic/bt8xx.h>
# define VBI_MAXLINES 19
# define VIDIOCSFREQ    TVTUNER_SETFREQ
# define VIDIOCGFREQ    TVTUNER_GETFREQ
# define VIDIOCGTUNER   TVTUNER_GETSTATUS
#else
# include "linux/videodev.h"
# define VBI_MAXLINES 16
#endif

#define VBINAME       "/dev/vbi"
#ifndef __NetBSD__
# define VIDEONAME    "/dev/video"
# define TUNERNAME    VIDEONAME
# define BASE_VIDIOCPRIVATE      192
# define BTTV_VERSION            _IOR('v' , BASE_VIDIOCPRIVATE+6, int)
# define BTTV_VBISIZE            _IOR('v' , BASE_VIDIOCPRIVATE+8, int)
#else
# define VIDEONAME    "/dev/bktr"
# define TUNERNAME    "/dev/tuner"
# define MAX_CARDS    4            // max number of supported cards
# define MAX_INPUTS   4            // max number of supported inputs
#endif
#define PIDFILENAME   "/tmp/.vbi%u.pid"

#define VBI_LINENUM VBI_MAXLINES
#define VBI_BPL     2048
#define VBI_BPF     (VBI_LINENUM*2*VBI_BPL)

#define DEV_MAX_NAME_LEN 32

EPGACQ_BUF *pVbiBuf;

#ifndef USE_THREADS
// vars used in both processes
static bool isVbiProcess;
static int  shmId;
#else
static pthread_t  vbi_thread_id;
#endif

// vars used in the acq slave process
static bool acqShouldExit;
static bool freeDevice;
static int vbiCardIndex;
static int vbi_fdin;
static int bufSize;
static uchar *rawbuf = NULL;

// vars used in the control process
static bool recvWakeUpSig;
static int video_fd = -1;

#ifdef __NetBSD__
static int tuner_fd = -1;
static int vbiInputIndex;
static unsigned char *buffer;
#endif //__NetBSD__


static void * BtDriver_Main( void * foo );
static void BtDriver_OpenVbiBuf( void );

#ifdef __NetBSD__
// ---------------------------------------------------------------------------
// This function is called by the slave and the master
// The master calls with master==TRUE, the slave with slave==true
// If in master mode, this function scans all devices that are not inUse
// In slave mode it only scans the card which is inUse

void BtDriver_ScanDevices( bool master )
{
  int fd;
  int i,j;
  int input_id;
  char *input_name;
  char devName[DEV_MAX_NAME_LEN];
  
  for (i=0;i<MAX_CARDS;i++) {
    if (master) {
      if (pVbiBuf->tv_cards[i].inUse)
	continue;
    }
    else //slave
      if (!pVbiBuf->tv_cards[i].inUse) {
	continue;
      }
    sprintf(devName, VIDEONAME "%u", i);
    fd=open(devName,O_RDONLY);
    if (fd!=-1) {
      strcpy(pVbiBuf->tv_cards[i].name,devName);
      pVbiBuf->tv_cards[i].isAvailable=1;
      pVbiBuf->tv_cards[i].isBusy=0;
      for (j=0;j<MAX_INPUTS;j++) {
        switch (j) {
        case 0: //i map 0 to tuner
          input_id=METEOR_DEV1;
          input_name ="tuner";
          break;
        case 1:
          input_id=METEOR_DEV0;
          input_name="video";
          break;
        case 2:
          input_id=METEOR_DEV_SVIDEO;
          input_name="svideo";
          break;
        case 3:
          input_id=METEOR_DEV2;
          input_name ="csvideo";
          break;
        }
        if (ioctl(fd,METEORSINPUT,&input_id)==0) {
          pVbiBuf->tv_cards[i].inputs[j].inputID=input_id;
          pVbiBuf->tv_cards[i].inputs[j].isTuner=(input_id==METEOR_DEV1);
          strcpy(pVbiBuf->tv_cards[i].inputs[j].name,input_name);
          pVbiBuf->tv_cards[i].inputs[j].isAvailable=1;
        }
        else
          pVbiBuf->tv_cards[i].inputs[j].isAvailable=0;
      }
      close(fd);
    }
    else {
      if (errno==EBUSY) {
        sprintf(pVbiBuf->tv_cards[i].name,"%s (busy)",devName);
        pVbiBuf->tv_cards[i].isAvailable=1;
        pVbiBuf->tv_cards[i].isBusy=1;
      }
      else {
        pVbiBuf->tv_cards[i].isAvailable=0;
      }
    }
  }
}
#endif  //__NetBSD__

// ---------------------------------------------------------------------------
// Obtain the PID of the process which holds the VBI device
// - used by the master process/thread (to kill the acq daemon)
// - cannot use shared memory, because the daemon shares no mem with GUI
//
int BtDriver_GetDeviceOwnerPid( void )
{
   char devName[DEV_MAX_NAME_LEN];
   FILE *fp;
   int  pid = -1;

   if (pVbiBuf != NULL)
   {
      // open successful -> write pid in file
      sprintf(devName, PIDFILENAME, pVbiBuf->cardIndex);
      fp = fopen(devName, "r");
      if (fp != NULL)
      {
         if (fscanf(fp, "%d", &pid) != 1)
         {
            debug0("BtDriver-GetDeviceOwnerPid: pid file parse error");
            pid = -1;
         }
         fclose(fp);
      }
   }

   return pid;
}

// ---------------------------------------------------------------------------
// Open the VBI device
//
static void BtDriver_OpenVbi( void )
{
   char devName[DEV_MAX_NAME_LEN];
   FILE *fp;

   vbiCardIndex = pVbiBuf->cardIndex;
   #ifdef __NetBSD__
   vbiInputIndex = pVbiBuf->inputIndex;
   pVbiBuf->tv_cards[pVbiBuf->cardIndex].inUse=TRUE;
   BtDriver_ScanDevices(FALSE);
   if (BtDriver_StartCapture())
   #endif
   {
      sprintf(devName, VBINAME "%u", vbiCardIndex);
      vbi_fdin = open(devName, O_RDONLY);
   }
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
         #ifndef USE_THREADS
         fprintf(fp, "%d", pVbiBuf->vbiPid);
         #else
         fprintf(fp, "%d", pVbiBuf->epgPid);
         #endif
         fclose(fp);
      }
      // allocate memory for the VBI data buffer
      BtDriver_OpenVbiBuf();
   }
   #ifndef USE_THREADS
   // notify the parent that the operation is completed
   kill(pVbiBuf->epgPid, SIGUSR1);
   #endif
}

// ---------------------------------------------------------------------------
// Close the VBI device
//
static void BtDriver_CloseVbi( void )
{
   char devName[DEV_MAX_NAME_LEN];

   if (vbi_fdin != -1)
   {
      sprintf(devName, PIDFILENAME, vbiCardIndex);
      unlink(devName);

      close(vbi_fdin);
      vbi_fdin = -1;
      xfree(rawbuf);
      rawbuf = NULL;

      #ifdef __NetBSD__
      if (video_fd != -1)
      {
         close(video_fd);
         video_fd = -1;
         pVbiBuf->tv_cards[pVbiBuf->cardIndex].inUse = FALSE;
      }
      #endif //__NetBSD__
   }
}

// ---------------------------------------------------------------------------
// Close video device (in the master process/thread)
//
void BtDriver_CloseDevice( void )
{
   #ifndef __NetBSD__
   if (video_fd != -1)
   {
      close(video_fd);
      video_fd = -1;
   }
   #else //  __NetBSD__
   if (tuner_fd != -1)
   {
      close(tuner_fd);
      tuner_fd = -1;
   }
   #endif // __NetBSD__
}

// ---------------------------------------------------------------------------
// Change the video input source
//
bool BtDriver_SetInputSource( int inputIdx, bool keepOpen, bool * pIsTuner )
{
#ifndef __NetBSD__
   char devName[DEV_MAX_NAME_LEN];
   bool isTuner = FALSE;
   bool result = FALSE;
   struct video_channel vchan;
   struct video_tuner vtuner;

   if (video_fd == -1)
   {
      sprintf(devName, VIDEONAME "%u", pVbiBuf->cardIndex);
      video_fd = open(devName, O_RDONLY);
      dprintf1("BtDriver-SetInputSource: opened video device, fd=%d\n", video_fd);
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
         // XXX don't really know why I have to set the wrong norm first
         // XXX but if I set PAL here I have no VBI reception after booting
         vchan.norm = VIDEO_MODE_NTSC;
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
            {  // not a tuner -> don't need to set the frequency

               // XXX workaround continued: now set the correct norm PAL
               vchan.norm = VIDEO_MODE_PAL;
               if (ioctl(video_fd, VIDIOCSCHAN, &vchan) == 0)
               {
                  result = TRUE;
               }
               else
                  perror("VIDIOCSCHAN");
            }
         }
         else
            perror("VIDIOCSCHAN");
      }
      else
         perror("VIDIOCGCHAN");

      if ((keepOpen == FALSE) || (result == FALSE) || (isTuner == FALSE))
      {
         dprintf1("BtDriver-SetInputSource: closing video device, fd=%d\n", video_fd);
         BtDriver_CloseDevice();
      }
   }
   else
      debug1("BtDriver-SetInputSource: could not open device %s", devName);

   if (pIsTuner != NULL)
      *pIsTuner = isTuner;

   return result;

#else  // __NetBSD__
   int result = FALSE;
   int cardIndex = pVbiBuf->cardIndex;

   if ((cardIndex<MAX_CARDS) && (inputIdx<MAX_INPUTS))
     if (pVbiBuf->tv_cards[cardIndex].isAvailable)
       if (!pVbiBuf->tv_cards[cardIndex].isBusy)
         if (pVbiBuf->tv_cards[cardIndex].inputs[inputIdx].isAvailable) {
           result=TRUE;
           pVbiBuf->inputIndex=inputIdx;
           if (pVbiBuf->tv_cards[cardIndex].inputs[inputIdx].isTuner)
             *pIsTuner=TRUE;

         }

   return result;
#endif
}

// ---------------------------------------------------------------------------
// Tune a given frequency
//
bool BtDriver_TuneChannel( ulong freq, bool keepOpen )
{
#ifndef __NetBSD__
   char devName[DEV_MAX_NAME_LEN];
   bool result = FALSE;

   if (video_fd == -1)
   {
      sprintf(devName, TUNERNAME "%u", pVbiBuf->cardIndex);
      video_fd = open(devName, O_RDONLY);
      dprintf1("BtDriver-TuneChannel: opened video device, fd=%d\n", video_fd);
   }
   if (video_fd != -1)
   {
      // Set the tuner frequency
      if(ioctl(video_fd, VIDIOCSFREQ, &freq) == 0)
      {
         //printf("Vbi-TuneChannel: set to %.2f\n", (double)freq/16);

         pVbiBuf->frameSeqNo = 0;
         result = TRUE;
      }
      else
         perror("VIDIOCSFREQ");

      if (keepOpen == FALSE)
      {
         dprintf1("BtDriver-TuneChannel: closing video device, fd=%d\n", video_fd);
         BtDriver_CloseDevice();
      }
   }
#else // __NetBSD__
   char devName[DEV_MAX_NAME_LEN];
   bool result = FALSE;

   if (tuner_fd == -1)
   {
     if (!pVbiBuf->tv_cards[pVbiBuf->cardIndex].isBusy) {
       sprintf(devName, TUNERNAME "%u", pVbiBuf->cardIndex);
       tuner_fd = open(devName, O_RDONLY);
       dprintf1("BtDriver-TuneChannel: opened tuner device, fd=%d\n", tuner_fd);
     }
   }
   if (tuner_fd != -1)
   {
      // Set the tuner frequency
      if(ioctl(tuner_fd, VIDIOCSFREQ, &freq) == 0)
      {
         //printf("Vbi-TuneChannel: set to %.2f\n", (double)freq/16);

         pVbiBuf->frameSeqNo = 0;
         result = TRUE;
      }
      else
         perror("VIDIOCSFREQ");

      if (keepOpen == FALSE)
      {
         dprintf1("BtDriver-TuneChannel: closing tuner device, fd=%d\n", tuner_fd);
         BtDriver_CloseDevice();
      }
   }
#endif // __NetBSD__
   return result;
}

// ---------------------------------------------------------------------------
// Query current tuner frequency
// - The problem for Linux is that the VBI device is opened by the slave
//   process and cannot be accessed from the master. So the command must be
//   passed to the slave via IPC.
// - returns 0 in case of error
//
ulong BtDriver_QueryChannel( void )
{
   ulong freq = 0L;
#ifndef __NetBSD__
   struct timeval tv;

   if ((pVbiBuf != NULL) && (pVbiBuf->vbiPid != -1))
   {
      pVbiBuf->vbiQueryFreq = 0L;
      pVbiBuf->doQueryFreq = TRUE;
      recvWakeUpSig = FALSE;
      #ifdef USE_THREADS
      if (pthread_kill(vbi_thread_id, SIGUSR1) == 0)
      #else
      if (kill(pVbiBuf->vbiPid, SIGUSR1) != -1)
      #endif
      {
         if ((recvWakeUpSig == FALSE) && (pVbiBuf->doQueryFreq == FALSE))
         {
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            select(0, NULL, NULL, NULL, &tv);
         }
         freq = pVbiBuf->vbiQueryFreq;
      }
      pVbiBuf->doQueryFreq = FALSE;
   }
#else  // __NetBSD__
   char devName[DEV_MAX_NAME_LEN];

   if (tuner_fd == -1)
   {
      dprintf1("BtDriver-QueryChannel: opened video device, fd=%d\n", tuner_fd);
      sprintf(devName, TUNERNAME "%u", pVbiBuf->cardIndex);
      tuner_fd = open(devName, O_RDONLY);
   }
   if (tuner_fd != -1)
   {
      if (ioctl(tuner_fd, VIDIOCGFREQ, &freq) == 0)
      {
         dprintf1("BtDriver-BtDriver_QueryChannel: got %.2f\n", (double)freq/16);
      }
      else
         perror("VIDIOCGFREQ");

      dprintf1("BtDriver-QueryChannel: closing video device, fd=%d\n", tuner_fd);
      BtDriver_CloseDevice();
   }
#endif

   return freq;
}

// ---------------------------------------------------------------------------
// Get signal strength on current tuner frequency
//
bool BtDriver_IsVideoPresent( void )
{
#ifndef __NetBSD__
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

#else  // __NetBSD__
   /* disabled since it does not work [tom]
   bool result = FALSE;
   unsigned long status;
   if (tuner_fd != -1)
     if ((ioctl(tuner_fd,TVTUNER_GETSTATUS, &status))==0)
       result=((status &(0x40))!=0);

   return result;
   */
   return TRUE;
#endif
}

// ---------------------------------------------------------------------------
// Return name for given TV card
//
const char * BtDriver_GetCardName( uint cardIndex )
{
#ifndef __NetBSD__
   const char * pName = NULL;
   struct video_capability vcapab;
   char devName[DEV_MAX_NAME_LEN];
   #define MAX_CARD_NAME_LEN 32
   static char name[MAX_CARD_NAME_LEN + 1];

   if (video_fd != -1)
   {
      dprintf1("BtDriver-GetCardName: closing video device, fd=%d\n", video_fd);
      BtDriver_CloseDevice();
   }
   sprintf(devName, VIDEONAME "%u", cardIndex);
   video_fd = open(devName, O_RDONLY);

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

#else  // __NetBSD__
   char *pName=NULL;

   if (cardIndex<MAX_CARDS)
     if (pVbiBuf->tv_cards[cardIndex].isAvailable)
       pName=pVbiBuf->tv_cards[cardIndex].name;

   return pName;

#endif
}

// ---------------------------------------------------------------------------
// Return name for given input source index
// - has to be called repeatedly with incremented indices until NULL is returned
// - video device is kept open inbetween calls and only closed upon final call
//
const char * BtDriver_GetInputName( uint cardIndex, uint inputIdx )
{
#ifndef __NetBSD__
   struct video_capability vcapab;
   struct video_channel vchan;
   char devName[DEV_MAX_NAME_LEN];
   const char * pName = NULL;
   #define MAX_INPUT_NAME_LEN 32
   static char name[MAX_INPUT_NAME_LEN + 1];

   if (video_fd == -1)
   {
      dprintf1("BtDriver-GetInputName: opened video device, fd=%d\n", video_fd);
      sprintf(devName, VIDEONAME "%u", cardIndex);
      video_fd = open(devName, O_RDONLY);
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
      dprintf1("BtDriver-GetInputName: closing video device, fd=%d\n", video_fd);
      BtDriver_CloseDevice();
   }

   return pName;

#else  // __NetBSD__
   char *pName = NULL;
   if ((cardIndex<MAX_CARDS) && (inputIdx<MAX_INPUTS))
     if (pVbiBuf->tv_cards[cardIndex].isAvailable)
       if (!pVbiBuf->tv_cards[cardIndex].isBusy)
         if (pVbiBuf->tv_cards[cardIndex].inputs[inputIdx].isAvailable)
           pName =  pVbiBuf->tv_cards[cardIndex].inputs[inputIdx].name;

   return pName;

#endif
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
      fd = open(devName, O_RDONLY);
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
#ifndef USE_THREADS
static void BtDriver_SignalHangup( int sigval )
{
   if (pVbiBuf != NULL)
   {
      if (pVbiBuf->isEnabled)
      {  // stop acquisition

         if (vbi_fdin != -1)
         {
            freeDevice = TRUE;
         }
      }
      else
      {  // start acquisition

         if (isVbiProcess && (pVbiBuf->epgPid != -1))
         {  // just pass the signal through to the master process
            kill(pVbiBuf->epgPid, SIGHUP);
         }
      }
   }
   signal(sigval, BtDriver_SignalHangup);
}
#endif

// ---------------------------------------------------------------------------
// Check upon the acq slave after being signaled for death of a child
//
#ifndef USE_THREADS
static void BtDriver_SignalDeathOfChild( int sigval )
{
   pid_t pid;
   int   status;

   assert(isVbiProcess == FALSE);

   pid = waitpid(-1, &status, WNOHANG);
   if ((pid != -1) && (pid != 0) && (WIFEXITED(status) || WIFSIGNALED(status)))
   {
      if (pVbiBuf != NULL)
      {
         if (pid == pVbiBuf->vbiPid)
         {  // slave died without catching the signal (e.g. SIGKILL)
            debug1("BtDriver-SignalDeathOfChild: acq slave pid %d crashed - disable acq", pid);
            pVbiBuf->vbiPid = -1;
            pVbiBuf->isEnabled = FALSE;
         }
         else if ((pVbiBuf->vbiPid == -1) && pVbiBuf->isEnabled)
         {  // slave caught deadly signal and cleared his pid already
            debug1("BtDriver-SignalDeathOfChild: acq slave %d terminated - disable acq", pid);
            pVbiBuf->isEnabled = FALSE;
         }
         else
            dprintf2("BtDriver-SignalDeathOfChild: pid %d: %s\n", pid, (pVbiBuf->isEnabled ? "not the VBI slave" : "acq already disabled"));
      }
      else
         debug0("BtDriver-SignalDeathOfChild: no VbiBuf allocated");
   }
   else
      dprintf2("BtDriver-SignalDeathOfChild: nothing to do: wait returned pid %d, flags 0x%02X\n", pid, status);

   signal(sigval, BtDriver_SignalDeathOfChild);
}
#endif  // USE_THREADS

// ---------------------------------------------------------------------------
// If one tries to read from /dev/vbi while theres no video capturing, 
// the read() will block forever. Therefore it's surrounded by an alarm()
// This SignalHandler will close /dev/vbi in this case and thus forces a 
// reopening of both devices, as a read(2) will not exit when a signal 
// arrives but simply restarted.  
//
#ifdef __NetBSD__
static void BtDriver_SignalAlarm( int sigval )
{
  if (vbi_fdin!=-1) {
    close(vbi_fdin);
    vbi_fdin=-1;
    xfree(rawbuf);
    rawbuf = NULL;
  }
  if (video_fd!=-1) {
    close(video_fd);
    video_fd=-1;
  }
  signal(sigval, BtDriver_SignalAlarm);
}
#endif  //__NetBSD__

// ---------------------------------------------------------------------------
// Wake-up the acq child process to start acquisition
// - the child signals back after it completed the operation
// - the status of the operation is in the isEnabled flag
//
bool BtDriver_StartAcq( void )
{
   bool result = FALSE;
#ifdef USE_THREADS
   sigset_t sigmask;

   if (pthread_create(&vbi_thread_id, NULL, BtDriver_Main, NULL) == 0)
   {
      sigemptyset(&sigmask);
      sigaddset(&sigmask, SIGUSR1);
      pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

      result = TRUE;
   }
   else
      perror("pthread_create");

#else
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
#endif
   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition
//
void BtDriver_StopAcq( void )
{
#ifdef USE_THREADS
   if (pVbiBuf->vbiPid != -1)
   {
      acqShouldExit = TRUE;
      pthread_kill(vbi_thread_id, SIGUSR1);
      if (pthread_join(vbi_thread_id, NULL) != 0)
         perror("pthread_join");
   }
#endif
}

// ---------------------------------------------------------------------------
// Terminate the acquisition process
//
void BtDriver_Exit( void )
{
#ifndef USE_THREADS
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
#else  // USE_THREADS
   if (pVbiBuf != NULL)
   {
      xfree(pVbiBuf);
      pVbiBuf = NULL;
   }
#endif
}

// ---------------------------------------------------------------------------
// Die if the parent is no longer alive
// - called upon VBI ring buffer overlow
//
void BtDriver_CheckParent( void )
{
#ifndef USE_THREADS
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
#endif
}

// ---------------------------------------------------------------------------
// Determine the size of the VBI buffer
// - the buffer size depends on the number of VBI lines that are captured
//   for each frame; each read on the vbi device should request all the lines
//   of one frame, else the rest will be overwritten by the next frame
//
static void BtDriver_OpenVbiBuf( void )
{
   #ifndef __NetBSD__
   bufSize = ioctl(vbi_fdin, BTTV_VBISIZE);
   if (bufSize == -1)
   {
      perror("ioctl BTTV_VBISIZE");
      bufSize = VBI_BPF;
   }
   else if ((bufSize > VBI_BPL*100) || ((bufSize % VBI_BPL) != 0))
   {
      fprintf(stderr, "BTTV_VBISIZE: illegal buffer size %d\n", bufSize);
      bufSize = VBI_BPF;
   }

   #else  // __NetBSD__
   bufSize = VBI_BPF;
   #endif

   rawbuf = xmalloc(bufSize);

   pVbiBuf->frameSeqNo = 0;
}

// ---------------------------------------------------------------------------
// Decode all VBI lines of the last seen frame
//
static void BtDriver_DecodeFrame( void )
{
   uchar *pData;
   slong stat;
   uint  line;

   #ifdef __NetBSD__
   // wait 10 seconds for the read to complete. After this time, close
   // dev/vbi in the signal handler, avoiding endless blocking
   alarm(10);
   #endif

   stat = read(vbi_fdin, rawbuf, bufSize);

   #ifdef __NetBSD__
   alarm(0);
   #endif

   if ( stat >= VBI_BPL )
   {
      #ifndef __NetBSD__
      // retrieve frame sequence counter from the end of the buffer
      u32 seqno = *(u32 *)(rawbuf + stat - 4);
      if ((seqno != pVbiBuf->frameSeqNo + 1) && (pVbiBuf->frameSeqNo != 0))
      {  // report mising frame to the teletext decoder
         VbiDecodeLostFrame();
      }
      #else
      u32 seqno = 1;
      #endif

      // skip the first frame, since it could contain data from the previous channel
      if (pVbiBuf->frameSeqNo > 0)
      {
         pData = rawbuf;
         for (line=0; line < stat/VBI_BPL; line++)
         {
            VbiDecodeLine(pData, line, pVbiBuf->doVpsPdc);
            pData += VBI_BPL;
            //printf("%02d: %08lx\n", line, *((ulong*)pData-4));  /* frame counter */
         }
      }
      else
      {  // record which is the first valid line after the channel change
         // the reader index will be set here by the master process/thread
         pVbiBuf->start_writer_idx = pVbiBuf->writer_idx;
      }
      pVbiBuf->frameSeqNo = seqno;
   }
   else if ((stat < 0) && (errno != EINTR) && (errno != EAGAIN))
   {
      debug0("vbi decode: read");
   }
}

#ifdef __NetBSD__
// ---------------------------------------------------------------------------
// Sets up the capturing needed for NetBSD to receive vbi-data.
//
int BtDriver_StartCapture(void)
{
  char devName[DEV_MAX_NAME_LEN];
  struct meteor_geomet geo;
  int buffer_size;
  int width,height;
  struct bktr_clip clip[BT848_MAX_CLIP_NODE];
  int result=FALSE;
  int c;
  int close_fd=0;

  width=100;
  height=100;
  geo.rows = height;
  geo.columns = width;
  geo.frames = 1;
  geo.oformat = METEOR_GEO_RGB24 ;
  memset(clip,0,sizeof(clip));

  /* setup clipping */
  clip[0].x_min=0;
  clip[0].y_min=0;
  clip[0].x_max=2;
  clip[0].y_max=2;
  clip[1].y_min=0;
  clip[1].y_max=0;
  clip[1].x_min=0;
  clip[1].x_max=0;

  if (video_fd==-1) {
    sprintf(devName, VIDEONAME, vbiCardIndex);
    video_fd=open(devName,O_RDONLY);
    if (video_fd==-1) {//device opened by someone else
      dprintf1("BtDriver-StartCapture: could not open device %s\n", devName);
      result=TRUE;
    }
  }

  if (video_fd!=-1) {
    if (ioctl(video_fd, METEORSETGEO, &geo) == 0) {
      if (ioctl(video_fd, BT848SCLIP, &clip) == 0) {
        c = METEOR_FMT_PAL;
        if (ioctl(video_fd, METEORSFMT, &c) == 0) {
          switch (vbiInputIndex) {
          case 0:
            c = METEOR_INPUT_DEV1;
            break;
          case 1:
            c = METEOR_INPUT_DEV0;
            break;
          case 2:
            c=METEOR_DEV_SVIDEO;
            break;
          case 3:
            c=METEOR_DEV2;
            break;
          }
          if (ioctl(video_fd, METEORSINPUT, &c) == 0) {
            buffer_size = width*height*4;   /* R,G,B,spare */
            buffer = (unsigned char *)mmap((caddr_t)0,buffer_size,PROT_READ,
                                           MAP_SHARED, video_fd, (off_t)0);
            if (buffer != (unsigned char *) MAP_FAILED) {
              c = METEOR_CAP_CONTINOUS;
              if (ioctl(video_fd, METEORCAPTUR, &c)==0)
                result=TRUE;
              else
                perror("METEORCAPTUR");
            }
            else
              perror("mmap");
          }
          else
            perror("METEOROSINPUT");
        }
        else
          perror("METEORSFMT");
      }
      else
        perror("BT848SCLIP");

    }
    else
      perror("METERORSETGEO");
  }

  if ((result==FALSE)&&(video_fd!=0)) {
    close(video_fd);
    video_fd=-1;
  }
  else {
    c=CHNLSET_WEUROPE;
    if (tuner_fd==-1) {
      tuner_fd=open("/dev/tuner0",O_RDONLY);
      close_fd=1;
    }

    ioctl(tuner_fd, TVTUNER_SETTYPE, &c);
    if (close_fd) {
      close(tuner_fd);
      tuner_fd=-1;
    }
  }

  return result;
}
#endif //__NetBSD__

// ---------------------------------------------------------------------------
// VBI decoder main loop
//
static void * BtDriver_Main( void * foo )
{
   #ifdef USE_THREADS
   sigset_t sigmask;
   #else
   struct timeval tv;
   #endif

   pVbiBuf->vbiPid = getpid();
   vbi_fdin = -1;
   #ifdef __NetBSD__
   video_fd = -1;
   #endif

   acqShouldExit = FALSE;
   freeDevice = FALSE;
   #ifdef USE_THREADS
   sigemptyset(&sigmask);
   sigaddset(&sigmask, SIGHUP);
   pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
   sigemptyset(&sigmask);
   sigaddset(&sigmask, SIGUSR1);
   pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL);
   signal(SIGUSR1, BtDriver_SignalWakeUp);
   #else
   // notify parent that child is ready
   kill(pVbiBuf->epgPid, SIGUSR1);
   #endif

   while (acqShouldExit == FALSE)
   {
      if (pVbiBuf->isEnabled && (freeDevice == FALSE))
      {
         #ifndef __NetBSD__
         if ((vbiCardIndex != pVbiBuf->cardIndex) && (vbi_fdin != -1))
         #else
         if (((vbiCardIndex != pVbiBuf->cardIndex) && (vbi_fdin != -1)) ||
             ((vbiInputIndex != pVbiBuf->inputIndex) && (vbi_fdin != -1)))
         #endif
         {  // switching to a different device -> close the previous one
            BtDriver_CloseVbi();
         }
         if (vbi_fdin == -1)
         {  // acq was switched on -> open device
            BtDriver_OpenVbi();
         }
         else
         {  // device is open -> capture the VBI lines of this frame
            BtDriver_DecodeFrame();
         }
      }
      else
      {
         #ifndef USE_THREADS
         // acq was switched off -> close device
         BtDriver_CloseVbi();
         if (freeDevice)
         {  // hang-up signal received -> inform master thread by setting the state to disabled
            freeDevice = FALSE;
            pVbiBuf->isEnabled = FALSE;
         }

         // sleep until signal; check parent every 30 secs
         tv.tv_sec = 30;
         tv.tv_usec = 0;
         select(0, NULL, NULL, NULL, &tv);
         BtDriver_CheckParent();
         #else
         acqShouldExit = TRUE;
         #endif
      }

      if (pVbiBuf->doQueryFreq && (vbi_fdin != -1))
      {
         if (ioctl(vbi_fdin, VIDIOCGFREQ, &pVbiBuf->vbiQueryFreq) == 0)
         {
            dprintf1("BtDriver-BtDriver_QueryChannel: got %.2f\n", (double)pVbiBuf->vbiQueryFreq/16);
         }
         else
            perror("VIDIOCGFREQ");

         pVbiBuf->doQueryFreq = FALSE;
         kill(pVbiBuf->epgPid, SIGUSR1);
      }
   }

   BtDriver_CloseVbi();

   if (pVbiBuf->isEnabled)
   {  // notify the parent that acq has stopped (e.g. after SIGTERM)
      pVbiBuf->isEnabled = FALSE;
      #ifndef USE_THREADS
      kill(pVbiBuf->epgPid, SIGHUP);
      #endif
   }

   return NULL;
}

// ---------------------------------------------------------------------------
// Create the VBI slave process - also slave main loop
//
bool BtDriver_Init( void )
{
#ifndef USE_THREADS
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

   #ifdef __NetBSD__
   // scan cards and inputs
   BtDriver_ScanDevices(TRUE);
   #endif

   recvWakeUpSig = FALSE;
   signal(SIGUSR1, BtDriver_SignalWakeUp);
   signal(SIGCHLD, BtDriver_SignalDeathOfChild);

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
      // return TRUE if slave signaled USR1
      return recvWakeUpSig;
   }
   else
   {
      signal(SIGINT,  BtDriver_SignalHandler);
      signal(SIGTERM, BtDriver_SignalHandler);
      signal(SIGQUIT, BtDriver_SignalHandler);
      signal(SIGHUP,  BtDriver_SignalHangup);
      #ifdef __NetBSD__
      // install signal handler to implement read timeout on /dev/vbi
      signal(SIGALRM,  BtDriver_SignalAlarm);
      #endif

      isVbiProcess = TRUE;

      // enter main loop
      BtDriver_Main(NULL);
      BtDriver_Exit();

      exit(0);
   }

#else  //USE_THREADS
   signal(SIGUSR1, SIG_IGN);

   pVbiBuf = xmalloc(sizeof(*pVbiBuf));
   memset(pVbiBuf, 0, sizeof(EPGACQ_BUF));

   pVbiBuf->epgPid = getpid();
   pVbiBuf->vbiPid = -1;

   vbi_fdin = -1;
   video_fd = -1;

   return TRUE;
#endif  //USE_THREADS
}

