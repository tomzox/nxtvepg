/*
 *  Nextview EPG bit field decoder
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *    Converts Nextview bit fields of all types into C structures
 *    as defined in epgblock.h.  See ETS 300 707 (Nextview Spec.),
 *    chapters 10 to 11 for details.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGDB
#define DPRINTF_OFF

#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"


static time_t unixTimeBase1982;         // 1.1.1982 in UNIX time format
#define JULIAN_DATE_1982  (45000-30)    // 1.1.1982 in Julian date format

// when cross-compiling for WIN32 on Linux "timezone" is undefined
#if !defined(__NetBSD__) && !defined(__FreeBSD__)
# if defined(WIN32) && !defined(timezone)
#  define timezone _timezone
# endif
#endif


// ----------------------------------------------------------------------------
// Allocate a new block and initialize the common elements
//
EPGDB_BLOCK * EpgBlockCreate( BLOCK_TYPE type, uint size, time_t mtime )
{
   EPGDB_BLOCK *pBlock;

   pBlock = (EPGDB_BLOCK *) xmalloc(size + BLK_UNION_OFF);

   // initialize all pointers to NULL (and alignment gaps in header struct)
   memset((char*)pBlock, 0, BLK_UNION_OFF);

   pBlock->type = type;
   pBlock->size = size;

   pBlock->version = 0xff;

   pBlock->acqTimestamp = mtime;

   dprintf2("EpgBlock-Create: created block type=%d, (0x%lx)\n", type, (long)pBlock);
   return pBlock;
}

// ----------------------------------------------------------------------------
// Retrieve the Local Time Offset (LTO) at the given time
// - the LTO at the given time may be different from the current one
//   due to a change in daylight saving time inbetween
// - hence we compute it anew upon every invocation. Since it should only be
//   required for interactive GUI stuff performance is not considered
//
sint EpgLtoGet( time_t when )
{
   struct tm *pTm;
   sint lto;

   #if !defined(__NetBSD__) && !defined(__FreeBSD__)
   pTm = localtime(&when);
   lto = 60*60 * pTm->tm_isdst - timezone;
   #else  // BSD
   pTm = gmtime(&when);
   pTm->tm_isdst = -1;
   lto = when - mktime(pTm);
   #endif

   //printf("LTO = %d min, %s/%s, off=%ld, daylight=%d\n", lto/60, tzname[0], tzname[1], timezone/60, tm->tm_isdst);

   return lto;
}

// ---------------------------------------------------------------------------
// Initialize timer conversion variables
//
void EpgLtoInit( void )
{
   struct tm tm, *pTm;

   // initialize time variables in the standard C library
   tzset();

   // determine UNIX time format of "January 1st 1982, 0:00 am"
   // (required for conversion from Julian date to UNIX epoch)
   tm.tm_mday  = 1;
   tm.tm_mon   = 1 - 1;
   tm.tm_year  = 1982 - 1900;
   tm.tm_sec   = 0;
   tm.tm_min   = 0;
   tm.tm_hour  = 0;
   tm.tm_isdst = FALSE;
   unixTimeBase1982 = mktime(&tm);

   // undo the local->UTC conversion that mktime unwantedly always does
   pTm = gmtime(&unixTimeBase1982);
   pTm->tm_isdst = -1;
   unixTimeBase1982 += (unixTimeBase1982 - mktime(pTm));

#ifdef DEBUG_SWITCH_LTO
   printf("LTO-INIT: base:%ld dst:%d -> dst:%d zone:%ld\n",
          (long)unixTimeBase1982, tm.tm_isdst, pTm->tm_isdst, (long)timezone);
#endif
}

// ---------------------------------------------------------------------------
// Check reloaded PI block for gross consistancy errors
//
static bool EpgBlockCheckPi( EPGDB_BLOCK * pBlock )
{
   bool result = FALSE;
   const PI_BLOCK * pPi;

   pPi = &pBlock->blk.pi;

   if (pPi->netwop_no == INVALID_NETWOP_IDX)
   {
      debug1("EpgBlock-CheckPi: illegal netwop %d", pPi->netwop_no);
   }
   else if (pPi->no_themes > PI_MAX_THEME_COUNT)
   {
      debug1("EpgBlock-CheckPi: illegal theme count %d", pPi->no_themes);
   }
   else if (pPi->off_title != sizeof(PI_BLOCK))
   {
      debug1("EpgBlock-CheckPi: illegal off_title=%d", pPi->off_title);
   }
   else if (pPi->stop_time < pPi->start_time)
   {
      // note: stop == start is allowed for "defective" blocks
      // but stop < start is never possible because the duration is transmitted in Nextview
      debug2("EpgBlock-CheckPi: illegal start/stop times: %ld, %ld", (long)pPi->start_time, (long)pPi->stop_time);
   }
   else if ( PI_HAS_DESC_TEXT(pPi) &&
             ( (pPi->off_desc_text <= pPi->off_title) ||
               (pPi->off_desc_text >= pBlock->size) ||
               // check if the title string is terminated by a null byte
               (*(PI_GET_DESC_TEXT(pPi) - 1) != 0) ))
   {
      debug2("EpgBlock-CheckPi: desc text exceeds block size: off=%d, size=%d", pPi->off_desc_text, pBlock->size + BLK_UNION_OFF);
   }
   else if ( (pPi->no_descriptors > 0) &&
             ( (pPi->off_descriptors <= pPi->off_desc_text) ||
               (pPi->off_descriptors <= pPi->off_title) ||
               (pPi->off_descriptors + (pPi->no_descriptors * sizeof(EPGDB_MERGE_SRC)) != pBlock->size) ||
               // check if the title or description string is terminated by a null byte
               (*(((uchar *)PI_GET_DESCRIPTORS(pPi)) - 1) != 0) ))
   {
      debug3("EpgBlock-CheckPi: descriptor count %d exceeds block length: off=%d, size=%d", pPi->no_descriptors, pPi->off_descriptors, pBlock->size + BLK_UNION_OFF);
   }
   else if ( (pPi->no_descriptors == 0) &&
             (*((uchar *) pBlock + pBlock->size + BLK_UNION_OFF - 1) != 0) )
   {
      debug0("EpgBlock-CheckPi: last string not terminated by 0 byte");
   }
   else
      result = TRUE;

   ifdebug4(result == FALSE, "EpgBlock-CheckPi: inconsistancy detected: network=%d start=%d size=%d acq-time=%d", pPi->netwop_no, (int)pPi->start_time, pBlock->size, (int)pBlock->acqTimestamp);

   return result;
}

// ---------------------------------------------------------------------------
// Check an AI block for consistancy errors in counters, offsets or value ranges
//
static bool EpgBlockCheckAi( EPGDB_BLOCK * pBlock )
{
   const AI_BLOCK  * pAi;
   const AI_NETWOP * pNetwop;
   const char *pBlockEnd, *pName, *pPrevName;
   bool result = FALSE;

   pAi       = &pBlock->blk.ai;
   pBlockEnd = (char *) pBlock + pBlock->size + BLK_UNION_OFF;

   if ((pAi->netwopCount == 0) || (pAi->netwopCount == INVALID_NETWOP_IDX))
   {
      debug1("EpgBlock-CheckAi: illegal netwop count %d", pAi->netwopCount);
   }
   else if ((pAi->off_netwops != sizeof(AI_BLOCK)) ||
            ((pAi->off_netwops & (sizeof(uint32_t) - 1)) != 0))
   {
      debug1("EpgBlock-CheckAi: off_netwops=%d illegal", pAi->off_netwops);
   }
   else if (pAi->off_serviceNameStr != sizeof(AI_BLOCK) + (pAi->netwopCount * sizeof(AI_NETWOP)))
   {
      debug1("EpgBlock-CheckAi: off_serviceNameStr=%d illegal", pAi->off_serviceNameStr);
   }
   else if (pAi->off_serviceNameStr >= pBlock->size)
   {
      // note: this check implies the check for netwop list end > block size
      debug2("EpgBlock-CheckAi: service name off=%d exceeds block length %d", pAi->off_serviceNameStr, pBlock->size);
   }
   else if (pAi->off_sourceInfoStr >= pBlock->size)
   {
      // note: this check implies the check for netwop list end > block size
      debug2("EpgBlock-CheckAi: source info off=%d exceeds block length %d", pAi->off_sourceInfoStr, pBlock->size);
   }
   else if (pAi->off_genInfoStr >= pBlock->size)
   {
      // note: this check implies the check for netwop list end > block size
      debug2("EpgBlock-CheckAi: gen info off=%d exceeds block length %d", pAi->off_genInfoStr, pBlock->size);
   }
   else
   {
      result = TRUE;

      // check the name string offsets the netwop array
      pNetwop   = AI_GET_NETWOPS(pAi);
      pPrevName = AI_GET_SERVICENAME(pAi);
      for (uint netwop=0; netwop < pAi->netwopCount; netwop++, pNetwop++)
      {
         pName = AI_GET_STR_BY_OFF(pAi, pNetwop->off_name);
         if ( (pName <= pPrevName) ||
              (pName >= pBlockEnd) ||
              (*(pName - 1) != 0) )
         {
            debug2("EpgBlock-CheckAi: netwop name #%d has illegal offset %d", netwop, pNetwop->off_name);
            result = FALSE;
            break;
         }
         pPrevName = pName;
      }

      if (result && (*(pBlockEnd - 1) != 0))
      {
        debug0("EpgBlock-CheckAi: last netwop name not 0 terminated");
        result = FALSE;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Check consistancy of an EPG block
// - this check is required when loading a block from a file or through the
//   network, as it might contain errors due to undetected version conflicts or
//   data corruption; the application should not crash due to any such errors
// - checks for errors in counters, offsets or value ranges
//
bool EpgBlockCheckConsistancy( EPGDB_BLOCK * pBlock )
{
   bool result;

   if (pBlock != NULL)
   {
      switch (pBlock->type)
      {
         case BLOCK_TYPE_AI:
            result = EpgBlockCheckAi(pBlock);
            break;
         case BLOCK_TYPE_PI:
            result = EpgBlockCheckPi(pBlock);
            break;
         default:
            debug1("EpgBlock-CheckBlock: illegal block type %d", pBlock->type);
            result = FALSE;
            break;
      }
   }
   else
   {
      debug0("EpgBlock-CheckBlock: illegal NULL ptr arg");
      result = FALSE;
   }

   return result;
}
