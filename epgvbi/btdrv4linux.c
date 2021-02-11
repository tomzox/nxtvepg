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
 *  $Id: btdrv4linux.c,v 1.80 2021/02/11 20:40:47 tom Exp tom $
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

#ifdef USE_THREADS
# include <pthread.h>
#endif

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/syserrmsg.h"
#include "epgvbi/vbidecode.h"
#include "epgvbi/zvbidecoder.h"
#include "epgvbi/btdrv.h"

#ifndef USE_LIBZVBI
static vbi_raw_decoder   zvbi_rd;

#else // USE_LIBZVBI
# include "epgvbi/ttxdecode.h"
# include <libzvbi.h>
static vbi_capture     * pZvbiCapt;
static vbi_raw_decoder * pZvbiRawDec;
static vbi_sliced      * pZvbiData;
static double            zvbiLastTimestamp;
static ulong             zvbiLastFrameNo;
# define ZVBI_BUFFER_COUNT  10
# define ZVBI_TRACE          DEBUG_SWITCH_VBI
// automatically enable VBI proxy, if available
#if (VBI_VERSION_MAJOR>0) || (VBI_VERSION_MINOR>2) || (VBI_VERSION_MICRO >= 9)
# define USE_VBI_PROXY
#endif
#ifdef USE_VBI_PROXY
static vbi_proxy_client * pProxyClient;
typedef enum
{
   ZVBI_CHN_MSG_NONE,
   ZVBI_CHN_MSG_TOKEN_REQ,
   ZVBI_CHN_MSG_RELEASE_REQ,
   ZVBI_CHN_MSG_FAIL_REQ,
   ZVBI_CHN_MSG_DONE
} ZVBI_CHN_MSG;
#endif
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
// BSD doesn't know interrupted sys'calls, hence just declare IOCTL() macro transparent
# define IOCTL(fd, cmd, data)  ioctl(fd, cmd, data)

#else  // Linux
#include <linux/videodev2.h>
/* same as ioctl(), but repeat if interrupted */
#define IOCTL(fd, cmd, data)                                            \
({ int __result; do __result = ioctl(fd, cmd, data);                    \
   while ((__result == -1L) && (errno == EINTR) && (acqShouldExit == FALSE)); __result; })
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

#define VBI_MAX_LINENUM   (2*18)         // reasonable upper limits
#define VBI_MAX_LINESIZE (8*1024)
#define VBI_DEFAULT_BPL   2048

#define DEV_MAX_NAME_LEN 64

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
static int vbiDvbPid;
static int vbi_fdin;
#ifndef USE_LIBZVBI
static int bufLineSize;
static int bufLines;
static uchar *rawbuf = NULL;
#endif
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
   DEV_TYPE_DVB,
} BTDRV_DEV_TYPE;

