/*
 ##############################################################################
 * PAT/PMT decoder derived from w_scan's scan.c
 *
 * Source code reduction:
 * Copyright 2021 by T. Zoerner (tomzo at users.sf.net)
 *
 ##############################################################################
 * Simple MPEG/DVB parser to achieve network/service information without initial tuning data
 *
 * Copyright (C) 2006 - 2014 Winfried Koehler
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 * Or, point your browser to http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * The author can be reached at: w_scan AT gmx-topmail DOT de
 *
 * The project's page is http://wirbel.htpc-forum.de/w_scan/index2.html
 *
 *  referred standards:
 *    ISO/IEC 13818-1
 *    ETSI EN 300 468 v1.14.1
 *    ETSI TR 101 211
 *    ETSI ETR 211
 *    ITU-T H.222.0 / ISO/IEC 13818-1
 *    http://www.eutelsat.com/satellites/pdf/Diseqc/Reference docs/bus_spec.pdf
 *
 ##############################################################################
 * This is tool is derived from the dvb scan tool,
 * Copyright: Johannes Stezenbach <js@convergence.de> and others, GPLv2 and LGPL
 * (linuxtv-dvb-apps-1.1.0/utils/scan)
 *
 * Differencies:
 * - command line options
 * - detects dvb card automatically
 * - no need for initial tuning data
 * - some adaptions for VDR syntax
 *
 * have phun, wirbel 2006/02/16
 ##############################################################################
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <stdint.h>
#include <errno.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/version.h>
#include <sys/select.h>

#include "dvb_scan_desc.h"
#include "dvb_scan_pmt.h"

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#define false FALSE  // map to nxtvepg version of "bool" type
#define true TRUE

static const char * demux_devname = "/dev/dvb/adapter0/demux0";

#if DEBUG_SWITCH == ON
// used within macro dprintf()
#ifdef DPRINTF_OFF
static int opt_verbosity = 1;  // errors and warnings
#else   // !DPRINTF_OFF
static int opt_verbosity = 9;
#endif
#endif  // DEBUG_SWITCH == ON

/*******************************************************************************
 * double linked list.
 ******************************************************************************/

typedef struct _cItem {
   struct _cItem * prev;
   struct _cItem * next;
   uint32_t index;
   } cItem, * pItem;

typedef struct _cList {
   struct _cItem * first;
   struct _cItem * last;
   uint32_t count;
   } cList, * pList;

/*******************************************************************************
 * section buffer
 ******************************************************************************/

#define SECTION_FLAG_DEFAULT  (1U) << 0
#define SECTION_FLAG_INITIAL  (1U) << 1
#define SECTION_FLAG_FREE     (1U) << 2
#define SECTION_BUF_SIZE      4096

struct section_buf {
  /*----------------------------*/
  struct section_buf * prev;  // pItem
  struct section_buf * next;
  uint32_t index;
  /*----------------------------*/
  const char * dmx_devname;
  unsigned int run_once   : 1;
  unsigned int segmented  : 1;          // segmented by table_id_ext
  int fd;
  int pid;
  int table_id;
  int table_id_ext;
  int section_version_number;
  uint8_t section_done[32];
  int sectionfilter_done;
  unsigned char buf[SECTION_BUF_SIZE];
  uint32_t flags;
  time_t timeout;
  time_t start_time;
  struct section_buf * next_seg;        // this is used to handle segmented tables (like NIT-other)
};

/*******************************************************************************
 * service type.
 ******************************************************************************/

#define AUDIO_CHAN_MAX    (32)

struct service {
  /*----------------------------*/
  struct service *   prev;  // pItem
  struct service *   next;
  uint32_t index;
  /*----------------------------*/
  uint16_t transport_stream_id;
  uint16_t service_id;
  uint16_t pmt_pid;
  uint16_t video_pid;
  uint16_t teletext_pid;
  void   * priv;
};

static cList _current_services, * current_services = &_current_services;

static void setup_filter(struct section_buf * s, const char * dmx_devname, int pid, int table_id, int table_id_ext,
                         int run_once, int segmented, uint32_t filter_flags);
static bool add_filter(struct section_buf * s);


/*******************************************************************************
 * common typedefs && logging.
 ******************************************************************************/

#if DEBUG_SWITCH == ON
#define dprintf(level, fmt...)   \
   do {                          \
      if (level <= opt_verbosity) {  \
         fprintf(stderr, fmt); } \
   } while (0)
#else  /* DEBUG_SWITCH == OFF */
#define dprintf(level, fmt...)   do{}while(0)
#endif

