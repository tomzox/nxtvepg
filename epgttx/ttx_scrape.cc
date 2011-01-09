/*
 * Teletext EPG grabber: EPG data scraping main class
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
 * Copyright 2006-2011 by Tom Zoerner (tomzo at users.sf.net)
 *
 * $Id: ttx_scrape.cc,v 1.8 2011/01/08 13:28:24 tom Exp $
 */

#include <stdio.h>
#include <string.h>

#include <sstream>
#include <string>
#include <algorithm>

#include <boost/regex.h>
#include <boost/regex.hpp>

using namespace std;
using namespace boost;

#include "ttx_db.h"
#include "ttx_util.h"
#include "ttx_date.h"
#include "ttx_ov_fmt.h"
#include "ttx_feat.h"
#include "ttx_pg_ref.h"
#include "ttx_scrape.h"


OV_SLOT::OV_SLOT(int hour, int min, bool is_tip)
{
   m_hour = hour;
   m_min = min;
   m_is_tip = is_tip;
   m_start_t = -1;
   m_stop_t = -1;
   m_end_hour = -1;
   m_end_min = -1;
   m_date_wrap = 0;
   m_ttx_ref = -1;
}

OV_SLOT::~OV_SLOT()
{
}

void OV_SLOT::add_title(string subt)
{
   m_ov_title.push_back(subt);
}

void OV_SLOT::merge_feat(const TV_FEAT& feat)
{
  m_ext_feat = feat;
}

void OV_SLOT::merge_desc(const string& desc)
{
   m_ext_desc = desc;
}

void OV_SLOT::merge_title(const string& title, const string& sub_title)
{
   m_ext_title = title;
   m_ext_subtitle = sub_title;
}

void OV_SLOT::parse_ttx_ref(const T_TRAIL_REF_FMT& fmt, map<int,int>& ttx_ref_map)
{
   for (unsigned idx = 0; idx < m_ov_title.size(); idx++) {
      if (fmt.parse_trailing_ttx_ref(m_ov_title[idx], m_ttx_ref)) {
         ++ttx_ref_map[m_ttx_ref];
         break;
      }
   }
}

void OV_SLOT::detect_ttx_ref_fmt(vector<T_TRAIL_REF_FMT>& fmt_list)
{
   for (unsigned idx = 0; idx < m_ov_title.size(); idx++) {
      T_TRAIL_REF_FMT fmt;
      if (fmt.detect_ref_fmt(m_ov_title[idx])) {
         fmt_list.push_back(fmt);
      }
   }
}

void OV_SLOT::parse_feature_flags()
{
   for (unsigned idx = 0; idx < m_ov_title.size(); idx++) {
      if (idx != 0)
         m_ov_title[idx].insert(0, 2, ' '); //FIXME!! HACK

      m_ext_feat.ParseTrailingFeat(m_ov_title[idx]);
   }

   m_ext_feat.set_tip(m_is_tip);
}

void OV_SLOT::parse_ov_title()
{
#if 0 // obsolete?
   // kika special: subtitle appended to title
   static const regex expr7("(.*\\(\\d+( ?[\\&\\-\\+] ?\\d+)*\\))/ *(\\S.*)");
   smatch whats;
   if (regex_search(subt, whats, expr7)) {
      m_ov_title.push_back(string(whats[1]));
      m_ov_title.push_back(string(whats[3]));
      str_chomp(m_ov_title[1]);
   }
   else {
      m_ov_title.push_back(subt);
      str_chomp(m_ov_title[0]);
   }
#endif

   // combine title with next line only if finding "-"
   unsigned first_subt = 1;
   m_ext_title = m_ov_title[0];
   while (   (m_ov_title.size() > first_subt)
          && (str_concat_title(m_ext_title, m_ov_title[first_subt], true)) ) {
      ++first_subt;
   }

   // rest of lines: combine words separated by line end with "-"
   for (unsigned idx = first_subt; idx < m_ov_title.size(); idx++) {
      str_concat_title(m_ext_subtitle, m_ov_title[idx], false);
   }
}

bool OV_SLOT::is_same_prog(const OV_SLOT& v) const
{
   return (v.m_hour == m_hour) &&
          (v.m_min == m_min);
}

/* ------------------------------------------------------------------------------
 * Convert discrete start times into UNIX epoch format
 * - implies a conversion from local time zone into GMT
 */
time_t OV_SLOT::convert_start_t(const T_PG_DATE * pgdate, int date_off) const
{
   return pgdate->convert_time(m_hour, m_min, date_off);
}


OV_PAGE::OV_PAGE(int page, int sub)
   : m_page(page)
   , m_sub(sub)
   , m_sub_page_skip(0)
   , m_head(-1)
   , m_foot(23)
{
}

OV_PAGE::~OV_PAGE()
{
   for (unsigned idx = 0; idx < m_slots.size(); idx++) {
      delete m_slots[idx];
   }
}

/* ------------------------------------------------------------------------------
 * Convert a "traditional" VPS CNI into a 16-bit PDC channel ID
 * - first two digits are the PDC country code in hex (e.g. 1D,1A,24 for D,A,CH)
 * - 3rd digit is an "address area", indicating network code tables 1 to 4
 * - the final 3 digits are decimal and the index into the network table
 */
template<class IT>
int ConvertVpsCni(const IT& first, const IT& second)
{
   int a, b, c;

   if (   (first + (2+1+2) == second)
       && (sscanf(&first[0], "%2x%1d%2d", &a, &b, &c) == 3)
       && (b >= 1) && (b <= 4)) {
      return (a<<8) | ((4 - b) << 6) | (c & 0x3F);
   }
   else {
      return -1;
   }
}

/* ------------------------------------------------------------------------------
 * Parse and remove VPS indicators
 * - format as defined in ETS 300 231, ch. 7.3.1.3
 * - matching text is replaced with blanks
 * - TODO: KiKa special: "VPS 20.00" (magenta)
 */
