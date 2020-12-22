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
#if !defined (__TTX_OV_FMT_H)
#define __TTX_OV_FMT_H

#include <vector>

class T_OV_LINE_FMT
{
public:
   T_OV_LINE_FMT() : m_time_off(-1), m_vps_off(-1), m_title_off(-1), m_subt_off(-1) {}
   bool operator==(const T_OV_LINE_FMT& b) const {
      return (   (m_time_off == b.m_time_off)
              && (m_vps_off == b.m_vps_off)
              && (m_title_off == b.m_title_off)
              && (m_separator == b.m_separator));
   }
   bool operator<(const T_OV_LINE_FMT& b) const {
      return    (m_time_off < b.m_time_off)
             || (   (m_time_off == b.m_time_off)
                 && (   (m_vps_off < b.m_vps_off)
                     || (   (m_vps_off == b.m_vps_off)
                         && (   (m_title_off < b.m_title_off)
                             || (   (m_title_off == b.m_title_off)
                                 && (m_separator < b.m_separator) )))));
   }
   const char * print_key() const;
   bool detect_line_fmt(const string& text, const string& text2);
   bool parse_title_line(const string& text, int& hour, int& min, bool &is_tip) const;
   bool parse_subtitle(const string& text) const;
   string extract_title(const string& text) const;
   string extract_subtitle(const string& text) const;
   bool is_valid() const { return m_time_off >= 0; }
   int get_subt_off() const { return m_subt_off; }
   void set_subt_off(const T_OV_LINE_FMT& v) { m_subt_off = v.m_subt_off; }
   static T_OV_LINE_FMT select_ov_fmt(std::vector<T_OV_LINE_FMT>& fmt_list);
private:
   int m_time_off;    ///< Offset to HH:MM or HH.MM
   int m_vps_off;     ///< Offset to HHMM (concealed VPS)
   int m_title_off;   ///< Offset to title
   int m_subt_off;    ///< Offset to 2nd title line
   char m_separator;  ///< HH:MM separator character
};

T_OV_LINE_FMT DetectOvFormat(TTX_PAGE_DB * db, int ov_start, int ov_end);

#endif // __TTX_OV_FMT_H
