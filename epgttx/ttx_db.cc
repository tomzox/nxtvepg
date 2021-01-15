/*
 * Teletext EPG grabber: Teletext page database
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

#include <string>
#include <algorithm>
#include <regex>

#if defined (USE_LIBZVBI)
#include "libzvbi.h"
#endif

using namespace std;

#include "ttx_util.h"
#include "ttx_db.h"

#if !defined (USE_LIBZVBI)
const uint8_t unhamtab[256] =
{
   0x01, 0xff, 0x01, 0x01, 0xff, 0x00, 0x01, 0xff,
   0xff, 0x02, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x07,
   0xff, 0x00, 0x01, 0xff, 0x00, 0x00, 0xff, 0x00,
   0x06, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x03, 0xff,
   0xff, 0x0c, 0x01, 0xff, 0x04, 0xff, 0xff, 0x07,
   0x06, 0xff, 0xff, 0x07, 0xff, 0x07, 0x07, 0x07,
   0x06, 0xff, 0xff, 0x05, 0xff, 0x00, 0x0d, 0xff,
   0x06, 0x06, 0x06, 0xff, 0x06, 0xff, 0xff, 0x07,
   0xff, 0x02, 0x01, 0xff, 0x04, 0xff, 0xff, 0x09,
   0x02, 0x02, 0xff, 0x02, 0xff, 0x02, 0x03, 0xff,
   0x08, 0xff, 0xff, 0x05, 0xff, 0x00, 0x03, 0xff,
   0xff, 0x02, 0x03, 0xff, 0x03, 0xff, 0x03, 0x03,
   0x04, 0xff, 0xff, 0x05, 0x04, 0x04, 0x04, 0xff,
   0xff, 0x02, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x07,
   0xff, 0x05, 0x05, 0x05, 0x04, 0xff, 0xff, 0x05,
   0x06, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x03, 0xff,
   0xff, 0x0c, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x09,
   0x0a, 0xff, 0xff, 0x0b, 0x0a, 0x0a, 0x0a, 0xff,
   0x08, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x0d, 0xff,
   0xff, 0x0b, 0x0b, 0x0b, 0x0a, 0xff, 0xff, 0x0b,
   0x0c, 0x0c, 0xff, 0x0c, 0xff, 0x0c, 0x0d, 0xff,
   0xff, 0x0c, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x07,
   0xff, 0x0c, 0x0d, 0xff, 0x0d, 0xff, 0x0d, 0x0d,
   0x06, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x0d, 0xff,
   0x08, 0xff, 0xff, 0x09, 0xff, 0x09, 0x09, 0x09,
   0xff, 0x02, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x09,
   0x08, 0x08, 0x08, 0xff, 0x08, 0xff, 0xff, 0x09,
   0x08, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x03, 0xff,
   0xff, 0x0c, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x09,
   0x0f, 0xff, 0x0f, 0x0f, 0xff, 0x0e, 0x0f, 0xff,
   0x08, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x0d, 0xff,
   0xff, 0x0e, 0x0f, 0xff, 0x0e, 0x0e, 0xff, 0x0e
};

inline int vbi_unham8(unsigned int c)
{
   return (signed char)unhamtab[(uint8_t)c];
}
#endif // USE_LIBZVBI

TTX_DB_PAGE::TTX_DB_PAGE(unsigned page, unsigned sub, unsigned ctrl, time_t ts)
{
   m_page = page;
   m_sub = sub;
   m_raw_pkg_valid = 0;
   m_acq_rep_cnt = 1;
   m_text_valid = false;
   m_timestamp = ts;

   // store G0 char set (bits must be reversed: C12,C13,C14)
   int ctl2 = ctrl >> 16;
   m_lang = ((ctl2 >> 7) & 1) | ((ctl2 >> 5) & 2) | ((ctl2 >> 3) & 4);
}

void TTX_DB_PAGE::add_raw_pkg(unsigned idx, const uint8_t * p_data)
{
   assert(idx < TTX_RAW_PKG_CNT);

   if (idx == 0) {
      memset(m_raw_pkg[idx], ' ', 8);
      memcpy(m_raw_pkg[idx] + 8, p_data + 8, VT_PKG_RAW_LEN - 8);
   }
   else {
      memcpy(m_raw_pkg[idx], p_data, VT_PKG_RAW_LEN);
   }
   m_raw_pkg_valid |= 1 << idx;
}

void TTX_DB_PAGE::inc_acq_cnt(time_t ts)
{
   m_acq_rep_cnt += 1;
   m_timestamp = ts;
}

void TTX_DB_PAGE::erase_page_c4()
{
   m_raw_pkg_valid = 1;
   m_text_valid = false;
}

void TTX_DB_PAGE::page_to_latin1() const // modifies mutable
{
   assert(!m_text_valid); // checked by caller for efficiency

   for (unsigned idx = 0; idx < TTX_TEXT_LINE_CNT; idx++) {

      if (m_raw_pkg_valid & (1 << idx)) {
         m_ctrl[idx].assign(VT_PKG_RAW_LEN, ' ');
         line_to_latin1(idx, m_ctrl[idx]);

         m_text[idx].assign(VT_PKG_RAW_LEN, ' ');
         for (int col = 0; col < VT_PKG_RAW_LEN; col++) {
            unsigned char c = m_ctrl[idx][col];
            if ((c < 0x1F) || (c == 0x7F))
               m_text[idx][col] = ' ';
            else
               m_text[idx][col] = c;
         }
      }
      else {
         m_ctrl[idx].assign(VT_PKG_RAW_LEN, ' ');
         m_text[idx].assign(VT_PKG_RAW_LEN, ' ');
      }
   }

   m_text_valid = true;
}

/* ------------------------------------------------------------------------------
 * Conversion of teletext G0 charset into ISO-8859-1
 */
