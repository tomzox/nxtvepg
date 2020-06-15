/*
 * Teletext EPG grabber: C external interfaces
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2006-2011,2020 by T. Zoerner (tomzo at users.sf.net)
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <assert.h>

#include <string>
#include <map>
#include <vector>
#include <regex>

using namespace std;

#include "epgctl/epgversion.h"

#include "ttx_db.h"
#include "ttx_scrape.h"
#include "ttx_xmltv.h"
#include "ttx_cif.h"

/**
 * This function must be called once during start-up and after each channel
 * change to initialize the database for teletext acquisition.
 */
void ttx_db_init( void )
{
   ttx_db.flush();
   ttx_chn_id.flush();
}

/**
 * This function is called after capturing a packet 8/30 or VPS and extracting
 * the enclosed CNI (channel identification code.)
 */
void ttx_db_add_cni(unsigned cni)
{
   ttx_chn_id.add_cni(cni);
}

/**
 * This function adds the given teletext packet to the database. The function
 * should be called for each captured packet, filtering is done internally.
 */
bool ttx_db_add_pkg( int page, int ctrl, int pkgno, const uint8_t * p_data, time_t ts )
{
   static int cur_page = -1;
   static int cur_sub = -1;

   if (page < 0x100)
      page += 0x800;

   if (ttx_db.page_acceptable(page))
   {
      if (pkgno == 0)
      {
         cur_page = page;
         cur_sub = ctrl & 0x3F7F;
         ttx_db.add_page(cur_page, cur_sub, ctrl, p_data, ts);
      }
      else
      {
         ttx_db.add_page_data(cur_page, cur_sub, pkgno, p_data);
      }
   }
}

/**
 * This function is called once when acquisition is complete to start grabbing
 * EPG data from the previously captured teletext packets. The result is written
 * to the given file.
 */
int ttx_db_parse( int pg_start, int pg_end, int expire_min,
                  const char * p_xml_in, const char * p_xml_out,
                  const char * p_ch_name, const char * p_ch_id )
{
   int result = 0;

   // parse and export programme data
   // grab new XML data from teletext
   vector<OV_PAGE*> ov_pages = ParseAllOvPages(pg_start, pg_end);

   ParseAllContent(ov_pages);

   // retrieve descriptions from references teletext pages
   list<TV_SLOT> NewSlots = OV_PAGE::get_ov_slots(ov_pages);

   // remove programmes beyond the expiration threshold
   FilterExpiredSlots(NewSlots, expire_min);

   // make sure to never write an empty file
   if (!NewSlots.empty()) {
      XMLTV xmltv;

      xmltv.SetChannelName(p_ch_name, p_ch_id);

      xmltv.SetExpireTime(expire_min);

      // read and merge old data from XMLTV file
      if (p_xml_in != 0) {
         xmltv.ImportXmltvFile(p_xml_in);
      }

      xmltv.ExportXmltv(NewSlots, p_xml_out,
                        "nxtvepg/" EPG_VERSION_STR, NXTVEPG_URL);
   }
   else {
      // return error code to signal abormal termination
      result = 100;
   }

   for (unsigned idx = 0; idx < ov_pages.size(); idx++) {
      delete ov_pages[idx];
   }

   return result;
}

/**
 * This function may be called after acquisition to dump all captured teletext
 * pages in the given page number range to the given file.
 */
void ttx_db_dump(const char * p_name, int pg_start, int pg_end)
{
   DumpRawTeletext(p_name, pg_start, pg_end);
}

