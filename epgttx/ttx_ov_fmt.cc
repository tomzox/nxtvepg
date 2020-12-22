/*
 * Teletext EPG grabber: Overview list format parser
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

#include <string>
#include <map>

#include <regex>

using namespace std;

#include "ttx_util.h"
#include "ttx_db.h"
#include "ttx_ov_fmt.h"

const char * T_OV_LINE_FMT::print_key() const
{
   static char buf[100];
   sprintf(buf, "time:%d,vps:%d,title:%d,title2:%d,MMHH-sep:'%c'",
           m_time_off, m_vps_off, m_title_off, m_subt_off, m_separator);
   return buf;
}


/* ! 20.15  Eine Chance für die Liebe UT         ARD
 *           Spielfilm, D 2006 ........ 344
 *                     ARD-Themenwoche Krebs         
 *
 * 19.35  1925  WISO ................. 317       ZDF
 * 20.15        Zwei gegen Zwei           
 *              Fernsehfilm D/2005 UT  318
 *
 * 10:00 WORLD NEWS                              CNNi (week overview)
 * News bulletins on the hour (...)
 * 10:30 WORLD SPORT                      
 */
bool T_OV_LINE_FMT::parse_title_line(const string& text, int& hour, int& min, bool &is_tip) const
{
   match_results<string::const_iterator> whati;
   bool result = false;

   if (m_vps_off >= 0) {
      // TODO: Phoenix wraps titles into 2nd line, appends subtitle with "-"
      // m#^ {0,2}(\d\d)[\.\:](\d\d)( {1,3}(\d{4}))? +#
      static const regex expr2("^ *([\\*!] +)?$");
      if (regex_search(text.begin(), text.begin() + m_time_off, whati, expr2)) {
         is_tip = whati[1].matched;
         // note: VPS time has already been extracted above, so the area is blank
         static const regex expr3("^(2[0-3]|[01][0-9])([\\.:])([0-5][0-9]) +[^ ]$");
         if (   regex_search(text.begin() + m_time_off,
                             text.begin() + m_title_off + 1,
                             whati, expr3)
             && (whati[2].first[0] == m_separator) ) {
            hour = atoi_substr(whati[1]);
            min = atoi_substr(whati[3]);
            result = true;
         }
      }
   }
   else {
      // m#^( {0,5}| {0,3}\! {1,3})(\d\d)[\.\:](\d\d) +#
      static const regex expr5("^ *([\\*!] +)?$");
      if (regex_search(text.begin(), text.begin() + m_time_off, whati, expr5)) {
         is_tip = whati[1].matched;
         static const regex expr6("^(2[0-3]|[01][0-9])([\\.:])([0-5][0-9]) +[^ ]$");
         if (   regex_search(text.begin() + m_time_off,
                             text.begin() + m_title_off + 1,
                             whati, expr6)
             && (whati[2].first[0] == m_separator) ) {
            hour = atoi_substr(whati[1]);
            min = atoi_substr(whati[3]);
            result = true;
         }
      }
   }
   return result;
}

bool T_OV_LINE_FMT::parse_subtitle(const string& text) const
{
   bool result = false;

   if (m_subt_off >= 0) {
      static const regex expr10("^[ \\x00-\\x07\\x10-\\x17]*$");
      static const regex expr11("^[ \\x00-\\x07\\x10-\\x17]*$");
      match_results<string::const_iterator> whati;

      if (   !regex_search(text.begin(), text.begin() + m_subt_off, whati, expr10)
          || regex_search(text.begin() + m_subt_off, text.end(), whati, expr11) )
      {
      }
      else {
         result = true;
      }
   }
   return result;
}

string T_OV_LINE_FMT::extract_title(const string& text) const
{
   return text.substr(m_title_off);
}

string T_OV_LINE_FMT::extract_subtitle(const string& text) const
{
   return text.substr(m_subt_off);
}