const signed char NationalOptionsMatrix[] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x00
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x10
   -1, -1, -1,  0,  1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x20
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x30
    2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x40
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  3,  4,  5,  6,  7,  // 0x50
    8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x60
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  9, 10, 11, 12, -1   // 0x70
};

const char * const NatOptMaps[] =
{
   // for latin-1 font
   // English (100%)
   "£$@«½»¬#­¼¦¾÷",
   // German (100%)
   "#$§ÄÖÜ^_°äöüß",
   // Swedish/Finnish/Hungarian (100%)
   "#¤ÉÄÖÅÜ_éäöåü",
   // Italian (100%)
   "£$é°ç»¬#ùàòèì",
   // French (100%)
   "éïàëêùî#èâôûç",
   // Portuguese/Spanish (100%)
   "ç$¡áéíóú¿üñèà",
   // Czech/Slovak (60%)
   "#uctzýíréáeús",
   // reserved (English mapping)
   "£$@«½»¬#­¼¦¾÷",
   // Polish (6%: all but '#' missing)
   "#naZSLcoezslz",
   // German (100%)
   "#$§ÄÖÜ^_°äöüß",
   // Swedish/Finnish/Hungarian (100%)
   "#¤ÉÄÖÅÜ_éäöåü",
   // Italian (100%)
   "$é°ç»¬#ùàòèì",
   // French (100%)
   "ïàëêùî#èâôûç",
   // reserved (English mapping)
   "$@«½»¬#­¼¦¾÷",
   // Czech/Slovak 
   "uctzýíréáeús",
   // reserved (English mapping)
   "$@«½»¬#­¼¦¾÷",
   // English 
   "$@«½»¬#­¼¦¾÷",
   // German
   "$§ÄÖÜ^_°äöüß",
   // Swedish/Finnish/Hungarian (100%)
   "¤ÉÄÖÅÜ_éäöåü",
   // Italian (100%'
   "$é°ç»¬#ùàòèì",
   // French (100%)
   "ïàëêùî#èâôûç",
   // Portuguese/Spanish (100%)
   "$¡áéíóú¿üñèà",
   // Turkish (50%: 7 missing)
   "gISÖÇÜGisöçü",
   // reserved (English mapping)
   "$@«½»¬#­¼¦¾÷"
};

const bool DelSpcAttr[] =
{
   0,0,0,0,0,0,0,0,    // 0-7: alpha color
   1,1,1,1,            // 8-B: flash, box
   0,0,0,0,            // C-F: size
   1,1,1,1,1,1,1,1,    // 10-17: mosaic color
   0,                  // 18:  conceal
   1,1,                // 19-1A: continguous mosaic
   0,0,0,              // 1B-1D: charset, bg color
   1,1                 // 1E-1F: hold mosaic
};

// TODO: evaluate packet 26 and 28
void TTX_DB_PAGE::line_to_latin1(unsigned line, string& out) const
{
   bool is_g1 = false;

   for (int idx = 0; idx < VT_PKG_RAW_LEN; idx++) {
      uint8_t val = m_raw_pkg[line][idx];

      // alpha color code
      if (val <= 0x07) {
         is_g1 = false;
         //val = ' ';
      }
      // mosaic color code
      else if ((val >= 0x10) && (val <= 0x17)) {
         is_g1 = true;
         val = ' ';
      }
      // other control character or mosaic character
      else if (val < 0x20) {
         if (DelSpcAttr[val]) {
            val = ' ';
         }
      }
      // mosaic charset
      else if (is_g1) {
         val = ' ';
      }
      else if ( (val < 0x80) && (NationalOptionsMatrix[val] >= 0) ) {
         assert((size_t)m_lang < sizeof(NatOptMaps)/sizeof(NatOptMaps[0]));
         val = NatOptMaps[m_lang][NationalOptionsMatrix[val]];
      }
      out[idx] = val;
   }
}


/* ------------------------------------------------------------------------- */

bool TTX_DB_BTT::is_top_page(unsigned page) const
{
   return    (page == 0x1F0)
          || (m_have_btt && (m_link[0] == page))
          || (m_have_btt && (m_link[1] == page));
}

