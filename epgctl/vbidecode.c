/*
 *  Teletext decoder
 *  Based on vbidecode.cc by Ralph Metzler (author of the bttv package)
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
 *    With Un*x, this module grabs the VBI lines from each frame
 *    of the actual video image and decodes them according to the
 *    Enhanced Teletext specification (see ETS 300 706, available at
 *    http://www.etsi.org/). Result is 45 bytes for each line.
 *    With Win32, the VBI acquisition has to be handled externally.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: vbidecode.c,v 1.18 2000/09/28 20:32:05 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#ifndef WIN32
#include "sys/ioctl.h"

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/resource.h>
//#ifdef linux
//# define _WAITFLAGS_H              // conflicts with linux/wait.h
//#endif
#include <sys/wait.h>
#endif
#include <time.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/hamming.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbacq.h"
#include "epgctl/vbidecode.h"

#ifndef WIN32
# ifdef __NetBSD__
#  include <dev/ic/bt8xx.h>
#  define VBI_MAXLINES 19
# else
#  include "linux/videodev.h"
#  define VBI_MAXLINES 16
# endif
# define VBINAME "/dev/vbi%c"
# define VIDEONAME "/dev/video%c"
# define PIDFILENAME "/tmp/.vbi.pid"
#endif

#define VBI_LINENUM VBI_MAXLINES
#define VBI_BPL     2048
#define VBI_BPF     (VBI_LINENUM*2*VBI_BPL)

// use fixpoint arithmetic for scanning steps
#define FPSHIFT 16
#define FPFAC (1<<FPSHIFT)

// use standard frequency for Bt848 and PAL
// (no Nextview exists in any NTSC countries)
#define VTSTEP  ((int)((35.468950/6.9375)*FPFAC+0.5))
#define VPSSTEP ((int)(7.1 * FPFAC + 0.5))


// ---------------------------------------------------------------------------
// Decode teletext packet header
//
static void VbiDecodePacket( const uchar * data )
{
   sint  tmp1, tmp2, tmp3;
   uchar mag, pkgno;
   uint  page;
   uint  sub;

   if ( UnHam84Byte(data, &tmp1) )
   {
      mag   = tmp1 & 7;
      pkgno = (tmp1 >> 3) & 0x1f;

      if (pkgno == 0)
      {  // this is a page header - start of a new page
         if ( UnHam84Byte(data + 2, &tmp1) &&
              UnHam84Byte(data + 4, &tmp2) &&
              UnHam84Byte(data + 6, &tmp3) )
         {
            page = tmp1 | (mag << 8);
            sub = (tmp2 | (tmp3 << 8)) & 0x3f7f;
            //printf("**** page=%03x.%04X\n", page, sub);
            EpgDbAcqAddPacket(page, sub, 0, data + 2);
         }
         //else debug0("page number or subcode hamming error - skipping page");
      }
      else
      {
         EpgDbAcqAddPacket((uint)mag << 8, 0, pkgno, data + 2);
         //printf("**** pkgno=%d\n", pkgno);
      }
   }
   //else debug0("packet header decoding error - skipping");
}


// ---------------------------------------------------------------------------
// Get one byte from the analog VBI data line
//
static uchar vtscan(const uchar *lbuf, ulong *spos, int off)
{ 
  int j;
  uchar theByte;
  
  theByte = 0;
  for (j=7; j>=0; j--, *spos+=VTSTEP)
    theByte |= ((lbuf[*spos >> FPSHIFT] + off) & 0x80) >> j;

  return theByte;
}

// ---------------------------------------------------------------------------
//   Get one byte from the analog VPS data line
//   VPS uses a lower bit rate than teletext
//
static uchar vps_scan(const uchar *lbuf, ulong *spos, int off)
{ 
  int j;
  uchar theByte;
  
  theByte = 0;
  for (j=7; j>=0; j--, *spos+=VPSSTEP)
    theByte |= ((lbuf[*spos >> FPSHIFT] + off) & 0x80) >> j;

  return theByte;
}

// ---------------------------------------------------------------------------
// Low level decoder of raw VBI data 
// It calls the higher level decoders as needed 
//
void VbiDecodeLine(const uchar *lbuf, int line)
{
  uchar data[45];
  int i,p;
  int thresh, off, min=255, max=0;
  ulong spos, dpos;
  
  /* automatic gain control */
  for (i=120; i<450; i++)
  {
    if (lbuf[i] < min) 
      min = lbuf[i];
    if (lbuf[i] > max) 
      max = lbuf[i];
  }
  thresh = (max+min) / 2;
  off = 128 - thresh;
  
  // search for first 1 bit (VT always starts with 55 55 27)
  p=50;
  while ((lbuf[p]<thresh)&&(p<350))
    p++;
  // search for maximum of 1st peak
  while ((lbuf[p+1]>=lbuf[p])&&(p<350))
    p++;
  spos=dpos=(p<<FPSHIFT);
  
  /* ignore first bit for now */
  data[0]=vtscan(lbuf, &spos, off);

  if ((data[0]&0xfe)==0x54)
  {
    data[1]=vtscan(lbuf, &spos, off);
    switch (data[1])
    {
      case 0x75: /* missed first two 1-bits, TZ991230++ */
	//printf("****** step back by 2\n");
        spos-=2*VTSTEP; 
        data[1]=0xd5;
      case 0xd5: /* oops, missed first 1-bit: backup 2 bits */
	//printf("****** step back by 1\n");
        spos-=2*VTSTEP; 
        data[1]=0x55;
      case 0x55:
        data[2]=vtscan(lbuf, &spos, off);
        switch(data[2])
        {
          case 0xd8: /* this shows up on some channels?!?!? */
            //for (i=3; i<45; i++) 
            //  data[i]=vtscan(lbuf, &spos, off);
            //return;
          case 0x27:
            for (i=3; i<45; i++) 
              data[i]=vtscan(lbuf, &spos, off);
            VbiDecodePacket(data + 3);
            break;
          default:
	    //printf("****** line=%d  [2]=%x != 0x27 && 0xd8\n", line, data[2]);
            break;
        }
	break;
      default:
	//printf("****** line=%d  [1]=%x != 0x55 && 0xd5\n", line, data[1]);
        break;
    }
  }
  #ifndef WIN32
  else if ((line == 9) && pVbiBuf->isEpgScan)
  {
     if ((vps_scan(lbuf, &dpos, off) == 0x55) &&  // VPS run in
	 (vps_scan(lbuf, &dpos, off) == 0x55) &&
	 (vps_scan(lbuf, &dpos, off) == 0x51) &&  // VPS start code
	 (vps_scan(lbuf, &dpos, off) == 0x99))
     {
	for (i=3; i <= 14; i++)
	{
	   uint bit, j;
	   data[i] = 0;
	   for (j=0; j<8; j++, dpos+=VPSSTEP*2)
	   { // decode bi-phase data bit: 1='10', 0='01'
	     bit = (lbuf[dpos >> FPSHIFT] + off) & 0x80;
	     if (bit == ((lbuf[(dpos + VPSSTEP) >> FPSHIFT] + off) & 0x80))
		break;  // bit error
	     data[i] |= bit >> j;
	   }
	   if (j < 8)
	      break;  // was error
	}

	if (i > 14)
	{
           uint cni = ((data[13] & 0x3) << 10) | ((data[14] & 0xc0) << 2) |
                      ((data[11] & 0xc0) ) | (data[14] & 0x3f);
           if ((cni != 0) && ((cni & 0xfff) != 0xfff))
              pVbiBuf->vpsCni = cni;
	   //printf("VPS line %d: CNI=0x%04x\n", line, cni);
	}
     }
     //else
        //printf("VPS line %d=%02x %02x %02x\n", line, data[1], data[2], data[3]);
  }
  #endif  //WIN32
  //else
     //printf("****** line=%d  [0]=%x != 0x54\n", line, data[0]);
  
}

