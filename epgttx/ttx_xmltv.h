/*
 * Teletext EPG grabber: XMLTV import, merge and export
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
#if !defined (__TTX_XMLTV_H)
#define __TTX_XMLTV_H

#include "ttx_scrape.h"

class XMLTV
{
public:
   XMLTV(TTX_DB * db)
      : mp_db(db)
      , m_expire_min(-1) {}
   bool ImportXmltvFile(const char * fname);
   bool ExportXmltv(list<TV_SLOT>& NewSlots, const char * p_file_name,
                    const char * p_my_ver, const char * p_my_url);
   void SetChannelName(const char * user_chname, const char * user_chid);
   void SetExpireTime(int expire_min);
private:
   TTX_DB * const mp_db;
   int m_expire_min;
   map<string,string> m_merge_prog;
   map<string,string> m_merge_chn;
   string m_ch_name;
   string m_ch_id;
};

#endif // __TTX_XMLTV_H