void TTX_DB_BTT::add_btt_pkg(unsigned page, unsigned pkg, const uint8_t * p_data)
{
   if (page == 0x1F0)
   {
      if (!m_have_btt) {
         memset(m_pg_func, 0xFF, sizeof(m_pg_func));
         memset(m_link, 0xFF, sizeof(m_link));
         m_have_btt = true;
      }
      if ((pkg >= 1) && (pkg <= 20)) {
         int off = (pkg - 1) * 40;
         for (int idx = 0; idx < 40; idx++) {
            int v = vbi_unham8(p_data[idx]);
            if (v >= 0) {
               m_pg_func[off + idx] = v;
            }
         }
      }
      else if ((pkg >= 21) && (pkg <= 23)) {
         int off = (pkg - 21) * 5;
         for (int idx = 0; idx < 5; idx++) {
            m_link[off + idx] = (vbi_unham8(p_data[idx * 2*4 + 0]) << 8) |
                                (vbi_unham8(p_data[idx * 2*4 + 1]) << 4) |
                                (vbi_unham8(p_data[idx * 2*4 + 2]) << 0);
            // next 5 bytes are unused
         }
      }
   }
   else if (m_have_btt && (m_link[0] == page)) {
      if (!m_have_mpt) {
         memset(m_sub_cnt, 0, sizeof(m_sub_cnt));
         m_have_mpt = true;
      }
      if ((pkg >= 1) && (pkg <= 20)) {
         int off = (pkg - 1) * 40;
         for (int idx = 0; idx < 40; idx++) {
            int v = vbi_unham8(p_data[idx]);
            if (v >= 0) {
               m_sub_cnt[off + idx] = v;
            }
         }
      }
   }
   else if (m_have_btt && (m_link[1] == page)) {
      if (!m_have_ait) {
         memset(m_ait, 0, sizeof(m_ait));
         m_have_ait = true;
      }
      if ((pkg >= 1) && (pkg <= 22)) {
         int off = 2 * (pkg - 1);
         // note terminating 0 is written once during init
         memcpy(m_ait[off + 0].hd_text, p_data + 8, 12); // TODO char set conversion
         memcpy(m_ait[off + 1].hd_text, p_data + 20 + 8, 12);

         m_ait[off + 0].page = (vbi_unham8(p_data[0 + 0]) << 8) |
                               (vbi_unham8(p_data[0 + 1]) << 4) |
                               (vbi_unham8(p_data[0 + 2]) << 0);
         m_ait[off + 1].page = (vbi_unham8(p_data[20 + 0]) << 8) |
                               (vbi_unham8(p_data[20 + 1]) << 4) |
                               (vbi_unham8(p_data[20 + 2]) << 0);
      }
   }
}

int TTX_DB_BTT::get_last_sub(unsigned page) const
{
   if (m_have_mpt) {
      // array is indexed by decimal page number (compression)
      int d0 = (page >> 8);
      int d1 = (page >> 4) & 0x0F;
      int d2 = page & 0x0F;
      if ((d1 < 10) && (d2 < 10)) {
         assert((d0 >= 1) && (d0 <= 8));
         int idx = d0 * 100 + d1 * 10 + d2 - 100;
         if (m_sub_cnt[idx] == 0)
            return -1;
         else if (m_sub_cnt[idx] == 1)
            return 0;
         else
            return m_sub_cnt[idx];
      }
   }
   return -1;
}

void TTX_DB_BTT::dump_btt_as_text(FILE * fp)
{
   if (m_have_ait) {
      fprintf(fp, "PAGE BTT-AIT\n");
      for (int idx = 0; idx < 44; idx++) {
         fprintf(fp, " %03X %.12s\n", m_ait[idx].page, m_ait[idx].hd_text);
      }
   }
}

void TTX_DB_BTT::flush()
{
   m_have_btt = false;
   m_have_mpt = false;
   m_have_ait = false;
}

/* ------------------------------------------------------------------------- */

TTX_PAGE_DB::~TTX_PAGE_DB()
{
   for (iterator p = m_db.begin(); p != m_db.end(); p++) {
      delete p->second;
   }
}

bool TTX_PAGE_DB::sub_page_exists(unsigned page, unsigned sub) const
{
   return m_db.find(TTX_PG_HANDLE(page, sub)) != m_db.end();
}

const TTX_DB_PAGE* TTX_PAGE_DB::get_sub_page(unsigned page, unsigned sub) const
{
   const_iterator p = m_db.find(TTX_PG_HANDLE(page, sub));
   return (p != m_db.end()) ? p->second : 0;
}

TTX_PAGE_DB::const_iterator TTX_PAGE_DB::first_sub_page(unsigned page) const
{
   const_iterator p = m_db.lower_bound(TTX_PG_HANDLE(page, 0));
   return (p->first.page() == page) ? p : end();
}

TTX_PAGE_DB::const_iterator& TTX_PAGE_DB::next_sub_page(unsigned page, const_iterator& p) const
{
   ++p;
   if (p->first.page() > page)
      p = end();
   return p;
}

int TTX_PAGE_DB::last_sub_page_no(unsigned page) const
{
   int last_sub = m_btt.get_last_sub(page);
   if (last_sub == -1) {
      const_iterator p = m_db.lower_bound(TTX_PG_HANDLE(page + 1, 0));
      last_sub = -1;
      if ((p != m_db.begin()) && (m_db.size() > 0)) {
         --p;
         if (p->first.page() == page)
            last_sub = p->first.sub();
      }
   }
   return last_sub;
}

int TTX_PAGE_DB::get_sub_page_cnt(unsigned page) const
{
  return m_btt.get_last_sub(page);
}

// Decides if the page is acceptable for addition to the database.
bool TTX_PAGE_DB::page_acceptable(unsigned page) const
{
   // decimal page (human readable) or TOP table
   return (   (((page & 0x0F) <= 9) && (((page >> 4) & 0x0F) <= 9))
           || m_btt.is_top_page(page));
}

