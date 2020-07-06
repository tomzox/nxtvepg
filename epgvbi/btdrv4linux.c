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
 *    packets. This buffer is managed in the ttxdecode module. When
 *    you read the code in these modules, always remember that some
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
 *  $Id: btdrv4linux.c,v 1.79 2020/06/24 07:23:04 tom Exp tom $
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
#include <dirent.h>
#include <pthread.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/syserrmsg.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/zvbidecoder.h"
#include "epgvbi/ttxdecode.h"
#include "epgvbi/dvb_demux.h"
#include "epgvbi/btdrv.h"

#if defined(__NetBSD__) || defined(__FreeBSD__)
# include <sys/mman.h>
# ifdef __FreeBSD__
#  include <machine/ioctl_bt848.h>
#  include <machine/ioctl_meteor.h>
# else 
#  include <dev/ic/bt8xx.h>
# endif

# define VIDIOCSFREQ    TVTUNER_SETFREQ
# define VIDIOCGFREQ    TVTUNER_GETFREQ
# define VIDIOCGTUNER   TVTUNER_GETSTATUS
# define VIDIOCGCHAN	TVTUNER_GETCHNL
// BSD doesn't know interrupted sys'calls, hence just declare IOCTL() macro transparent
# define IOCTL(fd, cmd, data)  ioctl(fd, cmd, data)

#else  // Linux
#include <linux/videodev2.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

/* same as ioctl(), but repeat if interrupted */
#define IOCTL(fd, cmd, data)                                            \
({ int __result; do __result = ioctl(fd, cmd, data);                    \
   while ((__result == -1L) && (errno == EINTR) && (acqShouldExit == FALSE)); __result; })
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

#define VBI_DEFAULT_LINES 16
#define VBI_MAX_LINENUM   (2*18)         // reasonable upper limits
#define VBI_MAX_LINESIZE (8*1024)
#define VBI_DEFAULT_BPL   2048

#define DEV_MAX_NAME_LEN 64

typedef enum
{
   DEV_TYPE_VBI,
   DEV_TYPE_VIDEO,
#if defined(__NetBSD__) || defined(__FreeBSD__)
   DEV_TYPE_TUNER,
#endif
   DEV_TYPE_DVB,
} BTDRV_DEV_TYPE;

volatile EPGACQ_BUF *pVbiBuf;

static pthread_t        vbi_thread_id;
static pthread_cond_t   vbi_start_cond;
static pthread_mutex_t  vbi_start_mutex;

// vars used in the acq slave process
static bool acqShouldExit;
static int vbiCardIndex;
static int vbiDvbPid;
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


static vbi_raw_decoder   zvbi_rd;
static vbi_dvb_demux   * zvbi_demux;
static int64_t           zvbiLastTimestamp;
static ulong             zvbiLastFrameNo;


// function forward declarations
int BtDriver_StartCapture(void);
static void * BtDriver_Main( void * foo );
static void BtDriver_OpenVbiBuf( void );

