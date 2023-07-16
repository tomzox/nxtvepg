/*
 *  Nextview EPG block database interface
 *
 *  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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
 *    Provides access to the Nextview block database. All blocks
 *    are uniquely identified by either their block number alone
 *    or their block and network number. In case of PI you also
 *    can iterate over the database using a GetFirst/GetLast,
 *    GetNext/GetPrev scheme, possibly limited to those PI blocks
 *    matching criteria given in a filter context. For a list of
 *    blocks and their content, see ETS 300 707 (Nextview spec.)
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <time.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"


// internal shortcuts
typedef       EPGDB_CONTEXT *PDBC;
typedef const EPGDB_CONTEXT *CPDBC;

// ---------------------------------------------------------------------------
// Lock database for modification by acquisition
//
void EpgDbLockDatabase( PDBC dbc, uchar enable )
{
   if ( (enable & 1) == FALSE )
   {
      if (dbc->lockLevel > 0)
      {
         dbc->lockLevel -= 1;
      }
      else
         fatal0("EpgDb-LockDatabase: db already unlocked");
   }
   else
   {
      if (dbc->lockLevel < 255)
      {
         dbc->lockLevel += 1;
      }
      else
      {
         fatal0("EpgDb-LockDatabase: max lock level exceeded");
      }
   }
}

// ---------------------------------------------------------------------------
// Query if database is locked
//
bool EpgDbIsLocked( CPDBC dbc )
{
   return (dbc->lockLevel > 0);
}

// ---------------------------------------------------------------------------
// Query the provider CNI of a database context
// - NULL pointer or empty or dummy databases are allowed as argument
//
uint EpgDbContextGetCni( CPDBC dbc )
{
   uint cni;

   if (dbc != NULL)
   {
      cni = dbc->provCni;
   }
   else
      cni = 0;

   return cni;
}

// ---------------------------------------------------------------------------
// Query if the db is merged
//
bool EpgDbContextIsMerged( CPDBC dbc )
{
   if (dbc != NULL)
      return dbc->merged;
   else
      return FALSE;
}

// ---------------------------------------------------------------------------
// Query time of last AI update (by acquisition)
// - in network acq mode, this reflects the time the block was captured by the
//   daemon (the time is arrived at the client is irrelevant)
//
time_t EpgDbGetAiUpdateTime( CPDBC dbc )
{
   time_t  aiUpdateTime;

   if ((dbc != NULL) && (dbc->pAiBlock != NULL))
   {
      aiUpdateTime = dbc->pAiBlock->acqTimestamp;
   }
   else
      aiUpdateTime = 0;

   return aiUpdateTime;
}

// ---------------------------------------------------------------------------
// Set time of last AI update
// - only to be used by network acquisition client, because there AI blocks
//   are not transmitted if unchanged
//
void EpgDbSetAiUpdateTime( const EPGDB_CONTEXT * dbc, time_t acqTimestamp )
{
   if (dbc != NULL)
   {
      if (dbc->pAiBlock != NULL)
      {
         dbc->pAiBlock->acqTimestamp = acqTimestamp;
      }
      else
         debug0("EpgDb-SetAiUpdateTime: no AI block in db");
   }
   else
      fatal0("EpgDb-SetAiUpdateTime: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Install a callback for notifications about incoming PI blocks
// - NULL may be passed as function pointer to remove a previous callback
//
void EpgDbSetPiAcqCallback( EPGDB_CONTEXT * dbc, EPGDB_PI_ACQ_CB * pCb )
{
   if (dbc != NULL)
   {
      dbc->pPiAcqCb = pCb;
   }
   else
      fatal0("EpgDb-SetPiAcqCallback: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Get a pointer to the AI block in the database
//
const AI_BLOCK * EpgDbGetAi( CPDBC dbc )
{
   if ( EpgDbIsLocked(dbc) )
   {
      if (dbc->pAiBlock != NULL)
      {
         return &dbc->pAiBlock->ai;
      }
   }
   else
      fatal0("EpgDb-GetAi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for a PI block in the database
// - if the filter context parameter is not NULL, only blocks that match
//   these settings are considered; see the epgdbfil module on how to
//   create a filter context
//
const PI_BLOCK * EpgDbSearchPi( CPDBC dbc, time_t start_time, uint netwop_no )
{
   EPGDB_PI_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      assert(netwop_no < dbc->netwopCount);

      pBlock = dbc->pFirstNetwopPi[netwop_no];

      while (pBlock != NULL)
      {
         if (pBlock->pi.start_time == start_time)
         {
            assert(pBlock->pi.netwop_no == netwop_no);

            return &pBlock->pi;
         }
         pBlock = pBlock->pNextNetwopBlock;
      }
   }
   else
      fatal0("EpgDb-SearchPi: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for a PI block in the database with a given minimum start time
// - if the filter context parameter is not NULL, only blocks that match
//   these settings are considered
//
const PI_BLOCK * EpgDbSearchFirstPiAfter( CPDBC dbc, time_t min_time, EPGDB_TIME_SEARCH_MODE startOrStop,
                                          const FILTER_CONTEXT *fc )
{
   EPGDB_PI_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pFirstPi;

      if (startOrStop == STARTING_AT)
      {
         while ( (pBlock != NULL) && (pBlock->pi.start_time < min_time))
         {
            pBlock = pBlock->pNextBlock;
         }

         if (fc != NULL)
         {  // skip forward until a matching block is found
            while ( (pBlock != NULL) &&
                    (EpgDbFilterMatches(dbc, fc, &pBlock->pi) == FALSE) )
            {
               pBlock = pBlock->pNextBlock;
            }
         }
      }
      else if (startOrStop == RUNNING_AT)
      {
         while ((pBlock != NULL) && (pBlock->pi.stop_time <= min_time))
         {
            pBlock = pBlock->pNextBlock;
         }

         if (fc != NULL)
         {  // skip forward until a matching block is found
            // note: must keep on checking stop time (unlike start time search PI may follow on
            //       different networks which have an earlier stop time due to shorter running time)
            while ( (pBlock != NULL) &&
                    ( (pBlock->pi.stop_time <= min_time) ||
                      (EpgDbFilterMatches(dbc, fc, &pBlock->pi) == FALSE) ))
            {
               pBlock = pBlock->pNextBlock;
            }
         }
      }
      else
         fatal1("EpgDb-SearchFirstPiAfter: invalid time search mode %d", startOrStop);


      if (pBlock != NULL)
      {
         return &pBlock->pi;
      }
   }
   else
      fatal0("EpgDb-SearchFirstPiAfter: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for a PI block in the database with a given maximum start time
// - if the filter context parameter is not NULL, only blocks that match
//   these settings are considered
//
const PI_BLOCK * EpgDbSearchFirstPiBefore( CPDBC dbc, time_t min_time, EPGDB_TIME_SEARCH_MODE startOrStop,
                                           const FILTER_CONTEXT *fc )
{
   EPGDB_PI_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pLastPi;

      if (startOrStop == STARTING_AT)
      {
         while ((pBlock != NULL) && (pBlock->pi.start_time >= min_time))
         {
            pBlock = pBlock->pPrevBlock;
         }
      }
      else if (startOrStop == RUNNING_AT)
      {
         while ((pBlock != NULL) && (pBlock->pi.stop_time > min_time))
         {
            pBlock = pBlock->pPrevBlock;
         }
      }
      else
         fatal1("EpgDb-SearchFirstPiBefore: invalid time search mode %d", startOrStop);

      if (fc != NULL)
      {  // skip forward until a matching block is found
         while ( (pBlock != NULL) &&
                 (EpgDbFilterMatches(dbc, fc, &pBlock->pi) == FALSE) )
         {
            pBlock = pBlock->pPrevBlock;
         }
      }

      if (pBlock != NULL)
      {
         return &pBlock->pi;
      }
   }
   else
      fatal0("EpgDb-SearchFirstPiBefore: DB not locked");

   return NULL;
}

// ---------------------------------------------------------------------------
// Search for the first matching PI block in database
// - The first is the the block with the lowest start time.
//   The start time may lie in the past if the stop time is
//   still in the future
//
const PI_BLOCK * EpgDbSearchFirstPi( CPDBC dbc, const FILTER_CONTEXT *fc )
{
   const EPGDB_PI_BLOCK * pBlock = NULL;

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pFirstPi;

      if (fc != NULL)
      {  // skip forward until a matching block is found
         while ( (pBlock != NULL) &&
                 (EpgDbFilterMatches(dbc, fc, &pBlock->pi) == FALSE) )
         {
            pBlock = pBlock->pNextBlock;
         }
      }
   }
   else
      fatal0("EpgDb-SearchFirstPi: DB not locked");

   if (pBlock != NULL)
      return &pBlock->pi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Search for the last matching PI block in database
//
const PI_BLOCK * EpgDbSearchLastPi( CPDBC dbc, const FILTER_CONTEXT *fc )
{
   const EPGDB_PI_BLOCK * pBlock = NULL;

   if ( EpgDbIsLocked(dbc) )
   {
      pBlock = dbc->pLastPi;

      if (fc != NULL)
      {
         while ( (pBlock != NULL) &&
                 (EpgDbFilterMatches(dbc, fc, &pBlock->pi) == FALSE) )
         {
            pBlock = pBlock->pPrevBlock;
         }
      }
   }
   else
      fatal0("EpgDb-SearchLastPi: DB not locked");

   if (pBlock != NULL)
      return &pBlock->pi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Search for the next matching PI block after a given block
// - blocks are sorted by starting time, then netwop_no, i.e. this
//   function returns the first block which matches the filter criteria
//   and has a higher start time (or the same start time and a higher
//   netwop index) as the given block.
//
const PI_BLOCK * EpgDbSearchNextPi( CPDBC dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock )
{
   const EPGDB_PI_BLOCK * pBlock = NULL;

   if ( EpgDbIsLocked(dbc) )
   {
      if (pPiBlock != NULL)
      {
         pBlock = (const EPGDB_PI_BLOCK *)((ulong)pPiBlock - offsetof(EPGDB_PI_BLOCK, pi));
         pBlock = pBlock->pNextBlock;

         if (fc != NULL)
         {  // skip forward until a matching block is found
            while ( (pBlock != NULL) &&
                    (EpgDbFilterMatches(dbc, fc, &pBlock->pi) == FALSE) )
            {
               pBlock = pBlock->pNextBlock;
            }
         }
      }
      else
         fatal0("EpgDb-SearchNextPi: illegal NULL ptr param");
   }
   else
      fatal0("EpgDb-SearchNextPi: DB not locked");

   if (pBlock != NULL)
      return &pBlock->pi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Search for the next matching PI block before a given block
// - same as SearchNext(), just reverse
//
const PI_BLOCK * EpgDbSearchPrevPi( CPDBC dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock )
{
   const EPGDB_PI_BLOCK * pBlock = NULL;

   if ( EpgDbIsLocked(dbc) )
   {
      if (pPiBlock != NULL)
      {
         pBlock = (const EPGDB_PI_BLOCK *)((ulong)pPiBlock - offsetof(EPGDB_PI_BLOCK, pi));
         pBlock = pBlock->pPrevBlock;

         if (fc != NULL)
         {
            while ( (pBlock != NULL) &&
                    (EpgDbFilterMatches(dbc, fc, &pBlock->pi) == FALSE) )
            {
               pBlock = pBlock->pPrevBlock;
            }
         }
      }
      else
         fatal0("EpgDb-SearchPrevPi: illegal NULL ptr param");
   }
   else
      fatal0("EpgDb-SearchPrevPi: DB not locked");

   if (pBlock != NULL)
      return &pBlock->pi;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Count matching PI blocks starting with a given block
// - note: the returned count includes the given block (if it matches)
//
uint EpgDbCountPi( CPDBC dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock )
{
   const EPGDB_PI_BLOCK * pBlock = NULL;
   uint  count = 0;

   if ( EpgDbIsLocked(dbc) )
   {
      if (pPiBlock != NULL)
      {
         pBlock = (const EPGDB_PI_BLOCK *)((ulong)pPiBlock - offsetof(EPGDB_PI_BLOCK, pi));

         while (pBlock != NULL)
         {
            if ( (fc == NULL) ||
                 EpgDbFilterMatches(dbc, fc, &pBlock->pi) )
            {
               count += 1;
            }
            pBlock = pBlock->pNextBlock;
         }
      }
      else
         fatal0("EpgDb-CountPi: illegal NULL ptr param");
   }
   else
      fatal0("EpgDb-CountPi: DB not locked");

   return count;
}

// ---------------------------------------------------------------------------
// Count matching PI blocks before a given block
// - note: the returned count does not include the given block
//
uint EpgDbCountPrevPi( CPDBC dbc, const FILTER_CONTEXT *fc, const PI_BLOCK * pPiBlock )
{
   const EPGDB_PI_BLOCK * pBlock = NULL;
   uint  count = 0;

   if ( EpgDbIsLocked(dbc) )
   {
      if (pPiBlock != NULL)
      {
         pBlock = (const EPGDB_PI_BLOCK *)((ulong)pPiBlock - offsetof(EPGDB_PI_BLOCK, pi));
         pBlock = pBlock->pPrevBlock;

         while (pBlock != NULL)
         {
            if ( (fc == NULL) ||
                 EpgDbFilterMatches(dbc, fc, &pBlock->pi) )
            {
               count += 1;
            }
            pBlock = pBlock->pPrevBlock;
         }
      }
      else
         fatal0("EpgDb-CountPrevPi: illegal NULL ptr param");
   }
   else
      fatal0("EpgDb-CountPrevPi: DB not locked");

   return count;
}

// ---------------------------------------------------------------------------
// Search for the PI block with the given PIL
// - also search through the obsolete blocks, because the planned program
//   stop time might already have elapsed when the program is still running
//
const PI_BLOCK * EpgDbSearchPiByPil( CPDBC dbc, uint netwop_no, uint pil )
{
   const EPGDB_PI_BLOCK * pBlock;

   if ( EpgDbIsLocked(dbc) )
   {
      assert(netwop_no < dbc->netwopCount);

      pBlock = dbc->pFirstNetwopPi[netwop_no];
      while (pBlock != NULL)
      {
         if (pBlock->pi.pil == pil)
         {
            assert(pBlock->pi.netwop_no == netwop_no);
            return &pBlock->pi;
         }
         pBlock = pBlock->pNextNetwopBlock;
      }
   }
   else
      fatal0("EpgDb-SearchPiByPil: DB not locked");

   return NULL;
}

// ----------------------------------------------------------------------------
// Decode VPS/PDC timestamp from Nextview EPG format into standard struct
// - note: the VPS label represents local time (not UTC!)
// - returns TRUE is the label did contain a valid time
//
bool EpgDbGetVpsTimestamp( struct tm * pVpsTime, uint pil, time_t startTime )
{
   bool result;

   memcpy(pVpsTime, localtime(&startTime), sizeof(*pVpsTime));

   pVpsTime->tm_sec  = 0;
   pVpsTime->tm_min  =  (pil      ) & 0x3F;
   pVpsTime->tm_hour =  (pil >>  6) & 0x1F;
   pVpsTime->tm_mon  = ((pil >> 11) & 0x0F) - 1; // range 0-11
   pVpsTime->tm_mday =  (pil >> 15) & 0x1F;
   // the rest of the elements (year, day-of-week etc.) stay the same as in
   // start_time; since a VPS label usually has the same date as the actual
   // start time this should work out well.

   result = ( (pVpsTime->tm_min < 60) && (pVpsTime->tm_hour < 24) &&
              (pVpsTime->tm_mon >= 0) && (pVpsTime->tm_mon < 12) &&
              (pVpsTime->tm_mday >= 1) && (pVpsTime->tm_mday <= 31) );

   return result;
}

// ---------------------------------------------------------------------------
// Get the index of a PI in sort order of start time per network.
// - The index of a netwop's currently running PI is 0. Index is 1 for the
//   upcoming programme afterward. Index is increased for each subsequent
//   programme of the same network, regarless of possible gaps between
//   start/stop times.
// - Result is 0xFFFF if the PI is already expired, or its index is beyond
//   the given limit. Note expire time threshold is not considered.
// - Index calculation is cut off at the given limit to achieve acceptable
//   performance. Note this function is called by the filter function for each
//   PI. Iterating across the entire DB for each PI would be very slow.
//
uint EpgDbGetProgIdx( CPDBC dbc, const PI_BLOCK * pPiBlock, uint maxIndex )
{
   const EPGDB_PI_BLOCK * pBlock;
   const EPGDB_PI_BLOCK * pWalk;
   uint  nowIdx;
   time_t now = time(NULL);
   uint  result = 0xffff;

   if (pPiBlock->stop_time > now)
   {
      pBlock = (const EPGDB_PI_BLOCK *)((ulong)pPiBlock - offsetof(EPGDB_PI_BLOCK, pi));
      pWalk = pBlock;
      nowIdx = 0;

      // scan backward for the first block on that network not starting in the future
      while ((pWalk != NULL) && (pWalk->pi.start_time > now))
      {
         pWalk = pWalk->pPrevNetwopBlock;
         nowIdx += 1;

         if (nowIdx > maxIndex)
            goto abort;
      }

      if ((pWalk == NULL) || (pWalk->pi.stop_time < now))
      {  // 1. there is no current programme on this netwop OR
         // 2. there is a block missing in the database
         // We could try to distinguish these two cases, but there's
         // really no advantage because we couldn't find a better guess
         // than giving the first present block #1, i.e. NEXT
         result = nowIdx + 1;
      }
      else
         result = nowIdx;
   }

abort:
   return result;
}

// ---------------------------------------------------------------------------
// Returns the timestamp of last update for a PI block
//
time_t EpgDbGetPiUpdateTime( const PI_BLOCK * pPiBlock )
{
   const EPGDB_PI_BLOCK *pBlock;

   if (pPiBlock != NULL)
   {
      pBlock = (const EPGDB_PI_BLOCK *)((ulong)pPiBlock - offsetof(EPGDB_PI_BLOCK, pi));

      return pBlock->acqTimestamp;
   }
   else
      fatal0("EpgDb-GetPiUpdateTime: illegal NULL ptr param");

   return (time_t) 0;
}

// ---------------------------------------------------------------------------
// Count the number of PI blocks in the database
// - db needs not be locked since this is an atomic operation
//   and no pointers into the db are returned
//
bool EpgDbGetStat( CPDBC dbc, EPGDB_BLOCK_COUNT * pCount, time_t now, time_t tsAcq )
{
   const EPGDB_PI_BLOCK * pBlock;
   const time_t tsDay1 = tsAcq + 24*60*60;
   const time_t tsDay2 = tsDay1 + 24*60*60;
   bool   result;

   memset(pCount, 0, sizeof(EPGDB_BLOCK_COUNT));

   if (dbc->pAiBlock != NULL)
   {
      pBlock = dbc->pFirstPi;
      while (pBlock != NULL)
      {
         if (pBlock->pi.stop_time > now)
         {
            uint idx = ((pBlock->acqTimestamp >= tsAcq) ? 0 : 1);

            if (pBlock->pi.start_time < tsDay1)
               pCount->day1[idx] += 1;
            else if (pBlock->pi.start_time < tsDay2)
               pCount->day2[idx] += 1;
            else
               pCount->day3[idx] += 1;
         }
         else
         {
            if (pBlock->pi.stop_time >= pBlock->acqTimestamp)
               pCount->expiredSinceAcq += 1;
            pCount->expired += 1;
         }
         pBlock = pBlock->pNextBlock;
      }

      // count number of defective blocks
      pBlock = dbc->pObsoletePi;
      while (pBlock != NULL)
      {
         pCount->defective += 1;
         pBlock = pBlock->pNextBlock;
      }

      result = TRUE;
   }
   else
   {  // empty database -> return 0 for all counts
      dprintf0("EpgDb-GetStat: no AI in db\n");
      result = FALSE;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Returns the string representation of a theme handle
//
const char * EpgDbGetThemeStr( const EPGDB_CONTEXT * dbc, uint themeIdx )
{
   if (themeIdx < dbc->themeCount)
   {
      assert(dbc->pThemes != NULL);  // implied by themeCount > 0

      return dbc->pThemes[themeIdx];
   }
   else
   {
      debug2("EpgDb-GetThemeStr: invalid idx:%d >= count:%d", themeIdx, dbc->themeCount);
      return "unknown";
   }
}