TTX_DB_PAGE* TTX_PAGE_DB::add_page(unsigned page, unsigned sub, unsigned ctrl, const uint8_t * p_data, time_t ts)
{
   TTX_PG_HANDLE handle(page, sub);

   iterator p = m_db.lower_bound(handle);
   if ((p == m_db.end()) || (p->first != handle)) {
      TTX_DB_PAGE * p_pg = new TTX_DB_PAGE(page, sub, ctrl, ts);
      p = m_db.insert(p, make_pair(handle, p_pg));
   }
   else {
      p->second->inc_acq_cnt(ts);
   }
   p->second->add_raw_pkg(0, p_data);
   return p->second;
}

void TTX_PAGE_DB::add_page_data(unsigned page, unsigned sub, unsigned idx, const uint8_t * p_data)
{
   if (m_btt.is_top_page(page)) {
      // forward the data to the BTT
      m_btt.add_btt_pkg(page, idx, p_data);
   }
   else {
      TTX_PG_HANDLE handle(page, sub);

      iterator p = m_db.find(handle);
      if (p != m_db.end()) {
         p->second->add_raw_pkg(idx, p_data);
      }
      else {
         //if (opt_debug) printf("ERROR: page:%d sub:%d not found for adding pkg:%d\n", page, sub, idx);
      }
   }
}

void TTX_PAGE_DB::flush()
{
   for (iterator p = m_db.begin(); p != m_db.end(); p++) {
      delete p->second;
   }
   m_db.clear();
   m_btt.flush();
}

/* Erase the page with the given number from memory: used to handle the "erase"
 * control bit in the TTX header. Since the page is added again right after we
 * only invalidate the page contents, but keep the page.
 */
void TTX_PAGE_DB::erase_page_c4(int page, int sub)
{
   TTX_PG_HANDLE handle(page, sub);

   iterator p = m_db.find(handle);
   if (p != m_db.end()) {
      p->second->erase_page_c4();
   }
}

double TTX_PAGE_DB::get_acq_rep_stats()
{
   int page_cnt = 0;
   int page_rep = 0;
   unsigned prev_page = 0xFFFu;

   for (const_iterator p = m_db.begin(); p != m_db.end(); p++) {
      if (p->first.page() != prev_page)
         page_cnt += 1;
      prev_page = p->first.page();

      page_rep += p->second->get_acq_rep();
   }
   return (page_cnt > 0) ? ((double)page_rep / page_cnt) : 0.0;
}

/* ------------------------------------------------------------------------------
 * Dump all loaded teletext pages as plain text
 * - teletext control characters and mosaic is replaced by space
 * - used for -dump option, intended for debugging only
 */
void DumpTextPages(TTX_DB * db, const char * p_name)
{
   if (p_name != 0) {
      FILE * fp = fopen(p_name, "w");
      if (fp == NULL) {
         fprintf(stderr, "Failed to create %s: %s\n", p_name, strerror(errno));
         exit(1);
      }

      db->page_db.dump_db_as_text(fp);
      fclose(fp);
   }
   else {
      db->page_db.dump_db_as_text(stdout);
   }
}

void TTX_PAGE_DB::dump_db_as_text(FILE * fp)
{
   for (iterator p = m_db.begin(); p != m_db.end(); p++) {
      p->second->dump_page_as_text(fp);
   }

   m_btt.dump_btt_as_text(fp);
}

void TTX_DB_PAGE::dump_page_as_text(FILE * fp)
{
   fprintf(fp, "PAGE %03X.%04X\n", m_page, m_sub);

   for (unsigned idx = 1; idx < TTX_TEXT_LINE_CNT; idx++) {
      fprintf(fp, "%.40s\n", get_text(idx).c_str());
   }
   fprintf(fp, "\n");
}

/* ------------------------------------------------------------------------------
 * Dump all loaded teletext data as Perl script
 * - the generated script can be loaded with the -verify option
 */
void DumpRawTeletext(TTX_DB * db, const char * p_name, int pg_start, int pg_end)
{
   FILE * fp = stdout;

   if (p_name != 0) {
      fp = fopen(p_name, "w");
      if (fp == NULL) {
         fprintf(stderr, "Failed to create %s: %s\n", p_name, strerror(errno));
         exit(1);
      }
   }

   fprintf(fp, "#!tv_grab_ttx -verify\n");

   db->page_db.dump_db_as_raw(fp, pg_start, pg_end);

   db->chn_id.dump_as_raw(fp);

   if (fp != stdout) {
      fclose(fp);
   }
}

void TTX_CHN_ID::dump_as_raw(FILE * fp)
{
   for (const_iterator p = m_cnis.begin(); p != m_cnis.end(); p++) {
      fprintf(fp, "$PkgCni{0x%X} = %d;\n", p->first, p->second);
   }
}

void TTX_PAGE_DB::dump_db_as_raw(FILE * fp, int pg_start, int pg_end)
{
   // acq start time (for backwards compatibility with Perl version only)
   const_iterator first = m_db.begin();
   time_t acq_ts = (first != m_db.end()) ? first->second->get_timestamp() : 0;
   fprintf(fp, "$VbiCaptureTime = %ld;\n", (long)acq_ts);

   for (iterator p = m_db.begin(); p != m_db.end(); p++) {
      int page = p->first.page();
      if ((page >= pg_start) && (page <= pg_end)) {
         int last_sub = last_sub_page_no(page);

         p->second->dump_page_as_raw(fp, last_sub);
      }
   }
}

