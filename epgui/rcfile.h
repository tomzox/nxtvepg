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
 *  $Id: rcfile.h,v 1.9 2008/10/19 17:54:50 tom Exp tom $
 */

#ifndef __RCFILE_H
#define __RCFILE_H

#include "epgdb/epgblock.h"             // b/c epgdbmerge.h
#include "epgdb/epgdbmerge.h"           // b/c MERGE_TYPE_COUNT - FIXME

// ---------------------------------------------------------------------------
// Definitions

// default time after which PI which are no longer part of the stream are discarded
// (note: PI which still have a valid block no are not discarded until acq is started)
#define EPGDB_DFLT_EXPIRE_TIME  (4*60)  // in minutes

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

#define TTXGRAB_DFLT_CHN_COUNT 100
#define TTXGRAB_DFLT_START_PG 0x300
#define TTXGRAB_DFLT_END_PG 0x399
#define TTXGRAB_DFLT_OV_PG 0x301
#define TTXGRAB_DFLT_DURATION 120

#ifndef WIN32  // for WIN32 this enum is defined in winshm.h
typedef enum
{
   TVAPP_NONE,
   TVAPP_XAWTV,
   TVAPP_XAWDECODE,
   TVAPP_XDTV,       // former xawdecode
   TVAPP_ZAPPING,
   TVAPP_TVTIME,
   TVAPP_VDR,
   TVAPP_MPLAYER,
   TVAPP_KAFFEINE,
   TVAPP_TOTEM,
   TVAPP_XINE,
   TVAPP_COUNT
} TVAPP_NAME;
#endif

#define ACQ_START_AUTO 0
#define ACQ_START_MANUAL 1

#define ACQ_DFLT_ACQ_MODE ACQMODE_CYCLIC_2

#define RC_MAX_ACQ_CNI_PROV     (2 * MAX_MERGED_DB_COUNT)
#define RC_MAX_ACQ_CNI_FREQS    (4 * MAX_MERGED_DB_COUNT)  // list contains pairs of CNI and freq
#define RC_MAX_DB_NETWOPS       MAX_NETWOP_COUNT

// limit for forwards compatibility
#define RC_FILE_MIN_VERSION 0x0207C2
#define RC_FILE_COMPAT_VERSION 0x0207E0

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
   uint         acq_start;
   //uint         acq_cni_count;  // obsolete
   //uint         acq_cnis[RC_MAX_ACQ_CNI_PROV];  // obsolete
   //uint         prov_freq_count;
   //uint         prov_freqs[RC_MAX_ACQ_CNI_FREQS];
   uint         epgscan_opt_ftable;
} RCFILE_ACQ;

typedef struct
{
   uint         piexpire_cutoff;
   uint         auto_merge_ttx;
   uint         prov_sel_count;
   uint         prov_selection[RC_MAX_ACQ_CNI_PROV];

   uint         prov_merge_count;
   uint         prov_merge_cnis[MAX_MERGED_DB_COUNT];
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
   uint         drv_type;
   uint         card_idx;
   uint         input;
   uint         acq_prio;
   uint         slicer_type;
   uint         wdm_stop;  // obsolete

   uint         winsrc_count;
   uint         winsrc_param_count[RCFILE_MAX_WINSRC_COUNT]; // obsolete
   uint         winsrc[RCFILE_MAX_WINSRC_COUNT][4];  // obsolete, former EPGTCL_TVCF_IDX_COUNT
} RCFILE_TVCARD;

typedef struct
{
   uint         ttx_enable;
   uint         ttx_chn_count;
   uint         ttx_start_pg;
   uint         ttx_end_pg;
   uint         ttx_ov_pg;
   uint         ttx_duration;
   uint         keep_ttx_data;
   const char * perl_path_win;
} RCFILE_TTX;

typedef struct
{
   uint         tvapp_win;
   const char * tvpath_win;
   uint         tvapp_unix;
   const char * tvpath_unix;
} RCFILE_TVAPP;

typedef struct
{
   uint         prov_cni;
   uint         add_sub;
   uint         net_count;
   uint         net_cnis[RC_MAX_DB_NETWOPS];  // note: constant also used in parser config!
} RCFILE_NET_ORDER;

typedef struct
{
   uint         net_cni;
   uint         net_flags;
   const char * name;
} RCFILE_NET_NAMES;

typedef struct
{
   uint         prov_cni;
   uint         atime;
   uint         acount;
   const char * path;
} RCFILE_XMLTV_PROV;

typedef struct
{
   uint         prov_cni;
   uint         net_cni;
   const char * chn_id;
} RCFILE_XMLTV_NETS;

typedef struct
{
   RCFILE_VERSION       version;
   RCFILE_ACQ           acq;
   RCFILE_TTX           ttx;
   RCFILE_DB            db;
   RCFILE_NETACQ        netacq;
   RCFILE_TVCARD        tvcard;
   RCFILE_TVAPP         tvapp;

   RCFILE_NET_ORDER    *net_order;
   RCFILE_NET_NAMES    *net_names;
   RCFILE_XMLTV_PROV   *xmltv_prov;
   RCFILE_XMLTV_NETS   *xmltv_nets;

   uint                 net_order_count;
   uint                 net_names_count;
   uint                 xmltv_prov_count;
   uint                 xmltv_nets_count;
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
void RcFile_SetAcqMode( const char * pAcqModeStr );
void RcFile_SetAcqAutoStart( int autoStart );
void RcFile_SetAcqScanOpt( uint optFtable );
void RcFile_SetDbExpireDelay( uint delay );
void RcFile_SetAutoMergeTtx( int enable );
void RcFile_SetTtxGrabOpt( const RCFILE_TTX * pRcTtxGrab );
void RcFile_SetTvApp( uint appIdx, const char * pPath );

void RcFile_UpdateDbMergeCnis( const uint * pCniList, uint cniCount );
bool RcFile_UpdateProvSelection( uint cni );
bool RcFile_UpdateMergedProvSelection( void );
void RcFile_UpdateDbMergeOptions( uint type, const uint * pCniList, uint cniCount );
void RcFile_RemoveProvider( uint cni );

const RCFILE * RcFile_Query( void );

const char * RcFile_GetAcqModeStr( uint mode );
const char * RcFile_GetXmltvNetworkId( uint cni );
const char * RcFile_GetNetworkName( uint cni );
void RcFile_UpdateNetworkNames( uint count, const uint * pCniList, const char ** pNameList );
void RcFile_GetNetworkSelection( uint provCni, uint * pSelCount, const uint ** ppSelCnis,
                                               uint * pSupCount, const uint ** ppSupCnis );
void RcFile_UpdateNetworkSelection( uint provCni, uint selCount, const uint * pSelCnis,
                                                  uint supCount, const uint * pSupCnis );
void RcFile_AddXmltvProvider( uint cni, const char * pXmlFile );
void RcFile_AddXmltvNetwork( uint provCni, uint netCni, const char * pChnId );
bool RcFile_UpdateXmltvProvAtime( uint provCni, time_t atime, bool doAcountIncr );

#endif // __RCFILE_H
