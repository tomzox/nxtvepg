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
 *  $Id: ttxdecode.c,v 1.49 2002/05/30 13:57:45 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <string.h>
#include <stdio.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/cni_tables.h"
#include "epgvbi/hamming.h"
#include "epgvbi/btdrv.h"
#include "epgvbi/ttxdecode.h"


// ----------------------------------------------------------------------------
// Internal decoder status
//

// struct that holds the state of the slave process, i.e. ttx decoder
static struct
{
   bool       isEpgPage;         // current ttx page is the EPG page
   uchar      isMipPage;         // bitfield: current ttx page in magazine M is the MIP page for M=0..7
   uint       frameSeqNo;        // last reported VBI frame sequence number
   bool       skipFrames;        // skip first frames after channel change
   uint       pkgPerFrame;       // counter to determine teletext data rate (as ttx pkg per frame)
} acqSlaveState;

// max. number of pages that are allowed for EPG transmission: mFd & mdF: m[0..7],d[0..9]
#define NXTV_VALID_PAGE_PER_MAG (1 + 10 + 10)
#define NXTV_VALID_PAGE_COUNT   (8 * NXTV_VALID_PAGE_PER_MAG)

typedef struct
{
   uchar   pkgCount;
   uchar   lastPkg;
   uchar   okCount;
} PAGE_SCAN_STATE;

static PAGE_SCAN_STATE scanState[NXTV_VALID_PAGE_COUNT];
static int             lastMagIdx[8];

// ----------------------------------------------------------------------------
// Start EPG acquisition
//
void TtxDecode_StartEpgAcq( uint epgPageNo, bool isEpgScan )
{
   // pass the configuration variables to the ttx process via shared memory
   pVbiBuf->epgPageNo = epgPageNo;
   pVbiBuf->isEpgScan = isEpgScan;
   pVbiBuf->doVpsPdc  = TRUE;

   // enable ttx processing in the slave process/thread
   pVbiBuf->isEnabled = TRUE;

   // skip first VBI frame, reset ttx decoder, then set reader idx to writer idx
   pVbiBuf->chanChangeReq = pVbiBuf->chanChangeCnf + 2;
}

// ----------------------------------------------------------------------------
// Stop the acquisition
// - the external process may continue to collect data
//
void TtxDecode_StopAcq( void )
{
   // inform writer process/thread
   pVbiBuf->isEnabled = FALSE;
}