void TTX_DB_PAGE::dump_page_as_raw(FILE * fp, int last_sub)
{
   fprintf(fp, "$PgCnt{0x%03X} = %d;\n", m_page, m_acq_rep_cnt);
   fprintf(fp, "$PgSub{0x%03X} = %d;\n", m_page, last_sub);
   fprintf(fp, "$PgLang{0x%03X} = %d;\n", m_page, m_lang);
   fprintf(fp, "$PgTime{0x%03X} = %ld;\n", m_page, (long)m_timestamp);

   fprintf(fp, "$Pkg{0x%03X|(0x%04X<<12)} =\n[\n", m_page, m_sub);

   for (unsigned idx = 0; idx < TTX_RAW_PKG_CNT; idx++) {
      if (m_raw_pkg_valid & (1 << idx)) {
         fprintf(fp, "  \"");
         for (unsigned cidx = 0; cidx < VT_PKG_RAW_LEN; cidx++) {
            // quote binary characters
            unsigned char c = m_raw_pkg[idx][cidx];
            if ((c < 0x20) || (c == 0x7F)) {
               fprintf(fp, "\\x%02X", c);
            }
            // backwards compatibility: quote C and Perl special characters
            else if (   (c == '@')
                     || (c == '$')
                     || (c == '%')
                     || (c == '"')
                     || (c == '\\') ) {
               fputc('\\', fp);
               fputc(c, fp);
            }
            else {
               fputc(c, fp);
            }
         }
         fprintf(fp, "\",\n");
      }
      else {
         fprintf(fp, "  undef,\n");
      }
   }
   fprintf(fp, "];\n");
}

/* ------------------------------------------------------------------------------
 * Import a data file generated by DumpRawTeletext
 * - the function returns FALSE if the header isn't found (w/o error message)
 */
bool ImportRawDump(TTX_DB * db, const char * p_name)
{
   FILE * fp;

   if ((p_name == 0) || (*p_name == 0)) {
      fp = stdin;
   } else {
      fp = fopen(p_name, "r");
      if (fp == NULL) {
         fprintf(stderr, "Failed to open %s: %s\n", p_name, strerror(errno));
         exit(1);
      }
   }

   cmatch what;
   static const regex expr1("#!tv_grab_ttx -verify\\s*");
   static const regex expr2("\\$VbiCaptureTime\\s*=\\s*(\\d+);\\s*");
   static const regex expr3("\\$PkgCni\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr4("\\$PgCnt\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr5("\\$PgSub\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr6("\\$PgLang\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr6b("\\$PgTime\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr7("\\$Pkg\\{0x([0-9A-Za-z]+)\\|\\(0x([0-9A-Za-z]+)<<12\\)\\}\\s*=\\s*");
   static const regex expr8("\\[\\s*");
   static const regex expr9("\\s*undef,\\s*");
   static const regex expr10("\\s*\"(.*)\",\\s*");
   static const regex expr11("\\];\\s*");
   static const regex expr12("1;\\s*");
   static const regex expr13("(#.*\\s*|\\s*)");

   bool found_head = false;
   int file_line_no = 0;
   int page = -1;
   unsigned sub = 0;
   unsigned pkg_idx = 0;
   unsigned lang = 0;
   unsigned pg_cnt = 0;
   TTX_DB_PAGE::TTX_RAW_PKG pg_data[TTX_DB_PAGE::TTX_RAW_PKG_CNT];
   uint32_t pg_data_valid = 0;
   time_t timestamp = 0;

   char buf[256];
   while (fgets(buf, sizeof(buf), fp) != 0) {
      file_line_no ++;
      if (regex_match(buf, what, expr1)) {
         found_head = true;
      }
      else if (!found_head) {
        return false;
      }
      else if (regex_match(buf, what, expr2)) {
         timestamp = atol(string(what[1]).c_str());
      }
      else if (regex_match(buf, what, expr3)) {
         int cni = atox_substr(what[1]);
         int cnt = atoi_substr(what[2]);
         for (int idx = 0; idx < cnt; idx++) {
            db->chn_id.add_cni(cni);
         }
      }
      else if (regex_match(buf, what, expr4)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         pg_cnt = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr5)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         sub = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr6)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         lang = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr6b)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         timestamp = atol(string(what[2]).c_str());
      }
      else if (regex_match(buf, what, expr7)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         sub = atox_substr(what[2]);
         pg_data_valid = 0;
         pkg_idx = 0;
      }
      else if (regex_match(buf, what, expr8)) {
      }
      else if (regex_match(buf, what, expr9)) {
         // undef
         pkg_idx += 1;
      }
      else if (regex_match(buf, what, expr10)) {
         // line
         assert((page != -1) && (pkg_idx < TTX_DB_PAGE::TTX_RAW_PKG_CNT));
         const char * p = &what[1].first[0];
         int idx = 0;
         while ((*p != 0) && (idx < VT_PKG_RAW_LEN)) {
            int val, val_len;
            if ((*p == '\\') && (p[1] == 'x') &&
                (sscanf(p + 2, "%2x%n", &val, &val_len) >= 1) && (val_len == 2)) {
               pg_data[pkg_idx][idx++] = val;
               p += 4;
            } else if (*p == '\\') {
               pg_data[pkg_idx][idx++] = p[1];
               p += 2;
            } else {
               pg_data[pkg_idx][idx++] = *p;
               p += 1;
            }
         }
         pg_data_valid |= (1 << pkg_idx);
         pkg_idx += 1;
      }
      else if (regex_match(buf, what, expr11)) {
         assert(page != -1);
         int ctrl = sub | ((lang & 1) << (16+7)) | ((lang & 2) << (16+5)) | ((lang & 4) << (16+3));
         TTX_DB_PAGE * pgtext = db->page_db.add_page(page, sub, ctrl, pg_data[0], timestamp);
         for (unsigned idx = 1; idx < pkg_idx; idx++) {
            if (pg_data_valid & (1 << idx))
               db->page_db.add_page_data(page, sub, idx, pg_data[idx]);
         }
         for (unsigned idx = 1; idx < pg_cnt; idx++) {
            pgtext->inc_acq_cnt(timestamp);
         }
         page = -1;
      }
      else if (regex_match(buf, what, expr12)) {
      }
      else if (regex_match(buf, what, expr13)) {
         // comment or empty line - ignored
      }
      else {
         fprintf(stderr, "Import parse error in line %d '%s'\n", file_line_no, buf);
         exit(1);
      }
   }
   fclose(fp);
   return true;
}


