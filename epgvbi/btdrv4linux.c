/*
 *  VBI driver interface for Linux, NetBSD and FreeBSD
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
 *    Linux:   Tom Zoerner
 *    NetBSD:  Mario Kemper <magick@bundy.zhadum.de>
 *    FreeBSD: Simon Barner <barner@gmx.de>
 *
 *  $Id: btdrv4linux.c,v 1.46 2003/04/12 17:51:05 tom Exp tom $
 */

#if !defined(linux) && !defined(__NetBSD__) && !defined(__FreeBSD__) 
#error "This module is for Linux, NetBSD or FreeBSD only"
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
#include "epgvbi/syserrmsg.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/btdrv.h"

#ifdef ZVBI_DECODER
#include "epgvbi/ttxdecode.h"
#include "epgvbi/zvbidecoder.h"
static vbi_raw_decoder zvbi_rd;
#endif

#if defined(__NetBSD__) || defined(__FreeBSD__)
# include <sys/mman.h>
# ifdef __FreeBSD__
#  include <machine/ioctl_bt848.h>
#  include <machine/ioctl_meteor.h>
# else 
#  include <dev/ic/bt8xx.h>
# endif

# define VBI_DEFAULT_LINES 16
# define VIDIOCSFREQ    TVTUNER_SETFREQ
# define VIDIOCGFREQ    TVTUNER_GETFREQ
# define VIDIOCGTUNER   TVTUNER_GETSTATUS
# define VIDIOCGCHAN	TVTUNER_GETCHNL

#else  // Linux
# include "linux/videodev.h"
# define VBI_DEFAULT_LINES 16
#endif

#if !defined (__NetBSD__) && !defined (__FreeBSD__)
# define BASE_VIDIOCPRIVATE      192
# define BTTV_VERSION            _IOR('v' , BASE_VIDIOCPRIVATE+6, int)
# define BTTV_VBISIZE            _IOR('v' , BASE_VIDIOCPRIVATE+8, int)
#else
# define MAX_CARDS    4            // max number of supported cards
# define MAX_INPUTS   4            // max number of supported inputs
#endif
#define PIDFILENAME   "/tmp/.vbi%u.pid"

#define VBI_MAX_LINENUM   32         // reasonable upper limits
#define VBI_MAX_LINESIZE (8*1024)
#define VBI_DEFAULT_BPL   2048

#define DEV_MAX_NAME_LEN 32

volatile EPGACQ_BUF *pVbiBuf;

#ifndef USE_THREADS
// vars used in both processes
static bool isVbiProcess;
static int  shmId;
#else
static pthread_t        vbi_thread_id;
static pthread_cond_t   vbi_start_cond;
static pthread_mutex_t  vbi_start_mutex;
#endif

// vars used in the acq slave process
static bool acqShouldExit;
static int vbiCardIndex;
static int vbi_fdin;
static int bufLineSize;
static int bufLines;
static uchar *rawbuf = NULL;
static char *pSysErrorText = NULL;

// vars used in the control process
static bool recvWakeUpSig;
static int video_fd = -1;

#if defined(__NetBSD__) || defined(__FreeBSD__)
static int tuner_fd = -1;
static bool devKeptOpen = FALSE;
static int vbiInputIndex;
static unsigned char *buffer;
#endif //__NetBSD__ || __FreeBSD__


// function forward declarations
int BtDriver_StartCapture(void);
static void * BtDriver_Main( void * foo );
static void BtDriver_OpenVbiBuf( void );

typedef enum
{
   DEV_TYPE_VBI,
   DEV_TYPE_VIDEO,
#if defined(__NetBSD__) || defined(__FreeBSD__)
   DEV_TYPE_TUNER,
#endif
} BTDRV_DEV_TYPE;

// ---------------------------------------------------------------------------
// Get name of the specified device type
//
static char * BtDriver_GetDevicePath( BTDRV_DEV_TYPE devType, uint cardIdx )
{
   static char devName[DEV_MAX_NAME_LEN];
#if !defined(__NetBSD__) && !defined(__FreeBSD__)
   static char * pDevPath = NULL;

   if (pDevPath == NULL)
   {
      if (access("/dev/v4l", X_OK) == 0)
         pDevPath = "/dev/v4l";
      else
         pDevPath = "/dev";

      dprintf1("BtDriver-GetDevicePath: set device path %s\n", pDevPath);
   }

   switch (devType)
   {
      case DEV_TYPE_VBI:
         sprintf(devName, "%s/vbi%u", pDevPath, cardIdx);
         break;
      case DEV_TYPE_VIDEO:
         sprintf(devName, "%s/video%u", pDevPath, cardIdx);
         break;
      default:
         strcpy(devName, "/dev/null");
         fatal1("BtDriver-GetDevicePath: illegal device type %d", devType);
         break;
   }

#else  // NetBSD
   switch (devType)
   {
      case DEV_TYPE_VBI:
         sprintf(devName, "/dev/vbi%u", cardIdx);
         break;
      case DEV_TYPE_VIDEO:
         sprintf(devName, "/dev/bktr%u", cardIdx);
         break;
      case DEV_TYPE_TUNER:
         sprintf(devName, "/dev/tuner%u", cardIdx);
         break;
      default:
         strcpy(devName, "/dev/null");
         fatal1("BtDriver-GetDevicePath: illegal device type %d", devType);
         break;
   }
#endif
   return devName;
}

