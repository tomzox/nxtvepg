/*
 *  Nextview database context management
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
 *    The module "owns" all database contexts. All create, reload, peek, close
 *    and scan (list) requests have to be made through here. The purpose is to
 *    open a provider's database only once and to maintain a reference count.
 *    A database can be opened more than once on the following circumstances:
 *    - db is opened for both the browser and acquisition
 *    - acquisition for a database that's part of a merged db:
 *      then the db is opened once by the epgacqctl module and
 *      once by the epgdbmerge module
 *
 *    In addition to databases the module manages a cache of "database peeks",
 *    which contain the general provider info (i.e. tuner frequency) plus the
 *    AI and OI#0 blocks. The peeks are used e.g. by the GUI when a list of
 *    provider names is to be displayed.  Hence when a database is closed,
 *    it's not entirely freed, but instead stripped down to a peek.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgctxctl.c,v 1.25 2004/03/21 17:39:46 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <time.h>
#include <string.h>
#include <errno.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbif.h"
#include "epgui/uictrl.h"
#include "epgui/epgmain.h"
#include "epgctl/epgctxmerge.h"
#include "epgctl/epgctxctl.h"


// ---------------------------------------------------------------------------
// declaration of local types

typedef enum
{
   CTX_CACHE_ERROR,                    // failed to open the database
   CTX_CACHE_DUMMY,                    // empty context, e.g. used during scan
   CTX_CACHE_STAT,                     // only existance of a db file is known about
   CTX_CACHE_PEEK,                     // only AI & OI are in memory
   CTX_CACHE_OPEN,                     // db is fully loaded
#define CTX_CACHE_STATE_COUNT (CTX_CACHE_OPEN + 1)
} CTX_CACHE_STATE;

typedef struct CTX_CACHE_struct
{
   struct CTX_CACHE_struct *pNext;

   CTX_CACHE_STATE      state;         // state of this cache entry
   uint                 openRefCount;  // reference counter for open requests
   uint                 peekRefCount;  // reference counter for peek requests
   uint                 provCni;       // provider CNI (same as this_netwop in AI)
   EPGDB_RELOAD_RESULT  reloadErr;     // reload error code (used in error state only)
   time_t               mtime;         // db file modification time (used in state STAT only)

   EPGDB_CONTEXT      * pDbContext;    // the actual EPG database

} CTX_CACHE;


// ---------------------------------------------------------------------------
// local variables

// anchor of the list of all db contexts
static CTX_CACHE * pContextCache = NULL;

// empty context, used when no provider is available
static CTX_CACHE * pContextDummy = NULL;

static bool contextLockDump = FALSE;


#if DEBUG_SWITCH == ON
// ---------------------------------------------------------------------------
// Return state description
//
static const char * CtxCacheStateStr( CTX_CACHE_STATE state )
{
   switch (state)
   {
      case CTX_CACHE_ERROR:      return "ERROR"; break;
      case CTX_CACHE_DUMMY:      return "DUMMY"; break;
      case CTX_CACHE_STAT:       return "STAT"; break;
      case CTX_CACHE_PEEK:       return "PEEK"; break;
      case CTX_CACHE_OPEN:       return "OPEN"; break;
      default:                   return "UNKNOWN"; break;
   }
}

// ---------------------------------------------------------------------------
// Check consistancy of all cached contexts and the cache in general
//
static bool EpgContextCtl_CheckConsistancy( void )
{
   CTX_CACHE * pContext;
   CTX_CACHE * pWalk;

   pContext = pContextCache;
   while (pContext != NULL)
   {
      if ((pContext->state == CTX_CACHE_STAT) || (pContext->state == CTX_CACHE_ERROR))
      {
         if ((pContext->openRefCount > 0) || (pContext->peekRefCount > 0))
            fatal5("Context-Check: illegal openRefCount=%d or peekRefCount=%d in context 0x%lx, state %s, CNI 0x%04X", pContext->openRefCount, pContext->peekRefCount, (long)pContext, CtxCacheStateStr(pContext->state), pContext->provCni);
         if (pContext->pDbContext != NULL)
            fatal3("Context-Check: context 0x%lx has DB, state %s, CNI 0x%04X", (long)pContext, CtxCacheStateStr(pContext->state), pContext->provCni);
         if ((pContext->state == CTX_CACHE_ERROR) && (pContext->reloadErr == EPGDB_RELOAD_OK))
            fatal3("Context-Check: illegal error code %d d in context 0x%lx, CNI 0x%04X", pContext->reloadErr, (long)pContext, pContext->provCni);
         if ((pContext->provCni == 0) || (pContext->provCni == 0x00ff))
            fatal3("Context-Check: context 0x%lx has illegal CNI 0x%04x, state %s", (long)pContext, pContext->provCni, CtxCacheStateStr(pContext->state));
      }
      else if ((pContext->state == CTX_CACHE_OPEN) || (pContext->state == CTX_CACHE_PEEK))
      {
         if ((pContext->state == CTX_CACHE_PEEK) && (pContext->openRefCount > 0))
            fatal3("Context-Check: illegal openRefCount=%d in PEEK context 0x%lx, CNI 0x%04X", pContext->openRefCount, (long)pContext, pContext->provCni);
         if (pContext->pDbContext == NULL)
            fatal3("Context-Check: context 0x%lx has no DB, state %s, CNI 0x%04X", (long)pContext, CtxCacheStateStr(pContext->state), pContext->provCni);
         if ((pContext->pDbContext != NULL) &&
             ((pContext->provCni == 0) || (pContext->provCni == 0x00ff)))
            fatal3("Context-Check: context 0x%lx has illegal CNI 0x%04x, state %s", (long)pContext, pContext->provCni, CtxCacheStateStr(pContext->state));
         if ((pContext->pDbContext != NULL) && (pContext->pDbContext->pAiBlock != NULL) &&
             (AI_GET_CNI(&pContext->pDbContext->pAiBlock->blk.ai) != pContext->provCni))
            fatal4("Context-Check: context 0x%lx: prov CNI 0x%04x != AI CNI 0x%04X (state %s)", (long)pContext, pContext->provCni, AI_GET_CNI(&pContext->pDbContext->pAiBlock->blk.ai), CtxCacheStateStr(pContext->state));
         if ((pContext->state == CTX_CACHE_PEEK) && (pContext->pDbContext != NULL) &&
             (pContext->pDbContext->pFirstPi != NULL))
            fatal2("Context-Check: PEEK context 0x%04x (0x%lx) has PI", pContext->provCni, (long)pContext);
      }
      else
      {
         fatal4("Context-Check: illegal state %s (%d) in context cache 0x%lx, CNI 0x%04X", CtxCacheStateStr(pContext->state), pContext->state, (long)pContext, pContext->provCni);
      }

      // search through all previous contexts for an identical CNI
      pWalk = pContextCache;
      while ((pWalk != NULL) && (pWalk != pContext))
      {
         if (pWalk->provCni == pContext->provCni)
            fatal3("Context-Check: found CNI 0x%04X in 2 contexts: 0x%lx, 0x%lx", pWalk->provCni, (long)pContext, (long)pWalk);
         pWalk = pWalk->pNext;
      }

      pContext = pContext->pNext;
   }

   // check dummy context
   if (pContextDummy->state != CTX_CACHE_DUMMY)
      fatal4("Context-Check: illegal state %d (%s) in dummy context 0x%lx, CNI 0x%04X", pContextDummy->state, CtxCacheStateStr(pContextDummy->state), (long)pContextDummy, pContextDummy->provCni);
   if (pContextDummy->provCni != 0)
      fatal4("Context-Check: illegal CNI 0x%04X in dummy context 0x%lx, state %d (%s)", pContextDummy->provCni, (long)pContextDummy, pContextDummy->state, CtxCacheStateStr(pContextDummy->state));
   if (pContextDummy->pNext != NULL)
      fatal4("Context-Check: dummy context 0x%lx next ptr != 0: 0x%lx state %d (%s)", (long)pContextDummy, (long) pContextDummy->pNext, pContextDummy->state, CtxCacheStateStr(pContextDummy->state));
   if (pContextDummy->pDbContext->modified)
      fatal1("Context-Check: dummy context (0x%lx) was modified", (long)pContextDummy);
   if (pContextDummy->pDbContext->pAiBlock != NULL)
      fatal2("Context-Check: dummy context (0x%lx) has AI block 0x%04X", (long)pContextDummy, AI_GET_CNI(&pContextDummy->pDbContext->pAiBlock->blk.ai));

   // dummy result to allow call from inside assert()
   return TRUE;
}
#endif  // DEBUG_SWITCH == ON

// ---------------------------------------------------------------------------
// Create an empty database context
//
static CTX_CACHE * EpgContextCtl_CreateNew( void )
{
   CTX_CACHE * pContext;

   pContext = xmalloc(sizeof(CTX_CACHE));
   memset(pContext, 0, sizeof(*pContext));

   pContext->state      = CTX_CACHE_DUMMY;
   pContext->pDbContext = EpgDbCreate();

   dprintf1("EpgContextCtl-CreateNew: created empty db (%lx)\n", (long)pContext);
   return pContext;
}

// ---------------------------------------------------------------------------
// Add a stat entry to the cache
//
static CTX_CACHE * EpgContextCtl_AddStat( uint cni, time_t mtime )
{
   CTX_CACHE * pContext;

   pContext = xmalloc(sizeof(CTX_CACHE));
   memset(pContext, 0, sizeof(*pContext));
   dprintf3("EpgContextCtl-AddStat: adding context 0x%04X (0x%lx), mtime %ld\n", cni, (long)pContext, mtime);

   // state STAT will be modified to either ERROR or PEEK below
   pContext->state         = CTX_CACHE_STAT;
   pContext->mtime         = mtime;
   pContext->provCni       = cni;
   pContext->openRefCount  = 0;
   pContext->peekRefCount  = 0;

   // link into the cache chain
   pContext->pNext         = pContextCache;
   pContextCache           = pContext;

   return pContext;
}

// ---------------------------------------------------------------------------
// Search for the given CNI in the list of opened context
//
static CTX_CACHE * EpgContextCtl_SearchCni( uint cni )
{
   CTX_CACHE * pContext;

   pContext = pContextCache;
   while (pContext != NULL)
   {
      if (pContext->provCni == cni)
      {
         break;
      }
      pContext = pContext->pNext;
   }
   return pContext;
}

// ---------------------------------------------------------------------------
// Search for the "newest" provider amongst unopened ones
//
static CTX_CACHE * EpgContextCtl_SearchNewest( void )
{
   CTX_CACHE  * pContext;
   CTX_CACHE  * pWalk;

   pContext = NULL;

   pWalk = pContextCache;
   while (pWalk != NULL)
   {
      if ( (pWalk->state == CTX_CACHE_STAT) ||
           (pWalk->state == CTX_CACHE_PEEK) )
      {
         if ((pContext == NULL) || (pWalk->mtime > pContext->mtime))
         {
            pContext = pWalk;
         }
      }
      pWalk = pWalk->pNext;
   }

   return pContext;
}

// ---------------------------------------------------------------------------
// Load a database, i.e. upgrade from stat or peek state to open
//
static bool EpgContextCtl_Load( CTX_CACHE * pContext )
{
   EPGDB_CONTEXT  * pDbContext;
   bool result;

   pDbContext = EpgDbReload(pContext->provCni, &pContext->reloadErr);
   if (pDbContext != NULL)
   {
      // free peek database for this database, if it exists
      if (pContext->pDbContext != NULL)
      {
         assert(pContext->pDbContext->lockLevel == 0);
         if (pContext->peekRefCount == 0)
         {  // no one is using the peek context currently -> discard it
            EpgDbDestroy(pContext->pDbContext, FALSE);
            // install the new & complete context in its place
            pContext->pDbContext = pDbContext;
         }
         else
         {  // db context pointer is still in use -> pointer must not change
            // remove all blocks from the old context
            // XXX should be done in epgdbmgmt.c
            if (pContext->pDbContext->pAiBlock != NULL)
               xfree(pContext->pDbContext->pAiBlock);
            if (pContext->pDbContext->pFirstGenericBlock[BLOCK_TYPE_OI] != NULL)
               xfree(pContext->pDbContext->pFirstGenericBlock[BLOCK_TYPE_OI]);
            // overwrite the old context with the new data
            memcpy(pContext->pDbContext, pDbContext, sizeof(EPGDB_CONTEXT));
            // free the now obsolete new context structure
            xfree(pDbContext);
            pDbContext = pContext->pDbContext;  // unused
         }
      }
      else
         pContext->pDbContext = pDbContext;

      pContext->state          = CTX_CACHE_OPEN;
      pContext->openRefCount  += 1;
      result = TRUE;
   }
   else
   {  // upgrade failed -> convert context to error state

      if (pContext->peekRefCount == 0)
      {
         if (pContext->pDbContext != NULL)
         {
            EpgDbDestroy(pContext->pDbContext, FALSE);
            pContext->pDbContext = NULL;
         }
         pContext->state = CTX_CACHE_ERROR;
      }
      else
      {  // peek is in use, but complete load failed (e.g. file is truncated)
         // XXX cannot destroy the db
      }
      result = FALSE;
   }

   return result;
}

// ---------------------------------------------------------------------------
// Peek into a database
// - returns a database context with at least an AI block in it plus an OI
//   if available; may be a complete database if it's is already loaded
// - if no info is available for the CNI we attempt to peek into the db anyways
//   (1) useful to remember error state and suppress subsequent error messages
//   (2) useful in case the dbdir is shared between client & server
// - returns NULL if the reload fails and sends an error indication to the GUI
//   the parameter failMsgMode is passed to GUI error handler
//
EPGDB_CONTEXT * EpgContextCtl_Peek( uint cni, int failMsgMode )
{
   CTX_CACHE      * pContext;
   EPGDB_CONTEXT  * pDbContext;
   EPGDB_RELOAD_RESULT dberr;
   bool isNew;

   if ((cni != 0) && (cni != 0x00ff))
   {
      dberr = EPGDB_RELOAD_OK;
      pContext = EpgContextCtl_SearchCni(cni);

      if (pContext == NULL)
      {  // not found in the cache -> create stat entry (see also comment above)
         pContext = EpgContextCtl_AddStat(cni, 0);
         isNew = TRUE;
      }
      else
         isNew = FALSE;

      if ( (pContext->state == CTX_CACHE_OPEN) ||
           (pContext->state == CTX_CACHE_PEEK) )
      {  // database or peek in cache
         dprintf4("EpgContextCtl-Peek: prov %04X found in cache, state %s, peek/open refCount %d/%d\n", cni, CtxCacheStateStr(pContext->state), pContext->peekRefCount, pContext->openRefCount);
         pContext->peekRefCount += 1;
      }
      else if (pContext->state == CTX_CACHE_STAT)
      {  // database known, but not yet loaded -> load AI & OI only
         pDbContext = EpgDbPeek(cni, &dberr);
         if (pDbContext != NULL)
         {
            dprintf1("EpgContextCtl-Peek: prov %04X upgrade STAT to PEEK\n", cni);
            pContext->state         = CTX_CACHE_PEEK;
            pContext->pDbContext    = pDbContext;
            pContext->peekRefCount  = 1;
         }
         else
         {  // failed to peek into the db file -> convert to error context
            assert(dberr != EPGDB_RELOAD_OK);
            pContext->state         = CTX_CACHE_ERROR;
            pContext->reloadErr     = dberr;

            // send a messaage to the user interface
            UiControlMsg_ReloadError(cni, dberr, failMsgMode, isNew);
            pContext = NULL;
         }
      }
      else if (pContext->state == CTX_CACHE_ERROR)
      {
         UiControlMsg_ReloadError(cni, pContext->reloadErr, failMsgMode, FALSE);
         pContext = NULL;
      }
      else
      {
         fatal2("EpgContextCtl_Peek: ctx 0x%04X has illegal state %s", pContext->provCni, CtxCacheStateStr(pContext->state));
         pContext = NULL;
      }
   }
   else
   {
      fatal1("EpgContextCtl-Peek: illegal CNI 0x%04X", cni);
      pContext = NULL;
   }

   assert(EpgContextCtl_CheckConsistancy());

   if (pContext != NULL)
      return pContext->pDbContext;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Load any provider: use an already open one or load the latest dumped one
// - for clients which don't care about the CNI
// - searches for the latest updated one, to avoid loading an empty db
//
EPGDB_CONTEXT * EpgContextCtl_OpenAny( int failMsgMode )
{
   CTX_CACHE  * pContext;
   CTX_CACHE  * pWalk;
   EPGDB_RELOAD_RESULT dberr;
   time_t       maxAiTimestamp;
   uint errCni;

   dberr  = EPGDB_RELOAD_OK;
   errCni = 0;

   // search for the newest among the open databases
   pContext = NULL;
   pWalk    = pContextCache;
   maxAiTimestamp = 0;
   while (pWalk != NULL)
   {
      if ( (pWalk->state == CTX_CACHE_OPEN) &&
           (pWalk->pDbContext != NULL) && (pWalk->pDbContext->pAiBlock != NULL) )
      {
         if ( (pContext == NULL) ||
              (pWalk->pDbContext->pAiBlock->acqTimestamp > maxAiTimestamp) )
         {
            maxAiTimestamp = pWalk->pDbContext->pAiBlock->acqTimestamp;
            pContext = pWalk;
         }
      }
      pWalk = pWalk->pNext;
   }

   if (pContext != NULL)
   {  // found an open one
      pContext->openRefCount += 1;
   }
   else
   {  // no db open yet -> search and reload the "newest"

      // loop until a database could be successfully loaded
      // (note: the loop is guaranteed to terminate because in each pass one db is marked erronous)
      while ( (pContext = EpgContextCtl_SearchNewest()) != NULL )
      {
         // found a database which is not open yet
         if (EpgContextCtl_Load(pContext))
         {
            // a database has been loaded -> terminate the loop
            break;
         }
         else
         {  // reload failed -> downgrade provider into an error state, try next one
            if (RELOAD_ERR_WORSE(pContext->reloadErr, dberr))
            {  // worst error so far -> save it to report it to the user later
               dberr  = pContext->reloadErr;
               errCni = pContext->provCni;
            }
         }
      }

      if (pContext == NULL)
      {  // no db found (or all failed to load) -> return dummy
         dprintf1("EpgContextCtl-OpenAny: return dummy context 0x%0lx\n", (long)pContextDummy);
         pContext = pContextDummy;
         pContextDummy->openRefCount += 1;
      }

      // report errors to the user interface
      if ((errCni != 0) && (dberr != EPGDB_RELOAD_OK))
      {
         UiControlMsg_ReloadError(errCni, dberr, failMsgMode, FALSE);
      }
   }

   assert(EpgContextCtl_CheckConsistancy());
   assert(pContext != NULL);  // should never return NULL - instead returns dummy context

   return pContext->pDbContext;
}

// ---------------------------------------------------------------------------
// Open a database for a specific (or the last used) provider
// - if the db fails to be loaded, the handling is controlled by failRetMode
// - errors are reported to the GUI asynchronously; note: an error may have been
//   reported even if load succeeded, in case caller used CNI=0 and one of the
//   dbs found had an error
//
EPGDB_CONTEXT * EpgContextCtl_Open( uint cni, CTX_FAIL_RET_MODE failRetMode, int failMsgMode )
{
   CTX_CACHE     * pContext;
   bool isNew;

   if ((cni != 0) && (cni != 0x00ff))
   {
      pContext = EpgContextCtl_SearchCni(cni);
      if (pContext == NULL)
      {  // not found in the cache -> create stat entry (see also comment above)
         pContext = EpgContextCtl_AddStat(cni, 0);
         isNew = TRUE;
      }
      else
         isNew = FALSE;

      if (pContext->state == CTX_CACHE_OPEN)
      {  // database already open
         dprintf4("EpgContextCtl-Open: prov %04X found in cache, state %s, peek/open refCount %d/%d\n", cni, CtxCacheStateStr(pContext->state), pContext->peekRefCount, pContext->openRefCount);
         pContext->openRefCount += 1;
      }
      else if ( (pContext->state == CTX_CACHE_PEEK) ||
                (pContext->state == CTX_CACHE_STAT) )
      {  // provider known, but database not yet loaded -> load now

         if (EpgContextCtl_Load(pContext) == FALSE)
         {  // upgrade failed
            UiControlMsg_ReloadError(cni, pContext->reloadErr, failMsgMode, isNew);
            // do not use this context as result
            pContext = NULL;
         }
      }
      else if (pContext->state == CTX_CACHE_ERROR)
      {
         UiControlMsg_ReloadError(cni, pContext->reloadErr, failMsgMode, FALSE);
         pContext = NULL;
      }
      else
      {
         fatal3("EpgContextCtl-Open: context 0x%04X (0x%lx) has illegal state %d", pContext->provCni, (long)pContext, pContext->state);
         pContext = NULL;
      }
   }
   else
   {
      fatal1("EpgContextCtl-Open: illegal CNI 0x%04X", cni);
      pContext = NULL;
   }

   // handle failures according to the given failure handling mode
   if (pContext == NULL)
   {  // provider with the given CNI not found or failed to load
      switch (failRetMode)
      {
         case CTX_FAIL_RET_DUMMY:
            // return a reference to the "dummy" database
            dprintf0("EpgContextCtl-Open: return dummy context\n");
            pContext = pContextDummy;
            pContextDummy->openRefCount += 1;
            break;

         case CTX_FAIL_RET_CREATE:
            // create a new, empty database - for acquisition only
            dprintf1("EpgContextCtl-Open: create new context for CNI 0x%04X\n", cni);
            pContext = EpgContextCtl_SearchCni(cni);
            if (pContext == NULL)
            {
               pContext = EpgContextCtl_CreateNew();
               // add the new context to the cache
               pContext->pNext = pContextCache;
               pContextCache = pContext;
            }
            else
            {
               assert(pContext->state == CTX_CACHE_ERROR);
               if (pContext->pDbContext == NULL)
               {
                  pContext->pDbContext = EpgDbCreate();
               }
            }
            pContext->state         = CTX_CACHE_OPEN;
            pContext->openRefCount  = 1;
            // insert the CNI of the new provider - an AI block has to be added with the same CNI
            pContext->provCni    = cni;
            break;

         case CTX_FAIL_RET_NULL:
            // return NULL pointer - failure checked and handled by caller
            break;

         default:
            fatal1("EpgContextCtl-Open: illegal failRetMode %d", failRetMode);
            pContext = pContextDummy;
            pContextDummy->openRefCount += 1;
            break;
      }
   }
   else
      dprintf5("EpgContextCtl-Open: req CNI 0x%04X: opened db 0x%04X (0x%lx): peek/open ref count %d/%d\n", cni, EpgDbContextGetCni(pContext->pDbContext), (long)pContext, pContext->peekRefCount, pContext->openRefCount);

   assert(EpgContextCtl_CheckConsistancy());

   if (pContext != NULL)
      return pContext->pDbContext;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Opens a database in demo mode
// - returns dummy db if open failed
//
EPGDB_CONTEXT * EpgContextCtl_OpenDemo( void )
{
   EPGDB_RELOAD_RESULT dberr;
   EPGDB_CONTEXT * pDbContext;
   CTX_CACHE     * pContext;

   pDbContext = EpgDbReload(0x00f1, &dberr);  // dummy cni

   if (pDbContext != NULL)
   {
      pContext = EpgContextCtl_SearchCni(EpgDbContextGetCni(pDbContext));
      if (pContext == NULL)
      {
         pContext = xmalloc(sizeof(CTX_CACHE));
         memset(pContext, 0, sizeof(*pContext));

         // add the new context to the cache
         pContext->pNext = pContextCache;
         pContextCache = pContext;
      }
      else
         assert(pContext->state < CTX_CACHE_PEEK);

      pContext->pDbContext    = pDbContext;
      pContext->provCni       = AI_GET_CNI(&pDbContext->pAiBlock->blk.ai);
      pContext->state         = CTX_CACHE_OPEN;
      pContext->openRefCount  = 1;
   }
   else
   {  // open failed -> destroy context, use dummy instead
      pContextDummy->openRefCount += 1;
      pContext = pContextDummy;
   }

   assert(EpgContextCtl_CheckConsistancy());

   return pContext->pDbContext;
}

// ---------------------------------------------------------------------------
// Returns a dummy database context
// - used by epg scan
//
EPGDB_CONTEXT * EpgContextCtl_OpenDummy( void )
{
   dprintf0("EpgContextCtl-OpenDummy: open dummy context\n");
   assert(pContextDummy != NULL);

   assert(EpgContextCtl_CheckConsistancy());

   pContextDummy->openRefCount += 1;
   return pContextDummy->pDbContext;
}

// ---------------------------------------------------------------------------
// Close a database
// - if the db was modified, it's automatically saved before the close
// - the db may be kept open by another party (ui or acq repectively)
//   so actually only the context pointer may be invalidated for the
//   calling party
//
static void EpgContextCtl_CloseInt( EPGDB_CONTEXT * pDbContext, bool isPeek )
{
   CTX_CACHE * pContext;

   assert(EpgContextCtl_CheckConsistancy());

   if (pDbContext != NULL)
   {
      // search for the context in the cache
      pContext = pContextCache;
      while (pContext != NULL)
      {
         if ( (pContext->state >= CTX_CACHE_PEEK) &&
              (pContext->pDbContext == pDbContext) )
         {
            break;
         }
         pContext = pContext->pNext;
      }

      if (pContext != NULL)
      {
         dprintf6("EpgContextCtl-Close: close context 0x%04X (%lx): state %s, peek/open refCount %d/%d, modified: %d\n", EpgDbContextGetCni(pContext->pDbContext), (long)pContext, CtxCacheStateStr(pContext->state), pContext->peekRefCount, pContext->openRefCount, pContext->pDbContext->modified);
         if (isPeek)
         {
            assert(pContext->peekRefCount > 0);
            if (pContext->peekRefCount > 0)
               pContext->peekRefCount -= 1;
         }
         else
         {
            assert((pContext->state == CTX_CACHE_OPEN) && (pContext->openRefCount > 0));
            if (pContext->openRefCount > 0)
               pContext->openRefCount -= 1;
         }

         if ( (pContext->openRefCount == 0) &&
              (pContext->state == CTX_CACHE_OPEN) )
         {
            // if neccessary dump the database
            if (contextLockDump == FALSE)
            {
               EpgDbDump(pContext->pDbContext);
            }

            // free everything except AI and OI
            EpgDbDestroy(pContext->pDbContext, TRUE);
            pContext->state = CTX_CACHE_PEEK;
         }
      }
      else
      {  // not in the cache

         assert(isPeek == FALSE);
         if (pDbContext == pContextDummy->pDbContext)
         {
            dprintf0("EpgContextCtl-Close: close dummy context\n");
            assert(pDbContext->pAiBlock == NULL);

            if (pContextDummy->openRefCount > 0)
               pContextDummy->openRefCount -= 1;
            else
               fatal0("EpgContextCtl-Close: dummy context openRefCount already 0");
         }
         else
         {
            if (EpgDbContextIsMerged(pDbContext))
            {  // merged database is not in the list - there must be only one at any time
               if (pDbContext->pMergeContext != NULL)
               {  // free sub-context
                  EpgContextMergeDestroy(pDbContext->pMergeContext);
                  pDbContext->pMergeContext = NULL;
               }
               EpgDbDestroy(pDbContext, FALSE);
            }
            else
            {  // error: unconnected context
               fatal2("EpgContextCtl-Close: context 0x%lx (AI CNI 0x%04X) not found in list", (ulong)pDbContext, ((pDbContext->pAiBlock != NULL) ? AI_GET_CNI(&pDbContext->pAiBlock->blk.ai) : 0));
               // free the memory anyways
               EpgDbDestroy(pDbContext, FALSE);
            }
         }
      }
   }
   else
      fatal0("EpgContextCtl-Close: NULL ptr param");

   assert(EpgContextCtl_CheckConsistancy());
}

// ---------------------------------------------------------------------------
// Wrappers as interface to internal close function
//
void EpgContextCtl_Close( EPGDB_CONTEXT * pDbContext )
{
   EpgContextCtl_CloseInt(pDbContext, FALSE);
}

void EpgContextCtl_ClosePeek( EPGDB_CONTEXT * pDbContext )
{
   EpgContextCtl_CloseInt(pDbContext, TRUE);
}

#if 0  // currently unused
// ---------------------------------------------------------------------------
// Check if a given provider database is fully loaded
//
void EpgContextCtl_IsLoaded( uint cni )
{
   CTX_CACHE * pContext;
   bool result = FALSE;

   assert(EpgContextCtl_CheckConsistancy());

   if (pDbContext != NULL)
   {
      // search for the context in the cache
      pContext = pContextCache;
      while (pContext != NULL)
      {
         if (pContext->provCni == cni)
         {
            dprintf5("EpgContextCtl-IsPeek: result: context 0x%04X (%lx): state %s, peek/open refCount %d/%d, modified: %d\n", EpgDbContextGetCni(pContext->pDbContext), (long)pContext, CtxCacheStateStr(pContext->state), pContext->peekRefCount, pContext->openRefCount, pContext->pDbContext->modified);
            result = (pContext->state == CTX_CACHE_OPEN);
            break;
         }
         pContext = pContext->pNext;
      }
   }
   else
      fatal0("EpgContextCtl-IsPeek: NULL ptr param");

   return result;
}
#endif

// ---------------------------------------------------------------------------
// Lock automatic dump
// - used by GUI in network acquisition mode
//
void EpgContextCtl_LockDump( bool enable )
{
   contextLockDump = enable;
}

// ---------------------------------------------------------------------------
// Query database for a modification timestamp
//
time_t EpgContextCtl_GetAiUpdateTime( uint cni )
{
   CTX_CACHE * pContext;
   time_t lastAiUpdate;

   pContext = EpgContextCtl_SearchCni(cni);
   if ( (pContext != NULL) &&
        ( (pContext->state == CTX_CACHE_OPEN) ||
          (pContext->state == CTX_CACHE_PEEK) ) )
   {
      lastAiUpdate = EpgDbGetAiUpdateTime(pContext->pDbContext);
      dprintf2("EpgContextCtl-GetAiUpdateTime: use context timestamp %ld = %s", lastAiUpdate, ctime(&lastAiUpdate));
   }
   else
   {
      lastAiUpdate = EpgReadAiUpdateTime(cni);
      dprintf2("EpgContextCtl-GetAiUpdateTime: read file mtime %ld = %s", lastAiUpdate, ctime(&lastAiUpdate));
   }

   return lastAiUpdate;
}

// ---------------------------------------------------------------------------
// Set PI "cut-off" time, i.e. time offset after which expired PI are removed from db
// - assigned to all open databases (already removed PI are not recoverable though)
// - also passed to reload module (where it's stored internally for future loads)
//
void EpgContextCtl_SetPiExpireDelay( time_t expireDelay )
{
   CTX_CACHE  * pWalk;

   EpgDbSavSetPiExpireDelay(expireDelay);

   pWalk = pContextCache;
   while (pWalk != NULL)
   {
      if (pWalk->pDbContext != NULL)
      {
         pWalk->pDbContext->expireDelayPi = expireDelay;

         EpgDbExpire(pWalk->pDbContext);
      }
      pWalk = pWalk->pNext;
   }
}

// ---------------------------------------------------------------------------
// Update tuner frequency for a database
// - allows to change the frequency without loading the complete db
// - used by EPG scan for already known providers (the provider might have been
//   found in passive mode when the channel freq. could not be determined)
//
bool EpgContextCtl_UpdateFreq( uint cni, uint freq )
{
   CTX_CACHE * pContext;
   bool result;

   assert(EpgContextCtl_CheckConsistancy());

   pContext = EpgContextCtl_SearchCni(cni);
   if ((pContext != NULL) && (pContext->state == CTX_CACHE_OPEN))
   {  // db is open -> just set the frequency there
      dprintf3("EpgContextCtl-UpdateFreq: update freq for opened db 0x%04X: %u -> %u\n", cni, pContext->pDbContext->tunerFreq, freq);

      // set the flag that the db needs to be saved
      if (pContext->pDbContext->tunerFreq != freq)
        pContext->pDbContext->modified = TRUE;

      pContext->pDbContext->tunerFreq = freq;
      result = TRUE;
   }
   else
   {  // db is not open -> write the frequency into the file header
      debug2("EpgContextCtl-UpdateFreq: update freq for non-open db 0x%04X: %u", cni, freq);
      result = EpgDbDumpUpdateHeader(cni, freq);
   }
   return result;
}

// ---------------------------------------------------------------------------
// Get number of available providers
// - provider databases which are already known to be defective are not counted
//
uint EpgContextCtl_GetProvCount( void )
{
   CTX_CACHE * pContext;
   uint count;

   assert(EpgContextCtl_CheckConsistancy());

   count = 0;
   pContext = pContextCache;
   while (pContext != NULL)
   {
      if ( (pContext->state == CTX_CACHE_STAT) ||
           (pContext->state == CTX_CACHE_PEEK) ||
           (pContext->state == CTX_CACHE_OPEN) )
      {
         count += 1;
      }
      pContext = pContext->pNext;
   }

   return count;
}

// ---------------------------------------------------------------------------
// Get list of available providers
// - the list is dynamically allocated and must be freed by the caller
//
const uint * EpgContextCtl_GetProvList( uint * pCount )
{
   CTX_CACHE * pContext;
   uint * pCniList;
   uint count, idx;

   // determine the number of providers
   count = EpgContextCtl_GetProvCount();
   if (count > 0)
   {
      // allocate memory for the result table
      pCniList = xmalloc(count * sizeof(uint));

      // build the list
      idx = 0;
      pContext = pContextCache;
      while (pContext != NULL)
      {
         if ( (pContext->state == CTX_CACHE_STAT) ||
              (pContext->state == CTX_CACHE_PEEK) ||
              (pContext->state == CTX_CACHE_OPEN) )
         {
            pCniList[idx++] = pContext->provCni;
         }
         pContext = pContext->pNext;
      }
   }
   else
      pCniList = NULL;

   // return the length of the returned list
   if (pCount != NULL)
      *pCount = count;

   // note: the list must be freed by the caller!
   return pCniList;
}

// ---------------------------------------------------------------------------
// Get list of available providers and their tuner frequencies
// - the list is dynamically allocated and must be freed by the caller
// - for databases which are not cached because they are defective or have and
//   incompatible version, the tuner frequency is read from the header
//
uint EpgContextCtl_GetFreqList( uint ** ppProvList, uint ** ppFreqList )
{
   CTX_CACHE * pContext;
   uint maxCount, idx;
   uint freq;

   // determine the number of providers, including "error" contexts
   maxCount = 0;
   pContext = pContextCache;
   while (pContext != NULL)
   {
      maxCount += 1;
      pContext = pContext->pNext;
   }

   if (maxCount > 0)
   {
      // allocate memory for the result lists
      *ppProvList = xmalloc(maxCount * sizeof(**ppProvList));
      *ppFreqList = xmalloc(maxCount * sizeof(**ppFreqList));

      // build the lists
      idx = 0;
      pContext = pContextCache;
      while ((pContext != NULL) && (idx < maxCount))
      {
         if (pContext->pDbContext != NULL)
         {  // database header is in the cache -> take frequency from there
            freq = pContext->pDbContext->tunerFreq;
         }
         else
         {  // database not loaded/not loadable -> read frequency from file header
            freq = EpgDbReadFreqFromDefective(pContext->provCni);
         }

         if (freq != 0)
         {  // valid frequency available -> append it to the list
            (*ppProvList)[idx] = pContext->provCni;
            (*ppFreqList)[idx] = freq;
            idx += 1;
         }

         pContext = pContext->pNext;
      }

      if (idx == 0)
      {  // not a single database contained a frequency -> free the lists
         xfree(*ppProvList);
         xfree(*ppFreqList);
         *ppProvList = NULL;
         *ppFreqList = NULL;
      }

      // return the number of actually available frequencies
      maxCount = idx;
   }
   else
   {
      *ppProvList = NULL;
      *ppFreqList = NULL;
   }

   // note: the list must be freed by the caller!
   return maxCount;
}

// ---------------------------------------------------------------------------
// Remove a provider from the cache and remove the database
// - fails if the database is still opened
// - returns 0 on success, EBUSY if db open, or POSIX error code from file unlink
//
uint EpgContextCtl_Remove( uint cni )
{
   CTX_CACHE * pContext;
   uint result = 0;

   // search for the context in the cache
   pContext = pContextCache;
   while (pContext != NULL)
   {
      if (pContext->provCni == cni)
         break;
      pContext = pContext->pNext;
   }

   if (pContext != NULL)
   {
      if ( (pContext->openRefCount == 0) &&
           (pContext->peekRefCount == 0) )
      {
         if (pContext->pDbContext != NULL)
         {
            EpgDbDestroy(pContext->pDbContext, FALSE);
            pContext->pDbContext = NULL;
         }

         pContext->state     = CTX_CACHE_ERROR;
         pContext->reloadErr = EPGDB_RELOAD_EXIST;

         result = EpgDbRemoveDatabaseFile(cni);
      }
      else
      {  // db still opened -> abort
         debug3("EpgContextCtl-Remove: cannot remove open db 0x%04X: ref=%d+%d", cni, pContext->openRefCount, pContext->peekRefCount);
         result = EBUSY;
      }
   }
   else
      result = EpgDbRemoveDatabaseFile(cni);

   // a missing database file is no error
   if (result == ENOENT)
      result = 0;

   return result;
}

// ---------------------------------------------------------------------------
// Free all cached contexts
// - called upon program shutdown to free all resources
//
void EpgContextCtl_ClearCache( void )
{
   CTX_CACHE * pContext;
   CTX_CACHE * pNext;

   assert(EpgContextCtl_CheckConsistancy());

   pContext = pContextCache;
   while (pContext != NULL)
   {
      assert((pContext->peekRefCount == 0) && (pContext->openRefCount == 0));  // all users must have closed the databases

      pNext = pContext->pNext;

      if (pContext->pDbContext != NULL)
         EpgDbDestroy(pContext->pDbContext, FALSE);
      xfree(pContext);

      pContext = pNext;
   }
   pContextCache = NULL;

   // free the dummy context
   EpgDbDestroy(pContextDummy->pDbContext, FALSE);
   xfree(pContextDummy);
}

// ---------------------------------------------------------------------------
// Initialize the module
// - called once at program start
// - scans the database directory for available db files
//
void EpgContextCtl_InitCache( void )
{
   const EPGDB_SCAN_BUF  * pScanBuf;
   CTX_CACHE  * pContext;
   uint  idx;

   assert(pContextCache == NULL);

   pContextDummy = EpgContextCtl_CreateNew();

   pScanBuf = EpgDbReloadScan();
   if (pScanBuf != NULL)
   {
      for (idx=0; idx < pScanBuf->count; idx++)
      {
         pContext = EpgContextCtl_AddStat(pScanBuf->list[idx].cni, pScanBuf->list[idx].mtime);
      }
      xfree((void *)pScanBuf);
   }

   assert(EpgContextCtl_CheckConsistancy());
}