// ---------------------------------------------------------------------------
//                  V B I - C O N T R O L - P R O C E S S
// ---------------------------------------------------------------------------
#ifndef WIN32

static bool acqShouldExit;
static bool freeDevice;
static bool isVbiProcess;
static bool recvWakeUpSig;
static int vbi_fdin;
static int shmId;
EPGACQ_BUF *pVbiBuf;

static char devName[20];  //sizeof(*VBINAME)
static uchar videoDevicePostfix;

// ---------------------------------------------------------------------------
// Decode all VBI lines of the last seen frame
//
bool VbiDecodeFrame( void )
{
   uchar data[VBI_BPF];
   slong stat;
   uint  line;

   #ifdef MEASURE_CALLING_DELTA
   {
      static struct timeval tprev;
      struct timeval tcur, tdelta;

      gettimeofday(&tcur, NULL);
      timersub(&tcur, &tprev, &tdelta);
      if ((tdelta.tv_sec > 0) || (tdelta.tv_usec >= 38*1000))
         debug2("delta: %ld.%03ld", tdelta.tv_sec, (tdelta.tv_usec+500)/1000);
      tprev = tcur;
   }
   #endif

   stat = read(vbi_fdin, data, VBI_BPF);

   if ( stat > 0 )
   {
      for (line=0; line < stat/VBI_BPL; line++)
      {
         VbiDecodeLine(data + line * VBI_BPL, line);
      }
   }
   else if ((stat < 0) && (errno != EINTR) && (errno != EAGAIN))
      perror("vbi decode: read");  // unguenstig, weil output nach stderr

   return (stat > 0);
}