void ParseVpsLabel(string& text, const string& text_pred, T_VPS_TIME& vps_data, bool is_desc)
{
   match_results<string::iterator> whati;
   string::iterator starti = text.begin();

   // for performance reasons check first if there's a magenta or conceal char
   static const regex expr0("([\\x05\\x18]+[\\x05\\x18 ]*)([^\\x00-\\x20])");
   while (regex_search(starti, text.end(), whati, expr0)) {
      bool is_concealed = str_is_concealed(whati[1]);
      starti = whati[2].first;

      static const regex expr1("^(VPS[\\x05\\x18 ]+)?"
                               "(\\d{4})([\\x05\\x18 ]+[\\dA-Fs]*)*([\\x00-\\x04\\x06\\x07]| *$)");
      static const regex expr1b("^VPS[\\x05\\x18 ]+" // obligatory "VPS "
                               "(\\d{2})[.:](\\d{2})([\\x00-\\x04\\x06\\x07]| *$)");
      static const regex expr1c("^((\\d{2})[.:](\\d{2}))([\\x00-\\x04\\x06\\x07]| *$)"); // obligatory VPS in preceding line
      static const regex expr1c2("[\\x00-\\x07\\x018 ]*VPS[\\x00-\\x07\\x018 ]*");
      static const regex expr2("^([0-9A-F]{2}\\d{3})[\\x05\\x18 ]+"
                               "(\\d{6})([\\x05\\x18 ]+[\\dA-Fs]*)*([\\x00-\\x04\\x06\\x07]| *$)");
      static const regex expr3("^(\\d{6})([\\x05\\x18 ]+[\\dA-Fs]*)*([\\x00-\\x04\\x06\\x07]| *$)");
      //static const regex expr4("^(\\d{2}[.:]\\d{2}) oo *([\\x00-\\x04\\x06\\x07]| *$)");
      static const regex expr5("^VPS *([\\x00-\\x04\\x06\\x07]| *$)");

      // time
      // discard any concealed/magenta labels which follow
      if (regex_search(starti, text.end(), whati, expr1)) {
         vps_data.m_vps_time.assign(whati[2].first, whati[2].second);
         vps_data.m_new_vps_time = true;
         // blank out the same area in the text-only string
         str_blank(whati[0]);
         if (opt_debug) printf("VPS time found: %s\n", vps_data.m_vps_time.c_str());
      }
      // time with ":" separator - only allowed after "VPS" prefix
      // discard any concealed/magenta labels which follow
      else if (regex_search(starti, text.end(), whati, expr1b)) {
         vps_data.m_vps_time.assign(whati[1].first, whati[1].second);
         vps_data.m_vps_time += string(whati[2].first, whati[2].second);
         vps_data.m_new_vps_time = true;
         // blank out the same area in the text-only string
         str_blank(whati[0]);
         if (opt_debug) printf("VPS time found: %s\n", vps_data.m_vps_time.c_str());
      }
      // FIXME should accept this only inside the start time column
      else if (!is_desc && regex_search(starti, text.end(), whati, expr1c)) {
         match_results<string::const_iterator> whatic; // whati remains valid
         string str_tmp = string(text_pred.begin() + whati.position(1),
                                 text_pred.begin() + whati.position(1) + whati[1].length());
         if (regex_match(text_pred.begin() + whati.position(1),
                         text_pred.begin() + whati.position(1) + whati[1].length(),
                         whatic, expr1c2)) {
            vps_data.m_vps_time.assign(whati[2].first, whati[2].second);
            vps_data.m_vps_time += string(whati[3].first, whati[3].second);
            vps_data.m_new_vps_time = true;
            // blank out the same area in the text-only string
            str_blank(whati[0]);
            if (opt_debug) printf("VPS time found: %s\n", vps_data.m_vps_time.c_str());
         }
      }
      // CNI and date "1D102 120406 F9" (ignoring checksum)
      else if (regex_search(starti, text.end(), whati, expr2)) {
         vps_data.m_vps_date.assign(whati[2].first, whati[2].second);
         vps_data.m_new_vps_date = true;
         str_blank(whati[0]);
         vps_data.m_vps_cni = ConvertVpsCni(whati[1].first, whati[1].second);
         if (opt_debug) printf("VPS date and CNI: 0x%X, /%s/\n", vps_data.m_vps_cni, vps_data.m_vps_date.c_str());
      }
      // date
      else if (regex_search(starti, text.end(), whati, expr3)) {
         vps_data.m_vps_date.assign(whati[1].first, whati[1].second);
         vps_data.m_new_vps_date = true;
         str_blank(whati[0]);
         if (opt_debug) printf("VPS date: /%s/\n", vps_data.m_vps_date.c_str());
      }
      // end time (RTL special - not standardized)
      //else if (!is_desc && regex_search(starti, text.end(), whati, expr4)) {
         // detected by the OV parser without evaluating the conceal code
         //vps_data.m_page_end_time = string(whati[1].first, whati[1].second);
         //if (opt_debug) printf("VPS(pseudo) page end time: %s\n", vps_data.m_page_end_time);
      //}
      // "VPS" marker string
      else if (regex_search(starti, text.end(), whati, expr5)) {
         str_blank(whati[0]);
      }
      else if (is_concealed) {
         if (opt_debug) printf("VPS label unrecognized in line \"%s\"\n", text.c_str());

         // replace all concealed text with blanks (may also be non-VPS related, e.g. HR3: "Un", "Ra" - who knows what this means to tell us)
         // FIXME text can also be concealed by setting fg := bg (e.g. on desc pages of MDR)
         static const regex expr6("^[^\\x00-\\x07\\x10-\\x17]*");
         if (regex_search(starti, text.end(), whati, expr6)) {
            str_blank(whati[0]);
         }
         if (opt_debug) printf("VPS label unrecognized in line \"%s\"\n", text.c_str());
      }
   }
}

/* ------------------------------------------------------------------------------
 * Chop footer
 * - footers usually contain advertisments (or more precisely: references
 *   to teletext pages with ads) and can simply be discarded
 *
 *   Schule. Dazu Arbeitslosigkeit von    
 *   bis zu 30 Prozent. Wir berichten ü-  
 *   ber Menschen in Ostdeutschland.  318>
 *    Ab in die Sonne!................500 
 *
 *   Teilnahmebedingungen: Seite 881      
 *     Türkei-Urlaub jetzt buchen! ...504 
 */
int ParseFooter(int page, int sub, int head)
{
   const TTX_DB_PAGE * pgtext = ttx_db.get_sub_page(page, sub);
   smatch whats;
   int foot;

   for (foot = 23 ; foot >= head; foot--) {
      // note missing lines are treated as empty lines
      const string& text = pgtext->get_text(foot);

      // stop at lines which look like separators
      static const regex expr1("^ *-{10,} *$");
      if (regex_search(text, whats, expr1)) {
         foot -= 1;
         break;
      }
      static const regex expr2("\\S");
      if (!regex_search(text, whats, expr2)) {
         static const regex expr3("[\\x0D\\x0F][^\\x0C]*[^ ]");
         if (!regex_search(pgtext->get_ctrl(foot - 1), whats, expr3)) {
            foot -= 1;
            break;
         }
         // ignore this empty line since there's double-height chars above
         continue;
      }
      // check for a teletext reference
      // TODO internationalize
      static const regex expr4("^ *(seite |page |<+ *)?[1-8][0-9][0-9]([^\\d]|$)", regex::icase);
      static const regex expr5("[^\\d][1-8][0-9][0-9]([\\.\\?!:,]|>+)? *$");
      //static const regex expr6("(^ *<|> *$)");
      if (   regex_search(text, whats, expr4)
          || regex_search(text, whats, expr5)) {
           //|| ((sub != 0) && regex_search(text, whats, expr6)) )
      }
      else {
         break;
      }
   }
   // plausibility check (i.e. don't discard the entire page)
   if (foot < 23 - 5) {
      foot = 23;
   }
   return foot;
}

/* Try to identify footer lines by a different background color
 * - bg color is changed by first changing the fg color (codes 0x0-0x7),
 *   then switching copying fg over bg color (code 0x1D)
 */