#if defined(__NetBSD__) || defined(__FreeBSD__)
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
  const char *pDevName;
  
  for (i=0;i<MAX_CARDS;i++) {
    if (master) {
      if (pVbiBuf->tv_cards[i].inUse)
	continue;
    }
    else //slave
      if (!pVbiBuf->tv_cards[i].inUse) {
	continue;
      }
    pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, i);
    fd=open(pDevName,O_RDONLY);
    if (fd!=-1) {
      strncpy((char*)pVbiBuf->tv_cards[i].name,pDevName, MAX_BSD_CARD_NAME_LEN);
      pVbiBuf->tv_cards[i].name[MAX_BSD_CARD_NAME_LEN - 1] = 0;
      pVbiBuf->tv_cards[i].isAvailable=1;
      pVbiBuf->tv_cards[i].isBusy=0;
      for (j=0;j<MAX_INPUTS;j++) {
        switch (j) {
        case 0: //i map 0 to tuner
          input_id=METEOR_DEV1;
          input_name = "tuner";
          break;
        case 1:
          input_id=METEOR_DEV0;
          input_name = "video";
          break;
        case 2:
          input_id=METEOR_DEV_SVIDEO;
          input_name = "svideo";
          break;
        case 3:
          input_id=METEOR_DEV2;
          input_name = "csvideo";
          break;
        }
		
        if (ioctl(fd,METEORSINPUT,&input_id)==0) {
	      pVbiBuf->tv_cards[i].inputs[j].inputID=input_id;
          pVbiBuf->tv_cards[i].inputs[j].isTuner=(input_id==METEOR_DEV1);
          strncpy((char*)pVbiBuf->tv_cards[i].inputs[j].name,input_name, MAX_BSD_CARD_NAME_LEN);
          pVbiBuf->tv_cards[i].inputs[j].name[MAX_BSD_CARD_NAME_LEN - 1] = 0;
          pVbiBuf->tv_cards[i].inputs[j].isAvailable=1;
        }
        else
          pVbiBuf->tv_cards[i].inputs[j].isAvailable=0;
      }
      close(fd);
    }
    else {
      if (errno==EBUSY) {
        sprintf((char*)pVbiBuf->tv_cards[i].name,"%s (busy)",pDevName);
        pVbiBuf->tv_cards[i].isAvailable=1;
        pVbiBuf->tv_cards[i].isBusy=1;
      }
      else {
        pVbiBuf->tv_cards[i].isAvailable=0;
      }
    }
  }
}
#endif  //__NetBSD__ || __FreeBSD__

// ---------------------------------------------------------------------------
// Obtain the PID of the process which holds the VBI device
// - used by the master process/thread (to kill the acq daemon)
// - cannot use shared memory, because the daemon shares no mem with GUI
//
int BtDriver_GetDeviceOwnerPid( void )
{
   char pDevName[DEV_MAX_NAME_LEN];
   FILE *fp;
   int  pid = -1;

   if (pVbiBuf != NULL)
   {
      // open successful -> write pid in file
      sprintf(pDevName, PIDFILENAME, pVbiBuf->cardIndex);
      fp = fopen(pDevName, "r");
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
   char * pDevName;
   char tmpName[DEV_MAX_NAME_LEN];
   FILE *fp;

   vbiCardIndex = pVbiBuf->cardIndex;
   #if defined(__NetBSD__) || defined(__FreeBSD__)
   vbiInputIndex = pVbiBuf->inputIndex;
   pVbiBuf->tv_cards[pVbiBuf->cardIndex].inUse=TRUE;
   BtDriver_ScanDevices(FALSE);
   if (BtDriver_StartCapture())
   #endif
   {
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_VBI, vbiCardIndex);
      vbi_fdin = open(pDevName, O_RDONLY);
   }
   if (vbi_fdin == -1)
   {
      debug2("VBI open %s failed: errno=%d", pDevName, errno);
      pVbiBuf->failureErrno = errno;
      pVbiBuf->hasFailed = TRUE;
   }
   else
   {  // open successful -> write pid in file
      sprintf(tmpName, PIDFILENAME, vbiCardIndex);
      fp = fopen(tmpName, "w");
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
   char tmpName[DEV_MAX_NAME_LEN];

   if (vbi_fdin != -1)
   {
      sprintf(tmpName, PIDFILENAME, vbiCardIndex);
      unlink(tmpName);

      close(vbi_fdin);
      vbi_fdin = -1;
      xfree(rawbuf);
      rawbuf = NULL;

      #if defined(__NetBSD__) || defined(__FreeBSD__)
      if (video_fd != -1)
      {
         close(video_fd);
         video_fd = -1;
         pVbiBuf->tv_cards[pVbiBuf->cardIndex].inUse = FALSE;
      }
      #endif //__NetBSD__ || __FreeBSD__
   }
}

// ---------------------------------------------------------------------------
// Close video device (in the master process/thread)
//
void BtDriver_CloseDevice( void )
{
   #if !defined (__NetBSD__) && !defined (__FreeBSD__)
   if (video_fd != -1)
   {
      close(video_fd);
      video_fd = -1;
   }
   #else //  __NetBSD__ || __FreeBSD__
   if (tuner_fd != -1)
   {
      // unmute tuner
      int mute_arg = AUDIO_UNMUTE;
      if (ioctl (tuner_fd, BT848_SAUDIO, &mute_arg) == 0)
      {
         dprintf0("Unmuted tuner audio.\n");
      }
      else 
         SystemErrorMessage_Set(&pSysErrorText, errno, "unmuting audio (ioctl AUDIO_UNMUTE): ", NULL);

      close(tuner_fd);
      tuner_fd = -1;
   }
   devKeptOpen = FALSE;
   #endif // __NetBSD__ || __FreeBSD__
}

// ---------------------------------------------------------------------------
// Change the video input source and TV norm
//
static bool BtDriver_SetInputSource( int inputIdx, int norm, bool * pIsTuner )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   bool isTuner = FALSE;
   bool result = FALSE;
   struct video_channel vchan;
   struct video_tuner vtuner;

   if (video_fd != -1)
   {
      // get current config of the selected chanel
      memset(&vchan, 0, sizeof(vchan));
      vchan.channel = inputIdx;
      if (ioctl(video_fd, VIDIOCGCHAN, &vchan) == 0)
      {  // channel index is valid

         vchan.norm    = norm;
         vchan.channel = inputIdx;

         if (ioctl(video_fd, VIDIOCSCHAN, &vchan) == 0)
         {
            if ( (vchan.type & VIDEO_TYPE_TV) && (vchan.flags & VIDEO_VC_TUNER) )
            {
               isTuner = TRUE;

               // query the settings of tuner #0
               memset(&vtuner, 0, sizeof(vtuner));
               #ifndef SAA7134_0_2_2
               if (ioctl(video_fd, VIDIOCGTUNER, &vtuner) == 0)
               {
                  if (vtuner.flags & ((norm == VIDEO_MODE_SECAM) ? VIDEO_TUNER_SECAM : VIDEO_TUNER_PAL))
                  {
                     result = TRUE;

                     // set tuner norm again (already done in CSCHAN above) - ignore errors
                     vtuner.mode = norm;
                     if (ioctl(video_fd, VIDIOCSTUNER, &vtuner) != 0)
                        debug3("BtDriver-SetInputSource: v4l ioctl VIDIOCSTUNER for %s failed: %d: %s", (norm == VIDEO_MODE_SECAM) ? "SECAM" : "PAL", errno, strerror(errno));
                  }
                  else
                     SystemErrorMessage_Set(&pSysErrorText, 0, "tuner supports no ", (norm == VIDEO_MODE_SECAM) ? "SECAM" : "PAL", NULL);
               }
               else
                  SystemErrorMessage_Set(&pSysErrorText, errno, "failed to query tuner capabilities (v4l ioctl VIDIOCGTUNER): ", NULL);

               #else
               // workaround for SAA7134 driver version 0.2.2 and assorted v4l2 kernel patch:
               // in this version vtuner.flags lacks the video norm capability flags & VIDIOCSTUNER always returns an error code
               result = TRUE;
               #endif
            }
            else
            {  // not a tuner -> don't need to set the frequency
               result = TRUE;
            }
         }
         else
            SystemErrorMessage_Set(&pSysErrorText, errno, "failed to set input channel (v4l ioctl VIDIOCSCHAN): ", NULL);
      }
      else
         SystemErrorMessage_Set(&pSysErrorText, errno, "failed to query channel capabilities (v4l ioctl VIDIOCGCHAN): ", NULL);
   }
   else
      fatal0("BtDriver-SetInputSource: device not open");

   if (pIsTuner != NULL)
      *pIsTuner = isTuner;

   return result;

#else  // __NetBSD__
   int result = FALSE;
   int cardIndex = pVbiBuf->cardIndex;

   // XXX TODO: need to set TV norm
   if ((cardIndex<MAX_CARDS) && (inputIdx<MAX_INPUTS)) {
     if (pVbiBuf->tv_cards[cardIndex].isAvailable) {
       if (!pVbiBuf->tv_cards[cardIndex].isBusy) {
         if (pVbiBuf->tv_cards[cardIndex].inputs[inputIdx].isAvailable) {
           result=TRUE;
           pVbiBuf->inputIndex=inputIdx;
           if (pVbiBuf->tv_cards[cardIndex].inputs[inputIdx].isTuner) {
             *pIsTuner=TRUE;
           }
         }
       }
     }
   }

   return result;
#endif
}