bool T_OV_LINE_FMT::detect_line_fmt(const string& text, const string& text2)
{
   smatch whats;

   // look for a line containing a start time (hour 0-23 : minute 0-59)
   // TODO allow start-stop times "10:00-11:00"?
   static const regex expr1("^( *| *! +)([01][0-9]|2[0-3])([\\.:])([0-5][0-9]) +");
   if (regex_search(text, whats, expr1)) {
      int off = whats[0].length();

      m_time_off = whats[1].length();
      m_vps_off = -1;
      m_title_off = off;
      m_subt_off = -1;
      m_separator = whats[3].first[0];
      // TODO require that times are increasing (within the same format)
      //m_mod = atoi_substr(whats[2]) * 60 + atoi_substr(whats[4]);

      // look for a VPS label on the same line after the human readable start time
      // TODO VPS must be magenta or concealed
      static const regex expr2("^([0-2][0-9][0-5][0-9] +)");
      if (regex_search(text.begin() + off, text.end(), whats, expr2)) {
         m_vps_off = off;
         m_title_off = off + whats[1].length();
      }
      else {
         m_vps_off = -1;
         m_title_off = off;
      }

      // measure the indentation of the following line, if starting with a letter (2nd title line)
      static const regex expr3("^( *| *([01][0-9]|2[0-3])[0-5][0-9] +)[[:alpha:]]");
      static const regex expr4("^( *)[[:alpha:]]");
      if ( (m_vps_off == -1)
           ? regex_search(text2, whats, expr3)
           : regex_search(text2, whats, expr4) )
      {
         m_subt_off = whats[1].second - whats[1].first;
      }

      //if (opt_debug) printf("FMT: %s\n", print_key());
      return true;
   }
   return false;
}

T_OV_LINE_FMT T_OV_LINE_FMT::select_ov_fmt(vector<T_OV_LINE_FMT>& fmt_list)
{
   if (!fmt_list.empty()) {
      map<T_OV_LINE_FMT,int> fmt_stats;
      int max_cnt = 0;
      int max_idx = -1;

      // search the most used format (ignoring "subt_off")
      for (unsigned idx = 0; idx < fmt_list.size(); idx++) {
         map<T_OV_LINE_FMT,int>::iterator p = fmt_stats.lower_bound(fmt_list[idx]);
         if ((p == fmt_stats.end()) || (fmt_list[idx] < p->first)) {
            p = fmt_stats.insert(p, make_pair(fmt_list[idx], 1));
         }
         else {
            p->second += 1;
         }
         if (p->second > max_cnt) {
            max_cnt = fmt_stats[fmt_list[idx]];
            max_idx = idx;
         }
      }
      T_OV_LINE_FMT& fmt = fmt_list[max_idx];

      // search the most used "subt_off" among the most used format
      map<int,int> fmt_subt;
      max_cnt = 0;
      for (unsigned idx = 0; idx < fmt_list.size(); idx++) {
         if (   (fmt_list[idx] == fmt)
             && (fmt_list[idx].get_subt_off() != -1))
         {
            int subt_off = fmt_list[idx].get_subt_off();
            map<int,int>::iterator p = fmt_subt.lower_bound(subt_off);
            if ((p == fmt_subt.end()) || (subt_off < p->first)) {
               p = fmt_subt.insert(p, make_pair(subt_off, 1));
            }
            else {
               p->second += 1;
            }
            if (p->second > max_cnt) {
               max_cnt = p->second;
               fmt.set_subt_off( fmt_list[idx] );
            }
         }
      }

      if (opt_debug) printf("auto-detected overview format: %s\n", fmt.print_key());
      return fmt;
   }
   else {
      return T_OV_LINE_FMT();
   }
}

/* ------------------------------------------------------------------------------
 *  Determine layout of programme overview pages
 *  (to allow stricter parsing)
 *  - TODO: detect color used to mark features (e.g. yellow on ARD) (WDR: ttx ref same color as title)
 *  - TODO: detect color used to distinguish title from subtitle/category (WDR,Tele5)
 */
T_OV_LINE_FMT DetectOvFormat(TTX_PAGE_DB * db, int ov_start, int ov_end)
{
   vector<T_OV_LINE_FMT> fmt_list;
   T_OV_LINE_FMT fmt;

   // look at the first 5 pages (default start at page 301)
   int cnt = 0;
   for (TTX_PAGE_DB::const_iterator p = db->begin(); p != db->end(); p++)
   {
      int page = p->first.page();
      int sub = p->first.sub();
      if ((page >= ov_start) && (page <= ov_end)) {
         const TTX_DB_PAGE * pgtext = db->get_sub_page(page, sub);

         for (int line = 5; line <= 21; line++) {
            const string& text = pgtext->get_text(line);
            const string& text2 = pgtext->get_text(line + 1);

            if (fmt.detect_line_fmt(text, text2)) {
               fmt_list.push_back(fmt);
            }
         }

         if (++cnt > 5)
            break;
      }
   }

   if (fmt_list.empty()) {
      if (cnt == 0)
         fprintf(stderr, "No pages found in range %03X-%03X\n", ov_start, ov_end);
      else
         fprintf(stderr, "Failed to detect overview format on pages %03X-%03X\n", ov_start, ov_end);
   }

   return T_OV_LINE_FMT::select_ov_fmt(fmt_list);
}

