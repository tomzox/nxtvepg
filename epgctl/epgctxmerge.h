/*
 *  Nextview EPG database merging
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
 *  Description: see C source file.
 */

#ifndef __EPGCTXMERGE_H
#define __EPGCTXMERGE_H


// ----------------------------------------------------------------------------
// Declaration of interface functions
//
#ifdef __EPGDBMERGE_H
EPGDB_CONTEXT * EpgContextMerge( uint dbCount, const uint * pCni, MERGE_ATTRIB_VECTOR_PTR pMax,
                                 uint netwopCount, uint * pNetwopList, int errHand );
#endif  // __EPGDBMERGE_H
void EpgContextMergeInsertPi( const EPGDB_CONTEXT * pAcqContext, EPGDB_PI_BLOCK * pNewBlock );
bool EpgContextMergeUpdateDb( uint updCount, uint addCnt, const uint * pProvCni, int errHand );
void EpgContextMergeDestroy( void * pMergeContextPtr );
bool EpgContextMergeGetCnis( const EPGDB_CONTEXT * dbc, uint * pCniCount, uint *pCniTab );
bool EpgContextMergeCheckForCni( const EPGDB_CONTEXT * dbc, uint cni );

#endif  // __EPGCTXMERGE_H
