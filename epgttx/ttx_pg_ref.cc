/*
 * Teletext EPG grabber: Description page reference parser
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
 * Copyright 2006-2011,2020-2021 by T. Zoerner (tomzo at users.sf.net)
 */

#include <stdio.h>
#include <string.h>

#include <sstream>
#include <string>
#include <map>
#include <regex>

using namespace std;

#include "ttx_util.h"
#include "ttx_pg_ref.h"

/* ------------------------------------------------------------------------------
 * Detect position and leading garbage for description page references
 * - The main goal is to find the exact position relative to the line end
 * - When that is fixed, we can be flexible about leading separators, i.e.
 *   swallow when present, else require a single blank only.
 *
 * ARD:  "Die Sendung mit der\x03UT\x06... 313""
 * ZDF:  "ZDF SPORTreportage ...\x06321"
 * SAT1: "Navy CIS: Das Boot        >353 "
 * RTL:  "Die Camper..............>389 "
 * RTL2: "X-Factor: Das Unfassbare...328 "
 * Kab1: "X-Men\x03                       >375"
 */
const char * T_TRAIL_REF_FMT::print_key() const
{
   static char buf[100];
   sprintf(buf, "ch1:%c,ch2:%c,spc1:%d,spc2:%d",
           (m_ch1 != 0)?m_ch1:'X', (m_ch2 != 0)?m_ch2:'X', m_spc_lead, m_spc_trail);
   return buf;
}

bool T_TRAIL_REF_FMT::detect_ref_fmt(const string& text)
{
   smatch whats;

   static const regex expr1("((\\.+|>+)(>{1,4})?)?([ \\x00-\\x07\\x1D]{0,2})"
                            "[1-8][0-9][0-9]([ \\x00-\\x07\\x1D]{0,3})$");
   if (   regex_search(text, whats, expr1)
       && (whats[1].matched || (whats[4].length() > 0)) )
   {
      if (whats[1].matched) {
         m_ch1 = whats[2].first[0];
         m_ch2 = whats[3].matched ? whats[3].first[0] : 0;
         m_spc_lead = whats[4].length();
      }
      else {
         m_ch1 = 0;
         m_ch2 = 0;
         m_spc_lead = 1;
      }
      m_spc_trail = whats[5].length();

      //if (opt_debug) printf("FMT: %s\n", print_key());
      return true;
   }
   return false;
}

void T_TRAIL_REF_FMT::init_expr() const
{
   ostringstream re;
   if (m_ch1 != 0) {
      m_subexp_idx = 2;

      if (is_regex_special(m_ch1))
         re << "(\\" << m_ch1 << "*";  // add escape if char is "special" to regex
      else
         re << "(" << m_ch1 << "*";

      if (m_ch2 != 0) {
         if (is_regex_special(m_ch2))
            re << "\\" << m_ch2 << "{0,4}";
         else
            re << m_ch2 << "{0,4}";
      }
      re << "[ \\x00-\\x07\\x1D]{" << m_spc_lead << "}|[ \\x00-\\x07\\x1D]+)";
   }
   else {
      re << "[ \\x00-\\x07\\x1D]+";
      m_subexp_idx = 1;
   }
   re << "([1-8][0-9][0-9])[ \\x00-\\x07\\x1D]{" << m_spc_trail << "}$";

   //printf("TTX REF expr '%s'\n", re.str().c_str());

   m_expr.assign(re.str());
}

/* ------------------------------------------------------------------------------
 * Extract references to teletext pages with content description
 *   unfortunately these references come in a great varity of formats, so we
 *   must mostly rely on the teletext number (risking to strip off 3-digit
 *   numbers at the end of titles in rare cases)
 */
bool T_TRAIL_REF_FMT::parse_trailing_ttx_ref(string& title, int& ttx_ref) const
{
   smatch whats;

   if (is_valid()) {
      if (!m_is_initialized) {
         m_is_initialized = true;
         init_expr();
      }
      if (regex_search(title, whats, m_expr)) {
         string::const_iterator p = whats[m_subexp_idx].first;

         ttx_ref = ((p[0] - '0')<<8) |
                   ((p[1] - '0')<<4) |
                   (p[2] - '0');

         if (opt_debug) printf("TTX_REF %03X on title '%s'\n", ttx_ref, title.c_str());

         // warning: must be done last - invalidates "whats"
         title.erase(title.length() - whats[0].length());
         return true;
      }
   }
   return false;
}

T_TRAIL_REF_FMT T_TRAIL_REF_FMT::select_ttx_ref_fmt(const vector<T_TRAIL_REF_FMT>& fmt_list)
{
   map<T_TRAIL_REF_FMT,int> fmt_stats;
   int max_cnt = 0;
   int max_idx = -1;

   if (!fmt_list.empty()) {
      for (unsigned idx = 0; idx < fmt_list.size(); idx++) {
         // count the number of occurrences of the same format in the list
         map<T_TRAIL_REF_FMT,int>::iterator p = fmt_stats.lower_bound(fmt_list[idx]);
         if ((p == fmt_stats.end()) || (fmt_list[idx] < p->first)) {
            p = fmt_stats.insert(p, make_pair(fmt_list[idx], 1));
         }
         else {
            p->second += 1;
         }
         // track the most frequently used format
         if (p->second > max_cnt) {
            max_cnt = fmt_stats[fmt_list[idx]];
            max_idx = idx;
         }
      }
      if (opt_debug) printf("auto-detected TTX reference format: %s\n", fmt_list[max_idx].print_key());

      return fmt_list[max_idx];
   }
   else {
      if (opt_debug) printf("no TTX references found for format auto-detection\n");
      return T_TRAIL_REF_FMT();
   }
}

