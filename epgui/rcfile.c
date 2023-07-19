/*
 *  Reading/writing the .nxtvepgrc / nxtvepg.ini file
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *    This module implements functions to manage reading and writing of
 *    the parameter configuration file. Parameters are read once during
 *    start-up into a C structure and can be changed during run-time
 *    via update functions.  Parameter assignments are split into
 *    separate sections.  Not all sections are managed here: parameters
 *    belonging to the GUI are read and written separately; a function
 *    is provided to copy through foreign sections during updates in case
 *    the calling application doesn't include the GUI code.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#ifndef WIN32
#include <unistd.h>
#include <sys/param.h>
#include <sys/time.h>
#include <pwd.h>
#include <signal.h>
#else
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <direct.h>
#endif
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "epgctl/mytypes.h"
#include "epgctl/epgversion.h"
#include "epgctl/debug.h"

#include "epgdb/epgblock.h"
#include "epgdb/epgdbfil.h"
#include "epgdb/epgdbif.h"
#include "epgdb/epgdbmgmt.h"
#include "epgdb/epgnetio.h"
#include "epgctl/epgacqctl.h"
#include "epgctl/epgacqsrv.h"
#include "epgctl/epgacqclnt.h"
#include "epgvbi/syserrmsg.h"
#include "epgui/uictrl.h"
#include "epgctl/epgctxctl.h"
#include "epgtcl/dlg_hwcfg.h"
#include "epgtcl/dlg_acqmode.h"

#include "epgui/cmdline.h"
#include "epgui/rcfile.h"

// ----------------------------------------------------------------------------
// Container struct for "dynamic list" sections
// - these sections effectively are a single assignment where each line contains
//   the same structure and is appended to the list
//
typedef struct
{
   char   ** ppData;
   uint      itemSize;
   uint      maxItemCount;
   uint    * pItemCount;
} RCFILE_DYN_LIST;

#define RC_DYNLIST_STEP_SZ_1       8
#define RC_DYNLIST_STEP_SZ_2      32
#define RC_DYNLIST_STEP_SZ_CRIT  100

// ----------------------------------------------------------------------------
// Table describing mapping of config keywords and values to the C struct
//
#define RC_OFF(MEMBER) ((size_t) &((RCFILE *)0)->MEMBER)
#define RC_CNT(X)  (sizeof(mainRc.X)/sizeof(mainRc.X[0]))
#define RC_TOFF(TYPE,MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define RC_NAME_NONE() NULL
#define RC_OFF_NONE() 0
#define RC_ARR_CNT(X)  (sizeof((X))/sizeof((X)[0]))

static RCFILE mainRc;
static bool mainRcInit = FALSE;

typedef enum
{
   RCPARSE_NO_FLAGS        = 0,
   RCPARSE_EXTERN          = 1<<0,
   RCPARSE_IGNORE_UNKNOWN  = 1<<1,
   RCPARSE_ALLOC           = 1<<2,
   RCPARSE_EXTERN_IGNORE_UNKNOWN = RCPARSE_EXTERN | RCPARSE_IGNORE_UNKNOWN,
} RCPARSE_CFG_FLAGS;

typedef struct
{
   const char *         pKey;
   uint                 value;
} RCPARSE_ENUM;

static const RCPARSE_ENUM rcEnum_AcqMode[] =
{
   { "passive", ACQMODE_PASSIVE },
   { "external", ACQMODE_PASSIVE /*ACQMODE_EXTERNAL*/ },  // obsolete
   { "cyclic_2", ACQMODE_CYCLIC_2 },
   { "cyclic_02", ACQMODE_CYCLIC_02 },
   { "follow-ui", ACQMODE_CYCLIC_2 /*ACQMODE_FOLLOW_UI*/ },  // obsolete
   { "cyclic_012", ACQMODE_CYCLIC_2 /*ACQMODE_CYCLIC_012*/ },  // obsolete
   { "cyclic_12", ACQMODE_CYCLIC_2 /*ACQMODE_CYCLIC_12*/ },  // obsolete
   { (const char *) NULL, ACQMODE_COUNT }
};

static const RCPARSE_ENUM rcEnum_AcqStart[] =
{
   { "auto", ACQ_START_AUTO },
   { "manual", ACQ_START_MANUAL },
   { (const char *) NULL, 0 }
};

typedef enum
{
   RC_TYPE_INT,
   RC_TYPE_HEX,
   RC_TYPE_STR,
   RC_TYPE_STRZ,
   RC_TYPE_ENUM,
   RC_TYPE_OBS
} RC_VAL_TYPE;

typedef struct
{
   RC_VAL_TYPE  type;
   uint         off;
   const char * pKey;
   uint         counterOff;
   uint         maxCount;
   const RCPARSE_ENUM * pEnum;
} RCPARSE_CFG;

#define RCPARSE_IS_LIST(X) ((X)->maxCount != 0)

static const RCPARSE_CFG rcParseCfg_version[] =
{
   { RC_TYPE_INT,  RC_OFF(version.rc_nxtvepg_version), "nxtvepg_version", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STR,  RC_OFF(version.rc_nxtvepg_version_str), "nxtvepg_version_str", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(version.rc_compat_version), "rc_compat_version", RC_OFF_NONE(), 0, NULL },
};
static const RCPARSE_CFG rcParseCfg_acq[] =
{
   { RC_TYPE_OBS,  0, "prov_freqs", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_OBS,  0, "acq_mode_cnis", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_ENUM, RC_OFF(acq.acq_mode), "acq_mode", RC_OFF_NONE(), 0, rcEnum_AcqMode },
   { RC_TYPE_ENUM, RC_OFF(acq.acq_start), "acq_start", RC_OFF_NONE(), 0, rcEnum_AcqStart },
   { RC_TYPE_INT,  RC_OFF(acq.epgscan_opt_ftable), "epgscan_opt_ftable", RC_OFF_NONE(), 0, NULL },
};
static const RCPARSE_CFG rcParseCfg_db[] =
{
   { RC_TYPE_INT,  RC_OFF(db.piexpire_cutoff), "piexpire_cutoff", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(db.auto_merge_ttx), "auto_merge_ttx", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_HEX,  RC_OFF(db.prov_selection), "prov_selection", RC_OFF(db.prov_sel_count), RC_CNT(db.prov_selection), NULL },

   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_cnis), "prov_merge_cnis", RC_OFF(db.prov_merge_count), RC_CNT(db.prov_merge_cnis), NULL },
   { RC_TYPE_OBS,  0, "prov_merge_netwops", RC_OFF_NONE(), 0, NULL },

   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_TITLE]), "prov_merge_cftitle", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_TITLE]), MAX_MERGED_DB_COUNT, NULL },
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_DESCR]), "prov_merge_cfdescr", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_DESCR]), MAX_MERGED_DB_COUNT, NULL },
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_THEMES]), "prov_merge_cfthemes", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_THEMES]), MAX_MERGED_DB_COUNT, NULL},
   { RC_TYPE_OBS,  0, "prov_merge_cfseries", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_EDITORIAL]), "prov_merge_cfeditorial", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_EDITORIAL]), MAX_MERGED_DB_COUNT, NULL},
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_PARENTAL]), "prov_merge_cfparental", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_PARENTAL]), MAX_MERGED_DB_COUNT, NULL},
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_SOUND]), "prov_merge_cfsound", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_SOUND]), MAX_MERGED_DB_COUNT, NULL},
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_FORMAT]), "prov_merge_cfformat", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_FORMAT]), MAX_MERGED_DB_COUNT, NULL},
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_REPEAT]), "prov_merge_cfrepeat", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_REPEAT]), MAX_MERGED_DB_COUNT, NULL},
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_SUBT]), "prov_merge_cfsubt", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_SUBT]), MAX_MERGED_DB_COUNT, NULL},
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_OTHERFEAT]), "prov_merge_cfmisc", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_OTHERFEAT]), MAX_MERGED_DB_COUNT, NULL},
   { RC_TYPE_HEX,  RC_OFF(db.prov_merge_opts[MERGE_TYPE_VPS]), "prov_merge_cfvps", RC_OFF(db.prov_merge_opt_count[MERGE_TYPE_VPS]), MAX_MERGED_DB_COUNT, NULL},
};
static const RCPARSE_CFG rcParseCfg_netacq[] =
{
   { RC_TYPE_INT,  RC_OFF(netacq.netacq_enable), "netacq_enable", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(netacq.remctl), "netacqcf_remctl", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(netacq.do_tcp_ip), "netacqcf_do_tcp_ip", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STRZ, RC_OFF(netacq.pHostName), "netacqcf_host", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STRZ, RC_OFF(netacq.pPort), "netacqcf_port", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STRZ, RC_OFF(netacq.pIpStr), "netacqcf_ip", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STRZ, RC_OFF(netacq.pLogfileName), "netacqcf_logname", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(netacq.max_conn), "netacqcf_max_conn", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(netacq.fileloglev), "netacqcf_fileloglev", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(netacq.sysloglev), "netacqcf_sysloglev", RC_OFF_NONE(), 0, NULL },
};
static const RCPARSE_CFG rcParseCfg_tvcard[] =
{
   { RC_TYPE_INT,  RC_OFF(tvcard.drv_type), "hwcf_drv_type", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(tvcard.card_idx), "hwcf_cardidx", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(tvcard.input), "hwcf_input", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(tvcard.acq_prio), "hwcf_acq_prio", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(tvcard.slicer_type), "hwcf_slicer_type", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(tvcard.wdm_stop), "hwcf_wdm_stop", RC_OFF_NONE(), 0, NULL },  // obsolete
   // note: following must match RCFILE_MAX_WINSRC_COUNT
   { RC_TYPE_INT,  RC_OFF(tvcard.winsrc_count), "tvcardcf_count", RC_OFF_NONE(), 0, NULL },  // obsolete
   { RC_TYPE_INT,  RC_OFF(tvcard.winsrc[0]), "tvcardcf_0", RC_OFF(tvcard.winsrc_param_count[0]), 4, NULL },  // obsolete
   { RC_TYPE_INT,  RC_OFF(tvcard.winsrc[1]), "tvcardcf_1", RC_OFF(tvcard.winsrc_param_count[1]), 4, NULL },  // obsolete
   { RC_TYPE_INT,  RC_OFF(tvcard.winsrc[2]), "tvcardcf_2", RC_OFF(tvcard.winsrc_param_count[2]), 4, NULL },  // obsolete
   { RC_TYPE_INT,  RC_OFF(tvcard.winsrc[3]), "tvcardcf_3", RC_OFF(tvcard.winsrc_param_count[3]), 4, NULL },  // obsolete
   { RC_TYPE_INT,  RC_OFF(tvcard.winsrc[4]), "tvcardcf_4", RC_OFF(tvcard.winsrc_param_count[4]), 4, NULL },  // obsolete
};
static const RCPARSE_CFG rcParseCfg_ttx[] =
{
   { RC_TYPE_INT,  RC_OFF(ttx.ttx_enable), "ttx_enable", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(ttx.ttx_chn_count), "ttx_chn_count", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_HEX,  RC_OFF(ttx.ttx_start_pg), "ttx_start_pg", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_HEX,  RC_OFF(ttx.ttx_end_pg), "ttx_end_pg", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_HEX,  RC_OFF(ttx.ttx_ov_pg), "ttx_ov_pg", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(ttx.ttx_duration), "ttx_duration", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(ttx.keep_ttx_data), "keep_ttx_data", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STRZ, RC_OFF(ttx.perl_path_win), "perl_path_win", RC_OFF_NONE(), 0, NULL },
};
static const RCPARSE_CFG rcParseCfg_tvapp[] =
{
   { RC_TYPE_INT,  RC_OFF(tvapp.tvapp_win), "tvapp_win", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STRZ, RC_OFF(tvapp.tvpath_win), "tvpath_win", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_OFF(tvapp.tvapp_unix), "tvapp_unix", RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STRZ, RC_OFF(tvapp.tvpath_unix), "tvpath_unix", RC_OFF_NONE(), 0, NULL },
};