int ParseFooterByColor(int page, int sub)
{
   int ColCount[8] = {0};
   int LineCol[24] = {0};
   smatch whats;

   // check which color is used for the main part of the page
   // - ignore top-most header and one footer line
   // - ignore missing lines (although they would display black (except for
   //   double-height) but we don't know if they're intentionally missing)
   const TTX_DB_PAGE * pgtext = ttx_db.get_sub_page(page, sub);
   for (int line = 4; line <= 23; line++) {
      // get first non-blank char; skip non-color control chars
      static const regex expr1("^[^\\x21-\\x7F]*\\x1D");
      static const regex expr2("[\\x21-\\x7F]");
      static const regex expr3("[\\x0D\\x0F][^\\x0C]*[^ ]");

      if (regex_search(pgtext->get_ctrl(line), whats, expr1)) {
         int bg = str_bg_col(whats[0]);
         ColCount[bg] += 1;
         LineCol[line] = bg;
      }
      else if (   !regex_search(pgtext->get_ctrl(line), whats, expr2)
               && regex_search(pgtext->get_ctrl(line - 1), whats, expr3)) {
         // ignore this empty line since there's double-height chars above
         int bg = LineCol[line - 1];
         ColCount[bg] += 1;
         LineCol[line] = bg;
      }
      else {
         // background color unchanged
         ColCount[0] += 1;
         LineCol[line] = 0;
      }
   }

   // search most used color
   int max_idx = 0;
   for (int col_idx = 0; col_idx < 8; col_idx++) {
      if (ColCount[col_idx] > ColCount[max_idx]) {
         max_idx = col_idx;
      }
   }

   // count how many consecutive lines have this color
   // FIXME allow also use of several different colors in the middle and different ones for footer
   int max_count = 0;
   int count = 0;
   for (int line = 4; line <= 23; line++) {
      if (LineCol[line] == max_idx) {
         count += 1;
         if (count > max_count)
            max_count = count;
      }
      else {
         count = 0;
      }
   }

   // TODO: merge with other footer function: require TTX ref in every skipped segment with the same bg color

   // ignore last line if completely empty (e.g. ARTE: in-between double-hirhgt footer and FLOF)
   int last_line = 23;
   static const regex expr4("[\\x21-\\xff]");
   if (!regex_search(pgtext->get_ctrl(last_line), whats, expr4)) {
      --last_line;
   }

   // reliable only if 8 consecutive lines without changes, else don't skip any footer lines
   if (max_count >= 8) {
      // skip footer until normal color is reached
      for (int line = last_line; line >= 0; line--) {
         if (LineCol[line] == max_idx) {
            last_line = line;
            break;
         }
      }
   }
   //if (opt_debug) printf("FOOTER max-col:%d count:%d continuous:%d last_line:%d\n",
   //                      max_idx, ColCount[max_idx], max_count, last_line);

   return last_line;
}

/* ------------------------------------------------------------------------------
 * Remove garbage from line end in overview title lines
 * - KiKa sometimes uses the complete page for the overview and includes
 *   the footer text at the right side on a line used for the overview,
 *   separated by using a different background color
 *   " 09.45 / Dragon (7)        Weiter   >>> " (line #23)
 * - The same occurs on 3sat description pages:
 *   "\\x06Lorraine Nancy.\\x03      Mitwirkende >>>  "
 *   TODO: however with foreground colour intsead of BG
 * - note the cases with no text before the page ref is handled b the
 *   regular footer detection
 */
void RemoveTrailingPageFooter(string& text)
{
   match_results<string::iterator> whati;

   // look for a page reference or ">>" at line end
   static const regex expr1("^(.*[^[:alnum:]])([1-8][0-9][0-9]|>{1,4})[^\\x1D\\x21-\\xFF]*$");
   if (regex_search(text.begin(), text.end(), whati, expr1)) {
      int ref_off = whati[1].length();
      // check if the background color is changed
      static const regex expr2("^(.*)\\x1D[^\\x1D]+$");
      if (regex_search(text.begin(), text.begin() + ref_off, whati, expr2)) {
         ref_off = whati[1].length();
         // determine the background color of the reference (i.e. last used FG color before 1D)
         int ref_col = 7;
         static const regex expr3("^(.*)([\\x00-\\x07\\x10-\\x17])[^\\x00-\\x07\\x10-\\x17\\x1D]*$");
         if (regex_search(text.begin(), text.begin() + ref_off, whati, expr3)) {
            ref_col = text[whati.position(2)];
         }
         //print "       REF OFF:$ref_off COL:$ref_col\n";
         // determine the background before the reference
         bool matched = false;
         int txt_off = ref_off;
         if (regex_search(text.begin(), text.begin() + ref_off, whati, expr2)) {
            int tmp_off = whati[1].length();
            if (regex_search(text.begin(), text.begin() + tmp_off, whati, expr3)) {
               int txt_col = text[whati.position(2)];
               //print "       TXTCOL:$txt_col\n";
               // check if there's any text with this color
               static const regex expr4("[\\x21-\\xff]");
               if (regex_search(text.begin() + tmp_off, text.begin() + ref_off, whati, expr4)) {
                  matched = (txt_col != ref_col);
                  txt_off = tmp_off;
               }
            }
         }
         // check for text at the default BG color (unless ref. has default too)
         if (!matched && (ref_col != 7)) {
            static const regex expr5("[\\x21-\\xff]");
            matched = regex_search(text.begin(), text.begin() + txt_off, whati, expr5);
            //if (matched) print "       DEFAULT TXTCOL:7\n";
         }
         if (matched) {
            if (opt_debug) printf("OV removing trailing page ref\n");
            for (unsigned idx = ref_off; idx < text.size(); idx++) {
               text[idx] = ' ';
            }
         }
      }
   }
}

/* ------------------------------------------------------------------------------
 * Search for the line which contains the title string and return the line
 * number. Additionally, extract feature attributes from the lines holding the
 * title.  The comparison is done case-insensitive as some networks write the
 * titles in all upper-case.
 *
 * The title may span multiple lines, both in overview lists and the
 * description pages.  Often line-breaks are positioned differently. The latter
 * can be used to determine which lines actually hold the title and where
 * sub-titles begin (e.g. episode title). The algorithm below independently
 * adds lines from overview and description pages respectively until the
 * concatenated strings match up at a line end. The overview title is then
 * replaced with this corrected result. Examples:
 *
 *   Overview:                     Description page:
 * - Licht aus in Erichs           Licht aus in Erichs Lampenladen
 *   Lampenladen
 * - Battlefield Earth - Kampf     Battlefield Earth - Kampf um die
 *   um die Erde                   Erde
 * - K 2                           K2 - Das letzte Abenteuer
 *   Das letzte Abenteuer
 */