/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------------
 * Determine channel ID from CNI
 */
const TTX_CHN_ID::T_CNI_TO_ID_MAP TTX_CHN_ID::Cni2ChannelId[] =
{
   { 0x1DCA, "1.br-online.de" },
   { 0x4801, "1.omroep.nl" },
   { 0x1AC1, "1.orf.at" },
   { 0x3901, "1.rai.it" },
   { 0x24C1, "1.sfdrs.ch" },
   { 0x24C2, "1.tsr.ch" },
   { 0x4802, "2.omroep.nl" },
   { 0x1AC2, "2.orf.at" },
   { 0x3902, "2.rai.it" },
   { 0x24C7, "2.sfdrs.ch" },
   { 0x24C8, "2.tsr.ch" },
   { 0x1DCB, "3.br-online.de" },
   { 0x4803, "3.omroep.nl" },
   { 0x1AC3, "3.orf.at" },
   { 0x3903, "3.rai.it" },
   { 0x1DC7, "3sat.de" },
   { 0x1D44, "alpha.br-online.de" },
   { 0x1DC1, "ard.de" },
   { 0x1D85, "arte-tv.com" },
   { 0x2C7F, "bbc1.bbc.co.uk" },
   { 0x2C40, "bbc2.bbc.co.uk" },
   { 0x2C57, "bbcworld.com" },
   { 0x1DE1, "bw.swr.de" },
   { 0x2C11, "channel4.com" },
   { 0x1546, "cnn.com" },
   { 0x1D76, "das-vierte.de" },
   { 0x5BF2, "discoveryeurope.com" },
   { 0x1D8D, "dsf.com" },
   { 0x2FE1, "euronews.net" },
   { 0x1D91, "eurosport.de" },
   { 0x1DD1, "hamburg1.de" },
   { 0x1DCF, "hr-online.de" },
   { 0x1D92, "kabel1.de" },
   { 0x1DC9, "kika.de" },
   { 0x3203, "la1.rtbf.be" },
   { 0x3204, "la2.rtbf.be" },
   { 0x2F06, "m6.fr" },
   { 0x1DFE, "mdr.de" },
   { 0x1D73, "mtv.de" },
   { 0x1D8C, "n-tv.de" },
   { 0x1D7A, "n24.de" },
   { 0x2C31, "nbc.com" },
   { 0x1DD4, "ndr.de" },
   { 0x1DBA, "neunlive.de" },
   { 0x1D7C, "onyx.tv" },
   { 0x1D82, "orb.de" },
   { 0x1DC8, "phoenix.de" },
   { 0x2487, "prosieben.ch" },
   { 0x1D94, "prosieben.de" },
   { 0x1DDC, "rbb-online.de" },
   { 0x1DE4, "rp.swr.de" },
   { 0x1DAB, "rtl.de" },
   { 0x1D8F, "rtl2.de" },
   { 0x1DB9, "sat1.de" },
   { 0x2C0D, "sky-news.sky.com" },
   { 0x2C0E, "sky-one.sky.com" },
   { 0x1DE2, "sr.swr.de" },
   { 0x1D8A, "superrtl.de" },
   { 0x1DE0, "swr.de" },
   { 0x1D78, "tele5.de" },
   { 0x9001, "trt.net.tr" },
   { 0x1601, "tv1.vrt.be" },
   { 0x2FE5, "tv5.org" },
   { 0x1D88, "viva.tv" },
   { 0x1D89, "viva2.de" },
   { 0x1D8E, "vox.de" },
   { 0x1DE6, "wdr.de" },
   { 0x1DC2, "zdf.de" },
   { 0, 0 }
};