// ---------------------------------------------------------------------------
// Channel frequency table (Europe only)
//
typedef struct
{
   double  freqStart;      // base freq of first channel in this band
   double  freqMax;        // max. freq of last channel, including fine-tuning
   uint    freqOffset;     // freq offset between two channels in this band
   uint    firstChannel;
   uint    lastChannel;
} FREQ_TABLE;

// channel table for Europe
#define FREQ_TABLE_COUNT  8
#define FIRST_CHANNEL   2
const FREQ_TABLE freqTable[FREQ_TABLE_COUNT] =
{  // must be sorted by channel numbers
   { 48.25,   66.25, 7.0,   2,   4},
   {175.25,  228.25, 7.0,   5,  12},
   {471.25,  859.25, 8.0,  21,  69},
   {112.25,  172.25, 7.0,  72,  80},
   {231.25,  298.25, 7.0,  81,  90},
   {303.25,  451.25, 8.0,  91, 109},
   {455.25,  471.25, 8.0, 110, 112},
   {859.25, 1175.25, 8.0, 161, 200}
};

// ---------------------------------------------------------------------------
// Get number of channels in table (for EPG scan progress bar)
//
int VbiGetChannelCount( void )
{
   int band, count;

   count = 0;
   for (band=0; band < FREQ_TABLE_COUNT; band++)
   {
      count += freqTable[band].lastChannel - freqTable[band].firstChannel + 1;
   }
   return count;
}

// ---------------------------------------------------------------------------
// Get the next channel and frequency from the table
//
bool VbiGetNextChannel( uint *pChan, ulong *pFreq )
{
   int band;

   if (*pChan < FIRST_CHANNEL)
      *pChan = FIRST_CHANNEL;
   else
      *pChan = *pChan + 1;

   for (band=0; band < FREQ_TABLE_COUNT; band++)
   {
      // assume that the table is sorted by channel numbers
      if (*pChan < freqTable[band].lastChannel)
      {
         // skip possible channel gap between bands
         if (*pChan < freqTable[band].firstChannel)
            *pChan = freqTable[band].firstChannel;

         // get the frequency of this channel
         *pFreq = (ulong) (16.0 * (freqTable[band].freqStart +
                                  (*pChan - freqTable[band].firstChannel) * freqTable[band].freqOffset));
         break;
      }
   }

   return (band < FREQ_TABLE_COUNT);
}

