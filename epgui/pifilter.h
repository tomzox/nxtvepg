/*
 *  Nextview GUI: PI search filter control
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
 *  $Id: pifilter.h,v 1.10 2003/02/01 15:03:00 tom Exp tom $
 */

#ifndef __PIFILTER_H
#define __PIFILTER_H


#ifndef __PIFILTER_C
// search context for pi listbox
extern FILTER_CONTEXT *pPiFilterContext;
#endif

// Initialization and destruction - Interface to the EPG main module
void PiFilter_Create( void );
void PiFilter_Destroy( void );

// Interface to ui control module
void PiFilter_SetNetwopPrefilter( void );
void PiFilter_Expire( void );

// Interface to PI listbox column output
bool PiFilter_ContextCacheMatch( const PI_BLOCK * pPiBlock, uint idx );

#endif  // __PIFILTER_H
