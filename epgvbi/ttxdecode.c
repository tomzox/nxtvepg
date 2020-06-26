/*
 *  Decoding of teletext and VPS packets
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
 *    This module contains the interface between the slave and control
 *    processes (or threads), i.e. there are two execution threads running
 *    in here:
 *
 *    The acquisition slave process/thread puts all teletext packets of a
 *    given page (e.g. default page 1DF) into a ring buffer. Additionally
 *    Magazine Inventory Pages (MIP) and channel identification codes (VPS,
 *    PDC, Packet 8/30/1) are decoded. This data is passed via shared memory
 *    to the master.
 *
 *    The master process/thread extracts the packets from the ring buffer and
 *    hands them to the streams module for assembly to Nextview blocks. The
 *    interface to the higher level acquisition control consists of functions
 *    to start and stop acquisition, get statistics values and detect
 *    channel changes.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: ttxdecode.c,v 1.67 2020/06/22 07:12:17 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/cni_tables.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/hamming.h"
#include "epgvbi/ttxdecode.h"


// ----------------------------------------------------------------------------
// Internal decoder status
//

// struct that holds the state of the slave process, i.e. ttx decoder
static struct
{
   bool       magParallel;       // 0: serial page mode; 1: parallel (invert of header bit C11)
   uint       frameSeqNo;        // last reported VBI frame sequence number
   bool       skipFrames;        // skip first frames after channel change
   uint       pkgPerFrame;       // counter to determine teletext data rate (as ttx pkg per frame)
   uint       lastMag;
   uint       vbiMaxReaderIdx;
   struct
   {
      uint    curPageNo;         // current ttx page number per magazine; 0 if unknown
      bool    fwdPage;           // current ttx page is forwarded
      bool    isMipPage;         // current ttx page in this magazine is the MIP page for M=0..7
   } mags[8];
} acqSlaveState;

// max. number of pages that are allowed for EPG transmission: mFd & mdF: m[0..7],d[0..9]
#define NXTV_VALID_PAGE_PER_MAG (1 + 10 + 10)
#define NXTV_VALID_PAGE_COUNT   (8 * NXTV_VALID_PAGE_PER_MAG)

static time32_t        ttxStatsStart;

// Unknown CNI and obvious invalid values
#define CNI_IS_INVALID(CNI) (((CNI) == 0) || ((CNI) == 0xffff) || ((CNI) == 0x0fff))
// This (unassigned) value is transmitted by several networks in DE via DVB
#define CNI_IS_BLOCKED(CNI) ((CNI) == 0x1234)

// ----------------------------------------------------------------------------
// Start EPG acquisition
//
void TtxDecode_StartScan( void )
{
   dprintf0("TtxDecode-StartScan\n");

   // enable ttx processing in the slave process/thread
   pVbiBuf->scanEnabled = TRUE;

   // skip first VBI frame, reset ttx decoder, then set reader idx to writer idx
   if (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf)
   {
      pVbiBuf->chanChangeReq = pVbiBuf->chanChangeCnf + 2;
   }
}

// ----------------------------------------------------------------------------
// Start teletext grabbing
//
void TtxDecode_StartTtxAcq( bool enableScan, uint startPageNo, uint stopPageNo )
{
   dprintf3("TtxDecode-StartTtxAcq: scan:%d page=%03X..%03X\n", enableScan, startPageNo, stopPageNo);

   // pass the configuration variables to the ttx process via shared memory
   pVbiBuf->startPageNo = startPageNo;
   pVbiBuf->stopPageNo = stopPageNo;

   // enable ttx processing in the slave process/thread
   pVbiBuf->ttxEnabled = TRUE;
   pVbiBuf->ttxHeader.op_mode = (enableScan ? EPGACQ_TTX_HEAD_DEC : EPGACQ_TTX_HEAD_NONE);

   // skip first VBI frame, reset ttx decoder, then set reader idx to writer idx
   if (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf)
   {
      pVbiBuf->chanChangeReq = pVbiBuf->chanChangeCnf + 2;
   }
}

// ----------------------------------------------------------------------------
// Stop EPG acquisition
// - the external process may continue to collect data
//
void TtxDecode_StopScan( void )
{
   dprintf0("TtxDecode-StopScan\n");

   // inform writer process/thread
   pVbiBuf->scanEnabled = FALSE;
}

// ----------------------------------------------------------------------------
// Stop EPG acquisition
//
void TtxDecode_StopTtxAcq( void )
{
   dprintf0("TtxDecode-StopTtxAcq\n");

   // inform writer process/thread
   pVbiBuf->ttxEnabled = FALSE;
}

// ---------------------------------------------------------------------------
// Return CNIs and number of data pages found
// - result values are not invalidized, b/c the caller will change the
//   channel after a value was read anyways
//
void TtxDecode_GetScanResults( uint *pCni, bool *pNiWait, char *pDispText, uint textMaxLen )
{
   CNI_TYPE type;
   uint     idx;
   uint     cni;
   bool     niWait  = FALSE;

   cni     = 0;
   niWait  = FALSE;

   if (textMaxLen > PDC_TEXT_LEN + 1)
      textMaxLen = PDC_TEXT_LEN + 1;

   if (pDispText != NULL)
      pDispText[0] = 0;

   // check if initialization for the current channel is complete
   if (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf)
   {
      // search for any available source
      for (type=0; type < CNI_TYPE_COUNT; type++)
      {
         if (pVbiBuf->cnis[type].haveCni)
         {  // have a verified CNI -> return it
            if (type == CNI_TYPE_NI)
               cni = CniConvertP8301ToVps(pVbiBuf->cnis[type].outCni);
            else if (type == CNI_TYPE_PDC)
               cni = CniConvertPdcToVps(pVbiBuf->cnis[type].outCni);
            else
               cni = pVbiBuf->cnis[type].outCni;
            break;
         }
         else if (pVbiBuf->cnis[type].cniRepCount > 0)
         {  // received at least one VPS or P8/30 packet -> wait for repetition
            niWait = TRUE;
         }
      }

      if (pDispText != NULL)
      {
         for (type=0; type < CNI_TYPE_COUNT; type++)
         {
            if (pVbiBuf->cnis[type].haveText)
            {
               // skip spaces from beginning of string
               const uint8_t * p = (const uint8_t *) pVbiBuf->cnis[type].outText;
               for (idx=textMaxLen; idx > 0; idx--, p++)
                  if (*p != ' ')
                     break;
               // skip spaces at end of string
               while ((idx > 1) && (p[idx - 2] <= ' '))
                  idx -= 1;
               if (idx > 1)
               {  // copy the trimmed string into the output array
                  memcpy(pDispText, p, idx - 1);
                  pDispText[idx - 1] = 0;
               }
            }
            else if (pVbiBuf->cnis[type].charRepCount[0] > 0)
            {
               niWait = TRUE;
            }
         }
      }
   }

   if (pCni != NULL)
      *pCni = cni;
   if (pNiWait != NULL)
      *pNiWait = niWait;

   if ((cni != 0) || (niWait) || ((pDispText != NULL) && (pDispText[0] != 0)))
      dprintf3("TtxDecode-GetScanResults: cni=%04X niWait=%d text=%s\n", cni, niWait, pDispText);
}

// ---------------------------------------------------------------------------
// Return captured and verified CNI and PIL information
// - return an ID which is incremented each time a new CNI or PIL value is
//   added - this can be used by the caller to ignore obsolete values
//   (e.g. after a channel change to a channel w/o CNI)
// - CNI can be transmitted in 3 ways: VPS, PDC, NI. This function returns
//   values from the "best" available source (speed, reliability).
// - Note: for channels which have different IDs in VPS and PDC or NI, the
//   CNI value is converted to VPS or PDC respectively so that the caller
//   needs not care how the CNI was obtained.
//
bool TtxDecode_GetCniAndPil( uint * pCni, uint * pPil, CNI_TYPE * pCniType,
                             uint pCniInd[CNI_TYPE_COUNT], uint pPilInd[CNI_TYPE_COUNT],
                             volatile EPGACQ_BUF * pThisVbiBuf )
{
   CNI_TYPE type;
   bool result = FALSE;

   if (pThisVbiBuf == NULL)
      pThisVbiBuf = pVbiBuf;

   if (pThisVbiBuf != NULL)
   {
      // check if initialization for the current channel is complete
      if (pThisVbiBuf->chanChangeReq == pThisVbiBuf->chanChangeCnf)
      {
         // search for the best available source
         for (type=0; type < CNI_TYPE_COUNT; type++)
         {
            if ( pThisVbiBuf->cnis[type].haveCni &&
                 ((pCniInd == NULL) || (pCniInd[type] != pThisVbiBuf->cnis[type].outCniInd)) )
            {
               if (pCni != NULL)
               {
                  if (type == CNI_TYPE_NI)
                     *pCni = CniConvertP8301ToVps(pThisVbiBuf->cnis[type].outCni);
                  else if (type == CNI_TYPE_PDC)
                     *pCni = CniConvertPdcToVps(pThisVbiBuf->cnis[type].outCni);
                  else
                     *pCni = pThisVbiBuf->cnis[type].outCni;

                  if (pCniInd != NULL)
                     pCniInd[type] = pThisVbiBuf->cnis[type].outCniInd;
               }

               if (pPil != NULL)
               {
                  if ( pThisVbiBuf->cnis[type].havePil &&
                       ((pPilInd == NULL) || (pPilInd[type] != pThisVbiBuf->cnis[type].outPilInd)) )
                  {
                     *pPil = pThisVbiBuf->cnis[type].outPil;

                     if (pPilInd != NULL)
                        pPilInd[type] = pThisVbiBuf->cnis[type].outPilInd;
                  }
                  else
                     *pPil = INVALID_VPS_PIL;
               }
               if (pCniType != NULL)
               {
                  *pCniType = type;
               }
               dprintf7("TtxDecode-GetCniAndPil: type:%d CNI?:%d 0x%04X, PIL?:%d %X (Ind:%d,%d)\n", type, pThisVbiBuf->cnis[type].haveCni, pThisVbiBuf->cnis[type].havePil, pThisVbiBuf->cnis[type].outCni, pThisVbiBuf->cnis[type].outPil, pThisVbiBuf->cnis[type].outCniInd, pThisVbiBuf->cnis[type].outPilInd);
               result = TRUE;
               break;
            }
         }
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Save CNI and PIL from the VBI process/thread
//
static void TtxDecode_AddCni( CNI_TYPE type, uint cni, uint pil )
{
   if (pVbiBuf != NULL)
   {
      if (type < CNI_TYPE_COUNT)
      {
         if (!CNI_IS_INVALID(cni) && !CNI_IS_BLOCKED(cni))
         {
            DBGONLY( if (pVbiBuf->cnis[type].cniRepCount == 0) )
               dprintf3("TtxDecode-AddCni: new CNI 0x%04X, PIL=%X, type %d\n", cni, pil, type);

            if ( (pVbiBuf->cnis[type].cniRepCount > 0) &&
                 (pVbiBuf->cnis[type].lastCni != cni) )
            {  // comparison failure -> reset repetition counter
               #if DEBUG_SWITCH_STREAM == ON
               debug3("TtxDecode-AddCni: %s CNI error: last %04X != %04X", ((type == CNI_TYPE_VPS) ? "VPS" : ((type == CNI_TYPE_PDC) ? "PDC" : "NI")), pVbiBuf->cnis[type].lastCni, cni);
               #endif
               pVbiBuf->cnis[type].cniRepCount = 0;
            }
            pVbiBuf->cnis[type].lastCni = cni;
            pVbiBuf->cnis[type].cniRepCount += 1;

            if ( (pVbiBuf->cnis[type].cniRepCount > 2) || (type == CNI_TYPE_PDC) )
            {  // the same CNI value received 3 times -> make it available as result
               // PDC is Hamming-8/4 coded, so one reception is safe enough

               if (pVbiBuf->cnis[type].havePil && (pVbiBuf->cnis[type].outCni != cni))
               {  // CNI result value changed -> remove PIL
                  pVbiBuf->cnis[type].havePil = FALSE;
               }
               pVbiBuf->cnis[type].outCni = cni;
               pVbiBuf->cnis[type].outCniInd += 1;
               pVbiBuf->cnis[type].haveCni = TRUE;
            }

            if (pil != INVALID_VPS_PIL)
            {
               if ( (pVbiBuf->cnis[type].pilRepCount > 0) &&
                    (pVbiBuf->cnis[type].lastPil != pil) )
               {  // comparison failure -> reset repetition counter
                  #if DEBUG_SWITCH_STREAM == ON
                  debug11("TtxDecode-AddCni: %s PIL error: last %02d.%02d. %02d:%02d (0x%04X) != %02d.%02d. %02d:%02d (0x%04X)", ((type == CNI_TYPE_VPS) ? "VPS" : ((type == CNI_TYPE_PDC) ? "PDC" : "NI")), (pVbiBuf->cnis[type].lastPil >> 15) & 0x1F, (pVbiBuf->cnis[type].lastPil >> 11) & 0x0F, (pVbiBuf->cnis[type].lastPil >> 6) & 0x1F, pVbiBuf->cnis[type].lastPil & 0x3F, pVbiBuf->cnis[type].lastPil, (pil >> 15) & 0x1F, (pil >> 11) & 0x0F, (pil >> 6) & 0x1F, pil & 0x3F, pil);
                  #endif
                  pVbiBuf->cnis[type].pilRepCount = 0;
               }
               pVbiBuf->cnis[type].lastPil = pil;
               pVbiBuf->cnis[type].pilRepCount += 1;

               if ( (pVbiBuf->cnis[type].pilRepCount > 2) || (type == CNI_TYPE_PDC) )
               {
                  // don't save as result if CNI is unknown or does not match the last CNI
                  if (pVbiBuf->cnis[type].haveCni && (pVbiBuf->cnis[type].outCni == cni))
                  {
                     pVbiBuf->cnis[type].outPil = pil;
                     // set flag that PIL is available
                     pVbiBuf->cnis[type].outPilInd += 1;
                     pVbiBuf->cnis[type].havePil = TRUE;
                  }
               }
            }
         }
      }
      else
         fatal1("TtxDecode-AddCni: invalid type %d\n", type);
   }
}

// ---------------------------------------------------------------------------
// Save status display text
//
static void TtxDecode_AddText( CNI_TYPE type, const uchar * data )
{
   volatile CNI_ACQ_STATE  * pState;
   schar c1;
   uint  minRepCount;
   uint  idx;

   if (data != NULL)
   {
      if (type < CNI_TYPE_COUNT)
      {
         pState = pVbiBuf->cnis + type;
         minRepCount = 3;

         for (idx=0; idx < PDC_TEXT_LEN; idx++)
         {
            // ignore characters with parity decoding errors
            c1 = (schar)parityTab[*(data++)];
            if (c1 >= 0)
            {
               if (pState->lastChar[idx] != c1)
               {  // character changed -> reset repetition counter
                  pState->lastChar[idx] = c1;
                  pState->charRepCount[idx] = 0;
               }
               else
                  pState->charRepCount[idx] += 1;
            }

            if (pState->charRepCount[idx] < minRepCount)
               minRepCount = pState->charRepCount[idx];
         }

         if (minRepCount >= 3)
         {  // all characters are received often enough -> copy string into output field
            if ( (pState->haveText == FALSE) ||
                 (memcmp((char *) pState->outText, (char *) pState->lastChar, PDC_TEXT_LEN) != 0) )
            {
               pState->haveText = FALSE;
               memcpy((char *) pState->outText, (char *) pState->lastChar, PDC_TEXT_LEN);
               pState->outText[PDC_TEXT_LEN] = 0;
               pState->haveText = TRUE;
            }
         }
      }
      else
         fatal1("TtxDecode-AddText: invalid type %d\n", type);
   }
   else
      fatal0("TtxDecode-AddText: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Save status display text
//
static void TtxDecode_AddTime( CNI_TYPE type, uint timeVal, sint lto )
{
   volatile TTX_TIME_BUF * pState;

   if (type < CNI_TYPE_COUNT)
   {
      pState = &pVbiBuf->ttxTime;

      if ( (pState->lastLto != lto) ||
           (timeVal - pState->lastTimeVal > 2) ||
           (timeVal < pState->lastTimeVal) )
      {
         pState->timeRepCount = 0;
      }

      pState->lastTimeVal = timeVal;
      pState->lastLto     = lto;

      pState->timeRepCount += 1;
      if (pState->timeRepCount >= 3)
      {
         // XXX FIXME not MT safe
         pState->timeVal  = pState->lastTimeVal;
         pState->lto      = pState->lastLto;
         pState->haveTime = TRUE;
      }
   }
   else
      fatal1("TtxDecode-AddText: invalid type %d\n", type);
}

// ---------------------------------------------------------------------------
// Retrieve last received time
// - returns 0 if none available
//
uint TtxDecode_GetDateTime( sint * pLto )
{
   volatile TTX_TIME_BUF * pState;
   uint result = 0;

   pState = &pVbiBuf->ttxTime;

   if (pState->haveTime)
   {
      if (pLto != NULL)
         *pLto = pState->lto;

      result = pState->timeVal;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Decode an incoming MIP packet
// - note that this runs inside of the ttx sub-thread
// - if an EPG id is found, it is passed to the acq control via shared memory
// - only table cells which refer to pages allowed for Nextview are decoded
//
static void TtxDecode_MipPacket( uchar magNo, uchar pkgNo, const uchar *data )
{
#if 0  // unused code
   sint id;
   uint i;

   if ((pkgNo >= 6) && (pkgNo <= 8))
   {
      for (i=0; i < 20; i++)
      {
         if ( UnHam84Byte(data + i * 2, &id) && (id == MIP_EPG_ID))
         {
            pVbiBuf->mipPageNo = (0xA0 + (pkgNo - 6) * 0x20 + (i / 10) * 0x10 + (i % 10)) | (uint)(magNo << 8);
            break;
         }
      }
   }
   else if ((pkgNo >= 9) && (pkgNo <= 13))
   {
      for (i=0; i < 18; i++)
      {
         if ( UnHam84Byte(data + i * 2, &id) && (id == MIP_EPG_ID))
         {
            pVbiBuf->mipPageNo = (0x0A + (pkgNo - 9) * 0x30 + (i / 6) * 0x10 + (i % 6)) | (uint)(magNo << 8);
            break;
         }
      }
   }
   else if (pkgNo == 14)
   {
      for (i=0; i < 6; i++)
      {
         if ( UnHam84Byte(data + i * 2, &id) && (id == MIP_EPG_ID))
         {
            pVbiBuf->mipPageNo = (0xFA + i) | (uint)(magNo << 8);
            break;
         }
      }
   }
#endif
}

// ---------------------------------------------------------------------------
// Assemble PIL components to nextview format
// - invalid dates or times are replaced by VPS error code ("system code")
//   except for the 2 defined VPS special codes: pause and empty
//
static uint TtxDecode_AssemblePil( uint mday, uint month, uint hour, uint minute )
{
   uint pil;

   if ( (mday != 0) && (month >= 1) && (month <= 12) &&
        (hour < 24) && (minute < 60) )
   {  // valid PIL value
      pil = (mday << 15) | (month << 11) | (hour << 6) | minute;
   }
   else if ((mday == 0) && (month == 15) && (hour >= 29) && (minute == 63))
   {  // valid VPS control code
      pil = (0 << 15) | (15 << 11) | (hour << 6) | 63;
   }
   else
   {  // invalid code -> replace with system code
      pil = (0 << 15) | (15 << 11) | (31 << 6) | 63;
   }
   return pil;
}

// ---------------------------------------------------------------------------
// Decode a VPS data line
// - bit fields are defined in "VPS Richtlinie 8R2" from August 1995
// - called by the VBI decoder for every received VPS line
//
void TtxDecode_AddVpsData( const uchar * data )
{
   uint mday, month, hour, minute;
   uint cni, pil;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) &&
        (acqSlaveState.skipFrames == 0) )
   {
      pVbiBuf->ttxStats.vpsLineCount += 1;

      cni = ((data[13 - 3] & 0x3) << 10) | ((data[14 - 3] & 0xc0) << 2) |
            ((data[11 - 3] & 0xc0)) | (data[14 - 3] & 0x3f);

      if ((cni != 0) && (cni != 0xfff))
      {
         if (cni == 0xDC3)
         {  // special case: "ARD/ZDF Gemeinsames Vormittagsprogramm"
            cni = (data[5 - 3] & 0x20) ? 0xDC1 : 0xDC2;
         }

         // decode VPS PIL
         mday   =  (data[11 - 3] & 0x3e) >> 1;
         month  = ((data[12 - 3] & 0xe0) >> 5) | ((data[11 - 3] & 1) << 3);
         hour   =  (data[12 - 3] & 0x1f);
         minute =  (data[13 - 3] >> 2);

         dprintf5("AddVpsData: CNI 0x%04X, PIL %d.%d. %02d:%02d\n", cni, mday, month, hour, minute);

         // check the date and time and assemble them to a PIL
         pil = TtxDecode_AssemblePil(mday, month, hour, minute);

         // pass the CNI and PIL into the VPS state machine
         TtxDecode_AddCni(CNI_TYPE_VPS, cni, pil);
      }
   }
}

// ---------------------------------------------------------------------------
// reverse bit order in one byte
//
static uchar TtxDecode_ReverseBitOrder( uchar b )
{
   int i;
   uchar result;

   result = b & 0x1;

   for (i=0; i < 7; i++)
   {
      b >>= 1;
      result <<= 1;

      result |= b & 0x1;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Parse packet 8/30 /1 and /2 for CNI and PIL
// - CNI values received here are converted to VPS CNI values, if the network
//   has a VPS code too. This avoids that the upper layers have to deal with
//   multiple CNIs per network; currently all Nextview providers use VPS codes
//   when available.
//
static void TtxDecode_GetP830Cni( const uchar * data )
{
   uchar pdcbuf[10];
   schar dc, c1;
   uint  mday, month, hour, minute;
   uint  cni, pil;
   sint  lto;
   uint  mjd, utc_h, utc_m, utc_s, tv;
   uint  idx;

   if ( UnHam84Nibble(data, &dc) )
   {
      dc >>= 1;

      if (dc == 0)
      {  // this is a packet 8/30/1
         cni = ((uint)TtxDecode_ReverseBitOrder(data[7]) << 8) |
                      TtxDecode_ReverseBitOrder(data[8]);
         if ((cni != 0) && (cni != 0xffff))
         {
            TtxDecode_AddCni(CNI_TYPE_NI, cni, INVALID_VPS_PIL);
         }
         TtxDecode_AddText(CNI_TYPE_NI, data + 20);

         lto = ((data[9] & 0x7F) >> 1) * 30*60;
         if ((data[9] & 0x80) == 0)
            lto = 0 - lto;

         mjd = + ((data[10] & 15) - 1) * 10000
               + ((data[11] >> 4) - 1) * 1000
               + ((data[11] & 15) - 1) * 100
               + ((data[12] >> 4) - 1) * 10
               + ((data[12] & 15) - 1);

         utc_h = ((data[13] >> 4) - 1) * 10 + ((data[13] & 15) - 1);
         utc_m = ((data[14] >> 4) - 1) * 10 + ((data[14] & 15) - 1);
         utc_s = ((data[15] >> 4) - 1) * 10 + ((data[15] & 15) - 1);

         if ( (utc_h < 24) && (utc_m < 60) && (utc_s < 60) &&
              (mjd >= 40587) && (lto <= 12*60*60) && (lto >= -12*60*60) )
         {
            tv = ((mjd - 40587) * (24*60*60)) + (utc_h * 60*60) + (utc_m * 60) + utc_s;

            TtxDecode_AddTime(CNI_TYPE_NI, tv, lto);
         }
      }
      else if (dc == 1)
      {  // this is a packet 8/30/2 (PDC)
         for (idx=0; idx < 9; idx++)
         {
            if ((c1 = (schar) unhamtab[data[9 + idx]]) >= 0)
            {  // CNI and PIL are transmitted MSB first -> must reverse bit order of all nibbles
               pdcbuf[idx] = reverse4Bits[(uchar) c1];
            }
            else  // decoding error -> abort
               break;
         }

         if (idx >= 9)
         {  // no hamming decoding errors
            // decode CNI and PIL as specified in ETS 231, chapter 8.2.1

            cni = (pdcbuf[0] << 12) | ((pdcbuf[6] & 0x3) << 10) | ((pdcbuf[7] & 0xc) << 6) |
                  ((pdcbuf[1] & 0xc) << 4) | ((pdcbuf[7] & 0x3) << 4) | (pdcbuf[8] & 0xf);

            if ((cni != 0) && (cni != 0xffff))
            {
               mday   = ((pdcbuf[1] & 0x3) << 3) | ((pdcbuf[2] & 0xe) >> 1);
               month  = ((pdcbuf[2] & 0x1) << 3) | ((pdcbuf[3] & 0xe) >> 1);
               hour   = ((pdcbuf[3] & 0x1) << 4) | pdcbuf[4];
               minute = (pdcbuf[5] << 2) | ((pdcbuf[6] & 0xc) >> 2);

               pil = TtxDecode_AssemblePil(mday, month, hour, minute);
               TtxDecode_AddCni(CNI_TYPE_PDC, cni, pil);
            }
         }
         TtxDecode_AddText(CNI_TYPE_PDC, data + 20);
      }
      #if DEBUG_SWITCH_STREAM == ON
      else
         debug1("TtxDecode-GetP830Cni: unknown DC %d - discarding packet", dc);
      #endif
   }
}

// ---------------------------------------------------------------------------
// Return reception statistics collected by slave process/thread
//
void TtxDecode_GetStatistics( TTX_DEC_STATS * pStats, time_t * pStatsStart )
{
   // check if initialization for the current channel is complete
   if (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf)
   {
      *pStats = pVbiBuf->ttxStats;
      *pStatsStart = ttxStatsStart;
   }
   else
   {
      memset(pStats, 0, sizeof(*pStats));
      *pStatsStart = time(NULL);
   }
}

// ---------------------------------------------------------------------------
// Store teletext page headers in rolling buffer
// - note: not a ring buffer, i.e. writer does not wait for reader
//
static void TtxDecode_AddPageHeader( uint pageNo, uint ctrl, const uchar * data )
{
   bool do_add;
   uint mag;
   uint idx;

   if (pVbiBuf->ttxHeader.write_lock == FALSE)
   {
      if (pVbiBuf->ttxHeader.op_mode == EPGACQ_TTX_HEAD_DEC)
      {
         // add pages with decimal numbers only (only 2nd and 3d digit can be hex)
         do_add = ((pageNo & 0x0f) <= 9) && (((pageNo >> 4) & 0x0f) <= 9);
      }
      else if (pVbiBuf->ttxHeader.op_mode == EPGACQ_TTX_HEAD_ALL)
      {
         do_add = TRUE;
      }
      else // EPGACQ_TTX_HEAD_NONE
         do_add = FALSE;

      if (do_add)
      {
         idx = pVbiBuf->ttxHeader.write_idx;
         if ((idx >= EPGACQ_ROLL_HEAD_COUNT) || (idx > pVbiBuf->ttxHeader.fill_cnt))
            idx = 0;

         memcpy((char*)pVbiBuf->ttxHeader.ring_buf[idx].data, data, 40);
         pVbiBuf->ttxHeader.ring_buf[idx].pageno = pageNo;
         pVbiBuf->ttxHeader.ring_buf[idx].ctrl_lo = ctrl & 0xffff;
         pVbiBuf->ttxHeader.ring_buf[idx].ctrl_hi = ctrl >> 16;

         pVbiBuf->ttxHeader.write_ind += 1;  // overflow ok
         pVbiBuf->ttxHeader.write_idx = (pVbiBuf->ttxHeader.write_idx + 1) % EPGACQ_ROLL_HEAD_COUNT;
         if (pVbiBuf->ttxHeader.fill_cnt < EPGACQ_ROLL_HEAD_COUNT)
            pVbiBuf->ttxHeader.fill_cnt += 1;
      }

      // page number sequence statistics
      if (pVbiBuf->ttxHeader.magPgResetReq != pVbiBuf->ttxHeader.magPgResetCnf)
      {
         memset((void*)pVbiBuf->ttxHeader.pg_no_ring, 0xff, sizeof(pVbiBuf->ttxHeader.pg_no_ring));
         pVbiBuf->ttxHeader.ring_val_cnt = 0;
         pVbiBuf->ttxHeader.ring_wr_idx = 0;
         pVbiBuf->ttxHeader.magPgDirection = 0;
         pVbiBuf->ttxHeader.magPgResetCnf = pVbiBuf->ttxHeader.magPgResetReq;
      }
      if ( ((pageNo & 0x0f) <= 9) && (((pageNo >> 4) & 0x0f) <= 9) )
      {
         pVbiBuf->ttxHeader.ring_wr_idx += 1;
         if (pVbiBuf->ttxHeader.ring_wr_idx >= EPGACQ_PG_NO_RING_SIZE)
           pVbiBuf->ttxHeader.ring_wr_idx = 0;

         pVbiBuf->ttxHeader.pg_no_ring[pVbiBuf->ttxHeader.ring_wr_idx] = pageNo;

         if (pVbiBuf->ttxHeader.ring_val_cnt < EPGACQ_PG_NO_RING_SIZE)
           pVbiBuf->ttxHeader.ring_val_cnt += 1;
      }

      // page number direction prediction
      mag = (pageNo >> 8) & 7;
      if (acqSlaveState.mags[mag].curPageNo != ~0u)
      {
         if (pageNo < acqSlaveState.mags[mag].curPageNo)
            pVbiBuf->ttxHeader.magPgDirection -= 1;
         else if (pageNo > acqSlaveState.mags[mag].curPageNo)
            pVbiBuf->ttxHeader.magPgDirection += 1;
      }
   }
}

// ---------------------------------------------------------------------------
// Retrieve last page header
// - the buffer must have space for 40 bytes
//
bool TtxDecode_GetPageHeader( uchar * pBuf, uint * pPgNum, uint pkgOff )
{
   uint idx;
   bool result = FALSE;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->ttxHeader.fill_cnt >= 1 + pkgOff) )
   {
      pVbiBuf->ttxHeader.write_lock = TRUE;

      idx = (pVbiBuf->ttxHeader.write_idx + (EPGACQ_ROLL_HEAD_COUNT - (pkgOff + 1))) % EPGACQ_ROLL_HEAD_COUNT;
      memcpy(pBuf, (char *)pVbiBuf->ttxHeader.ring_buf[idx].data, 40);
      *pPgNum = pVbiBuf->ttxHeader.ring_buf[idx].pageno;

      pVbiBuf->ttxHeader.write_lock = FALSE;
      result = TRUE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Retrieve per-magazine page count statistics
// - the magazine counter buffer must be large enough for 8 integers
//
bool TtxDecode_GetMagStats( uint * pMagBuf, sint * pPgDirection, bool reset )
{
   uint idx;
   bool result = FALSE;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->ttxHeader.magPgResetReq == pVbiBuf->ttxHeader.magPgResetCnf) )
   {
      if (pMagBuf != NULL)
      {
         for (idx = 0; idx < 8; idx++)
         {
            pMagBuf[idx] = 0;
         }
         for (idx = 0; idx < pVbiBuf->ttxHeader.ring_val_cnt; idx++)
         {
            uint page = pVbiBuf->ttxHeader.pg_no_ring[idx];
            pMagBuf[(page >> 8) & 7] += 1;
         }
         *pPgDirection = pVbiBuf->ttxHeader.magPgDirection;
      }
      if (reset)
      {
         pVbiBuf->ttxHeader.magPgResetReq += 1;
      }
      result = TRUE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// The following functions implement a ring buffer for incoming EPG packets
//
//    All icoming EPG packets are stored in a ring buffer to
//    de-couple the time-critical (real-time!) teletext decoder
//    from the rest of the database and user-interface handling.
//

// ---------------------------------------------------------------------------
// Retrieve the next available teletext packet from the ring buffer
// - at this point the EPG database process/thread takes the data from the
//   teletext acquisition process
// - note that the buffer slot is not yet freed in this function;
//   that has to be done separately after the packet was processed
//
const VBI_LINE * TtxDecode_GetPacket( uint pkgOff )
{
   const VBI_LINE * pVbl = NULL;
   uint pkgIdx;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) )
   {
      assert((pVbiBuf->reader_idx <= TTXACQ_BUF_COUNT) && (pVbiBuf->writer_idx <= TTXACQ_BUF_COUNT));

      pkgIdx = (pVbiBuf->reader_idx + pkgOff) % TTXACQ_BUF_COUNT;

      if (pkgIdx != acqSlaveState.vbiMaxReaderIdx)
      {
         pVbl = (const VBI_LINE *) &pVbiBuf->line[pkgIdx];

         #if DUMP_TTX_PACKETS == ON
         // dump the complete parity decoded content of the TTX packet
         DebugDumpTeletextPkg("TTX", pVbl->data, pVbl->frame, pVbl->line, pVbl->pkgno, pVbl->pageno, pVbl->ctrl_lo & 0x3F7F, TRUE);
         //printf("TTX frame=%d line %2d: pkg=%2d page=%03X sub=%04X '%s' BP=0x%02x\n", pVbl->frame, pVbl->line, pVbl->pkgno, pVbl->pageno, pVbl->ctrl_lo & 0x3F7F, tmparr, pVbl->data[0]);
         #endif
      }
      else
      {
         #if DUMP_TTX_PACKETS == ON
         fflush(stdout);
         #endif
      }
   }
   return pVbl;
}

// ---------------------------------------------------------------------------
// Release all packets in the ring buffer
// - the index must be the value passed up by the "check" function
//
void TtxDecoder_ReleasePackets( void )
{
   if (pVbiBuf->reader_idx != pVbiBuf->writer_idx)
   {
      pVbiBuf->reader_idx = acqSlaveState.vbiMaxReaderIdx;
   }
}

// ---------------------------------------------------------------------------
// Check if there are packets in the buffer
//
bool TtxDecode_CheckForPackets( bool * pStopped )
{
   bool stopped = FALSE;
   bool result = FALSE;

   if (pVbiBuf != NULL)
   {
      acqSlaveState.vbiMaxReaderIdx = pVbiBuf->writer_idx;

      if (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf)
      {
         if (pVbiBuf->reader_idx != pVbiBuf->writer_idx)
         {
            result = TRUE;
         }

         if ((result == FALSE) && (pVbiBuf->hasFailed))
         {  // vbi slave has stopped acquisition -> inform master control
            debug0("TtxDecode-CheckForPackets: slave process has disabled acq");
            stopped = TRUE;
            result = FALSE;
         }
      }
      else
      {  // after channel change: skip the first frame

         if (pVbiBuf->hasFailed)
         {  // must inform master the acq has stopped, or acq will lock up in waiting for channel change completion
            debug0("TtxDecode-CheckForPackets: slave process has disabled acq while waiting for new channel");
            stopped = TRUE;
            result = FALSE;
         }
      }
   }
   else  // should never happen
      stopped = TRUE;

   if (pStopped != NULL)
      *pStopped = stopped;

   return result;
}

// ---------------------------------------------------------------------------
// Append a packet to the VBI buffer
//
static void TtxDecode_BufferAdd( uint pageNo, uint ctrl, uchar pkgno, const uchar * data, uint line )
{
   DBGONLY(static int overflow = 0;)

   assert((pVbiBuf->reader_idx <= TTXACQ_BUF_COUNT) && (pVbiBuf->writer_idx <= TTXACQ_BUF_COUNT));

   if (pVbiBuf->reader_idx != ((pVbiBuf->writer_idx + 1) % TTXACQ_BUF_COUNT))
   {
      #if DUMP_TTX_PACKETS == ON
      pVbiBuf->line[pVbiBuf->writer_idx].frame  = acqSlaveState.frameSeqNo;
      pVbiBuf->line[pVbiBuf->writer_idx].line   = line;
      #endif
      pVbiBuf->line[pVbiBuf->writer_idx].pageno   = pageNo;
      pVbiBuf->line[pVbiBuf->writer_idx].ctrl_lo  = ctrl & 0xFFFF;
      pVbiBuf->line[pVbiBuf->writer_idx].ctrl_hi  = ctrl >> 16;
      pVbiBuf->line[pVbiBuf->writer_idx].pkgno    = pkgno;

      memcpy((char *)pVbiBuf->line[pVbiBuf->writer_idx].data, data, 40);

      pVbiBuf->writer_idx = (pVbiBuf->writer_idx + 1) % TTXACQ_BUF_COUNT;

      DBGONLY(overflow = 0;)
   }
#if DEBUG_SWITCH == ON
   else
   {
      ifdebug0((overflow == 0), "TtxDecode-BufferAdd: VBI buffer overflow");
      overflow += 1;
   }
#endif
}

// ---------------------------------------------------------------------------
// Notification of synchronous channel change
// - this function must be called by acq before a new frequency is tuned.
//   it is not meant for uncontrolled external channel changes.
// - the ring buffer is no longer read, until the slave has read at least
//   two new frames (the first frame after a channel change is skipped
//   since it might contain data from the previous channel)
// - the slave records the index where he restarted for the new channel.
//   this index is copied to the reader in the master
//
void TtxDecode_NotifyChannelChange( volatile EPGACQ_BUF * pThisVbiBuf )
{
   dprintf0("TtxDecode-NotifyChannelChange\n");

   if (pThisVbiBuf == NULL)
   {
      // notify the slave of the channel change
      // this also initiates a state machine reset
      pVbiBuf->chanChangeReq += 1;
   }
   else
   {
      pThisVbiBuf->chanChangeReq += 1;
   }
}

// ---------------------------------------------------------------------------
// Notification of the start of VBI processing for a new video field
// - Executed inside the teletext slave process/thread
// - Must be called by the driver before any VBI lines of a new video fields
//   are processed (the driver may bundle the even/odd fields into one frame)
// - This function has two purposes:
//   1. Detect lost frames: when a complete frame is lost, it's quite likely that
//      a page header was lost, so we cannot assume that the following packets
//      still belong to the current page. Note: there's no need to reset the EPG
//      decoder (i.e. to throw away the current block) because the next processed
//      ttx packet will be a page header, which carries a continuation index (CI)
//      which will detect if an EPG page was lost.
//   2. Handle channel changes: after a channel change all buffers must be
//      cleared and the state machines must be resetted. Note that the initial
//      acq startup is handled as a channel change.
//
bool TtxDecode_NewVbiFrame( uint frameSeqNo )
{
   bool result = TRUE;

   if (pVbiBuf != NULL)
   {
      if (pVbiBuf->chanChangeReq != pVbiBuf->chanChangeCnf)
      {  // acq master signaled channel change (e.g. new tuner freq.)
         dprintf0("TtxDecode-NewVbiFrame: channel change - reset state\n");

         // reset all result values in shared memory
         memset((void *)&pVbiBuf->cnis, 0, sizeof(pVbiBuf->cnis));
         memset((void *)&pVbiBuf->ttxStats, 0, sizeof(pVbiBuf->ttxStats));
         ttxStatsStart = time(NULL);
         pVbiBuf->ttxHeader.fill_cnt = 0;
         pVbiBuf->ttxHeader.magPgResetReq = pVbiBuf->ttxHeader.magPgResetCnf - 1;

         // discard all data in the teletext packet buffer
         // (note: reader_idx can be written safely b/c the reader is locked until the change is confirmed)
         pVbiBuf->reader_idx    = 0;
         pVbiBuf->writer_idx    = 0;

         // reset internal ttx decoder state
         memset(&acqSlaveState, 0, sizeof(acqSlaveState));
         acqSlaveState.lastMag = 8;

         // skip the current and the following frame, since they could contain data from the previous channel
         acqSlaveState.skipFrames  = 2;

         // Indicate to the master that the reset request has been executed (must be set last)
         pVbiBuf->chanChangeCnf = pVbiBuf->chanChangeReq;

         // return FALSE to indicate that the current VBI frame must be discarded
         // (note: the driver must discard all buffered frames if its VBI buffer holds more than one frame)
         result = FALSE;
      }
      else
      {
         if (acqSlaveState.skipFrames > 0)
         {
            acqSlaveState.skipFrames -= 1;
         }
         else
         {
            // calculate running average of ttx packets per page (16-bit fix point arithmetics)
            if (pVbiBuf->ttxStats.frameCount > 0)
            {
               pVbiBuf->ttxStats.ttxPkgRate  = ( (pVbiBuf->ttxStats.ttxPkgRate * ((1 << TTX_PKG_RATE_FACT) - 1)) +
                                                 (acqSlaveState.pkgPerFrame << TTX_PKG_RATE_FIXP) +
                                                 (1 << (TTX_PKG_RATE_FACT - 1))
                                               ) >> TTX_PKG_RATE_FACT;
            }
            else
            {
               pVbiBuf->ttxStats.ttxPkgRate  = (acqSlaveState.pkgPerFrame << TTX_PKG_RATE_FIXP);
            }
            pVbiBuf->ttxStats.frameCount += 1;
         }

         if ((frameSeqNo != acqSlaveState.frameSeqNo + 1) && (frameSeqNo != 0))
         {  // mising frame (0 is special case: no support for seq.no.)
            #if DEBUG_SWITCH_STREAM == ON
            debug1("TtxDecode-NewVbiFrame: lost vbi frame #%u", acqSlaveState.frameSeqNo + 1);
            #endif
            memset(&acqSlaveState.mags, 0, sizeof(acqSlaveState.mags));
         }
      }

      acqSlaveState.frameSeqNo = frameSeqNo;
      acqSlaveState.pkgPerFrame = 0;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Process a received teletext packet, check if it belongs to EPG
// - note that this procedure is called by the teletext decoder process/thread
//   and hence can not access the state variables of the EPG process/thread
//   except for the shared buffer
//
void TtxDecode_AddPacket( const uchar * data, uint line )
{
   sint  tmp1, tmp2, tmp3, tmp4;
   uchar mag, pkgno;
   uint  pageNo;
   uint  ctrlBits;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) &&
        (acqSlaveState.skipFrames == 0) )
   {
      pVbiBuf->ttxStats.ttxPkgCount += 1;
      acqSlaveState.pkgPerFrame += 1;

      if (UnHam84Byte(data, &tmp1))
      {
         mag   = tmp1 & 7;
         pkgno = (tmp1 >> 3) & 0x1f;
         pageNo= ctrlBits = 0;

         if (pkgno == 0)
         {  // new teletext page header
            if (UnHam84Byte(data + 2, &tmp1) &&
                UnHam84Byte(data + 4, &tmp2) &&
                UnHam84Byte(data + 6, &tmp3) &&
                UnHam84Byte(data + 8, &tmp4))
            {
               pageNo = tmp1 | ((uint)mag << 8);
               ctrlBits = tmp2 | (tmp3 << 8) | (tmp4 << 16);

               acqSlaveState.magParallel = ((tmp3 & 0x10) == 0);

               if ( (acqSlaveState.magParallel == FALSE) &&
                    (mag != acqSlaveState.lastMag) && (pkgno < 30) &&
                    (acqSlaveState.lastMag < 8) )
               {
                  // serial mode: magazine change -> close previous page
                  acqSlaveState.mags[acqSlaveState.lastMag].curPageNo = ~0u;
                  acqSlaveState.mags[acqSlaveState.lastMag].fwdPage = FALSE;
               }

               if (pVbiBuf->scanEnabled)
               {
                  pVbiBuf->ttxStats.scanPkgCount += 1;
                  pVbiBuf->ttxStats.scanPagCount += 1;
               }
               else if ((pVbiBuf->ttxEnabled) &&
                        (pageNo >= pVbiBuf->startPageNo) && (pageNo <= pVbiBuf->stopPageNo))
               {
                  acqSlaveState.mags[mag].fwdPage = TRUE;
                  TtxDecode_BufferAdd(pageNo, ctrlBits, 0, data + 2, line);

                  pVbiBuf->ttxStats.ttxPkgGrab += 1;
                  pVbiBuf->ttxStats.ttxPagGrab += 1;
               }
               else
               {
                  acqSlaveState.mags[mag].fwdPage = FALSE;

                  // magazine inventory page - is decoded immediately by the ttx slave
                  acqSlaveState.mags[mag].isMipPage = ((pageNo & 0xFF) == 0xFD);
               }

               // save the last received page header
               TtxDecode_AddPageHeader(pageNo, ctrlBits, data + 2);

               acqSlaveState.mags[mag].curPageNo = pageNo;
               acqSlaveState.lastMag = mag;
            }
            else
            {  // missed a teletext page header due to hamming error
               pVbiBuf->ttxStats.ttxPkgDrop += 1;

               // close all previous pages in the same magazine
               if (acqSlaveState.mags[mag].fwdPage)
               {
                  #if DEBUG_SWITCH_STREAM == ON
                  debug1("TtxDecode-AddPacket: closing TTX page %03X after hamming err", acqSlaveState.mags[mag].curPageNo);
                  #endif
                  acqSlaveState.mags[mag].fwdPage = FALSE;
               }
               acqSlaveState.mags[mag].isMipPage = FALSE;
               acqSlaveState.mags[mag].curPageNo = ~0u;
            }
         }
         else
         {  // regular teletext packet (i.e. not a header)

            if ( ((pVbiBuf->scanEnabled) || (pVbiBuf->ttxEnabled)) && 
                 (acqSlaveState.mags[mag].fwdPage) &&
                 (pkgno < 30) )
            {
               TtxDecode_BufferAdd(acqSlaveState.mags[mag].curPageNo, 0, pkgno, data + 2, line);
               if (pVbiBuf->scanEnabled)
                  pVbiBuf->ttxStats.scanPkgCount += 1;

               pVbiBuf->ttxStats.ttxPkgGrab += 1;
            }
            else if ( acqSlaveState.mags[mag].isMipPage )
            {  // new MIP packet (may be received in parallel for several magazines)
               TtxDecode_MipPacket(mag, pkgno, data + 2);
            }
            else if ((pkgno == 30) && (mag == 0))
            {  // packet 8/30/1
               TtxDecode_GetP830Cni(data + 2);
            }
         }

         #if DUMP_TTX_PACKETS == ON
         // dump all TTX packets, even non-EPG ones
         DebugDumpTeletextPkg("SLAVE", data+2, acqSlaveState.frameSeqNo, line, pkgno, pageNo, ctrlBits & 0x3F7F, acqSlaveState.mags[mag].fwdPage);
         //printf("SLAVE frame=%d line %2d: pkg=%2d page=%03X sub=%04X %d '%s'\n", acqSlaveState.frameSeqNo, line, pkgno, pageNo, sub, acqSlaveState.mags[mag].fwdPage, tmparr);
         #endif
      }
      else
         pVbiBuf->ttxStats.ttxPkgDrop += 1;
   }
}