static const RCPARSE_CFG rcParseCfg_NetOrder[] =
{
   { RC_TYPE_HEX,  RC_TOFF(RCFILE_NET_ORDER, prov_cni), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_TOFF(RCFILE_NET_ORDER, add_sub), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_HEX,  RC_TOFF(RCFILE_NET_ORDER, net_cnis), RC_NAME_NONE(), RC_TOFF(RCFILE_NET_ORDER, net_count), RC_MAX_DB_NETWOPS, NULL },
};
static const RCPARSE_CFG rcParseCfg_NetNames[] =
{
   { RC_TYPE_HEX,  RC_TOFF(RCFILE_NET_NAMES, net_cni), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_HEX,  RC_TOFF(RCFILE_NET_NAMES, net_flags), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STR,  RC_TOFF(RCFILE_NET_NAMES, name), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
};
static const RCPARSE_CFG rcParseCfg_XmltvProv[] =
{
   { RC_TYPE_HEX,  RC_TOFF(RCFILE_XMLTV_PROV, prov_cni), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_TOFF(RCFILE_XMLTV_PROV, atime), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_INT,  RC_TOFF(RCFILE_XMLTV_PROV, acount), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STR,  RC_TOFF(RCFILE_XMLTV_PROV, path), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
};
static const RCPARSE_CFG rcParseCfg_XmltvNets[] =
{
   { RC_TYPE_HEX,  RC_TOFF(RCFILE_XMLTV_NETS, prov_cni), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_HEX,  RC_TOFF(RCFILE_XMLTV_NETS, net_cni), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
   { RC_TYPE_STR,  RC_TOFF(RCFILE_XMLTV_NETS, chn_id), RC_NAME_NONE(), RC_OFF_NONE(), 0, NULL },
};

typedef struct
{
   const char         * pName;
   const RCPARSE_CFG  * pList;
   uint                 listLen;
   RCPARSE_CFG_FLAGS    flags;
   uint                 allocItemSize;
   uint                 allocPtrOff;
   uint                 allocCounterOff;
} RCPARSE_SECT;

static const RCPARSE_SECT rcParseCfg[] =
{
   // assignment sections
   { "VERSION", rcParseCfg_version, RC_ARR_CNT(rcParseCfg_version), RCPARSE_EXTERN_IGNORE_UNKNOWN, 0,0,0 },
   { "ACQUISITION", rcParseCfg_acq, RC_ARR_CNT(rcParseCfg_acq), RCPARSE_NO_FLAGS, 0,0,0 },
   { "TELETEXT GRABBER", rcParseCfg_ttx, RC_ARR_CNT(rcParseCfg_ttx), RCPARSE_NO_FLAGS, 0,0,0 },
   { "DATABASE", rcParseCfg_db, RC_ARR_CNT(rcParseCfg_db), RCPARSE_IGNORE_UNKNOWN, 0,0,0 },
   { "CLIENT SERVER", rcParseCfg_netacq, RC_ARR_CNT(rcParseCfg_netacq), RCPARSE_NO_FLAGS, 0,0,0 },
   { "TV CARDS", rcParseCfg_tvcard, RC_ARR_CNT(rcParseCfg_tvcard), RCPARSE_NO_FLAGS, 0,0,0 },
   { "TV APPLICATION", rcParseCfg_tvapp, RC_ARR_CNT(rcParseCfg_tvapp), RCPARSE_NO_FLAGS, 0,0,0 },
   // list sections
   { "NETWORK ORDER", rcParseCfg_NetOrder, RC_ARR_CNT(rcParseCfg_NetOrder), RCPARSE_ALLOC, sizeof(RCFILE_NET_ORDER), RC_OFF(net_order), RC_OFF(net_order_count) },
   { "NETWORK NAMES", rcParseCfg_NetNames, RC_ARR_CNT(rcParseCfg_NetNames), RCPARSE_ALLOC, sizeof(RCFILE_NET_NAMES), RC_OFF(net_names), RC_OFF(net_names_count) },
   { "XMLTV PROVIDERS", rcParseCfg_XmltvProv, RC_ARR_CNT(rcParseCfg_XmltvProv), RCPARSE_ALLOC, sizeof(RCFILE_XMLTV_PROV), RC_OFF(xmltv_prov), RC_OFF(xmltv_prov_count) },
   { "XMLTV NETWORKS", rcParseCfg_XmltvNets, RC_ARR_CNT(rcParseCfg_XmltvNets), RCPARSE_ALLOC, sizeof(RCFILE_XMLTV_NETS), RC_OFF(xmltv_nets), RC_OFF(xmltv_nets_count) },
};

// ----------------------------------------------------------------------------
// Map value to enum index
// - returns -1 if no
//
static int RcFile_MapValueToEnumIdx( const RCPARSE_ENUM * pEnum, int value )
{
   int  idx;

   if (pEnum != NULL)
   {
      idx = 0;
      while (pEnum->pKey != NULL)
      {
         if (pEnum->value == value)
         {
            break;
         }
         pEnum++;
         idx++;
      }
      if (pEnum->pKey == NULL)
         idx = -1;
   }
   else
      idx = -1;

   return idx;
}

// ----------------------------------------------------------------------------
// Search given enum keyword in list and return associated value
// - if string is not in list, returns pre-defined fallback value
//
static int RcFile_MapEnumStrToValue( const RCPARSE_ENUM * pEnum, const char * pKey )
{
   int value = 0;

   if (pEnum != NULL)
   {
      while (pEnum->pKey != NULL)
      {
         if (strcmp(pKey, pEnum->pKey) == 0)
         {
            value = pEnum->value;
            break;
         }
         pEnum++;
      }
      // not found: use fallback value
      if (pEnum->pKey == NULL)
         value = pEnum->value;
   }

   return value;
}

// ----------------------------------------------------------------------------
// Create file for writing config values
// - on UNIX a temporary file is created which is later moved over the old one
//
FILE * RcFile_WriteCreateFile( const char * pRcPath, char ** ppErrMsg )
{
   time_t now;
   FILE * fp = NULL;

   if (pRcPath != NULL)
   {
#ifndef WIN32
      char * pTmpRcPath = (char*) xmalloc(strlen(pRcPath) + 1+4);
      strcpy(pTmpRcPath, pRcPath);
      strcat(pTmpRcPath, ".tmp");
#else
      const char * pTmpRcPath = pRcPath;
#endif
      fp = fopen(pTmpRcPath, "w");
      if (fp != NULL)
      {
         now = time(NULL);
         fprintf(fp, "#\n"
                     "# nxtvepg configuration file\n"
                     "#\n"
                     "# This file is automatically generated - do not edit\n"
                     "# Written at: %s"
                     "#\n",
                     ctime(&now));
      }
      else
      {
         SystemErrorMessage_Set(ppErrMsg, errno, "Failed to write new config file '", pTmpRcPath, "': ", NULL);
      }
#ifndef WIN32
      xfree(pTmpRcPath);
#endif
   }
   else
      debug0("RcFile-WriteCreateFile: illegal NULL ptr param");

   return fp;
}

// ----------------------------------------------------------------------------
// Close the file for writing
// - UNIX only: if there was a write error the written file is discarded
//
bool RcFile_WriteCloseFile( FILE * fp, bool writeOk, const char * pRcPath, char ** ppErrMsg )
{
#ifndef WIN32
   char * pTmpRcPath;
#endif

   if (fp != NULL)
   {
#ifndef WIN32
      if (fsync(fileno(fp)) != 0)
#else
      if (_commit(fileno(fp)) != 0)
#endif
      {
         debug2("RcFile-WriteCloseFile: error on fsync: %d (%s)", errno, strerror(errno));
         writeOk = FALSE;
      }
      if (fclose(fp) != 0)
      {
         debug2("RcFile-WriteCloseFile: error on close: %d (%s)", errno, strerror(errno));
         writeOk = FALSE;
      }
#ifndef WIN32
      pTmpRcPath = (char*) xmalloc(strlen(pRcPath) + 1 + 4);
      strcpy(pTmpRcPath, pRcPath);
      strcat(pTmpRcPath, ".tmp");

      if (writeOk)
      {
         if (rename(pTmpRcPath, pRcPath) != 0)
         {
            debug4("RcFile-WriteCloseFile: failed to rename '%s' into '%s': %d (%s)", pTmpRcPath, pRcPath, errno, strerror(errno));
            if (*ppErrMsg == NULL)
               SystemErrorMessage_Set(ppErrMsg, errno, "Failed to replace config file '", pRcPath, "': ", NULL);
         }
      }
      else
         unlink(pTmpRcPath);

      xfree(pTmpRcPath);
#endif
   }
   else
      debug0("RcFile-WriteCloseFile: illegal NULL ptr param");

   return writeOk;
}

// ----------------------------------------------------------------------------
// Write section with version information
//
static void RcFile_WriteVersionSection( FILE * fp )
{
   if (fp != NULL)
   {
      fprintf(fp, "\n[VERSION]\n"
                  "nxtvepg_version = 0x%X\n"
                  "nxtvepg_version_str = %s\n"
                  "rc_compat_version = 0x%X\n"
                  "rc_timestamp = %d\n",
                  EPG_VERSION_NO,
                  EPG_VERSION_STR,
                  RC_FILE_COMPAT_VERSION,
                  (int)time(NULL));
   }
   else
      debug0("RcFile-WriteVersionSection: illegal NULL ptr param");
}

// ----------------------------------------------------------------------------
// Write sections of config file which are managed by C level
//
static void RcFile_WriteElement( FILE * fp, const RCPARSE_CFG * pDesc, char * pBase, bool space )
{
   uint  listLen;
   uint  listIdx;
   int   enumIdx;
   int   * pInt;
   char  ** pChar;

   if (RCPARSE_IS_LIST(pDesc))
      listLen = *((int *)(pBase + pDesc->counterOff));
   else
      listLen = 1;

   for (listIdx = 0; listIdx < listLen; listIdx++)
   {
      if (space || (listIdx != 0))
         fprintf(fp, " ");

      switch (pDesc->type)
      {
         case RC_TYPE_INT:
            pInt = (int *)(pBase + pDesc->off);
            fprintf(fp, "%d", pInt[listIdx]);
            break;

         case RC_TYPE_HEX:
            pInt = (int *)(pBase + pDesc->off);
            fprintf(fp, "0x%X", pInt[listIdx]);
            break;

         case RC_TYPE_STR:
         case RC_TYPE_STRZ:
            pChar = (char **)(pBase + pDesc->off);
            if (*pChar != NULL)
            {
               fprintf(fp, "%s", *pChar);
            }
            break;

         case RC_TYPE_ENUM:
            pInt = (int *)(pBase + pDesc->off);
            enumIdx = RcFile_MapValueToEnumIdx(pDesc->pEnum, pInt[listIdx]);
            if (enumIdx >= 0)
            {
               fprintf(fp, "%s", pDesc->pEnum[enumIdx].pKey);
            }
            break;

         case RC_TYPE_OBS:
         default:
            SHOULD_NOT_BE_REACHED;
            break;
      }
   }
}

// ----------------------------------------------------------------------------
// Write sections of config file which are managed by C level
//
bool RcFile_WriteOwnSections( FILE * fp )
{
   const RCPARSE_CFG * pDesc;
   uint  listIdx;
   uint  sectIdx;
   uint  elemIdx;
   uint  allocCount;
   char * allocPtr;

   RcFile_WriteVersionSection(fp);

   for (sectIdx = 0; sectIdx < RC_ARR_CNT(rcParseCfg); sectIdx++)
   {
      if ((rcParseCfg[sectIdx].flags & RCPARSE_EXTERN) == 0)
      {
         fprintf(fp, "\n[%s]\n", rcParseCfg[sectIdx].pName);

         if (rcParseCfg[sectIdx].flags & RCPARSE_ALLOC)
         {
            allocCount = *(uint*)((long)&mainRc + rcParseCfg[sectIdx].allocCounterOff);
            allocPtr = *(char**)((long)&mainRc + rcParseCfg[sectIdx].allocPtrOff);

            for (listIdx = 0; listIdx < allocCount; listIdx++)
            {
               pDesc = rcParseCfg[sectIdx].pList;
               for (elemIdx = 0; elemIdx < rcParseCfg[sectIdx].listLen; elemIdx++, pDesc++)
               {
                  RcFile_WriteElement(fp, pDesc, allocPtr, (elemIdx != 0));
               }
               fprintf(fp, "\n");
               allocPtr += rcParseCfg[sectIdx].allocItemSize;
            }
         }
         else
         {
            pDesc = rcParseCfg[sectIdx].pList;
            for (elemIdx = 0; elemIdx < rcParseCfg[sectIdx].listLen; elemIdx++, pDesc++)
            {
               if (pDesc->type != RC_TYPE_OBS)
               {
                  fprintf(fp, "%s =", pDesc->pKey);

                  RcFile_WriteElement(fp, pDesc, (char*)&mainRc, TRUE);

                  fprintf(fp, "\n");
               }
            }
         }
      }
   }

   return (ferror(fp) == FALSE);
}

// ----------------------------------------------------------------------------
// Read GUI sections into a temporary buffer
// - memory for the buffer must be freed by caller
// - returns error code if file exists but there was an error reading it
//   (caller should not overwrite the file in this case to avoid losing data)
//
bool RcFile_CopyForeignSections( const char * pRcPath, char ** ppBuf, uint * pBufLen )
{
   struct stat st;
   FILE * fp;
   char   sbuf[256];
   char   key[100];
   uint   sectIdx;
   uint   lineLen;
   uint   bufOff;
   bool   copySection;
   bool   partialLine;
   bool   result = TRUE;

   if ((pRcPath != NULL) && (ppBuf != NULL) && (pBufLen != NULL))
   {
      *ppBuf = NULL;
      *pBufLen = 0;

      if (stat(pRcPath, &st) == 0)
      {
         dprintf1("RcFile-CopyForeignSections: allocate buffer for %ld bytes\n", (long)st.st_size);
         *ppBuf = (char*) xmalloc(st.st_size + 1);
         bufOff = 0;

         fp = fopen(pRcPath, "r");
         if (fp != NULL)
         {
            sectIdx = RC_ARR_CNT(rcParseCfg);
            copySection = FALSE;
            partialLine = FALSE;

            while (fgets(sbuf, sizeof(sbuf), fp) != NULL)
            {
               if ( (partialLine == FALSE) &&
                    (sbuf[0] == '[') && (sscanf(sbuf,"[%99[^]]]", key) == 1) )
               {
                  for (sectIdx = 0; sectIdx < RC_ARR_CNT(rcParseCfg); sectIdx++)
                     if (strcmp(rcParseCfg[sectIdx].pName, key) == 0)
                        break;
                  copySection = (sectIdx >= RC_ARR_CNT(rcParseCfg));
               }

               lineLen = strlen(sbuf);

               if (copySection)
               {
                  if (bufOff + lineLen < st.st_size)
                  {
                     memcpy(*ppBuf + bufOff, sbuf, lineLen);
                     bufOff += lineLen;
                  }
                  else
                  {  // should never happen (unless fgets() adds characters for some reason)
                     debug3("RcFile-CopyForeignSections: internal error: buffer overflow: stat size=%ld, read %d+%d", (long)st.st_size, bufOff, lineLen);
                     result = FALSE;
                     break;
                  }
               }
               partialLine = ((lineLen > 0) && (sbuf[lineLen - 1] != '\n'));
            }
            result &= (ferror(fp) == FALSE);
            fclose(fp);
         }
         else
         {  // stat OK buf open not (maybe weird file permissions)
            debug3("RcFile-CopyForeignSections: rc open failed: '%s': %d (%s)", pRcPath, errno, strerror(errno));
            result = FALSE;
         }

         // return number of bytes in buffer and append 0-byte
         *pBufLen = bufOff;
         if (bufOff < st.st_size)
            (*ppBuf)[bufOff] = 0;
      }
   }
   else
      debug0("RcFile-CopyForeignSections: illegal NULL ptr params");

   return result;
}

// ----------------------------------------------------------------------------
// Initialize a dynamic list "context"
// - used to build a new list or to update an existing list
//
static void RcFile_DynListInit( RCFILE_DYN_LIST * pDynList, uint itemSize,
                                char ** ppPtr, uint * pItemCount )
{
   memset(pDynList, 0, sizeof(*pDynList));
   pDynList->itemSize = itemSize;

   pDynList->ppData = ppPtr;
   pDynList->pItemCount = pItemCount;

   pDynList->maxItemCount = *pItemCount;
}

// ----------------------------------------------------------------------------
// Make space for a new item in the channel table buffer
//
static void RcFile_DynListGrow( RCFILE_DYN_LIST * pDynList )
{
   char * pOldData;

   assert(*pDynList->pItemCount <= pDynList->maxItemCount);

   // grow the table if necessary
   if (*pDynList->pItemCount == pDynList->maxItemCount)
   {
      if (pDynList->itemSize < RC_DYNLIST_STEP_SZ_CRIT)
         pDynList->maxItemCount += RC_DYNLIST_STEP_SZ_2;
      else
         pDynList->maxItemCount += RC_DYNLIST_STEP_SZ_1;

      pOldData = *pDynList->ppData;
      *pDynList->ppData = (char*) xmalloc(pDynList->maxItemCount * pDynList->itemSize);

      if (pOldData != NULL)
      {
         memcpy(*pDynList->ppData, pOldData, *pDynList->pItemCount * pDynList->itemSize);
         xfree(pOldData);
      }
      // rc data structures must be zero-initialized (e.g. for string pointers)
      memset(*pDynList->ppData + (*pDynList->pItemCount * pDynList->itemSize), 0,
             (pDynList->maxItemCount - *pDynList->pItemCount) * pDynList->itemSize);
   }

   *pDynList->pItemCount += 1;
}

// ----------------------------------------------------------------------------
// Discard the last entry in the list - called upon parser errors
//
static void RcFile_DynListUngrow( RCFILE_DYN_LIST * pDynList )
{
   if (*pDynList->pItemCount > 0)
   {
      *pDynList->pItemCount -= 1;
   }
   else
      fatal0("RcFile-DynListUngrow: list is already empty");
}

// ----------------------------------------------------------------------------
// Return pointer to the last element
//
static char * RcFile_DynListGetPtr( RCFILE_DYN_LIST * pDynList )
{
   assert(*pDynList->pItemCount > 0);

   return ((char*)*pDynList->ppData + ((*pDynList->pItemCount - 1) * pDynList->itemSize));
}

// ----------------------------------------------------------------------------
// Count elements in integer list
// - elements are separated by blanks
// - note: only integer lists are supported (which don't have escaped blanks)
//
static uint RcFile_ParseTclList( char * pList )
{
   char * p;
   uint  count;

   count = 0;
   p = pList;
   while (*p != 0)
   {
      // Skip leading space. Theoretically, Tcl also accepts any isspace() chars
      // as list separators, but we assume we only get well-formed lists here.
      while (*p == ' ')
         p++;
      if (*p == 0)
         break;
      // found an element
      count += 1;
      // skip element
      while ((*p != 0) && (*p != ' '))
         p++;
   }

   //dprintf2("LIST len=%d: '%s'\n", count, pList);
   return count;
}

// ----------------------------------------------------------------------------
// Assign a value from the rc/ini file to the config struct
//
static bool RcFile_AssignParam( const RCPARSE_CFG * pDesc, uint descCount,
                                char * strval, char * pBase )
{
   uint   count;
   int    scanlen;
   int    value;
   int    listIdx;
   int  * pInt;
   char ** pChar;
   bool   result = TRUE;

   do
   {
      if (RCPARSE_IS_LIST(pDesc))
      {
         assert((pDesc->type != RC_TYPE_STR) && (pDesc->type != RC_TYPE_STRZ));

         count = RcFile_ParseTclList(strval);
         if (count > pDesc->maxCount)  // XXX FIXME allocate dynamically
         {
            debug3("RcFile-AssignParam: too many elements: %d>%d '%.100s'", count, pDesc->maxCount, strval);
            count = pDesc->maxCount;
         }
      }
      else
         count = 1;

      for (listIdx = 0; (listIdx < count) && result; listIdx++)
      {
         scanlen = 0;
         switch (pDesc->type)
         {
            case RC_TYPE_INT:
            case RC_TYPE_HEX:
               if ( (sscanf(strval, " %i%n", &value, &scanlen) >= 1) &&
                    ( (strval[scanlen] == 0) || ((strval[scanlen] == ' ') && ((descCount > 1) || (listIdx + 1 < count))) ))
               {
                  //dprintf3("RcFile-AssignParam: %s: idx=%d val=%.100s\n", pDesc->pKey, listIdx, strval);
                  pInt = (int *)(pBase + pDesc->off);
                  pInt[listIdx] = value;
               }
               else
               {
                  debug3("RcFile-AssignParam: parse error key %s idx=%d: '%.100s'", pDesc->pKey, listIdx, strval);
                  result = FALSE;
               }
               break;

            case RC_TYPE_STR:
            case RC_TYPE_STRZ:
               pChar = (char **)(pBase + pDesc->off);
               if (*pChar != NULL)
                  xfree(*pChar);
               // chop off whitespace at both ends of the string argument
               while (*strval == ' ')
                  strval++;
               scanlen = strlen(strval);
               while ((scanlen > 0) && (strval[scanlen - 1] == ' '))
                  scanlen--;
               if (*strval != 0)
                  *pChar = xstrdup(strval);
               else if (pDesc->type == RC_TYPE_STRZ)
                  *pChar = NULL;  // null-string is allowed
               else
               {
                  debug1("RcFile-AssignParam: missing string value for key %s", pDesc->pKey);
                  result = FALSE;
               }
               break;

            case RC_TYPE_ENUM:
               if (pDesc->pEnum != NULL)
               {
                  value = RcFile_MapEnumStrToValue(pDesc->pEnum, strval);
                  pInt = (int *)(pBase + pDesc->off);
                  *pInt = value;
               }
               else
               {
                  fatal1("RcFile-AssignParam: enum without description array: %s", pDesc->pKey);
                  result = FALSE;
               }
               break;

            case RC_TYPE_OBS:
               // an obsolete element - silently skipped
               break;

            default:
               SHOULD_NOT_BE_REACHED;
               break;
         }
         strval += scanlen;
      }

      if (RCPARSE_IS_LIST(pDesc))
      {
         pInt = (int *)(pBase + pDesc->counterOff);
         *pInt = (result ? count : 0);
      }

      descCount -= 1;
      pDesc += 1;
   } while (result && (descCount > 0));

   return result;
}

// ----------------------------------------------------------------------------
// Process a single line of the config file
//
static bool RcFile_ParseLine( char * sbuf, uint * pSectIdx, RCFILE_DYN_LIST * pDynList )
{
   const RCPARSE_CFG * pDesc;
   char   key[100];
   char * pValStr;
   uint   elemIdx;
   uint   scanlen;
   uint   sectIdx = *pSectIdx;
   bool   result = TRUE;

   if ((sbuf[0] == '[') && (sscanf(sbuf,"[%99[^]]]", key) == 1))
   {
      for (sectIdx = 0; sectIdx < RC_ARR_CNT(rcParseCfg); sectIdx++)
         if (strcmp(rcParseCfg[sectIdx].pName, key) == 0)
            break;
      *pSectIdx = sectIdx;

      if ( (sectIdx < RC_ARR_CNT(rcParseCfg)) &&
           (rcParseCfg[sectIdx].flags & RCPARSE_ALLOC) )
      {
         RcFile_DynListInit( pDynList, rcParseCfg[sectIdx].allocItemSize,
                             (char**)((long)&mainRc + rcParseCfg[sectIdx].allocPtrOff),
                             (uint*)((long)&mainRc + rcParseCfg[sectIdx].allocCounterOff) );
      }
   }
   else if (sectIdx < RC_ARR_CNT(rcParseCfg))
   {
      if (rcParseCfg[sectIdx].flags & RCPARSE_ALLOC)
      {
         if (sbuf[0] != 0)
         {
            RcFile_DynListGrow(pDynList);
            if (RcFile_AssignParam(rcParseCfg[sectIdx].pList, rcParseCfg[sectIdx].listLen,
                                   sbuf, RcFile_DynListGetPtr(pDynList)) == FALSE)
            {
               debug2("RcFile-ParseLine: Parse error section [%s] skipping '%.100s'", rcParseCfg[sectIdx].pName, sbuf);
               RcFile_DynListUngrow(pDynList);
            }
         }
      }
      else if (sscanf(sbuf, "%99s = %n", key, &scanlen) >= 1)
      {
         pValStr = sbuf + scanlen;
         // search key in the descriptor list of the current section
         pDesc = rcParseCfg[sectIdx].pList;
         for (elemIdx = 0; elemIdx < rcParseCfg[sectIdx].listLen; elemIdx++, pDesc++)
         {
            if (strcmp(pDesc->pKey, key) == 0)
            {
               dprintf3("RC [%s] %s = %.100s\n", rcParseCfg[sectIdx].pName, key, pValStr);
               result = RcFile_AssignParam(pDesc, 1, pValStr, (char*)&mainRc);
               break;
            }
         }
         if ( (elemIdx >= rcParseCfg[sectIdx].listLen) &&
              ((rcParseCfg[sectIdx].flags & RCPARSE_IGNORE_UNKNOWN) == 0) )
         {
            debug3("RcFile-Load: [%s] unknown key %s (val '%.100s')", rcParseCfg[sectIdx].pName, key, pValStr);
         }
      }
      else if (sscanf(sbuf, " %1[^#\n]", key) >= 1)
      {
         debug2("RcFile-Load: [%s] parse error: %.100s", rcParseCfg[sectIdx].pName, sbuf);
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Read config values from the rc/ini
// - rc file is split in two parts: first is in Tcl format, which is skipped
// - 2nd part is simple assignments in the form: "KEY=value"
//
bool RcFile_Load( const char * pRcPath, bool isDefault, char ** ppErrMsg )
{
   FILE * fp;
   char * sbuf;
   uint   sectIdx;
   RCFILE_DYN_LIST dynList;
   uint   len;
   bool   parseError;
   bool   result = FALSE;

   if (pRcPath != NULL)
   {
      fp = fopen(pRcPath, "r");
      if (fp != NULL)
      {
         sbuf = (char*) xmalloc(RC_LINE_BUF_SIZE);
         sectIdx = RC_ARR_CNT(rcParseCfg);
         parseError = FALSE;
         result = TRUE;

         while (fgets(sbuf, RC_LINE_BUF_SIZE, fp) != NULL)
         {
            // remove newline from the end of the line
            len = strlen(sbuf);
            if ((len > 0) && (sbuf[len - 1] == '\n'))
            {
               if ((len > 1) && (sbuf[len - 2] == '\r'))
                  sbuf[len - 2] = 0;
               else
                  sbuf[len - 1] = 0;

               if ((RcFile_ParseLine(sbuf, &sectIdx, &dynList) == FALSE) && (parseError == FALSE))
               {
                  SystemErrorMessage_Set(ppErrMsg, errno, "Parse error in rc/ini file, section [", rcParseCfg[sectIdx].pName, "]: ", sbuf, NULL);
               }
            }
            else
            {  // line found which is longer than input buffer - should only occur in foreign sections
               if (sectIdx < RC_ARR_CNT(rcParseCfg))
                  debug2("RcFile-Load: [%s] overly long line skipped (%.40s...)", rcParseCfg[sectIdx].pName, sbuf);

               // continue reading until end of line if reached
               while (fgets(sbuf, RC_LINE_BUF_SIZE, fp) != NULL)
               {
                  len = strlen(sbuf);
                  if ((len > 0) && (sbuf[len - 1] == '\n'))
                     break;
               }
            }
         }

         if (ferror(fp))
         {
            SystemErrorMessage_Set(ppErrMsg, errno, "Read error in config file '", pRcPath, "': ", NULL);
            result = FALSE;
         }
         else if ((mainRc.version.rc_compat_version > RC_FILE_COMPAT_VERSION) && result)
         {
            SystemErrorMessage_Set(ppErrMsg, errno, "rc/ini file is from incompatible newer version ",
                                   mainRc.version.rc_nxtvepg_version_str, NULL);
            // switch to different rc file
            CmdLine_AddRcFilePostfix(EPG_VERSION_STR);
            result = FALSE;
         }
         else if (mainRc.version.rc_compat_version < RC_FILE_COMPAT_VERSION)
         {
            // backward compatibility
            if ( (mainRc.version.rc_compat_version == 0x0207C2) &&
                 ( (mainRc.version.rc_nxtvepg_version >= 0x0207C5) &&
                   (mainRc.version.rc_nxtvepg_version <= 0x0207C6) ))
            {
               // predecessor version had WDM sources appended to the list
               mainRc.tvcard.winsrc_count /= 2;

               if (mainRc.tvcard.winsrc_count > RCFILE_MAX_WINSRC_COUNT)
               {
                  mainRc.tvcard.winsrc_count = RCFILE_MAX_WINSRC_COUNT;
               }
            }
         }
         fclose(fp);
         xfree(sbuf);
      }
      else
      {  // failed to open the rc/ini file
         if ( (errno != ENOENT) || (isDefault == FALSE) )
         {
            SystemErrorMessage_Set(ppErrMsg, errno, "Failed to read config file '", pRcPath, "': ", NULL);
         }
      }
   }
   else
      debug0("RcFile-Load: illegal NULL ptr param");

   return result;
}

// ----------------------------------------------------------------------------
// Read config values from newline-separated string
//
bool RcFile_LoadFromString( const char * pRcString )
{
   const char * pLineEnd;
   char   sbuf[512];
   uint   sectIdx;
   RCFILE_DYN_LIST dynList;
   uint   len;
   bool   result = FALSE;

   sectIdx = RC_ARR_CNT(rcParseCfg);
   result = TRUE;

   while ((pLineEnd = strchr(pRcString, '\n')) != NULL)
   {
      len = pLineEnd - pRcString;
      if (len < sizeof(sbuf) - 1)
      {
         strncpy(sbuf, pRcString, len);
         sbuf[len] = 0;

         result &= RcFile_ParseLine(sbuf, &sectIdx, &dynList);
      }
      pRcString = pLineEnd + 1;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Free dynamically allocated memory in a parameter element
//
static void RcFile_FreeParamMemory( const RCPARSE_CFG * pDesc, uint descCount, char * pBase )
{
   char  ** pChar;
   uint  elemIdx;

   for (elemIdx = 0; elemIdx < descCount; elemIdx++, pDesc++)
   {
      if ((pDesc->type == RC_TYPE_STR) || (pDesc->type == RC_TYPE_STRZ))
      {
         pChar = (char **)(pBase + pDesc->off);
         if (*pChar != NULL)
            xfree(*pChar);
         *pChar = NULL;
      }
   }
}

// ----------------------------------------------------------------------------
// Free dynamically allocated memory in a section
//
static void RcFile_FreeSectionMemory( uint sectIdx )
{
   uint  listIdx;
   uint  allocCount;
   char  * allocPtr;

   if (sectIdx < RC_ARR_CNT(rcParseCfg))
   {
      if (rcParseCfg[sectIdx].flags & RCPARSE_ALLOC)
      {
         allocCount = *(uint*)((long)&mainRc + rcParseCfg[sectIdx].allocCounterOff);
         allocPtr = *(char**)((long)&mainRc + rcParseCfg[sectIdx].allocPtrOff);
         if (allocPtr != NULL)
         {
            for (listIdx = 0; listIdx < allocCount; listIdx++)
            {
               RcFile_FreeParamMemory(rcParseCfg[sectIdx].pList, rcParseCfg[sectIdx].listLen,
                                      allocPtr);
               allocPtr += rcParseCfg[sectIdx].allocItemSize;
            }
            xfree(*(char**)((long)&mainRc + rcParseCfg[sectIdx].allocPtrOff));
            *(uint*)((long)&mainRc + rcParseCfg[sectIdx].allocCounterOff) = 0;
            *(char**)((long)&mainRc + rcParseCfg[sectIdx].allocPtrOff) = NULL;
         }
      }
      else
      {
         RcFile_FreeParamMemory(rcParseCfg[sectIdx].pList, rcParseCfg[sectIdx].listLen,
                                (char *)&mainRc);
      }
   }
}

// ----------------------------------------------------------------------------
// Search the section with the given name
//
static uint RcFile_SearchSectionName( const char * pName )
{
   uint  sectIdx;

   for (sectIdx = 0; sectIdx < RC_ARR_CNT(rcParseCfg); sectIdx++)
   {
      if (strcmp(pName, rcParseCfg[sectIdx].pName) == 0)
      {
         break;
      }
   }
   assert(sectIdx < RC_ARR_CNT(rcParseCfg));  // name mismatch

   return sectIdx;
}

// ----------------------------------------------------------------------------
// Update functions invoked by GUI
// - CAUTION: caller must not provide copy of earlier query if the section
//   contains strings, because old string pointers are freed here!
//
void RcFile_SetNetAcqEnable( bool isEnabled )
{
   mainRc.netacq.netacq_enable = isEnabled;
}

void RcFile_SetNetAcq( const RCFILE_NETACQ * pRcNetAcq )
{
   RcFile_FreeSectionMemory( RcFile_SearchSectionName("CLIENT SERVER") );

   mainRc.netacq = *pRcNetAcq;
}

void RcFile_SetTvCard( const RCFILE_TVCARD * pRcTvCard )
{
   mainRc.tvcard = *pRcTvCard;
}

void RcFile_SetAcqMode( const char * pAcqModeStr )
{
   uint  mode;

   mode = RcFile_MapEnumStrToValue(rcEnum_AcqMode, pAcqModeStr);
   if (mode < ACQMODE_COUNT)
   {
      mainRc.acq.acq_mode = mode;
   }
   else
      debug1("RcFile-SetAcqMode: unknown mode '%s'", pAcqModeStr);
}

void RcFile_SetAcqAutoStart( int autoStart )
{
   mainRc.acq.acq_start = autoStart;
}

void RcFile_SetAcqScanOpt( uint optFtable )
{
   mainRc.acq.epgscan_opt_ftable = optFtable;
}

void RcFile_SetDbExpireDelay( uint delay )
{
   mainRc.db.piexpire_cutoff = delay;
}

void RcFile_SetAutoMergeTtx( int enable )
{
   mainRc.db.auto_merge_ttx = enable;
}

void RcFile_SetTtxGrabOpt( const RCFILE_TTX * pRcTtxGrab )
{
   RcFile_FreeSectionMemory( RcFile_SearchSectionName("TELETEXT GRABBER") );

   mainRc.ttx = *pRcTtxGrab;
}

void RcFile_SetTvApp( uint appIdx, const char * pPath )
{
#ifdef WIN32
   mainRc.tvapp.tvapp_win = appIdx;
   if (mainRc.tvapp.tvpath_win != NULL)
      xfree((void*) mainRc.tvapp.tvpath_win);
   if ((pPath != NULL) && (*pPath != 0))
      mainRc.tvapp.tvpath_win = xstrdup(pPath);
   else
      mainRc.tvapp.tvpath_win = NULL;
#else
   mainRc.tvapp.tvapp_unix = appIdx;
   if (mainRc.tvapp.tvpath_unix != NULL)
      xfree((void*) mainRc.tvapp.tvpath_unix);
   if ((pPath != NULL) && (*pPath != 0))
      mainRc.tvapp.tvpath_unix = xstrdup(pPath);
   else
      mainRc.tvapp.tvpath_unix = NULL;
#endif
}

void RcFile_UpdateDbMergeCnis( const uint * pCniList, uint cniCount )
{
   if (cniCount > MAX_MERGED_DB_COUNT)
      cniCount = MAX_MERGED_DB_COUNT;

   memcpy(mainRc.db.prov_merge_cnis, pCniList, cniCount * sizeof(uint));
   mainRc.db.prov_merge_count = cniCount;
}

void RcFile_UpdateDbMergeOptions( uint type, const uint * pCniList, uint cniCount )
{
   if (type < MERGE_TYPE_COUNT)
   {
      dprintf3("RcFile-UpdateDbMergeOptions: %d: %d CNIs, first 0x%04X\n", type, cniCount, (((cniCount > 0) && (pCniList != NULL)) ? *pCniList : 0));

      if (cniCount > MAX_MERGED_DB_COUNT)
         cniCount = MAX_MERGED_DB_COUNT;

      if ((pCniList != NULL) && (cniCount > 0))
      {
         memcpy(mainRc.db.prov_merge_opts[type], pCniList, cniCount * sizeof(uint));
         mainRc.db.prov_merge_opt_count[type] = cniCount;
      }
      else
         mainRc.db.prov_merge_opt_count[type] = 0;
   }
   else
      fatal1("RcFile-UpdateDbMergeOptions: invalid type %d", type);
}

const char * RcFile_GetAcqModeStr( uint mode )
{
   const char * pResult = NULL;
   int   enumIdx;

   enumIdx = RcFile_MapValueToEnumIdx(rcEnum_AcqMode, mode);
   if (enumIdx >= 0)
   {
      pResult = rcEnum_AcqMode[enumIdx].pKey;
   }
   else
      debug1("RcFile-GetAcqModeStr: invalid mode %d", mode);

   return pResult;
}

// ----------------------------------------------------------------------------
// Fetch the user-assigned names for the given network or provider CNI
//
const char * RcFile_GetNetworkName( uint cni )
{
   RCFILE_NET_NAMES  * pNetNames;
   uint idx;
   const char * pResult = NULL;

   assert(mainRcInit != FALSE);

   pNetNames = mainRc.net_names;
   for (idx = 0; idx < mainRc.net_names_count; idx++, pNetNames++)
   {
      if (pNetNames->net_cni == cni)
      {
         pResult = pNetNames->name;
         break;
      }
   }
   return pResult;
}

// ----------------------------------------------------------------------------
// Fetch the user-assigned names for the given network or provider CNI
//
void RcFile_UpdateNetworkNames( uint count, const uint * pCniList, const char ** pNameList )
{
   RCFILE_DYN_LIST dynList;
   uint newIdx;
   uint oldIdx;

   RcFile_DynListInit(&dynList, sizeof(RCFILE_NET_NAMES),
                      (char**)&mainRc.net_names, &mainRc.net_names_count);

   for (newIdx = 0; newIdx < count; newIdx++)
   {
      if (pCniList[newIdx] != 0)
      {
         for (oldIdx = 0; oldIdx < mainRc.net_names_count; oldIdx++)
            if (mainRc.net_names[oldIdx].net_cni == pCniList[newIdx])
               break;
         if (oldIdx < mainRc.net_names_count)
         {  // replace existing name
            if ( (mainRc.net_names[oldIdx].name == NULL) ||
                 (strcmp(mainRc.net_names[oldIdx].name, pNameList[newIdx]) != 0) )
            {
               dprintf3("RcFile-UpdateNetworkNames: change 0x%04X: '%s' into '%s'\n", mainRc.net_names[oldIdx].net_cni, mainRc.net_names[oldIdx].name, pNameList[newIdx]);
               if (mainRc.net_names[oldIdx].name != NULL)
                  xfree((char*)mainRc.net_names[oldIdx].name);
               mainRc.net_names[oldIdx].name = xstrdup(pNameList[newIdx]);
            }
            //else printf("UNCHANGED: 0x%04X: %s\n", mainRc.net_names[oldIdx].net_cni, pNameList[newIdx]);
         }
         else
         {  // append new name
            dprintf2("RcFile-UpdateNetworkNames: append 0x%04X: %s\n", pCniList[newIdx], pNameList[newIdx]);
            RcFile_DynListGrow(&dynList);
            mainRc.net_names[oldIdx].name = xstrdup(pNameList[newIdx]);
            mainRc.net_names[oldIdx].net_cni = pCniList[newIdx];
            //printf("NEW: 0x%04X: %s\n", mainRc.net_names[oldIdx].net_cni, mainRc.net_names[oldIdx].name);
         }
      }
      else
         debug1("RcFile-UpdateNetworkNames: invalid CNI 0 in name list at idx %d", newIdx);
   }
}

// ----------------------------------------------------------------------------
// Get network selection configuration for the given provider
// - pointers may be NULL if the respective return value is not needed
//   (makes no sense though to retrieve a CNI list without count)
// - if no config is available for the provider, the returned pointers are NULL
//
void RcFile_GetNetworkSelection( uint provCni, uint * pSelCount, const uint ** ppSelCnis,
                                               uint * pSupCount, const uint ** ppSupCnis )
{
   bool foundSel = FALSE;
   bool foundSup = FALSE;
   uint idx;

   for (idx = 0; idx < mainRc.net_order_count; idx++)
   {
      if (mainRc.net_order[idx].prov_cni == provCni)
      {
         if (mainRc.net_order[idx].add_sub != 0)
         {
            if (ppSelCnis != NULL)
               *ppSelCnis = mainRc.net_order[idx].net_cnis;
            if (pSelCount != NULL)
               *pSelCount = mainRc.net_order[idx].net_count;
            foundSel = TRUE;
         }
         else
         {
            if (ppSupCnis != NULL)
               *ppSupCnis = mainRc.net_order[idx].net_cnis;
            if (pSupCount != NULL)
               *pSupCount = mainRc.net_order[idx].net_count;
            foundSup = TRUE;
         }
      }
   }

   if (foundSel == FALSE)
   {
      if (ppSelCnis != NULL)
         *ppSelCnis = NULL;
      if (pSelCount != NULL)
         *pSelCount = 0;
   }
   if (foundSup == FALSE)
   {
      if (ppSupCnis != NULL)
         *ppSupCnis = NULL;
      if (pSupCount != NULL)
         *pSupCount = 0;
   }
}

// ----------------------------------------------------------------------------
// Update network selection configuration for the given provider
// - either list pointer can be NULL if the list need not be updated
//
void RcFile_UpdateNetworkSelection( uint provCni, uint selCount, const uint * pSelCnis,
                                                  uint supCount, const uint * pSupCnis )
{
   RCFILE_DYN_LIST dynList;
   uint idx;
   bool foundSel;
   bool foundSup;

   if (selCount > RC_MAX_DB_NETWOPS)
   {
       debug2("RcFile-UpdateNetworkSelection: Dropping excessive number of selected CNIs: %d > %d", selCount, RC_MAX_DB_NETWOPS);
       selCount = RC_MAX_DB_NETWOPS;
   }
   if (supCount > RC_MAX_DB_NETWOPS)
   {
       debug2("RcFile-UpdateNetworkSelection: Dropping excessive number of suppressed CNIs: %d > %d", supCount, RC_MAX_DB_NETWOPS);
       supCount = RC_MAX_DB_NETWOPS;
   }

   RcFile_DynListInit(&dynList, sizeof(RCFILE_NET_ORDER),
                      (char**)&mainRc.net_order, &mainRc.net_order_count);

   foundSel = FALSE;
   foundSup = FALSE;
   for (idx = 0; idx < mainRc.net_order_count; idx++)
   {
      if (mainRc.net_order[idx].prov_cni == provCni)
      {
         dprintf3("RcFile-UpdateNetworkSelection: update %s list for prov 0x%04X: %d entries\n", (mainRc.net_order[idx].add_sub?"SEL":"SUP"), provCni, selCount);
         if (mainRc.net_order[idx].add_sub != 0)
         {
            if (pSelCnis != NULL)
            {
               memcpy(mainRc.net_order[idx].net_cnis, pSelCnis, selCount * sizeof(*pSelCnis));
               mainRc.net_order[idx].net_count = selCount;
            }
            foundSel = TRUE;
         }
         else
         {
            if (pSupCnis != NULL)
            {
               memcpy(mainRc.net_order[idx].net_cnis, pSupCnis, supCount * sizeof(*pSupCnis));
               mainRc.net_order[idx].net_count = supCount;
            }
            foundSup = TRUE;
         }
      }
   }

   if ((foundSel == FALSE) && (pSelCnis != NULL))
   {
      dprintf2("RcFile-UpdateNetworkSelection: append SEL list for prov 0x%04X: %d entries\n", provCni, selCount);
      RcFile_DynListGrow(&dynList);
      idx = mainRc.net_order_count - 1;
      memcpy(mainRc.net_order[idx].net_cnis, pSelCnis, selCount * sizeof(*pSelCnis));
      mainRc.net_order[idx].net_count = selCount;
      mainRc.net_order[idx].add_sub = 1;
      mainRc.net_order[idx].prov_cni = provCni;
   }
   if ((foundSup == FALSE) && (pSupCnis != NULL))
   {
      dprintf2("RcFile-UpdateNetworkSelection: append SUP list for prov 0x%04X: %d entries\n", provCni, selCount);
      RcFile_DynListGrow(&dynList);
      idx = mainRc.net_order_count - 1;
      memcpy(mainRc.net_order[idx].net_cnis, pSupCnis, supCount * sizeof(*pSupCnis));
      mainRc.net_order[idx].net_count = supCount;
      mainRc.net_order[idx].add_sub = 0;
      mainRc.net_order[idx].prov_cni = provCni;
   }
}

// ----------------------------------------------------------------------------
// Add a new XMLTV provider path
// - caller must make sure the provider CNI is not yet defined
//
void RcFile_AddXmltvProvider( uint cni, const char * pXmlFile )
{
   RCFILE_DYN_LIST dynList;

   RcFile_DynListInit(&dynList, sizeof(RCFILE_XMLTV_PROV),
                      (char**)&mainRc.xmltv_prov, &mainRc.xmltv_prov_count);

   RcFile_DynListGrow(&dynList);

   mainRc.xmltv_prov[mainRc.xmltv_prov_count - 1].prov_cni = cni;
   mainRc.xmltv_prov[mainRc.xmltv_prov_count - 1].path = xstrdup(pXmlFile);
}

// ----------------------------------------------------------------------------
// Update an XMLTV provider's access time and count
//
bool RcFile_UpdateXmltvProvAtime( uint provCni, time_t atime, bool doAcountIncr )
{
   uint idx;
   bool result = FALSE;

   for (idx = 0; idx < mainRc.xmltv_prov_count; idx++)
   {
      if (mainRc.xmltv_prov[idx].prov_cni == provCni)
      {
         mainRc.xmltv_prov[idx].atime = atime;
         if (doAcountIncr)
            mainRc.xmltv_prov[idx].acount += 1;
         result = TRUE;
         break;
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Fetch the original channel ID for the given network CNI
//
const char * RcFile_GetXmltvNetworkId( uint cni )
{
   RCFILE_XMLTV_NETS   *pNetNames;
   uint idx;
   const char * pResult = NULL;

   pNetNames = mainRc.xmltv_nets;
   for (idx = 0; idx < mainRc.xmltv_nets_count; idx++, pNetNames++)
   {
      if (pNetNames->net_cni == cni)
      {
         pResult = pNetNames->chn_id;
         break;
      }
   }
   return pResult;
}

// ----------------------------------------------------------------------------
// Add a new XMLTV channel ID for a given provider
// - caller must make sure the network CNI is not yet defined for this provider
//
void RcFile_AddXmltvNetwork( uint provCni, uint netCni, const char * pChnId )
{
   RCFILE_DYN_LIST dynList;

   RcFile_DynListInit(&dynList, sizeof(RCFILE_XMLTV_NETS),
                      (char**)&mainRc.xmltv_nets, &mainRc.xmltv_nets_count);

   RcFile_DynListGrow(&dynList);

   mainRc.xmltv_nets[mainRc.xmltv_nets_count - 1].prov_cni = provCni;
   mainRc.xmltv_nets[mainRc.xmltv_nets_count - 1].net_cni = netCni;
   mainRc.xmltv_nets[mainRc.xmltv_nets_count - 1].chn_id = xstrdup(pChnId);
}

// ----------------------------------------------------------------------------
// Update the provider preference order: move the last selected CNI to the front
// - note: only the first item is used currently; rest of the list is OBSOLETE
//
bool RcFile_UpdateProvSelection( uint cni )
{
   uint prov_sel_count;
   uint prov_selection[RC_MAX_ACQ_CNI_PROV];
   uint idx;
   bool modified;

   // check if an update is required
   if ( (mainRc.db.prov_sel_count == 0) || (mainRc.db.prov_selection[0] != cni))
   {
      dprintf1("RcFile-UpdateProvSelection: CNI 0x%04X\n", cni);

      // place new CNI at first position
      prov_sel_count = 1;
      prov_selection[0] = cni;

      // copy the other CNIs behind it (while skipping the new CNI)
      for (idx = 0; (idx < mainRc.db.prov_sel_count) && (prov_sel_count < RC_MAX_ACQ_CNI_PROV); idx++)
      {
         if (mainRc.db.prov_selection[idx] != cni)
         {
            prov_selection[prov_sel_count] = mainRc.db.prov_selection[idx];
            prov_sel_count += 1;
         }
      }

      // finally copy the new list into the database
      memcpy(mainRc.db.prov_selection, prov_selection, sizeof(mainRc.db.prov_selection));
      mainRc.db.prov_sel_count = prov_sel_count;

      modified = TRUE;
   }
   else
      modified = FALSE;

   return modified;
}

// ----------------------------------------------------------------------------
// Same as above, but with more than one CNI, taken from merged database
// - first CNI 0x00FF is implied
//
bool RcFile_UpdateMergedProvSelection( void )
{
   uint  prov_sel_count;
   uint  prov_selection[RC_MAX_ACQ_CNI_PROV];
   uint  cmpIdx;
   uint  idx;
   uint  cni;
   bool  modified;

   // check if an update is required
   if ( (mainRc.db.prov_sel_count == mainRc.db.prov_merge_count + 1) &&
        (mainRc.db.prov_selection[0] == MERGED_PROV_CNI) )
   {
      for (cmpIdx = 0; (cmpIdx < mainRc.db.prov_merge_count) &&
                       (cmpIdx + 1 < mainRc.db.prov_sel_count); cmpIdx++)
         if (mainRc.db.prov_merge_cnis[cmpIdx] != mainRc.db.prov_selection[cmpIdx + 1])
            break;
      modified = (cmpIdx >= mainRc.db.prov_merge_count);
   }
   else
      modified = TRUE;

   if (modified)
   {
      // place new CNI at first position
      prov_sel_count = 1 + mainRc.db.prov_merge_count;
      prov_selection[0] = MERGED_PROV_CNI;

      memcpy(prov_selection + 1, mainRc.db.prov_merge_cnis,
                                 mainRc.db.prov_merge_count * sizeof(uint));

      // copy the other CNIs behind it (while skipping the new CNI)
      for (idx = 0; (idx < mainRc.db.prov_sel_count) &&
                    (prov_sel_count < RC_MAX_ACQ_CNI_PROV); idx++)
      {
         cni = mainRc.db.prov_selection[idx];
         if ((cni != MERGED_PROV_CNI) && (cni != 0))
         {
            for (cmpIdx = 0; cmpIdx < mainRc.db.prov_merge_count; cmpIdx++)
               if (mainRc.db.prov_merge_cnis[cmpIdx] == cni)
                  break;
            if (cmpIdx >= mainRc.db.prov_merge_count)
            {
               prov_selection[prov_sel_count] = cni;
               prov_sel_count += 1;
            }
         }
      }

      // finally copy the new list into the database
      memcpy(mainRc.db.prov_selection, prov_selection, sizeof(mainRc.db.prov_selection));
      mainRc.db.prov_sel_count = prov_sel_count;

      modified = TRUE;
   }
   else
      modified = FALSE;

   return modified;
}

// ----------------------------------------------------------------------------
// Remove provider CNI from all lists
//
void RcFile_RemoveProvider( uint cni )
{
   uint  tmpl[RC_MAX_ACQ_CNI_FREQS];
   uint  type;
   uint  idx;
   uint  count;

   // remove from provider selection
   count = 0;
   for (idx = 0; idx < mainRc.db.prov_sel_count; idx++)
   {
      if (mainRc.db.prov_selection[idx] != cni)
      {
         tmpl[count] = mainRc.db.prov_selection[idx];
         count += 1;
      }
   }
   memcpy(mainRc.db.prov_selection, tmpl, count * sizeof(uint));
   mainRc.db.prov_sel_count = count;

   // remove from merge provider list
   count = 0;
   for (idx = 0; idx < mainRc.db.prov_merge_count; idx++)
   {
      if (mainRc.db.prov_merge_cnis[idx] != cni)
      {
         tmpl[count] = mainRc.db.prov_merge_cnis[idx];
         count += 1;
      }
   }
   memcpy(mainRc.db.prov_merge_cnis, tmpl, count * sizeof(uint));
   mainRc.db.prov_merge_count = count;

   // remove from all merge options
   for (type = 0; type < MERGE_TYPE_COUNT; type++)
   {
      count = 0;
      for (idx = 0; idx < mainRc.db.prov_merge_opt_count[type]; idx++)
         if (mainRc.db.prov_merge_opts[type][idx] != cni)
            tmpl[count++] = mainRc.db.prov_merge_opts[type][idx];

      memcpy(mainRc.db.prov_merge_opts[type], tmpl, count * sizeof(uint));
      mainRc.db.prov_merge_opt_count[type] = count;
   }

   // remove network selection
   for (idx = 0; idx < mainRc.net_order_count; /* no increment */ )
   {
      if (mainRc.net_order[idx].prov_cni == cni)
      {
         memmove(mainRc.net_order + idx, mainRc.net_order + idx + 1,
                 (mainRc.net_order_count - idx) * sizeof(mainRc.net_order[0]));
         mainRc.net_order_count -= 1;
      }
      else
         idx += 1;
   }

   // TODO: remove net selection
}

// ----------------------------------------------------------------------------
// Return pointer to configuration data
//
const RCFILE * RcFile_Query( void )
{
   assert(mainRcInit != FALSE);

   return &mainRc;
}

// ----------------------------------------------------------------------------
// Free allocated memory in config struct
//
void RcFile_Destroy( void )
{
   uint  sectIdx;

   for (sectIdx = 0; sectIdx < RC_ARR_CNT(rcParseCfg); sectIdx++)
   {
      RcFile_FreeSectionMemory(sectIdx);
   }
}

// ----------------------------------------------------------------------------
// Initialize configuration with default values (same as in GUI)
//
void RcFile_Init( void )
{
   memset(&mainRc, 0, sizeof(mainRc));
   mainRcInit = TRUE;

   mainRc.acq.acq_mode = ACQ_DFLT_ACQ_MODE;

   mainRc.netacq.netacq_enable = NETACQ_DFLT_ENABLE;
   mainRc.netacq.do_tcp_ip = NETACQ_DFLT_DO_TCP_IP;
   mainRc.netacq.max_conn = NETACQ_DFLT_MAX_CONN;
   mainRc.netacq.fileloglev = NETACQ_DFLT_FILELOGLEV;
   mainRc.netacq.sysloglev = NETACQ_DFLT_SYSLOGLEV;
   mainRc.netacq.pHostName = xstrdup(NETACQ_DFLT_HOSTNAME_STR);
   mainRc.netacq.pPort = xstrdup(NETACQ_DFLT_PORT_STR);

   mainRc.ttx.ttx_chn_count = TTXGRAB_DFLT_CHN_COUNT;
   mainRc.ttx.ttx_start_pg = TTXGRAB_DFLT_START_PG;
   mainRc.ttx.ttx_end_pg = TTXGRAB_DFLT_END_PG;
   mainRc.ttx.ttx_ov_pg = TTXGRAB_DFLT_OV_PG;
   mainRc.ttx.ttx_duration = TTXGRAB_DFLT_DURATION;

   mainRc.tvcard.drv_type = BTDRV_SOURCE_UNDEF;
   mainRc.tvcard.wdm_stop = TRUE;

   mainRc.db.piexpire_cutoff = EPGDB_DFLT_EXPIRE_TIME;
   mainRc.db.auto_merge_ttx = TRUE;
}