int OV_SLOT::parse_desc_title(int page, int sub)
{
   const TTX_DB_PAGE * pgctrl = ttx_db.get_sub_page(page, sub);

   string title_ov;
   unsigned first_subt = 1;
   title_ov = m_ov_title[0];
   while (   (m_ov_title.size() > first_subt)
          && (str_concat_title(title_ov, m_ov_title[first_subt], true)) ) {
      ++first_subt;
   }

   for (unsigned idx = 1; idx <= 23/2; idx++)
   {
      string title_desc = pgctrl->get_ctrl(idx);
      unsigned indent_desc = str_get_indent(title_desc);
      str_chomp(title_desc);

      unsigned dpos_ov, dpos_desc;
      if (str_len_alnum(title_desc) > 0)
      {
         str_cmp_alnum(title_ov, title_desc, &dpos_ov, &dpos_desc);
         if ((dpos_ov >= title_ov.length()) || (dpos_desc >= title_desc.length()))
         {
            unsigned idx_ov = first_subt;
            unsigned idx_desc = idx;
            bool ok = false;

            string title_ov_ext(title_ov);

            m_ext_feat.ParseTrailingFeat(title_desc);
            TV_FEAT ext_feat(m_ext_feat);

            do {
               if (dpos_desc < title_desc.length()) {
                  // title from overview page is shorter: try appending one more subtitle line
                  if (idx_ov >= m_ov_title.size()) {
                     // overview title is too short and no more sub-title lines following:
                     // allow if the following text is separated by ":" or similar
                     static const regex expr0("(\\: | \\- | \\& | \\()$");
                     match_results<string::iterator> whati;
                     if (regex_search(title_desc.begin(),
                                      title_desc.begin() + dpos_desc, whati, expr0)) {
                        // strip trailing non-alnum
                        title_desc.erase(whati[0].first, title_desc.end());
                        ok = true;
                        break;
                     }
                     ok = false;
                     break;
                  }
                  str_concat_title(title_ov_ext, m_ov_title[idx_ov++], false);
                  while (   (m_ov_title.size() > idx_ov)
                         && (str_concat_title(title_ov_ext, m_ov_title[idx_ov], true)) ) {
                     ++idx_ov;
                  }
               }
               else if (dpos_ov < title_ov_ext.length()) {
                  // title from description page is shorter: try appending one more text line
                  if (idx_desc >= 22) {
                     ok = false;
                     break;
                  }
                  string next = pgctrl->get_ctrl(++idx_desc);
                  if (str_get_indent(next) != indent_desc) {
                     ok = false;
                     break;
                  }
                  // put found features in temprary buffer - apply only if extended title matches
                  ext_feat.ParseTrailingFeat(next);
                  str_concat_title(title_desc, next, false);
               }

               str_cmp_alnum(title_ov_ext, title_desc, &dpos_ov, &dpos_desc);
               if ((dpos_ov < title_ov_ext.length()) && (dpos_desc < title_desc.length())) {
                  ok = false;
                  break;
               }
               ok = true;
            } while ((dpos_ov < title_ov_ext.length()) || (dpos_desc < title_desc.length()));

            if (ok) {
               if (opt_debug && ((idx_ov > first_subt) || (idx_desc > idx)))
                  printf("DESC title adjusted by %d sub-title and %d desc. page title lines: \"%s\"\n",
                         idx_ov - first_subt, idx_desc - idx, title_ov_ext.c_str());
               // replace the overview title with the longer one of the concatenated title strings
               // because sometimes one of them is missing a " - " before title extentions
               if (title_ov_ext.length() >= title_desc.length())
                  m_ext_title.assign(title_ov_ext);
               else
                  m_ext_title.assign(title_desc);
               // erase possible trailing ":" or "," (may be left from cutting off feature tags)
               if (m_ext_title.length() > 0) {
                  string::iterator p = m_ext_title.begin() + m_ext_title.length() - 1;
                  if ((*p == ':') || (*p == ',') || (*p == '&') || (*p == '-')) {
                     m_ext_title.erase(p, m_ext_title.end());
                     str_chomp(m_ext_title);
                  }
               }
               // re-build the sub-title string from unused overview lines
               m_ext_subtitle.assign("");
               for (unsigned idx2 = idx_ov; idx2 < m_ov_title.size(); idx2++) {
                  str_concat_title(m_ext_subtitle, m_ov_title[idx2], false);
               }
               str_chomp(m_ext_subtitle);
               m_ext_feat = ext_feat;
            } else {
               idx_desc = idx;
            }
            // extract feature attributes from the next line too (may be sub-title text)
            if (idx_desc < 23) {
               string next = pgctrl->get_ctrl(idx_desc + 1);
               if (str_get_indent(next) == indent_desc) {
                  m_ext_feat.ParseTrailingFeat(next);
               }
            }

            return idx;
         }
      }
   }
   //print "NOT FOUND title\n";
   return 1;
}

/* ------------------------------------------------------------------------------
 * Compare two text pages line-by-line until a difference is found.
 * Thus identical lines on sub-pages can be excluded from descriptions.
 */
int CorrelateDescTitles(int page, int sub1, int sub2, int head)
{
   const TTX_DB_PAGE * pgctrl1 = ttx_db.get_sub_page(page, sub1);
   const TTX_DB_PAGE * pgctrl2 = ttx_db.get_sub_page(page, sub2);

   for (unsigned line = head; line < TTX_DB_PAGE::TTX_TEXT_LINE_CNT; line++) {
      const string& p1 = pgctrl1->get_ctrl(line);
      const string& p2 = pgctrl2->get_ctrl(line);
      unsigned cnt = 0;
      for (unsigned col = 0; col < VT_PKG_RAW_LEN; col++) {
         if (p1[col] == p2[col])
            cnt++;
      }
      // stop if lines are not similar enough
      if (cnt < VT_PKG_RAW_LEN - VT_PKG_RAW_LEN/10) {
         return line;
      }
   }
   return TTX_DB_PAGE::TTX_TEXT_LINE_CNT;
}

/* ------------------------------------------------------------------------------
 * Replace page index markers in description pages with space; the marker
 * is expected at the end of one of the top lines of the page only
 * (e.g. "...     2/2 "). Note currently not handled: providers
 * might mark a page with "1/1" when sub-pages differ by ads only.
 */
void DescRemoveSubPageIdx(vector<string>& Lines, int sub)
{
   smatch whats;

   for (unsigned row = 0; (row < Lines.size()) && (row < 6); row++) {
      static const regex expr1(" (\\d+)/(\\d+) {0,2}$");
      if (regex_search(Lines[row], whats, expr1)) {
         int sub_idx = atoi_substr(whats[1]);
         int sub_cnt = atoi_substr(whats[2]);
         if ((sub == 0)
               ? ((sub_idx == 1) && (sub_cnt == 1))
               : ((sub_idx == sub) && (sub_cnt > 1)) )
         {
            Lines[row].replace(whats.position(), whats[0].length(), whats[0].length(), ' ');
            break;
         }
      }
   }
}

/* ------------------------------------------------------------------------------
 * Reformat tables in "cast" format into plain lists. Note this function
 * must be called before removing control characters so that columns are
 * still aligned. Example:
 *
 * Dr.Hermann Seidel .. Heinz Rühmann
 * Vera Bork .......... Wera Frydtberg
 * Freddy Blei ........ Gert Fröbe
 * OR
 * Regie..............Ute Wieland
 * Produktion.........Ralph Schwingel,
 *                    Stefan Schubert
 */