const uint16_t TTX_CHN_ID::NiToPdcCni[] =
{
   // Austria
   0x4301, 0x1AC1,
   0x4302, 0x1AC2,
   // Belgium
   0x0404, 0x1604,
   0x3201, 0x1601,
   0x3202, 0x1602,
   0x3205, 0x1605,
   0x3206, 0x1606,
   // Croatia
   // Czech Republic
   0x4201, 0x32C1,
   0x4202, 0x32C2,
   0x4203, 0x32C3,
   0x4211, 0x32D1,
   0x4212, 0x32D2,
   0x4221, 0x32E1,
   0x4222, 0x32E2,
   0x4231, 0x32F1,
   0x4232, 0x32F2,
   // Denmark
   0x4502, 0x2902,
   0x4503, 0x2904,
   0x49CF, 0x2903,
   0x7392, 0x2901,
   // Finland
   0x3581, 0x2601,
   0x3582, 0x2602,
   0x358F, 0x260F,
   // France
   0x330A, 0x2F0A,
   0x3311, 0x2F11,
   0x3312, 0x2F12,
   0x3320, 0x2F20,
   0x3321, 0x2F21,
   0x3322, 0x2F22,
   0x33C1, 0x2FC1,
   0x33C2, 0x2FC2,
   0x33C3, 0x2FC3,
   0x33C4, 0x2FC4,
   0x33C5, 0x2FC5,
   0x33C6, 0x2FC6,
   0x33C7, 0x2FC7,
   0x33C8, 0x2FC8,
   0x33C9, 0x2FC9,
   0x33CA, 0x2FCA,
   0x33CB, 0x2FCB,
   0x33CC, 0x2FCC,
   0x33F1, 0x2F01,
   0x33F2, 0x2F02,
   0x33F3, 0x2F03,
   0x33F4, 0x2F04,
   0x33F5, 0x2F05,
   0x33F6, 0x2F06,
   0xF101, 0x2FE2,
   0xF500, 0x2FE5,
   0xFE01, 0x2FE1,
   // Germany
   0x4901, 0x1DC1,
   0x4902, 0x1DC2,
   0x490A, 0x1D85,
   0x490C, 0x1D8E,
   0x4915, 0x1DCF,
   0x4918, 0x1DC8,
   0x4941, 0x1D41,
   0x4942, 0x1D42,
   0x4943, 0x1D43,
   0x4944, 0x1D44,
   0x4982, 0x1D82,
   0x49BD, 0x1D77,
   0x49BE, 0x1D7B,
   0x49BF, 0x1D7F,
   0x49C7, 0x1DC7,
   0x49C9, 0x1DC9,
   0x49CB, 0x1DCB,
   0x49D4, 0x1DD4,
   0x49D9, 0x1DD9,
   0x49DC, 0x1DDC,
   0x49DF, 0x1DDF,
   0x49E1, 0x1DE1,
   0x49E4, 0x1DE4,
   0x49E6, 0x1DE6,
   0x49FE, 0x1DFE,
   0x49FF, 0x1DCE,
   0x5C49, 0x1D7D,
   // Greece
   0x3001, 0x2101,
   0x3002, 0x2102,
   0x3003, 0x2103,
   // Hungary
   // Iceland
   // Ireland
   0x3531, 0x4201,
   0x3532, 0x4202,
   0x3533, 0x4203,
   // Italy
   0x3911, 0x1511,
   0x3913, 0x1513,
   0x3914, 0x1514,
   0x3915, 0x1515,
   0x3916, 0x1516,
   0x3917, 0x1517,
   0x3918, 0x1518,
   0x3919, 0x1519,
   0x3942, 0x1542,
   0x3943, 0x1543,
   0x3944, 0x1544,
   0x3945, 0x1545,
   0x3946, 0x1546,
   0x3947, 0x1547,
   0x3948, 0x1548,
   0x3949, 0x1549,
   0x3960, 0x1560,
   0x3968, 0x1568,
   0x3990, 0x1590,
   0x3991, 0x1591,
   0x3992, 0x1592,
   0x3993, 0x1593,
   0x3994, 0x1594,
   0x3996, 0x1596,
   0x39A0, 0x15A0,
   0x39A1, 0x15A1,
   0x39A2, 0x15A2,
   0x39A3, 0x15A3,
   0x39A4, 0x15A4,
   0x39A5, 0x15A5,
   0x39A6, 0x15A6,
   0x39B0, 0x15B0,
   0x39B2, 0x15B2,
   0x39B3, 0x15B3,
   0x39B4, 0x15B4,
   0x39B5, 0x15B5,
   0x39B6, 0x15B6,
   0x39B7, 0x15B7,
   0x39B9, 0x15B9,
   0x39C7, 0x15C7,
   // Luxembourg
   // Netherlands
   0x3101, 0x4801,
   0x3102, 0x4802,
   0x3103, 0x4803,
   0x3104, 0x4804,
   0x3105, 0x4805,
   0x3106, 0x4806,
   0x3120, 0x4820,
   0x3122, 0x4822,
   // Norway
   // Poland
   // Portugal
   // San Marino
   // Slovakia
   0x42A1, 0x35A1,
   0x42A2, 0x35A2,
   0x42A3, 0x35A3,
   0x42A4, 0x35A4,
   0x42A5, 0x35A5,
   0x42A6, 0x35A6,
   // Slovenia
   // Spain
   // Sweden
   0x4601, 0x4E01,
   0x4602, 0x4E02,
   0x4640, 0x4E40,
   // Switzerland
   0x4101, 0x24C1,
   0x4102, 0x24C2,
   0x4103, 0x24C3,
   0x4107, 0x24C7,
   0x4108, 0x24C8,
   0x4109, 0x24C9,
   0x410A, 0x24CA,
   0x4121, 0x2421,
   // Turkey
   0x9001, 0x4301,
   0x9002, 0x4302,
   0x9003, 0x4303,
   0x9004, 0x4304,
   0x9005, 0x4305,
   0x9006, 0x4306,
   // UK
   0x01F2, 0x5BF1,
   0x10E4, 0x2C34,
   0x1609, 0x2C09,
   0x1984, 0x2C04,
   0x200A, 0x2C0A,
   0x25D0, 0x2C30,
   0x28EB, 0x2C2B,
   0x2F27, 0x2C37,
   0x37E5, 0x2C25,
   0x3F39, 0x2C39,
   0x4401, 0x5BFA,
   0x4402, 0x2C01,
   0x4403, 0x2C3C,
   0x4404, 0x5BF0,
   0x4405, 0x5BEF,
   0x4406, 0x5BF7,
   0x4407, 0x5BF2,
   0x4408, 0x5BF3,
   0x4409, 0x5BF8,
   0x4440, 0x2C40,
   0x4441, 0x2C41,
   0x4442, 0x2C42,
   0x4444, 0x2C44,
   0x4457, 0x2C57,
   0x4468, 0x2C68,
   0x4469, 0x2C69,
   0x447B, 0x2C7B,
   0x447D, 0x2C7D,
   0x447E, 0x2C7E,
   0x447F, 0x2C7F,
   0x44D1, 0x5BCC,
   0x4D54, 0x2C14,
   0x4D58, 0x2C20,
   0x4D59, 0x2C21,
   0x4D5A, 0x5BF4,
   0x4D5B, 0x5BF5,
   0x5AAF, 0x2C3F,
   0x82DD, 0x2C1D,
   0x82E1, 0x2C05,
   0x833B, 0x2C3D,
   0x884B, 0x2C0B,
   0x8E71, 0x2C31,
   0x8E72, 0x2C35,
   0x9602, 0x2C02,
   0xA2FE, 0x2C3E,
   0xA82C, 0x2C2C,
   0xADD8, 0x2C18,
   0xADDC, 0x5BD2,
   0xB4C7, 0x2C07,
   0xB7F7, 0x2C27,
   0xC47B, 0x2C3B,
   0xC8DE, 0x2C1E,
   0xF33A, 0x2C3A,
   0xF9D2, 0x2C12,
   0xFA2C, 0x2C2D,
   0xFA6F, 0x2C2F,
   0xFB9C, 0x2C1C,
   0xFCD1, 0x2C11,
   0xFCE4, 0x2C24,
   0xFCF3, 0x2C13,
   0xFCF4, 0x5BF6,
   0xFCF5, 0x2C15,
   0xFCF6, 0x5BF9,
   0xFCF7, 0x2C17,
   0xFCF8, 0x2C08,
   0xFCF9, 0x2C19,
   0xFCFA, 0x2C1A,
   0xFCFB, 0x2C1B,
   0xFCFC, 0x2C0C,
   0xFCFD, 0x2C0D,
   0xFCFE, 0x2C0E,
   0xFCFF, 0x2C0F,
   // Ukraine
   // USA
   0, 0
};