// ---------------------------------------------------------------------------
// Set the input channel and tune a given frequency and norm
// - input source is only set upon the first call when the device is kept open
//   also note that the isTuner flag is only set upon the first call
//
bool BtDriver_TuneChannel( int inputIdx, uint freq, bool keepOpen, bool * pIsTuner )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   const char * pDevName;
   ulong lfreq;
   uint  norm;
   bool wasOpen;
   bool result = FALSE;

   norm  = freq >> 24;
   lfreq = freq & 0xffffff;

   if (video_fd == -1)
   {
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, pVbiBuf->cardIndex);
      video_fd = open(pDevName, O_RDONLY);
      dprintf1("BtDriver-TuneChannel: opened video device, fd=%d\n", video_fd);
      wasOpen = FALSE;

      if (video_fd == -1)
      {
         if (errno == EBUSY)
            SystemErrorMessage_Set(&pSysErrorText, 0, "video input device ", pDevName, " is busy (-> close all video apps)", NULL);
         else
            SystemErrorMessage_Set(&pSysErrorText, errno, "failed to open ", pDevName, ": ", NULL);
      }
   }
   else
      wasOpen = TRUE;

   if (video_fd != -1)
   {
      if ( wasOpen || BtDriver_SetInputSource(inputIdx, norm, pIsTuner) )
      {
         if ( (wasOpen || *pIsTuner) && (lfreq != 0) )
         {
            // Set the tuner frequency
            if (ioctl(video_fd, VIDIOCSFREQ, &lfreq) == 0)
            {
               dprintf1("BtDriver-TuneChannel: set to %.2f\n", (double)lfreq/16);

               result = TRUE;
            }
            else
               SystemErrorMessage_Set(&pSysErrorText, errno, "failed to set tuner frequency (v4l ioctl VIDIOCSFREQ): ", NULL);
         }
         else
            result = TRUE;
      }

      if ((keepOpen == FALSE) || (result == FALSE))
      {
         dprintf1("BtDriver-TuneChannel: closing video device, fd=%d\n", video_fd);
         BtDriver_CloseDevice();
      }
   }

