/*
 *  Nextview AI block fragments assembly
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: epgaifrag.h,v 1.2 2020/06/17 19:33:00 tom Exp tom $
 */

#ifndef __EPGAIFRAG_H
#define __EPGAIFRAG_H

void EpgAiFragmentsAddPkg( uint stream, uint pkgNo, const uchar * pData );
void EpgAiFragmentsStartPage( uint streamOfPage, uint firstPkg, uint newPkgCount );
void EpgAiFragmentsBlockStart( const uchar * pHead, uint blockLen, const uchar * pData, uint pkgLen );
void EpgAiFragmentsBreak( uint stream );
void EpgAiFragmentsRestart( void );
const uchar * EpgAiFragmentsAssemble( uint * pBlockLen, uint * pParityErrCnt );

#endif  // __EPGAIFRAG_H
