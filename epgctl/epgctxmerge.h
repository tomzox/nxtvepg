/*
 *  Nextview EPG database merging
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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgctxmerge.h,v 1.4 2002/01/02 17:06:43 tom Exp tom $
 */

#ifndef __EPGCTXMERGE_H
#define __EPGCTXMERGE_H


// ----------------------------------------------------------------------------
// Declaration of interface functions
//
#ifdef __EPGDBMERGE_H
EPGDB_CONTEXT * EpgContextMerge( uint dbCount, const uint * pCni, MERGE_ATTRIB_VECTOR_PTR pMax,
                                 uint netwopCount, uint * pNetwopList );
#endif  // __EPGDBMERGE_H
void EpgContextMergeInsertPi( const EPGDB_CONTEXT * pAcqContext, EPGDB_BLOCK * pNewBlock );
void EpgContextMergeAiUpdate( const EPGDB_CONTEXT * pAcqContext );
void EpgContextMergeDestroy( void * pMergeContextPtr );
bool EpgContextMergeGetCnis( const EPGDB_CONTEXT * dbc, uint * pCniCount, uint *pCniTab );
bool EpgContextMergeCheckForCni( const EPGDB_CONTEXT * dbc, uint cni );

#ifdef __EPGTSCQUEUE_H
// interface for GUI timescales window
void EpgContextMergeEnableTimescale( const EPGDB_CONTEXT * dbc, bool enable );
EPGDB_PI_TSC * EpgContextMergeGetTimescaleQueue( const EPGDB_CONTEXT * dbc );
#endif


#endif  // __EPGCTXMERGE_H