#else // __NetBSD__ || __FreeBSD__
   char * pDevName;
   ulong lfreq;
   uint  norm;
   bool result = FALSE;

   norm  = freq >> 24;
   lfreq = freq & 0xffffff;

   if (devKeptOpen || BtDriver_SetInputSource(inputIdx, norm, pIsTuner))
   {
      if ( (devKeptOpen || *pIsTuner) && (lfreq != 0) )
      {
         if (tuner_fd == -1)
         {
           assert(devKeptOpen == FALSE);
           pDevName = BtDriver_GetDevicePath(DEV_TYPE_TUNER, pVbiBuf->cardIndex);
           if (!pVbiBuf->tv_cards[pVbiBuf->cardIndex].isBusy) {
             tuner_fd = open(pDevName, O_RDONLY);
             if (tuner_fd == -1)
               SystemErrorMessage_Set(&pSysErrorText, errno, "open tuner device  ", pDevName, ": ", NULL);
             else
               dprintf1("BtDriver-TuneChannel: opened tuner device, fd=%d\n", tuner_fd);
           }
           else
             SystemErrorMessage_Set(&pSysErrorText, 0, "tuner device ", pDevName, " is busy (-> close other video apps)", NULL);
         }

         if (tuner_fd != -1)
         {
            // mute audio
            int mute_arg = AUDIO_MUTE;
            if (ioctl (tuner_fd, BT848_SAUDIO, &mute_arg) == 0)
            {
               dprintf0("Muted tuner audio.\n");
            }
            else
               SystemErrorMessage_Set(&pSysErrorText, errno, "muting audio (ioctl AUDIO_MUTE)", NULL);

            // Set the tuner frequency
            if(ioctl(tuner_fd, VIDIOCSFREQ, &lfreq) == 0)
            {
               dprintf1("Vbi-TuneChannel: set to %.2f\n", (double)freq/16);

               result = TRUE;
            }
            else
               SystemErrorMessage_Set(&pSysErrorText, errno, "setting tuner frequency (ioctl VIDIOCSFREQ): ", NULL);

            if (keepOpen == FALSE)
            {
               dprintf1("BtDriver-TuneChannel: closing tuner device, fd=%d\n", tuner_fd);
               BtDriver_CloseDevice();
            }
            else
               devKeptOpen = TRUE;
			   
         }
      }
   }
#endif // __NetBSD__ || __FreeBSD__
   return result;
}

// ---------------------------------------------------------------------------
// Query current tuner frequency
// - The problem for Linux is that the VBI device is opened by the slave
//   process and cannot be accessed from the master. So the command must be
//   passed to the slave via IPC.
// - returns 0 in case of error
//
uint BtDriver_QueryChannel( void )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   struct timeval tv;
   uint freq = 0;

   if ((pVbiBuf != NULL) && (pVbiBuf->vbiPid != -1))
   {
      pVbiBuf->vbiQueryFreq = 0;
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
   return freq;

#else  // __NetBSD__ || FreeBSD
   char * pDevName;
   ulong lfreq = 0L;

   if (tuner_fd == -1)
   {
      dprintf1("BtDriver-QueryChannel: opened video device, fd=%d\n", tuner_fd);
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_TUNER, pVbiBuf->cardIndex);
      tuner_fd = open(pDevName, O_RDONLY);
   }
   if (tuner_fd != -1)
   {
      if (ioctl(tuner_fd, VIDIOCGFREQ, &lfreq) == 0)
      {
         dprintf1("BtDriver-QueryChannel: got %.2f\n", (double)lfreq/16);
      }
      else
         perror("VIDIOCGFREQ");

      dprintf1("BtDriver-QueryChannel: closing video device, fd=%d\n", tuner_fd);
      BtDriver_CloseDevice();
   }

   return (uint) lfreq;
#endif
}

// ---------------------------------------------------------------------------
// Get signal strength on current tuner frequency
//
bool BtDriver_IsVideoPresent( void )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
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
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   const char * pName = NULL;
   struct video_capability vcapab;
   char * pDevName;
   #define MAX_CARD_NAME_LEN 32
   static char name[MAX_CARD_NAME_LEN + 1];

   if (video_fd != -1)
   {
      dprintf1("BtDriver-GetCardName: closing video device, fd=%d\n", video_fd);
      BtDriver_CloseDevice();
   }
   pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, cardIndex);
   video_fd = open(pDevName, O_RDONLY);

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

#else  // __NetBSD__ || __FreeBSD__
   char *pName=NULL;

   if (cardIndex<MAX_CARDS)
     if (pVbiBuf->tv_cards[cardIndex].isAvailable)
       pName=(char*)pVbiBuf->tv_cards[cardIndex].name;

   return pName;

#endif
}

// ---------------------------------------------------------------------------
// Return name for given input source index
// - has to be called repeatedly with incremented indices until NULL is returned
// - video device is kept open inbetween calls and only closed upon final call
//
const char * BtDriver_GetInputName( uint cardIndex, uint cardType, uint inputIdx )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   struct video_capability vcapab;
   struct video_channel vchan;
   char * pDevName;
   const char * pName = NULL;
   #define MAX_INPUT_NAME_LEN 32
   static char name[MAX_INPUT_NAME_LEN + 1];

   if (video_fd == -1)
   {
      dprintf1("BtDriver-GetInputName: opened video device, fd=%d\n", video_fd);
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, cardIndex);
      video_fd = open(pDevName, O_RDONLY);
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
           pName = (char*) pVbiBuf->tv_cards[cardIndex].inputs[inputIdx].name;

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
bool BtDriver_Configure( int cardIndex, int prio, int chipType, int cardType, int tunerType, int pllType )
{
   struct timeval tv;
   bool wasEnabled;

   wasEnabled = pVbiBuf->isEnabled && !pVbiBuf->hasFailed;

   // pass the new card index to the slave via shared memory
   pVbiBuf->cardIndex = cardIndex;

   if (wasEnabled)
   {  // wait 30ms for the slave to process the request
      tv.tv_sec  = 0;
      tv.tv_usec = 30000L;
      select(0, NULL, NULL, NULL, &tv);
   }

   // return FALSE if acq was disabled while processing the request
   return (!wasEnabled || !pVbiBuf->hasFailed);
}