// ---------------------------------------------------------------------------
// Get name of the specified device type
//
static char * BtDriver_GetDevicePath( BTDRV_DEV_TYPE devType, uint cardIdx, bool isDvb )
{
   static char devName[DEV_MAX_NAME_LEN + 1];

#if !defined(__NetBSD__) && !defined(__FreeBSD__)
   if (isDvb)
   {
      switch (devType)
      {
         case DEV_TYPE_VIDEO:
            snprintf(devName, DEV_MAX_NAME_LEN, "/dev/dvb/adapter%u/frontend0", cardIdx);
            break;

         case DEV_TYPE_VBI:
            snprintf(devName, DEV_MAX_NAME_LEN, "/dev/dvb/adapter%u/demux0", cardIdx);
            break;

         default:
            strcpy(devName, "/dev/null");
            fatal1("BtDriver-GetDevicePath: illegal DVB device type %d", devType);
            break;
      }
   }
   else
   {
      static char * pLastDevPath = NULL;
      char * pDevPath;
      uint try;

      switch (devType)
      {
         case DEV_TYPE_VIDEO:
         case DEV_TYPE_VBI:
            for (try = 0; try < 3; try++)
            {
               if (pLastDevPath != NULL)
                  pDevPath = pLastDevPath;
               else if (try <= 1)
                  pDevPath = "/dev";
               else
                  pDevPath = "/dev/v4l";

               if (devType == DEV_TYPE_VIDEO)
                  snprintf(devName, DEV_MAX_NAME_LEN, "%s/video%u", pDevPath, cardIdx);
               else
                  snprintf(devName, DEV_MAX_NAME_LEN, "%s/vbi%u", pDevPath, cardIdx);

               if (access(devName, R_OK) == 0)
               {
                  dprintf3("BtDriver-GetDevicePath: set analog device path %s (for device #%d, %s)\n", pDevPath, cardIdx, devName);
                  pLastDevPath = pDevPath;
                  break;
               }
            }
            break;

         default:
            strcpy(devName, "/dev/null");
            fatal1("BtDriver-GetDevicePath: illegal device type %d", devType);
            break;
      }
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
   devName[DEV_MAX_NAME_LEN] = 0;  // 0-terminate in case of overflow

   return devName;
}

// ---------------------------------------------------------------------------
// Search for a VBI device file with the given or higher index
// - returns index of next device file, or -1 if none found
//
static sint BtDriver_SearchDeviceFile( BTDRV_DEV_TYPE devType, uint cardIdx, bool isDvb )
{
   DIR    *dir;
   struct dirent *entry;
   uint   try;
   int    scanLen;
   uint   devIdx;
   sint   result = -1;

   if (isDvb == FALSE)
   {
      for (try = 0; (try <= 1) && (result == -1); try++)
      {
         if (try == 0)
            dir = opendir("/dev");
         else
            dir = opendir("/dev/v4l");

         if (dir != NULL)
         {
            while ((entry = readdir(dir)) != NULL)
            {
               if ( ( (devType == DEV_TYPE_VIDEO) ?
                      (sscanf(entry->d_name, "video%u%n", &devIdx, &scanLen) >= 1) :
                      (sscanf(entry->d_name, "vbi%u%n", &devIdx, &scanLen) >= 1) ) &&
                    (entry->d_name[scanLen] == 0) &&
                    (devIdx > cardIdx) )
               {
                  result = devIdx;
                  break;
               }
            }
            closedir(dir);
         }
      }
   }
   else if (devType == DEV_TYPE_DVB)
   {
      dir = opendir("/dev/dvb");

      if (dir != NULL)
      {
         while ((entry = readdir(dir)) != NULL)
         {
            if ( (sscanf(entry->d_name, "adapter%u%n", &devIdx, &scanLen) >= 1) &&
                 (entry->d_name[scanLen] == 0) &&
                 (devIdx > cardIdx) )
            {
               result = devIdx;
               break;
            }
         }
         closedir(dir);
      }
   }

   return result;
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
    pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, i, FALSE);
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
        default:
          fatal0("BtDriver-ScanDevices: internal error: MAX_INPUTS > 4");
          input_name = "";
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
// Set channel priority for next channel change
//
void BtDriver_SetChannelProfile( VBI_CHANNEL_PRIO_TYPE prio )
{
   if (pVbiBuf != NULL)
   {
      switch (prio)
      {
         case VBI_CHANNEL_PRIO_BACKGROUND:
            pVbiBuf->chnPrio = V4L2_PRIORITY_BACKGROUND;
            break;
         case VBI_CHANNEL_PRIO_INTERACTIVE:
            pVbiBuf->chnPrio = V4L2_PRIORITY_INTERACTIVE;
            break;
         default:
            pVbiBuf->chnPrio = V4L2_PRIORITY_UNSET;
            break;
      }
   }
}

// ---------------------------------------------------------------------------
// Configure the PES PID for DVB
//
static bool BtDriver_SetDvbPid(int pid)
{
   bool result = FALSE;

   if (vbi_fdin != -1)
   {
      struct dmx_pes_filter_params filter;
      memset(&filter, 0, sizeof(filter));

      filter.pid          = pid;
      filter.input        = DMX_IN_FRONTEND;
      filter.output       = DMX_OUT_TAP;
      filter.pes_type     = DMX_PES_OTHER;
      filter.flags        = DMX_IMMEDIATE_START;

      if (IOCTL(vbi_fdin, DMX_SET_PES_FILTER, &filter) != -1)
      {
         dprintf2("BtDriver-SetDvbPid: fd:%d demux DVB PID:%d\n", vbi_fdin, pid);
         result = TRUE;
      }
      else
         debug2("BtDriver-SetDvbPid: failed to set DVB PID %d: %s", pid, strerror(errno));
   }
   return result;
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
   vbiDvbPid = pVbiBuf->cardIsDvb ? pVbiBuf->dvbPid : -1;

   #if defined(__NetBSD__) || defined(__FreeBSD__)
   // the bktr driver on NetBSD requires to start video capture for VBI to work
   vbiInputIndex = pVbiBuf->inputIndex;
   pVbiBuf->tv_cards[pVbiBuf->cardIndex].inUse=TRUE;
   BtDriver_ScanDevices(FALSE);
   if (BtDriver_StartCapture())
   #endif
   {
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_VBI, vbiCardIndex, (vbiDvbPid != -1));
      if (vbiDvbPid != -1)
      {
         zvbiLastTimestamp = 0.0;
         zvbiLastFrameNo = 0;

         // read non-blocking, as number/size of packets per frame may vary
         vbi_fdin = open(pDevName, O_RDONLY | O_NONBLOCK);
         if (vbi_fdin != -1)
         {
            dprintf2("BtDriver-OpenVbi: opened %s fd: %d\n", pDevName, vbi_fdin);
            if (BtDriver_SetDvbPid(vbiDvbPid) == FALSE)
            {
               int bak_errno = errno;
               close(vbi_fdin);
               vbi_fdin = -1;
               errno = bak_errno;
            }
         }
      }
      else  // V4L2
      {
         vbi_fdin = open(pDevName, O_RDONLY);
         if (vbi_fdin != -1)
         {
            struct v4l2_capability  vcap;
            memset(&vcap, 0, sizeof(vcap));
            if (IOCTL(vbi_fdin, VIDIOC_QUERYCAP, &vcap) != -1)
            {  // this is a v4l2 device
#ifdef VIDIOC_S_PRIORITY
               // set device user priority to "background" -> channel swtiches will fail while
               // higher-priority users (e.g. an interactive TV app) have opened the device
               enum v4l2_priority prio = V4L2_PRIORITY_BACKGROUND;
               if (IOCTL(vbi_fdin, VIDIOC_S_PRIORITY, &prio) != 0)
                  debug4("ioctl VIDIOC_S_PRIORITY=%d (background) failed on %s: %d, %s", V4L2_PRIORITY_BACKGROUND, pDevName, errno, strerror(errno));
#endif  // VIDIOC_S_PRIORITY

               dprintf4("BtDriver-OpenVbi: %s (%s) is a v4l2 vbi device, driver %s, version 0x%08x\n", pDevName, vcap.card, vcap.driver, vcap.version);
               if ((vcap.capabilities & V4L2_CAP_VBI_CAPTURE) == 0)
               {
                  debug2("%s (%s) does not support vbi capturing - stop acquisition.", pDevName, vcap.card);
                  close(vbi_fdin);
                  errno = ENOSYS;
                  vbi_fdin = -1;
               }
            }
         }
      }
   }
   if (vbi_fdin == -1)
   {
      pVbiBuf->failureErrno = errno;
      pVbiBuf->hasFailed = TRUE;
      debug2("BtDriver-OpenVbi: failed with errno %d (%s)", pVbiBuf->failureErrno, strerror(pVbiBuf->failureErrno));
   }
   else
   {  // open successful -> write pid in file
      sprintf(tmpName, PIDFILENAME, vbiCardIndex);
      fp = fopen(tmpName, "w");
      if (fp != NULL)
      {
         fprintf(fp, "%d", getpid());
         fclose(fp);
      }
      // allocate memory for the VBI data buffer
      BtDriver_OpenVbiBuf();
   }
}