#define dpprintf(level, fmt, args...) \
        dprintf(level, "%s:%d: " fmt, __FUNCTION__, __LINE__ , ##args)

#define fatal(fmt, args...)  do { dpprintf(-1, "FATAL: " fmt , ##args); exit(1); } while(0)
#define error(msg...)         dprintf(0, "\nERROR: " msg)
#define errorn(msg)           dprintf(0, "%s:%d: ERROR: " msg ": %d %s\n", __FUNCTION__, __LINE__, errno, strerror(errno))
#define warning(msg...)       dprintf(1, "WARNING: " msg)
#define info(msg...)          dprintf(2, msg)
#define verbose(msg...)       dprintf(3, msg)
#define moreverbose(msg...)   dprintf(4, msg)
#define debug(msg...)        dpprintf(5, msg)
#define verbosedebug(msg...) dpprintf(6, msg)

/*******************************************************************************
 * double linked list.
 ******************************************************************************/

// initializes a list before first use
static void NewList(pList const list) {
  list->first   = NULL;
  list->last    = NULL;
  list->count   = 0;
}

// append item at end of list.
static void AddItem(pList list, void * item) {
  pItem p = (pItem) item;

  p->index = list->count;
  p->prev  = list->last;
  p->next  = NULL;

  if (list->count == 0) {
     list->first = p;
     }
  else {
     p = (pItem) list->last;
     p->next = (pItem) item;
     }

  list->last = (pItem) item;
  list->count++;
}

// returns true, if a pointer is part of list.
static bool IsMember(pList list, void * item) {
  pItem p;
  for(p = (pItem) list->first; p; p = p->next) {
     if (p == item) {
        return true;
        }
     }
  return false;
}

// remove item from list. free allocated memory if release_mem non-zero.
static void UnlinkItem(pList list, void * item, bool freemem) {
  pItem prev,next,p = (pItem) item;

  if (IsMember(list, item) == false) {
     warning("Cannot %s: item %p is not member of list.\n",
              freemem?"delete":"unlink", item);
     return;
     }
  else if (item == list->first) {
     list->first = p->next;
     list->count--;
     if (freemem) {
        free(p);
        }
     p = list->first;
     if (p != NULL)
        p->prev = NULL;
     while (p != NULL) {
        p->index--;
        p = p->next;
        }
     }
  else if (item == list->last) {
     list->last = p->prev;
     list->count--;
     if (freemem) {
        free(p);
        }
     p = list->last;
     if (p != NULL) {
        p->next = NULL;
        }
     }
  else {
     prev = p->prev;
     next = p->next;
     prev->next = next;
     next->prev = prev;
     list->count--;
     if (freemem) {
        free(p);
        }
     p = next;
     while (p != NULL) {
        p->index--;
        p = p->next;
        }
     }
}

/* service_ids are guaranteed to be unique within one TP
 * (acc. DVB standards unique within one network, but in real life...)
 */
static struct service * alloc_service(uint16_t service_id) {
  struct service * s = (struct service*) calloc(1, sizeof(* s));
  s->service_id = service_id;
  AddItem(current_services, s);
  return s;
}

static struct service * find_service(uint16_t service_id) {
  struct service * s;

  for(s = (struct service*) current_services->first; s; s = s->next) {
     if (s->service_id == service_id)
        return s;
     }
  return NULL;
}

static __u32 crc_table[256];
static __u8  crc_initialized = 0;

static int crc_check (const unsigned char * buf, __u16 len) {
  __u16 i, j;
  __u32 crc = 0xffffffff;
  __u32 transmitted_crc = buf[len-4] << 24 | buf[len-3] << 16 | buf[len-2] << 8 | buf[len-1];

  if (! crc_initialized) { // initialize crc lookup table before first use.
     __u32 accu;
     for(i = 0; i < 256; i++) {
        accu = ((__u32) i << 24);
        for(j = 0; j < 8; j++) {
           if (accu & 0x80000000L)
              accu = (accu << 1) ^ 0x04C11DB7L; // CRC32 Polynom
           else
              accu = (accu << 1);
           }
        crc_table[i] = accu;
        }
     crc_initialized = 1;
     }

  for(i = 0; i < len-4; i++)
     crc = (crc << 8) ^ crc_table[((crc >> 24) ^ *buf++) & 0xFF];

  if (crc == transmitted_crc)
     return 1;
  else {
     warning("received garbage data: crc = 0x%08x; expected crc = 0x%08x\n", crc, transmitted_crc);
     return 0;
     }
}

static int find_descriptor(uint8_t tag, const unsigned char * buf, int descriptors_loop_len, const unsigned char ** desc,
                          int * desc_len) {
  while(descriptors_loop_len > 0) {
     unsigned char descriptor_tag = buf[0];
     unsigned char descriptor_len = buf[1] + 2;

     if (!descriptor_len) {
        warning("descriptor_tag == 0x%02x, len is 0\n", descriptor_tag);
        break;
        }

     if (tag == descriptor_tag) {
        if (desc)
           *desc = buf;
        if (desc_len)
           *desc_len = descriptor_len;
        return 1;
        }

     buf                  += descriptor_len;
     descriptors_loop_len -= descriptor_len;
  }
  return 0;
}

static void parse_descriptors(enum table_id t, const unsigned char * buf, int descriptors_loop_len, void *data) {
  while(descriptors_loop_len > 0) {
     unsigned char descriptor_tag = buf[0];
     unsigned char descriptor_len = buf[1] + 2;

     if (descriptor_len == 0) {
        debug("descriptor_tag == 0x%02x, len is 0\n", descriptor_tag);
        break;
        }

     switch(descriptor_tag) {
        case MHP_application_descriptor:
        case MHP_application_name_desriptor:
        case MHP_transport_protocol_descriptor:
        case dvb_j_application_descriptor:
        case dvb_j_application_location_descriptor:
                break;
        case ca_descriptor: /* 20080106 */
                //if (t == TABLE_PMT)
                //   parse_ca_descriptor(buf, data);
                break;
        case iso_639_language_descriptor:
                //if (t == TABLE_PMT)
                //   parse_iso639_language_descriptor(buf, data);
                break;
        case application_icons_descriptor:
        case carousel_identifier_descriptor:
                break;
        case network_name_descriptor:
                break;
        case service_list_descriptor:
        case stuffing_descriptor:
                break;
        case satellite_delivery_system_descriptor:
                break;
        case cable_delivery_system_descriptor:
                break;
        case vbi_data_descriptor:
        case vbi_teletext_descriptor:
                // TODO?
                break;
        case bouquet_name_descriptor:
                break;
        case service_descriptor:
                //if ((t == TABLE_SDT_ACT) || (t == TABLE_SDT_OTH))
                //   parse_service_descriptor(buf, data, flags.codepage);
                break;
        case country_availability_descriptor:
        case linkage_descriptor:
        case nvod_reference_descriptor:
        case time_shifted_service_descriptor:
        case short_event_descriptor:
        case extended_event_descriptor:
        case time_shifted_event_descriptor:
        case component_descriptor:
        case mosaic_descriptor:
        case stream_identifier_descriptor:
                break;
        case ca_identifier_descriptor:
                break;
        case content_descriptor:
        case parental_rating_descriptor:
        case teletext_descriptor:
        case telephone_descriptor:
        case local_time_offset_descriptor:
        case subtitling_descriptor:
                //parse_subtitling_descriptor(buf, data);
                break;
        case terrestrial_delivery_system_descriptor:
                break;
        case extension_descriptor: // 6.2.16 Extension descriptor
                break;
        case multilingual_network_name_descriptor:
        case multilingual_bouquet_name_descriptor:
        case multilingual_service_name_descriptor:
        case multilingual_component_descriptor:
        case private_data_specifier_descriptor:
        case service_move_descriptor:
        case short_smoothing_buffer_descriptor:
                break;
        case frequency_list_descriptor:
                break;
        case partial_transport_stream_descriptor:
        case data_broadcast_descriptor:
        case scrambling_descriptor:
        case data_broadcast_id_descriptor:
        case transport_stream_descriptor:
        case dsng_descriptor:
        case pdc_descriptor:
        case ac3_descriptor:
        case ancillary_data_descriptor:
        case cell_list_descriptor:
        case cell_frequency_link_descriptor:
        case announcement_support_descriptor:
        case application_signalling_descriptor:
        case service_identifier_descriptor:
        case service_availability_descriptor:
        case default_authority_descriptor:
        case related_content_descriptor:
        case tva_id_descriptor:
        case content_identifier_descriptor:
        case time_slice_fec_identifier_descriptor:
        case ecm_repetition_rate_descriptor:
                break;
        case s2_satellite_delivery_system_descriptor:
                break;
        case enhanced_ac3_descriptor:
        case dts_descriptor:
        case aac_descriptor:
                break;
        case logical_channel_descriptor:
                break;
        case 0xF2: // 0xF2 Private DVB Descriptor  Premiere.de, Content Transmission Descriptor
                break;
        default:
                verbosedebug("skip descriptor 0x%02x\n", descriptor_tag);
        }

     buf += descriptor_len;
     descriptors_loop_len -= descriptor_len;
     }
}

/* EN 13818-1 p.43 Table 2-25 - Program association section
 */
static void parse_pat(const unsigned char * buf, uint16_t section_length, uint16_t transport_stream_id, uint32_t flags) {
  verbose("PAT (xxxx:xxxx:%u)\n", transport_stream_id);

  while(section_length > 0) {
     struct service * s;
     uint16_t service_id     =  (buf[0] << 8) | buf[1];
     uint16_t program_number = ((buf[2] & 0x1f) << 8) | buf[3];
     buf            += 4;
     section_length -= 4;

     if (service_id == 0) {
        if (program_number != 16)
           info("        %s: network_PID = %d (transport_stream_id %d)\n", __FUNCTION__, program_number, transport_stream_id);
        //network_PID = program_number;
        continue;
        }
     // SDT might have been parsed first...
     s = find_service(service_id);
     //if (s == NULL)
     //   s = alloc_service(service_id);
     if (s != NULL) {
       s->pmt_pid = program_number;

       if (! (flags & SECTION_FLAG_INITIAL)) {
          if (s->priv == NULL) { //  && s->pmt_pid) |  pmt_pid is by spec: 0x0010 .. 0x1FFE . see EN13818-1 p.19 Table 2-3 - PID table
             s->priv = calloc(1, sizeof(struct section_buf));
             setup_filter((struct section_buf*) s->priv, demux_devname, s->pmt_pid, TABLE_PMT, -1, 1, 0, SECTION_FLAG_FREE);
             add_filter((struct section_buf*) s->priv);
             }
          }
       }
    }
}

static void parse_pmt(const unsigned char * buf, uint16_t section_length, uint16_t service_id) {
  int program_info_len;
  struct service * s;

  s = find_service(service_id);
  if (s == NULL) {
     error("PMT for service_id 0x%04x was not in PAT\n", service_id);
     return;
     }

  //s->pcr_pid = ((buf[0] & 0x1f) << 8) | buf[1];
  program_info_len = ((buf[2] & 0x0f) << 8) | buf[3];

  // 20080106, search PMT program info for CA Ids
  buf +=4;
  section_length -= 4;

  while(program_info_len > 0) {
     int descriptor_length = ((int)buf[1]) + 2;
     parse_descriptors(TABLE_PMT, buf, section_length, s);
     buf += descriptor_length;
     section_length   -= descriptor_length;
     program_info_len -= descriptor_length;
     }

  while(section_length > 0) {
     int ES_info_len = ((buf[3] & 0x0f) << 8) | buf[4];
     int elementary_pid = ((buf[1] & 0x1f) << 8) | buf[2];

     switch(buf[0]) { // stream type
        case iso_iec_11172_video_stream:
        case iso_iec_13818_1_11172_2_video_stream:
           moreverbose("  VIDEO     : PID %d (stream type 0x%x)\n", elementary_pid, buf[0]);
           if (s->video_pid == 0) {
              s->video_pid = elementary_pid;
              }
           break;
        case iso_iec_11172_audio_stream:
        case iso_iec_13818_3_audio_stream:
           moreverbose("  AUDIO     : PID %d (stream type 0x%x)\n", elementary_pid, buf[0]);
           break;
        case iso_iec_13818_1_private_sections:
        case iso_iec_13818_1_private_data:
           // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets containing private data
           if (find_descriptor(teletext_descriptor, buf + 5, ES_info_len, NULL, NULL)) {
              moreverbose("  TELETEXT  : PID %d\n", elementary_pid);
              s->teletext_pid = elementary_pid;
              break;
              }
           else if (find_descriptor(subtitling_descriptor, buf + 5, ES_info_len, NULL, NULL)) {
              // Note: The subtitling descriptor can also signal
              // teletext subtitling, but then the teletext descriptor
              // will also be present; so we can be quite confident
              // that we catch DVB subtitling streams only here, w/o
              // parsing the descriptor.
              moreverbose("  SUBTITLING: PID %d\n", elementary_pid);
              break;
              }
           // we shouldn't reach this one, usually it should be Teletext, Subtitling or AC3 ..
           moreverbose("  unknown private data: PID 0x%04x\n", elementary_pid);
           break;
        case iso_iec_13522_MHEG:
           //
           //MHEG-5, or ISO/IEC 13522-5, is part of a set of international standards relating to the
           //presentation of multimedia information, standardized by the Multimedia and Hypermedia Experts Group (MHEG).
           //It is most commonly used as a language to describe interactive television services.
           moreverbose("  MHEG      : PID %d\n", elementary_pid);
           break;
        case iso_iec_13818_1_Annex_A_DSM_CC:
           moreverbose("  DSM CC    : PID %d\n", elementary_pid);
           break;
        case iso_iec_13818_1_11172_1_auxiliary:
           moreverbose("  ITU-T Rec. H.222.0 | ISO/IEC 13818-1/11172-1 auxiliary : PID %d\n", elementary_pid);
           break;
        case iso_iec_13818_6_type_a_multiproto_encaps:
           moreverbose("  ISO/IEC 13818-6 Multiprotocol encapsulation    : PID %d\n", elementary_pid);
           break;
        case iso_iec_13818_6_type_b:
           // Digital storage media command and control (DSM-CC) is a toolkit for control channels associated
           // with MPEG-1 and MPEG-2 streams. It is defined in part 6 of the MPEG-2 standard (Extensions for DSM-CC).
           // DSM-CC may be used for controlling the video reception, providing features normally found
           // on VCR (fast-forward, rewind, pause, etc). It may also be used for a wide variety of other purposes
           // including packet data transport. MPEG-2 ISO/IEC 13818-6 (part 6 of the MPEG-2 standard).
           //
           // DSM-CC defines or extends five distinct protocols:
           //  * User-User
           //  * User-Network
           //  * MPEG transport profiles (profiles to the standard MPEG transport protocol ISO/IEC 13818-1 to allow
           //          transmission of event, synchronization, download, and other information in the MPEG transport stream)
           //  * Download
           //  * Switched Digital Broadcast-Channel Change Protocol (SDB/CCP)
           //         Enables a client to remotely switch from channel to channel in a broadcast environment.
           //         Used to attach a client to a continuous-feed session (CFS) or other broadcast feed. Sometimes used in pay-per-view.
           //
           moreverbose("  DSM-CC U-N Messages : PID %d\n", elementary_pid);
           break;
        case iso_iec_13818_6_type_c://DSM-CC Stream Descriptors
           moreverbose("  ISO/IEC 13818-6 Stream Descriptors : PID %d\n", elementary_pid);
           break;
        case iso_iec_13818_6_type_d://DSM-CC Sections (any type, including private data)
           moreverbose("  ISO/IEC 13818-6 Sections (any type, including private data) : PID %d\n", elementary_pid);
           break;
        case iso_iec_13818_1_auxiliary:
           moreverbose("  ISO/IEC 13818-1 auxiliary : PID %d\n", elementary_pid);
           break;
        case iso_iec_13818_7_audio_w_ADTS_transp:
           moreverbose("  ADTS Audio Stream (usually AAC) : PID %d (stream type 0x%x)\n", elementary_pid, buf[0]);
           break;
        case iso_iec_14496_2_visual:
           moreverbose("  ISO/IEC 14496-2 Visual : PID %d\n", elementary_pid);
           break;
        case iso_iec_14496_3_audio_w_LATM_transp:
           moreverbose("  ISO/IEC 14496-3 Audio with LATM transport syntax as def. in ISO/IEC 14496-3/AMD1 : PID %d (stream type 0x%x)\n", elementary_pid, buf[0]);
           break;
        case iso_iec_14496_1_packet_stream_in_PES:
           moreverbose("  ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in PES packets : PID 0x%04x\n", elementary_pid);
           break;
        case iso_iec_14496_1_packet_stream_in_14996:
           moreverbose("  ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in ISO/IEC 14496 sections : PID 0x%04x\n", elementary_pid);
           break;
        case iso_iec_13818_6_synced_download_protocol:
           moreverbose("  ISO/IEC 13818-6 DSM-CC synchronized download protocol : PID 0x%04x\n", elementary_pid);
           break;
        case metadata_in_PES:
           moreverbose("  Metadata carried in PES packets using the Metadata Access Unit Wrapper : PID 0x%04x\n", elementary_pid);
           break;
        case metadata_in_metadata_sections:
           moreverbose("  Metadata carried in metadata_sections : PID 0x%04x\n", elementary_pid);
           break;
        case metadata_in_iso_iec_13818_6_data_carous:
           moreverbose("  Metadata carried in ISO/IEC 13818-6 (DSM-CC) Data Carousel : PID 0x%04x\n", elementary_pid);
           break;
        case metadata_in_iso_iec_13818_6_obj_carous:
           moreverbose("  Metadata carried in ISO/IEC 13818-6 (DSM-CC) Object Carousel : PID 0x%04x\n", elementary_pid);
           break;
        case metadata_in_iso_iec_13818_6_synced_dl:
           moreverbose("  Metadata carried in ISO/IEC 13818-6 Synchronized Download Protocol using the Metadata Access Unit Wrapper : PID 0x%04x\n", elementary_pid);
           break;
        case iso_iec_13818_11_IPMP_stream:
           moreverbose("  IPMP stream (defined in ISO/IEC 13818-11, MPEG-2 IPMP) : PID 0x%04x\n", elementary_pid);
           break;
        case iso_iec_14496_10_AVC_video_stream:
           moreverbose("  AVC Video stream, ITU-T Rec. H.264 | ISO/IEC 14496-10 : PID %d (stream type 0x%x)\n", elementary_pid, buf[0]);
           if (s->video_pid == 0) {
              s->video_pid = elementary_pid;
              }
           break;
        case iso_iec_23008_2_H265_video_hevc_stream:
           moreverbose("  HEVC Video stream, ITU-T Rec. H.265 | ISO/IEC 23008-1 : PID %d (stream type 0x%x)\n", elementary_pid, buf[0]);
           if (s->video_pid == 0) {
              s->video_pid = elementary_pid;
              }
           break;
        case atsc_a_52b_ac3:
           moreverbose("  AC-3 Audio per ATSC A/52B : PID %d (stream type 0x%x)\n", elementary_pid, buf[0]);
           break;
        default:
           moreverbose("  OTHER     : PID %d TYPE 0x%02x\n", elementary_pid, buf[0]);
        } //END switch stream type
     buf            += ES_info_len + 5;
     section_length -= ES_info_len + 5;
     }


  debug("tsid=%d sid=%d: pmt_pid 0x%04x, vpid 0x%04x\n",
        s->transport_stream_id,
        s->service_id,
        s->pmt_pid, s->video_pid);
}


static int get_bit(uint8_t *bitfield, int bit) {
  return (bitfield[bit/8] >> (bit % 8)) & 1;
}

static void set_bit(uint8_t *bitfield, int bit) {
  bitfield[bit/8] |= 1 << (bit % 8);
}


/*   returns 0 when more sections are expected
 *           1 when all sections are read on this pid
 *          -1 on invalid table id
 */
static int parse_section(struct section_buf * s) {
  const unsigned char * buf = s->buf;
  uint8_t  table_id;
//uint8_t  section_syntax_indicator;
  uint16_t section_length;                                        // 12bit: 0..4095
  uint16_t table_id_ext;
  uint8_t  section_version_number;
//uint8_t  current_next_indicator;
  uint8_t  section_number;
  uint8_t  last_section_number;
//int pcr_pid;
//int program_info_length;
  int i;

  table_id = buf[0];
  if (s->table_id != table_id)
     return -1;
//section_syntax_indicator = buf[1] & 0x80;
  section_length = (((buf[1] & 0x0f) << 8) | buf[2]) - 9;         // skip 9bytes: 5byte header + 4byte CRC32

  if (! crc_check(&buf[0],section_length+12)) {
     int slow_rep_rate = 30;
     if (s->timeout < slow_rep_rate) {
        info("increasing filter timeout to %d secs (pid:%d table_id:%d table_id_ext:%d).\n",
             slow_rep_rate,s->pid,s->table_id, s->table_id_ext);
        s->timeout = slow_rep_rate;
        }

     return 0;
     }

  table_id_ext = (buf[3] << 8) | buf[4];                          // p.program_number
  section_version_number = (buf[5] >> 1) & 0x1f;                  // p.version_number = getBits (b, 0, 42, 5); -> 40 + 1 -> 5 bit weit? -> version_number = buf[5] & 0x3e;
//current_next_indicator = buf[5] & 0x01;
  section_number = buf[6];
  last_section_number = buf[7];
//pcr_pid = ((buf[8] & 0x1f) << 8) | buf[9];
//program_info_length = ((buf[10] & 0x0f) << 8) | buf[11];

  if (s->segmented && s->table_id_ext != -1 && s->table_id_ext != table_id_ext) {
     /* find or allocate actual section_buf matching table_id_ext */
     while (s->next_seg) {
        s = s->next_seg;
        if (s->table_id_ext == table_id_ext)
           break;
        }
     if (s->table_id_ext != table_id_ext) {
        assert(s->next_seg == NULL);
        s->next_seg = (struct section_buf*) calloc(1, sizeof(struct section_buf));
        s->next_seg->segmented = s->segmented;
        s->next_seg->run_once = s->run_once;
        s->next_seg->timeout = s->timeout;
        s = s->next_seg;
        s->table_id = table_id;
        s->table_id_ext = table_id_ext;
        s->section_version_number = section_version_number;
        }
     }

  if (s->section_version_number != section_version_number || s->table_id_ext != table_id_ext) {
     struct section_buf *next_seg = s->next_seg;

     if (s->section_version_number != -1 && s->table_id_ext != -1)
        debug("section version_number or table_id_ext changed "
              "%d -> %d / %04x -> %04x\n",
              s->section_version_number, section_version_number,
              s->table_id_ext, table_id_ext);
     s->table_id_ext = table_id_ext;
     s->section_version_number = section_version_number;
     s->sectionfilter_done = 0;
     memset(s->section_done, 0, sizeof(s->section_done));
     s->next_seg = next_seg;
     }

  buf += 8;

  if (!get_bit(s->section_done, section_number)) {
     set_bit(s->section_done, section_number);

     verbosedebug("pid %d (0x%02x), tid %d (0x%02x), table_id_ext %d (0x%04x), "
         "section_number %i, last_section_number %i, version %i\n",
         s->pid, s->pid,
         table_id, table_id,
         table_id_ext, table_id_ext, section_number,
         last_section_number, section_version_number);

     switch(table_id) {
     case TABLE_PAT:
        //verbose("PAT for transport_stream_id %d (0x%04x)\n", table_id_ext, table_id_ext);
        parse_pat(buf, section_length, table_id_ext, s->flags);
        break;
     case TABLE_PMT:
        verbose("PMT %d (0x%04x) for service %d (0x%04x)\n", s->pid, s->pid, table_id_ext, table_id_ext);
        parse_pmt(buf, section_length, table_id_ext);
        break;
     case TABLE_NIT_ACT:
     case TABLE_NIT_OTH:
        //verbose("NIT(%s TS, network_id %d (0x%04x) )\n", table_id == 0x40 ? "actual":"other",
        //       table_id_ext, table_id_ext);
        //parse_nit(buf, section_length, table_id, table_id_ext, s->flags);
        break;
     case TABLE_SDT_ACT:
     case TABLE_SDT_OTH:
        verbose("SDT(%s TS, transport_stream_id %d (0x%04x) )\n", table_id == 0x42 ? "actual":"other",
               table_id_ext, table_id_ext);
        //parse_sdt(buf, section_length, table_id_ext);
        break;
     case TABLE_VCT_TERR:
     case TABLE_VCT_CABLE:
        verbose("ATSC VCT, table_id %d, table_id_ext %d\n", table_id, table_id_ext);
        //parse_psip_vct(buf, section_length, table_id, table_id_ext);
        break;
     default:;
     }

     for(i = 0; i <= last_section_number; i++)
        if (get_bit(s->section_done, i) == 0)
           break;

     if (i > last_section_number)
        s->sectionfilter_done = 1;
  }

  if (s->segmented) {
     /* always wait for timeout; this is because we don't now how
      * many segments there are
      */
     return 0;
     }
  else if (s->sectionfilter_done)
     return 1;

  return 0;
}


static int read_sections(struct section_buf * s) {
  int section_length, count;

  if (s->sectionfilter_done && !s->segmented)
     return 1;

  /* the section filter API guarantess that we get one full section
   * per read(), provided that the buffer is large enough (it is)
   */
  if (((count = read(s->fd, s->buf, sizeof(s->buf))) < 0) && errno == EOVERFLOW)
     count = read(s->fd, s->buf, sizeof(s->buf));
  if (count < 0) {
     fprintf(stderr, "read error: (count < 0)");
     return -1;
     }

  if (count < 4)
     return -1;

  section_length = ((s->buf[1] & 0x0f) << 8) | s->buf[2];

  if (count != section_length + 3)
     return -1;

  if (parse_section(s) == 1)
     return 1;

  return 0;
}


static cList _running_filters, * running_filters = &_running_filters;
static int n_running;

// see http://www.linuxtv.org/pipermail/linux-dvb/2005-October/005577.html:
// #define MAX_RUNNING 32
#define MAX_RUNNING 27

static struct pollfd poll_fds[MAX_RUNNING];
static struct section_buf * poll_section_bufs[MAX_RUNNING];


static void setup_filter(struct section_buf * s, const char * dmx_devname,
                          int pid, int table_id, int table_id_ext,
                          int run_once, int segmented, uint32_t filter_flags) {
  memset(s, 0, sizeof(struct section_buf));

  s->fd = -1;
  s->dmx_devname = dmx_devname;
  s->pid = pid;
  s->table_id = table_id;
  s->flags = filter_flags;

  s->run_once = run_once;
  s->segmented = segmented;
  s->timeout = 2; // add 1sec for safety..

  s->table_id_ext = table_id_ext;
  s->section_version_number = -1;
  s->next = 0;
  s->prev = 0;
}

static void update_poll_fds(void) {
  struct section_buf * s;
  int i;

  memset(poll_section_bufs, 0, sizeof(poll_section_bufs));
  for(i = 0; i < MAX_RUNNING; i++)
     poll_fds[i].fd = -1;
  i = 0;
  for(s = (struct section_buf*) running_filters->first; s; s = s->next) {
     if (i >= MAX_RUNNING)
        fatal("too many poll_fds\n");
     if (s->fd == -1)
        fatal("s->fd == -1 on running_filters\n");
     verbosedebug("poll fd %d\n", s->fd);
     poll_fds[i].fd = s->fd;
     poll_fds[i].events = POLLIN;
     poll_fds[i].revents = 0;
     poll_section_bufs[i] = s;
     i++;
     }
  if (i != n_running)
     fatal("n_running is hosed\n");
}

static int start_filter(struct section_buf * s) {
  struct dmx_sct_filter_params f;

  if (n_running >= MAX_RUNNING) {
     fprintf(stderr, "ERROR: too many filters.\n");
     goto err0;
     }
  if ((s->fd = open(s->dmx_devname, O_RDWR)) < 0) {
     fprintf(stderr, "ERROR: could not open demux %s: %s\n", s->dmx_devname, strerror(errno));
     goto err0;
     }

  verbosedebug("%s pid %d (0x%04x) table_id 0x%02x\n",
               __FUNCTION__, s->pid, s->pid, s->table_id);

  memset(&f, 0, sizeof(f));
  f.pid = (uint16_t) s->pid;

  if (s->table_id < 0x100 && s->table_id > 0) {
     f.filter.filter[0] = (uint8_t) s->table_id;
     f.filter.mask[0]   = 0xff;
     }

  f.timeout = 0;
  f.flags = DMX_IMMEDIATE_START;

  if (ioctl(s->fd, DMX_SET_FILTER, &f) == -1) {
     fprintf(stderr, "ioctl DMX_SET_FILTER failed");
     goto err1;
     }

  s->sectionfilter_done = 0;
  time(&s->start_time);

  AddItem(running_filters, s);

  n_running++;
  update_poll_fds();

  return 0;

  err1:
     ioctl(s->fd, DMX_STOP);
     close(s->fd);
  err0:
     s->fd = -1;
     return -1;
}


static void stop_filter(struct section_buf * s) {
  verbosedebug("%s: pid %d (0x%04x)\n", __FUNCTION__,s->pid,s->pid);

  ioctl(s->fd, DMX_STOP);
  close(s->fd);

  s->fd = -1;
  UnlinkItem(running_filters, s, false);

  n_running--;
  update_poll_fds();
}


static bool add_filter(struct section_buf * s) {
  verbosedebug("%s %d: pid=%d (0x%04x), s=%p\n",
     __FUNCTION__,__LINE__,s->pid, s->pid, s);
  return (start_filter(s) == 0);
}


static void remove_filter(struct section_buf * s) {
  verbosedebug("%s: pid %d (0x%04x)\n",__FUNCTION__,s->pid,s->pid);
  stop_filter(s);

  if (s->flags & SECTION_FLAG_FREE) {
     free(s);
     s = NULL;
     }

  if (running_filters->count > (MAX_RUNNING - 1)) // maximum num of filters reached.
     return;
}

/* return value:
 * non-zero on success.
 * zero on timeout.
 */
static int read_filters(void) {
  struct section_buf * s;
  int i, done = 0;

  //n = poll(poll_fds, n_running, 25);
  //if (n == -1)
  //   fprintf(stderr, "poll");

  for(i = 0; i < n_running; i++) {
     s = poll_section_bufs[i];
     if (!s)
        fatal("poll_section_bufs[%d] is NULL\n", i);
     if (poll_fds[i].revents)
        done = read_sections(s) == 1;
     else
        done = 0; /* timeout */
     if (done || time(NULL) > s->start_time + s->timeout) {
        if (s->run_once) {
           if (done)
              verbosedebug("filter success: pid 0x%04x\n", s->pid);
           else {
#if DEBUG_SWITCH == ON
              const char * intro = "        Info: no data from ";
              // timeout waiting for data.
              switch(s->table_id) {
                 case TABLE_PAT:       info   ("%sPAT after %lld seconds\n",         intro, (long long) s->timeout); break;
                 case TABLE_PMT:       info   ("%sPMT after %lld seconds\n",         intro, (long long) s->timeout); break;
                 default:              info   ("%spid %u after %lld seconds\n",      intro, s->pid, (long long) s->timeout);
                 }
#endif
             }
           remove_filter(s);
           }
        }
     }
  return done;
}

// ----------------------------------------------------------------------------
// Start reading PAT & PMT for a given list of service IDs
// - using static storage, so only one instance of the scan may be started at a time
//
int DvbScanPmt_Start(const char * pDemuxDev, const int * srv, int srvCnt)
{
  struct section_buf * s;

  assert(n_running == 0);
  demux_devname = pDemuxDev;

  NewList(running_filters);
  NewList(current_services);

  for(int idx = 0; idx < srvCnt; ++idx) {
    alloc_service(srv[idx]);
  }

  s = (struct section_buf*) calloc(1, sizeof(struct section_buf));
  setup_filter(s, demux_devname, PID_PAT, TABLE_PAT, -1, 1, 0, SECTION_FLAG_FREE);
  if (add_filter(s) == false) {
    DvbScanPmt_Stop();
    srvCnt = 0;
  }

  return srvCnt;
}

// ----------------------------------------------------------------------------
// Retrieve results after end of scan
// - may be called only after "ProcessFds" indicated end of scan
// - fills in "ttxPid" to entries in the given list with matching service ID
// - frees internally allocated resources
//
int DvbScanPmt_GetResults(T_DVB_SCAN_PMT * srv, int srvCnt)
{
  struct service * s;
  int result = 0;

  while (current_services->first) {
    s = (struct service*) current_services->first;
    //printf("%d: video:%d teletext:%d\n", s->service_id, s->video_pid, s->teletext_pid);

    for(int idx = 0; idx < srvCnt; ++idx) {
      if (srv[idx].serviceId == s->service_id) {
        srv[idx].ttxPid = s->teletext_pid;
        result += 1;
      }
    }
    UnlinkItem(current_services, s, true);
  }
  assert(n_running == 0);

  return result;
}

// ----------------------------------------------------------------------------
// Abort an ongoing scan & free resources
//
void DvbScanPmt_Stop(void)
{
  // close devices & free memory
  while (running_filters->first) {
    stop_filter((struct section_buf*) running_filters->first);
  }
  assert(n_running == 0);

  // free memory
  while (current_services->first) {
    UnlinkItem(current_services, current_services->first, true);
  }
}

// ----------------------------------------------------------------------------
// Retrieve list of file descriptors foe use in select()
//
int DvbScanPmt_GetFds(fd_set *set, int max_fd)
{
  struct section_buf * s;

  for(s = (struct section_buf*) running_filters->first; s; s = s->next) {
     if (s->fd != -1) {
       FD_SET(s->fd, set);
       if (s->fd > max_fd) {
         max_fd = s->fd;
       }
     }
  }
  return max_fd;
}

// ----------------------------------------------------------------------------
// Process readable file descriptors indicated by select()
// - returns 1 if the scan is finished and results are ready
//
int DvbScanPmt_ProcessFds(fd_set *set)
{
  struct section_buf * s;
  int n_ev = 0;
  int i = 0;

  for(s = (struct section_buf*) running_filters->first; s; s = s->next) {
     if ((s->fd != -1) && FD_ISSET(s->fd, set)) {
       poll_fds[i].revents = POLLIN;
       n_ev++;
     }
     else {
       poll_fds[i].revents = 0;
     }
  }
  if (n_ev) {
    read_filters();
  }
  return (n_running == 0);
}