#if 0
// ---------------------------------------------------------------------------
// Check if the video device is free
//
bool BtDriver_CheckDevice( void )
{
   char * pDevName;
   int  fd;
   bool result;

   if (video_fd == -1)
   {
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, pVbiBuf->cardIndex);
      fd = open(pDevName, O_RDONLY);
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
// - do nothing
//
static void BtDriver_SignalWakeUp( int sigval )
{
   recvWakeUpSig = TRUE;
   signal(sigval, BtDriver_SignalWakeUp);
}

// ---------------------------------------------------------------------------
// Receive signal to free or take vbi device
// - if threads are used, the signal is received by the master thread
//
#ifndef USE_THREADS
static void BtDriver_SignalHangup( int sigval )
{
   if (pVbiBuf != NULL)
   {
      if (isVbiProcess && (pVbiBuf->epgPid != -1))
      {  // just pass the signal through to the master process
         kill(pVbiBuf->epgPid, SIGHUP);
      }
   }
   signal(sigval, BtDriver_SignalHangup);
}
#endif

// ---------------------------------------------------------------------------
// Check upon the acq slave after being signaled for death of a child
// - if threads are used, the signal is ignored
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
            if (pVbiBuf->failureErrno == 0)
               SystemErrorMessage_Set(&pSysErrorText, 0, "acquisition child process was killed", NULL);
            pVbiBuf->hasFailed = TRUE;
         }
         else if ((pVbiBuf->vbiPid == -1) && (pVbiBuf->hasFailed == FALSE))
         {  // slave caught deadly signal and cleared his pid already
            dprintf1("BtDriver-SignalDeathOfChild: acq slave %d terminated - disable acq", pid);
            if (pVbiBuf->failureErrno == 0)
               SystemErrorMessage_Set(&pSysErrorText, 0, "acquisition child process was killed", NULL);
            pVbiBuf->hasFailed = TRUE;
         }
         else
            dprintf2("BtDriver-SignalDeathOfChild: pid %d: %s\n", pid, ((pVbiBuf->hasFailed == FALSE) ? "not the VBI slave" : "acq already disabled"));
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
#if defined(__NetBSD__) || defined(__FreeBSD__)
static void BtDriver_SignalAlarm( int sigval )
{
  BtDriver_CloseVbi ();
  signal(sigval, BtDriver_SignalAlarm);
}
#endif  //__NetBSD__ || __FreeBSD__

// ---------------------------------------------------------------------------
// Wake-up the acq child process to start acquisition
// - the child signals back after it completed the operation
// - the status of the operation is in the hasFailed flag
//
bool BtDriver_StartAcq( void )
{
   bool result = FALSE;
#ifdef USE_THREADS
   sigset_t sigmask;

   if ((pVbiBuf != NULL) && (pVbiBuf->vbiPid == -1))
   {
      dprintf0("BtDriver-StartAcq: starting thread\n");

      pthread_mutex_lock(&vbi_start_mutex);
      SystemErrorMessage_Set(&pSysErrorText, 0, NULL);
      pVbiBuf->hasFailed = FALSE;
      pVbiBuf->failureErrno = 0;
      if (pthread_create(&vbi_thread_id, NULL, BtDriver_Main, NULL) == 0)
      {
         sigemptyset(&sigmask);
         sigaddset(&sigmask, SIGUSR1);
         pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

         // wait for the slave to report the initialization result
         pthread_cond_wait(&vbi_start_cond, &vbi_start_mutex);
         pthread_mutex_unlock(&vbi_start_mutex);

         result = (pVbiBuf->hasFailed == FALSE);
      }
      else
         SystemErrorMessage_Set(&pSysErrorText, errno, "failed to create acquisition thread: ", NULL);
   }
   else
   {
      debug0("BtDriver-StartAcq: acq already running");
      result = TRUE;
   }

#else
   struct timeval tv;

   if ((pVbiBuf != NULL) && (pVbiBuf->vbiPid != -1))
   {
      SystemErrorMessage_Set(&pSysErrorText, 0, NULL);
      pVbiBuf->hasFailed = FALSE;
      pVbiBuf->failureErrno = 0;
      pVbiBuf->freeDevice = FALSE;
      recvWakeUpSig = FALSE;
      if (kill(pVbiBuf->vbiPid, SIGUSR1) != -1)
      {
         if ((recvWakeUpSig == FALSE) && (pVbiBuf->hasFailed == FALSE))
         {
            dprintf1("BtDriver-StartAcq: waiting for response from slave pid=%d\n", pVbiBuf->vbiPid);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            select(0, NULL, NULL, NULL, &tv);
         }
         dprintf3("BtDriver-StartAcq: slave pid=%d %s; state=%s\n", pVbiBuf->vbiPid, (recvWakeUpSig ? "replied" : "did not reply"), (pVbiBuf->hasFailed ? "failed" : "ok"));
         pVbiBuf->freeDevice = pVbiBuf->hasFailed;
         result = !pVbiBuf->hasFailed;
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
      dprintf0("BtDriver-StopAcq: killing thread\n");

      acqShouldExit = TRUE;
      pthread_kill(vbi_thread_id, SIGUSR1);
      if (pthread_join(vbi_thread_id, NULL) != 0)
         perror("pthread_join");
   }
   else
      debug0("BtDriver-StopAcq: acq not running");

#else
   if (pVbiBuf != NULL)
   {
      pVbiBuf->freeDevice = TRUE;

      if (pVbiBuf->vbiPid != -1)
      {  // wake up the child
         kill(pVbiBuf->vbiPid, SIGUSR1);
      }
   }
#endif
}

// ---------------------------------------------------------------------------
// Query error description for last failed operation
// - the returned pointer refers to internally managed memory and must not be freed
// - returns NULL if no error occurred
//
const char * BtDriver_GetLastError( void )
{
   if (pVbiBuf != NULL)
   {
      if (pSysErrorText == NULL)
      {
         if (pVbiBuf->failureErrno == EBUSY)
            SystemErrorMessage_Set(&pSysErrorText, 0, "VBI device is busy (-> close all video applications)", NULL);
         else if (pVbiBuf->failureErrno != 0)
            SystemErrorMessage_Set(&pSysErrorText, pVbiBuf->failureErrno, "access error ",
                                   BtDriver_GetDevicePath(DEV_TYPE_VBI, pVbiBuf->cardIndex), ": ", NULL);
         else
            debug0("BtDrvier-GetLastError: no error occurred");

         pVbiBuf->failureErrno = 0;
      }
   }
   else
      debug0("BtDrvier-GetLastError: not initializd");

   return pSysErrorText;
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
      shmdt((void *) pVbiBuf);
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
      xfree((void *) pVbiBuf);
      pVbiBuf = NULL;
   }
#endif
   SystemErrorMessage_Set(&pSysErrorText, 0, NULL);
}

