/*
 *  Nextview EPG block decoder
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
 *  $Id: epgdbacq.c,v 1.35 2002/01/13 18:46:36 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <string.h>
#include <stdio.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/cni_tables.h"
#include "epgvbi/hamming.h"
#include "epgvbi/btdrv.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgqueue.h"
#include "epgdb/epgdbacq.h"
#include "epgdb/epgstream.h"


// ----------------------------------------------------------------------------
// internal decoder status
//

#define HEADER_CHECK_LEN         12
#define HEADER_CHECK_LEN_STR    "12"   // for debug output
#define HEADER_CHECK_MAX_ERRORS   2

typedef struct
{
   uint          epgPageNo;
   bool          isEpgPage;
   bool          bEpgDbAcqEnabled;
   bool          bHeaderCheckInit;
   bool          bNewChannel;
   uchar         lastPageHeader[HEADER_CHECK_LEN];
} EPGDBACQ_STATE;

static EPGDBACQ_STATE dbAcqState;


// ----------------------------------------------------------------------------
// Initialize the internal state
//
void EpgDbAcqInit( void )
{
   dbAcqState.bEpgDbAcqEnabled = FALSE;

   pVbiBuf->writer_idx = 0;
   pVbiBuf->reader_idx = 0;
   pVbiBuf->isEnabled = FALSE;
}

// ----------------------------------------------------------------------------
// Start the acquisition
//
void EpgDbAcqStart( EPGDB_CONTEXT *dbc, EPGDB_QUEUE * pQueue, uint pageNo, uint appId )
{
   uint epgAppId;
   bool bWaitForBiAi;

   if (dbAcqState.bEpgDbAcqEnabled == FALSE)
   {
      dbAcqState.isEpgPage = FALSE;
      dbAcqState.bHeaderCheckInit = FALSE;

      if (pageNo != EPG_ILLEGAL_PAGENO)
         dbAcqState.epgPageNo = dbc->pageNo = pageNo;
      else if ((dbc->pageNo != EPG_ILLEGAL_PAGENO) && VALID_EPG_PAGENO(dbc->pageNo))
         dbAcqState.epgPageNo = dbc->pageNo;
      else
         dbAcqState.epgPageNo = dbc->pageNo = EPG_DEFAULT_PAGENO;

      if (appId != EPG_ILLEGAL_APPID)
         epgAppId = appId;
      else if (dbc->appId != EPG_ILLEGAL_APPID)
         epgAppId = dbc->appId;
      else
         epgAppId = EPG_DEFAULT_APPID;

      // clear the ring buffer
      // (must not modify the writer index, which belongs to another process/thread)
      pVbiBuf->reader_idx = pVbiBuf->writer_idx;
      pVbiBuf->frameSeqNo = 0;  // XXX not MT-safe
      dbAcqState.bNewChannel = TRUE;
      // pass the configuration variables to the ttx process via IPC
      // and reset the ttx decoder state
      pVbiBuf->epgPageNo = dbAcqState.epgPageNo;
      pVbiBuf->mipPageNo = 0;
      pVbiBuf->dataPageCount = 0;
      pVbiBuf->isEpgPage = FALSE;
      pVbiBuf->isMipPage = 0;
      pVbiBuf->isEpgScan = FALSE;
      // reset CNI state machine
      memset(pVbiBuf->cnis, 0, sizeof(pVbiBuf->cnis));
      pVbiBuf->doVpsPdc  = TRUE;
      // enable acquisition in the slave process/thread
      pVbiBuf->isEnabled = TRUE;
      // reset statistics variables
      pVbiBuf->ttxPkgCount = 0;
      pVbiBuf->epgPkgCount = 0;
      pVbiBuf->epgPagCount = 0;

      if (dbc->pAiBlock != NULL)
      {  // provider already known
         // set up a list of alphabets for string decoding
         EpgBlockSetAlphabets(&dbc->pAiBlock->blk.ai);
         // since alphabets are known PI can be collected right from the start
         bWaitForBiAi = FALSE;
      }
      else
      {  // unknown provider -> wait for AI before allowing PI
         bWaitForBiAi = TRUE;
      }

      // initialize the state of the streams decoder
      EpgStreamInit(pQueue, bWaitForBiAi, epgAppId);

      dbAcqState.bEpgDbAcqEnabled = TRUE;
   }
   else
      debug0("EpgDbAcq-Start: already running");
}

// ----------------------------------------------------------------------------
// Stop the acquisition
// - the external process may continue to collect data
//
void EpgDbAcqStop( void )
{
   if (dbAcqState.bEpgDbAcqEnabled)
   {
      // inform writer process/thread
      pVbiBuf->isEnabled = FALSE;

      EpgStreamClear();

      dbAcqState.bEpgDbAcqEnabled = FALSE;
   }
   else
      debug0("EpgDbAcq-Stop: already stopped");
}

// ---------------------------------------------------------------------------
// Stop and Re-Start the acquisition
// - called after change of channel or internal parameters
//
void EpgDbAcqReset( EPGDB_CONTEXT *dbc, EPGDB_QUEUE * pQueue, uint pageNo, uint appId )
{
   if (dbAcqState.bEpgDbAcqEnabled)
   {
      // discard remaining blocks in the scratch buffer
      EpgStreamClear();

      // set acq state to OFF internally only to satisfy start-acq function
      dbAcqState.bEpgDbAcqEnabled = FALSE;

      EpgDbAcqStart(dbc, pQueue, pageNo, appId);
   }
   else
      debug0("EpgDbAcq-Reset: acq not enabled");
}

// ---------------------------------------------------------------------------
// Initialize EPG scan upon start-up or after a channel change
//
void EpgDbAcqInitScan( void )
{
   if (dbAcqState.bEpgDbAcqEnabled)
   {
      EpgStreamSyntaxScanInit();

      // reset result variables
      // XXX not MT-safe!
      memset(pVbiBuf->cnis, 0, sizeof(pVbiBuf->cnis));
      // reset statistics variables
      pVbiBuf->ttxPkgCount = 0;
      pVbiBuf->epgPkgCount = 0;
      pVbiBuf->epgPagCount = 0;
      // enable scan mode
      pVbiBuf->isEpgScan = TRUE;
      pVbiBuf->doVpsPdc  = TRUE;
      // clear the ring buffer
      pVbiBuf->reader_idx = pVbiBuf->writer_idx;
   }
   else
      debug0("EpgDbAcq-InitScan: acq not enabled");
}

// ---------------------------------------------------------------------------
// Reset VPS, PDC and Packet 8/30/1 acquisition
//
void EpgDbAcqResetVpsPdc( void )
{
   if (dbAcqState.bEpgDbAcqEnabled)
   {
      // reset result variables
      // XXX not MT-safe!
      memset(pVbiBuf->cnis, 0, sizeof(pVbiBuf->cnis));

      // enable VPS and PDC acquisition
      pVbiBuf->doVpsPdc = TRUE;
   }
   else
      debug0("EpgDbAcq-ResetVpsPdc: acq not enabled");
}

// ---------------------------------------------------------------------------
// Return CNIs and number of data pages found
// - result values are not invalidized, b/c the caller will change the
//   channel after a value was read anyways
//
void EpgDbAcqGetScanResults( uint *pCni, bool *pNiWait, uint *pDataPageCnt )
{
   CNI_TYPE type;
   uint     cni     = 0;
   bool     niWait  = FALSE;

   cni = 0;
   niWait = FALSE;

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

   if (pCni != NULL)
      *pCni = cni;
   if (pNiWait != NULL)
      *pNiWait = niWait;

   if (pDataPageCnt != NULL)
      *pDataPageCnt = pVbiBuf->dataPageCount;
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
bool EpgDbAcqGetCniAndPil( uint * pCni, uint *pPil )
{
   CNI_TYPE type;
   bool result = FALSE;

   if (pVbiBuf != NULL)
   {
      // search for the best available source
      for (type=0; type < CNI_TYPE_COUNT; type++)
      {
         if (pVbiBuf->cnis[type].haveCni)
         {
            if (pCni != NULL)
               *pCni = pVbiBuf->cnis[type].outCni;

            if (pPil != NULL)
            {
               if (pVbiBuf->cnis[type].havePil)
               {
                  *pPil = pVbiBuf->cnis[type].outPil;
                  pVbiBuf->cnis[type].havePil = FALSE;
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
         pVbiBuf->cnis[type].haveCni = FALSE;
      }
   }
   return result;
}

// ---------------------------------------------------------------------------
// Save CNI and PIL from the VBI process/thread
//
static void EpgDbAcqAddCni( CNI_TYPE type, uint cni, uint pil )
{
   if (pVbiBuf != NULL)
   {
      if (type < CNI_TYPE_COUNT)
      {
         if ((cni != 0) && (cni != 0xffff) && (cni != 0x0fff))
         {
            if ( (pVbiBuf->cnis[type].cniRepCount > 0) &&
                 (pVbiBuf->cnis[type].lastCni != cni) )
            {  // comparison failure -> reset repetition counter
               debug3("EpgDbAcq-AddCni: %s CNI error: last %04X != %04X", ((type == CNI_TYPE_VPS) ? "VPS" : ((type == CNI_TYPE_PDC) ? "PDC" : "NI")), pVbiBuf->cnis[type].lastCni, cni);
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
                  debug11("EpgDbAcq-AddCni: %s PIL error: last %02d.%02d. %02d:%02d (0x%04X) != %02d.%02d. %02d:%02d (0x%04X)", ((type == CNI_TYPE_VPS) ? "VPS" : ((type == CNI_TYPE_PDC) ? "PDC" : "NI")), (pVbiBuf->cnis[type].lastPil >> 15) & 0x1F, (pVbiBuf->cnis[type].lastPil >> 11) & 0x0F, (pVbiBuf->cnis[type].lastPil >> 6) & 0x1F, pVbiBuf->cnis[type].lastPil & 0x3F, pVbiBuf->cnis[type].lastPil, (pil >> 15) & 0x1F, (pil >> 11) & 0x0F, (pil >> 6) & 0x1F, pil & 0x3F, pil);
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
         debug1("EpgDbAcq-AddCni: invalid type %d\n", type);
   }
}

// ---------------------------------------------------------------------------
// Decode an incoming MIP packet
// - note that this runs inside of the ttx sub-thread
// - if an EPG id is found, it is passed to the acq control via shared memory
// - only table cells which refer to pages allowed for Nextview are decoded
//
static void EpgDbAcqMipPacket( uchar magNo, uchar pkgNo, const uchar *data )
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
uint EpgDbAcqGetMipPageNo( void )
{
   return pVbiBuf->mipPageNo;
}

// ---------------------------------------------------------------------------
// Assemble PIL components to nextview format
// - invalid dates or times are replaced by VPS error code ("system code")
//   except for the 2 defined VPS special codes: pause and empty
//
static uint EpgDbAcqAssemblePil( uint mday, uint month, uint hour, uint minute )
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
void EpgDbAcqAddVpsData( const char * data )
{
   uint mday, month, hour, minute;
   uint cni, pil;

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
      pil = EpgDbAcqAssemblePil(mday, month, hour, minute);

      // pass the CNI and PIL into the VPS state machine
      EpgDbAcqAddCni(CNI_TYPE_VPS, cni, pil);
   }
}

// ---------------------------------------------------------------------------
// reverse bit order in one byte
//
static uchar EpgDbAcqReverseBitOrder( uchar b )
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
static void EpgDbAcqGetP830Cni( const char * data )
{
   uchar pdcbuf[10];
   schar dc;
   uint mday, month, hour, minute;
   uint cni, pil;

   if ( UnHam84Nibble(data, &dc) )
   {
      if (dc == 0)
      {  // this is a packet 8/30/1
         cni = ((uint)EpgDbAcqReverseBitOrder(data[7]) << 8) |
                      EpgDbAcqReverseBitOrder(data[8]);
         if ((cni != 0) && (cni != 0xffff))
         {
            cni = CniConvertP8301ToVps(cni);
            EpgDbAcqAddCni(CNI_TYPE_NI, cni, INVALID_VPS_PIL);
         }
      }
      else if (dc == 4)
      {  // this is a packet 8/30/2 (PDC)
         memcpy(pdcbuf, data + 9, 9);
         if (UnHam84Array(pdcbuf, 9))
         {
            cni = (data[0] << 12) | ((data[6] & 3) << 10) | ((data[7] & 0xc) << 6) |
                  ((data[1] & 0xc) << 4) | ((data[7] & 0x3) << 4) | (data[8] & 0xf);
            if ((cni != 0) && (cni != 0xffff))
            {
               mday   = (data[1] >> 2) | ((data[2] & 7) << 3);
               month  = (data[2] >> 3) | ((data[3] & 7) << 1);
               hour   = (data[3] >> 3) | data[4];
               minute = data[5] | (data[6] & 3);

               cni = CniConvertPdcToVps(cni);
               pil = EpgDbAcqAssemblePil(mday, month, hour, minute);
               EpgDbAcqAddCni(CNI_TYPE_PDC, cni, pil);
            }
         }
      }
   }
}

// ---------------------------------------------------------------------------
// Return statistics collected by slave process/thread during acquisition
//
void EpgDbAcqGetStatistics( ulong *pTtxPkgCount, ulong *pEpgPkgCount, ulong *pEpgPagCount )
{
   *pTtxPkgCount = pVbiBuf->ttxPkgCount;
   *pEpgPkgCount = pVbiBuf->epgPkgCount;
   *pEpgPagCount = pVbiBuf->epgPagCount;
}

// ---------------------------------------------------------------------------
// Check the page-header of the current page
// - the acquisition control should immediately stop insertion of acquired
//   blocks, until a new instance of an AI block has been received and
//   its CNI evaluated
// - the page header comparison fails if more than 3 characters are
//   different; we need some tolerance here, sonce the page header
//   is only parity protected
//
static bool EpgDbAcqDoPageHeaderCheck( const uchar * curPageHeader )
{
   uint  i, err;
   schar dec;
   bool  result = TRUE;

   if (dbAcqState.bHeaderCheckInit)
   {
      err = 0;
      for (i=0; i < HEADER_CHECK_LEN; i++)
      {
         dec = (schar)parityTab[curPageHeader[i]];
         if ( (dec >= 0) && (dec != dbAcqState.lastPageHeader[i]) )
         {  // Abweichung gefunden
            err += 1;
         }
      }

      if (err > HEADER_CHECK_MAX_ERRORS)
      {
         debug2("EpgDbAcq-DoPageHeaderCheck: %d diffs from header \"%" HEADER_CHECK_LEN_STR "s\" - stop acq", err, dbAcqState.lastPageHeader);
         result = FALSE;
      }
   }
   else
   {  // erster Aufruf -> Parity dekodieren; falls fehlerfrei abspeichern
      for (i=0; i < HEADER_CHECK_LEN; i++)
      {
         dec = (schar)parityTab[ curPageHeader[i] ];
         if (dec >= 0)
         {
            dbAcqState.lastPageHeader[i] = dec;
         }
         else
            break;
      }

      // erst OK wenn fehlerfrei abgespeichert
      if (i >= HEADER_CHECK_LEN)
      {
         dprintf1("EpgDbAcq-DoPageHeaderCheck: found header \"%" HEADER_CHECK_LEN_STR "s\"\n", dbAcqState.lastPageHeader);
         dbAcqState.bHeaderCheckInit = TRUE;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// The following functions implement a ring buffer for incoming EPG packets
//
//    All icoming EPG packets are stored in a ring buffer to
//    de-couple the time-critical (real-time!) teletext decoder
//    from the rest of the database and user-interface handling.
//    Optimally, the teletext decoder should run in a separate
//    thread or process (connected by shared memory)
//

// ---------------------------------------------------------------------------
// Retrieve all available packets from VBI buffer and convert them to EPG blocks
// - at this point the EPG database process/thread takes the data from the
//   teletext acquisition process
// - returns FALSE when an uncontrolled channel change was detected; the caller
//   then should reset the acquisition
//
bool EpgDbAcqProcessPackets( void )
{
   VBI_LINE *vbl;
   bool channelChange = TRUE;

   if (dbAcqState.bEpgDbAcqEnabled && (dbAcqState.bNewChannel == FALSE))
   {
      assert((pVbiBuf->reader_idx <= EPGACQ_BUF_COUNT) && (pVbiBuf->writer_idx <= EPGACQ_BUF_COUNT));

      while (pVbiBuf->reader_idx != pVbiBuf->writer_idx)
      {
         vbl = &pVbiBuf->line[pVbiBuf->reader_idx];
         //dprintf4("Process idx=%d: pkg=%d pg=%03X.%04X\n", pVbiBuf->reader_idx, vbl->pkgno, vbl->pageno, vbl->sub);
         if (vbl->pkgno == 0)
         {
            // have to check page number again for the case of a page number change, in which
            // the separate teletext thread would finish its current page with the wrong number
            if (vbl->pageno == dbAcqState.epgPageNo)
            {
               if ( EpgDbAcqDoPageHeaderCheck(vbl->data + 13 - 5) == FALSE )
               {
                  dbAcqState.bHeaderCheckInit = FALSE;
                  // abort processing remaining data
                  channelChange = FALSE;
                  break;
               }
               else
                  dbAcqState.isEpgPage = EpgStreamNewPage(vbl->sub);
            }
            else
               dbAcqState.isEpgPage = FALSE;
         }
         else if (dbAcqState.isEpgPage)
         {
            EpgStreamDecodePacket(vbl->pkgno, vbl->data);
         }
         pVbiBuf->reader_idx = (pVbiBuf->reader_idx + 1) % EPGACQ_BUF_COUNT;
      }
   }
   return channelChange;
}

// ---------------------------------------------------------------------------
// Check if there are packets in the buffer
//
bool EpgDbAcqCheckForPackets( bool * pStopped )
{
   bool stopped = FALSE;
   bool result = FALSE;

   if (dbAcqState.bEpgDbAcqEnabled)
   {
      if (dbAcqState.bNewChannel == FALSE)
      {
         result = (pVbiBuf->reader_idx != pVbiBuf->writer_idx);

         if ((result == FALSE) && (pVbiBuf->isEnabled == FALSE))
         {  // vbi slave has stopped acquisition -> inform master control
            debug0("EpgDbAcq-CheckForPackets: slave process has disabled acq");
            stopped = TRUE;
            result = FALSE;
         }
      }
      else
      {  // after channel change: skip the first frame
         if (pVbiBuf->frameSeqNo != 0)
         {  // the slave has started for the new channel
            // discard old data in the buffer
            pVbiBuf->reader_idx = pVbiBuf->start_writer_idx;
            dbAcqState.bNewChannel = FALSE;
         }
         if (pVbiBuf->isEnabled == FALSE)
         {  // must inform master the acq has stopped, or acq will lock up in waiting for channel change completion
            debug0("EpgDbAcq-CheckForPackets: slave process has disabled acq while waiting for new channel");
            stopped = TRUE;
            result = FALSE;
         }
      }
   }

   if (pStopped != NULL)
      *pStopped = stopped;

   return result;
}

// ---------------------------------------------------------------------------
// Append a packet to the VBI buffer
//
static void EpgDbAcqBufferAdd( uint pageNo, uint sub, uchar pkgno, const uchar * data )
{
   static int overflow = 0;

   assert((pVbiBuf->reader_idx <= EPGACQ_BUF_COUNT) && (pVbiBuf->writer_idx <= EPGACQ_BUF_COUNT));

   if (pVbiBuf->reader_idx != ((pVbiBuf->writer_idx + 1) % EPGACQ_BUF_COUNT))
   {
      pVbiBuf->line[pVbiBuf->writer_idx].pageno = pageNo;
      pVbiBuf->line[pVbiBuf->writer_idx].sub    = sub;
      pVbiBuf->line[pVbiBuf->writer_idx].pkgno  = pkgno;

      memcpy(pVbiBuf->line[pVbiBuf->writer_idx].data, data, 40);

      pVbiBuf->writer_idx = (pVbiBuf->writer_idx + 1) % EPGACQ_BUF_COUNT;

      overflow = 0;
   }
   else
   {
      if ((overflow % EPGACQ_BUF_COUNT) == 0)
      {
         debug0("EpgDbAcq-BufferAdd: buffer overflow");
         #ifndef WIN32
         BtDriver_CheckParent();
         #endif
      }
      overflow += 1;
   }
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
void EpgDbAcqNotifyChannelChange( void )
{
   if (dbAcqState.bEpgDbAcqEnabled)
   {
      pVbiBuf->frameSeqNo = 0;  // XXX not MT-safe
      dbAcqState.bNewChannel = TRUE;
      dbAcqState.bHeaderCheckInit = FALSE;

      pVbiBuf->isEpgPage = FALSE;
      pVbiBuf->isMipPage = 0;
      pVbiBuf->mipPageNo = 0;
      pVbiBuf->dataPageCount = 0;

      EpgDbAcqResetVpsPdc();
   }
}

// ---------------------------------------------------------------------------
// Notification of a lost frame
// - executed inside the teletext slave process/thread
// - when a complete frame is lost, it's quite likely that a page header
//   was lost, so we cannot assume that the following packets still belong
//   to the current page
// - reset the ttx page state machine
// - there's no need to reset the EPG decoder (i.e. to throw away the current
//   block) because the next processed packet will be an header, which carries
//   a continuation index (CI) which will detect if an EPG page was lost
//
void EpgDbAcqLostFrame( void )
{
   debug1("EpgDbAcq-LostFrame: lost vbi frame %u", pVbiBuf->frameSeqNo + 1);
   pVbiBuf->isEpgPage = FALSE;
   pVbiBuf->isMipPage = 0;
}

// ---------------------------------------------------------------------------
// Process a received teletext packet, check if it belongs to EPG
// - note that this procedure is called by the teletext decoder process/thread
//   and hence can not access the state variables of the EPG process/thread
//   except for the shared buffer
//
bool EpgDbAcqAddPacket( uint pageNo, uint sub, uchar pkgno, const uchar * data )
{
   bool result = FALSE;

   if ((pVbiBuf != NULL) && pVbiBuf->isEnabled)
   {
      pVbiBuf->ttxPkgCount += 1;

      if (pkgno == 0)
      {  // new page header
         if (pageNo == pVbiBuf->epgPageNo)
         {
            pVbiBuf->isEpgPage = TRUE;
            EpgDbAcqBufferAdd(pageNo, sub, 0, data);
            pVbiBuf->epgPkgCount += 1;
            pVbiBuf->epgPagCount += 1;
            result = TRUE;
         }
         else
         {
            if ( (pageNo >> 8) == (pVbiBuf->epgPageNo >> 8) )
            {  // same magazine, but not the EPG page -> previous EPG page closed
               pVbiBuf->isEpgPage = FALSE;
            }

            if ( (pageNo & 0xFF) == 0xFD )
            {  // magazine inventory page - is decoded immediately by the ttx client
               pVbiBuf->isMipPage |= (1 << (pageNo >> 8));
            }
            else
               pVbiBuf->isMipPage &= ~(1 << (pageNo >> 8));
         }

         if (pVbiBuf->isEpgScan)
            EpgStreamSyntaxScanHeader(pageNo, sub);
      }
      else
      {
         if ( pVbiBuf->isEpgPage &&
                   ((pageNo >> 8) == (pVbiBuf->epgPageNo >> 8)) &&
                   (pkgno < 26) )
         {  // new EPG packet
            EpgDbAcqBufferAdd(0, 0, pkgno, data);
            pVbiBuf->epgPkgCount += 1;
            result = TRUE;
         }
         else if ( pVbiBuf->isMipPage & (1 << (pageNo >> 8)) )
         {  // new MIP packet (may be received in parallel for several magazines)
            EpgDbAcqMipPacket((uchar)(pageNo >> 8), pkgno, data);
         }
         else if (pVbiBuf->doVpsPdc && (pkgno == 30) && ((pageNo >> 8) == 0))
         {  // packet 8/30/1 is evaluated for CNI during EPG scan
            EpgDbAcqGetP830Cni(data);
         }
         // packets from other magazines (e.g. in parallel mode) and 8/30 have to be ignored

         if (pVbiBuf->isEpgScan)
            if (EpgStreamSyntaxScanPacket((uchar)(pageNo >> 8), pkgno, data))
               pVbiBuf->dataPageCount += 1;
      }
   }

   // return TRUE if packet was used by EPG
   return result;
}