bool DescFormatCastTable(vector<string>& Lines)
{
   smatch whats;
   bool result = false;

   // step #1: build statistic about lines with ".." (including space before and after)
   map<int,int> Tabs;
   unsigned tab_max = 0;
   for (unsigned row = 0; row < Lines.size(); row++) {
      static const regex expr1("^( *)((.*?)( ?)\\.\\.+( ?))[^ ].*?[^ ]( *)$");
      if (regex_search(Lines[row], whats, expr1)) {
         // left-aligned (ignore spacing on the right)
         unsigned rec = whats[1].length() |
                    ((whats[1].length() + whats[2].length()) << 6)|
                    (whats[4].length() << 12) |
                    (whats[5].length() << 18) |
                    (0x3F << 24);
         Tabs[rec]++;
         if ((tab_max == 0) || (Tabs[tab_max] < Tabs[rec]))
            tab_max = rec;

         // right-aligned (ignore spacing on the left)
         rec = whats[1].length() |
               (0x3F << 6) |
               (whats[4].length() << 12) |
               (whats[5].length() << 18) |
               (whats[6].length() << 24);
         Tabs[rec]++;
         if ((tab_max == 0) || (Tabs[tab_max] < Tabs[rec]))
            tab_max = rec;
      }
   }

   // minimum is two lines looking like table rows
   if ((tab_max != 0) && (Tabs[tab_max] >= 2)) {
      const unsigned spc0 = tab_max & 0x3F;
      const unsigned off  = (tab_max >> 6) & 0x3F;
      const unsigned spc1 = (tab_max >> 12) & 0x3F;
      const unsigned spc2 = (tab_max >> 18) & 0x3F;
      const unsigned spc3 = tab_max >> 24;

      regex expr2;
      if (spc3 == 0x3F) {
         expr2.assign(string("^") + string(spc0, ' ') +
                      string("([^ ].*?)") + string(spc1, ' ') +
                      string("(\\.*)") + string(spc2, ' ') + "[^ \\.]$");
      } else {
         expr2.assign(string("^(.*?)") + string(spc1, ' ') +
                      string("(\\.+)") + string(spc2, ' ') +
                      string("([^ \\.].*?)") + string(spc3, ' ') + "$");
      }
      // Special handling for lines with overlong names on left or right side:
      // only for lines inside of table; accept anything which looks like a separator
      static const regex expr3("^(.*?)\\.\\.+ ?[^ \\.]");
      static const regex expr4("^(.*?[^ ]) \\. [^ \\.]");  // must not match "Mr. X"

      if (opt_debug) printf("DESC reformat table into list: %d rows, FMT:%d,%d %d,%d EXPR:%s\n",
                            Tabs[tab_max], off, spc1, spc2, spc3, expr2.str().c_str());

      // step #2: find all lines with dots ending at the right column and right amount of spaces
      int first_row = -1;
      int last_row = -1;
      for (unsigned row = 0; row < Lines.size(); row++) {
         if (spc3 == 0x3F) {
            //
            // 2nd column is left-aligned
            //
            if (   (   regex_search(Lines[row].substr(0, off + 1), whats, expr2)
                    && ((last_row != -1) || (whats[2].length() > 0)) )
                || ((last_row != -1) && regex_search(Lines[row], whats, expr3))
                || ((last_row != -1) && regex_search(Lines[row], whats, expr4)) ) {
               string tab1 = Lines[row].substr(spc0, whats[1].length());
               string tab2 = Lines[row].substr(whats[0].length() - 1); // for expr3 or 4, else it's fixed to "off"
               // match -> replace dots with colon
               tab1 = regex_replace(tab1, regex("[ :]*$"), "");
               tab2 = regex_replace(tab2, regex("[ ,]*$"), "");
               if (tab1 == ".") {
                  // left column is empty
                  Lines[row] = tab2 + string(",");
               }
               else {
                  Lines[row] = tab1 + string(": ") + tab2 + string(",");
               }
               last_row = row;
               if (first_row == -1)
                  first_row = row;
            }
            else if (last_row != -1) {
               static const regex expr5("^ +[^ ]$");
               if (regex_search(Lines[row].substr(0, off + 1), whats, expr5)) {
                  // right-side table column continues (left side empty)
                  Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), "");
                  string tab2 = regex_replace(Lines[row].substr(off), regex(",? +$"), "");
                  Lines[row] = tab2 + ",";
                  last_row = row;
               }
            }
         } else {
            //
            // 2nd column is right-aligned
            //
            if (regex_search(Lines[row], whats, expr2)) {
               string tab1 = string(whats[1]);
               string tab2 = Lines[row].substr(whats.position(3));
               // match -> replace dots with colon
               tab1 = regex_replace(tab1, regex(" *:$"), "");
               tab2 = regex_replace(tab2, regex(",? +$"), "");
               if (tab1 == ".") {
                  // left column is empty
                  Lines[row] = tab2 + string(",");
               }
               else {
                  Lines[row] = tab1 + string(": ") + tab2 + string(",");
               }
               last_row = row;
               if (first_row == -1)
                  first_row = row;
            }
         }

         if ((last_row != -1) && (last_row < (int)row)) {
            // end of table: terminate list
            Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), ".");
            last_row = -1;
         }
      }
      if (last_row != -1) {
         Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), "");
      }
      if (first_row >= 2) {
         // remove paragraph break after table header (e.g. "Cast:")
         static const regex expr6("^\\s*$");
         static const regex expr7(":\\s*$");
         if (   regex_search(Lines[first_row - 1], whats, expr6)
             && regex_search(Lines[first_row - 2], whats, expr7)) {
            // remove the empty line (Warning: this invalidates line index ref's)
            Lines.erase(Lines.begin() + first_row - 1);
         }
      }
      result = true;
   }
   return result;
}

string ParseDescContent(int page, int sub, int head, int foot)
{
   T_VPS_TIME vps_data;
   smatch whats;
   vector<string> Lines;

   const TTX_DB_PAGE * pgctrl = ttx_db.get_sub_page(page, sub);
   bool is_nl = 0;
   string desc;

   for (int idx = head; idx <= foot; idx++) {
      string ctrl = pgctrl->get_ctrl(idx);

      // TODO parse features behind date, title or subtitle

      // extract and remove VPS labels and all concealed text
      ParseVpsLabel(ctrl, pgctrl->get_ctrl(idx - 1), vps_data, true);

      static const regex expr2("[\\x00-\\x1F\\x7F]");
      ctrl = regex_replace(ctrl, expr2, " ");

      // remove VPS time and date labels
      static const regex expr3("^ +[0-9A-F]{2}\\d{3} (\\d{2})(\\d{2})(\\d{2}) [0-9A-F]{2} *$");
      static const regex expr4(" +VPS \\d{4} *$");
      ctrl = regex_replace(ctrl, expr3, "");
      ctrl = regex_replace(ctrl, expr4, "");

      Lines.push_back(ctrl);
   }

   DescRemoveSubPageIdx(Lines, sub);
   DescFormatCastTable(Lines);

   for (unsigned idx = 0; idx < Lines.size(); idx++) {
      // TODO: replace 0x7f (i.e. square) at line start with "-"
      // line consisting only of "-": treat as paragraph break
      string line = Lines[idx];
      static const regex expr5("^ *[\\-_\\+]{20,} *$");
      line = regex_replace(line, regex(expr5), "");

      static const regex expr6("[^ ]");
      if (regex_search(line, whats, expr6)) {
         static const regex expr8("(^ +| +$)");
         line = regex_replace(line, expr8, "");

         static const regex expr9("  +");
         line = regex_replace(line, expr9, " ");

         if (is_nl)
            desc += "\n";
         static const regex expr12("\\S-$");
         if (   regex_search(desc, whats, expr12)
             && (line.size() > 0) && islower_latin1(line[0]))
         {
            desc.erase(desc.end() - 1);
            desc += line;
         }
         else if ((desc.length() > 0) && !is_nl) {
            desc += " ";
            desc += line;
         }
         else {
            desc += line;
         }
         is_nl = false;
      } else {
         // empty line: paragraph break
         if (desc.length() > 0) {
            is_nl = true;
         }
      }
   }
   // TODO: also cut off ">314" et.al. (but not ref. to non-TV pages)
   static const regex expr14("(>>+ *| +)$");
   if (regex_search(desc, whats, expr14)) {
      desc.erase(whats.position());
   }
   return desc;
}

/* TODO: check for all referenced text pages where no match was found if they
 *       describe a yet unknown programme (e.g. next instance of a weekly series)
 */