#ifndef USE_THREADS
// ---------------------------------------------------------------------------
// Terminate acq main loop if the parent is no longer alive
// - not required for threaded mode since we only care for program crashes,
//   i.e. if the main thread dies, the acq thread dies too
//
static void BtDriver_CheckParent( void )
{
   if ((pVbiBuf != NULL) && (isVbiProcess == TRUE))
   {
      if (pVbiBuf->epgPid != -1)
      {
         if ((kill(pVbiBuf->epgPid, 0) == -1) && (errno == ESRCH))
         {  // process no longer exists -> kill acquisition
            debug1("parent is dead - terminate acq pid %d", pVbiBuf->vbiPid);
            acqShouldExit = TRUE;
         }
         else
            dprintf0("BtDriver-CheckParent: parent is alive\n");
      }
      else
         acqShouldExit = TRUE;
   }
}
#endif

// ---------------------------------------------------------------------------
// Determine the size of the VBI buffer
// - the buffer size depends on the number of VBI lines that are captured
//   for each frame; each read on the vbi device should request all the lines
//   of one frame, else the rest will be overwritten by the next frame
//
static void BtDriver_OpenVbiBuf( void )
{
   #if !defined (__NetBSD__) && !defined (__FreeBSD__)
   struct vbi_format fmt;
   struct vbi_format fmt_copy;
   long bufSize;
   #endif

   bufLines            = VBI_DEFAULT_LINES * 2;
   bufLineSize         = VBI_DEFAULT_BPL;

   #ifndef ZVBI_DECODER
   VbiDecodeSetSamplingRate(0, 0);
   #else
   memset(&zvbi_rd, 0, sizeof(zvbi_rd));
   zvbi_rd.sampling_rate    = 35468950L;
   zvbi_rd.offset           = (int)(9.2e-6 * 35468950L);
   zvbi_rd.bytes_per_line   = VBI_DEFAULT_BPL;
   zvbi_rd.start[0]         = 7;
   zvbi_rd.count[0]         = VBI_DEFAULT_LINES;
   zvbi_rd.start[1]         = 319;
   zvbi_rd.count[1]         = VBI_DEFAULT_LINES;
   zvbi_rd.interlaced       = FALSE;
   zvbi_rd.synchronous      = TRUE;
   zvbi_rd.sampling_format  = VBI_PIXFMT_YUV420;
   zvbi_rd.scanning         = 625;
   #endif  // ZVBI_DECODER

   #if !defined (__NetBSD__) && !defined (__FreeBSD__)

   dprintf1("BTTV driver version 0x%X\n", ioctl(vbi_fdin, BTTV_VERSION));

   if (ioctl(vbi_fdin, VIDIOCGVBIFMT, &fmt) == 0)
   {  // VBI format query succeeded -> now try to alter acc. to out preferences
      fmt_copy = fmt;
      fmt_copy.start[0]       = 7;
      fmt_copy.count[0]       = VBI_DEFAULT_LINES;
      fmt_copy.start[1]       = 319;
      fmt_copy.count[1]       = VBI_DEFAULT_LINES;
      fmt_copy.sample_format  = VIDEO_PALETTE_RAW;
      if (ioctl(vbi_fdin, VIDIOCSVBIFMT, &fmt_copy) == 0)
      {  // update succeeded -> use the new parameters (possibly altered by the driver)
         fmt = fmt_copy;
      }
      else  // ignore failure
         debug2("BtDriver-OpenVbiBuf: ioctl(VIDIOCSVBIFMT) failed with errno %d: %s", errno, strerror(errno));

      dprintf5("VBI format: %d lines, %d samples per line, %d sampling rate, start lines %d,%d\n", fmt.count[0] + fmt.count[1], fmt.samples_per_line, fmt.sampling_rate, fmt.start[0], fmt.start[1]);
      bufLines = fmt.count[0] + fmt.count[1];
      if (bufLines > VBI_MAX_LINENUM * 2)
         bufLines = VBI_MAX_LINENUM;

      bufLineSize = fmt.samples_per_line;
      if ((bufLineSize == 0) || (bufLineSize > VBI_MAX_LINESIZE))
         bufLineSize = VBI_MAX_LINESIZE;

      #ifndef ZVBI_DECODER
      VbiDecodeSetSamplingRate(fmt.sampling_rate, fmt.start[0]);
      #else  // ZVBI_DECODER
      zvbi_rd.sampling_rate    = fmt.sampling_rate;
      zvbi_rd.offset           = (int)(9.2e-6 * fmt.sampling_rate);
      zvbi_rd.bytes_per_line   = fmt.samples_per_line;
      zvbi_rd.start[0]         = fmt.start[0];
      zvbi_rd.count[0]         = fmt.count[0];
      zvbi_rd.start[1]         = fmt.start[1];
      zvbi_rd.count[1]         = fmt.count[1];
      //zvbi_rd.interlaced       = !!(fmt.flags & VBI_INTERLACED);
      //zvbi_rd.synchronous      = !(fmt.flags & VBI_UNSYNC);
      zvbi_rd.sampling_format  = VBI_PIXFMT_YUV420;
      #endif  // ZVBI_DECODER
   }
   else
   {
      ifdebug2(errno != EINVAL, "ioctl VIDIOCGVBIFMT error %d: %s", errno, strerror(errno));

      bufSize = ioctl(vbi_fdin, BTTV_VBISIZE);
      if (bufSize == -1)
      {
         perror("ioctl BTTV_VBISIZE");
      }
      else if ( (bufSize < VBI_DEFAULT_BPL) ||
                (bufSize > VBI_MAX_LINESIZE * VBI_MAX_LINENUM * 2) ||
                ((bufSize % VBI_DEFAULT_BPL) != 0) )
      {
         fprintf(stderr, "BTTV_VBISIZE: illegal buffer size %ld\n", bufSize);
      }
      else
      {  // ok
         bufLines    = bufSize / VBI_DEFAULT_BPL;
         bufLineSize = VBI_DEFAULT_BPL;

         #ifdef ZVBI_DECODER
         zvbi_rd.count[0] = (bufSize / VBI_DEFAULT_BPL) >> 1;
         zvbi_rd.count[1] = (bufSize / VBI_DEFAULT_BPL) - zvbi_rd.count[0];
         #endif
      }
   }
   #endif  // not NetBSD

   // pass parameters to VBI slicer
   #ifdef ZVBI_DECODER
   vbi_raw_decoder_add_services(&zvbi_rd, VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS, 1);
   #endif

   rawbuf = xmalloc(bufLines * bufLineSize);
}

