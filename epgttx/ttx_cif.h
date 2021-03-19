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
 * Copyright 2006-2011,2020-2021 by T. Zoerner (tomzo at users.sf.net)
 */
#if !defined (__TTX_IF_H)
#define __TTX_IF_H

#if defined (__cplusplus)
extern "C" {
#endif

void * ttx_db_create( void );
void ttx_db_destroy( void * db );
void ttx_db_add_cni(void * db, unsigned cni);
void ttx_db_add_pkg( void * db, int page, int ctrl, int pkgno, const uint8_t * p_data, time_t ts );
int ttx_db_parse( void * db, int pg_start, int pg_end, int expire_min,
                  const char * p_xml_in, const char * p_xml_out,
                  const char * p_ch_name, const char * p_ch_id );
void ttx_db_dump(void * db, const char * p_name, int pg_start, int pg_end);

#if defined (__cplusplus)
}
#endif

#endif // __TTX_IF_H
