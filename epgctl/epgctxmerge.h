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
 *  $Id: epgctxmerge.h,v 1.2 2001/04/29 08:45:24 tom Exp tom $
 */

#ifndef __EPGCTXMERGE_H
#define __EPGCTXMERGE_H


// ----------------------------------------------------------------------------
// Declaration of interface functions
//
EPGDB_CONTEXT * EpgContextMerge( uint dbCount, const uint * pCni, MERGE_ATTRIB_VECTOR_PTR pMax );
void EpgContextMergeInsertPi( const EPGDB_CONTEXT * pAcqContext, EPGDB_BLOCK * pNewBlock );
void EpgContextMergeAiUpdate( const EPGDB_CONTEXT * pAcqContext, EPGDB_BLOCK * pAiBlock );
void EpgContextMergeDestroy( void * pMergeContextPtr );
bool EpgContextMergeGetCnis( const EPGDB_CONTEXT * dbc, uint * pCniCount, uint *pCniTab );
void EpgContextMergeAiCheckBlockRange( const EPGDB_CONTEXT * pAcqContext );


#endif  // __EPGCTXMERGE_H