// ---------------------------------------------------------------------------
// Open video device for access to tuner
//
static int video_fd = -1;
static bool VbiTuneOpenDevice( void )
{
   struct video_capability vcapab;
   struct video_channel vchan;
   struct video_tuner vtuner;
   int channel;
   bool result = FALSE;

   sprintf(devName, VIDEONAME, videoDevicePostfix);
   video_fd = open(devName, O_RDWR);
   if (video_fd != -1)
   {
      // get capabilities: number of channels
      memset(&vcapab, 0, sizeof(vcapab));
      if ( (ioctl(video_fd, VIDIOCGCAP, &vcapab) == 0) &&
           (vcapab.type & VID_TYPE_TUNER) )
      {
         // search for the TV tuner channel
         for (channel=0; channel < vcapab.channels; channel++)
         {
            memset(&vchan, 0, sizeof(vchan));
            vchan.channel = channel;

            if ((ioctl(video_fd, VIDIOCGCHAN, &vchan) == 0) &&
                (vchan.type & VIDEO_TYPE_TV) &&
                (vchan.flags & VIDEO_VC_TUNER))
               break;
         }
         if (channel < vcapab.channels)
         {  // found a tuner -> set it as input channel
            //printf("found tuner on channel %d\n", vchan.channel);

            // set the tuner as input channel
            vchan.channel = channel;
            // XXX BUG WORKAROUND: need to set a different norm first, since
            // XXX initialization is only done upon norm change (needed after reboot)
            //vchan.norm  = VIDEO_MODE_PAL;
            vchan.norm    = VIDEO_MODE_AUTO;
            if(ioctl(video_fd, VIDIOCSCHAN, &vchan) == 0)
            {
               // query the settings of tuner #0
               memset(&vtuner, 0, sizeof(vtuner));
               if ( (ioctl(video_fd, VIDIOCGTUNER, &vtuner) == 0) &&
                    (vtuner.flags & VIDEO_TUNER_PAL) )
               {
                  vtuner.mode = VIDEO_MODE_PAL;
                  if (ioctl(video_fd, VIDIOCSTUNER, &vtuner) == 0)
                  {
                     result = TRUE;
                  }
                  else
                     perror("VIDIOCSTUNER");
               }
               else
                  perror("VIDIOCGTUNER");
            }
            else
               perror("VIDIOCSCHAN");
         }
         else
            debug1("no tuner found among %d input channels", vcapab.channels);
      }
      else
         perror("VIDIOCGCAP");
   }
   else
      debug1("Vbi-TuneOpenDevice: could not open device %s", devName);

   return result;
}

// ---------------------------------------------------------------------------
// Close video device
//
void VbiTuneCloseDevice( void )
{
   if (video_fd != -1)
   {
      close(video_fd);
      video_fd = -1;
   }
}

// ---------------------------------------------------------------------------
// Tune a given frequency
//
bool VbiTuneChannel( ulong freq, bool keepOpen )
{
   bool result = FALSE;

   if ( (video_fd != -1) || VbiTuneOpenDevice() )
   {
      // Set the tuner frequency
      if(ioctl(video_fd, VIDIOCSFREQ, &freq) == 0)
      {
         //printf("Vbi-TuneChannel: set to %.2f\n", (double)freq/16);

         if (keepOpen == FALSE)
         {
            VbiTuneCloseDevice();
         }
         result = TRUE;
      }
      else
         perror("VIDIOCSFREQ");
   }
   return result;
}