// ---------------------------------------------------------------------------
// Decode all VBI lines of the last seen frame
//
static void BtDriver_DecodeFrame( void )
{
#ifndef ZVBI_DECODER
   uchar *pData;
#else
   vbi_sliced rdo[32];
   uint count;
#endif
   size_t bufSize;
   slong stat;
   uint  line;

   #if defined(__NetBSD__) || defined(__FreeBSD__)
   // wait max. 10 seconds for the read to complete. After this time
   // close /dev/vbi in the signal handler to avoid endless blocking
   alarm(10);
   #endif

   bufSize = bufLineSize * bufLines;
   stat = read(vbi_fdin, rawbuf, bufSize);

   #if defined(__NetBSD__) || defined(__FreeBSD__)
   alarm(0);
   #endif

   if ( stat >= bufLineSize )
   {
      #if !defined (__NetBSD__) && !defined (__FreeBSD__)
      // retrieve frame sequence counter from the end of the buffer
      #ifndef ZVBI_DECODER
      VbiDecodeStartNewFrame(*(uint32_t *)(rawbuf + stat - 4));
      #else
      TtxDecode_NewVbiFrame(*(uint32_t *)(rawbuf + stat - 4));
      #endif
      #else
      VbiDecodeStartNewFrame(0);
      #endif

      #ifndef ZVBI_DECODER
      #ifndef SAA7134_0_2_2
      pData = rawbuf;
      for (line=0; line < stat/bufLineSize; line++, pData += bufLineSize)
      {
         VbiDecodeLine(pData, line, TRUE);
         //printf("%02d: %08lx\n", line, *((ulong*)pData-4));  // frame counter
      }
      #else  // SAA7134_0_2_2
      // workaround for bug in saa7134-0.2.2: the 2nd field of every frame is one buffer late
      pData = rawbuf + (16 * bufLineSize);
      for (line=16; line < 32; line++, pData += bufLineSize)
         VbiDecodeLine(pData, line, TRUE);
      pData = rawbuf;
      for (line=0; line < 16; line++, pData += bufLineSize)
         VbiDecodeLine(pData, line, TRUE);
      #endif
      #else  // ZVBI_DECODER
      count = vbi_raw_decode(&zvbi_rd, rawbuf, rdo);
      for (line=0; line < count; line++)
      {
         if ((rdo[line].id & VBI_SLICED_TELETEXT_B) != 0)
         {
            TtxDecode_AddPacket(rdo[line].data + 0, rdo[line].line);
         }
         else if (rdo[line].id == VBI_SLICED_VPS)
         {
            TtxDecode_AddVpsData(rdo[line].data - 3);
         }
      }
      #endif  // ZVBI_DECODER
   }
   else if (stat < 0)
   {
      if (errno == EBUSY)
      {  // Linux v4l2 API allows multiple open but only one capturing process
         debug0("BtDriver-DecodeFrame: device busy - abort");
         pVbiBuf->failureErrno = errno;
         pVbiBuf->hasFailed = TRUE;
      }
      else if ((errno != EINTR) && (errno != EAGAIN))
         debug2("BtDriver-DecodeFrame: read returned %d: %s", errno, strerror(errno));
   }
   else if (stat >= 0)
   {
      debug2("BtDriver-DecodeFrame: short read: %ld of %d", stat, bufSize);
   }
}

