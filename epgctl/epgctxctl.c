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
 *    The module "owns" all database contexts. All create, reload,
 *    close and destruction requests have to be made through here.
 *    The main purpose is to open a provider's database only once
 *    and to maintain a reference count. A database can be opened
 *    more than once on the following circumstances:
 *    - db is opened for both the browser and acquisition
 *    - acquisition for a database that's part of a merged db:
 *      then the db is opened once by the epgacqctl module and
 *      once by the epgdbmerge module
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id: epgctxctl.c,v 1.3 2001/01/21 12:28:35 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGCTL
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgdb/epgblock.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbsav.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgdbmerge.h"
#include "epgui/uictrl.h"
#include "epgctl/epgmain.h"
#include "epgctl/epgctxctl.h"


// anchor of the list of all db contexts
static EPGDB_CONTEXT * pContextList = NULL;


// ---------------------------------------------------------------------------
// Search for the given CNI in the list of opened context
//
static EPGDB_CONTEXT * EpgContextCtl_SearchCni( uint cni )
{
   EPGDB_CONTEXT * pContext;

   pContext = pContextList;
   while (pContext != NULL)
   {
      if ( EpgDbContextGetCni(pContext) == cni )
      {
         break;
      }
      pContext = pContext->pNext;
   }
   return pContext;
}

// ---------------------------------------------------------------------------
// Return any context that's not merged
//
static EPGDB_CONTEXT * EpgContextCtl_GetAny( void )
{
   EPGDB_CONTEXT * pContext;

   pContext = pContextList;
   while (pContext != NULL)
   {
      if (EpgDbContextIsMerged(pContext) == FALSE)
         break;
      pContext = pContext->pNext;
   }
   return pContext;
}

// ---------------------------------------------------------------------------
// Open a database for a specific (or the last used) provider
// - always returns a context; if there is no db for that CNI or if the
//   reload fails, the context is empty; caller should check if ctx CNI==0
// - always returns error code; maybe set even if load succeeded, in case
//   caller used CNI=0 and one of the dbs found had an error
//
EPGDB_CONTEXT * EpgContextCtl_Open( uint cni, CONTEXT_RELOAD_ERR_HAND errHand )
{
   EPGDB_CONTEXT * pContext;
   EPGDB_RELOAD_RESULT dberr, anErr;
   uint aCni, errCni, index;

   assert((pContextList == 0) || (pContextList->pNext != pContextList));

   dberr = EPGDB_RELOAD_OK;
   errCni = 0;

   if (cni == 0)
   {  // load any provider -> use an already opened one or search for the best
      pContext = EpgContextCtl_GetAny();

      if (pContext == NULL)
      {  // no db open yet -> search and reload the "best"
         cni = EpgDbReloadScan(-1);
         if (cni != 0)
         {
            pContext = EpgDbReload(cni, &dberr);
            if (pContext == NULL)
            {  // reload failed -> try all dbs in the dbdir one after the other
               errCni = cni;
               index = 0;
               while ( ((aCni = EpgDbReloadScan(index)) != 0) && (pContext == NULL) )
               {
                  if (aCni != cni)
                  {
                     pContext = EpgDbReload(aCni, &anErr);
                     if (RELOAD_ERR_WORSE(anErr, dberr))
                     {
                        dberr = anErr;
                        errCni = aCni;
                     }
                  }
                  index += 1;
               }
            }
         }

         if (pContext == NULL)
         {  // no db found -> create an empty one
            pContext = EpgDbCreate();
         }
         pContext->refCount = 1;
         pContext->pNext = pContextList;
         pContextList = pContext;
      }
      else
      {
         pContext->refCount += 1;
      }
   }
   else if (cni == 0x00ff)
   {  // merged db cannot be opened here
      SHOULD_NOT_BE_REACHED;
      pContext = NULL;
   }
   else
   {
      pContext = EpgContextCtl_SearchCni(cni);
      if (pContext == NULL)
      {
         pContext = EpgDbReload(cni, &dberr);
         if (pContext == NULL)
         {
            errCni = cni;
            pContext = EpgDbCreate();
         }
         pContext->refCount = 1;
         pContext->pNext = pContextList;
         pContextList = pContext;
      }
      else
      {
         pContext->refCount += 1;
      }
   }

   // send a messaage to the user interface
   if (dberr != EPGDB_RELOAD_OK)
   {
      UiControlMsg_ReloadError(errCni, dberr, errHand);
   }

   // always returns a valid context (at minimum an empty one for new providers)
   assert(pContextList->pNext != pContextList);
   assert(pContext != NULL);

   return pContext;
}

