/*
 *  Reading/writing the .nxtvepgrc / nxtvepg.ini file
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
 *  $Id: rcfile.h,v 1.2 2005/01/08 15:16:43 tom Exp tom $
 */

#ifndef __RCFILE_H
#define __RCFILE_H


// ---------------------------------------------------------------------------
// Definitions

#define NETACQ_DFLT_ENABLE 0

#define NETACQ_DFLT_PORT_STR "7658"
#define NETACQ_DFLT_HOSTNAME_STR "localhost"
#define NETACQ_DFLT_IP_STR ""
#define NETACQ_DFLT_MAX_CONN 10
#define NETACQ_DFLT_REMCTL 0
#define NETACQ_DFLT_DO_TCP_IP 0
#define NETACQ_DFLT_SYSLOGLEV 3
#define NETACQ_DFLT_FILELOGLEV 0
#define NETACQ_DFLT_LOGNAME_STR ""

#define ACQ_DFLT_ACQ_MODE ACQMODE_FOLLOW_UI

#define RC_MAX_ACQ_CNI_PROV     (2 * MAX_MERGED_DB_COUNT)
#define RC_MAX_ACQ_CNI_FREQS    (4 * MAX_MERGED_DB_COUNT)  // list contains paris of CNI and freq
#define RC_MAX_DB_NETWWOPS      MAX_NETWOP_COUNT

// limit for forwards compatibility
#define RC_FILE_MIN_VERSION 0x0207C2
#define RC_FILE_COMPAT_VERSION 0x0207C2

// ---------------------------------------------------------------------------
// Type definitions
//
#include "epgtcl/dlg_hwcfg.h"
#include "epgtcl/dlg_acqmode.h"

typedef struct
{
   uint         rc_compat_version;
   uint         rc_nxtvepg_version;
   const char * rc_nxtvepg_version_str;
} RCFILE_VERSION;

typedef struct
{
   uint         acq_mode;
   uint         acq_cni_count;
   uint         acq_cnis[RC_MAX_ACQ_CNI_PROV];
   uint         prov_freq_count;
   uint         prov_freqs[RC_MAX_ACQ_CNI_FREQS];
   uint         epgscan_opt_ftable;
} RCFILE_ACQ;

typedef struct
{
   uint         piexpire_cutoff;
   uint         prov_sel_count;
   uint         prov_selection[RC_MAX_ACQ_CNI_PROV];

   uint         prov_merge_count;
   uint         prov_merge_cnis[MAX_MERGED_DB_COUNT];
   uint         prov_merge_net_count;
   uint         prov_merge_netwops[RC_MAX_DB_NETWWOPS];
   uint         prov_merge_opt_count[MERGE_TYPE_COUNT];
   uint         prov_merge_opts[MERGE_TYPE_COUNT][MAX_MERGED_DB_COUNT];
} RCFILE_DB;

typedef struct
{
   uint         netacq_enable;
   uint         remctl;
   uint         do_tcp_ip;
   const char * pHostName;
   const char * pPort;
   const char * pIpStr;
   const char * pLogfileName;
   uint         max_conn;
   uint         fileloglev;
   uint         sysloglev;
} RCFILE_NETACQ;

#define RCFILE_MAX_WINSRC_COUNT 5
typedef struct
{
   uint         card_idx;
   uint         input;
   uint         acq_prio;
   uint         slicer_type;
   uint         wdm_stop;

   uint         winsrc_count;
   uint         winsrc_param_count[RCFILE_MAX_WINSRC_COUNT]; // dummy
   uint         winsrc[RCFILE_MAX_WINSRC_COUNT][EPGTCL_TVCF_IDX_COUNT];
} RCFILE_TVCARD;

typedef struct
{
   RCFILE_VERSION       version;
   RCFILE_ACQ           acq;
   RCFILE_DB            db;
   RCFILE_NETACQ        netacq;
   RCFILE_TVCARD        tvcard;
} RCFILE;

// ---------------------------------------------------------------------------
// Interface functions
//
void RcFile_Init( void );
void RcFile_Destroy( void );
bool RcFile_Load( const char * pRcPath, bool isDefault, char ** ppErrMsg );
bool RcFile_LoadFromString( const char * pRcString );

FILE * RcFile_WriteCreateFile( const char * pRcPath, char ** ppErrMsg );
bool RcFile_WriteOwnSections( FILE * fp );
bool RcFile_CopyForeignSections( const char * pRcPath, char ** ppBuf, uint * pBufLen );
bool RcFile_WriteCloseFile( FILE * fp, bool writeOk, const char * pRcPath, char ** ppErrMsg );

void RcFile_SetNetAcqEnable( bool isEnabled );
void RcFile_SetNetAcq( const RCFILE_NETACQ * pRcNetAcq );
void RcFile_SetTvCard( const RCFILE_TVCARD * pRcTvCard );
void RcFile_SetAcqMode( const char * pAcqModeStr, const uint * pCniList, uint cniCount );
void RcFile_SetAcqScanOpt( uint optFtable );
void RcFile_SetDbExpireDelay( uint delay );

void RcFile_UpdateDbMergeCnis( const uint * pCniList, uint cniCount );
bool RcFile_UpdateProvSelection( uint cni );
bool RcFile_UpdateMergedProvSelection( void );
void RcFile_UpdateDbMergeNetwops( const uint * pCniList, uint cniCount );
void RcFile_UpdateDbMergeOptions( uint type, const uint * pCniList, uint cniCount );
bool RcFile_UpdateProvFrequency( uint cni, uint freq );
void RcFile_RemoveProvider( uint cni );
void RcFile_UpdateTvCardWinSrc( uint cardIdx, const uint * pParams, uint paramCount );

const RCFILE * RcFile_Query( void );
const char * RcFile_GetAcqModeStr( uint mode );
uint RcFile_GetProvFreqForCni( uint cni );

#endif // __RCFILE_H

