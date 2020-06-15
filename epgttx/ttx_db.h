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
#if !defined (__TTX_DB_H)
#define __TTX_DB_H

#include <assert.h>
#include <map>

#define VT_PKG_RAW_LEN 40
class TTX_PG_HANDLE
{
public:
   TTX_PG_HANDLE(unsigned page, unsigned sub) : m_handle((page << 16) | (sub & 0x3f7f)) {}
   bool operator< (TTX_PG_HANDLE v) const { return m_handle < v.m_handle; }
   bool operator== (TTX_PG_HANDLE v) const { return m_handle == v.m_handle; }
   bool operator!= (TTX_PG_HANDLE v) const { return m_handle != v.m_handle; }
   unsigned page() const { return (m_handle >> 16); }
   unsigned sub() const { return (m_handle & 0x3f7f); }
private:
   unsigned m_handle;
};

class TTX_DB_PAGE
{
public:
   TTX_DB_PAGE(unsigned page, unsigned sub, unsigned ctrl, time_t ts);
   void add_raw_pkg(unsigned idx, const uint8_t * p_data);
   void inc_acq_cnt(time_t ts);
   void erase_page_c4();
   const string& get_ctrl(unsigned line) const {
      assert(line < TTX_TEXT_LINE_CNT);
      if (!m_text_valid)
         page_to_latin1();
      return m_ctrl[line];
   }
   const string& get_text(unsigned line) const {
      assert(line < TTX_TEXT_LINE_CNT);
      if (!m_text_valid)
         page_to_latin1();
      return m_text[line];
   }
   int get_lang() const { return m_lang; }
   int get_acq_rep() const { return m_acq_rep_cnt; }
   time_t get_timestamp() const { return m_timestamp; }
   void dump_page_as_text(FILE * fp);
   void dump_page_as_raw(FILE * fp, int last_sub);

   typedef uint8_t TTX_RAW_PKG[VT_PKG_RAW_LEN];
   static const unsigned TTX_TEXT_LINE_CNT = 24;
   static const unsigned TTX_RAW_PKG_CNT = 30;

private:
   void page_to_latin1() const;
   void line_to_latin1(unsigned line, string& out) const;

   TTX_RAW_PKG  m_raw_pkg[TTX_RAW_PKG_CNT];
   uint32_t     m_raw_pkg_valid;
   int          m_acq_rep_cnt;
   int          m_lang;
   int          m_page;
   int          m_sub;
   time_t       m_timestamp;
   
   mutable bool   m_text_valid;
   mutable string m_ctrl[TTX_TEXT_LINE_CNT];
   mutable string m_text[TTX_TEXT_LINE_CNT];
};

class TTX_DB_BTT
{
public:
   TTX_DB_BTT() : m_have_btt(false), m_have_mpt(false), m_have_ait(false) {}
   void add_btt_pkg(unsigned page, unsigned idx, const uint8_t * p_data);
   int get_last_sub(unsigned page) const;
   bool is_valid() const { return m_have_btt; }
   bool is_top_page(unsigned page) const;
   void dump_btt_as_text(FILE * fp);
   void flush();
private:
   struct BTT_AIT_ELEM {
      uint8_t      hd_text[13];
      uint16_t     page;
   };
private:
   bool         m_have_btt;
   bool         m_have_mpt;
   bool         m_have_ait;
   uint8_t      m_pg_func[900 - 100];  ///< Page function - see 9.4.2.1
   uint8_t      m_sub_cnt[900 - 100];
   uint16_t     m_link[3 * 5];
   BTT_AIT_ELEM m_ait[44];
};

class TTX_DB
{
public:
   ~TTX_DB();
   typedef std::map<TTX_PG_HANDLE, TTX_DB_PAGE*>::iterator iterator;
   typedef std::map<TTX_PG_HANDLE, TTX_DB_PAGE*>::const_iterator const_iterator;

   bool sub_page_exists(unsigned page, unsigned sub) const;
   const TTX_DB_PAGE* get_sub_page(unsigned page, unsigned sub) const;
   const_iterator begin() const { return m_db.begin(); }
   const_iterator end() const { return m_db.end(); }
   const_iterator first_sub_page(unsigned page) const;
   const_iterator& next_sub_page(unsigned page, const_iterator& p) const;
   int last_sub_page_no(unsigned page) const;
   int get_sub_page_cnt(unsigned page) const;

   TTX_DB_PAGE* add_page(unsigned page, unsigned sub, unsigned ctrl, const uint8_t * p_data, time_t ts);
   void add_page_data(unsigned page, unsigned sub, unsigned idx, const uint8_t * p_data);
   void erase_page_c4(int page, int sub);
   void dump_db_as_text(FILE * fp);
   void dump_db_as_raw(FILE * fp, int pg_start, int pg_end);
   double get_acq_rep_stats();
   bool page_acceptable(unsigned page) const;
   void flush();
private:
   std::map<TTX_PG_HANDLE, TTX_DB_PAGE*> m_db;
   TTX_DB_BTT m_btt;
};

class TTX_CHN_ID
{
public:
   typedef std::map<int,int>::iterator iterator;
   typedef std::map<int,int>::const_iterator const_iterator;

   void add_cni(unsigned cni);
   void dump_as_raw(FILE * fp);
   void flush();
   string get_ch_id();
private:
   struct T_CNI_TO_ID_MAP
   {
      uint16_t cni;
      const char * const p_name;
   };
   std::map<int,int> m_cnis;

   static const T_CNI_TO_ID_MAP Cni2ChannelId[];
   static const uint16_t NiToPdcCni[];
};

// global data
extern TTX_DB ttx_db;
extern TTX_CHN_ID ttx_chn_id;

bool ImportRawDump(const char * p_name);
void DumpTextPages(const char * p_name);
void DumpRawTeletext(const char * p_name, int pg_start, int pg_end);

#endif // __TTX_DB_H