void OV_SLOT::parse_desc_page(const T_PG_DATE * pg_date, int ref_count)
{
   bool found = false;
   int first_sub = -1;
   int page = m_ttx_ref;

   if (opt_debug) {
      char buf[100];
      strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", localtime(&m_start_t));
      printf("DESC parse page %03X: search match for %s \"%s\"\n",
             page, buf, m_ov_title[0].c_str());
   }

   for (TTX_DB::const_iterator p = ttx_db.first_sub_page(page);
           p != ttx_db.end();
              ttx_db.next_sub_page(page, p))
   {
      int sub = p->first.sub();

      // TODO: multiple sub-pages may belong to same title, but have no date
      //       caution: multiple pages may also have the same title, but describe different instances of a series

      // TODO: bottom of desc pages may contain a 2nd date/time for repeats (e.g. SAT.1: "Whg. Fr 05.05. 05:10-05:35") - note the different BG color!

      if (   pg_date->ParseDescDate(page, sub, m_start_t, m_date_wrap)
          || (ref_count <= 1)) {

         int head = parse_desc_title(page, sub);
         if (first_sub >= 0)
            head = CorrelateDescTitles(page, sub, first_sub, head);
         else
            first_sub = sub;

         int foot = ParseFooterByColor(page, sub);
         int foot2 = ParseFooter(page, sub, head);
         foot = (foot2 < foot) ? foot2 : foot;
         if (opt_debug) printf("DESC page %03X.%04X title matched: lines %d-%d\n", page, sub, head, foot);

         if (foot > head) {
            if (!m_ext_desc.empty())
               m_ext_desc += "\n";
            m_ext_desc += ParseDescContent(page, sub, head, foot);
         }
         found = true;
      }
      else {
         if (opt_debug) printf("DESC page %03X.%04X no match found (%d references)\n", page, sub, ref_count);
         if (found)
            break;
      }
   }
   if (!found && opt_debug) printf("DESC page %03X not found\n", page);
}

/* TODO  SWR:
 *  21.58  2158  Baden-Württemberg Wetter
 *          VPS   (bis 22.00 Uhr)
 * TODO ARD
 *  13.00  ARD-Mittagsmagazin ....... 312
 *         mit Tagesschau
 *    VPS  bis 14.00 Uhr
 */