// ---------------------------------------------------------------------------
// Get signal strength on current tuner frequency
//
uint VbiTuneGetSignalStrength( void )
{
   struct video_tuner vtuner;
   uint result = 0;

   if ( video_fd != -1 )
   {
      vtuner.tuner = 0;
      if (ioctl(video_fd, VIDIOCGTUNER, &vtuner) == 0)
      {
         //printf("VbiTune-GetSignalStrength: %u\n", vtuner.signal);
         result = vtuner.signal;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// The Acquisition process bows out on the usual signals
//
static void VbiSignalHandler( int sigval )
{
   acqShouldExit = TRUE;
   signal(sigval, VbiSignalHandler);
}

// ---------------------------------------------------------------------------
// Receive wake-up signal or ACK
//
static void VbiSignalWakeUp( int sigval )
{
   // do nothing
   recvWakeUpSig = TRUE;
   signal(sigval, VbiSignalWakeUp);
}

// ---------------------------------------------------------------------------
// Receive signal to free or take vbi device
//
static void VbiSignalHangup( int sigval )
{
   freeDevice = TRUE;
   signal(sigval, VbiSignalHangup);
}

// ---------------------------------------------------------------------------
// Wake-up the acq child process to start acquisition
// - the child signals back after it completed the operation
// - the status of the operation is in the isEnabled flag
//
bool VbiDecodeWakeUp( void )
{
   struct timeval tv;
   bool result = FALSE;

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
// Stop the acquisition process
//
void VbiDecodeExit( void )
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
void VbiDecodeCheckParent( void )
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
// VBI decoder main loop
//
static void VbiDecodeMain( void )
{
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
         if (vbi_fdin == -1)
         {  // acq was switched on -> open device
            sprintf(devName, VBINAME, videoDevicePostfix);
            vbi_fdin = open(devName, O_RDONLY);
            if (vbi_fdin == -1)
            {
               debug2("VBI open %s failed: errno=%d", devName, errno);
               pVbiBuf->isEnabled = FALSE;
            }
            else
            {  // open successful -> write pid in file
               fp = fopen(PIDFILENAME, "w");
               if (fp != NULL)
               {
                  fprintf(fp, "%d", pVbiBuf->vbiPid);
                  fclose(fp);
               }
            }
            kill(pVbiBuf->epgPid, SIGUSR1);
         }
         else
            VbiDecodeFrame();
      }
      else
      {
         if (vbi_fdin != -1)
         {  // acq was switched off -> close device
            unlink(PIDFILENAME);
            close(vbi_fdin);
            vbi_fdin = -1;
         }
         // sleep until signal; check parent every 30 secs
         tv.tv_sec = 30;
         tv.tv_usec = 0;
         select(0, NULL, NULL, NULL, &tv);
         VbiDecodeCheckParent();
      }
   }
}

// ---------------------------------------------------------------------------
// Create the VBI slave process - also slave main loop
//
bool VbiDecodeInit( uchar cardPostfix )
{
   struct timeval tv;
   int dbTaskPid;

   pVbiBuf = NULL;
   isVbiProcess = FALSE;
   videoDevicePostfix = cardPostfix;

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

   pVbiBuf->epgPid = getpid();
   EpgDbAcqInit(pVbiBuf);

   recvWakeUpSig = FALSE;
   signal(SIGHUP,  SIG_IGN);
   signal(SIGUSR1, VbiSignalWakeUp);
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
      signal(SIGINT,  VbiSignalHandler);
      signal(SIGTERM, VbiSignalHandler);
      signal(SIGQUIT, VbiSignalHandler);
      signal(SIGHUP,  VbiSignalHangup);

      vbi_fdin = -1;
      acqShouldExit = FALSE;
      freeDevice = FALSE;

      // notify parent that child is ready
      kill(pVbiBuf->epgPid, SIGUSR1);

      // enter main loop
      VbiDecodeMain();

      if (vbi_fdin != -1)
      {
         unlink(PIDFILENAME);
         close(vbi_fdin);
         vbi_fdin = -1;
      }

      VbiDecodeExit();
      exit(0);
   }
}

#endif  // WIN32

