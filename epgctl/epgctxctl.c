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
 *      once by the epgctxmerge module
 *
 *    In addition to databases the module manages a cache of "database peeks",
 *    which contain the general provider info (i.e. tuner frequency) plus the
 *    AI and OI#0 blocks. The peeks are used e.g. by the GUI when a list of
 *    provider names is to be displayed.  Hence when a database is closed,
 *    it's not entirely freed, but instead stripped down to a peek.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgctxctl.c,v 1.35 2020/06/24 07:33:41 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifndef WIN32
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbif.h"
#include "epgctl/epgctxmerge.h"
#include "epgctl/epgctxctl.h"
#include "epgui/uictrl.h"
#include "epgui/epgmain.h"
#include "xmltv/xmltv_main.h"
#include "xmltv/xmltv_cni.h"


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
   time_t               mtime;         // DB file modification time - for use in states
                                       // STAT & ERROR only - else use AI acqTimestamp!
   EPGDB_CONTEXT      * pDbContext;    // the actual EPG database

} CTX_CACHE;

typedef struct
{
   uint                 listAllocSize;
   uint                 listCount;
   uint               * pCniList;
} CTX_SCAN_LIST;

// ----------------------------------------------------------------------------
// local definitions
//
#ifdef WIN32
#define PATH_SEPARATOR       '\\'
#define PATH_SEPARATOR_STR   "\\"
#else
#define PATH_SEPARATOR       '/'
#define PATH_SEPARATOR_STR   "/"
#endif

// ---------------------------------------------------------------------------
// local variables

static CTX_CACHE * pContextCache;       // anchor of the list of all db contexts
static CTX_CACHE * pContextDummy;       // empty context, used when no provider is available
static bool contextScanDone;            // at least one dbdir scan done


