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
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
 *
 *  $Id$
 */

#ifndef __EPGCTXCTL_H
#define __EPGCTXCTL_H


// ---------------------------------------------------------------------------
// Declaration of service interface functions
//
EPGDB_CONTEXT * EpgContextCtl_Open( uint cni );
EPGDB_CONTEXT * EpgContextCtl_CreateNew( void );
void EpgContextCtl_Close( EPGDB_CONTEXT * pContext );
void EpgContextCtl_SetDateTime( void );
bool EpgContextCtl_UpdateFreq( uint cni, ulong freq );


#endif  // __EPGCTXCTL_H