// ---------------------------------------------------------------------------
// Create an empty database context
// - used by epg scan
// - used by acq when new provider is found
//
EPGDB_CONTEXT * EpgContextCtl_CreateNew( void )
{
   EPGDB_CONTEXT * pContext;

   pContext = EpgDbCreate();
   pContext->refCount = 1;

   pContext->pNext = pContextList;
   pContextList = pContext;

   return pContext;
}

// ---------------------------------------------------------------------------
// Close a database
// - if the db was modified, it's automatically saved before the close
// - the db may be kept open by another party (ui or acq repectively)
//   so actually only the context pointer may be invalidated for the
//   calling party
//
void EpgContextCtl_Close( EPGDB_CONTEXT * pContext )
{
   EPGDB_CONTEXT * pPrev, * pWalk;

   if (pContext != NULL)
   {
      if (EpgDbContextIsMerged(pContext) == FALSE)
      {
         // search for the given context in the list
         pWalk = pContextList;
         pPrev = NULL;
         while (pWalk != NULL)
         {
            if ( pWalk == pContext )
            {
               break;
            }
            pPrev = pWalk;
            pWalk = pWalk->pNext;
         }

         if (pWalk != NULL)
         {
            if (pContext->refCount <= 1)
            {
               assert(pContext->refCount > 0);

               // if neccessary dump the database
               EpgDbDump(pContext);

               // remove the context from the list
               if (pPrev != NULL)
                  pPrev->pNext = pContext->pNext;
               else
                  pContextList = pContext->pNext;

               // free the context
               EpgDbDestroy(pContext);
            }
            else
               pContext->refCount -= 1;
         }
         else
         {
            debug1("EpgContextCtl-Close: context 0x%lx not found in list", (ulong)pContext);
            SHOULD_NOT_BE_REACHED;
         }
      }
      else
      {  // merged database is not in the list - there must be only one at any time
         EpgDbDestroy(pContext);
      }
   }
   else
      debug0("EpgContextCtl-Close: NULL ptr arg");
}

// ---------------------------------------------------------------------------
// Remove expired PI from all db contexts
// - should be called every minute, as close as possible after sec 0
// - must be called when all dbs are unlocked, e.g. from main loop
//
bool EpgContextCtl_Expire( void )
{
   EPGDB_CONTEXT * pContext;
   bool expired;

   pContext = pContextList;
   expired = FALSE;
   while (pContext != NULL)
   {
      assert(EpgDbIsLocked(pContext) == FALSE);

      expired |= EpgDbExpire(pContext);

      pContext = pContext->pNext;
   }

   // handle merged db separately since it's not in the list
   if (EpgDbContextIsMerged(pUiDbContext))
   {
      EpgDbExpire(pUiDbContext);
   }

   return expired;
}

// ---------------------------------------------------------------------------
// Update tuner frequency for a database
//
bool EpgContextCtl_UpdateFreq( uint cni, ulong freq )
{
   EPGDB_CONTEXT * pContext;
   bool result;

   pContext = EpgContextCtl_SearchCni(cni);
   if (pContext != NULL)
   {  // db is opened -> just set the frequency there
      pContext->tunerFreq = freq;
      // set the flag that the db needs to be saved
      pContext->modified = TRUE;
      result = TRUE;
   }
   else
   {  // db is not opened -> write the frequency into the file header
      result = EpgDbDumpUpdateHeader(cni, freq);
   }
   return result;
}