// ---------------------------------------------------------------------------
// Return CNIs and number of data pages found
// - result values are not invalidized, b/c the caller will change the
//   channel after a value was read anyways
//
void TtxDecode_GetScanResults( uint *pCni, bool *pNiWait, uint *pDataPageCnt, uchar *pDispText, uint textMaxLen )
{
   CNI_TYPE type;
   const uchar * p;
   uint     idx;
   uint     cni;
   uint     pageCnt;
   bool     niWait  = FALSE;

   cni     = 0;
   pageCnt = 0;
   niWait  = FALSE;

   if (textMaxLen > PDC_TEXT_LEN + 1)
      textMaxLen = PDC_TEXT_LEN + 1;

   // check if initialization for the current channel is complete
   if (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf)
   {
      // search for any available source
      for (type=0; type < CNI_TYPE_COUNT; type++)
      {
         if (pVbiBuf->cnis[type].haveCni)
         {  // have a verified CNI -> return it
            cni = pVbiBuf->cnis[type].outCni;
            break;
         }
         else if (pVbiBuf->cnis[type].cniRepCount > 0)
         {  // received at least one VPS or P8/30 packet -> wait for repetition
            niWait = TRUE;
         }
      }

      pageCnt = pVbiBuf->dataPageCount;

      if (pDispText != NULL)
      {
         pDispText[0] = 0;
         for (type=0; type < CNI_TYPE_COUNT; type++)
         {
            if (pVbiBuf->cnis[type].haveText)
            {
               // skip spaces from beginning of string
               p = (const char *) pVbiBuf->cnis[type].outText;
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
   if (pDataPageCnt != NULL)
      *pDataPageCnt = pageCnt;
}

// ---------------------------------------------------------------------------
// Return captured and verified CNI and PIL information
// - deletes the information after the query - this ensures that only valid
//   information is returned even after a channel change to a channel w/o CNI
// - CNI can be transmitted in 3 ways: VPS, PDC, NI. This function returns
//   values from the "best" available source (speed, reliability).
// - Note: for channels which have different IDs in VPS and PDC or NI, the
//   CNI value is converted to VPS or PDC respectively so that the caller
//   needs not care how the CNI was obtained.
//
bool TtxDecode_GetCniAndPil( uint * pCni, uint *pPil, volatile EPGACQ_BUF *pThisVbiBuf )
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
            if (pThisVbiBuf->cnis[type].haveCni)
            {
               if (pCni != NULL)
                  *pCni = pThisVbiBuf->cnis[type].outCni;

               if (pPil != NULL)
               {
                  if (pThisVbiBuf->cnis[type].havePil)
                  {
                     *pPil = pThisVbiBuf->cnis[type].outPil;
                     pThisVbiBuf->cnis[type].havePil = FALSE;
                  }
                  else
                     *pPil = INVALID_VPS_PIL;
               }
               result = TRUE;
               break;
            }
         }

         // Reset all result values (but not the entire state machine,
         // i.e. repetition counters stay at 2 so that any newly received
         // CNI will immediately make an result available again)
         for (type=0; type < CNI_TYPE_COUNT; type++)
         {
            pThisVbiBuf->cnis[type].haveCni = FALSE;
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
         if ((cni != 0) && (cni != 0xffff) && (cni != 0x0fff))
         {
            DBGONLY( if (pVbiBuf->cnis[type].cniRepCount == 0) )
               dprintf3("TtxDecode-AddCni: new CNI 0x%04X, PIL=%X, type %d\n", cni, pil, type);

            if ( (pVbiBuf->cnis[type].cniRepCount > 0) &&
                 (pVbiBuf->cnis[type].lastCni != cni) )
            {  // comparison failure -> reset repetition counter
               debug3("TtxDecode-AddCni: %s CNI error: last %04X != %04X", ((type == CNI_TYPE_VPS) ? "VPS" : ((type == CNI_TYPE_PDC) ? "PDC" : "NI")), pVbiBuf->cnis[type].lastCni, cni);
               pVbiBuf->cnis[type].cniRepCount = 0;
            }
            pVbiBuf->cnis[type].lastCni = cni;
            pVbiBuf->cnis[type].cniRepCount += 1;

            if (pVbiBuf->cnis[type].cniRepCount > 2)
            {  // the same CNI value received 3 times -> make it available as result

               if (pVbiBuf->cnis[type].havePil && (pVbiBuf->cnis[type].outCni != cni))
               {  // CNI result value changed -> remove PIL
                  pVbiBuf->cnis[type].havePil = FALSE;
               }
               pVbiBuf->cnis[type].outCni = cni;
               // set flag that CNI is available, but only if not already set to avoid concurrency problems
               if (pVbiBuf->cnis[type].haveCni == FALSE)
                  pVbiBuf->cnis[type].haveCni = TRUE;
            }

            if (pil != INVALID_VPS_PIL)
            {
               if ( (pVbiBuf->cnis[type].pilRepCount > 0) &&
                    (pVbiBuf->cnis[type].lastPil != pil) )
               {  // comparison failure -> reset repetition counter
                  debug11("TtxDecode-AddCni: %s PIL error: last %02d.%02d. %02d:%02d (0x%04X) != %02d.%02d. %02d:%02d (0x%04X)", ((type == CNI_TYPE_VPS) ? "VPS" : ((type == CNI_TYPE_PDC) ? "PDC" : "NI")), (pVbiBuf->cnis[type].lastPil >> 15) & 0x1F, (pVbiBuf->cnis[type].lastPil >> 11) & 0x0F, (pVbiBuf->cnis[type].lastPil >> 6) & 0x1F, pVbiBuf->cnis[type].lastPil & 0x3F, pVbiBuf->cnis[type].lastPil, (pil >> 15) & 0x1F, (pil >> 11) & 0x0F, (pil >> 6) & 0x1F, pil & 0x3F, pil);
                  pVbiBuf->cnis[type].pilRepCount = 0;
               }
               pVbiBuf->cnis[type].lastPil = pil;
               pVbiBuf->cnis[type].pilRepCount += 1;

               if (pVbiBuf->cnis[type].pilRepCount > 2)
               {
                  // don't save as result if CNI is unknown or does not match the last CNI
                  if (pVbiBuf->cnis[type].haveCni && (pVbiBuf->cnis[type].outCni == cni))
                  {
                     pVbiBuf->cnis[type].outPil = pil;
                     // set flag that PIL is available, but only if not already set to avoid concurrency problems
                     if (pVbiBuf->cnis[type].havePil == FALSE)
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
// Decode an incoming MIP packet
// - note that this runs inside of the ttx sub-thread
// - if an EPG id is found, it is passed to the acq control via shared memory
// - only table cells which refer to pages allowed for Nextview are decoded
//
static void TtxDecode_MipPacket( uchar magNo, uchar pkgNo, const uchar *data )
{
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
}

// ---------------------------------------------------------------------------
// Obtain EPG page number found in MIP
// - returns 0 if MIP not received or EPG not listed in MIP
//
uint TtxDecode_GetMipPageNo( void )
{
   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) )
   {
      return pVbiBuf->mipPageNo;
   }
   else
      return 0;
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

   if ( (pVbiBuf != NULL) && (pVbiBuf->doVpsPdc) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) &&
        (acqSlaveState.skipFrames == 0) )
   {
      pVbiBuf->ttxStats.vpsLineCount += 1;

      cni = ((data[13] & 0x3) << 10) | ((data[14] & 0xc0) << 2) |
            ((data[11] & 0xc0)) | (data[14] & 0x3f);

      if ((cni != 0) && (cni != 0xfff))
      {
         if (cni == 0xDC3)
         {  // special case: "ARD/ZDF Gemeinsames Vormittagsprogramm"
            cni = (data[5] & 0x20) ? 0xDC1 : 0xDC2;
         }

         // decode VPS PIL
         mday   =  (data[11] & 0x3e) >> 1;
         month  = ((data[12] & 0xe0) >> 5) | ((data[11] & 1) << 3);
         hour   =  (data[12] & 0x1f);
         minute =  (data[13] >> 2);

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
            cni = CniConvertP8301ToVps(cni);
            TtxDecode_AddCni(CNI_TYPE_NI, cni, INVALID_VPS_PIL);
         }
         TtxDecode_AddText(CNI_TYPE_NI, data + 20);
      }
      else if (dc == 1)
      {  // this is a packet 8/30/2 (PDC)
         for (idx=0; idx < 9; idx++)
         {
            if ((c1 = (schar) unhamtab[data[9 + idx]]) >= 0)
            {  // CNI and PIL are transmitted MSB first -> must reverse bit order of all nibbles
               pdcbuf[idx] = reverse4Bits[(uint) c1];
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

               cni = CniConvertPdcToVps(cni);
               pil = TtxDecode_AssemblePil(mday, month, hour, minute);
               TtxDecode_AddCni(CNI_TYPE_PDC, cni, pil);
            }
         }
         TtxDecode_AddText(CNI_TYPE_PDC, data + 20);
      }
      else
         debug1("TtxDecode-GetP830Cni: unknown DC %d - discarding packet", dc);
   }
}

// ---------------------------------------------------------------------------
// Return statistics collected by slave process/thread during acquisition
//
void TtxDecode_GetStatistics( uint32_t * pTtxPkgCount, uint32_t * pVbiLineCount,
                              uint32_t * pEpgPkgCount, uint32_t * pEpgPagCount )
{
   // check if initialization for the current channel is complete
   if (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf)
   {
      *pTtxPkgCount = pVbiBuf->ttxStats.ttxPkgCount;
      *pEpgPkgCount = pVbiBuf->ttxStats.epgPkgCount;
      *pEpgPagCount = pVbiBuf->ttxStats.epgPagCount;
   }
   else
   {
      *pTtxPkgCount = 0;
      *pEpgPkgCount = 0;
      *pEpgPagCount = 0;
   }
}

// ---------------------------------------------------------------------------
// Scan EPG page header syntax
// - checks if page number is suggested EPG page number 0x1DF, mdF or mFd
//   (where <m> stands for magazine number, <d> for 0..9 and F for 0x0F)
//   and stream number in page subcode is 0 or 1.
//
static void TtxDecode_EpgScanHeader( uint page, uint sub )
{
   uint d1, d2;
   int idx;

   if ( ((sub & 0xf00) >> 8) < 2 )
   {
      // Compute an index into the page state array for a potential EPG page
      d1 = page & 0x0f;
      d2 = (page >> 4) & 0x0f;

      if ((page & 0xff) == 0xdf)
         idx = 0;
      else if ( (d1 == 0xf) && (d2 < 0xa) )
         idx = 1 + d2;
      else if ( (d2 == 0xf) && (d1 < 0xa) )
         idx = 11 + d1;
      else
         idx = -1;

      if (idx >= 0)
      {
         idx += ((page >> 8) & 0x07) * NXTV_VALID_PAGE_PER_MAG;
         dprintf2("TtxDecode-EpgScanHeader: found possible EPG page %03X, idx=%d\n", page, idx);

         scanState[idx].pkgCount = ((sub & 0x3000) >> (12-3)) | ((sub & 0x70) >> 4);
         scanState[idx].lastPkg = 0;
      }
   }
   else
   {  // invalid stream number
      idx = -1;
   }

   lastMagIdx[(page >> 8) & 0x07] = idx;
}

// ---------------------------------------------------------------------------
// Scan EPG data packet syntax
// - this is done in a very limited and simple way: only BP and BS are
//   checked, plus SH for hamming errors
// - a page is considered ok if at least two third of the packets are
//   syntactically correct
//
static bool TtxDecode_EpgScanPacket( uchar mag, uchar packNo, const uchar * dat )
{
   PAGE_SCAN_STATE *psc;
   schar bs, bp, c1, c2, c3, c4;
   bool result = FALSE;

   if ((mag < 8) && (lastMagIdx[mag] >= 0))
   {
      psc = &scanState[lastMagIdx[mag]];
      if (packNo > psc->lastPkg)
      {
         if (packNo <= psc->pkgCount)
         {
            if ( UnHam84Nibble(dat, &bp) )
            {
               if (bp < 0x0c)
               {
                  bp = 1 + 3 * bp;

                  if ( UnHam84Nibble(dat + bp, &bs) && (bs == 0x0c) &&
                       UnHam84Nibble(dat + bp + 1, &c1) &&
                       UnHam84Nibble(dat + bp + 2, &c2) &&
                       UnHam84Nibble(dat + bp + 3, &c3) &&
                       UnHam84Nibble(dat + bp + 4, &c4) )
                  {
                     psc->okCount++;
                  }
               }
               else if (bp == 0x0c)
               {
                  bp = 1 + 3 * bp;

                  if ( UnHam84Nibble(dat + bp, &bs) && (bs == 0x0c) &&
                       UnHam84Nibble(dat + bp + 1, &c1) &&
                       UnHam84Nibble(dat + bp + 2, &c2) )
                  {
                     psc->okCount++;
                  }
               }
               else if (bp == 0x0d)
               {
                  psc->okCount++;
               }

               if (psc->okCount >= 16)
               {  // this page has had enough syntactically correct packages
                  dprintf2("TtxDecode-EpgScanPacket: syntax ok: mag=%d, idx=%d\n", mag, lastMagIdx[mag]);
                  lastMagIdx[mag] = -1;
                  result = TRUE;
               }
            }
         }
      }
      else
         lastMagIdx[mag] = -1;
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
// - note that the buffer slot is not yet freed in this functions;
//   that has to be done separately after the packet was processed
//
const VBI_LINE * TtxDecode_GetPacket( bool freePrevPkg )
{
   const VBI_LINE * pVbl = NULL;

   if ( (pVbiBuf != NULL) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) )
   {
      assert((pVbiBuf->reader_idx <= EPGACQ_BUF_COUNT) && (pVbiBuf->writer_idx <= EPGACQ_BUF_COUNT));

      if ( freePrevPkg )
      {
         if (pVbiBuf->reader_idx != pVbiBuf->writer_idx)
         {
            pVbiBuf->reader_idx = (pVbiBuf->reader_idx + 1) % EPGACQ_BUF_COUNT;
         }
         else
            fatal0("TtxDecode-FreePacket: buffer is already empty");
      }

      if (pVbiBuf->reader_idx != pVbiBuf->writer_idx)
      {
         pVbl = (const VBI_LINE *) &pVbiBuf->line[pVbiBuf->reader_idx];
      }
   }
   return pVbl;
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
      if (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf)
      {
         result = (pVbiBuf->reader_idx != pVbiBuf->writer_idx);

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
static void TtxDecode_BufferAdd( uint pageNo, uint sub, uchar pkgno, const uchar * data )
{
   DBGONLY(static int overflow = 0;)

   assert((pVbiBuf->reader_idx <= EPGACQ_BUF_COUNT) && (pVbiBuf->writer_idx <= EPGACQ_BUF_COUNT));

   if (pVbiBuf->reader_idx != ((pVbiBuf->writer_idx + 1) % EPGACQ_BUF_COUNT))
   {
      pVbiBuf->line[pVbiBuf->writer_idx].pageno = pageNo;
      pVbiBuf->line[pVbiBuf->writer_idx].sub    = sub;
      pVbiBuf->line[pVbiBuf->writer_idx].pkgno  = pkgno;

      memcpy((char *)pVbiBuf->line[pVbiBuf->writer_idx].data, data, 40);

      pVbiBuf->writer_idx = (pVbiBuf->writer_idx + 1) % EPGACQ_BUF_COUNT;

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

   if ((pVbiBuf != NULL) && (pVbiBuf->isEnabled))
   {
      if (pVbiBuf->chanChangeReq != pVbiBuf->chanChangeCnf)
      {  // acq master signaled channel change (e.g. new tuner freq.)
         dprintf0("TtxDecode-NewVbiFrame: channel change - reset state\n");

         // reset all result values in shared memory
         memset((void *)&pVbiBuf->cnis, 0, sizeof(pVbiBuf->cnis));
         memset((void *)&pVbiBuf->ttxStats, 0, sizeof(pVbiBuf->ttxStats));
         pVbiBuf->lastHeader.pageno = 0xffff;
         pVbiBuf->mipPageNo     = 0;
         pVbiBuf->dataPageCount = 0;

         // Initialize state of EPG syntax scan
         if (pVbiBuf->isEpgScan)
         {
            // set all indices to -1
            memset(lastMagIdx, 0xff, sizeof(lastMagIdx));
            // set all ok counters to 0
            memset(scanState, 0, sizeof(scanState));
         }

         // discard all data in the teletext packet buffer
         // (note: reader_idx can be written safely b/c the reader is locked until the change is confirmed)
         pVbiBuf->reader_idx    = 0;
         pVbiBuf->writer_idx    = 0;

         // reset internal ttx decoder state
         acqSlaveState.isEpgPage   = FALSE;
         acqSlaveState.isMipPage   = 0;
         acqSlaveState.pkgPerFrame = 0;

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
            debug1("TtxDecode-NewVbiFrame: lost vbi frame #%u", acqSlaveState.frameSeqNo + 1);
            acqSlaveState.isEpgPage = FALSE;
            acqSlaveState.isMipPage = 0;
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
void TtxDecode_AddPacket( const uchar * data )
{
   sint  tmp1, tmp2, tmp3;
   uchar mag, pkgno;
   uint  pageNo;
   uint  sub;

   if ( (pVbiBuf != NULL) && (pVbiBuf->isEnabled) &&
        (pVbiBuf->chanChangeReq == pVbiBuf->chanChangeCnf) &&
        (acqSlaveState.skipFrames == 0) )
   {
      pVbiBuf->ttxStats.ttxPkgCount += 1;
      acqSlaveState.pkgPerFrame += 1;

      if (UnHam84Byte(data, &tmp1))
      {
         mag   = tmp1 & 7;
         pkgno = (tmp1 >> 3) & 0x1f;

         if (pkgno == 0)
         {  // new teletext page header
            if (UnHam84Byte(data + 2, &tmp1) &&
                UnHam84Byte(data + 4, &tmp2) &&
                UnHam84Byte(data + 6, &tmp3))
            {
               pageNo = tmp1 | ((uint)mag << 8);
               sub    = (tmp2 | (tmp3 << 8)) & 0x3f7f;

               if (pageNo == pVbiBuf->epgPageNo)
               {
                  acqSlaveState.isEpgPage = TRUE;
                  TtxDecode_BufferAdd(pageNo, sub, 0, data + 2);

                  pVbiBuf->ttxStats.epgPkgCount += 1;
                  pVbiBuf->ttxStats.epgPagCount += 1;
               }
               else
               {
                  if ( mag == (pVbiBuf->epgPageNo >> 8) )
                  {  // same magazine, but not the EPG page -> previous EPG page closed
                     acqSlaveState.isEpgPage = FALSE;
                  }

                  if ( (pageNo & 0xFF) == 0xFD )
                  {  // magazine inventory page - is decoded immediately by the ttx client
                     acqSlaveState.isMipPage |= (1 << mag);
                  }
                  else
                     acqSlaveState.isMipPage &= ~(1 << mag);
               }

               if (pVbiBuf->isEpgScan)
                  TtxDecode_EpgScanHeader(pageNo, sub);

               // save the last received page header
               memcpy((char*)pVbiBuf->lastHeader.data, data + 2, 40);
               pVbiBuf->lastHeader.pageno = pageNo;
               pVbiBuf->lastHeader.sub    = sub;
            }
            else
            {  // missed a teletext page header due to hamming error
               pVbiBuf->ttxStats.ttxPkgDrop += 1;

               // close all previous pages in the same magazine
               if ( (acqSlaveState.isEpgPage) &&
                    (mag == (pVbiBuf->epgPageNo >> 8)) )
               {
                  debug0("TtxDecode-AddPacket: closing EPG page after hamming err");
                  acqSlaveState.isEpgPage = FALSE;
               }
               else if (acqSlaveState.isMipPage & (1 << mag) )
               {
                  acqSlaveState.isMipPage &= ~(1 << mag);
               }
            }
         }
         else
         {  // regular teletext packet (i.e. not a header)

            if ( acqSlaveState.isEpgPage &&
                 (mag == (pVbiBuf->epgPageNo >> 8)) &&
                 (pkgno < 26) )
            {  // new EPG packet
               TtxDecode_BufferAdd(0, 0, pkgno, data + 2);
               pVbiBuf->ttxStats.epgPkgCount += 1;
            }
            else if ( acqSlaveState.isMipPage & (1 << mag) )
            {  // new MIP packet (may be received in parallel for several magazines)
               TtxDecode_MipPacket(mag, pkgno, data + 2);
            }
            else if (pVbiBuf->doVpsPdc && (pkgno == 30) && (mag == 0))
            {  // packet 8/30/1 is evaluated for CNI during EPG scan
               TtxDecode_GetP830Cni(data + 2);
            }
            // packets from other magazines (e.g. in parallel mode) and 8/30 have to be ignored

            if (pVbiBuf->isEpgScan)
               if (TtxDecode_EpgScanPacket(mag, pkgno, data + 2))
                  pVbiBuf->dataPageCount += 1;
         }
      }
      else
         pVbiBuf->ttxStats.ttxPkgDrop += 1;
   }
}