static time_t EpgContextCtl_GetMtime( const char * pFilename );
static void EpgContextCtl_ScanDbDir( const char * pDirPath, const char * pExtension, CTX_SCAN_LIST * pCniBuf );

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
         if ((pContext->provCni == 0) || IS_PSEUDO_CNI(pContext->provCni))
            fatal3("Context-Check: context 0x%lx has illegal CNI 0x%04x, state %s", (long)pContext, pContext->provCni, CtxCacheStateStr(pContext->state));
      }
      else if ((pContext->state == CTX_CACHE_OPEN) || (pContext->state == CTX_CACHE_PEEK))
      {
         if ((pContext->state == CTX_CACHE_PEEK) && (pContext->openRefCount > 0))
            fatal3("Context-Check: illegal openRefCount=%d in PEEK context 0x%lx, CNI 0x%04X", pContext->openRefCount, (long)pContext, pContext->provCni);
         if (pContext->pDbContext == NULL)
            fatal3("Context-Check: context 0x%lx has no DB, state %s, CNI 0x%04X", (long)pContext, CtxCacheStateStr(pContext->state), pContext->provCni);
         if ((pContext->pDbContext != NULL) &&
             ((pContext->provCni == 0) || IS_PSEUDO_CNI(pContext->provCni)))
            fatal3("Context-Check: context 0x%lx has illegal CNI 0x%04x, state %s", (long)pContext, pContext->provCni, CtxCacheStateStr(pContext->state));
         if ((pContext->pDbContext != NULL) && (pContext->pDbContext->provCni != pContext->provCni))
            fatal4("Context-Check: context 0x%lx: prov CNI 0x%04x != DB CNI 0x%04X (state %s)", (long)pContext, pContext->provCni, pContext->pDbContext->provCni, CtxCacheStateStr(pContext->state));
         if (pContext->reloadErr != EPGDB_RELOAD_OK)
            fatal4("Context-Check: illegal reloadErr=%d in %s context 0x%lx, CNI 0x%04X", pContext->reloadErr, CtxCacheStateStr(pContext->state), (long)pContext, pContext->provCni);
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
   if ((pContextDummy->provCni != 0) || (pContextDummy->pDbContext->provCni != 0))
      fatal5("Context-Check: illegal prov CNI 0x%04X or DB CNI 0x%04X in dummy context 0x%lx, state %d (%s)", pContextDummy->provCni, pContextDummy->pDbContext->provCni, (long)pContextDummy, pContextDummy->state, CtxCacheStateStr(pContextDummy->state));
   if (pContextDummy->pNext != NULL)
      fatal4("Context-Check: dummy context 0x%lx next ptr != 0: 0x%lx state %d (%s)", (long)pContextDummy, (long) pContextDummy->pNext, pContextDummy->state, CtxCacheStateStr(pContextDummy->state));
   if (pContextDummy->pDbContext->pAiBlock != NULL)
      fatal1("Context-Check: dummy context (0x%lx) has AI block", (long)pContextDummy);

   //dprintf0("EpgContextCtl-CheckConsistancy: OK\n");

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
static CTX_CACHE * EpgContextCtl_AddStat( uint cni, time_t mtime, const char * pPath )
{
   CTX_CACHE * pContext;

   pContext = xmalloc(sizeof(CTX_CACHE));
   memset(pContext, 0, sizeof(*pContext));

   dprintf4("EpgContextCtl-AddStat: adding context 0x%04X (0x%lx), mtime %ld, path '%s'\n", cni, (long)pContext, mtime, ((pPath != 0)?pPath:""));

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
// Checks if an ERROR cache entry should be used for a result
// - only use if there was a content error and mtime is unchanged
// - do not use cached stat failure (mtime == 0) as it's cheap to retry
//
static bool EpgContextCtl_CachedErrorValid( CTX_CACHE * pContext )
{
   const char * pXmlPath;
   bool result = FALSE;

   if (pContext != NULL)
   {
      pXmlPath = XmltvCni_LookupProviderPath(pContext->provCni);

      result = (pContext->state == CTX_CACHE_ERROR) &&
               ((pContext->reloadErr & EPGDB_RELOAD_XML_MASK) != 0) &&
               (pXmlPath != NULL) &&
               (pContext->mtime == EpgContextCtl_GetMtime(pXmlPath));
   }
   return result;
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
// Load a database, i.e. upgrade from stat or peek state to open
//
static EPGDB_RELOAD_RESULT EpgContextCtl_Load( CTX_CACHE * pContext )
{
   EPGDB_CONTEXT  * pDbContext;
   time_t mtime;
   EPGDB_RELOAD_RESULT dberr;

   dprintf4("EpgContextCtl-Load: CNI 0x%04X, state %s, peek/open refCount %d/%d\n", pContext->provCni, CtxCacheStateStr(pContext->state), pContext->peekRefCount, pContext->openRefCount);

   const char * pDbPath = XmltvCni_LookupProviderPath(pContext->provCni);
   if (pDbPath != NULL)
   {
      dprintf2("EpgContextCtl-Load: load XMLTV for context 0x%04X %s\n", pContext->provCni, pDbPath);
      pDbContext = Xmltv_CheckAndLoad(pDbPath, pContext->provCni, FALSE, &dberr, &mtime);
   }
   else
   {  // path vanished from hash table - should never happen
      debug1("EpgContextCtl-Load: path for XMLTV prov 0x%04X lost", pContext->provCni);
      dberr = EPGDB_RELOAD_ACCESS;
      pDbContext = NULL;
   }

   if (pDbContext != NULL)
   {
      assert(dberr == EPGDB_RELOAD_OK);

      // free peek database for this database, if it exists
      if (pContext->pDbContext != NULL)
      {
         assert(pContext->pDbContext->lockLevel == 0);
         if ((pContext->peekRefCount == 0) && (pContext->openRefCount == 0))
         {  // no one is using the context currently -> discard it
            EpgDbDestroy(pContext->pDbContext, FALSE);
            // install the new & complete context in its place
            pContext->pDbContext = pDbContext;
         }
         else
         {  // db context pointer is still in use -> pointer must not change
            // remove all blocks from the old context
            EpgDbDestroy(pContext->pDbContext, TRUE);

            // XXX should be done in epgdbmgmt.c
            if (pContext->pDbContext->pAiBlock != NULL)
               xfree(pContext->pDbContext->pAiBlock);
            if (pContext->pDbContext->pOiBlock != NULL)
               xfree(pContext->pDbContext->pOiBlock);
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
      pContext->reloadErr      = dberr;
      pContext->mtime          = mtime;
      pContext->openRefCount  += 1;
   }
   else
   {  // upgrade failed -> convert context to error state
      assert(dberr != EPGDB_RELOAD_OK);

      if ((pContext->peekRefCount == 0) && (pContext->openRefCount == 0))
      {
         if (pContext->pDbContext != NULL)
         {
            EpgDbDestroy(pContext->pDbContext, FALSE);
            pContext->pDbContext = NULL;
         }
         pContext->reloadErr = dberr;
         pContext->state = CTX_CACHE_ERROR;
      }
      else
      {  // DB is in use, but complete load failed (e.g. file is truncated)
         // XXX cannot destroy the db
         // note: don't write the error code into the context as it's previously been
         // loaded OK; the code is returned to the caller though
      }
   }

   return dberr;
}

// ---------------------------------------------------------------------------
// Peek into a database
// - returns a database context with at least an AI block in it plus an OI
//   if available; may be a complete database if already loaded
// - if no info is available for the CNI, attempt to peek into the db anyways
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
   time_t mtime;
   bool isNew;

   if ((cni != 0) && (IS_PSEUDO_CNI(cni) == FALSE))
   {
      dberr = EPGDB_RELOAD_OK;
      pContext = EpgContextCtl_SearchCni(cni);

      if (pContext == NULL)
      {  // not found in the cache -> create stat entry (see also comment above)
         pContext = EpgContextCtl_AddStat(cni, 0, NULL);
         isNew = TRUE;
      }
      else
      {  // already in cache -> report errors only if never opened yet
         isNew = (pContext->state == CTX_CACHE_STAT);
      }

      if ( (pContext->state == CTX_CACHE_OPEN) ||
           (pContext->state == CTX_CACHE_PEEK) )
      {  // database or peek in cache
         dprintf4("EpgContextCtl-Peek: prov %04X found in cache, state %s, peek/open refCount %d/%d\n", cni, CtxCacheStateStr(pContext->state), pContext->peekRefCount, pContext->openRefCount);
         pContext->peekRefCount += 1;
      }
      else if ( (pContext->state == CTX_CACHE_STAT) ||
                (pContext->state == CTX_CACHE_ERROR) )
      {  // database known, but not yet loaded -> load AI & OI only
         const char * pDbPath = XmltvCni_LookupProviderPath(pContext->provCni);
         if (pDbPath != NULL)
         {
            if (EpgContextCtl_CachedErrorValid(pContext) == FALSE)
            {
               pDbContext = Xmltv_CheckAndLoad(pDbPath, pContext->provCni, TRUE, &dberr, &mtime);
            }
            else
            {
               dberr = pContext->reloadErr;
               pDbContext = NULL;
            }
         }
         else
         {  // path vanished from hash table - should never happen
            debug1("EpgContextCtl-Peek: path for XMLTV prov 0x%04X lost", pContext->provCni);
            dberr = EPGDB_RELOAD_XML_CNI;
            pDbContext = NULL;
         }

         if (pDbContext != NULL)
         {
            dprintf1("EpgContextCtl-Peek: prov %04X upgrade STAT to PEEK\n", cni);
            assert(pDbContext->provCni == pContext->provCni);
            assert(dberr == EPGDB_RELOAD_OK);

            pContext->state         = CTX_CACHE_PEEK;
            pContext->reloadErr     = EPGDB_RELOAD_OK;
            pContext->mtime         = mtime;
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
      else
      {
         fatal2("EpgContextCtl-Peek: context CNI 0x%04X has illegal state %s", pContext->provCni, CtxCacheStateStr(pContext->state));
         pContext = NULL;
      }
   }
   else
   {  // merged database etc. is not allowed here
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
// Open a database for a specific (or the last used) provider
// - errors are reported to the GUI asynchronously; note: an error may have been
//   reported even if load succeeded, in case caller used CNI=0 and one of the
//   dbs found had an error
//
static CTX_CACHE * EpgContextCtl_OpenInt( CTX_CACHE * pContext, bool isNew,
                                          bool forceOpen, int failMsgMode )
{
   EPGDB_RELOAD_RESULT reloadErr;

   if (pContext != NULL)
   {
      if ( (pContext->state == CTX_CACHE_OPEN) && (forceOpen == FALSE) )
      {  // database already open
         dprintf4("EpgContextCtl-Open: prov %04X found in cache, state %s, peek/open refCount %d/%d\n", pContext->provCni, CtxCacheStateStr(pContext->state), pContext->peekRefCount, pContext->openRefCount);

         // check for content change
         if (pContext->openRefCount == 0)
         {
            const char * pXmlPath = XmltvCni_LookupProviderPath(pContext->provCni);
            if ( (pXmlPath != NULL) &&
                 (EpgDbGetAiUpdateTime(pContext->pDbContext) != EpgContextCtl_GetMtime(pXmlPath)) )
            {
               dprintf1("EpgContextCtl-Open: reloading prov %04X due to mtime change\n", pContext->provCni);
               // reload the database, ignoring error
               EpgContextCtl_Load(pContext);
            }
         }
         pContext->openRefCount += 1;
      }
      else if ( (pContext->state == CTX_CACHE_STAT) ||
                (pContext->state == CTX_CACHE_ERROR) ||
                (pContext->state == CTX_CACHE_PEEK) ||
                ((pContext->state == CTX_CACHE_OPEN) && forceOpen) )
      {  // provider known, but database not yet loaded -> load now

         // skip load if file corrupt and mtime unchanged
         if (EpgContextCtl_CachedErrorValid(pContext) == FALSE)
            reloadErr = EpgContextCtl_Load(pContext);
         else
            reloadErr = pContext->reloadErr;

         if (reloadErr != EPGDB_RELOAD_OK)
         {
            UiControlMsg_ReloadError(pContext->provCni, reloadErr, failMsgMode, isNew);
            // do not use this context as result
            pContext = NULL;
         }
      }
      else
      {
         fatal3("EpgContextCtl-Open: context 0x%04X (0x%lx) has illegal state %d", pContext->provCni, (long)pContext, pContext->state);
         pContext = NULL;
      }

      if (pContext != NULL)
         dprintf5("EpgContextCtl-Open: req CNI 0x%04X: opened db 0x%04X (0x%lx): peek/open ref count %d/%d\n", pContext->provCni, EpgDbContextGetCni(pContext->pDbContext), (long)pContext, pContext->peekRefCount, pContext->openRefCount);
   }
   else
      fatal0("EpgContextCtl-Open: illegal NULL ptr param");

   assert(EpgContextCtl_CheckConsistancy());

   return pContext;
}

// ---------------------------------------------------------------------------
// Open a database for a specific (or the last used) provider
//
EPGDB_CONTEXT * EpgContextCtl_Open( uint cni, bool forceOpen, int failMsgMode )
{
   CTX_CACHE     * pContext;
   bool isNew;

   if ((cni != 0) && (IS_PSEUDO_CNI(cni) == FALSE))
   {
      pContext = EpgContextCtl_SearchCni(cni);
      if (pContext == NULL)
      {  // not found during the directory scan
         // do reverse-lookup of the XML file path by CNI
         const char * pXmlPath = XmltvCni_LookupProviderPath(cni);
         if (pXmlPath != NULL)
         {  // known XMLTV provider -> continue as for explicit XML open
            pContext = EpgContextCtl_AddStat(cni, 0, pXmlPath);
         }
         else
         {  // dangling reference to XMLTV file, i.e. path is unknown -> raise error
            UiControlMsg_ReloadError(cni, EPGDB_RELOAD_XML_CNI, failMsgMode, TRUE);
            pContext = NULL;
         }
         isNew = TRUE;
      }
      else
         isNew = FALSE;

      if (pContext != NULL)
      {
         pContext = EpgContextCtl_OpenInt(pContext, isNew, forceOpen, failMsgMode);
      }
   }
   else
   {  // merged database etc. is not allowed here
      fatal1("EpgContextCtl-Open: illegal CNI 0x%04X", cni);
      pContext = NULL;
   }
   assert(EpgContextCtl_CheckConsistancy());

   if (pContext != NULL)
      return pContext->pDbContext;
   else
      return NULL;
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
         dprintf5("EpgContextCtl-Close: close context 0x%04X (%lx): state %s, peek/open refCount %d/%d\n", EpgDbContextGetCni(pContext->pDbContext), (long)pContext, CtxCacheStateStr(pContext->state), pContext->peekRefCount, pContext->openRefCount);
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
               fatal2("EpgContextCtl-Close: context 0x%lx (prov CNI 0x%04X) not found in list", (ulong)pDbContext, pDbContext->provCni);
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
            dprintf5("EpgContextCtl-IsPeek: result: context 0x%04X (%lx): state %s, peek/open refCount %d/%d\n", EpgDbContextGetCni(pContext->pDbContext), (long)pContext, CtxCacheStateStr(pContext->state), pContext->peekRefCount, pContext->openRefCount);
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
// Get modification time of the given XML file
//
static time_t EpgContextCtl_GetMtime( const char * pFilename )
{
   time_t mtime = 0;
#ifndef WIN32
   struct stat st;
   if (stat(pFilename, &st) == 0)
#else
   struct _stat st;
   if (_stat(pFilename, &st) == 0)
#endif
   {
      mtime = st.st_mtime;
   }
   return mtime;
}

// ---------------------------------------------------------------------------
// Query database for a modification timestamp
// - XMLTV: capture time is equivalent with the file modification time
//   XXX this is not correct for TTX grabber on channels with multiple networks (e.g. Arte/Kika)
//
time_t EpgContextCtl_GetAiUpdateTime( uint cni, bool reload )
{
   CTX_CACHE * pContext;
   time_t lastAiUpdate;

   pContext = EpgContextCtl_SearchCni(cni);
   if ( (pContext != NULL) &&
         (reload == FALSE) &&
        ( (pContext->state == CTX_CACHE_OPEN) ||
          (pContext->state == CTX_CACHE_PEEK) ))
   {
      lastAiUpdate = EpgDbGetAiUpdateTime(pContext->pDbContext);
      dprintf3("EpgContextCtl-GetAiUpdateTime: CNI 0x%04X: use context timestamp %ld = %s", cni, lastAiUpdate, ctime(&lastAiUpdate));
   }
   else
   {
      // XMLTV: get file modification time (if the CNI can be mapped to a file)
      const char * pXmlPath;

      pXmlPath = XmltvCni_LookupProviderPath(cni);
      if (pXmlPath != NULL)
         lastAiUpdate = EpgContextCtl_GetMtime(pXmlPath);
      else
         lastAiUpdate = 0;

      dprintf3("EpgContextCtl-GetAiUpdateTime: CNI 0x%04X: read file mtime %ld = %s", cni, lastAiUpdate, ctime(&lastAiUpdate));
   }

   return lastAiUpdate;
}

// ---------------------------------------------------------------------------
// Query if any provides are available (current directory by default)
// - provider databases which are already known to be defective are not counted
//
bool EpgContextCtl_HaveProviders( void )
{
   CTX_SCAN_LIST cniList;
   CTX_CACHE * pContext;
   bool result = FALSE;

   assert(EpgContextCtl_CheckConsistancy());

   // load provider list, if not done yet
   if (contextScanDone == FALSE)
   {
      dprintf0("EpgContextCtl-GetProvCount: starting scan\n");
      EpgContextCtl_ScanDbDir(".", ".xml", &cniList);
      contextScanDone = TRUE;

      if (cniList.listCount > 0)
      {
         xfree(cniList.pCniList);
         result = TRUE;
      }
   }
   else
   {
      pContext = pContextCache;
      while (pContext != NULL)
      {
         if ( (pContext->state == CTX_CACHE_STAT) ||
              (pContext->state == CTX_CACHE_PEEK) ||
              (pContext->state == CTX_CACHE_OPEN) )
         {
            result = TRUE;
            break;
         }
         pContext = pContext->pNext;
      }
   }

   return result;
}

// ---------------------------------------------------------------------------
// Get list of available providers
// - the list is dynamically allocated and must be freed by the caller
//
const uint * EpgContextCtl_GetProvList( const char * pPath, uint * pCount )
{
   CTX_SCAN_LIST cniList;

   EpgContextCtl_ScanDbDir(pPath, ".xml", &cniList);

   // return the length of the returned list
   if (pCount != NULL)
      *pCount = cniList.listCount;

   // note: the list must be freed by the caller!
   return cniList.pCniList;
}

// ---------------------------------------------------------------------------
// Append a CNI value to the dynamically growing list
//
static void EpgContextCtl_PushScanCni( uint cni, CTX_SCAN_LIST * pCniBuf )
{
   // append the CNI to the list
   if (pCniBuf->pCniList == NULL)
   {
      pCniBuf->listAllocSize = 100;
      pCniBuf->pCniList = xmalloc(sizeof(*pCniBuf->pCniList) * pCniBuf->listAllocSize);
   }
   else if (pCniBuf->listCount >= pCniBuf->listAllocSize)
   {
      pCniBuf->listAllocSize *= 2;
      pCniBuf->pCniList = xrealloc(pCniBuf->pCniList, sizeof(*pCniBuf->pCniList) * pCniBuf->listAllocSize);
   }
   pCniBuf->pCniList[pCniBuf->listCount] = cni;
   pCniBuf->listCount += 1;
}

// ---------------------------------------------------------------------------
// Callback function used during directory scan
//
static void EpgContextCtl_DirScanCb( const char * pFilePath, sint mtime, CTX_SCAN_LIST * pCniBuf )
{
   CTX_CACHE * pContext;
   uint cni;

   dprintf1("EpgContextCtl-DirScanCb: scanning %s\n", pFilePath);

   // TODO skip if already known and mtime unchanged?
   EPGDB_RELOAD_RESULT errCode = Xmltv_CheckHeader(pFilePath);
   if (errCode == EPGDB_RELOAD_OK)
   {
      cni = XmltvCni_MapProvider(pFilePath);

      pContext = EpgContextCtl_SearchCni(cni);
      if (pContext != NULL)
      {  // this file is already known
         if ( (pContext->state == CTX_CACHE_ERROR) &&
              (pContext->mtime != mtime) )
         {  // previous syntax error may be gone -> upgrade context status
            dprintf1("EpgContextCtl-DirScanCb: prov 0x%04X from ERROR to STAT\n", pContext->provCni);
            pContext->state = CTX_CACHE_STAT;
         }
         else
         {
            dprintf2("EpgContextCtl-DirScanCb: prov 0x%04X already in state %s\n", pContext->provCni, CtxCacheStateStr(pContext->state));
         }
      }
      else
      {  // new file -> add a context
         pContext = EpgContextCtl_AddStat(cni, mtime, pFilePath);
      }

      EpgContextCtl_PushScanCni(cni, pCniBuf);
   }
   else
   {
      dprintf1("EpgContextCtl-DirScanCb: XMLTV error: %s\n", Xmltv_TranslateErrorCode(errCode & ~EPGDB_RELOAD_XML_MASK));
      // TODO cache error
   }
}

// ---------------------------------------------------------------------------
// Re-scan for new, changed or removed database files
// - called when the provider selection menu is opened
//
static void EpgContextCtl_ScanDbDir( const char * pDirPath, const char * pExtension, CTX_SCAN_LIST * pCniBuf )
{
#ifndef WIN32
   DIR    *dir;
   struct dirent *entry;
   struct stat st;
   char   *pFilePath;
   uint   extlen, flen;

   if ((pDirPath != NULL) && (pExtension != NULL) && (pCniBuf != NULL))
   {
      dprintf2("EpgContextCtl-ScanDbDir: %s ext:%s\n", pDirPath, pExtension);
      memset(pCniBuf, 0, sizeof(*pCniBuf));
      extlen = strlen(pExtension);

      dir = opendir(pDirPath);
      if (dir != NULL)
      {
         while ((entry = readdir(dir)) != NULL)
         {
            flen = strlen(entry->d_name);
            if ( (extlen == 0) ||
                 ( (flen > extlen) &&
                   (strcmp(entry->d_name + flen - strlen(pExtension), pExtension) == 0) ))
            {
               pFilePath = xmalloc(strlen(pDirPath) + 1 + flen + 1);
               sprintf(pFilePath, "%s" PATH_SEPARATOR_STR "%s", pDirPath, entry->d_name);

               // get file status
               if (stat(pFilePath, &st) == 0)
               {
                  // check if it's a regular file; reject directories, devices, pipes etc.
                  if (S_ISREG(st.st_mode))
                  {
                     EpgContextCtl_DirScanCb(pFilePath, st.st_mtime, pCniBuf);
                  }
                  else
                     dprintf1("Xmltv-ScanDir: not a regular file: %s (skipped)\n", pFilePath);
               }
               else
                  dprintf2("Xmltv-ScanDir: failed stat %s: %s\n", pFilePath, strerror(errno));

               xfree(pFilePath);
            }
         }
         closedir(dir);
      }
      else
         fprintf(stderr, "failed to open XML directory %s: %s\n", pDirPath, strerror(errno));
   }
   else
      fatal0("Xmltv-ScanDir: illegal NULL ptr param");

#else // WIN32
   HANDLE hFind;
   WIN32_FIND_DATA finddata;
   SYSTEMTIME  systime;
   char *pDirPattern;
   struct tm tm;
   bool bMore;
   uint flen;
   char *pFilePath;

   if ((pDirPath != NULL) && (pExtension != NULL) && (pCniBuf != NULL))
   {
      dprintf2("EpgContextCtl-ScanDbDir: %s ext:%s\n", pDirPath, pExtension);
      memset(pCniBuf, 0, sizeof(*pCniBuf));

      pDirPattern = xmalloc(strlen(pDirPath) + 2 + strlen(pExtension) + 1);
      sprintf(pDirPattern, "%s\\*%s", pDirPath, pExtension);

      hFind = FindFirstFile(pDirPattern, &finddata);
      bMore = (hFind != (HANDLE) -1);
      while (bMore)
      {
         flen = strlen(finddata.cFileName);
         pFilePath = xmalloc(strlen(pDirPath) + 1 + flen + 1);
         sprintf(pFilePath, "%s" PATH_SEPARATOR_STR "%s", pDirPath, finddata.cFileName);

         FileTimeToSystemTime(&finddata.ftLastWriteTime, &systime);
         dprintf7("DB %s:  %02d:%02d:%02d - %02d.%02d.%04d\n", finddata.cFileName, systime.wHour, systime.wMinute, systime.wSecond, systime.wDay, systime.wMonth, systime.wYear);
         memset(&tm, 0, sizeof(tm));
         tm.tm_sec   = systime.wSecond;
         tm.tm_min   = systime.wMinute;
         tm.tm_hour  = systime.wHour;
         tm.tm_mday  = systime.wDay;
         tm.tm_mon   = systime.wMonth - 1;
         tm.tm_year  = systime.wYear - 1900;

         EpgContextCtl_DirScanCb(pFilePath, mktime(&tm), pCniBuf);

         xfree(pFilePath);
         bMore = FindNextFile(hFind, &finddata);
      }
      FindClose(hFind);
      xfree(pDirPattern);
   }
#endif

   assert(EpgContextCtl_CheckConsistancy());
}

// ---------------------------------------------------------------------------
// Free all cached contexts
// - called upon program shutdown to free all resources
//
void EpgContextCtl_Destroy( void )
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

   XmltvCni_Destroy();
}

// ---------------------------------------------------------------------------
// Initialize the module
//
void EpgContextCtl_Init( void )
{
   contextScanDone = FALSE;
   pContextDummy = NULL;
   pContextCache = NULL;

   pContextDummy = EpgContextCtl_CreateNew();

   XmltvCni_Init();

   assert(EpgContextCtl_CheckConsistancy());
}