string TTX_CHN_ID::get_ch_id()
{
   // search the most frequently seen CNI value 
   uint16_t cni = 0;
   int max_cnt = -1;
   for (const_iterator p = m_cnis.begin(); p != m_cnis.end(); p++) {
      if (p->second > max_cnt) {
         cni = p->first;
         max_cnt = p->second;
      }
   }

   if (cni != 0) {
      for (int idx = 0; NiToPdcCni[idx] != 0; idx += 2) {
         if (NiToPdcCni[idx] == cni) {
            cni = NiToPdcCni[idx + 1];
            break;
         }
      }
      int pdc_cni = cni;
      if ((cni >> 8) == 0x0D) {
         pdc_cni |= 0x1000;
      } else if ((cni >> 8) == 0x0A) {
         pdc_cni |= 0x1000;
      } else if ((cni >> 8) == 0x04) {
         pdc_cni |= 0x2000;
      }
      for (int idx = 0; Cni2ChannelId[idx].cni != 0; idx++) {
         if (Cni2ChannelId[idx].cni == pdc_cni) {
            return string(Cni2ChannelId[idx].p_name);
         }
      }

      // not found - derive pseudo-ID from CNI value
      char buf[20];
      sprintf(buf, "CNI%04X", cni);
      return string(buf);
   }
   else {
      return string("");
   }
}

void TTX_CHN_ID::add_cni(unsigned cni)
{
   iterator p = m_cnis.lower_bound(cni);
   if ((p == m_cnis.end()) || (p->first != int(cni))) {
      p = m_cnis.insert(p, make_pair<int,int>(cni, 1));
   }
   else {
      ++p->second;
   }
}

void TTX_CHN_ID::flush()
{
   m_cnis.clear();
}