// ---------------------------------------------------------------------------
// Close the VBI device
//
static void BtDriver_CloseVbi( bool keepVideoOpen )
{
   char tmpName[DEV_MAX_NAME_LEN];

   if (vbi_fdin != -1)
   {
      dprintf1("BtDriver-CloseVbi: closing VBI fd %d\n", vbi_fdin);

      sprintf(tmpName, PIDFILENAME, vbiCardIndex);
      unlink(tmpName);

      // free slicer buffer and pattern array
      ZvbiSliceAndProcess(NULL, NULL, 0);
      vbi_raw_decoder_destroy(&zvbi_rd);

      close(vbi_fdin);
      vbi_fdin = -1;
      xfree(rawbuf);
      rawbuf = NULL;
      if (zvbi_demux != NULL)
         vbi_dvb_demux_delete(zvbi_demux);
      zvbi_demux = NULL;

      if ((video_fd != -1) && !keepVideoOpen)
      {
         dprintf1("BtDriver-CloseVbi: closing video fd %d\n", video_fd);
         close(video_fd);
         video_fd = -1;
#if defined(__NetBSD__) || defined(__FreeBSD__)
         pVbiBuf->tv_cards[pVbiBuf->cardIndex].inUse = FALSE;
#endif
      }
   }
}

// ---------------------------------------------------------------------------
// Close video device (in the master process/thread)
//
void BtDriver_CloseDevice( void )
{
   #if !defined (__NetBSD__) && !defined (__FreeBSD__)
   dprintf1("BtDriver-CloseDevice: close fd: %d\n", video_fd);

   if (video_fd != -1)
   {
      close(video_fd);
      video_fd = -1;
   }
   #else //  __NetBSD__ || __FreeBSD__
   dprintf1("BtDriver-CloseDevice: close fd: %d\n", tuner_fd);

   if (tuner_fd != -1)
   {
      // unmute tuner
      int mute_arg = AUDIO_UNMUTE;
      if (IOCTL (tuner_fd, BT848_SAUDIO, &mute_arg) == 0)
      {
         dprintf0("Unmuted tuner audio.\n");
      }
      else if (pSysErrorText == NULL)
      {
         SystemErrorMessage_Set(&pSysErrorText, errno, "unmuting audio (ioctl AUDIO_UNMUTE): ", NULL);
      }

      close(tuner_fd);
      tuner_fd = -1;
   }
   devKeptOpen = FALSE;
   #endif // __NetBSD__ || __FreeBSD__
}