bool OV_PAGE::parse_end_time(const string& text, const string& ctrl, int& hour, int& min)
{
   smatch whats;
   bool result = false;

   // check if last line specifies and end time
   // (usually last line of page)
   // TODO internationalize "bis", "Uhr"
   static const regex expr8("^ *(\\(?bis |ab |\\- ?)(\\d{1,2})[\\.:](\\d{2})"
                            "( Uhr| ?h)( |\\)|$)");
   // FIXME should acceppt this only after title_off
   static const regex expr9("^([\\x00-\\x07\\x18 ]*)(\\d\\d)[\\.:](\\d\\d)([\\x00-\\x07\\x18 ]+oo)?[\\x00-\\x07\\x18 ]*$");
   if (   regex_search(text, whats, expr8)   // ARD,SWR,BR-alpha
       || (   regex_search(ctrl, whats, expr9)  // arte, RTL
           && (str_fg_col(ctrl.begin(), ctrl.begin() + whats.position(2)) != 5)) ) // KiKa VPS label across 2 lines
   {
      hour = atoi_substr(whats[2]);
      min = atoi_substr(whats[3]);

      if (opt_debug) printf("Overview end time: %02d:%02d\n", hour, min);
      result = true;
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Parse date in an overview page
 * - similar to dates on description pages
 */
bool OV_PAGE::parse_ov_date()
{
   return m_date.ParseOvDate(m_page, m_sub, m_head);
}

/* ------------------------------------------------------------------------------
 * Retrieve programme entries from an overview page
 * - the layout has already been determined in advance, i.e. we assume that we
 *   have a tables with strict columns for times and titles; rows that don't
 *   have the expected format are skipped (normally only header & footer)
 */
bool OV_PAGE::parse_slots(int foot_line, const T_OV_LINE_FMT& pgfmt)
{
   T_VPS_TIME vps_data;
   OV_SLOT * ov_slot = 0;

   const TTX_DB_PAGE * pgctrl = ttx_db.get_sub_page(m_page, m_sub);
   vps_data.m_new_vps_date = false;

   for (int line = 1; line <= 23; line++) {
      // note: use text including control-characters, because the next 2 steps require these
      string ctrl = pgctrl->get_ctrl(line);

      // extract and remove VPS labels
      // (labels not asigned here since we don't know yet if a new title starts in this line)
      vps_data.m_new_vps_time = false;
      ParseVpsLabel(ctrl, pgctrl->get_ctrl(line - 1), vps_data, false);

      // remove remaining control characters
      string text = ctrl;
      str_repl_ctrl(text);

      bool is_tip = false;
      int hour = -1;
      int min = -1;

      if ( pgfmt.parse_title_line(text, hour, min, is_tip) ) {
         // remember end of page header for date parser
         if (m_head < 0)
            m_head = line;

         if (opt_debug) printf("OV TITLE: \"%s\", %02d:%02d\n", pgfmt.extract_title(text).c_str(), hour, min);

         ov_slot = new OV_SLOT(hour, min, is_tip);
         m_slots.push_back(ov_slot);

         ov_slot->add_title(pgfmt.extract_title(ctrl));

         if (vps_data.m_new_vps_time) {
            ov_slot->m_vps_time = vps_data.m_vps_time;
         }
         if (vps_data.m_new_vps_date) {
            ov_slot->m_vps_date = vps_data.m_vps_date;
            vps_data.m_new_vps_date = false;
         }
         //ov_slot->m_vps_cni = vps_data.m_vps_cni; // currently unused

         //printf("ADD  %02d.%02d.%d %02d:%02d %s\n", ov_slot->m_mday, ov_slot->m_month, ov_slot->m_year, ov_slot->m_hour, ov_slot->m_min, ov_slot->m_ov_title.c_str());
      }
      else if (ov_slot != 0) {
         // stop appending subtitles before page footer ads
         if (line > foot_line)
            break;

         if (vps_data.m_new_vps_time)
            ov_slot->m_vps_time = vps_data.m_vps_time;

         //FIXME normally only following the last slot on the page
         if (parse_end_time(text, ctrl, ov_slot->m_end_hour, ov_slot->m_end_min)) {
            // end of this slot
            ov_slot = 0;
         }
         // check if we're still in a continuation of the previous title
         // time column must be empty (possible VPS labels were already removed above)
         else if (pgfmt.parse_subtitle(text)) {
            ov_slot->add_title(pgfmt.extract_subtitle(ctrl));

            if (vps_data.m_new_vps_date) {
               ov_slot->m_vps_date = vps_data.m_vps_date;
               vps_data.m_new_vps_date = false;
            }
         }
         else {
            ov_slot = 0;
         }
      }
   }
   return m_slots.size() > 0;
}

/* ------------------------------------------------------------------------------
 * Check if an overview page has exactly the same title list as the predecessor
 * (some networks use subpages just to display different ads)
 */
bool OV_PAGE::check_redundant_subpage(OV_PAGE * prev)
{
   bool result = false;

   if (   (m_page == prev->m_page)
       && (m_sub > 1)
       && (m_sub == prev->m_sub + prev->m_sub_page_skip + 1) )
   {
      // FIXME check for overlap in a more general way (allow for missing line on one page? but then we'd need to merge)
      if (prev->m_slots.size() == m_slots.size()) {
         result = true;
         for (unsigned idx = 0; idx < m_slots.size(); idx++) {
            if (!m_slots[idx]->is_same_prog(*prev->m_slots[idx])) {
               result = false;
               break;
            }
         }
         if (result) {
            if (opt_debug) printf("OV_PAGE 0x%3X.%d dropped: redundant to sub-page %d\n", m_page, m_sub, prev->m_sub);
            prev->m_sub_page_skip += 1;
         }
      }
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Calculate number of page following the previous one
 * - basically that's a trivial increment by 1, however page numbers are
 *   actually hexadecimal numbers, where the numbers with non-decimal digits
 *   are skipped (i.e. not intended for display)
 * - hence to get from 9 to 0 in the 2nd and 3rd digit we have to add 7
 *   instead of just 1.
 */
int GetNextPageNumber(int page)
{
   int next;

   if ((page & 0xF) == 9) {
      next = page + (0x010 - 0x009);
   }
   else {
      next = page + 1;
   }
   // same skip for the 2nd digit, if there was a wrap in the 3rd digit
   if ((next & 0xF0) == 0x0A0) {
      next = next + (0x100 - 0x0A0);
   }
   return next;
}

/* ------------------------------------------------------------------------------
 * Check if two given teletext pages are adjacent
 * - both page numbers must have decimal digits only (i.e. match /[1-8][1-9][1-9]/)
 */
bool CheckIfPagesAdjacent(int page1, int sub1, int sub_skip, int page2, int sub2)
{
   bool result = false;

   if ( (page1 == page2) &&
        (sub1 + sub_skip + 1 == sub2) ) {
      // next sub-page on same page
      result = true;
   }
   else {
      // check for jump from last sub-page of prev page to first of new page
      int next = GetNextPageNumber(page1);

      int last_sub = ttx_db.last_sub_page_no(page1);

      if ( (next == page2) &&
           (sub1 + sub_skip == last_sub) &&
           (sub2 <= 1) ) {
         result = true;
      } 
   }
   return result;
}

bool OV_PAGE::is_adjacent(const OV_PAGE * prev) const
{
   return CheckIfPagesAdjacent(prev->m_page, prev->m_sub, prev->m_sub_page_skip, m_page, m_sub);
}

/* ------------------------------------------------------------------------------
 * Determine dates of programmes on an overview page
 * - TODO: arte: remove overlapping: the first one encloses multiple following programmes; possibly recognizable by the VP label 2500
 *  "\x1814.40\x05\x182500\x07\x07THEMA: DAS \"NEUE GROSSE   ",
 *  "              SPIEL\"                    ",
 *  "\x0714.40\x05\x051445\x02\x02USBEKISTAN - ABWEHR DER   ",
 *  "            \x02\x02WAHABITEN  (2K)           ",
 */
bool OV_PAGE::calc_date_off(const OV_PAGE * prev)
{
   bool result = false;

   int date_off = 0;
   if ((prev != 0) && (prev->m_slots.size() > 0)) {
      const OV_SLOT * prev_slot1 = prev->m_slots[0];
      //FIXME const OV_SLOT * prev_slot2 = prev->m_slots[prev->m_slots.size() - 1];
      // check if the page number of the previous page is adjacent
      // and as consistency check require that the prev page covers less than a day
      if (   /* (prev_slot2->m_start_t - prev_slot1->m_start_t < 22*60*60) // TODO/FIXME
          &&*/ is_adjacent(prev))
      {
         int prev_delta = 0;
         // check if there's a date on the current page
         // if yes, get the delta to the previous one in days (e.g. tomorrow - today = 1)
         // if not, silently fall back to the previous page's date and assume date delta zero
         if (m_date.is_valid()) {
            prev_delta = prev->m_date.calc_date_delta(m_date);
         }
         if (prev_delta == 0) {
            // check if our start date should be different from the one of the previous page
            // -> check if we're starting on a new date (smaller hour)
            // (note: comparing with the 1st slot of the prev. page, not the last!)
            if (m_slots[0]->m_hour < prev_slot1->m_hour) {
               // TODO: check continuity to slot2: gap may have 6h max.
               // TODO: check end hour
               date_off = 1;
            }
         }
         else {
            if (opt_debug) printf("OV DATE %03X.%d: prev page %03X.%04X date cleared\n",
                                  m_page, m_sub, prev->m_page, prev->m_sub);
            prev = 0;
         }
         // TODO: date may also be wrong by +1 (e.g. when starting at 23:55 with date for 00:00)
      }
      else {
         // not adjacent -> disregard the info
         if (opt_debug) printf("OV DATE %03X.%d: prev page %03X.%04X not adjacent - not used for date check\n",
                               m_page, m_sub, prev->m_page, prev->m_sub);
         prev = 0;
      }
   }

   if (m_date.is_valid()) {
      // store date offset in page meta data (to calculate delta of subsequent pages)
      m_date.add_offset(date_off);
      if (opt_debug) printf("OV DATE %03X.%d: using page header date %s\n", m_page, m_sub, m_date.trace_str());
      result = true;
   }
   else if (prev != 0) {
      // copy date from previous page
      m_date = prev->m_date;
      // add date offset if a date change was detected
      m_date.add_offset(date_off);

      if (opt_debug) printf("OV DATE %03X.%d: using predecessor date %s\n", m_page, m_sub, m_date.trace_str());
      result = true;
   }
   else {
      if (opt_debug) printf("OV %03X.%d missing date - discarding programmes\n", m_page, m_sub);
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Calculate exact start time for each title: Based on page date and HH::MM
 * - date may wrap inside of the page
 */
void OV_PAGE::calculate_start_times()
{
   int date_off = 0;
   int prev_hour = -1;

   for (unsigned idx = 0; idx < m_slots.size(); idx++) {
      OV_SLOT * slot = m_slots[idx];

      // detect date change (hour wrap at midnight)
      if ((prev_hour != -1) && (prev_hour > slot->m_hour)) {
         date_off += 1;
      }
      prev_hour = slot->m_hour;

      slot->m_start_t = slot->convert_start_t(&m_date, date_off);
      slot->m_date_wrap = date_off;
   }
}

/* ------------------------------------------------------------------------------
 * Check plausibility of times on a page
 * - there are sometimes pages with times in reverse order (e.g. with ratings)
 *   these cause extreme overlaps as all titles will cover 22 to 23 hours
 */
bool OV_PAGE::check_start_times()
{
   int date_wraps = 0;
   int prev_hour = -1;
   int prev_min = -1;

   for (unsigned idx = 0; idx < m_slots.size(); idx++) {
      OV_SLOT * slot = m_slots[idx];

      // detect date change (hour wrap at midnight)
      if (   (prev_hour != -1)
          && (   (prev_hour > slot->m_hour)
              || (   (prev_hour == slot->m_hour)
                  && (prev_min > slot->m_min) )))  // allow ==
      {
         ++date_wraps;
      }
      prev_hour = slot->m_hour;
      prev_min = slot->m_min;
   }
   bool result = date_wraps < 2;

   if (opt_debug && !result) printf("DROP PAGE %03X.%d: %d date wraps\n", m_page, m_sub, date_wraps);
   return result;
}

/* ------------------------------------------------------------------------------
 * Determine stop times
 * - assuming that in overview tables the stop time is equal to the start of the
 *   following programme & that this also holds true inbetween adjacent pages
 * - if in doubt, leave it undefined (this is allowed in XMLTV)
 * - TODO: restart at non-adjacent text pages
 */
void OV_PAGE::calc_stop_times(const OV_PAGE * next)
{
   for (unsigned idx = 0; idx < m_slots.size(); idx++)
   {
      OV_SLOT * slot = m_slots[idx];

      if (slot->m_end_min >= 0) {
         // there was an end time in the overview or a description page -> use that
         struct tm tm = *localtime(&slot->m_start_t);

         tm.tm_min = slot->m_end_min;
         tm.tm_hour = slot->m_end_hour;

         // check for a day break between start and end
         if ( (slot->m_end_hour < slot->m_hour) ||
              ( (slot->m_end_hour == slot->m_hour) &&
                (slot->m_end_min < slot->m_min) )) {
            tm.tm_mday += 1; // possible wrap done by mktime()
         }
         slot->m_stop_t = mktime(&tm);

         if (opt_debug) printf("OV_SLOT %02d:%02d use end time %02d:%02d - %s",
                               slot->m_hour, slot->m_min, slot->m_end_hour, slot->m_end_min, ctime(&slot->m_stop_t));
      }
      else if (idx + 1 < m_slots.size()) {
         OV_SLOT * next_slot = m_slots[idx + 1];

         if (next_slot->m_start_t != slot->m_start_t) {
            slot->m_stop_t = next_slot->m_start_t;
         }

         if (opt_debug) printf("OV_SLOT %02d:%02d ends at next start time %s",
                               slot->m_hour, slot->m_min, ctime(&next_slot->m_start_t));
      }
      else if (   (next != 0)
               && (next->m_slots.size() > 0)
               && next->is_adjacent(this) )
      {
         OV_SLOT * next_slot = next->m_slots[0];

         // no end time: use start time of the following programme if less than 9h away
         if (   (next_slot->m_start_t > slot->m_start_t)
             && (next_slot->m_start_t - slot->m_start_t < 9*60*60) )
         {
            slot->m_stop_t = next_slot->m_start_t;
         }
         if (opt_debug) printf("OV_SLOT %02d:%02d ends at next page %03X.%d start time %s",
                               slot->m_hour, slot->m_min, next->m_page, next->m_sub,
                               ctime(&next_slot->m_start_t));
      }
   }
}

/* ------------------------------------------------------------------------------
 * Detect position and leading garbage for description page references
 */
T_TRAIL_REF_FMT OV_PAGE::detect_ov_ttx_ref_fmt(const vector<OV_PAGE*>& ov_pages)
{
   vector<T_TRAIL_REF_FMT> fmt_list;

   // parse all slot titles for TTX reference format
   for (unsigned pg_idx = 0; pg_idx < ov_pages.size(); pg_idx++) {
      OV_PAGE * ov_page = ov_pages[pg_idx];
      for (unsigned slot_idx = 0; slot_idx < ov_page->m_slots.size(); slot_idx++) {
         OV_SLOT * slot = ov_page->m_slots[slot_idx];
         slot->detect_ttx_ref_fmt(fmt_list);
      }
   }
   return T_TRAIL_REF_FMT::select_ttx_ref_fmt(fmt_list);
}

void OV_PAGE::extract_ttx_ref(const T_TRAIL_REF_FMT& fmt, map<int,int>& ttx_ref_map)
{
   for (unsigned idx = 0; idx < m_slots.size(); idx++) {
      OV_SLOT * slot = m_slots[idx];
      slot->parse_ttx_ref(fmt, ttx_ref_map);
   }
}

void OV_PAGE::extract_tv(map<int,int>& ttx_ref_map)
{
   if (m_slots.size() > 0) {
      RemoveTrailingPageFooter(m_slots.back()->m_ov_title.back());
   }

   for (unsigned idx = 0; idx < m_slots.size(); idx++) {
      OV_SLOT * slot = m_slots[idx];

      slot->parse_feature_flags();

      slot->parse_ov_title();

      if (slot->m_ttx_ref != -1) {
         slot->parse_desc_page(&m_date, ttx_ref_map[slot->m_ttx_ref]);
      }
   }
}

/* ------------------------------------------------------------------------------
 * Retrieve programme data from an overview page
 * - 1: compare several overview pages to identify the layout
 * - 2: parse all overview pages, retrieving titles and ttx references
 *   + a: retrieve programme list (i.e. start times and titles)
 *   + b: retrieve date from the page header
 *   + c: determine dates
 *   + d: determine stop times
 */
vector<OV_PAGE*> ParseAllOvPages(int ov_start, int ov_end)
{
   vector<OV_PAGE*> ov_pages;

   T_OV_LINE_FMT fmt = DetectOvFormat(ov_start, ov_end);
   if (fmt.is_valid()) {

      for (TTX_DB::const_iterator p = ttx_db.begin(); p != ttx_db.end(); p++)
      {
         int page = p->first.page();
         int sub = p->first.sub();

         if ((page >= ov_start) && (page <= ov_end)) {
            if (opt_debug) printf("OVERVIEW PAGE %03X.%04X\n", page, sub);

            OV_PAGE * ov_page = new OV_PAGE(page, sub);

            int foot = ParseFooterByColor(page, sub);

            if (ov_page->parse_slots(foot, fmt)) {
               if (ov_page->check_start_times()) {
                  ov_page->parse_ov_date();

                  if (   (ov_pages.size() == 0)
                      || !ov_page->check_redundant_subpage(ov_pages.back())) {

                     ov_pages.push_back(ov_page);
                     ov_page = 0;
                  }
               }
            }
            delete ov_page;
         }
      }

      for (unsigned idx = 0; idx < ov_pages.size(); ) {
         if (ov_pages[idx]->calc_date_off((idx > 0) ? ov_pages[idx - 1] : 0)) {

            ov_pages[idx]->calculate_start_times();
            idx++;
         }
         else {
            delete ov_pages[idx];
            ov_pages.erase(ov_pages.begin() + idx);
         }
      }

      // guess missing stop times for the current page
      // (requires start times for the next page)
      for (unsigned idx = 0; idx < ov_pages.size(); idx++) {
         OV_PAGE * next = (idx + 1 < ov_pages.size()) ? ov_pages[idx + 1] : 0;
         ov_pages[idx]->calc_stop_times(next);
      }

      // retrieve TTX page references
      T_TRAIL_REF_FMT ttx_ref_fmt = OV_PAGE::detect_ov_ttx_ref_fmt(ov_pages);
      map<int,int> ttx_ref_map;
      for (unsigned idx = 0; idx < ov_pages.size(); idx++) {
         ov_pages[idx]->extract_ttx_ref(ttx_ref_fmt, ttx_ref_map);
      }

      // retrieve descriptions from referenced teletext pages
      for (unsigned idx = 0; idx < ov_pages.size(); idx++) {
         ov_pages[idx]->extract_tv(ttx_ref_map);
      }
   }

   return ov_pages;
}

list<TV_SLOT> OV_PAGE::get_ov_slots(vector<OV_PAGE*> ov_pages)
{
   list<TV_SLOT> tv_slots;

   for (unsigned idx = 0; idx < ov_pages.size(); idx++) {
      for (unsigned slot_idx = 0; slot_idx < ov_pages[idx]->m_slots.size(); slot_idx++) {
         tv_slots.push_back(TV_SLOT(ov_pages[idx], slot_idx));
      }
   }
   return tv_slots;
}

/* ------------------------------------------------------------------------------
 * Filter out expired programmes
 * - the stop time is relevant for expiry (and this really makes a difference if
 *   the expiry time is low (e.g. 6 hours) since there may be programmes exceeding
 *   it and we certainly shouldn't discard programmes which are still running
 * - resulting problem: we don't always have the stop time
 */
void FilterExpiredSlots(list<TV_SLOT>& Slots, int expire_min)
{
   time_t exp_thresh = time(NULL) - expire_min * 60;

   if (!Slots.empty()) {
      for (list<TV_SLOT>::iterator p = Slots.begin(); p != Slots.end(); ) {
         if (   (   ((*p).get_stop_t() != -1)
                 && ((*p).get_stop_t() >= exp_thresh))
             || ((*p).get_start_t() + 120*60 >= exp_thresh) )
         {
            ++p;
         }
         else {
            if (opt_debug) printf("EXPIRED new %ld-%ld < %ld '%s'\n", (long)(*p).get_start_t(),
                                  (long)(*p).get_stop_t(), (long)exp_thresh, (*p).get_title().c_str());
            Slots.erase(p++);
         }
      }
      if (Slots.empty()) {
         fprintf(stderr, "Warning: all newly acquired programmes are already expired\n");
      }
   }
}