// ---------------------------------------------------------------------------
// Get name of the specified device type
//
static char * BtDriver_GetDevicePath( BTDRV_DEV_TYPE devType, uint cardIdx )
{
   static __thread char devName[DEV_MAX_NAME_LEN + 1];
#if !defined(__NetBSD__) && !defined(__FreeBSD__)
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
               dprintf3("BtDriver-GetDevicePath: set device path %s (for device #%d, %s)\n", pDevPath, cardIdx, devName);
               pLastDevPath = pDevPath;
               break;
            }
         }
         break;

      case DEV_TYPE_DVB:
         snprintf(devName, DEV_MAX_NAME_LEN, "/dev/dvb/adapter%u/demux0", cardIdx);
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
         snprintf(devName, DEV_MAX_NAME_LEN, "/dev/vbi%u", cardIdx);
         break;
      case DEV_TYPE_VIDEO:
         snprintf(devName, DEV_MAX_NAME_LEN, "/dev/bktr%u", cardIdx);
         break;
      case DEV_TYPE_TUNER:
         snprintf(devName, DEV_MAX_NAME_LEN, "/dev/tuner%u", cardIdx);
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
static sint BtDriver_SearchDeviceFile( BTDRV_DEV_TYPE devType, uint cardIdx )
{
   DIR    *dir;
   struct dirent *entry;
   uint   try;
   int    scanLen;
   uint   devIdx;
   sint   result = -1;

   if ((devType == DEV_TYPE_VBI) || (devType == DEV_TYPE_VIDEO))
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

#if defined(USE_VBI_PROXY)
// ---------------------------------------------------------------------------
// Callback for proxy events
//
static void BtDriver_ProxyCallback( void * p_client_data, VBI_PROXY_EV_TYPE ev_mask )
{
   if (ev_mask & VBI_PROXY_EV_CHN_GRANTED)
   {
      dprintf0("BtDriver-ProxyCallback: token granted\n");
      if (pVbiBuf->slaveChnToken == FALSE)
         pVbiBuf->slaveChnTokenGrant = TRUE;
      pVbiBuf->slaveChnToken = TRUE;
   }
   if (ev_mask & VBI_PROXY_EV_CHN_RECLAIMED)
   {
      dprintf0("BtDriver-ProxyCallback: token reclaimed\n");
      pVbiBuf->slaveChnTokenGrant = FALSE;
      pVbiBuf->slaveChnToken = FALSE;

      vbi_proxy_client_channel_notify(pProxyClient, VBI_PROXY_CHN_TOKEN, 0);
   }
   if (ev_mask & VBI_PROXY_EV_CHN_CHANGED)
   {
      dprintf0("BtDriver-ProxyCallback: external channel change\n");
      pVbiBuf->chanChangeCnf -= 1;
   }
}
#endif  // defined(USE_VBI_PROXY)

// ---------------------------------------------------------------------------
// Query if VBI device has been freed by higher-prio users
//
bool BtDriver_QueryChannelToken( void )
{
#if defined(USE_VBI_PROXY)
   bool result;

   if (pVbiBuf != NULL)
   {
      result = pVbiBuf->slaveChnToken && pVbiBuf->slaveChnTokenGrant;

      if (pVbiBuf->slaveChnTokenGrant)
         pVbiBuf->slaveChnTokenGrant = FALSE;
   }
   else
      result = FALSE;

   return result;
#else
   return FALSE;
#endif
}

// ---------------------------------------------------------------------------
// Set channel priority for next channel change
//
void BtDriver_SetChannelProfile( VBI_CHANNEL_PRIO_TYPE prio,
                                 int subPrio, int duration, int minDuration )
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
#if defined(USE_VBI_PROXY)
      // parameters for channel scheduling in proxy daemon
      pVbiBuf->chnProfValid   = TRUE;
      pVbiBuf->chnSubPrio     = subPrio;
      pVbiBuf->chnMinDuration = duration;
      pVbiBuf->chnExpDuration = minDuration;
#endif
   }
}

