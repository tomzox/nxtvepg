/*
 *  Nextview block ASCII dump
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
 *  $Id: epgtxtdump.h,v 1.9 2001/02/25 16:00:45 tom Exp tom $
 */

#ifndef __EPGTXTDUMP_H
#define __EPGTXTDUMP_H


// ---------------------------------------------------------------------------
// Header for the dumped file (to differentiate from binary dump)
//
#ifdef __EPGTXTDUMP_C
const char * pEpgTxtDumpHeader = "Nextview ASCII Dump\n";
#else
extern const char * pEpgTxtDumpHeader;
#endif

// ---------------------------------------------------------------------------
// declaration of service interface functions
//
#ifdef __EPGBLOCK_H
void EpgTxtDumpPi( FILE *fp, const PI_BLOCK * pPi, uchar stream, uchar version, const AI_BLOCK * pAi );
void EpgTxtDumpAi( FILE *fp, const AI_BLOCK * pAi, uchar stream );
void EpgTxtDumpOi( FILE *fp, const OI_BLOCK * pOi, uchar stream );
void EpgTxtDumpNi( FILE *fp, const NI_BLOCK * pNi, uchar stream );
void EpgTxtDumpMi( FILE *fp, const MI_BLOCK * pMi, uchar stream );
void EpgTxtDumpLi( FILE *fp, const LI_BLOCK * pLi, uchar stream );
void EpgTxtDumpTi( FILE *fp, const TI_BLOCK * pTi, uchar stream );
void EpgTxtDumpBi( FILE *fp, const BI_BLOCK * pBi, uchar stream );
void EpgTxtDumpUnknown( FILE *fp, uchar type );
#endif

// interface to GUI
void EpgTxtDump_Toggle( void );
void EpgTxtDump_Database( EPGDB_CONTEXT *pDbContext, FILE *fp,
                          bool do_pi, bool do_xi, bool do_ai, bool do_ni,
                          bool do_oi, bool do_mi, bool do_li, bool do_ti );

#endif  // __EPGTXTDUMP_H
