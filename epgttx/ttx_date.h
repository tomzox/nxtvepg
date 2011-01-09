/*
 * Teletext EPG grabber: date parsing
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
 * $Id: ttx_date.h,v 1.2 2011/01/06 16:59:34 tom Exp tom $
 */
#if !defined (__TTX_DATE_H)
#define __TTX_DATE_H

class T_PG_DATE
{
public:
   T_PG_DATE() {
      m_mday = -1;
      m_month = -1;
      m_year = -1;
      m_date_off = 0;
   };
   T_PG_DATE& operator=(const T_PG_DATE& src) {
      m_mday = src.m_mday;
      m_month = src.m_month;
      m_year = src.m_year;
      m_date_off = src.m_date_off;
      return *this;
   };
   bool is_valid() const { return m_year != -1; }
   void add_offset(int date_off) { m_date_off += date_off; }
   const char * trace_str() const;

   bool ParseOvDate(int page, int sub, int head);
   bool ParseDescDate(int page, int sub, time_t ov_start_t, int date_off) const;

   time_t convert_time(int hour, int min, int date_off) const;
   int calc_date_delta(const T_PG_DATE& slot2) const;
private:
   int m_mday;
   int m_month;
   int m_year;
   int m_date_off;
};

string ParseChannelName();

#endif // __TTX_DATE_H