// ---------------------------------------------------------------------------
// Open the VBI device
//
static void BtDriver_OpenVbi( void )
{
   char * pDevName;
   char tmpName[DEV_MAX_NAME_LEN];
   FILE *fp;
#ifdef USE_LIBZVBI
   char * pErrStr;
   unsigned services;
#endif

   vbiCardIndex = pVbiBuf->cardIndex;
   vbiDvbPid = pVbiBuf->dvbPid;

   #if defined(__NetBSD__) || defined(__FreeBSD__)
   // the bktr driver on NetBSD requires to start video capture for VBI to work
   vbiInputIndex = pVbiBuf->inputIndex;
   pVbiBuf->tv_cards[pVbiBuf->cardIndex].inUse=TRUE;
   BtDriver_ScanDevices(FALSE);
   if (BtDriver_StartCapture())
   #endif
   {
#ifndef USE_LIBZVBI
      if (vbiDvbPid == -1)
      {
         pDevName = BtDriver_GetDevicePath(DEV_TYPE_VBI, vbiCardIndex);

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
         else
            debug2("VBI open %s failed: errno=%d", pDevName, errno);
      }
      else
      {  // DVB not supported without ZVBI
         SystemErrorMessage_Set(&pSysErrorText, 0, "DVB is not supported because nxtvepg was compiled without ZVBI library (see Makefile)", NULL);
         errno = EINVAL;
      }

#else  // USE_LIBZVBI
      pErrStr = NULL;
      if (vbiDvbPid != -1)
      {
         pDevName = BtDriver_GetDevicePath(DEV_TYPE_DVB, vbiCardIndex);
         services = VBI_SLICED_TELETEXT_B;
         pZvbiCapt = vbi_capture_dvb_new2(pDevName, vbiDvbPid, &pErrStr, ZVBI_TRACE);
      }
      else
      {
         pDevName = BtDriver_GetDevicePath(DEV_TYPE_VBI, vbiCardIndex);
         services = VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS;
#ifdef USE_VBI_PROXY
         pProxyClient = vbi_proxy_client_create(pDevName, "nxtvepg", 0,
                                                &pErrStr, ZVBI_TRACE);
         if (pProxyClient != NULL)
         {
            pZvbiCapt = vbi_capture_proxy_new(pProxyClient, ZVBI_BUFFER_COUNT, 0, &services, 0, &pErrStr);
            if (pZvbiCapt != NULL)
            {
               vbi_proxy_client_set_callback(pProxyClient, BtDriver_ProxyCallback, NULL);
            }
            else
            {  // failed to connect to proxy
               vbi_proxy_client_destroy(pProxyClient);
               pProxyClient = NULL;
            }
         }
         pVbiBuf->slaveVbiProxy = (pProxyClient != NULL);
         if (pZvbiCapt == NULL)
#endif  // USE_VBI_PROXY
         {
            services = VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS;
            pZvbiCapt = vbi_capture_v4l2_new(pDevName, ZVBI_BUFFER_COUNT, &services, 0, &pErrStr, ZVBI_TRACE);
         }
         if (pZvbiCapt == NULL)
         {
            services = VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS;
            pZvbiCapt = vbi_capture_v4l_new(pDevName, 0, &services, 0, &pErrStr, ZVBI_TRACE);
         }
#if defined (USE_VBI_PROXY) && defined (VIDIOC_S_PRIORITY)
         if ((pZvbiCapt != NULL) && (pProxyClient == NULL))
         {
            // set device user priority to "background" -> channel swtiches will fail while
            // higher-priority users (e.g. an interactive TV app) have opened the device
            enum v4l2_priority prio = V4L2_PRIORITY_BACKGROUND;
            int fd = vbi_capture_fd(pZvbiCapt);
            if (IOCTL(fd, VIDIOC_S_PRIORITY, &prio) != 0)
               debug4("ioctl VIDIOC_S_PRIORITY=%d (background) failed on %s: %d, %s", V4L2_PRIORITY_BACKGROUND, pDevName, errno, strerror(errno));
         }
#endif
      }

      if (pZvbiCapt != NULL)
      {
         pZvbiRawDec = vbi_capture_parameters(pZvbiCapt);
         if ((pZvbiRawDec != NULL) && ((services & VBI_SLICED_TELETEXT_B) != 0))
         {
            pZvbiData = xmalloc((pZvbiRawDec->count[0] + pZvbiRawDec->count[1]) * sizeof(*pZvbiData));
            zvbiLastTimestamp = 0.0;
            zvbiLastFrameNo = 0;
            vbi_fdin = vbi_capture_fd(pZvbiCapt);
         }
         else
         {
            vbi_capture_delete(pZvbiCapt);
            pZvbiCapt = NULL;
            pZvbiRawDec = NULL;  // no delete/free required
            pErrStr = strdup("The ZVBI library failed initialize the teletext decoder.");
            errno = EINVAL;
         }
      }

      if (pErrStr != NULL)
      {  // re-allocate the error string with internal malloc func
         SystemErrorMessage_Set(&pSysErrorText, 0, pErrStr, NULL);
         free(pErrStr);  // not xFree: allocated by libzvbi
      }
#endif  // USE_LIBZVBI
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
      dprintf1("BtDriver-CloseVbi: fd %d\n", vbi_fdin);

      sprintf(tmpName, PIDFILENAME, vbiCardIndex);
      unlink(tmpName);

#ifndef USE_LIBZVBI
      // free slicer buffer and pattern array
      ZvbiSliceAndProcess(NULL, NULL, 0);
      vbi_raw_decoder_destroy(&zvbi_rd);

      close(vbi_fdin);
      vbi_fdin = -1;
      xfree(rawbuf);
      rawbuf = NULL;
#else
      if (pZvbiData != NULL)
         xfree(pZvbiData);
      pZvbiData = NULL;
      if (pZvbiCapt != NULL)
         vbi_capture_delete(pZvbiCapt);
      pZvbiCapt = NULL;
# ifdef USE_VBI_PROXY
      if (pProxyClient != NULL)
         vbi_proxy_client_destroy(pProxyClient);
      pProxyClient = NULL;
# endif
      vbi_fdin = -1;
#endif  // USE_LIBZVBI

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
static bool BtDriver_SetInputSource( int inputIdx, int norm, bool * pIsTuner )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   bool isTuner = FALSE;
   bool result = FALSE;

   if (video_fd != -1)
   {
      if (pVbiBuf->dvbPid != -1)
      {
         dprintf0("BtDriver-SetInputSource: not supported for DVB\n");
      }
      else  // V4L2
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

#if defined(USE_VBI_PROXY)
// ---------------------------------------------------------------------------
// Pass channel change command to VBI slave process/thread
// - only required for libzvbi, where the slave owns the slicer context
//
static void BtDriver_MasterTuneChannel( ZVBI_CHN_MSG cmd )
{
   if ((pVbiBuf != NULL) && (pVbiBuf->vbiPid != -1))
   {
      assert(pVbiBuf->slaveChnSwitch == ZVBI_CHN_MSG_NONE);

      #ifdef USE_THREADS
      pthread_mutex_lock(&vbi_start_mutex);
      #endif
      pVbiBuf->slaveChnSwitch = cmd;

      recvWakeUpSig = FALSE;
      #ifdef USE_THREADS
      // wake the process/thread up from being blocked in read (Linux only)
      if (pthread_kill(vbi_thread_id, SIGUSR1) == 0)
      {
         struct timespec tsp;
         struct timeval tv;

         gettimeofday(&tv, NULL);
         tv.tv_usec += 1000 * 1000L;
         if (tv.tv_usec > 1000 * 1000L)
         {
            tv.tv_sec  += 1;
            tv.tv_usec -= 1000 * 1000;
         }
         tsp.tv_sec  = tv.tv_sec;
         tsp.tv_nsec = tv.tv_usec * 1000;

         // wait for signal from slave on condition variable
         pthread_cond_timedwait(&vbi_start_cond, &vbi_start_mutex, &tsp);
      }
      #else  // not USE_THREADS
      if (kill(pVbiBuf->vbiPid, SIGUSR1) != -1)
      {
         if ((recvWakeUpSig == FALSE) && (pVbiBuf->slaveChnSwitch == cmd))
         {
            struct timeval tv;
            int repeat;

            for (repeat = 5; (repeat > 0) && (pVbiBuf->slaveChnSwitch != ZVBI_CHN_MSG_DONE); repeat--)
            {
               tv.tv_sec = 0;
               tv.tv_usec = 200*1000;
               select(0, NULL, NULL, NULL, &tv);
            }
         }
      }
      #endif
      pVbiBuf->slaveChnSwitch = ZVBI_CHN_MSG_NONE;

      #ifdef USE_THREADS
      pthread_mutex_unlock(&vbi_start_mutex);
      #endif
   }
}

// ---------------------------------------------------------------------------
// Channel change signaling between proxy daemon and acq slave
//
static void BtDriver_SlaveTuneChannel( void )
{
   vbi_channel_profile prof;

   #ifdef USE_THREADS
   pthread_mutex_lock(&vbi_start_mutex);
   #endif

   if (pProxyClient != NULL)
   {
      if (pVbiBuf->slaveChnSwitch == ZVBI_CHN_MSG_TOKEN_REQ)
      {
         dprintf0("BtDriver-SlaveTuneChannel: requesting channel token\n");
         assert(pVbiBuf->chnPrio != V4L2_PRIORITY_UNSET);

         memset(&prof, 0, sizeof(prof));
         prof.is_valid     = pVbiBuf->chnProfValid;
         prof.sub_prio     = pVbiBuf->chnSubPrio;
         prof.min_duration = pVbiBuf->chnMinDuration;
         prof.exp_duration = pVbiBuf->chnExpDuration;

         if (vbi_proxy_client_channel_request(pProxyClient, pVbiBuf->chnPrio, &prof) > 0)
         {
            dprintf0("BtDriver-SlaveTuneChannel: token granted\n");
            pVbiBuf->slaveChnToken = TRUE;
         }
      }
      else if (pVbiBuf->slaveChnSwitch == ZVBI_CHN_MSG_RELEASE_REQ)
      {
         dprintf0("BtDriver-SlaveTuneChannel: releasing token after sucessful switch\n");

         vbi_proxy_client_channel_notify(pProxyClient, VBI_PROXY_CHN_TOKEN | VBI_PROXY_CHN_FLUSH, 0);
         pVbiBuf->slaveChnToken = FALSE;
      }
      else if (pVbiBuf->slaveChnSwitch == ZVBI_CHN_MSG_FAIL_REQ)
      {
         dprintf0("BtDriver-SlaveTuneChannel: releasing channel after failed switch\n");

         vbi_proxy_client_channel_notify(pProxyClient, VBI_PROXY_CHN_FAIL, 0);
         pVbiBuf->slaveChnToken = FALSE;
      }
   }
   else
      fprintf(stderr, "Cannot switch channel: libzvbi not initialized\n");

   pVbiBuf->slaveChnSwitch = ZVBI_CHN_MSG_DONE;

   #ifdef USE_THREADS
   pthread_cond_signal(&vbi_start_cond);
   pthread_mutex_unlock(&vbi_start_mutex);
   #else  // not USE_THREADS
   kill(pVbiBuf->epgPid, SIGUSR1);
   #endif
}
#endif  // defined(USE_VBI_PROXY)

// ---------------------------------------------------------------------------
// Set the input channel and tune a given frequency and norm
// - input source is only set upon the first call when the device is kept open
//   also note that the isTuner flag is only set upon the first call
// - note: assumes that VBI device is opened before
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

#if defined(USE_VBI_PROXY)
   if (pVbiBuf->slaveVbiProxy && !pVbiBuf->slaveChnToken)
   {
      BtDriver_MasterTuneChannel(ZVBI_CHN_MSG_TOKEN_REQ);
      if ((pVbiBuf->slaveChnToken == FALSE) && (pVbiBuf->chnPrio == VBI_CHN_PRIO_BACKGROUND))
         return FALSE;
   }
#endif

   if (video_fd == -1)
   {
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, pVbiBuf->cardIndex);
      video_fd = open(pDevName, O_RDONLY);
      dprintf3("BtDriver-TuneChannel: opened %s, fd=%d, keep-open=%d\n", pDevName, video_fd, keepOpen);
      wasOpen = FALSE;

#if defined(VIDIOC_S_PRIORITY)
      if ((video_fd != -1) &&
          (pVbiBuf->chnPrio != V4L2_PRIORITY_UNSET))
      {
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
      if ( BtDriver_SetInputSource(inputIdx, norm, pIsTuner) || wasOpen )
      {
         if ( (wasOpen || *pIsTuner) && (lfreq != 0) )
         {
            // Set the tuner frequency
            {
               struct v4l2_frequency vfreq2;

               memset(&vfreq2, 0, sizeof(vfreq2));
               if (IOCTL(video_fd, VIDIOC_G_FREQUENCY, &vfreq2) == 0)
               {
                  vfreq2.frequency = lfreq;
                  if (IOCTL(video_fd, VIDIOC_S_FREQUENCY, &vfreq2) == 0)
                  {
                     dprintf1("BtDriver-TuneChannel: set to %.2f\n", (double)lfreq/16);

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

#if defined(USE_VBI_PROXY)
   if (pVbiBuf->slaveVbiProxy)
   {
      BtDriver_MasterTuneChannel(result ? ZVBI_CHN_MSG_RELEASE_REQ : ZVBI_CHN_MSG_FAIL_REQ);
      pVbiBuf->slaveChnToken = FALSE;
   }
#endif

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
bool BtDriver_QueryChannel( uint * pFreq, uint * pInput, bool * pIsTuner )
{
#if (!defined (__NetBSD__) && !defined (__FreeBSD__)) || defined(USE_LIBZVBI)
   char * pDevName;
   bool wasOpen;
   bool result = FALSE;

   if ( (pVbiBuf != NULL) && (pFreq != NULL) && (pInput != NULL) && (pIsTuner != NULL) )
   {
      if (pVbiBuf->dvbPid != -1)
      {
         dprintf0("BtDriver-QueryChannel: not supported for DVB\n");
         *pFreq = 0;
         *pInput = 0;
         *pIsTuner = FALSE;
         result = TRUE;
      }
      else  // V4L2
      {
         wasOpen = (video_fd != -1);
         if (wasOpen == FALSE)
         {
            pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, pVbiBuf->cardIndex);
            video_fd = open(pDevName, O_RDONLY);
            dprintf2("BtDriver-QueryChannel: opened (v4l2) %s, fd=%d\n", pDevName, video_fd);
         }
         if (video_fd != -1)
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
                     if (v4l2_freq.type == V4L2_TUNER_ANALOG_TV)
                     {
                        *pFreq = v4l2_freq.frequency;

                        // get TV norm set in the tuner (channel #0)
                        if (IOCTL(vbi_fdin, VIDIOC_G_STD, &vstd2) == 0)
                        {
                           if (vstd2 & V4L2_STD_PAL)
                           {
                              *pFreq |= EPGACQ_TUNER_NORM_PAL << 24;
                           }
                           else if (vstd2 & V4L2_STD_NTSC)
                           {
                              *pFreq |= EPGACQ_TUNER_NORM_NTSC << 24;
                           }
                           else if (vstd2 & V4L2_STD_SECAM)
                           {
                              *pFreq |= EPGACQ_TUNER_NORM_SECAM << 24;
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
                  *pFreq = 0;

               dprintf4("BtDriver-QueryChannel: fd=%d input=%d is-tuner?=%d freq=%ld\n", video_fd, *pInput, *pIsTuner, pFreqPar->freq);
            }
            else
               debug1("BtDriver-QueryChannel: v4l2 VIDIOC_G_INPUT error: %s", strerror(errno));

            if (wasOpen == FALSE)
            {
               dprintf1("BtDriver-QueryChannel: closing fd=%d\n", video_fd);
               close(video_fd);
               video_fd = -1;
            }
         }
      }
   }
   return result;

#else  // (NetBSD || FreeBSD) && !LIBZVBI
   char * pDevName;
   ulong lfreq;
   bool  result = FALSE;

   if (pVbiBuf != NULL)
   {
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
            if (pFreq != NULL)
               *pFreq = (uint)lfreq;

            if (pInput != NULL)
               *pInput = pVbiBuf->inputIndex;  // XXX FIXME should query driver instead
            if (pIsTuner != NULL)
               *pIsTuner = pVbiBuf->tv_cards[pVbiBuf->cardIndex].inputs[pVbiBuf->inputIndex].isTuner;

            result = TRUE;
         }
         else
            perror("VIDIOCGFREQ");

         dprintf1("BtDriver-QueryChannel: closing video device, fd=%d\n", tuner_fd);
         BtDriver_CloseDevice();
      }
   }

   return result;
#endif  // (NetBSD || FreeBSD) && !LIBZVBI
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
      {
         struct v4l2_tuner vtuner2;

         memset(&vtuner2, 0, sizeof(vtuner2));
         if (IOCTL(video_fd, VIDIOC_G_TUNER, &vtuner2) == 0)
         {
            result = (vtuner2.signal >= 32768);
         }
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
// Enable DVB passive mode and pass DVB PID
// - note: must only be called during program start
//
bool BtDriver_SetDvbPid( int pid )
{
   pVbiBuf->dvbPid = pid;

   return TRUE;
}

// ---------------------------------------------------------------------------
// Query TV card name from a device with the given index
// - returns NULL if query fails and no devices with higher indices exist
//
const char * BtDriver_GetCardName( uint cardIndex )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   const char * pName = NULL;
   char * pDevName;
   #define MAX_CARD_NAME_LEN 32
   static char name[MAX_CARD_NAME_LEN];

   if (video_fd != -1)
   {
      dprintf1("BtDriver-GetCardName: closing video device, fd=%d\n", video_fd);
      BtDriver_CloseDevice();
   }
   pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, cardIndex);
   video_fd = open(pDevName, O_RDONLY);

   if (video_fd != -1)
   {
      struct v4l2_capability  v4l2_cap;

      memset(&v4l2_cap, 0, sizeof(v4l2_cap));
      if (IOCTL(video_fd, VIDIOC_QUERYCAP, &v4l2_cap) == 0)
      {
         strncpy(name, (char*)v4l2_cap.card, MAX_CARD_NAME_LEN);
         name[MAX_CARD_NAME_LEN - 1] = 0;
         pName = (const char *) name;
      }

      BtDriver_CloseDevice();
   }
   else if (errno == EBUSY)
   {  // device exists, but is busy -> must not return NULL
      snprintf(name, MAX_CARD_NAME_LEN, "%s (device busy)", pDevName);
      pName = (const char *) name;
   }

   if (pName == NULL)
   {  // device file missing -> scan for devices with subsequent indices
      if (BtDriver_SearchDeviceFile(DEV_TYPE_VBI, cardIndex) != -1)
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
// - video device is kept open inbetween calls and only closed upon final call
//
const char * BtDriver_GetInputName( uint cardIndex, uint cardType, uint drvType, uint inputIdx )
{
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   char * pDevName = NULL;
   const char * pName = NULL;
   #define MAX_INPUT_NAME_LEN 32
   static char name[MAX_INPUT_NAME_LEN];

   if (video_fd == -1)
   {
      pDevName = BtDriver_GetDevicePath(DEV_TYPE_VIDEO, cardIndex);
      video_fd = open(pDevName, O_RDONLY);
      dprintf2("BtDriver-GetInputName: opened %s, fd=%d\n", pDevName, video_fd);
   }

   if (video_fd != -1)
   {
      struct v4l2_capability  v4l2_cap;
      struct v4l2_input v4l2_inp;

      memset(&v4l2_cap, 0, sizeof(v4l2_cap));
      if (IOCTL(video_fd, VIDIOC_QUERYCAP, &v4l2_cap) == 0)
      {
         memset(&v4l2_inp, 0, sizeof(v4l2_inp));
         v4l2_inp.index = inputIdx;
         if (IOCTL(video_fd, VIDIOC_ENUMINPUT, &v4l2_inp) == 0)
         {
            strncpy(name, (char*)v4l2_inp.name, MAX_INPUT_NAME_LEN);
            name[MAX_INPUT_NAME_LEN - 1] = 0;
            pName = (const char *) name;
         }
         else  // we iterate until an error is returned, hence ignore errors after input #0
            ifdebug4(inputIdx == 0, "BtDriver-GetInputName: ioctl(ENUMINPUT) for %s, input #%d failed with errno %d: %s", ((pDevName != NULL) ? pDevName : BtDriver_GetDevicePath(DEV_TYPE_VIDEO, cardIndex)), inputIdx, errno, strerror(errno));
      }
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
// - tuner type and PLL etc. are already configured in the kernel
//   hence these parameters can be ignored in Linux
// - there isn't any need for priority adaptions, so that's not supported either
//
bool BtDriver_Configure( int cardIndex, int drvType, int prio, int chipType, int cardType,
                         int tunerType, int pllType, bool wdmStop )
{
   struct timeval tv;
   bool wasEnabled;

   wasEnabled = (pVbiBuf->epgEnabled || pVbiBuf->ttxEnabled) && !pVbiBuf->hasFailed;

   // pass the new card index to the slave via shared memory
   pVbiBuf->cardIndex = cardIndex;

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
#ifndef USE_LIBZVBI
   if ((slicerType != VBI_SLICER_AUTO) && (slicerType < VBI_SLICER_COUNT))
   {
      dprintf1("BtDriver-SelectSlicer: slicer %d\n", slicerType);
      pVbiBuf->slicerType = slicerType;
   }
   else
      debug1("BtDriver-SelectSlicer: invalid slicer type %d", slicerType);
#endif
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
// The Acquisition process bows out on the usual signals
//
#ifndef USE_THREADS
static void BtDriver_SignalHandler( int sigval )
{
   dprintf2("Received signal %d in %s process\n", sigval, isVbiProcess ? "VBI" : "EPG");
   acqShouldExit = TRUE;
   signal(sigval, BtDriver_SignalHandler);
}
#endif

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
         {  // slave died from an uncaught signal (e.g. SIGKILL)
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
   int repeat;

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
            for (repeat = 5; (repeat > 0) && (pVbiBuf->hasFailed == FALSE); repeat--)
            {
               tv.tv_sec = 0;
               tv.tv_usec = 200*1000;
               select(0, NULL, NULL, NULL, &tv);
            }
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
         if (kill(pVbiBuf->vbiPid, SIGUSR1) != 0)
         {
            debug3("BtDriver-StopAcq: failed to wake up child process %d: errno %d: %s", pVbiBuf->vbiPid, errno, strerror(errno));
            pVbiBuf->vbiPid = -1;
         }
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
   char * pDevName;

   if (pVbiBuf != NULL)
   {
      if (pSysErrorText == NULL)
      {
         if (pVbiBuf->dvbPid != -1)
           pDevName = BtDriver_GetDevicePath(DEV_TYPE_DVB, pVbiBuf->cardIndex);
         else
           pDevName = BtDriver_GetDevicePath(DEV_TYPE_VBI, pVbiBuf->cardIndex);

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
#ifndef USE_THREADS
   struct timeval tv;
   uint max_wait;
   uint wret;
   struct shmid_ds stat;

   if (pVbiBuf != NULL)
   {
      if ((isVbiProcess == FALSE) && (pVbiBuf->vbiPid != -1))
      {  // this is the parent process -> kill the child process first
         dprintf1("BtDriver-Exit: terminate child process %d\n", pVbiBuf->vbiPid);

         signal(SIGCHLD, SIG_DFL);
         if (kill(pVbiBuf->vbiPid, SIGTERM) == 0)
         {
            max_wait = 10;
            while ((pVbiBuf->vbiPid != -1) && (max_wait > 0))
            {
               dprintf0("BtDriver-Exit: waiting for child process...\n");
               wret = waitpid(pVbiBuf->vbiPid, NULL, WNOHANG);
               if (wret == 0)
               {  // sleep until child signals its death
                  tv.tv_sec =  0;
                  tv.tv_usec = 100 * 1000;
                  select(0, NULL, NULL, NULL, &tv);
                  max_wait -= 1;
               }
               else
                  max_wait = 0;
            }
         }
         else
         {
            debug3("BtDriver-Exit: failed to kill child process %d: errno %d: %s", pVbiBuf->vbiPid, errno, strerror(errno));
            pVbiBuf->vbiPid = -1;
         }
      }

      if (isVbiProcess == FALSE)
      {
         pVbiBuf->epgPid = -1;
      }
      else
         dprintf1("BtDriver-Exit: child process %d terminated\n", (int)getpid());

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
         //else
         //   dprintf0("BtDriver-CheckParent: parent is alive\n");
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
#ifndef USE_LIBZVBI
#if !defined (__NetBSD__) && !defined (__FreeBSD__)
   long bufSize;
   struct v4l2_format vfmt2;
   struct v4l2_format vfmt2_copy;
#endif

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
      ifdebug2(errno != EINVAL, "ioctl VIDIOC_G_FMT error %d: %s", errno, strerror(errno));
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

   rawbuf = xmalloc(bufLines * bufLineSize);

#endif  // not USE_LIBZVBI
}

// ---------------------------------------------------------------------------
// Decode all VBI lines of the last seen frame
//
static void BtDriver_DecodeFrame( void )
{
#ifndef USE_LIBZVBI
   uchar *pData;
   uint  line;
   size_t bufSize;
   ssize_t stat;

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
#else  // USE_LIBZVBI
   double timestamp;
   struct timeval timeout;
   int  lineCount;
   int  line;
   int  res;

   #if defined(__NetBSD__) || defined(__FreeBSD__)
   // wait max. 10 seconds for the read to complete. After this time
   // close /dev/vbi in the signal handler to avoid endless blocking
   alarm(10);
   #endif

   // need a small timeout as the following function does not return upo SIGUSR1
   // as LIBZVBI internally retries the read upon EINTR
   timeout.tv_sec =  1;
   timeout.tv_usec = 0;
   res = vbi_capture_read_sliced(pZvbiCapt, pZvbiData, &lineCount, &timestamp, &timeout);
   if (res > 0)
   {
      if (timestamp - zvbiLastTimestamp > (1.5 * 1 / 25))
         zvbiLastFrameNo += 2;
      else
         zvbiLastFrameNo += 1;
      zvbiLastTimestamp = timestamp;
      VbiDecodeStartNewFrame(zvbiLastFrameNo);

      for (line=0; line < lineCount; line++)
      {  // dump all TTX packets, even non-EPG ones
         if ((pZvbiData[line].id & VBI_SLICED_TELETEXT_B) != 0)
         {
            TtxDecode_AddPacket(pZvbiData[line].data + 0, pZvbiData[line].line);
         }
         else if ((pZvbiData[line].id & VBI_SLICED_VPS) != 0)
         {
            TtxDecode_AddVpsData(pZvbiData[line].data);
         }
         else if (vbiDvbPid == -1)
            debug1("BtDriver-DecodeFrame: unrequested service 0x%x", pZvbiData[line].id);
      }
   }
   else if (res < 0)
   {  // I/O error
      pVbiBuf->failureErrno = errno;
      pVbiBuf->hasFailed = TRUE;
      debug2("BtDriver-DecodeFrame: read error %d (%s)", pVbiBuf->failureErrno, strerror(pVbiBuf->failureErrno));
   }
#endif  // USE_LIBZVBI
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
   lastReaderIdx = pVbiBuf->reader_idx;
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
         if ((vbi_fdin != -1) &&
             ((vbiCardIndex != pVbiBuf->cardIndex) || (vbiDvbPid != pVbiBuf->dvbPid)))
         #else
         if ((vbi_fdin != -1) &&
             ((vbiCardIndex != pVbiBuf->cardIndex) ||
              (vbiInputIndex != pVbiBuf->inputIndex) ||
              (vbiDvbPid != pVbiBuf->dvbPid)))
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
            lastReaderIdx = pVbiBuf->reader_idx;
            #endif
         }
      }
      else
      {  // acq was switched off -> close device
         #ifndef USE_THREADS
         BtDriver_CloseVbi();

         // sleep until signal; check parent every 30 secs
         if (acqShouldExit == FALSE)
         {
            tv.tv_sec = 30;
            tv.tv_usec = 0;
            select(0, NULL, NULL, NULL, &tv);
            BtDriver_CheckParent();
         }
         #else
         // the thread terminates when acq is stopped
         acqShouldExit = TRUE;
         #endif
      }

#if defined(USE_VBI_PROXY)
      if ( (pVbiBuf->slaveChnSwitch != ZVBI_CHN_MSG_NONE) &&
           (pVbiBuf->slaveChnSwitch != ZVBI_CHN_MSG_DONE) )
      {
         BtDriver_SlaveTuneChannel();
      }
#endif  // USE_VBI_PROXY
   }

   BtDriver_CloseVbi();

   pVbiBuf->hasFailed = TRUE;

   #ifdef USE_THREADS
   pVbiBuf->vbiPid = -1;
   #else
   if (pVbiBuf->epgEnabled || pVbiBuf->ttxEnabled)
   {  // notify the parent that acq has stopped (e.g. after SIGTERM)
      dprintf1("BtDriver-Main: acq slave exiting - signalling parent %d\n", pVbiBuf->epgPid);
      kill(pVbiBuf->epgPid, SIGHUP);
   }
   else
      dprintf0("BtDriver-Main: acq slave exiting\n");
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

#ifdef USE_LIBZVBI
   pZvbiCapt = NULL;
   pZvbiRawDec = NULL;
   pZvbiData = NULL;
#endif

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
   pVbiBuf->slicerType = VBI_SLICER_TRIVIAL;
   pVbiBuf->dvbPid = -1;

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
         tv.tv_sec = 2;
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
   pVbiBuf->slicerType = VBI_SLICER_TRIVIAL;
   pVbiBuf->dvbPid = -1;

   vbi_fdin = -1;
   video_fd = -1;

   pthread_cond_init(&vbi_start_cond, NULL);
   pthread_mutex_init(&vbi_start_mutex, NULL);

#ifdef USE_LIBZVBI
   pZvbiCapt = NULL;
   pZvbiRawDec = NULL;
   pZvbiData = NULL;
# ifdef USE_VBI_PROXY
   pProxyClient = NULL;
# endif
#endif

   return TRUE;
#endif  //USE_THREADS
}