#if defined(__NetBSD__) || defined(__FreeBSD__)
// ---------------------------------------------------------------------------
// Sets up the capturing needed for NetBSD to receive vbi-data.
//
int BtDriver_StartCapture(void)
{
  char * pDevName;
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
    pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, vbiCardIndex);
    video_fd=open(pDevName,O_RDONLY);
    if (video_fd==-1) {//device opened by someone else
      dprintf1("BtDriver-StartCapture: could not open device %s\n", pDevName);
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
#endif //__NetBSD__ || __FreeBSD__

// ---------------------------------------------------------------------------
// VBI decoder main loop
//
static void * BtDriver_Main( void * foo )
{
   #ifdef USE_THREADS
   sigset_t sigmask;
   #else
   struct timeval tv;
   uint parentCheckCounter;
   uint lastReaderIdx;
   #endif

   pVbiBuf->vbiPid = getpid();
   vbi_fdin = -1;
   #if defined(__NetBSD__) || defined(__FreeBSD__)
   video_fd = -1;
   #endif

   acqShouldExit = FALSE;
   #ifdef USE_THREADS
   sigemptyset(&sigmask);
   sigaddset(&sigmask, SIGHUP);
   pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
   sigemptyset(&sigmask);
   sigaddset(&sigmask, SIGUSR1);
   pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL);
   signal(SIGUSR1, BtDriver_SignalWakeUp);

   // open the VBI device and inform the master about the result (via the hasFailed flag)
   pthread_mutex_lock(&vbi_start_mutex);
   if (vbi_fdin == -1)
      BtDriver_OpenVbi();
   if (pVbiBuf->hasFailed)
      acqShouldExit = TRUE;
   pthread_cond_signal(&vbi_start_cond);
   pthread_mutex_unlock(&vbi_start_mutex);
   #else
   // notify parent that child is ready
   kill(pVbiBuf->epgPid, SIGUSR1);
   parentCheckCounter = 0;
   lastReaderIdx = pVbiBuf->reader_idx;;
   #endif

   while (acqShouldExit == FALSE)
   {
      if ( (pVbiBuf->hasFailed == FALSE)
           #ifndef USE_THREADS
           && (pVbiBuf->freeDevice == FALSE)
           #endif
         )
      {
         #if !defined (__NetBSD__) && !defined (__FreeBSD__)
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

            #ifndef USE_THREADS
            // if acq is stalled or the reader is not processing its data anymore
            // check every 15 secs if the master process is still alive
            if (lastReaderIdx == pVbiBuf->reader_idx)
            {
               parentCheckCounter += 1;
               if (parentCheckCounter > 15 * 25)
               {
                  BtDriver_CheckParent();
                  parentCheckCounter = 0;
               }
            }
            else
               parentCheckCounter = 0;
            lastReaderIdx = pVbiBuf->reader_idx;;
            #endif
         }
      }
      else
      {  // acq was switched off -> close device
         #ifndef USE_THREADS
         BtDriver_CloseVbi();

         // sleep until signal; check parent every 30 secs
         tv.tv_sec = 30;
         tv.tv_usec = 0;
         select(0, NULL, NULL, NULL, &tv);
         BtDriver_CheckParent();

         #else
         // the thread terminates when acq is stopped
         acqShouldExit = TRUE;
         #endif
      }

      #if !defined (__NetBSD__) && !defined (__FreeBSD__)
      // We get here only on Linux (because of pVbiBuf->doQueryFreq),
      if (pVbiBuf->doQueryFreq && (vbi_fdin != -1))
      {
         struct video_channel vchan;
         ulong lfreq;

         if (ioctl(vbi_fdin, VIDIOCGFREQ, &lfreq) == 0)
         {
            dprintf1("BtDriver-Main: QueryChannel got %.2f MHz\n", (double)lfreq/16);

            // get TV norm set in the tuner (channel #0)
            memset(&vchan, 0, sizeof(vchan));

            if (ioctl(vbi_fdin, VIDIOCGCHAN, &vchan) == 0)
            {
               dprintf1("BtDriver-Main: QueryChannel got norm %d\n", vchan.norm);
               lfreq |= (uint)vchan.norm << 24;
            }
            else
               debug1("BtDriver-Main: VIDIOCGCHAN error: %s", strerror(errno));

            pVbiBuf->vbiQueryFreq = lfreq;
         }
         else
            perror("VIDIOCGFREQ");

         pVbiBuf->doQueryFreq = FALSE;
         kill(pVbiBuf->epgPid, SIGUSR1);
      }
      #endif
   }

   BtDriver_CloseVbi();

   pVbiBuf->hasFailed = TRUE;

   #ifdef USE_THREADS
   pVbiBuf->vbiPid = -1;
   #else
   if (pVbiBuf->isEnabled)
   {  // notify the parent that acq has stopped (e.g. after SIGTERM)
      dprintf1("BtDriver-Main: acq slave exiting - signalling parent %d\n", pVbiBuf->epgPid);
      kill(pVbiBuf->epgPid, SIGHUP);
   }
   #endif

   return NULL;
}

// ---------------------------------------------------------------------------
// Initialize the module - called once at program start
// - if processes are used: create shared memory and the VBI slave process
//   and enter the main loop
// - if threads are used: just alloc memory and initialize a few variables
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

   memset((void *) pVbiBuf, 0, sizeof(EPGACQ_BUF));
   pVbiBuf->epgPid = getpid();
   pVbiBuf->freeDevice = TRUE;

   #if defined(__NetBSD__) || defined(__FreeBSD__)
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

      #if defined(__NetBSD__) || defined(__FreeBSD__)
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

   #if defined(__NetBSD__) || defined(__FreeBSD__)
   // install signal handler to implement read timeout on /dev/vbi
   signal(SIGALRM,  BtDriver_SignalAlarm);
   #endif

   pVbiBuf = xmalloc(sizeof(*pVbiBuf));
   memset((void *) pVbiBuf, 0, sizeof(EPGACQ_BUF));

   pVbiBuf->epgPid = getpid();
   pVbiBuf->vbiPid = -1;

   vbi_fdin = -1;
   video_fd = -1;

   pthread_cond_init(&vbi_start_cond, NULL);
   pthread_mutex_init(&vbi_start_mutex, NULL);

   return TRUE;
#endif  //USE_THREADS
}