// ---------------------------------------------------------------------------
// Change the video input source and TV norm
//
static bool BtDriver_SetInputSource( int inputIdx, EPGACQ_TUNER_NORM norm, bool * pIsTuner )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   bool isTuner = FALSE;
   bool result = FALSE;

   if (video_fd != -1)
   {
      if (pVbiBuf->cardIsDvb && (norm == EPGACQ_TUNER_NORM_DVB))
      {
         // TODO front-end selection?
         isTuner = TRUE;
         result = TRUE;
      }
      else if (norm != EPGACQ_TUNER_NORM_DVB)  // V4L2
      {
         struct v4l2_input v4l2_desc_in;
         uint32_t v4l2_inp;
         v4l2_std_id vstd2;

         v4l2_inp = inputIdx;
         if (IOCTL(video_fd, VIDIOC_S_INPUT, &v4l2_inp) == 0)
         {
            switch (norm)
            {
               case EPGACQ_TUNER_NORM_PAL:
               default:
                  vstd2 = V4L2_STD_PAL;
                  break;
               case EPGACQ_TUNER_NORM_NTSC:
                  vstd2 = V4L2_STD_NTSC;
                  break;
               case EPGACQ_TUNER_NORM_SECAM:
                  vstd2 = V4L2_STD_SECAM;
                  break;
            }
            if (IOCTL(video_fd, VIDIOC_S_STD, &vstd2) == 0)
            {
               result = TRUE;
               isTuner = FALSE;

               memset(&v4l2_desc_in, 0, sizeof(v4l2_desc_in));
               v4l2_desc_in.index = inputIdx;
               if (IOCTL(video_fd, VIDIOC_ENUMINPUT, &v4l2_desc_in) == 0)
                  isTuner = ((v4l2_desc_in.type & V4L2_INPUT_TYPE_TUNER) != 0);
               else
                  debug2("BtDriver-SetInputSource: v4l2 VIDIOC_ENUMINPUT #%d error: %s", inputIdx, strerror(errno));
            }
            else
               SystemErrorMessage_Set(&pSysErrorText, errno, "failed to set input norm (v4l ioctl VIDIOC_S_STD): ", NULL);
         }
         else
            SystemErrorMessage_Set(&pSysErrorText, errno, "failed to switch input channel (v4l ioctl VIDIOC_S_INPUT): ", NULL);
      }
      else
      {
         if (pVbiBuf->cardIsDvb)
            SystemErrorMessage_Set(&pSysErrorText, 0, "Cannot tune analog TV channels using a digital TV card", NULL);
         else
            SystemErrorMessage_Set(&pSysErrorText, 0, "Cannot tune DVB frequency using analog TV card", NULL);
      }
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
// - note: assumes that VBI device is opened before
//
bool BtDriver_TuneChannel( int inputIdx, const EPGACQ_TUNER_PAR * pFreqPar, bool keepOpen, bool * pIsTuner )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   const char * pDevName;
   bool wasOpen;
   bool result = FALSE;

   if (video_fd == -1)
   {
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, pVbiBuf->cardIndex, pVbiBuf->cardIsDvb);
      video_fd = open(pDevName, O_RDWR);
      dprintf3("BtDriver-TuneChannel: opened %s, fd=%d, keep-open=%d\n", pDevName, video_fd, keepOpen);
      wasOpen = FALSE;

#if defined(VIDIOC_S_PRIORITY)
      if ((video_fd != -1) && (!pVbiBuf->cardIsDvb) &&
          (pVbiBuf->chnPrio != V4L2_PRIORITY_UNSET))
      {
         // this is a v4l2 device
         enum v4l2_priority prio = pVbiBuf->chnPrio;

         if (IOCTL(video_fd, VIDIOC_S_PRIORITY, &prio) != 0)
            debug4("ioctl VIDIOC_S_PRIORITY=%d failed on %s: %d, %s", pVbiBuf->chnPrio, pDevName, errno, strerror(errno));
      }
#endif  // VIDIOC_S_PRIORITY

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
      if ( BtDriver_SetInputSource(inputIdx, pFreqPar->norm, pIsTuner) || wasOpen )
      {
         if ( (wasOpen || *pIsTuner) && (pFreqPar->freq != 0) )
         {
            // Set the tuner frequency
            if (pFreqPar->norm == EPGACQ_TUNER_NORM_DVB)
            {
               struct dtv_property props[] =
               {
                  { .cmd = DTV_FREQUENCY, .u = { .data = pFreqPar->freq } }, // S: kHz, C+T: Hz
                  { .cmd = DTV_MODULATION, .u = { .data = pFreqPar->modulation } },
                  { .cmd = DTV_SYMBOL_RATE, .u = { .data = pFreqPar->symbolRate } },
                  //{ .cmd = DTV_INNER_FEC, .u = { .data = 0 } },
                  //{ .cmd = DTV_INVERSION, .u = { .data = 1 } },
                  { .cmd = DTV_TUNE, .u = { .data = 0 } }
               };
               struct dtv_properties prop =
               {
                  .num = sizeof(props) / sizeof(props[0]),
                  .props = props,
               };
               if (IOCTL(video_fd, FE_SET_PROPERTY, &prop) == 0)
               {
                  dprintf3("BtDriver-TuneChannel: set DVB freq %ld, mod:%d symb:%ld\n", pFreqPar->freq, pFreqPar->modulation, pFreqPar->symbolRate);
                  result = TRUE;

                  // DVB keep device open, else frontend stops working
                  keepOpen = TRUE;
               }
               else
                  SystemErrorMessage_Set(&pSysErrorText, errno, "failed to tune DVB FE (v4l ioctl FE_SET_PROPERTY): ", NULL);
            }
            else if (pFreqPar->norm != EPGACQ_TUNER_EXTERNAL)
            {
               struct v4l2_frequency vfreq2;

               memset(&vfreq2, 0, sizeof(vfreq2));
               if (IOCTL(video_fd, VIDIOC_G_FREQUENCY, &vfreq2) == 0)
               {
                  vfreq2.frequency = pFreqPar->freq;
                  if (IOCTL(video_fd, VIDIOC_S_FREQUENCY, &vfreq2) == 0)
                  {
                     dprintf1("BtDriver-TuneChannel: set to %.2f\n", (double)pFreqPar->freq/16);

                     result = TRUE;
                  }
                  else
                     SystemErrorMessage_Set(&pSysErrorText, errno, "failed to set tuner frequency (v4l ioctl VIDIOC_S_FREQUENCY): ", NULL);
               }
               else
                  SystemErrorMessage_Set(&pSysErrorText, errno, "failed to get tuner params (v4l ioctl VIDIOC_G_FREQUENCY): ", NULL);
            }
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

   // setting PID will succeed even if tuning failed
   if (pVbiBuf->cardIsDvb && (pFreqPar->norm == EPGACQ_TUNER_NORM_DVB))
   {
      BtDriver_TuneDvbPid(pFreqPar->ttxPid);
   }

#else // __NetBSD__ || __FreeBSD__
   char * pDevName;
   ulong lfreq;
   bool result = FALSE;

   if (devKeptOpen || BtDriver_SetInputSource(inputIdx, pFreqPar->norm, pIsTuner))
   {
      if ( (devKeptOpen || *pIsTuner) && (pFreqPar->freq != 0) )
      {
         if (tuner_fd == -1)
         {
           assert(devKeptOpen == FALSE);
           pDevName = BtDriver_GetDevicePath(DEV_TYPE_TUNER, pVbiBuf->cardIndex, FALSE);
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
            if(ioctl(tuner_fd, VIDIOCSFREQ, &pFreqPar->freq) == 0)
            {
               dprintf1("Vbi-TuneChannel: set to %.2f\n", (double)pFreqPar->freq/16);

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
   if ((result == FALSE) && (pSysErrorText != NULL))
      debug1("BtDriver-TuneChannel: failed: %s", pSysErrorText);

   return result;
}

// ---------------------------------------------------------------------------
// Query current tuner frequency
// - A difficulty for libzvbi and v4l1 drivers is that the VBI device is opened
//   by the slave process and cannot be accessed from the master, so the query
//   must be passed to the slave via IPC.
// - returns FALSE in case of error
//
bool BtDriver_QueryChannel( EPGACQ_TUNER_PAR * pFreqPar, uint * pInput, bool * pIsTuner )
{
   bool result = FALSE;
#if (!defined (__NetBSD__) && !defined (__FreeBSD__))
   char * pDevName;
   bool wasOpen;

   memset(pFreqPar, 0, sizeof(*pFreqPar));

   if ( (pVbiBuf != NULL) && (pFreqPar != NULL) && (pInput != NULL) && (pIsTuner != NULL) )
   {
      wasOpen = (video_fd != -1);
      if (wasOpen == FALSE)
      {
         pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, pVbiBuf->cardIndex, pVbiBuf->cardIsDvb);
         video_fd = open(pDevName, O_RDONLY);
         dprintf2("BtDriver-QueryChannel: opened (v4l2) %s, fd=%d\n", pDevName, video_fd);
      }
      if (video_fd != -1)
      {
         if (pVbiBuf->cardIsDvb)
         {
            struct dtv_property props[] =
            {
               { .cmd = DTV_FREQUENCY, .u = { .data = 0 } }, // S: kHz, C+T: Hz
               { .cmd = DTV_MODULATION, .u = { .data = 0 } },
               { .cmd = DTV_SYMBOL_RATE, .u = { .data = 0 } },
               //{ .cmd = DTV_INVERSION, .u = { .data = 0 } },
               //{ .cmd = DTV_INNER_FEC, .u = { .data = 0 } },
               //{ .cmd = DTV_BANDWIDTH_HZ, .u = { .data = 0 } },
            };
            struct dtv_properties prop =
            {
               .num = sizeof(props) / sizeof(props[0]),
               .props = props,
            };
            if (IOCTL(video_fd, FE_GET_PROPERTY, &prop) == 0)
            {
               pFreqPar->norm = EPGACQ_TUNER_NORM_DVB;
               pFreqPar->freq = props[0].u.data;
               pFreqPar->modulation = props[1].u.data;
               pFreqPar->symbolRate = props[2].u.data;

               // PID is configured by ourselves, so it makes no sense to query
               pFreqPar->ttxPid = 0;

               result = TRUE;
            }
            else
               debug1("BtDriver-QueryChannel: DVB FE_GET_PROPERTY error: %s", strerror(errno));

            *pIsTuner = TRUE;
         }
         else // V4L2
         {
            struct v4l2_frequency v4l2_freq;
            struct v4l2_input v4l2_desc_in;
            v4l2_std_id vstd2;
            int vinp2;

            if (IOCTL(video_fd, VIDIOC_G_INPUT, &vinp2) == 0)
            {
               *pInput = vinp2;
               *pIsTuner = TRUE;
               result = TRUE;

               memset(&v4l2_desc_in, 0, sizeof(v4l2_desc_in));
               v4l2_desc_in.index = vinp2;
               if (IOCTL(video_fd, VIDIOC_ENUMINPUT, &v4l2_desc_in) == 0)
                  *pIsTuner = ((v4l2_desc_in.type & V4L2_INPUT_TYPE_TUNER) != 0);
               else
                  debug2("BtDriver-QueryChannel: v4l2 VIDIOC_ENUMINPUT #%d error: %s", vinp2, strerror(errno));

               if (*pIsTuner)
               {
                  result = FALSE;
                  memset(&v4l2_freq, 0, sizeof(v4l2_freq));
                  if (IOCTL(video_fd, VIDIOC_G_FREQUENCY, &v4l2_freq) == 0)
                  {
                     pFreqPar->freq = v4l2_freq.frequency;
                     pFreqPar->norm = EPGACQ_TUNER_NORM_PAL;

                     if (v4l2_freq.type == V4L2_TUNER_ANALOG_TV)
                     {
                        // get TV norm set in the tuner (channel #0)
                        if (IOCTL(vbi_fdin, VIDIOC_G_STD, &vstd2) == 0)
                        {
                           if (vstd2 & V4L2_STD_PAL)
                           {
                              pFreqPar->norm = EPGACQ_TUNER_NORM_PAL;
                           }
                           else if (vstd2 & V4L2_STD_NTSC)
                           {
                              pFreqPar->norm = EPGACQ_TUNER_NORM_NTSC;
                           }
                           else if (vstd2 & V4L2_STD_SECAM)
                           {
                              pFreqPar->norm = EPGACQ_TUNER_NORM_SECAM;
                           }
                           else
                           {
                              debug1("BtDriver-QueryChannel: unknown standard 0x%X", (int)vstd2);
                           }
                           dprintf2("BtDriver-QueryChannel: VIDIOC_G_STD returned V4L2 norm 0x%0x, map to %d\n", (int)vstd2, pFreqPar->norm);
                        }
                        else
                           debug1("BtDriver-QueryChannel: VIDIOC_G_STD error: %s", strerror(errno));
                     }
                     result = TRUE;
                  }
                  else
                     debug1("BtDriver-QueryChannel: v4l2 VIDIOC_G_FREQUENCY error: %s", strerror(errno));
               }
               else
               {
                  pFreqPar->freq = 0;
                  pFreqPar->norm = EPGACQ_TUNER_EXTERNAL;
               }
               dprintf4("BtDriver-QueryChannel: fd=%d input=%d is-tuner?=%d freq=%ld\n", video_fd, *pInput, *pIsTuner, pFreqPar->freq);
            }
            else
               debug1("BtDriver-QueryChannel: v4l2 VIDIOC_G_INPUT error: %s", strerror(errno));
         }

         if (wasOpen == FALSE)
         {
            BtDriver_CloseDevice();
         }
      }
   }
#endif
   return result;
}

// ---------------------------------------------------------------------------
// Get signal strength on current tuner frequency
//
bool BtDriver_IsVideoPresent( void )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   bool result = FALSE;

   if ( video_fd != -1 )
   {
      if (pVbiBuf->cardIsDvb)
      {
         enum fe_status st;
         if (IOCTL(video_fd, FE_READ_STATUS, &st) == 0)
         {
            result = ((st & FE_HAS_LOCK) != 0);
            dprintf2("BtDriver-IsVideoPresent: status:%d lock?:%d\n", (int)st, result);
         }
         else
            debug1("BtDriver-IsVideoPresent: ioctl FE_READ_STATUS error: %s", strerror(errno));
      }
      else
      {
         struct v4l2_tuner vtuner2;

         memset(&vtuner2, 0, sizeof(vtuner2));
         if (IOCTL(video_fd, VIDIOC_G_TUNER, &vtuner2) == 0)
         {
            result = (vtuner2.signal >= 32768);
         }
         else
            debug1("BtDriver-IsVideoPresent: ioctl VIDIOC_G_TUNER error: %s", strerror(errno));
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
// Configure DVB demux PID
// - can be set independently even if tuning failed
// - actual configuration is executed within the slave thread
//
void BtDriver_TuneDvbPid( int pid )
{
   dprintf1("BtDriver-TuneDvbPid: pid:%d\n", pid);
   pVbiBuf->dvbPid = pid;

   // interrupt the slave thread if blocked in read()
   if (pVbiBuf->vbiSlaveRunning)
   {
      if (pthread_kill(vbi_thread_id, SIGUSR1) != 0)
         debug2("BtDriver-TuneDvbPid: failed to notify slave thread (%d) %s\n", errno, strerror(errno));
   }
}

// ---------------------------------------------------------------------------
// Determine default driver type upon first start
// - prefer DVB if device is present (assuming devfs)
//
BTDRV_SOURCE_TYPE BtDriver_GetDefaultDrvType( void )
{
   BTDRV_SOURCE_TYPE result;
   char * pDevName;

   pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, 0, TRUE);
   if (access(pDevName, F_OK) == 0)
      result = BTDRV_SOURCE_DVB;
   else
      result = BTDRV_SOURCE_ANALOG;

   return result;
}

// ---------------------------------------------------------------------------
// Query TV card name from a device with the given index
// - returns NULL if query fails and no devices with higher indices exist
//
const char * BtDriver_GetCardName( uint cardIndex, bool dvb )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   const char * pName = NULL;
   char * pDevName;
   #define MAX_CARD_NAME_LEN 32
   static char name[MAX_CARD_NAME_LEN];
   int fd;

   pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, cardIndex, dvb);
   // do not use video_fd as this may be different card and acq may be running
   fd = open(pDevName, O_RDONLY);
   if (fd != -1)
   {
      if (dvb)
      {
         struct dvb_frontend_info info;

         // FIXME this returns tuner name, not card name
         if (ioctl(fd, FE_GET_INFO, &info) == 0)
         {
            strncpy(name, (char*)info.name, MAX_CARD_NAME_LEN);
            name[MAX_CARD_NAME_LEN - 1] = 0;
            pName = (const char *) name;
         }
      }
      else  // V4L
      {
         struct v4l2_capability  v4l2_cap;

         memset(&v4l2_cap, 0, sizeof(v4l2_cap));
         if (IOCTL(fd, VIDIOC_QUERYCAP, &v4l2_cap) == 0)
         {
            strncpy(name, (char*)v4l2_cap.card, MAX_CARD_NAME_LEN);
            name[MAX_CARD_NAME_LEN - 1] = 0;
            pName = (const char *) name;
         }
      }

      close(fd);
   }
   else if (errno == EBUSY)
   {  // device exists, but is busy -> must not return NULL
      snprintf(name, MAX_CARD_NAME_LEN, "%s (device busy)", pDevName);
      pName = (const char *) name;
   }

   if (pName == NULL)
   {  // device file missing -> scan for devices with subsequent indices
      if (BtDriver_SearchDeviceFile(DEV_TYPE_VBI, cardIndex, dvb) != -1)
      {
         // more device files with higher indices follow -> return dummy for "gap"
         snprintf(name, MAX_CARD_NAME_LEN, "%s (no such device)", pDevName);
         pName = (const char *) name;
      }
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
//
const char * BtDriver_GetInputName( uint cardIndex, uint cardType, uint drvType, uint inputIdx )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   char * pDevName = NULL;
   const char * pName = NULL;
   #define MAX_INPUT_NAME_LEN 64
   static char name[MAX_INPUT_NAME_LEN];
   int fd;

   if (drvType == BTDRV_SOURCE_DVB)
   {
      if (inputIdx == 0)
          pName = "DVB antenna";
      // TODO multiple DVB front-ends
   }
   else
   {
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, cardIndex, FALSE);
      fd = open(pDevName, O_RDONLY);
      if (fd != -1)
      {
         struct v4l2_capability  v4l2_cap;
         struct v4l2_input v4l2_inp;

         memset(&v4l2_cap, 0, sizeof(v4l2_cap));
         if (IOCTL(fd, VIDIOC_QUERYCAP, &v4l2_cap) == 0)
         {
            memset(&v4l2_inp, 0, sizeof(v4l2_inp));
            v4l2_inp.index = inputIdx;
            if (IOCTL(fd, VIDIOC_ENUMINPUT, &v4l2_inp) == 0)
            {
               strncpy(name, (char*)v4l2_inp.name, MAX_INPUT_NAME_LEN);
               name[MAX_INPUT_NAME_LEN - 1] = 0;
               pName = (const char *) name;
            }
            else  // we iterate until an error is returned, hence ignore errors after input #0
               ifdebug4(inputIdx == 0, "BtDriver-GetInputName: ioctl(ENUMINPUT) for %s, input #%d failed with errno %d: %s", ((pDevName != NULL) ? pDevName : BtDriver_GetDevicePath(DEV_TYPE_VIDEO, cardIndex, FALSE)), inputIdx, errno, strerror(errno));
         }
      }
      close(fd);
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
// - tuner type and PLL etc. are already configured in the kernel
//   hence these parameters can be ignored in Linux
// - there isn't any need for priority adaptions, so that's not supported either
//
bool BtDriver_Configure( int cardIndex, int drvType, int prio, int chipType, int cardType,
                         int tunerType, int pllType, bool wdmStop )
{
   struct timeval tv;
   bool wasEnabled;

   wasEnabled = (pVbiBuf->scanEnabled || pVbiBuf->ttxEnabled) && !pVbiBuf->hasFailed;

   // pass the new card index to the slave via shared memory
   pVbiBuf->cardIndex = cardIndex;
   pVbiBuf->cardIsDvb = (drvType == BTDRV_SOURCE_DVB);

   if (wasEnabled)
   {  // wait 100ms for the slave to process the request
      tv.tv_sec  = 0;
      tv.tv_usec = 100000L;
      select(0, NULL, NULL, NULL, &tv);
   }

   // return FALSE if acq was disabled while processing the request
   return (!wasEnabled || !pVbiBuf->hasFailed);
}

// ---------------------------------------------------------------------------
// Set slicer type
// - note: slicer type "automatic" not allowed here:
//   type must be decided by upper layers
//
void BtDriver_SelectSlicer( VBI_SLICER_TYPE slicerType )
{
   if ((slicerType != VBI_SLICER_AUTO) && (slicerType < VBI_SLICER_COUNT))
   {
      dprintf1("BtDriver-SelectSlicer: slicer %d\n", slicerType);
      pVbiBuf->slicerType = slicerType;
   }
   else
      debug1("BtDriver-SelectSlicer: invalid slicer type %d", slicerType);
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
// Receive wake-up signal or ACK
// - do nothing
//
static void BtDriver_SignalWakeUp( int sigval )
{
   recvWakeUpSig = TRUE;
   signal(sigval, BtDriver_SignalWakeUp);
}

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
   sigset_t sigmask;

   if ((pVbiBuf != NULL) && (pVbiBuf->vbiSlaveRunning == FALSE))
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

   return result;
}

// ---------------------------------------------------------------------------
// Stop acquisition
//
void BtDriver_StopAcq( void )
{
   if (pVbiBuf->vbiSlaveRunning)
   {
      dprintf0("BtDriver-StopAcq: killing thread\n");

      acqShouldExit = TRUE;
      pthread_kill(vbi_thread_id, SIGUSR1);
      if (pthread_join(vbi_thread_id, NULL) != 0)
         perror("pthread_join");
   }
   else
      debug0("BtDriver-StopAcq: acq not running");
}

// ---------------------------------------------------------------------------
// Query error description for last failed operation
// - the returned pointer refers to internally managed memory and must not be freed
// - returns NULL if no error occurred
//
const char * BtDriver_GetLastError( void )
{
   char * pDevName;

   if (pVbiBuf != NULL)
   {
      if (pSysErrorText == NULL)
      {
         pDevName = BtDriver_GetDevicePath(DEV_TYPE_VBI, pVbiBuf->cardIndex, pVbiBuf->cardIsDvb);

         if (pVbiBuf->failureErrno == EBUSY)
            SystemErrorMessage_Set(&pSysErrorText, 0, "VBI device ", pDevName, " is busy (-> close all video, radio and teletext applications)", NULL);
         else if (pVbiBuf->failureErrno != 0)
            SystemErrorMessage_Set(&pSysErrorText, pVbiBuf->failureErrno, "access error ", pDevName, ": ", NULL);
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
   if (pVbiBuf != NULL)
   {
      xfree((void *) pVbiBuf);
      pVbiBuf = NULL;
   }
   SystemErrorMessage_Set(&pSysErrorText, 0, NULL);
}

// ---------------------------------------------------------------------------
// Determine the size of the VBI buffer
// - the buffer size depends on the number of VBI lines that are captured
//   for each frame; each read on the vbi device should request all the lines
//   of one frame, else the rest will be overwritten by the next frame
//
static void BtDriver_OpenVbiBuf( void )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   long bufSize;
   struct v4l2_format vfmt2;
   struct v4l2_format vfmt2_copy;
#endif

   if (vbiDvbPid != -1)
   {
      bufLines            = 1;
      bufLineSize         = 8*1024;

      if (zvbi_demux == NULL)
      {
         zvbi_demux = vbi_dvb_pes_demux_new();
      }
   }
   else
   {
      bufLines            = VBI_DEFAULT_LINES * 2;
      bufLineSize         = VBI_DEFAULT_BPL;

      // initialize "trivial" slicer
      VbiDecodeSetSamplingRate(0, 0);

      // initialize zvbi slicer
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

#if !defined (__NetBSD__) && !defined (__FreeBSD__)

      memset(&vfmt2, 0, sizeof(vfmt2));
      vfmt2.type = V4L2_BUF_TYPE_VBI_CAPTURE;

      if (IOCTL(vbi_fdin, VIDIOC_G_FMT, &vfmt2) == 0)
      {
         vfmt2_copy = vfmt2;
         // VBI format query succeeded -> now try to alter acc. to our preferences
         vfmt2.fmt.vbi.sample_format	= V4L2_PIX_FMT_GREY;
         vfmt2.fmt.vbi.offset		= 0;
         vfmt2.fmt.vbi.flags              &= V4L2_VBI_UNSYNC | V4L2_VBI_INTERLACED;
         vfmt2.fmt.vbi.start[0]		= 6;
         vfmt2.fmt.vbi.count[0]		= 17;
         vfmt2.fmt.vbi.start[1]		= 319;
         vfmt2.fmt.vbi.count[1]		= 17;
         if (IOCTL(vbi_fdin, VIDIOC_S_FMT, &vfmt2) != 0)
         {
            debug2("BtDriver-OpenVbiBuf: ioctl(VIDIOC_S_FMT) failed with errno %d: %s", errno, strerror(errno));
            vfmt2 = vfmt2_copy;
         }
         dprintf8("VBI format: lines %d+%d, %d samples per line, rate=%d, offset=%d, flags=0x%X, start lines %d,%d\n", vfmt2.fmt.vbi.count[0], vfmt2.fmt.vbi.count[1], vfmt2.fmt.vbi.samples_per_line, vfmt2.fmt.vbi.sampling_rate, vfmt2.fmt.vbi.offset, vfmt2.fmt.vbi.flags, vfmt2.fmt.vbi.start[0], vfmt2.fmt.vbi.start[1]);

         bufLines = vfmt2.fmt.vbi.count[0] + vfmt2.fmt.vbi.count[1];
         if (bufLines > VBI_MAX_LINENUM)
            bufLines = VBI_MAX_LINENUM;

         bufLineSize = vfmt2.fmt.vbi.samples_per_line;
         if ((bufLineSize == 0) || (bufLineSize > VBI_MAX_LINESIZE))
            bufLineSize = VBI_MAX_LINESIZE;

         VbiDecodeSetSamplingRate(vfmt2.fmt.vbi.sampling_rate, vfmt2.fmt.vbi.start[0]);

         zvbi_rd.sampling_rate     = vfmt2.fmt.vbi.sampling_rate;
         zvbi_rd.bytes_per_line    = vfmt2.fmt.vbi.samples_per_line;
         zvbi_rd.offset            = vfmt2.fmt.vbi.offset;
         zvbi_rd.start[0]          = vfmt2.fmt.vbi.start[0];
         zvbi_rd.count[0]          = vfmt2.fmt.vbi.count[0];
         zvbi_rd.start[1]          = vfmt2.fmt.vbi.start[1];
         zvbi_rd.count[1]          = vfmt2.fmt.vbi.count[1];
         zvbi_rd.interlaced        = !!(vfmt2.fmt.vbi.flags & V4L2_VBI_INTERLACED);
         zvbi_rd.synchronous       = !(vfmt2.fmt.vbi.flags & V4L2_VBI_UNSYNC);
      }
      else
      {
         ifdebug2(errno != EINVAL, "ioctl VIDIOCGVBIFMT error %d: %s", errno, strerror(errno));
         dprintf1("BTTV driver version 0x%X\n", IOCTL(vbi_fdin, BTTV_VERSION, NULL));

         bufSize = IOCTL(vbi_fdin, BTTV_VBISIZE, NULL);
         if (bufSize == -1)
         {
            perror("ioctl BTTV_VBISIZE");
         }
         else if ( (bufSize < VBI_DEFAULT_BPL) ||
                   (bufSize > VBI_MAX_LINESIZE * VBI_MAX_LINENUM) ||
                   ((bufSize % VBI_DEFAULT_BPL) != 0) )
         {
            fprintf(stderr, "BTTV_VBISIZE: illegal buffer size %ld\n", bufSize);
         }
         else
         {  // ok
            bufLines    = bufSize / VBI_DEFAULT_BPL;
            bufLineSize = VBI_DEFAULT_BPL;

            zvbi_rd.count[0] = (bufSize / VBI_DEFAULT_BPL) >> 1;
            zvbi_rd.count[1] = (bufSize / VBI_DEFAULT_BPL) - zvbi_rd.count[0];
         }
      }
#endif  // not NetBSD

      // pass parameters to zvbi slicer
      if (vbi_raw_decoder_add_services(&zvbi_rd, VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS, 0)
           != (VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS) )
      {
         fprintf(stderr, "Failed to initialize VBI slicer for teletext & VPS\n");
         pVbiBuf->failureErrno = errno;
         pVbiBuf->hasFailed = TRUE;
      }
   }

   assert(rawbuf == NULL);
   rawbuf = xmalloc(bufLines * bufLineSize);
}

// ---------------------------------------------------------------------------
// Decode all VBI lines of the last seen frame
//
static void BtDriver_DecodeFrame( void )
{
   uchar *pData;
   uint  line;
   size_t bufSize;
   ssize_t stat;

   #if defined(__NetBSD__) || defined(__FreeBSD__)
   // wait max. 10 seconds for the read to complete. After this time
   // close /dev/vbi in the signal handler to avoid endless blocking
   alarm(10);
   #endif

   // DVB demux is opened non-blocking because we do not know packet size per frame;
   // use select() for waiting until data is available (master sends signals upon events)
   if (vbiDvbPid != -1)
   {
      fd_set ins;
      FD_ZERO(&ins);
      FD_SET(vbi_fdin, &ins);

      int sel = select(vbi_fdin + 1, &ins, NULL, NULL, NULL);  // infinitly
      if (sel <= 0)
         return;
   }

   bufSize = bufLineSize * bufLines;
   stat = read(vbi_fdin, rawbuf, bufSize);

   #if defined(__NetBSD__) || defined(__FreeBSD__)
   alarm(0);
   #endif

   if ((vbiDvbPid != -1) && (stat > 0))
   {
      vbi_sliced sliced[128];
      const uint8_t * restPtr = (uint8_t*) rawbuf;
      unsigned int restBuf = stat;
      int64_t pts;

      while (restBuf > 0)
      {
         unsigned lineCount = vbi_dvb_demux_cor(zvbi_demux,
                                                sliced, sizeof(sliced) / sizeof(sliced[0]),
                                                &pts,
                                                &restPtr, &restBuf);
         if (lineCount > 0)
         {
            if (pts > zvbiLastTimestamp + (90000 * 1.5 * 1 / 25))   // PTS resolution 90kHz
               zvbiLastFrameNo += 2;
            else
               zvbiLastFrameNo += 1;
            zvbiLastTimestamp = pts;
            VbiDecodeStartNewFrame(zvbiLastFrameNo);

            for (line=0; line < lineCount; line++)
            {
               if ((sliced[line].id & VBI_SLICED_TELETEXT_B) != 0)
               {
                  TtxDecode_AddPacket(sliced[line].data + 0, sliced[line].line);
               }
               else if ((sliced[line].id & VBI_SLICED_VPS) != 0)
               {
                  TtxDecode_AddVpsData(sliced[line].data);
               }
               // else WSS, CAPTION: discard
            }
         }
      }
   }
   else if ((vbiDvbPid == -1) && (stat >= bufLineSize))
   {
      if (pVbiBuf->slicerType == VBI_SLICER_TRIVIAL)
      {
         #if !defined (__NetBSD__) && !defined (__FreeBSD__)
         // retrieve frame sequence counter from the end of the buffer
         VbiDecodeStartNewFrame(*(uint32_t *)(rawbuf + stat - 4));
         #else
         VbiDecodeStartNewFrame(0);
         #endif

         #ifndef SAA7134_0_2_2
         pData = rawbuf;
         for (line=0; line < (uint)stat/bufLineSize; line++, pData += bufLineSize)
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
      }
      else // if (pVbiBuf->slicerType == VBI_SLICER_ZVBI)
      {
         #if !defined (__NetBSD__) && !defined (__FreeBSD__)
         ZvbiSliceAndProcess(&zvbi_rd, rawbuf, *(uint32_t *)(rawbuf + stat - 4));
         #else
         ZvbiSliceAndProcess(&zvbi_rd, rawbuf, 0);
         #endif
      }
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
      debug2("BtDriver-DecodeFrame: short read: %ld of %ld", (long)stat, (long)bufSize);
   }
#if DUMP_TTX_PACKETS == ON
   fflush(stdout);
#endif
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
    pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, vbiCardIndex, FALSE);
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
   sigset_t sigmask;

   pVbiBuf->vbiSlaveRunning = TRUE;
   vbi_fdin = -1;
   #if defined(__NetBSD__) || defined(__FreeBSD__)
   video_fd = -1;
   #endif

   acqShouldExit = FALSE;
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

   while (acqShouldExit == FALSE)
   {
      if (pVbiBuf->hasFailed == FALSE)
      {
         #if !defined (__NetBSD__) && !defined (__FreeBSD__)
         if ((vbi_fdin != -1) &&
             ((vbiCardIndex != pVbiBuf->cardIndex) ||
              (vbiDvbPid != (pVbiBuf->cardIsDvb ? pVbiBuf->dvbPid : -1))))
         #else
         if ((vbi_fdin != -1) &&
             ((vbiCardIndex != pVbiBuf->cardIndex) ||
              (vbiInputIndex != pVbiBuf->inputIndex) ||
              (vbiDvbPid != (pVbiBuf->cardIsDvb ? pVbiBuf->dvbPid : -1))))
         #endif
         {  // switching to a different device -> close the previous one
            BtDriver_CloseVbi(vbi_fdin != -1);
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
      {  // acq was switched off -> close device
         // the thread terminates when acq is stopped
         acqShouldExit = TRUE;
      }
   }

   BtDriver_CloseVbi(FALSE);

   pVbiBuf->hasFailed = TRUE;
   pVbiBuf->vbiSlaveRunning = FALSE;

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
   signal(SIGUSR1, SIG_IGN);

   #if defined(__NetBSD__) || defined(__FreeBSD__)
   // install signal handler to implement read timeout on /dev/vbi
   signal(SIGALRM,  BtDriver_SignalAlarm);
   #endif

   pVbiBuf = xmalloc(sizeof(*pVbiBuf));
   memset((void *) pVbiBuf, 0, sizeof(EPGACQ_BUF));

   pVbiBuf->vbiSlaveRunning = FALSE;
   pVbiBuf->slicerType = VBI_SLICER_TRIVIAL;

   vbi_fdin = -1;
   video_fd = -1;

   pthread_cond_init(&vbi_start_cond, NULL);
   pthread_mutex_init(&vbi_start_mutex, NULL);

   return TRUE;
}
