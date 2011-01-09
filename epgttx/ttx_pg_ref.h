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
 * Copyright 2006-2011 by Tom Zoerner (tomzo at users.sf.net)
 *
 * $Id: ttx_pg_ref.h,v 1.2 2011/01/06 16:59:34 tom Exp tom $
 */
#if !defined (__TTX_PG_REF_H)
#define __TTX_PG_REF_H

class T_TRAIL_REF_FMT
{
public:
   T_TRAIL_REF_FMT() : m_spc_trail(-1) {}
   bool operator<(const T_TRAIL_REF_FMT& b) const {
      return    (m_ch1 < b.m_ch1)
             || (   (m_ch1 == b.m_ch1)
                 && (   (m_ch2 < b.m_ch2)
                     || (   (m_ch2 == b.m_ch2)
                         && (   (m_spc_lead < b.m_spc_lead)
                             || (   (m_spc_lead == b.m_spc_lead)
                                 && (m_spc_trail < b.m_spc_trail) )))));
   }
   const char * print_key() const;
   bool detect_ref_fmt(const string& text);
   bool parse_trailing_ttx_ref(string& text, int& ttx_ref) const;
   bool is_valid() const { return m_spc_trail >= 0; }
   static T_TRAIL_REF_FMT select_ttx_ref_fmt(const vector<T_TRAIL_REF_FMT>& fmt_list);
private:
   void init_expr() const;
public:
   char m_ch1;          ///< Leading separator (e.g. "..... 314"), or zero
   char m_ch2;          ///< Second terminator char (e.g. "...>314"), or zero
   int m_spc_lead;      ///< Number of leading spaces
   int m_spc_trail;     ///< Number of spaces before line end

   mutable regex m_expr;  ///< Regex used for extracting the match
   mutable int m_subexp_idx;  ///< Index of TTX page sub-expression in match result
};

#endif // __TTX_PG_REF_H
